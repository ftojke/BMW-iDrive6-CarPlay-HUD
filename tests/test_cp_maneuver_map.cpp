// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests for the CarPlay iAP2 maneuver-type -> KOMBI ManeuverDescription mapping.
#include "../src/cp_maneuver_map.hpp"
#include "test_util.hpp"

using namespace cp;

static ManeuverIn mk(int type, int side = -1, bool hasAngle = false, int angle = 0) {
    ManeuverIn m;
    m.type = type; m.driving_side = side; m.has_exit_angle = hasAngle; m.exit_angle = angle;
    return m;
}

static bool isValidDir(int d) {
    switch (d) {
        case kombi::DIR_STRAIGHT: case kombi::DIR_HALF_LEFT: case kombi::DIR_HALF_RIGHT:
        case kombi::DIR_TIGHT_LEFT: case kombi::DIR_TIGHT_RIGHT: case kombi::DIR_LEFT:
        case kombi::DIR_RIGHT: case kombi::DIR_UTURN_LEFT: case kombi::DIR_UTURN_RIGHT:
        case kombi::DIR_DESTINATION: case kombi::DIR_ROUNDABOUT_CW: case kombi::DIR_ROUNDABOUT_CCW:
        case kombi::DIR_HIGHWAY_IN_L: case kombi::DIR_HIGHWAY_IN_R: case kombi::DIR_HIGHWAY_OUT_L:
        case kombi::DIR_HIGHWAY_OUT_R: case kombi::DIR_LANE_CHANGE_L: case kombi::DIR_LANE_CHANGE_R:
        case kombi::DIR_FORK_LEFT: case kombi::DIR_FORK_RIGHT:
            return true;
        default: return false;
    }
}

static int test_every_type_maps() {
    // Every CarPlay maneuver type (0..53 and 255) must map to a valid KOMBI glyph, never garbage.
    for (int ty = 0; ty <= 53; ++ty)
        CHECK(isValidDir(cpManeuverToKombi(mk(ty, 0))));
    CHECK(isValidDir(cpManeuverToKombi(mk(IM_NOT_SET, 0))));
    // An out-of-range value falls back to STRAIGHT_ON.
    CHECK_EQ(cpManeuverToKombi(mk(200, 0)), kombi::DIR_STRAIGHT);
    return 0;
}

static int test_basic_turns() {
    CHECK_EQ(cpManeuverToKombi(mk(IM_LEFT_TURN, 0)),     kombi::DIR_LEFT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_RIGHT_TURN, 0)),    kombi::DIR_RIGHT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_STRAIGHT, 0)),      kombi::DIR_STRAIGHT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_FOLLOW_ROAD, 0)),   kombi::DIR_STRAIGHT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_SHARP_LEFT, 0)),    kombi::DIR_TIGHT_LEFT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_SHARP_RIGHT, 0)),   kombi::DIR_TIGHT_RIGHT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_SLIGHT_LEFT, 0)),   kombi::DIR_HALF_LEFT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_KEEP_RIGHT, 0)),    kombi::DIR_HALF_RIGHT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_LEFT_TURN_AT_END,0)),kombi::DIR_LEFT);
    CHECK_EQ(cpManeuverToKombi(mk(IM_CHANGE_HIGHWAY_LEFT,0)), kombi::DIR_LANE_CHANGE_L);
    CHECK_EQ(cpManeuverToKombi(mk(IM_CHANGE_HIGHWAY_RIGHT,0)),kombi::DIR_LANE_CHANGE_R);
    return 0;
}

static int test_arrival_and_destination() {
    CHECK_EQ(cpManeuverToKombi(mk(IM_ARRIVE_A, 0)), kombi::DIR_DESTINATION);
    CHECK_EQ(cpManeuverToKombi(mk(IM_ARRIVE_DEST_LEFT, 0)), kombi::DIR_DESTINATION);
    CHECK(cpIsArrival(IM_ARRIVE_A));
    CHECK(cpIsArrival(IM_ARRIVE_DEST_RIGHT));
    CHECK(!cpIsArrival(IM_LEFT_TURN));
    return 0;
}

