// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI (NBT Evo ID6). iAP2 Route Guidance wire format.
//
// Parses the iAP2 control-session Route Guidance messages that iOS CarPlay sends once the head unit
// subscribes with 0x5200. Pure, no I/O, strict C++98 (QNX GCC 4.4.2 / gnu++98 clean).
//
// Message framing (big-endian), from the on-unit mm-ipod slog and the MHI2Q RE notes:
//   control-session message : 40 40 | len(BE16, whole message, >=6) | msgid(BE16) | params...
//   parameter (TLV)         : len(BE16, incl. the 4-byte header) | id(BE16) | value...
//
// Route Guidance message IDs:
//   0x5200 StartRouteGuidanceUpdates           HU -> phone (we inject it)
//   0x5201 RouteGuidanceUpdate                 phone -> HU
//   0x5202 RouteGuidanceManeuverUpdate         phone -> HU
//   0x5203 StopRouteGuidanceUpdates            HU -> phone
//   0x5204 RouteGuidanceLaneGuidanceInformation phone -> HU
//
// Field IDs below are from Apple's accessoryd enums as recovered by luka-dev/mib2q-carplay-rgi
// (docs/cluster-and-rgi/IAP2_ROUTE_GUIDANCE_TLVS.md) and woshiwangshuang/DiPlay-Lynk. The 0x5204 lane
// layout is best-effort and marked for on-car confirmation (project phase P0/P4).
#ifndef CP_IAP2_RGI_HPP
#define CP_IAP2_RGI_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace cp {
namespace iap2 {

// ---- message IDs -----------------------------------------------------------------------------------
const uint16_t MSG_START_RG        = 0x5200;
const uint16_t MSG_RG_UPDATE       = 0x5201;
const uint16_t MSG_RG_MANEUVER     = 0x5202;
const uint16_t MSG_STOP_RG         = 0x5203;
const uint16_t MSG_RG_LANE         = 0x5204;

// ---- 0x5201 RouteGuidanceUpdate field IDs ----------------------------------------------------------
const uint16_t RGU_ROUTE_STATE       = 0x0001; // u8, see RouteState
const uint16_t RGU_MANEUVER_STATE    = 0x0002; // u8
const uint16_t RGU_CURRENT_ROAD      = 0x0003; // utf8
const uint16_t RGU_DESTINATION       = 0x0004; // utf8
const uint16_t RGU_ETA               = 0x0005; // (skipped)
const uint16_t RGU_TIME_REMAINING    = 0x0006; // (skipped)
const uint16_t RGU_DIST_REMAINING    = 0x0007; // numeric metres to destination
const uint16_t RGU_DIST_REM_STR      = 0x0008; // utf8 (we format ourselves)
const uint16_t RGU_DIST_REM_UNITS    = 0x0009; // u8   (we format ourselves)
const uint16_t RGU_DIST_MANEUVER     = 0x000A; // numeric metres to next maneuver
const uint16_t RGU_DIST_MAN_STR      = 0x000B; // utf8
const uint16_t RGU_DIST_MAN_UNITS    = 0x000C; // u8
const uint16_t RGU_MANEUVER_LIST     = 0x000D; // current maneuver index list
const uint16_t RGU_MANEUVER_COUNT    = 0x000E; // u16
const uint16_t RGU_SHOWN_IN_APP      = 0x000F; // u8 (not route-active)
const uint16_t RGU_LANE_CUR_INDEX    = 0x0010; // u16
const uint16_t RGU_LANE_TOTAL        = 0x0011; // u16
const uint16_t RGU_LANE_SHOWING      = 0x0012; // u8
const uint16_t RGU_SOURCE_NAME       = 0x0013; // utf8 (nav app name)
const uint16_t RGU_SOURCE_SUPPORTS   = 0x0014; // u8  <-- the on/off gate

// ---- 0x5202 RouteGuidanceManeuverUpdate field IDs --------------------------------------------------
const uint16_t RGM_INDEX             = 0x0001; // u16
const uint16_t RGM_DESCRIPTION       = 0x0002; // utf8
const uint16_t RGM_TYPE              = 0x0003; // u8 0..53 (see cp_maneuver_map)
const uint16_t RGM_AFTER_ROAD        = 0x0004; // utf8
const uint16_t RGM_DIST_BETWEEN      = 0x0005; // numeric
const uint16_t RGM_DIST_STR          = 0x0006; // utf8
const uint16_t RGM_DIST_UNITS        = 0x0007; // u8
const uint16_t RGM_DRIVING_SIDE      = 0x0008; // u8 0 RHT, 1 LHT
const uint16_t RGM_JUNCTION_TYPE     = 0x0009; // u8 0 intersection, 1 roundabout
const uint16_t RGM_JUNCTION_ANGLE    = 0x000A; // i16 array
const uint16_t RGM_EXIT_ANGLE        = 0x000B; // i16
const uint16_t RGM_LINKED_LANE       = 0x000C; // group
const uint16_t RGM_EXIT_INFO         = 0x000D; // group

// RouteGuidanceState (0x5201 / 0x0001) values.
enum RouteState {
    RS_NO_ROUTE   = 0,
    RS_ROUTE_SET  = 1,
    RS_ARRIVED    = 2,
    RS_LOADING    = 3,
    RS_LOCATING   = 4,
    RS_REROUTING  = 5,
    RS_PROCEED    = 6
};

// ---- big-endian helpers ----------------------------------------------------------------------------
inline uint16_t rdBE16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}
inline int16_t rdBE16s(const uint8_t* p) {
    return (int16_t)rdBE16(p);
}
inline void wrBE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)(v & 0xFF));
}

