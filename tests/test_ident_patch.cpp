/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Tests for the iAP2 Identify rewrite (hook/ident_patch.c) against a reconstructed real 0x1D01 packet.
 */
#include "../hook/ident_patch.h"
#include "../tests/test_util.hpp"
#include <string.h>
#include <vector>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

/* one id-list param (MessagesSent/Received): len|id|value(2-byte msg ids) */
static void put_idlist(std::vector<uint8_t>& p, uint16_t id, const uint16_t* ids, size_t n) {
    uint16_t len = (uint16_t)(4 + 2 * n);
    wr16(p, len); wr16(p, id);
    for (size_t i = 0; i < n; ++i) wr16(p, ids[i]);
}
static void put_str(std::vector<uint8_t>& p, uint16_t id, const char* s) {
    uint16_t len = (uint16_t)(4 + strlen(s) + 1);
    wr16(p, len); wr16(p, id);
    p.insert(p.end(), (const uint8_t*)s, (const uint8_t*)s + strlen(s) + 1);
}

/* Wrap control-session params into a full FF 5A link packet. */
static std::vector<uint8_t> link_wrap(uint16_t msgid, const std::vector<uint8_t>& params,
                                      uint8_t ctrl, uint8_t seq, uint8_t ack, uint8_t sess) {
    std::vector<uint8_t> msg;
    uint16_t mlen = (uint16_t)(6 + params.size());
    msg.push_back(0x40); msg.push_back(0x40);
    wr16(msg, mlen); wr16(msg, msgid);
    msg.insert(msg.end(), params.begin(), params.end());

    std::vector<uint8_t> pkt;
    uint16_t total = (uint16_t)(9 + msg.size() + 1);
    pkt.push_back(0xFF); pkt.push_back(0x5A);
    wr16(pkt, total);
    pkt.push_back(ctrl); pkt.push_back(seq); pkt.push_back(ack); pkt.push_back(sess);
    pkt.push_back(cp_link_cksum(&pkt[0], 8));
    pkt.insert(pkt.end(), msg.begin(), msg.end());
    pkt.push_back(cp_link_cksum(&pkt[9], msg.size()));
    return pkt;
}

/* Reconstructs the CarPlay WiFi-instance IdentificationInformation (the 880-byte packet captured on the car,
 * p0/.../identify_1D01.txt), with the fields that matter to the patch. */
static std::vector<uint8_t> real_identify() {
    std::vector<uint8_t> p;
    put_str(p, 0x0000, "BMW 00000");                 // Name
    put_str(p, 0x0001, "G30-NBTEvo-0716");           // ModelIdentifier
    put_str(p, 0x0002, "BMW");                        // Manufacturer
    put_str(p, 0x0003, "0H0000000");                  // SerialNumber
    put_str(p, 0x0004, "NBTevo_Y21432J");             // FirmwareVersion
    put_str(p, 0x0005, "03");                         // HardwareVersion
    {
        const uint16_t sent[] = {0xEA02,0x5000,0x5002,0x5003,0xAE00,0xAE02,0xFFFB,0x4154,0x4156,0x4157,
                                 0x4159,0x415A,0x415B,0x415C,0x415D,0x415E,0x415F,0x4160,0x4161,0x5700,
                                 0x5703,0x10DA,0x10DC,0x10DD};
        put_idlist(p, 0x0006, sent, sizeof(sent)/2);
    }
    {
        const uint16_t recv[] = {0xEA00,0xEA01,0x5001,0xAE01,0xFFFA,0xFFFC,0x4155,0x4158,0x4E09,0x4E0A,
                                 0x4E0B,0x4E0C,0x4E0D,0x5701,0x5702,0x10DB};
        put_idlist(p, 0x0007, recv, sizeof(recv)/2);
    }
    put_str(p, 0x000C, "en");                         // CurrentLanguage
    put_str(p, 0x000D, "en");                         // SupportedLanguage
    put_str(p, 0x000D, "fr");
    return p;
}

/* Decode helpers for assertions (walk the patched packet's params). */
static const uint8_t* find_param(const uint8_t* params, size_t len, uint16_t id, uint16_t* tlv_len) {
    size_t off = 0;
    while (off + 4 <= len) {
        uint16_t l = rd16(params + off);
        if (l < 4 || off + l > len) return 0;
        if (rd16(params + off + 2) == id) { if (tlv_len) *tlv_len = l; return params + off; }
        off += l;
    }
    return 0;
}
static bool list_has(const uint8_t* tlv, uint16_t id) {
    uint16_t l = rd16(tlv);
    for (size_t i = 4; i + 2 <= l; i += 2) if (rd16(tlv + i) == id) return true;
    return false;
}

