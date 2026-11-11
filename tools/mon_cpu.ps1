<#
.SYNOPSIS
  Measures CPU load on the NBT Evo head unit while you run a CarPlay test, so we can tell whether the
  route-guidance hook spikes CPU (the watchdog HU reset coincided with procnto at ~87%).

.DESCRIPTION
  Runs QNX `hogs` over SSH for -Seconds, streaming a per-interval CPU% snapshot of every busy process into
  p0\cpu_<label>_<timestamp>.raw, then parses it and prints peak/avg CPU for:
    - the hooked CarPlay daemon  (process name contains "mm-ipod"; the wrapper execs mm-ipod.real)
    - procnto                    (the microkernel; this is what hit 87% before the reset)
    - the whole system           (sum of all busy processes per interval)
  A verdict flags whether anything approached the danger zone.

  Scientific use: run it ONCE on STOCK (before go.ps1 -Go) during CarPlay nav with -Label stock_<app>, then
  again with the hook installed as -Label hook_<app>, and pass the stock file to -Compare to see the delta
  attributable to the modification.

  `hogs` is a standard QNX Neutrino utility and prints integer percentages. If it is somehow absent on this
  unit, pass -UsePidin to fall back to `pidin times` cumulative-CPU deltas (coarser, but always present).

.EXAMPLE
  # 1) Baseline (car still stock), start Apple Maps navigating first, then:
  .\tools\mon_cpu.ps1 -Seconds 180 -Label stock_apple
  # 2) After go.ps1 -Go + reboot, Apple Maps navigating:
  .\tools\mon_cpu.ps1 -Seconds 180 -Label hook_apple -Compare .\p0\cpu_stock_apple_*.raw
#>
[CmdletBinding()]
param(
    [int]$Seconds = 180,
    [Parameter(Mandatory = $true)][string]$Label,
    [ValidateSet('omap', 'jacinto')][string]$Board = 'omap',
    [int]$Interval = 2,
    [switch]$UsePidin,
    [string]$Compare,
    [int]$SpikeProcnto = 70,   # procnto% at/above this = danger (reset dump showed 87%)
    [int]$SpikeMmipod = 20,    # our daemon% at/above this = our mod is heavy
    [int]$SpikeSystem = 90     # total busy% at/above this = system saturated
)

$ErrorActionPreference = 'Stop'
$proj = Split-Path -Parent $PSScriptRoot
$p0 = Join-Path $proj 'p0'
New-Item -ItemType Directory -Force -Path $p0 | Out-Null
$hu = Join-Path (Split-Path -Parent $proj) 'tools\hu.ps1'
if (-not (Test-Path $hu)) { $hu = Join-Path $proj 'tools\hu.ps1' }   # fallback if layout differs
if (-not (Test-Path $hu)) { throw "hu.ps1 not found (looked in repo-root tools\ and project tools\)" }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$raw = Join-Path $p0 ("cpu_{0}_{1}.raw" -f ($Label -replace '[^A-Za-z0-9_]', '_'), $stamp)

# ---- the remote sampler -------------------------------------------------------------------------------------
# hogs -i N self-loops, emitting one block of busy processes every N seconds. We block in plink for the test
# duration; hu.ps1 kills plink at -TimeoutSec, which ends hogs, leaving the partial capture in $raw. Each block
# is N seconds apart, so block index * N approximates wall time from the start marker below.
if ($UsePidin) {
    # Cumulative CPU per process; host diffs consecutive samples. `date +%s` is best-effort (ignored if absent).
    $n = [int][math]::Ceiling($Seconds / [double]$Interval)
    $remote = "i=0; while [ `$i -lt $n ]; do printf '=== SAMPLE t=%s\n' `"`$(date +%s 2>/dev/null)`"; pidin -F '%J %N' times; sleep $Interval; i=`$((i+1)); done"
} else {
    # This QNX hogs build has no -i option and self-loops at its own cadence, printing a block of busy
    # processes repeatedly; blocking in plink for the test duration captures the series, and hu.ps1's
    # -TimeoutSec ends it. The parser splits blocks on reprinted headers / blank lines.
    $remote = "hogs"
}

