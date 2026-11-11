// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests the C ABI bridge (hook/cp_bridge.*) the way the hook feeds it on the car: decoded control-session
// messages (msgid + params) arrive from the driver's receive seam, and the worker reads cp_bridge_current().
#include "../hook/cp_bridge.h"
#include "../src/iap2_rgi.hpp"
#include "../src/cp_maneuver_map.hpp"
#include "../tests/test_util.hpp"

#include <string.h>
#include <vector>

using namespace cp;

// Build a 0x5201 param block (not framed: the bridge takes params after the msgid, as the driver seam delivers).
static std::vector<uint8_t> upd(int state, const char* src, int supports, int curIndex, int distM) {
    std::vector<uint8_t> p;
    iap2::putTlvU8(p, iap2::RGU_ROUTE_STATE, (uint8_t)state);
    if (src) iap2::putTlv(p, iap2::RGU_SOURCE_NAME, (const uint8_t*)src, strlen(src));
    if (supports >= 0) iap2::putTlvU8(p, iap2::RGU_SOURCE_SUPPORTS, (uint8_t)supports);
    if (curIndex >= 0) { uint8_t b[2] = { (uint8_t)(curIndex >> 8), (uint8_t)curIndex };
                         iap2::putTlv(p, iap2::RGU_MANEUVER_LIST, b, 2); }
    if (distM >= 0) { uint8_t b[4] = { 0,0, (uint8_t)(distM>>8), (uint8_t)distM };
                      iap2::putTlv(p, iap2::RGU_DIST_MANEUVER, b, 4); }
    return p;
}
static std::vector<uint8_t> man(int index, int type, const char* road, int side) {
    std::vector<uint8_t> p;
    uint8_t ib[2] = { (uint8_t)(index >> 8), (uint8_t)index };
    iap2::putTlv(p, iap2::RGM_INDEX, ib, 2);
    iap2::putTlvU8(p, iap2::RGM_TYPE, (uint8_t)type);
    if (road) iap2::putTlv(p, iap2::RGM_AFTER_ROAD, (const uint8_t*)road, strlen(road));
    if (side >= 0) iap2::putTlvU8(p, iap2::RGM_DRIVING_SIDE, (uint8_t)side);
    return p;
}
static int feed(uint16_t id, const std::vector<uint8_t>& p) {
    return cp_bridge_on_message(id, p.empty() ? 0 : &p[0], p.size());
}

static int test_apple_maps_drive() {
    cp_bridge_reset();
    // supported app, guiding, left turn 250 m into Knez Mihailova
    CHECK_EQ(feed(0x5201, upd(iap2::RS_ROUTE_SET, "Apple Maps", 1, 0, 250)), 1);
    CHECK_EQ(feed(0x5202, man(0, IM_LEFT_TURN, "Knez Mihailova", 0)), 1);
    CHECK(cp_bridge_guidance_on());
    cp_maneuver_t m;
    cp_bridge_current(&m);
    CHECK(m.valid);
    CHECK_EQ(m.direction, kombi::DIR_LEFT);
    CHECK_STR(std::string(m.road), "Knez Mihailova");
    CHECK(m.has_distance);
    CHECK_STR(std::string(m.dist_text), "250");
    CHECK_EQ(m.unit, kombi::U_M);
    return 0;
}

static int test_old_waze_nothing() {
    cp_bridge_reset();
    feed(0x5201, upd(iap2::RS_ROUTE_SET, "Waze", 0, 0, 250));   // supports = 0
    feed(0x5202, man(0, IM_RIGHT_TURN, "Terazije", 0));
    CHECK(!cp_bridge_guidance_on());
    cp_maneuver_t m;
    cp_bridge_current(&m);
    CHECK(!m.valid);
    CHECK(strstr(cp_bridge_describe(), "Waze") != 0);
    return 0;
}

static int test_non_rg_ignored() {
    cp_bridge_reset();
    // a NowPlaying id must be reported as "not route guidance" (0) and must not affect state
    std::vector<uint8_t> p; p.push_back(0); p.push_back(0);
    CHECK_EQ(feed(0x5001, p), 0);
    CHECK(!cp_bridge_guidance_on());
    return 0;
}

static int test_reroute_then_arrive() {
    cp_bridge_reset();
    feed(0x5201, upd(iap2::RS_ROUTE_SET, "Google Maps", 1, 0, 1500));
    feed(0x5202, man(0, IM_RIGHT_TURN, "E-75", 0));
    cp_maneuver_t m; cp_bridge_current(&m);
    CHECK_STR(std::string(m.dist_text), "1.5");
    CHECK_EQ(m.unit, kombi::U_KM);
    // arrival
    feed(0x5201, upd(iap2::RS_ARRIVED, 0, -1, 0, 0));
    feed(0x5202, man(0, IM_ARRIVE_A, "", -1));
    cp_bridge_current(&m);
    CHECK(m.valid);
    CHECK(m.arrival);
    CHECK_EQ(m.direction, kombi::DIR_DESTINATION);
    // stop clears
    feed(0x5203, std::vector<uint8_t>());
    CHECK(!cp_bridge_guidance_on());
    return 0;
}

int main() {
    test_apple_maps_drive();
    test_old_waze_nothing();
    test_non_rg_ignored();
    test_reroute_then_arrive();
    TEST_RETURN();
}