// ---- one TLV parameter -----------------------------------------------------------------------------
struct Tlv {
    uint16_t id;
    const uint8_t* data;
    size_t len;
    Tlv() : id(0), data(0), len(0) {}
};

// Walks the TLV parameters of a message body (the bytes after msgid). Robust against truncation:
// stops cleanly and sets ok()=false on a malformed length.
class TlvReader {
public:
    TlvReader(const uint8_t* p, size_t n) : p_(p), end_(p + n), ok_(true) {}
    bool next(Tlv& out) {
        if (p_ + 4 > end_) return false;
        uint16_t len = rdBE16(p_);
        if (len < 4 || p_ + len > end_) { ok_ = false; return false; }
        out.id = rdBE16(p_ + 2);
        out.data = p_ + 4;
        out.len = (size_t)(len - 4);
        p_ += len;
        return true;
    }
    bool ok() const { return ok_; }
private:
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_;
};

// Finds the first TLV with the given id. Returns false if absent.
inline bool tlvFind(const uint8_t* body, size_t len, uint16_t id, Tlv& out) {
    TlvReader r(body, len);
    Tlv t;
    while (r.next(t)) {
        if (t.id == id) { out = t; return true; }
    }
    return false;
}

// Reads an unsigned integer TLV (1, 2, 4 or 8 bytes, big-endian). Returns false if absent or wrong size.
inline bool tlvU(const uint8_t* body, size_t len, uint16_t id, uint32_t& v) {
    Tlv t;
    if (!tlvFind(body, len, id, t)) return false;
    if (t.len == 1) { v = t.data[0]; return true; }
    if (t.len == 2) { v = rdBE16(t.data); return true; }
    if (t.len == 4) {
        v = (uint32_t)t.data[0] << 24 | (uint32_t)t.data[1] << 16 |
            (uint32_t)t.data[2] << 8  | (uint32_t)t.data[3];
        return true;
    }
    if (t.len == 8) {
        v = (uint32_t)t.data[4] << 24 | (uint32_t)t.data[5] << 16 |
            (uint32_t)t.data[6] << 8  | (uint32_t)t.data[7];
        return true;
    }
    return false;
}

// Reads a signed 16-bit TLV. Returns false if absent or not 2 bytes.
inline bool tlvI16(const uint8_t* body, size_t len, uint16_t id, int& v) {
    Tlv t;
    if (!tlvFind(body, len, id, t)) return false;
    if (t.len != 2) return false;
    v = rdBE16s(t.data);
    return true;
}