Write-Host "=== CPU monitor: board=$Board tool=$(if($UsePidin){'pidin'}else{'hogs'}) interval=${Interval}s duration=${Seconds}s"
Write-Host "    output -> $raw"
Write-Host ""
Write-Host ">>> START YOUR TEST NOW (app must be ACTIVELY navigating). Monitoring for $Seconds s..." -ForegroundColor Yellow
$t0 = Get-Date
Write-Host "    t0 = $($t0.ToString('HH:mm:ss'))"

# hu.ps1 exits non-zero on the expected timeout; run it as a child process so its `exit` cannot end us.
$huArgs = @('-ExecutionPolicy', 'Bypass', '-File', $hu, '-Command', $remote, '-Board', $Board,
    '-OutFile', $raw, '-TimeoutSec', ($Seconds + 8))
& powershell @huArgs 2>&1 | ForEach-Object { }   # swallow hu.ps1's timeout notice on stderr
$t1 = Get-Date
Write-Host "    t1 = $($t1.ToString('HH:mm:ss'))  (elapsed $([int]($t1 - $t0).TotalSeconds)s)"

if (-not (Test-Path $raw) -or (Get-Item $raw).Length -eq 0) {
    Write-Host "No data captured. Is the car reachable and is `hogs` present? Try -UsePidin." -ForegroundColor Red
    exit 2
}

# ---- parse ---------------------------------------------------------------------------------------------------
function Parse-HogsFile([string]$path) {
    # Returns: list of blocks; each block = @{ mmipod=<%>; procnto=<%>; system=<%>; top=@(name,%)... }
    $lines = Get-Content -LiteralPath $path
    $blocks = New-Object System.Collections.ArrayList
    $cur = $null
    foreach ($ln in $lines) {
        # A new hogs iteration begins at a header line ("PID" + "Name") or a procnto/kernel line; detect header.
        if ($ln -match '^\s*PID\b' -and $ln -match '[Nn]ame') {
            if ($cur) { [void]$blocks.Add($cur) }
            $cur = @{ mmipod = 0.0; procnto = 0.0; system = 0.0; rows = @() }
            continue
        }
        # A blank line also closes an iteration (some hogs builds separate cycles with a blank line instead of
        # reprinting the header). Only close a block that actually collected rows.
        if ($ln -match '^\s*$') {
            if ($cur -and $cur.rows.Count) { [void]$blocks.Add($cur); $cur = $null }
            continue
        }
        if (-not $cur) { $cur = @{ mmipod = 0.0; procnto = 0.0; system = 0.0; rows = @() } }
        # Data line: leading PID (digits), a name token, then numbers; last number on the line is system%.
        if ($ln -match '^\s*\d+\s+(\S.*?)\s+[\d.]+') {
            $nums = [regex]::Matches($ln, '[\d.]+') | ForEach-Object { $_.Value }
            if ($nums.Count -ge 2) {
                $pct = [double]($nums[-1] -replace '%', '')
                # name = everything between the PID and the first numeric column
                $name = ''
                if ($ln -match '^\s*\d+\s+(.+?)\s+[\d.]') { $name = $Matches[1].Trim() }
                $cur.rows += , @($name, $pct)
                $cur.system += $pct
                if ($name -match 'mm-ipod') { $cur.mmipod += $pct }
                if ($name -match 'procnto') { $cur.procnto += $pct }
            }
        }
    }
    if ($cur -and $cur.rows.Count) { [void]$blocks.Add($cur) }
    return $blocks
}

