#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
"""
rgi_decode.py - CarPlay -> BMW HUD/KOMBI project helper.

Two jobs, both read-only:

  1. Baseline scan of an mm-ipod slog capture (phase P0): summarise which iAP2 control-session
     messages the car's stock CarPlay stack exchanged, and flag whether any Route Guidance
     message (0x5200-0x5204) is present. On a stock capture there should be NONE - the head unit
     never sends 0x5200, so the phone never sends 0x5201/0x5202 - which is exactly the gap this
     project fills by injecting 0x5200.

        python rgi_decode.py <slogfile>

  2. Decode one captured iAP2 control-session message (phases P3/P4), given as hex. Use it to
     verify an injected 0x5200 or a received 0x5201/0x5202 frame on the bench / car.

        python rgi_decode.py --hex "40 40 00 0E 52 00 00 06 00 00 00 1E ..."

The TLV field names mirror src/iap2_rgi.hpp so the Python reference and the C++ agree.
"""
import re
import sys

MSG_NAMES = {
    0x5200: "StartRouteGuidanceUpdates",
    0x5201: "RouteGuidanceUpdate",
    0x5202: "RouteGuidanceManeuverUpdate",
    0x5203: "StopRouteGuidanceUpdates",
    0x5204: "RouteGuidanceLaneGuidanceInformation",
    0x5001: "NowPlayingUpdate",
}

RGU_FIELDS = {
    0x0001: "RouteGuidanceState", 0x0002: "ManeuverState", 0x0003: "CurrentRoadName",
    0x0004: "DestinationName", 0x0005: "ETA", 0x0006: "TimeRemaining",
    0x0007: "DistanceRemaining", 0x0008: "DistRemainingStr", 0x0009: "DistRemainingUnits",
    0x000A: "DistanceToManeuver", 0x000B: "DistManeuverStr", 0x000C: "DistManeuverUnits",
    0x000D: "ManeuverCurrentList", 0x000E: "ManeuverCount", 0x000F: "ShownInApp",
    0x0010: "LaneCurrentIndex", 0x0011: "LaneTotalCount", 0x0012: "LaneShowing",
    0x0013: "SourceName", 0x0014: "SourceSupportsRouteGuidance",
}
RGM_FIELDS = {
    0x0001: "Index", 0x0002: "ManeuverDescription", 0x0003: "ManeuverType",
    0x0004: "AfterManeuverRoadName", 0x0005: "DistanceBetween", 0x0006: "DistanceStr",
    0x0007: "DistanceUnits", 0x0008: "DrivingSide", 0x0009: "JunctionType",
    0x000A: "JunctionElementAngle", 0x000B: "JunctionExitAngle", 0x000C: "LinkedLaneGuidance",
    0x000D: "ExitInfo",
}
ROUTE_STATE = {0: "NoRoute", 1: "Guiding", 2: "Arrived", 3: "Loading",
               4: "Locating", 5: "Rerouting", 6: "ProceedToRoute"}

# iAP2 maneuver type -> short name (0x5202 / 0x03), for a readable dump.
MANEUVER_TYPE = {
    0: "NoTurn", 1: "Left", 2: "Right", 3: "Straight", 4: "UTurn", 5: "FollowRoad",
    6: "EnterRoundabout", 7: "ExitRoundabout", 8: "OffRamp", 9: "OnRamp", 10: "Arrive",
    11: "StartRoute", 12: "Arrive", 13: "KeepLeft", 14: "KeepRight", 15: "EnterFerry",
    16: "ExitFerry", 17: "ChangeFerry", 18: "StartRouteUTurn", 19: "UTurnAtRoundabout",
    20: "LeftAtEnd", 21: "RightAtEnd", 22: "HwyOffRampLeft", 23: "HwyOffRampRight",
    24: "ArriveDestLeft", 25: "ArriveDestRight", 26: "UTurnWhenPossible", 27: "Arrive",
    47: "SharpLeft", 48: "SharpRight", 49: "SlightLeft", 50: "SlightRight",
    51: "ChangeHighway", 52: "ChangeHighwayLeft", 53: "ChangeHighwayRight", 255: "NotSet",
}
for _n in range(28, 47):
    MANEUVER_TYPE[_n] = "RoundaboutExit%d" % (_n - 28 + 1)


