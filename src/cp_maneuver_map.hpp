// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI (NBT Evo ID6). iAP2 maneuver type -> KOMBI ManeuverDescription.
//
// Maps the CarPlay iAP2 maneuver type (0x5202 / 0x03, values 0..53, 255 = not set) to the BMW KOMBI
// ManeuverDescription enum (kombi_defs.hpp). The iAP2 type semantics are from Apple's accessoryd enum
// as tabulated in luka-dev/mib2q-carplay-rgi (car-verified on Audi MHI2Q); the KOMBI target values are
// car-verified by NBT-AAD on a BMW/J29 cluster+HUD. Both are reused for this non-commercial project.
//
// Where the iAP2 type does not encode a turn side (U-turn, roundabout, ramps), the side is resolved
// from driving side (0 RHT, 1 LHT) and the exit angle (negative = left by Apple convention). These two
// side assumptions are documented for on-car confirmation (phase P4).
//
// Pure, no I/O, strict C++98.
#ifndef CP_MANEUVER_MAP_HPP
#define CP_MANEUVER_MAP_HPP

#include "kombi_defs.hpp"

namespace cp {

// iAP2 CarPlay maneuver type values (0x5202 / 0x0003).
enum Iap2Maneuver {
    IM_NO_TURN                = 0,
    IM_LEFT_TURN              = 1,
    IM_RIGHT_TURN             = 2,
    IM_STRAIGHT               = 3,
    IM_U_TURN                 = 4,
    IM_FOLLOW_ROAD            = 5,
    IM_ENTER_ROUNDABOUT       = 6,
    IM_EXIT_ROUNDABOUT        = 7,
    IM_OFF_RAMP               = 8,
    IM_ON_RAMP                = 9,
    IM_ARRIVE_A               = 10,
    IM_START_ROUTE            = 11,
    IM_ARRIVE_B               = 12,
    IM_KEEP_LEFT              = 13,
    IM_KEEP_RIGHT             = 14,
    IM_ENTER_FERRY            = 15,
    IM_EXIT_FERRY             = 16,
    IM_CHANGE_FERRY           = 17,
    IM_START_ROUTE_U_TURN     = 18,
    IM_U_TURN_AT_ROUNDABOUT   = 19,
    IM_LEFT_TURN_AT_END       = 20,
    IM_RIGHT_TURN_AT_END      = 21,
    IM_HWY_OFF_RAMP_LEFT      = 22,
    IM_HWY_OFF_RAMP_RIGHT     = 23,
    IM_ARRIVE_DEST_LEFT       = 24,
    IM_ARRIVE_DEST_RIGHT      = 25,
    IM_U_TURN_WHEN_POSSIBLE   = 26,
    IM_ARRIVE_C               = 27,
    IM_ROUNDABOUT_EXIT_1      = 28,   // 28..46 = roundabout exit 1..19
    IM_ROUNDABOUT_EXIT_19     = 46,
    IM_SHARP_LEFT             = 47,
    IM_SHARP_RIGHT            = 48,
    IM_SLIGHT_LEFT            = 49,
    IM_SLIGHT_RIGHT           = 50,
    IM_CHANGE_HIGHWAY         = 51,
    IM_CHANGE_HIGHWAY_LEFT    = 52,
    IM_CHANGE_HIGHWAY_RIGHT   = 53,
    IM_NOT_SET                = 255
};

// Inputs needed to resolve a maneuver that does not encode its side.
struct ManeuverIn {
    int type;           // iAP2 maneuver type
    int driving_side;   // 0 RHT, 1 LHT, -1 unknown
    bool has_exit_angle;
    int exit_angle;     // signed degrees, negative = left (Apple convention)
    ManeuverIn() : type(IM_NOT_SET), driving_side(-1), has_exit_angle(false), exit_angle(0) {}
};

namespace detail {

// U-turn side: in right-hand traffic a U-turn loops to the left; mirror for LHT. Unknown -> left.
inline int uturnDir(int driving_side) {
    return driving_side == 1 ? kombi::DIR_UTURN_RIGHT : kombi::DIR_UTURN_LEFT;
}
// Roundabout rotation: right-hand traffic is counter-clockwise; LHT is clockwise. Unknown -> CCW.
inline int roundaboutDir(int driving_side) {
    return driving_side == 1 ? kombi::DIR_ROUNDABOUT_CW : kombi::DIR_ROUNDABOUT_CCW;
}
// Ramp side from the exit angle if present, else the traffic-side default (RHT ramps to the right).
inline bool rampGoesLeft(const ManeuverIn& m) {
    if (m.has_exit_angle) return m.exit_angle < 0;
    return m.driving_side == 1;   // LHT ramps to the left; RHT/unknown to the right
}

} // namespace detail

inline int cpLaneArrowFromAngle(int angle_deg);

// Maps one iAP2 maneuver to a KOMBI Direction. Unknown/out-of-range -> STRAIGHT_ON (a safe glyph).
inline int cpManeuverToKombi(const ManeuverIn& m) {
    switch (m.type) {
        case IM_NO_TURN:
        case IM_STRAIGHT:
        case IM_FOLLOW_ROAD:
        case IM_ENTER_FERRY:
        case IM_CHANGE_FERRY:
        case IM_NOT_SET:
            return kombi::DIR_STRAIGHT;

        case IM_START_ROUTE:
        case IM_EXIT_FERRY:
        case IM_CHANGE_HIGHWAY:
            if (m.has_exit_angle && m.exit_angle != 1000 && m.exit_angle != -1000)
                return cpLaneArrowFromAngle(m.exit_angle);
            return kombi::DIR_STRAIGHT;

        case IM_LEFT_TURN:
        case IM_LEFT_TURN_AT_END:
            return kombi::DIR_LEFT;
        case IM_RIGHT_TURN:
        case IM_RIGHT_TURN_AT_END:
            return kombi::DIR_RIGHT;

        case IM_SLIGHT_LEFT:
        case IM_KEEP_LEFT:
            return kombi::DIR_HALF_LEFT;
        case IM_SLIGHT_RIGHT:
        case IM_KEEP_RIGHT:
            return kombi::DIR_HALF_RIGHT;

        case IM_SHARP_LEFT:
            return kombi::DIR_TIGHT_LEFT;
        case IM_SHARP_RIGHT:
            return kombi::DIR_TIGHT_RIGHT;

        case IM_U_TURN:
        case IM_START_ROUTE_U_TURN:
        case IM_U_TURN_WHEN_POSSIBLE:
            return detail::uturnDir(m.driving_side);

        case IM_ENTER_ROUNDABOUT:
        case IM_EXIT_ROUNDABOUT:
        case IM_U_TURN_AT_ROUNDABOUT:
            return detail::roundaboutDir(m.driving_side);

        case IM_ON_RAMP:
            return detail::rampGoesLeft(m) ? kombi::DIR_HIGHWAY_IN_L : kombi::DIR_HIGHWAY_IN_R;
        case IM_OFF_RAMP:
            return detail::rampGoesLeft(m) ? kombi::DIR_HIGHWAY_OUT_L : kombi::DIR_HIGHWAY_OUT_R;
        case IM_HWY_OFF_RAMP_LEFT:
            return kombi::DIR_HIGHWAY_OUT_L;
        case IM_HWY_OFF_RAMP_RIGHT:
            return kombi::DIR_HIGHWAY_OUT_R;

        case IM_CHANGE_HIGHWAY_LEFT:
            return kombi::DIR_LANE_CHANGE_L;
        case IM_CHANGE_HIGHWAY_RIGHT:
            return kombi::DIR_LANE_CHANGE_R;

        case IM_ARRIVE_A:
        case IM_ARRIVE_B:
        case IM_ARRIVE_C:
        case IM_ARRIVE_DEST_LEFT:
        case IM_ARRIVE_DEST_RIGHT:
            return kombi::DIR_DESTINATION;

        default:
            // 28..46 = roundabout exit 1..19
            if (m.type >= IM_ROUNDABOUT_EXIT_1 && m.type <= IM_ROUNDABOUT_EXIT_19)
                return detail::roundaboutDir(m.driving_side);
            return kombi::DIR_STRAIGHT;
    }
}

// Roundabout exit number (1..19) for the roundabout-exit maneuver types, else 0.
inline int cpRoundaboutExit(int iap2_type) {
    if (iap2_type >= IM_ROUNDABOUT_EXIT_1 && iap2_type <= IM_ROUNDABOUT_EXIT_19)
        return iap2_type - IM_ROUNDABOUT_EXIT_1 + 1;
    return 0;
}

// True if the maneuver is an arrival (so the caller can draw the destination flag / end the route).
inline bool cpIsArrival(int iap2_type) {
    return iap2_type == IM_ARRIVE_A || iap2_type == IM_ARRIVE_B || iap2_type == IM_ARRIVE_C ||
           iap2_type == IM_ARRIVE_DEST_LEFT || iap2_type == IM_ARRIVE_DEST_RIGHT;
}

// ---- lane arrow bucketing ---------------------------------------------------------------------------
// CarPlay lane directions arrive in 0x5204 as angles; the exact wire layout is confirmed on-car (P0/P4).
// This buckets a lane's turn angle (signed degrees, negative = left) into a KOMBI lane arrow, matching
// the maneuver glyph vocabulary.
inline int cpLaneArrowFromAngle(int angle_deg) {
    int a = angle_deg;
    int mag = a < 0 ? -a : a;
    if (mag <= 10) return kombi::DIR_STRAIGHT;
    bool left = a < 0;
    if (mag <= 35) return left ? kombi::DIR_HALF_LEFT : kombi::DIR_HALF_RIGHT;
    if (mag <= 110) return left ? kombi::DIR_LEFT : kombi::DIR_RIGHT;
    if (mag <= 160) return left ? kombi::DIR_TIGHT_LEFT : kombi::DIR_TIGHT_RIGHT;
    return left ? kombi::DIR_UTURN_LEFT : kombi::DIR_UTURN_RIGHT;
}

} // namespace cp

#endif