function Summarize($blocks, [string]$name) {
    if (-not $blocks -or $blocks.Count -eq 0) { Write-Host "  ${name}: no parseable blocks"; return $null }
    $mm = $blocks | ForEach-Object { $_.mmipod }
    $pc = $blocks | ForEach-Object { $_.procnto }
    $sy = $blocks | ForEach-Object { $_.system }
    $r = [pscustomobject]@{
        blocks      = $blocks.Count
        mmipod_peak = ($mm | Measure-Object -Maximum).Maximum
        mmipod_avg  = [math]::Round(($mm | Measure-Object -Average).Average, 1)
        procnto_peak= ($pc | Measure-Object -Maximum).Maximum
        procnto_avg = [math]::Round(($pc | Measure-Object -Average).Average, 1)
        system_peak = ($sy | Measure-Object -Maximum).Maximum
        system_avg  = [math]::Round(($sy | Measure-Object -Average).Average, 1)
    }
    return $r
}

Write-Host ""
Write-Host "=== Analysis ($($raw | Split-Path -Leaf)) ==="
$blocks = Parse-HogsFile $raw
$s = Summarize $blocks $Label
if (-not $s) {
    Write-Host "Could not parse (unexpected hogs format). Raw file saved for manual review: $raw" -ForegroundColor Red
    Write-Host "First 25 lines:"; Get-Content $raw -TotalCount 25 | ForEach-Object { "  $_" }
    exit 3
}

"{0,-16}{1,10}{2,10}" -f 'metric', 'peak %', 'avg %' | Write-Host
"{0,-16}{1,10}{2,10}" -f 'mm-ipod (hook)', $s.mmipod_peak, $s.mmipod_avg | Write-Host
"{0,-16}{1,10}{2,10}" -f 'procnto', $s.procnto_peak, $s.procnto_avg | Write-Host
"{0,-16}{1,10}{2,10}" -f 'system total', $s.system_peak, $s.system_avg | Write-Host
Write-Host ("    ($($s.blocks) samples over ~$([int]($s.blocks*$Interval)) s)")

Write-Host ""
$verdict = @()
if ($s.procnto_peak -ge $SpikeProcnto) { $verdict += "PROCNTO SPIKE: $($s.procnto_peak)% >= $SpikeProcnto% (reset danger zone)" }
if ($s.mmipod_peak -ge $SpikeMmipod) { $verdict += "mm-ipod heavy: $($s.mmipod_peak)% >= $SpikeMmipod%" }
if ($s.system_peak -ge $SpikeSystem) { $verdict += "system saturated: $($s.system_peak)% >= $SpikeSystem%" }
if ($verdict.Count -eq 0) {
    Write-Host "VERDICT: no CPU spike. mm-ipod peak $($s.mmipod_peak)%, procnto peak $($s.procnto_peak)%, system peak $($s.system_peak)%. Acceptable." -ForegroundColor Green
} else {
    Write-Host "VERDICT: NOT acceptable -" -ForegroundColor Red
    $verdict | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
}

# ---- optional stock-vs-hook comparison ----------------------------------------------------------------------
if ($Compare) {
    $other = Get-Item $Compare -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $other) { Write-Host "`n-Compare: file not found ($Compare)"; }
    else {
        Write-Host "`n=== Compare vs $($other.Name) (baseline) ==="
        $b2 = Parse-HogsFile $other.FullName
        $s2 = Summarize $b2 'baseline'
        if ($s2) {
            "{0,-16}{1,12}{2,12}{3,10}" -f 'metric', 'this peak', 'base peak', 'delta' | Write-Host
            "{0,-16}{1,12}{2,12}{3,10}" -f 'mm-ipod', $s.mmipod_peak, $s2.mmipod_peak, ($s.mmipod_peak - $s2.mmipod_peak) | Write-Host
            "{0,-16}{1,12}{2,12}{3,10}" -f 'procnto', $s.procnto_peak, $s2.procnto_peak, ($s.procnto_peak - $s2.procnto_peak) | Write-Host
            "{0,-16}{1,12}{2,12}{3,10}" -f 'system', $s.system_peak, $s2.system_peak, ($s.system_peak - $s2.system_peak) | Write-Host
            Write-Host "    (delta = CPU% attributable to the hook)"
        }
    }
}
Write-Host "`nRaw capture kept at: $raw"
