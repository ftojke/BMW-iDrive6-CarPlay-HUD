// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Tests for the iAP2 control-session message framing + TLV reader + 0x5200 builder.
#include "../src/iap2_rgi.hpp"
#include "iap2_build.hpp"
#include "test_util.hpp"

using namespace cp;

static int test_framing() {
    // Build a 0x5201 with a couple of TLVs, then parse it back.
    std::vector<uint8_t> params;
    t::u8(params, iap2::RGU_ROUTE_STATE, iap2::RS_ROUTE_SET);
    t::str(params, iap2::RGU_CURRENT_ROAD, "Bulevar");
    t::u8(params, iap2::RGU_SOURCE_SUPPORTS, 1);
    std::vector<uint8_t> msg = t::framed(iap2::MSG_RG_UPDATE, params);

    // Frame: 40 40 | len | 52 01 | params
    CHECK_EQ(msg[0], 0x40);
    CHECK_EQ(msg[1], 0x40);
    CHECK_EQ(iap2::rdBE16(&msg[2]), (int)msg.size());
    CHECK_EQ(iap2::rdBE16(&msg[4]), iap2::MSG_RG_UPDATE);

    uint16_t msgid = 0; const uint8_t* p = 0; size_t plen = 0;
    CHECK(iap2::parseControlMessage(&msg[0], msg.size(), msgid, p, plen));
    CHECK_EQ(msgid, iap2::MSG_RG_UPDATE);

    uint32_t u = 0;
    CHECK(iap2::tlvU(p, plen, iap2::RGU_ROUTE_STATE, u));
    CHECK_EQ(u, iap2::RS_ROUTE_SET);
    CHECK(iap2::tlvU(p, plen, iap2::RGU_SOURCE_SUPPORTS, u));
    CHECK_EQ(u, 1);
    CHECK_STR(iap2::tlvStr(p, plen, iap2::RGU_CURRENT_ROAD), "Bulevar");

    // Absent TLV -> false / empty.
    CHECK(!iap2::tlvU(p, plen, iap2::RGU_MANEUVER_COUNT, u));
    CHECK_STR(iap2::tlvStr(p, plen, iap2::RGU_DESTINATION), "");
    return 0;
}

static int test_tlv_reader_walk() {
    std::vector<uint8_t> params;
    t::u16(params, iap2::RGM_INDEX, 0);
    t::u8(params, iap2::RGM_TYPE, 2);
    t::i16(params, iap2::RGM_EXIT_ANGLE, -90);
    t::str(params, iap2::RGM_AFTER_ROAD, "Knez Mihailova");

    int count = 0;
    iap2::TlvReader r(&params[0], params.size());
    iap2::Tlv tv;
    while (r.next(tv)) ++count;
    CHECK(r.ok());
    CHECK_EQ(count, 4);

    int a = 0;
    CHECK(iap2::tlvI16(&params[0], params.size(), iap2::RGM_EXIT_ANGLE, a));
    CHECK_EQ(a, -90);
    return 0;
}

static int test_truncation() {
    // A declared length longer than the buffer must be rejected, not read out of bounds.
    std::vector<uint8_t> bad;
    cp::iap2::wrBE16(bad, 20);      // claims 20-byte TLV
    cp::iap2::wrBE16(bad, 0x0003);  // id
    bad.push_back(0x01);            // only 1 value byte present
    iap2::TlvReader r(&bad[0], bad.size());
    iap2::Tlv tv;
    CHECK(!r.next(tv));
    CHECK(!r.ok());
    return 0;
}

static int test_start_builder() {
    std::vector<uint8_t> msg = cp::iap2::buildStartRouteGuidance(0x001E);
    uint16_t msgid = 0; const uint8_t* p = 0; size_t plen = 0;
    CHECK(iap2::parseControlMessage(&msg[0], msg.size(), msgid, p, plen));
    CHECK_EQ(msgid, iap2::MSG_START_RG);
    // component id 0x001E present as the first TLV (id 0x0000).
    iap2::Tlv t0;
    CHECK(iap2::tlvFind(p, plen, 0x0000, t0));
    CHECK_EQ(t0.len, 2);
    CHECK_EQ(iap2::rdBE16(t0.data), 0x001E);
    // the three request flags are present as empty TLVs.
    iap2::Tlv t1;
    CHECK(iap2::tlvFind(p, plen, 0x0001, t1));
    CHECK_EQ(t1.len, 0);
    CHECK(iap2::tlvFind(p, plen, 0x0003, t1));
    return 0;
}

int main() {
    test_framing();
    test_tlv_reader_walk();
    test_truncation();
    test_start_builder();
    TEST_RETURN();
}