static int test_uturn_side() {
    CHECK_EQ(cpManeuverToKombi(mk(IM_U_TURN, 0)), kombi::DIR_UTURN_LEFT);   // RHT
    CHECK_EQ(cpManeuverToKombi(mk(IM_U_TURN, 1)), kombi::DIR_UTURN_RIGHT);  // LHT
    CHECK_EQ(cpManeuverToKombi(mk(IM_U_TURN_WHEN_POSSIBLE, 0)), kombi::DIR_UTURN_LEFT);
    return 0;
}

static int test_roundabout() {
    CHECK_EQ(cpManeuverToKombi(mk(IM_ENTER_ROUNDABOUT, 0)), kombi::DIR_ROUNDABOUT_CCW); // RHT
    CHECK_EQ(cpManeuverToKombi(mk(IM_ENTER_ROUNDABOUT, 1)), kombi::DIR_ROUNDABOUT_CW);  // LHT
    // exit number from the 28..46 types
    CHECK_EQ(cpRoundaboutExit(28), 1);
    CHECK_EQ(cpRoundaboutExit(30), 3);
    CHECK_EQ(cpRoundaboutExit(46), 19);
    CHECK_EQ(cpRoundaboutExit(IM_LEFT_TURN), 0);
    CHECK_EQ(cpManeuverToKombi(mk(30, 0)), kombi::DIR_ROUNDABOUT_CCW);
    return 0;
}

static int test_ramps() {
    // explicit L/R ramp types
    CHECK_EQ(cpManeuverToKombi(mk(IM_HWY_OFF_RAMP_LEFT, 0)),  kombi::DIR_HIGHWAY_OUT_L);
    CHECK_EQ(cpManeuverToKombi(mk(IM_HWY_OFF_RAMP_RIGHT, 0)), kombi::DIR_HIGHWAY_OUT_R);
    // generic off-ramp: side from the exit angle
    CHECK_EQ(cpManeuverToKombi(mk(IM_OFF_RAMP, 0, true, -30)), kombi::DIR_HIGHWAY_OUT_L);
    CHECK_EQ(cpManeuverToKombi(mk(IM_OFF_RAMP, 0, true,  30)), kombi::DIR_HIGHWAY_OUT_R);
    // no angle: default to the traffic side (RHT -> right ramp, LHT -> left)
    CHECK_EQ(cpManeuverToKombi(mk(IM_OFF_RAMP, 0)), kombi::DIR_HIGHWAY_OUT_R);
    CHECK_EQ(cpManeuverToKombi(mk(IM_OFF_RAMP, 1)), kombi::DIR_HIGHWAY_OUT_L);
    CHECK_EQ(cpManeuverToKombi(mk(IM_ON_RAMP, 0, true, -20)), kombi::DIR_HIGHWAY_IN_L);
    CHECK_EQ(cpManeuverToKombi(mk(IM_ON_RAMP, 0, true,  20)), kombi::DIR_HIGHWAY_IN_R);
    return 0;
}

static int test_lane_angle_bucket() {
    CHECK_EQ(cpLaneArrowFromAngle(0),    kombi::DIR_STRAIGHT);
    CHECK_EQ(cpLaneArrowFromAngle(-20),  kombi::DIR_HALF_LEFT);
    CHECK_EQ(cpLaneArrowFromAngle(20),   kombi::DIR_HALF_RIGHT);
    CHECK_EQ(cpLaneArrowFromAngle(-90),  kombi::DIR_LEFT);
    CHECK_EQ(cpLaneArrowFromAngle(90),   kombi::DIR_RIGHT);
    CHECK_EQ(cpLaneArrowFromAngle(-140), kombi::DIR_TIGHT_LEFT);
    CHECK_EQ(cpLaneArrowFromAngle(175),  kombi::DIR_UTURN_RIGHT);
    return 0;
}

int main() {
    test_every_type_maps();
    test_basic_turns();
    test_arrival_and_destination();
    test_uturn_side();
    test_roundabout();
    test_ramps();
    test_lane_angle_bucket();
    TEST_RETURN();
}
