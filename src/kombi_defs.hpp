// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI (NBT Evo ID6). KOMBI target enums + distance formatting.
//
// The KOMBI (instrument cluster + windscreen HUD) draws BMW's own route-guidance graph. The enum values
// here are the ManeuverDescription / lane-arrow / unit codes of the BMW.IC.Guidance (SOME/IP 0xB006)
// service, as reverse-engineered and car-verified by nanaisu's NBT-AAD
// (github.com/Sq00ky/Supra-iDrive6-Android-Auto, milestone-7/src/kombi_nav_msg.hpp). Reused here under
// the same PolyForm Noncommercial license for this personal, non-commercial CarPlay integration.
//
// This header only defines the KOMBI-side vocabulary and host-side distance formatting; the actual
// SOME/IP frame builder and client (kombi_graph / kombi_client) are reused unchanged from NBT-AAD on
// the car. Pure, no I/O, strict C++98.
#ifndef CP_KOMBI_DEFS_HPP
#define CP_KOMBI_DEFS_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace cp {
namespace kombi {

// KOMBI ManeuverDescription enum (BMW.IC.Guidance). Car-verified values from NBT-AAD: LEFT 6 / RIGHT 7
// and destination 10; the rest follow the KOMBI enum names.
enum Direction {
    DIR_STRAIGHT      = 1,   // STRAIGHT_ON
    DIR_HALF_LEFT     = 2,
    DIR_HALF_RIGHT    = 3,
    DIR_TIGHT_LEFT    = 4,
    DIR_TIGHT_RIGHT   = 5,
    DIR_LEFT          = 6,
    DIR_RIGHT         = 7,
    DIR_UTURN_LEFT    = 8,
    DIR_UTURN_RIGHT   = 9,
    DIR_DESTINATION   = 10,
    DIR_ROUNDABOUT_CW = 14,  // left-hand traffic
    DIR_ROUNDABOUT_CCW= 15,  // right-hand traffic (Serbia / EU)
    DIR_HIGHWAY_IN_L  = 25,
    DIR_HIGHWAY_IN_R  = 26,
    DIR_HIGHWAY_OUT_L = 28,
    DIR_HIGHWAY_OUT_R = 29,
    DIR_LANE_CHANGE_L = 32,
    DIR_LANE_CHANGE_R = 33,
    DIR_FORK_LEFT     = 37,
    DIR_FORK_RIGHT    = 38
};

// KOMBI distance unit byte that follows the distance text (read off a J29 HUD by NBT-AAD):
//   0 km, 1 mi, 3 m, 4 yd, 5 ft.
enum Unit {
    U_KM = 0x00,
    U_MI = 0x01,
    U_M  = 0x03,
    U_YD = 0x04,
    U_FT = 0x05
};

// One KOMBI lane as the cluster draws it. kind 1 = a lane to take (highlighted), 2 = another lane.
// arrow uses the same enum as Direction (STRAIGHT_ON 1, HALF_* 2/3, TIGHT_* 4/5, LEFT/RIGHT 6/7,
// UTURN_* 8/9). One arrow per lane, as in every factory lane model.
struct Lane {
    uint8_t kind;
    uint8_t arrow;
    Lane() : kind(0), arrow(0) {}
    Lane(uint8_t k, uint8_t a) : kind(k), arrow(a) {}
    bool operator==(const Lane& o) const { return kind == o.kind && arrow == o.arrow; }
};
typedef std::vector<Lane> Lanes;
const uint8_t LANE_TAKE  = 1;
const uint8_t LANE_OTHER = 2;
const size_t  LANE_MAX   = 8;   // factory lane models seen: 2..7 lanes

inline const char* unitName(int u) {
    switch (u) {
        case U_KM: return "km";
        case U_MI: return "mi";
        case U_M:  return "m";
        case U_YD: return "yd";
        case U_FT: return "ft";
        default:   return "?";
    }
}

inline const char* directionName(int d) {
    switch (d) {
        case DIR_STRAIGHT:       return "STRAIGHT_ON";
        case DIR_HALF_LEFT:      return "HALF_LEFT";
        case DIR_HALF_RIGHT:     return "HALF_RIGHT";
        case DIR_TIGHT_LEFT:     return "TIGHT_LEFT";
        case DIR_TIGHT_RIGHT:    return "TIGHT_RIGHT";
        case DIR_LEFT:           return "LEFT";
        case DIR_RIGHT:          return "RIGHT";
        case DIR_UTURN_LEFT:     return "UTURN_LEFT";
        case DIR_UTURN_RIGHT:    return "UTURN_RIGHT";
        case DIR_DESTINATION:    return "DESTINATION";
        case DIR_ROUNDABOUT_CW:  return "ROUNDABOUT_CW";
        case DIR_ROUNDABOUT_CCW: return "ROUNDABOUT_CCW";
        case DIR_HIGHWAY_IN_L:   return "HIGHWAY_IN_LEFT";
        case DIR_HIGHWAY_IN_R:   return "HIGHWAY_IN_RIGHT";
        case DIR_HIGHWAY_OUT_L:  return "HIGHWAY_OUT_LEFT";
        case DIR_HIGHWAY_OUT_R:  return "HIGHWAY_OUT_RIGHT";
        case DIR_LANE_CHANGE_L:  return "LANE_CHANGE_LEFT";
        case DIR_LANE_CHANGE_R:  return "LANE_CHANGE_RIGHT";
        case DIR_FORK_LEFT:      return "FORK_LEFT";
        case DIR_FORK_RIGHT:     return "FORK_RIGHT";
        default:                 return "?";
    }
}

// ---- integer-only distance formatting (the daemon does not link libm) ------------------------------
inline std::string fmtInt(int64_t v) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", (long long)v);
    return std::string(b);
}

// One decimal place, integer math: v_milli is value*1000 (e.g. 1.3 -> 1300).
inline std::string fmt1FromMilli(int64_t v_milli) {
    bool neg = v_milli < 0;
    int64_t a = neg ? -v_milli : v_milli;
    int64_t whole = a / 1000;
    int64_t frac1 = (a % 1000) / 100;            // tenths, truncated
    char b[48];
    snprintf(b, sizeof(b), "%s%lld.%lld", neg ? "-" : "", (long long)whole, (long long)frac1);
    return std::string(b);
}

// Metric display from metres, Google-Maps style: >=1 km -> one-decimal km; below that metres in
// 10 m steps under 100 m, else 50 m steps; floor 10 m. Sets text + KOMBI unit byte. Mirrors NBT-AAD.
inline void metricDisplay(int64_t m, std::string& text, int& unit) {
    if (m >= 1000) {
        // round to nearest 100 m for the one-decimal km value
        int64_t km_milli = ((m + 50) / 100) * 100;   // metres rounded to 100 m
        text = fmt1FromMilli(km_milli);              // km_milli is metres == value*1000 of km
        unit = U_KM;
        return;
    }
    int64_t step = (m < 100) ? 10 : 50;
    int64_t r = (m / step) * step;
    if (r < 10) r = 10;
    text = fmtInt(r);
    unit = U_M;
}

} // namespace kombi
} // namespace cp

#endif
