# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# CarPlay -> BMW HUD/KOMBI: one-command "kreni". Run this once the car is connected.
#
#   powershell -ExecutionPolicy Bypass -File tools\go.ps1            # pre-flight only (safe, no writes)
#   powershell -ExecutionPolicy Bypass -File tools\go.ps1 -Go        # pre-flight + install (the real write)
#
# Pre-flight (always, read-only):
#   1. both HU boards reachable over SSH
#   2. the hook .so exists and is a QNX ARM shared object
#   3. the on-car ipod-drvr-iap2.so STILL matches the binary we reverse-engineered (else a dealer update moved
#      the offsets -> abort, re-run P0/RE). mm-ipod is matched too, to tell stock from already-installed.
# With -Go it then runs the install (backup + wrapper), all reversible with rollback_hook.ps1.
param([switch]$Go)
$ErrorActionPreference = 'Stop'
$proj = Split-Path -Parent $PSScriptRoot
$hu   = Join-Path $PSScriptRoot 'hu.ps1'
if (-not (Test-Path $hu)) { $hu = Join-Path (Split-Path -Parent $proj) 'tools\hu.ps1' }
$so   = Join-Path $proj 'build\libnbt_carplay_hud_real.so'
$baseFile = Join-Path $proj 'p0\baseline_hashes.txt'
$tmp = Join-Path $env:TEMP ('cphud_' + [Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Ok($s)   { Write-Host "  [ok]   $s" -ForegroundColor Green }
function Bad($s)  { Write-Host "  [STOP] $s" -ForegroundColor Red }
function Info($s) { Write-Host $s }

# baseline hashes
$base = @{}
Get-Content $baseFile | Where-Object { $_ -match '^[0-9A-Fa-f]{64}\s' } | ForEach-Object {
    $p = $_ -split '\s+'; $base[$p[1]] = $p[0].ToUpper()
}

Info "CarPlay->HUD  pre-flight  (Go=$Go)"
$fail = 0

# 1. reachability
foreach ($ip in '169.254.199.119','169.254.199.99') {
    $r = Test-NetConnection -ComputerName $ip -Port 22 -WarningAction SilentlyContinue -InformationLevel Quiet
    if ($r) { Ok "SSH $ip" } else { Bad "SSH $ip unreachable (ignition on? ENET cable? NIC 169.254.0.10/16?)"; $fail = 1 }
}
if ($fail) { Bad "car not reachable - connect it and retry"; return }

# 2. the hook .so
if (-not (Test-Path $so)) { Bad "hook .so missing: $so  (build: tools\qnx_build_hook.sh)"; return }
$bytes = [IO.File]::ReadAllBytes($so)
if ($bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and $bytes[4] -eq 1 -and $bytes[18] -eq 0x28) {
    Ok ("hook .so is ELF32 ARM ({0:N0} bytes, sha256 {1}...)" -f $bytes.Length, (Get-FileHash $so -Algorithm SHA256).Hash.Substring(0,12))
} else { Bad "hook .so is not an ELF32 ARM object"; return }

# 3. on-car binaries vs the RE baseline
function PullHash($path, $name) {
    $out = Join-Path $tmp $name
    & $hu ("cat " + $path) -OutFile $out 2>$null | Out-Null
    if (-not (Test-Path $out) -or (Get-Item $out).Length -eq 0) { return $null }
    return (Get-FileHash $out -Algorithm SHA256).Hash.ToUpper()
}
$drv = PullHash '/opt/conn/lib/carplay/ipod-drvr-iap2.so' 'drv'
if (-not $drv) { Bad "could not read ipod-drvr-iap2.so"; return }
if ($drv -eq $base['ipod-drvr-iap2.so']) { Ok "iap2 driver matches the reverse-engineered baseline" }
else { Bad "iap2 driver CHANGED (dealer update?) - offsets invalid, re-run P0/RE before installing"; $fail = 1 }

$mm = PullHash '/opt/conn/bin/carplay/mm-ipod' 'mm'
$realExists = (& $hu 'test -f /opt/conn/bin/carplay/mm-ipod.real && echo yes || echo no' 2>$null | Select-Object -Last 1)
if ($mm -eq $base['mm-ipod']) {
    Ok "mm-ipod is stock (ready to install)"
    $state = 'stock'
} elseif ($realExists -match 'yes') {
    Ok "mm-ipod.real present -> hook already installed"
    $state = 'installed'
} else {
    Bad "mm-ipod is neither stock nor our wrapper - unknown state, not touching it"
    $state = 'unknown'; $fail = 1
}

Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Info ""
if ($fail) { Bad "pre-flight FAILED - not installing"; return }
Ok "pre-flight PASSED"

if ($state -eq 'installed') {
    Info "`nAlready installed. If CarPlay HUD isn't working, check logs or run rollback_hook.ps1 -Apply."
    return
}
if (-not $Go) {
    Info "`nPre-flight only. To install now, re-run:  tools\go.ps1 -Go"
    return
}

# install (the write) - reuses the audited install_hook.ps1
Info "`n=== installing (backup + wrapper) ==="
& (Join-Path $PSScriptRoot 'install_hook.ps1') -Apply -So $so
Info "`n=== NEXT: reboot the HU (hold the knob ~30 s), then connect the iPhone and start a route ==="
Info "    Apple Maps / Google Maps / Waze 5.4.0+  -> arrow on the HUD + cluster."
Info "    Anything wrong? tools\rollback_hook.ps1 -Apply  (then reboot). SSH always survives."
