// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests for the RGI state machine and the app-agnostic gate (the Waze on/off behaviour).
#include "../src/cp_rgi_state.hpp"
#include "iap2_build.hpp"
#include "test_util.hpp"

#include <cstring>

using namespace cp;

// Builds a 0x5201 RouteGuidanceUpdate.
static std::vector<uint8_t> update(int routeState, const char* source, int supports /*-1 = omit*/,
                                   int curIndex /*-1 = omit*/, int distManeuverM /*-1 = omit*/,
                                   int maneuverCount) {
    std::vector<uint8_t> p;
    t::u8(p, iap2::RGU_ROUTE_STATE, (uint8_t)routeState);
    if (source) t::str(p, iap2::RGU_CURRENT_ROAD, "Current Rd");
    if (source) t::str(p, iap2::RGU_SOURCE_NAME, source);
    if (supports >= 0) t::u8(p, iap2::RGU_SOURCE_SUPPORTS, (uint8_t)supports);
    if (maneuverCount >= 0) t::u16(p, iap2::RGU_MANEUVER_COUNT, (uint16_t)maneuverCount);
    if (curIndex >= 0) t::u16(p, iap2::RGU_MANEUVER_LIST, (uint16_t)curIndex);
    if (distManeuverM >= 0) t::u32(p, iap2::RGU_DIST_MANEUVER, (uint32_t)distManeuverM);
    return t::framed(iap2::MSG_RG_UPDATE, p);
}

// Builds a 0x5202 RouteGuidanceManeuverUpdate.
static std::vector<uint8_t> maneuver(int index, int type, const char* afterRoad, int side) {
    std::vector<uint8_t> p;
    t::u16(p, iap2::RGM_INDEX, (uint16_t)index);
    t::u8(p, iap2::RGM_TYPE, (uint8_t)type);
    if (afterRoad) t::str(p, iap2::RGM_AFTER_ROAD, afterRoad);
    if (side >= 0) t::u8(p, iap2::RGM_DRIVING_SIDE, (uint8_t)side);
    return t::framed(iap2::MSG_RG_MANEUVER, p);
}

static void feed(RgiState& st, const std::vector<uint8_t>& m) {
    st.onFramedMessage(&m[0], m.size());
}

// Apple Maps / Google Maps / Waze 5.4.0+: supports=1, a real turn shows on the cluster.
static int test_supported_app_draws() {
    RgiState st;
    feed(st, update(iap2::RS_ROUTE_SET, "Apple Maps", 1, 0, 250, 3));
    feed(st, maneuver(0, IM_LEFT_TURN, "Knez Mihailova", 0));
    CHECK(st.guidanceOn());
    CpView v;
    CHECK(st.currentView(v));
    CHECK(v.valid);
    CHECK_EQ(v.kombi_dir, kombi::DIR_LEFT);
    CHECK_STR(v.road, "Knez Mihailova");
    CHECK(v.has_dist);
    CHECK_STR(v.dist_text, "250");
    CHECK_EQ(v.kombi_unit, kombi::U_M);
    CHECK_EQ(st.diag().maneuver_count, 3);
    return 0;
}

// Old Waze build: supports=0 -> nothing drawn (fail-clean), and the readout says to update.
static int test_old_waze_fail_clean() {
    RgiState st;
    feed(st, update(iap2::RS_ROUTE_SET, "Waze", 0, 0, 250, 3));
    feed(st, maneuver(0, IM_RIGHT_TURN, "Terazije", 0));
    CHECK(!st.guidanceOn());
    CpView v;
    CHECK(!st.currentView(v));
    CHECK(!v.valid);
    // diagnostics still populated, and describe() explains why nothing shows
    CHECK(st.diag().source_supports_known);
    CHECK(!st.diag().source_supports);
    CHECK_STR(st.diag().source_name, "Waze");
    CHECK(std::strstr(st.describe().c_str(), "update") != 0);
    return 0;
}

// supports flag not received yet -> conservative: draw nothing until we know.
static int test_supports_unknown() {
    RgiState st;
    std::vector<uint8_t> p;
    t::u8(p, iap2::RGU_ROUTE_STATE, iap2::RS_ROUTE_SET);
    t::u16(p, iap2::RGU_MANEUVER_LIST, 0);
    std::vector<uint8_t> m = t::framed(iap2::MSG_RG_UPDATE, p);
    feed(st, m);
    feed(st, maneuver(0, IM_LEFT_TURN, "X", 0));
    CHECK(!st.diag().source_supports_known);
    CHECK(!st.guidanceOn());
    CpView v;
    CHECK(!st.currentView(v));
    return 0;
}

// A supported app but the route is still loading -> off until it is guiding.
static int test_loading_state_off() {
    RgiState st;
    feed(st, update(iap2::RS_LOADING, "Apple Maps", 1, 0, 250, 1));
    feed(st, maneuver(0, IM_LEFT_TURN, "X", 0));
    CHECK(!st.guidanceOn());
    return 0;
}

