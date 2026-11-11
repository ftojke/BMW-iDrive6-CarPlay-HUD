# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# CarPlay -> BMW HUD/KOMBI: remove the hook and restore the stock mm-ipod. WRITE operation.
# DRY-RUN BY DEFAULT; add -Apply to execute. SSH stays reachable even if the HMI is looping, so this always
# works from the laptop. After it finishes, reboot the HU (hold the knob ~30 s).
param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$hu   = Join-Path $PSScriptRoot 'hu.ps1'
if (-not (Test-Path $hu)) { $hu = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'tools\hu.ps1' }

function HU($cmd) { if ($Apply) { & $hu $cmd } else { Write-Host "  [DRY] hu> $cmd" } }

Write-Host "CarPlay->HUD rollback  (Apply=$Apply)"
Write-Host "Restores /opt/conn/bin/carplay/mm-ipod from mm-ipod.real and removes /opt/aa-cp.`n"

# stop a running hooked instance (harmless if not running); the daemon is not auto-restarted (MaxProcessRestarts=0)
HU 'for p in `pidin | grep mm-ipod | grep -v grep | awk "{print \$1}"`; do slay -Q $p 2>/dev/null; done'

HU 'mount -uw /fs/sda0'
# restore the stock binary over the wrapper, only if the backup rename exists
HU 'test -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real && mv -f /fs/sda0/opt/conn/bin/carplay/mm-ipod.real /fs/sda0/opt/conn/bin/carplay/mm-ipod'
HU 'rm -rf /fs/sda0/opt/aa-cp'
# restore the stock CarPlay Wi-Fi iAP2 config (removes the [ident] route-guidance declaration)
HU 'test -f /var/opt/conn/carplay/iap2_Wifi.cfg.orig && cp /var/opt/conn/carplay/iap2_Wifi.cfg.orig /var/opt/conn/carplay/iap2_Wifi.cfg && rm /var/opt/conn/carplay/iap2_Wifi.cfg.orig'
HU 'sync'
HU 'mount -ur /fs/sda0'
HU 'mount | grep sda0'
HU 'ls -l /opt/conn/bin/carplay/mm-ipod; ls /opt/aa-cp 2>/dev/null || echo "/opt/aa-cp removed"'
HU 'cksum /opt/conn/bin/carplay/mm-ipod'

Write-Host ""
if ($Apply) {
    Write-Host "DONE. Reboot the HU (hold the knob ~30 s). Verify the mm-ipod cksum matches the backed-up mm-ipod.orig."
    Write-Host "CarPlay then behaves exactly as stock (no HUD arrows)."
} else {
    Write-Host "DRY-RUN only. Re-run with -Apply to restore stock."
}