static int test_passthrough_non_identify() {
    // A NowPlaying (0x5001) packet must be reported as "not identify" (0) and left for pass-through.
    std::vector<uint8_t> p;
    wr16(p, 10); wr16(p, 0x0001); // dummy param
    std::vector<uint8_t> pkt = link_wrap(0x5001, p, 0x40, 1, 2, 1);
    std::vector<uint8_t> out(pkt.size() + 256);
    int r = cp_ident_patch(&pkt[0], pkt.size(), &out[0], out.size(), CP_RG_COMPONENT_ID);
    CHECK_EQ(r, 0);
    return 0;
}

static int test_patch_real_identify() {
    std::vector<uint8_t> pkt = link_wrap(CP_IAP2_IDENT_INFO, real_identify(), 0x40, 0x0D, 0xE6, 1);
    CHECK(cp_link_is_valid(&pkt[0], pkt.size()));

    std::vector<uint8_t> out(pkt.size() + 256);
    int r = cp_ident_patch(&pkt[0], pkt.size(), &out[0], out.size(), CP_RG_COMPONENT_ID);
    CHECK(r > 0);
    out.resize(r);

    // The result is a valid link packet and a valid control message 0x1D01.
    CHECK(cp_link_is_valid(&out[0], out.size()));
    const uint8_t* params; size_t plen;
    CHECK_EQ(cp_link_ctrl_msg(&out[0], out.size(), &params, &plen), CP_IAP2_IDENT_INFO);

    // Header fields preserved.
    CHECK_EQ(out[4], 0x40); CHECK_EQ(out[5], 0x0D); CHECK_EQ(out[6], 0xE6); CHECK_EQ(out[7], 0x01);

    // MessagesSent now has 0x5200 and 0x5203; MessagesRecv has 0x5201/0x5202/0x5204.
    uint16_t sl = 0, rl = 0;
    const uint8_t* sent = find_param(params, plen, 0x0006, &sl);
    const uint8_t* recv = find_param(params, plen, 0x0007, &rl);
    CHECK(sent && recv);
    CHECK(list_has(sent, 0x5200)); CHECK(list_has(sent, 0x5203));
    CHECK(list_has(sent, 0xEA02));   // original ids still there
    CHECK(list_has(recv, 0x5201)); CHECK(list_has(recv, 0x5202)); CHECK(list_has(recv, 0x5204));
    CHECK(list_has(recv, 0x5001));

    // RouteGuidanceDisplayComponent (0x1E) present, with component id and the name.
    uint16_t cl = 0;
    const uint8_t* comp = find_param(params, plen, 0x001E, &cl);
    CHECK(comp != 0);
    // inner: 0x0000 identifier == component id
    CHECK_EQ(rd16(comp + 4), 6);           // inner tlv0 len
    CHECK_EQ(rd16(comp + 6), 0x0000);      // inner tlv0 id
    CHECK_EQ(rd16(comp + 8), CP_RG_COMPONENT_ID);

    // Growth is within the documented bound.
    CHECK((int)out.size() - (int)pkt.size() <= CP_IDENT_PATCH_GROWTH);
    return 0;
}

static int test_idempotent() {
    std::vector<uint8_t> pkt = link_wrap(CP_IAP2_IDENT_INFO, real_identify(), 0x40, 1, 2, 1);
    std::vector<uint8_t> a(pkt.size() + 256);
    int r1 = cp_ident_patch(&pkt[0], pkt.size(), &a[0], a.size(), CP_RG_COMPONENT_ID);
    CHECK(r1 > 0);
    a.resize(r1);
    // Patching the already-patched packet reports "nothing to do" (0).
    std::vector<uint8_t> b(a.size() + 256);
    int r2 = cp_ident_patch(&a[0], a.size(), &b[0], b.size(), CP_RG_COMPONENT_ID);
    CHECK_EQ(r2, 0);
    return 0;
}

static int test_small_out_buffer() {
    std::vector<uint8_t> pkt = link_wrap(CP_IAP2_IDENT_INFO, real_identify(), 0x40, 1, 2, 1);
    std::vector<uint8_t> out(pkt.size());   // too small
    int r = cp_ident_patch(&pkt[0], pkt.size(), &out[0], out.size(), CP_RG_COMPONENT_ID);
    CHECK_EQ(r, -1);
    return 0;
}

int main() {
    test_passthrough_non_identify();
    test_patch_real_identify();
    test_idempotent();
    test_small_out_buffer();
    TEST_RETURN();
}
