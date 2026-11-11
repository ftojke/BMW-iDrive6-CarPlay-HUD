/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI (NBT Evo ID6): iAP2 link-packet helpers + IdentificationInformation rewrite.
 *
 * Pure C99, no allocation, no libc beyond memcpy/memmove/memcmp/strlen. Used inside mm-ipod (LD_PRELOAD hook,
 * which must not pull libstdc++) and by the host/QNX unit tests.
 *
 * iAP2 link packet (all big-endian):
 *   [0..1] FF 5A sync   [2..3] packet length (whole packet)   [4] control   [5] seq   [6] ack   [7] session
 *   [8] header checksum = -(sum of bytes 0..7)
 *   [9 .. n-2] payload  [n-1] payload checksum = -(sum of payload bytes)      (no payload => n == 9)
 * Control-session payload: 40 40 | msg length (whole message) | msg id | params
 * Param (TLV): length (incl. the 4-byte header) | id | value
 */
#ifndef CP_IDENT_PATCH_H
#define CP_IDENT_PATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP_LINK_HDR_LEN        9
#define CP_IAP2_IDENT_INFO     0x1D01

/* Route-guidance additions (identification param ids / message ids). */
#define CP_IDENT_MSGS_SENT     0x0006   /* MessagesSentByAccessory     */
#define CP_IDENT_MSGS_RECV     0x0007   /* MessagesReceivedFromDevice  */
#define CP_IDENT_RG_COMPONENT  0x001E   /* RouteGuidanceDisplayComponent */
#define CP_RG_COMPONENT_ID     0x0040   /* our component id. MUST be unique across ALL identify components.
                                         * On BMW NBT, EA-protocol ids 0x00..0x10 are taken (com.bmw.carplay=0x10),
                                         * and 0x04D2/0x08AE/0x0D05 by location/BT/vehicle. 0x0040 is free. The
                                         * reference's 0x0010 collided with com.bmw.carplay -> phone ignored it. */

/* Worst-case growth of an Identify rewrite (2 + 3 message ids + the component). */
#define CP_IDENT_PATCH_GROWTH  128

/* Negated 8-bit sum (iAP2 link checksum). */
uint8_t cp_link_cksum(const uint8_t *p, size_t n);

/* 1 if pkt[0..n) is one complete, well-formed iAP2 link packet (sync, length == n, both checksums). */
int cp_link_is_valid(const uint8_t *pkt, size_t n);

/* If pkt is a valid link packet whose payload is one complete control-session message, returns the message id
 * (0..0xFFFF) and sets *params / *params_len. Otherwise returns -1. */
int cp_link_ctrl_msg(const uint8_t *pkt, size_t n, const uint8_t **params, size_t *params_len);

/* Rewrites an IdentificationInformation (0x1D01) link packet so it declares iAP2 route guidance:
 *   MessagesSentByAccessory    += 0x5200 0x5203
 *   MessagesReceivedFromDevice += 0x5201 0x5202 0x5204
 *   + RouteGuidanceDisplayComponent (param 0x1E) with component id `component_id`
 * Header fields (control/seq/ack/session) are kept; lengths and both checksums are recomputed.
 * Idempotent: ids / component already present are not added again.
 * Returns the new packet length (written to out), 0 if pkt is not an Identify packet (pass it through
 * unchanged), or -1 if it is an Identify packet that cannot be rewritten safely (malformed, missing the
 * message lists, or out_cap too small) - also pass through unchanged. */
int cp_ident_patch(const uint8_t *pkt, size_t n, uint8_t *out, size_t out_cap, uint16_t component_id);

#ifdef __cplusplus
}
#endif

#endif
