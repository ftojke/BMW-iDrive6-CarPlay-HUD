# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Phase P0 - READ-ONLY capture from the NBT Evo head unit for the CarPlay -> HUD project.
#
# Nothing is written to the car: only cat / ls / pidin / ping / netstat / sloginfo.
# Uses the project's SSH helper ..\..\tools\hu.ps1 (plink, pinned host keys, password file).
#
# usage (car awake, on charger, ENET cable, laptop NIC 169.254.0.10/16):
#   powershell -NoProfile -ExecutionPolicy Bypass -File p0_capture.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File p0_capture.ps1 -DryRun     # no connection
#   ... -SkipLive        # skip the interactive slog capture
#   ... -SlogSeconds 180 # length of the live capture
param(
    [switch]$DryRun,
    [switch]$SkipLive,
    [int]$SlogSeconds = 150
)
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $here                       # carplay_hud
$root = Split-Path -Parent $proj                       # handof
$hu   = Join-Path $root 'tools\hu.ps1'
if (-not (Test-Path $hu)) { Write-Error "missing $hu"; exit 2 }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$out = Join-Path $proj ("p0\" + $stamp + $(if ($DryRun) { '_dryrun' } else { '' }))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$summary = Join-Path $out 'SUMMARY.txt'

function Note([string]$s) { Write-Host $s; Add-Content -Path $summary -Value $s }

# One read-only command on the HU. $file = save stdout (binary-safe) under $out.
function HU([string]$cmd, [string]$file, [string]$board = 'omap', [int]$timeout = 60) {
    $a = @{ Command = $cmd; Board = $board; TimeoutSec = $timeout }
    if ($file) { $a.OutFile = Join-Path $out $file }
    if ($DryRun) { $a.DryRun = $true }
    $o = & $hu @a
    if ($DryRun) { $o | ForEach-Object { Write-Host ("  " + $_) } }
    return $LASTEXITCODE
}

Note "P0 read-only capture  $stamp  (DryRun=$DryRun)"
Note "output: $out"

# ---- 0. reachability ----------------------------------------------------------------------------------
if (-not $DryRun) {
    foreach ($ip in '169.254.199.119', '169.254.199.99') {
        $ok = Test-NetConnection -ComputerName $ip -Port 22 -WarningAction SilentlyContinue -InformationLevel Quiet
        Note ("ssh {0}:22 -> {1}" -f $ip, $ok)
        if (-not $ok) { Note "HU not reachable: check ignition, ENET cable, NIC 169.254.0.10/16. Stopping."; exit 3 }
    }
}

# ---- 1. identity --------------------------------------------------------------------------------------
Note "`n[1] identity"
HU 'cat /etc/nbt_version.txt; uname -a' 'id_jacinto.txt' 'jacinto' | Out-Null
HU 'uname -a; cat /var/opt/conn/carplay/carplay.ini' 'id_omap_carplay_ini.txt' | Out-Null

# ---- 2. iAP2 / CarPlay configuration ------------------------------------------------------------------
Note "`n[2] iAP2 configs"
foreach ($f in 'iap2.cfg', 'iap2_Wifi.cfg', 'protocolsIap2.cfg', 'carplay.ini') {
    HU ("cat /var/opt/conn/carplay/" + $f) ("cfg_" + $f) | Out-Null
}
HU 'cat /opt/conn/bin/Connectivity.cfg' 'cfg_Connectivity.cfg' | Out-Null
HU 'ls -l /opt/conn/bin/carplay /opt/conn/lib/carplay /opt/conn/lib/carplay/devctrl /var/opt/conn/carplay' 'listing_carplay.txt' | Out-Null

# ---- 3. binaries for RE (send/recv seam, Identify builder) ------------------------------------------
Note "`n[3] binaries"
$bins = @(
    '/opt/conn/bin/carplay/mm-ipod',
    '/opt/conn/bin/carplay/iAP2ProtocolMgmt',
    '/opt/conn/lib/carplay/ipod-drvr-iap2.so',
    '/opt/conn/lib/carplay/mss-ipodiap2.so',
    '/opt/conn/lib/carplay/libiap2client.so',
    '/opt/conn/lib/carplay/ipod-transport-bt.so',
    '/opt/conn/lib/carplay/libmmipodtrresmgr.so',
    '/opt/conn/lib/carplay/ipod-acp-har-i2c-iap2.so',
    '/opt/conn/lib/carplay/devctrl/libconn_iap2lib.so',
    '/opt/conn/lib/carplay/devctrl/libconn_iap2devctrl.so',
    '/opt/conn/lib/carplay/devctrl/libconn_iap2_wifi_qnx.so',
    '/opt/conn/lib/carplay/devctrl/libconn_iAP2ProtocolMgmt.so',
    '/opt/conn/lib/carplay/devctrl/libconn_carplay.so',
    '/opt/conn/lib/carplay/devctrl/libconn_carplaydevctrl.so'
)
New-Item -ItemType Directory -Force -Path (Join-Path $out 'bin') | Out-Null
foreach ($b in $bins) {
    $name = 'bin\' + (Split-Path $b -Leaf)
    $rc = HU ("cat " + $b) $name 'omap' 120
    if (-not $DryRun -and $rc -ne 0) { Note "  WARN: $b -> exit $rc" }
}

# ---- 4. who launches mm-ipod, with what environment ---------------------------------------------------
Note "`n[4] mm-ipod process"
HU 'pidin -p mm-ipod arg; pidin -p mm-ipod env; pidin family | grep -i -e mm-ipod -e Connectivity -e ConnStarter' 'proc_mm-ipod.txt' | Out-Null
HU 'ls -lR /pps/services/multimedia/iap2_wifi; cat /pps/services/multimedia/iap2_wifi/*' 'pps_iap2_wifi.txt' | Out-Null

# ---- 5. KOMBI reachability (SOME/IP 0xB006 @ 160.48.199.96:30502) ------------------------------------
Note "`n[5] KOMBI"
HU 'ping -c 2 160.48.199.96; netstat -an | grep 160.48.199.96' 'kombi.txt' | Out-Null

# ---- 6. live slog while the iPhone connects and navigates --------------------------------------------
if (-not $SkipLive -and -not $DryRun) {
    Note "`n[6] LIVE slog capture ($SlogSeconds s)"
    Write-Host ""
    Write-Host "Na iPhone-u: iskljuci pa ukljuci Bluetooth (da se CarPlay ponovo poveze),"
    Write-Host "zatim pokreni rutu u Apple Maps (~60 s), pa rutu u Waze-u (~60 s)."
    Read-Host "Pritisni Enter kad si spreman (snimanje traje $SlogSeconds s)"
}
if (-not $SkipLive) {
    HU ("sloginfo -w & P=`$!; sleep " + $SlogSeconds + "; kill `$P") 'slog_live.txt' 'omap' ($SlogSeconds + 40) | Out-Null
}

# ---- 7. local post-processing -------------------------------------------------------------------------
if (-not $DryRun) {
    Note "`n[7] hashes + scan"
    Get-ChildItem $out -Recurse -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' -and $_.Name -ne 'SUMMARY.txt' } |
        ForEach-Object { '{0}  {1}  {2}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, $_.Length, $_.FullName.Substring($out.Length + 1) } |
        Set-Content (Join-Path $out 'SHA256SUMS.txt')
    $slog = Join-Path $out 'slog_live.txt'
    if (Test-Path $slog) {
        python (Join-Path $here 'rgi_decode.py') $slog | Tee-Object (Join-Path $out 'rgi_scan.txt')
        Select-String -Path $slog -Pattern '\[1D0[0-3]\]' -Context 0, 120 |
            ForEach-Object { $_.Line; $_.Context.PostContext } | Set-Content (Join-Path $out 'identify.txt')
        Note ("identify lines: " + (Get-Content (Join-Path $out 'identify.txt') -ErrorAction SilentlyContinue).Count)
    }
    $empty = Get-ChildItem $out -Recurse -File | Where-Object { $_.Length -eq 0 }
    if ($empty) { Note ("empty files (path missing on this firmware?): " + (($empty | ForEach-Object Name) -join ', ')) }
}
Note "`nP0 capture done. Nothing was written to the head unit."
