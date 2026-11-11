// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Test-only helpers to build iAP2 Route Guidance messages (0x5201/0x5202/0x5204) byte buffers.
#ifndef IAP2_BUILD_HPP
#define IAP2_BUILD_HPP

#include "../src/iap2_rgi.hpp"
#include <string>
#include <vector>

namespace t {

inline void u8(std::vector<uint8_t>& params, uint16_t id, uint8_t v) {
    cp::iap2::putTlvU8(params, id, v);
}
inline void u16(std::vector<uint8_t>& params, uint16_t id, uint16_t v) {
    uint8_t b[2]; b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)(v & 0xFF);
    cp::iap2::putTlv(params, id, b, 2);
}
inline void u32(std::vector<uint8_t>& params, uint16_t id, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)(v & 0xFF);
    cp::iap2::putTlv(params, id, b, 4);
}
inline void i16(std::vector<uint8_t>& params, uint16_t id, int v) {
    uint16_t u = (uint16_t)(int16_t)v;
    uint8_t b[2]; b[0] = (uint8_t)(u >> 8); b[1] = (uint8_t)(u & 0xFF);
    cp::iap2::putTlv(params, id, b, 2);
}
inline void str(std::vector<uint8_t>& params, uint16_t id, const std::string& s) {
    cp::iap2::putTlv(params, id, (const uint8_t*)s.data(), s.size());
}
inline std::vector<uint8_t> framed(uint16_t msgid, const std::vector<uint8_t>& params) {
    return cp::iap2::buildControlMessage(msgid, params);
}

} // namespace t

#endif
