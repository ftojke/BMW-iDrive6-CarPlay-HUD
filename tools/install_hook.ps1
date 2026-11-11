# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# CarPlay -> BMW HUD/KOMBI: install the hook on the head unit over SSH. WRITE operation.
#
# DRY-RUN BY DEFAULT: prints every step and the exact on-HU commands, writes NOTHING. Add -Apply to execute,
# which the user must explicitly approve. Each write is backed up (on-HU .orig + pulled to the laptop) and is
# fully reversible with rollback_hook.ps1.
#
# What it does (only with -Apply):
#   1. Pull current /opt/conn/bin/carplay/mm-ipod off-unit (backup) + record its SHA-256.
#   2. Remount /fs/sda0 rw (QNX6 power-safe), create /opt/aa-cp/, copy the hook .so there (chmod 755).
#   3. Rename mm-ipod -> mm-ipod.real, install the shell wrapper as mm-ipod (chmod 755).
#   4. sync, remount /fs/sda0 ro, verify ro, verify hashes.
#   5. Reboot the HU (hold the power knob ~30 s) is done by the USER afterwards.
#
# Pre-req: car awake + on charger; HU reachable; tools\hu.ps1 working. The .so must be built first
# (tools\qnx_build_hook.sh -> /root/cp_hook_build/libnbt_carplay_hud_real.so, copied to .\build\ via -FetchSo).
param(
    [switch]$Apply,
    [switch]$FetchSo,                               # copy the freshly built .so out of WSL into .\build\
    [string]$So = "$PSScriptRoot\..\build\libnbt_carplay_hud_real.so"
)
$ErrorActionPreference = 'Stop'
$hu   = Join-Path $PSScriptRoot 'hu.ps1'
if (-not (Test-Path $hu)) { $hu = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'tools\hu.ps1' }
$proj = Split-Path -Parent $PSScriptRoot                        # carplay_hud
$wrapper = Join-Path $proj 'hook\mm-ipod-wrapper.sh'
$backupDir = Join-Path $proj ('p0\install_backup_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))

function Say($s) { Write-Host $s }
function HU($cmd) {
    if ($Apply) { & $hu $cmd } else { Say "  [DRY] hu> $cmd" }
}

if ($FetchSo) {
    New-Item -ItemType Directory -Force -Path (Split-Path $So) | Out-Null
    wsl.exe -d Ubuntu-24.04 -u root -- bash -c "cp /root/cp_hook_build/libnbt_carplay_hud_real.so '$((Resolve-Path (Split-Path $So)).Path -replace '\\','/' -replace 'C:','/mnt/c')/'" 2>&1 | Out-Null
}

if (-not (Test-Path $So)) { throw "hook .so not found: $So  (build it with tools\qnx_build_hook.sh, then -FetchSo)" }
if (-not (Test-Path $wrapper)) { throw "wrapper not found: $wrapper" }

Say ("CarPlay->HUD install  (Apply=$Apply)")
Say ("  hook .so : $So  ($((Get-Item $So).Length) bytes)")
Say ("  wrapper  : $wrapper")
Say ("  backups  : $backupDir")
Say ""

# 1. backup the stock mm-ipod off-unit
if ($Apply) {
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    & $hu 'cat /opt/conn/bin/carplay/mm-ipod' -OutFile (Join-Path $backupDir 'mm-ipod.orig') | Out-Null
    Say ("  backed up mm-ipod -> " + (Join-Path $backupDir 'mm-ipod.orig') + " (" + (Get-Item (Join-Path $backupDir 'mm-ipod.orig')).Length + " bytes)")
} else {
    Say "  [DRY] pull /opt/conn/bin/carplay/mm-ipod -> $backupDir\mm-ipod.orig"
}

# 2-3. remount rw, stage files, rename + wrapper
Say "`n--- on-HU write sequence (rw window kept minimal) ---"
HU 'mount -uw /fs/sda0'
HU 'mkdir -p /fs/sda0/opt/aa-cp'
if ($Apply) {
    & $hu 'cat > /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so' -InFile $So | Out-Null
    & $hu 'cat > /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh' -InFile $wrapper | Out-Null
} else {
    Say "  [DRY] upload $So -> /opt/aa-cp/libnbt_carplay_hud.so"
    Say "  [DRY] upload $wrapper -> /opt/aa-cp/mm-ipod-wrapper.sh"
}
HU 'chmod 755 /fs/sda0/opt/aa-cp/libnbt_carplay_hud.so /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh'
# rename stock aside and drop the wrapper in its place (atomic-ish: mv then cp)
HU 'test -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real || mv /fs/sda0/opt/conn/bin/carplay/mm-ipod /fs/sda0/opt/conn/bin/carplay/mm-ipod.real'
HU 'cp /fs/sda0/opt/aa-cp/mm-ipod-wrapper.sh /fs/sda0/opt/conn/bin/carplay/mm-ipod'
HU 'chmod 755 /fs/sda0/opt/conn/bin/carplay/mm-ipod'

# 3b. declare the route-guidance message IDs in the CarPlay Wi-Fi iAP2 config (separate rw partition).
#     Backup once on-unit as .orig (rollback restores it); append [ident] only if not already present.
$cfg = '/var/opt/conn/carplay/iap2_Wifi.cfg'
$frag = Join-Path $proj 'hook\iap2_ident.cfgfrag'
if ($Apply) {
    & $hu "cat $cfg" -OutFile (Join-Path $backupDir 'iap2_Wifi.cfg.orig') | Out-Null
    & $hu "test -f $cfg.orig || cp $cfg $cfg.orig" | Out-Null
    & $hu "grep -q '^\[ident\]' $cfg || cat >> $cfg" -InFile $frag | Out-Null
} else {
    Say "  [DRY] backup $cfg -> $cfg.orig (+ laptop), append [ident] sendmsgs/recvmsgs if missing"
}
HU "grep msgs $cfg"

# 4. sync + remount ro + verify
HU 'sync'
HU 'mount -ur /fs/sda0'
HU 'mount | grep sda0'
HU 'ls -l /opt/conn/bin/carplay/mm-ipod /opt/conn/bin/carplay/mm-ipod.real /opt/aa-cp/'

Say "`n--- hashes (verify after apply) ---"
HU 'cksum /opt/conn/bin/carplay/mm-ipod.real /opt/aa-cp/libnbt_carplay_hud.so'

Say ""
if ($Apply) {
    Say "DONE. Now reboot the HU (hold the power/volume knob ~30 s). After it boots, connect the iPhone,"
    Say "open CarPlay + Apple Maps and start a route; the arrow should appear on the HUD/cluster."
    Say "If anything is wrong, run rollback_hook.ps1 -Apply (SSH survives even an HMI restart loop)."
} else {
    Say "DRY-RUN only. Re-run with -Apply to perform the install (requires your approval)."
    Say "The car must be awake + on a charger; SSH stays reachable for rollback."
}