// Distance >= 1 km formats as one-decimal kilometres.
static int test_distance_km() {
    RgiState st;
    feed(st, update(iap2::RS_ROUTE_SET, "Apple Maps", 1, 0, 1500, 1));
    feed(st, maneuver(0, IM_RIGHT_TURN, "A1", 0));
    CpView v;
    CHECK(st.currentView(v));
    CHECK_STR(v.dist_text, "1.5");
    CHECK_EQ(v.kombi_unit, kombi::U_KM);
    return 0;
}

// Roundabout exit from the 28..46 maneuver types; RHT rotates counter-clockwise.
static int test_roundabout_exit_view() {
    RgiState st;
    feed(st, update(iap2::RS_ROUTE_SET, "Google Maps", 1, 0, 120, 1));
    feed(st, maneuver(0, /*ROUNDABOUT_EXIT_3*/30, "Vojvode Stepe", 0));
    CpView v;
    CHECK(st.currentView(v));
    CHECK_EQ(v.kombi_dir, kombi::DIR_ROUNDABOUT_CCW);
    CHECK_EQ(v.roundabout_exit, 3);
    return 0;
}

// Arrival draws the destination flag.
static int test_arrival_view() {
    RgiState st;
    feed(st, update(iap2::RS_ARRIVED, "Apple Maps", 1, 0, 0, 1));
    feed(st, maneuver(0, IM_ARRIVE_A, "", -1));
    CpView v;
    CHECK(st.currentView(v));
    CHECK(v.arrival);
    CHECK_EQ(v.kombi_dir, kombi::DIR_DESTINATION);
    return 0;
}

// StopRouteGuidance clears the route.
static int test_stop_resets() {
    RgiState st;
    feed(st, update(iap2::RS_ROUTE_SET, "Apple Maps", 1, 0, 250, 1));
    feed(st, maneuver(0, IM_LEFT_TURN, "X", 0));
    CHECK(st.guidanceOn());
    std::vector<uint8_t> stop = cp::iap2::buildStopRouteGuidance(0x001E);
    feed(st, stop);
    CHECK(!st.guidanceOn());
    return 0;
}

// iAP2 NUL-terminated strings (incl. 1-byte "\0" empty string) + 0x5202 initialDistance fallback.
static int test_nul_strings_and_maneuver_distance_fallback() {
    RgiState st;
    // Build a 0x5201 where strings carry trailing NULs (as on the real iAP2 wire) and 0x000A is omitted.
    std::vector<uint8_t> u;
    t::u8(u, iap2::RGU_ROUTE_STATE, iap2::RS_ROUTE_SET);
    const char src_nul[] = "Apple Maps";
    iap2::putTlv(u, iap2::RGU_SOURCE_NAME, (const uint8_t*)src_nul, sizeof(src_nul)); // includes '\0'
    const char rd_nul[] = "Vidakoviceva";
    iap2::putTlv(u, iap2::RGU_CURRENT_ROAD, (const uint8_t*)rd_nul, sizeof(rd_nul));  // includes '\0'
    t::u8(u, iap2::RGU_SOURCE_SUPPORTS, 1);
    t::u16(u, iap2::RGU_MANEUVER_LIST, 0);
    feed(st, t::framed(iap2::MSG_RG_UPDATE, u));

    // describe() must not be truncated by an embedded NUL after "Apple Maps".
    CHECK(std::strstr(st.describe().c_str(), "supports=yes") != 0);

    // Build a 0x5202 with a 1-byte "\0" empty AfterManeuverRoadName and a 350 m initialDistance (0x0005).
    std::vector<uint8_t> m;
    t::u16(m, iap2::RGM_INDEX, 0);
    t::u8(m, iap2::RGM_TYPE, IM_RIGHT_TURN);
    const uint8_t empty_nul[1] = { 0 };
    iap2::putTlv(m, iap2::RGM_AFTER_ROAD, empty_nul, 1);
    t::u32(m, iap2::RGM_DIST_BETWEEN, 350);
    feed(st, t::framed(iap2::MSG_RG_MANEUVER, m));

    CpView v;
    CHECK(st.currentView(v));
    // Empty NUL after-road must fall back to current_road ("Vidakoviceva"), never become "\0" / "".
    CHECK_STR(v.road, "Vidakoviceva");
    // Distance falls back to 0x5202 RGM_DIST_BETWEEN (350 m) when 0x5201 0x000A hasn't arrived yet.
    CHECK(v.has_dist);
    CHECK_STR(v.dist_text, "350");
    CHECK_EQ(v.kombi_unit, kombi::U_M);
    return 0;
}

int main() {
    test_supported_app_draws();
    test_old_waze_fail_clean();
    test_supports_unknown();
    test_loading_state_off();
    test_distance_km();
    test_roundabout_exit_view();
    test_arrival_view();
    test_stop_resets();
    test_nul_strings_and_maneuver_distance_fallback();
    TEST_RETURN();
}
