// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Offline end-to-end test of the transport-read capture path:
//   synthetic 0x5201/0x5202 iAP2 link packets -> cp_scan_incoming offset logic -> cp::RgiState -> KOMBI view.
// Proves that a route-guidance link packet, parsed with the hook's transport-read offsets, yields a maneuver.
#include "../src/cp_rgi_state.hpp"
#include "iap2_build.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

static uint8_t linkcksum(const uint8_t* p, size_t n) {
    uint8_t s = 0; for (size_t i = 0; i < n; ++i) s = (uint8_t)(s + p[i]); return (uint8_t)(-(int)s);
}

// Wrap a control message (starts 40 40) in an FF5A link packet, exactly like the driver's transport write.
static std::vector<uint8_t> link_wrap(const std::vector<uint8_t>& ctrl) {
    std::vector<uint8_t> pkt;
    size_t total = 9 + ctrl.size() + 1;
    pkt.resize(total);
    pkt[0] = 0xFF; pkt[1] = 0x5A;
    pkt[2] = (uint8_t)(total >> 8); pkt[3] = (uint8_t)(total & 0xFF);
    pkt[4] = 0x40; pkt[5] = 0x71; pkt[6] = 0xFD; pkt[7] = 0x01;
    pkt[8] = linkcksum(&pkt[0], 8);
    memcpy(&pkt[9], &ctrl[0], ctrl.size());
    pkt[total - 1] = linkcksum(&pkt[9], ctrl.size());
    return pkt;
}

// Replica of cp_scan_incoming's per-packet extraction: returns (msgid, params, plen) for control packets.
static int scan_one(const uint8_t* buf, size_t rc, uint16_t* out_msgid, const uint8_t** out_params, size_t* out_plen) {
    size_t i = 0;
    while (i + 15 <= rc) {
        if (buf[i] == 0xFF && buf[i + 1] == 0x5A) {
            size_t len = ((size_t)buf[i + 2] << 8) | buf[i + 3];
            if (len >= 9 && i + len <= rc) {
                if (len > 15 && buf[i + 9] == 0x40 && buf[i + 10] == 0x40) {
                    uint16_t msgid = (uint16_t)((buf[i + 13] << 8) | buf[i + 14]);
                    size_t mlen = ((size_t)buf[i + 11] << 8) | buf[i + 12];
                    if (mlen >= 6 && i + 9 + mlen <= rc) {
                        *out_msgid = msgid; *out_params = buf + i + 15; *out_plen = mlen - 6;
                        return 1;
                    }
                }
                i += len; continue;
            }
        }
        ++i;
    }
    return 0;
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

int main() {
    cp::RgiState st;

    // --- 0x5201 RouteGuidanceUpdate: guiding, app supports RG, road + distance + maneuver list ---
    std::vector<uint8_t> p1;
    t::u8 (p1, cp::iap2::RGU_ROUTE_STATE,     cp::iap2::RS_ROUTE_SET);   // guiding
    t::u8 (p1, cp::iap2::RGU_SOURCE_SUPPORTS, 1);                        // the on/off gate
    t::str(p1, cp::iap2::RGU_CURRENT_ROAD,    "Knez Mihailova");
    t::u32(p1, cp::iap2::RGU_DIST_MANEUVER,   300);                      // 300 m to next turn
    t::u16(p1, cp::iap2::RGU_MANEUVER_COUNT,  1);
    { std::vector<uint8_t> list; uint8_t b[2] = {0,0}; cp::iap2::putTlv(list, cp::iap2::RGU_MANEUVER_LIST, b, 2);
      p1.insert(p1.end(), list.begin(), list.end()); }
    t::str(p1, cp::iap2::RGU_SOURCE_NAME,     "Apple Maps");
    std::vector<uint8_t> link1 = link_wrap(t::framed(cp::iap2::MSG_RG_UPDATE, p1));

    // --- 0x5202 ManeuverUpdate: index 0, LEFT turn, into "Main St" ---
    std::vector<uint8_t> p2;
    t::u16(p2, cp::iap2::RGM_INDEX,      0);
    t::u8 (p2, cp::iap2::RGM_TYPE,       1);   // 1 = left turn (cp_maneuver_map)
    t::str(p2, cp::iap2::RGM_AFTER_ROAD, "Terazije");
    std::vector<uint8_t> link2 = link_wrap(t::framed(cp::iap2::MSG_RG_MANEUVER, p2));

    // Prepend an ACK (n=9, no control payload) and concatenate both packets into one read buffer,
    // exactly the kind of multi-packet chunk the transport read() can return.
    std::vector<uint8_t> ack(9);
    ack[0]=0xFF; ack[1]=0x5A; ack[2]=0; ack[3]=9; ack[4]=0x40; ack[5]=0x9B; ack[6]=0x91; ack[7]=0x01;
    ack[8]=linkcksum(&ack[0],8);

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), ack.begin(),   ack.end());
    buf.insert(buf.end(), link1.begin(), link1.end());

    // Feed packet 1
    uint16_t mid; const uint8_t* par; size_t pl;
    CHECK(scan_one(&buf[0], buf.size(), &mid, &par, &pl) == 1);
    CHECK(mid == 0x5201);
    CHECK(st.onMessage(mid, par, pl) == true);

    // Feed packet 2 (separate read)
    CHECK(scan_one(&link2[0], link2.size(), &mid, &par, &pl) == 1);
    CHECK(mid == 0x5202);
    CHECK(st.onMessage(mid, par, pl) == true);

    // Now the bridge must report guidance ON with a LEFT maneuver, road "Terazije", ~300 m.
    CHECK(st.guidanceOn() == true);
    cp::CpView v;
    CHECK(st.currentView(v) == true);
    CHECK(v.valid == true);
    CHECK(v.kombi_dir == cp::kombi::DIR_LEFT);
    CHECK(v.road == "Terazije");
    CHECK(v.has_dist == true);
    printf("captured: dir=%d road='%s' dist=%lldm text='%s' unit=%d guidanceOn=%d\n",
           v.kombi_dir, v.road.c_str(), (long long)v.dist_m, v.dist_text.c_str(), v.kombi_unit, st.guidanceOn());

    // Negative control: if the app does NOT support RG (0x14=0), guidance must be OFF (fail-clean).
    cp::RgiState st2;
    std::vector<uint8_t> p3;
    t::u8(p3, cp::iap2::RGU_ROUTE_STATE,     cp::iap2::RS_ROUTE_SET);
    t::u8(p3, cp::iap2::RGU_SOURCE_SUPPORTS, 0);   // app does not send metadata (e.g. old Waze)
    std::vector<uint8_t> link3 = link_wrap(t::framed(cp::iap2::MSG_RG_UPDATE, p3));
    CHECK(scan_one(&link3[0], link3.size(), &mid, &par, &pl) == 1);
    st2.onMessage(mid, par, pl);
    CHECK(st2.guidanceOn() == false);

    printf(fails ? "\n%d CHECK(s) FAILED\n" : "\nALL CHECKS PASSED (transport-read capture -> KOMBI view works)\n", fails);
    return fails ? 1 : 0;
}