def _u(b):
    v = 0
    for x in b:
        v = (v << 8) | x
    return v


def _i16(b):
    v = _u(b)
    return v - 0x10000 if v >= 0x8000 else v


def parse_tlvs(body):
    """Yields (id, value_bytes) from a TLV param block. len includes the 4-byte header."""
    i = 0
    while i + 4 <= len(body):
        ln = _u(body[i:i + 2])
        if ln < 4 or i + ln > len(body):
            break
        yield _u(body[i + 2:i + 4]), body[i + 4:i + ln]
        i += ln


def decode_control_message(data):
    """data = bytes of a 40 40-framed control-session message. Returns (msgid, [(id, val)]) or None."""
    if len(data) < 6 or data[0] != 0x40 or data[1] != 0x40:
        return None
    ln = _u(data[2:4])
    if ln < 6 or ln > len(data):
        return None
    msgid = _u(data[4:6])
    return msgid, list(parse_tlvs(data[6:ln]))


def describe_message(msgid, tlvs):
    name = MSG_NAMES.get(msgid, "msg_0x%04X" % msgid)
    fields = RGU_FIELDS if msgid == 0x5201 else RGM_FIELDS if msgid == 0x5202 else {}
    lines = ["0x%04X %s (%d TLV)" % (msgid, name, len(tlvs))]
    for fid, val in tlvs:
        fname = fields.get(fid, "0x%04X" % fid)
        shown = ""
        if msgid == 0x5201 and fid == 0x0001 and len(val) == 1:
            shown = ROUTE_STATE.get(val[0], str(val[0]))
        elif msgid == 0x5201 and fid == 0x0014 and len(val) == 1:
            shown = "YES" if val[0] else "NO"
        elif msgid == 0x5202 and fid == 0x0003 and len(val) == 1:
            shown = MANEUVER_TYPE.get(val[0], str(val[0]))
        elif fid in (0x000B,) and msgid == 0x5202 and len(val) == 2:
            shown = "%d deg" % _i16(val)
        elif len(val) and all(32 <= c < 127 for c in val):
            shown = repr(bytes(val).decode("ascii", "replace"))
        elif 1 <= len(val) <= 4:
            shown = str(_u(val))
        else:
            shown = bytes(val).hex()
        lines.append("    %-28s %s" % (fname, shown))
    return "\n".join(lines)


def scan_slog(path):
    """Baseline scan: count [xxxx] message tags and flag any Route Guidance message."""
    tag = re.compile(r"\[([0-9A-Fa-f]{4})\]\s*([A-Za-z]\w*)?")
    counts = {}
    names = {}
    with open(path, "r", errors="replace") as f:
        for line in f:
            for m in tag.finditer(line):
                mid = int(m.group(1), 16)
                # only plausible iAP2 message ids (0x1000..0x7FFF range seen in these logs)
                if mid < 0x1000:
                    continue
                counts[mid] = counts.get(mid, 0) + 1
                if m.group(2):
                    names[mid] = m.group(2)
    print("iAP2 control-session messages seen in %s:" % path)
    for mid in sorted(counts):
        nm = names.get(mid, MSG_NAMES.get(mid, ""))
        print("  0x%04X  x%-6d %s" % (mid, counts[mid], nm))
    rgi = [m for m in counts if 0x5200 <= m <= 0x5204]
    print()
    if rgi:
        print("Route Guidance (0x520x) PRESENT: %s" % ", ".join("0x%04X" % m for m in sorted(rgi)))
        print("  -> the phone is sending route guidance; the KOMBI front-end can consume it.")
    else:
        print("Route Guidance (0x520x): NONE.")
        print("  -> expected on a STOCK capture: the head unit never subscribed (no 0x5200),")
        print("     so the phone sends no 0x5201/0x5202. This is the gap the project fills by")
        print("     injecting 0x5200 from the mm-ipod hook (phase P3).")
    return 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "--hex":
        data = bytes(int(x, 16) for x in re.findall(r"[0-9A-Fa-f]{2}", argv[2]))
        res = decode_control_message(data)
        if not res:
            print("not a 40 40-framed control-session message")
            return 1
        print(describe_message(res[0], res[1]))
        return 0
    if len(argv) == 2 and not argv[1].startswith("-"):
        return scan_slog(argv[1])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
