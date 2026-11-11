#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# CarPlay -> BMW HUD/KOMBI: mm-ipod wrapper (deployed as /opt/conn/bin/carplay/mm-ipod).
#
# Connectivity launches TWO mm-ipod instances from the same path: one for Bluetooth iAP2 (iap2.cfg) and one
# for the CarPlay Wi-Fi transport (iap2_Wifi.cfg). We preload the route-guidance hook ONLY for the CarPlay
# Wi-Fi instance; the Bluetooth instance runs byte-for-byte stock. The real binary is kept as mm-ipod.real.
#
# Fail-safe: if the hook .so is missing/unreadable, LD_PRELOAD of a missing lib is ignored by the loader and
# mm-ipod still starts. Removing the hook = restore mm-ipod.real over this wrapper (see rollback).
#
# SAFETY SELF-TEST: before preloading the hook into the real mm-ipod, we load it in a throwaway /bin/uname.
# If the QNX loader can't map/relocate the .so (or its constructor faults), uname exits non-zero and we do NOT
# set LD_PRELOAD -> mm-ipod runs byte-for-byte stock. This is what stops a bad build from ever crashing CarPlay
# (the system auto-launches this wrapper whenever CarPlay Wi-Fi is probed, so it must be crash-proof here).
HOOK=/opt/aa-cp/libnbt_carplay_hud.so
case "$*" in
    *iap2_wifi*)
        if [ -r "$HOOK" ] && LD_PRELOAD="$HOOK" NBT_CARPLAY_SELFTEST=1 /bin/uname >/dev/null 2>&1; then
            export LD_PRELOAD="$HOOK"
            export NBT_CARPLAY_HUD=1
            # cfg [ident] declares the RGI message IDs natively; the hook adds only the RouteGuidanceDisplayComponent
            # (0x001E) on top (cp_ident_patch skips IDs already present). Component is required for the phone to bind RG.
            export NBT_CARPLAY_LOG=/dev/shmem/cphud.log   # diagnostics; /dev/shmem is RAM (writable, per-boot)
            # Per-packet tracing is OFF by default (it caused a fopen/fclose syscall storm on the driver's
            # bt_read thread -> watchdog HU reset). Create /dev/shmem/cphud.debug over SSH to turn full tracing
            # back on for one CarPlay session without redeploying; delete it to return to the quiet production log.
            [ -f /dev/shmem/cphud.debug ] && export NBT_CARPLAY_DEBUG=1
        fi
        ;;
esac
exec /opt/conn/bin/carplay/mm-ipod.real "$@"
