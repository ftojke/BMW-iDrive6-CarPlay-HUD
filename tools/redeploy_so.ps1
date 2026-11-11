<#
.SYNOPSIS
  Fast redeploy of ONLY the hook .so (wrapper + cfg already installed by go.ps1 -Go). CarPlay must be OFF
  (a running mm-ipod holds the .so busy). Does not touch mm-ipod/mm-ipod.real/cfg, so the stock backup and
  rollback stay valid. Keeps the rw window minimal and verifies size + QNX-loadability before returning.

.EXAMPLE
  .\tools\redeploy_so.ps1            # push build\libnbt_carplay_hud_real.so to /opt/aa-cp
#>
[CmdletBinding()]
param(
    [ValidateSet('omap', 'jacinto')][string]$Board = 'omap',
    [string]$So
)
$ErrorActionPreference = 'Stop'
$proj = Split-Path -Parent $PSScriptRoot
if (-not $So) { $So = Join-Path $proj 'build\libnbt_carplay_hud_real.so' }
if (-not (Test-Path $So)) { throw "hook .so not found: $So" }
$hu = Join-Path (Split-Path -Parent $proj) 'tools\hu.ps1'
if (-not (Test-Path $hu)) { $hu = Join-Path $proj 'tools\hu.ps1' }
$len = (Get-Item $So).Length
function HU([string]$cmd, [string]$inFile) {
    $a = @('-ExecutionPolicy', 'Bypass', '-File', $hu, '-Command', $cmd, '-Board', $Board)
    if ($inFile) { $a += @('-InFile', $inFile) }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & powershell @a 2>&1 | Where-Object { "$_" -notmatch '(Saved|Sent) \d+ bytes|bytes (from|to)' } }
    finally { $ErrorActionPreference = $old }
}

Write-Host "=== redeploy .so ($len bytes) -> /opt/aa-cp/libnbt_carplay_hud.so  (board=$Board)"

# Guard: CarPlay must be off, else the running mm-ipod keeps the .so busy ("Resource busy").
$procs = (HU 'pidin | grep mm-ipod | grep -v grep') -join "`n"
if ($procs.Trim()) {
    Write-Host "mm-ipod is RUNNING -> turn CarPlay OFF first, then re-run:" -ForegroundColor Red
    Write-Host $procs
    exit 2
}

# Minimal rw window: remount, write, chmod, remount ro.
HU 'mount -uw /fs/sda0' | Out-Null
HU 'cat > /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so' $So | Out-Null
HU 'chmod 755 /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so' | Out-Null
HU 'mount -ur /fs/sda0' | Out-Null

# Verify size + that the QNX loader can actually map/relocate it (self-test via the wrapper's gate).
$ls = (HU 'ls -l /opt/aa-cp/libnbt_carplay_hud.so') -join "`n"
Write-Host $ls
$onlen = ([regex]::Match($ls, '\s(\d+)\s+\w+\s+\d+')).Groups[1].Value
if ("$onlen" -ne "$len") { Write-Host "SIZE MISMATCH: on-unit=$onlen expected=$len" -ForegroundColor Red; exit 3 }

$self = (HU 'LD_PRELOAD=/opt/aa-cp/libnbt_carplay_hud.so NBT_CARPLAY_SELFTEST=1 /bin/uname; echo rc=$?') -join "`n"
Write-Host $self
if ($self -match 'rc=0') {
    Write-Host "OK: .so redeployed and loads on QNX. Turn CarPlay ON to reload the hook (no HU reboot needed)." -ForegroundColor Green
} else {
    Write-Host "SELF-TEST FAILED: the loader rejected the .so. The wrapper will fall back to stock (safe), but the hook won't run." -ForegroundColor Red
    exit 4
}