// Reads a UTF-8 string TLV. iAP2 string TLVs include a trailing NUL byte (an empty iAP2 string has len=1
// containing "\0"), so stop at the first NUL and replace embedded CR/LF with " / ".
inline std::string tlvStr(const uint8_t* body, size_t len, uint16_t id) {
    Tlv t;
    if (!tlvFind(body, len, id, t)) return std::string();
    std::string out;
    for (size_t i = 0; i < t.len && t.data[i] != 0; ++i) {
        if (t.data[i] == '\r' || t.data[i] == '\n') {
            if (t.data[i] == '\r' && i + 1 < t.len && t.data[i + 1] == '\n') ++i;
            if (!out.empty()) out += " / ";
        } else {
            out.push_back((char)t.data[i]);
        }
    }
    return out;
}

// ---- control-session message framing ---------------------------------------------------------------
// Parses a 40 40-framed control-session message. On success sets msgid and the param slice.
inline bool parseControlMessage(const uint8_t* buf, size_t n,
                                uint16_t& msgid, const uint8_t*& params, size_t& params_len) {
    if (n < 6) return false;
    if (buf[0] != 0x40 || buf[1] != 0x40) return false;
    uint16_t len = rdBE16(buf + 2);
    if (len < 6 || (size_t)len > n) return false;
    msgid = rdBE16(buf + 4);
    params = buf + 6;
    params_len = (size_t)(len - 6);
    return true;
}

// Builds a 40 40-framed control-session message from a msgid and already-encoded params.
inline std::vector<uint8_t> buildControlMessage(uint16_t msgid, const std::vector<uint8_t>& params) {
    std::vector<uint8_t> out;
    uint16_t total = (uint16_t)(6 + params.size());
    out.push_back(0x40); out.push_back(0x40);
    wrBE16(out, total);
    wrBE16(out, msgid);
    for (size_t i = 0; i < params.size(); ++i) out.push_back(params[i]);
    return out;
}

// Appends one TLV (id + value) to a param buffer.
inline void putTlv(std::vector<uint8_t>& params, uint16_t id, const uint8_t* val, size_t vlen) {
    uint16_t len = (uint16_t)(4 + vlen);
    wrBE16(params, len);
    wrBE16(params, id);
    for (size_t i = 0; i < vlen; ++i) params.push_back(val[i]);
}
inline void putTlvU8(std::vector<uint8_t>& params, uint16_t id, uint8_t v) {
    putTlv(params, id, &v, 1);
}
inline void putTlvEmpty(std::vector<uint8_t>& params, uint16_t id) {
    putTlv(params, id, 0, 0);
}

// Builds the 0x5200 StartRouteGuidanceUpdates request that subscribes to route guidance.
// componentID + request source-name (0x01) + supports-RG (0x02) + exit-info (0x03), matching the
// MHI2Q request shape. The phone then starts sending 0x5201/0x5202/0x5204.
inline std::vector<uint8_t> buildStartRouteGuidance(uint16_t component_id) {
    std::vector<uint8_t> params;
    uint8_t cid[2]; cid[0] = (uint8_t)(component_id >> 8); cid[1] = (uint8_t)(component_id & 0xFF);
    putTlv(params, 0x0000, cid, 2); // component ID
    putTlvEmpty(params, 0x0001);    // request source name in 0x5201/0x13
    putTlvEmpty(params, 0x0002);    // request source-supports-RG in 0x5201/0x14
    putTlvEmpty(params, 0x0003);    // request exit info in 0x5202/0x0D
    return buildControlMessage(MSG_START_RG, params);
}

// Builds the 0x5203 StopRouteGuidanceUpdates request.
inline std::vector<uint8_t> buildStopRouteGuidance(uint16_t component_id) {
    std::vector<uint8_t> params;
    uint8_t cid[2]; cid[0] = (uint8_t)(component_id >> 8); cid[1] = (uint8_t)(component_id & 0xFF);
    putTlv(params, 0x0000, cid, 2);
    return buildControlMessage(MSG_STOP_RG, params);
}

} // namespace iap2
} // namespace cp

#endif
