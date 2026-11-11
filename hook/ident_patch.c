/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: iAP2 link helpers + IdentificationInformation rewrite. See ident_patch.h.
 */
#include "ident_patch.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }

uint8_t cp_link_cksum(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    size_t i;
    for (i = 0; i < n; ++i) s = (uint8_t)(s + p[i]);
    return (uint8_t)(-(int)s);
}

int cp_link_is_valid(const uint8_t *pkt, size_t n) {
    uint16_t len;
    if (!pkt || n < CP_LINK_HDR_LEN) return 0;
    if (pkt[0] != 0xFF || pkt[1] != 0x5A) return 0;
    len = rd16(pkt + 2);
    if (len != n) return 0;
    if (cp_link_cksum(pkt, 8) != pkt[8]) return 0;
    if (n == CP_LINK_HDR_LEN) return 1;                 /* header-only (ACK etc.) */
    if (cp_link_cksum(pkt + CP_LINK_HDR_LEN, n - CP_LINK_HDR_LEN - 1) != pkt[n - 1]) return 0;
    return 1;
}

int cp_link_ctrl_msg(const uint8_t *pkt, size_t n, const uint8_t **params, size_t *params_len) {
    const uint8_t *pl;
    size_t pll;
    uint16_t mlen;
    if (!cp_link_is_valid(pkt, n) || n <= CP_LINK_HDR_LEN) return -1;
    pl = pkt + CP_LINK_HDR_LEN;
    pll = n - CP_LINK_HDR_LEN - 1;                       /* minus the payload checksum */
    if (pll < 6 || pl[0] != 0x40 || pl[1] != 0x40) return -1;
    mlen = rd16(pl + 2);
    if (mlen < 6 || (size_t)mlen > pll) return -1;
    if (params) *params = pl + 6;
    if (params_len) *params_len = (size_t)(mlen - 6);
    return rd16(pl + 4);
}

/* Walk params; return pointer to the TLV with id, else NULL. *tlv_len gets its whole length. */
static const uint8_t *find_param(const uint8_t *params, size_t len, uint16_t id, uint16_t *tlv_len) {
    size_t off = 0;
    while (off + 4 <= len) {
        uint16_t l = rd16(params + off);
        if (l < 4 || off + l > len) return NULL;
        if (rd16(params + off + 2) == id) { if (tlv_len) *tlv_len = l; return params + off; }
        off += l;
    }
    return NULL;
}

/* True if the message-id list value (repeated 2-byte ids) already contains id. */
static int list_has_id(const uint8_t *val, size_t vlen, uint16_t id) {
    size_t i;
    for (i = 0; i + 2 <= vlen; i += 2)
        if (rd16(val + i) == id) return 1;
    return 0;
}

/* Builds the RouteGuidanceDisplayComponent param (id 0x1E), MHI2Q/MHI3 layout (9 inner TLVs).
 * Returns total param length. Needs >= 73 bytes. */
static size_t build_rg_component(uint8_t *out, uint16_t component_id) {
    static const char name[] = "RouteGuidanceDisplayComponent";
    size_t nlen = sizeof(name);              /* includes NUL, as the native component does */
    size_t inner = 6 + (4 + nlen) + 6 * 7;   /* id + name + seven u16 TLVs (ids 2..8) */
    size_t off = 0;
    wr16(out + off, (uint16_t)(4 + inner)); off += 2;
    wr16(out + off, CP_IDENT_RG_COMPONENT); off += 2;
    /* 0 Identifier */
    wr16(out + off, 6); off += 2; wr16(out + off, 0x0000); off += 2; wr16(out + off, component_id); off += 2;
    /* 1 Name */
    wr16(out + off, (uint16_t)(4 + nlen)); off += 2; wr16(out + off, 0x0001); off += 2;
    memcpy(out + off, name, nlen); off += nlen;
    /* 2..5 max lengths = 0x0100 */
    {
        uint16_t id;
        for (id = 0x0002; id <= 0x0005; ++id) {
            wr16(out + off, 6); off += 2; wr16(out + off, id); off += 2; wr16(out + off, 0x0100); off += 2;
        }
    }
    /* 6 MaxGuidanceManeuverStorageCapacity = 6 */
    wr16(out + off, 6); off += 2; wr16(out + off, 0x0006); off += 2; wr16(out + off, 0x0006); off += 2;
    /* 7 MaxLaneGuidanceDescriptionLength = 0x0100 */
    wr16(out + off, 6); off += 2; wr16(out + off, 0x0007); off += 2; wr16(out + off, 0x0100); off += 2;
    /* 8 MaxLaneGuidanceStorageCapacity = 6 */
    wr16(out + off, 6); off += 2; wr16(out + off, 0x0008); off += 2; wr16(out + off, 0x0006); off += 2;
    return off;
}

/* Appends the message ids in `add` (count `addc`) that are missing from the list TLV at `src`
 * (whole TLV) into `dst`, returning the new whole-TLV length. */
static size_t grow_id_list(uint8_t *dst, const uint8_t *src, const uint16_t *add, size_t addc) {
    uint16_t l = rd16(src);
    size_t vlen = l - 4, i, out = l;
    memcpy(dst, src, l);
    for (i = 0; i < addc; ++i) {
        if (!list_has_id(src + 4, vlen, add[i]) && !list_has_id(dst + 4, out - 4, add[i])) {
            wr16(dst + out, add[i]);
            out += 2;
        }
    }
    wr16(dst, (uint16_t)out);
    return out;
}

int cp_ident_patch(const uint8_t *pkt, size_t n, uint8_t *out, size_t out_cap, uint16_t component_id) {
    static const uint16_t sent_add[2] = { 0x5200, 0x5203 };
    static const uint16_t recv_add[3] = { 0x5201, 0x5202, 0x5204 };
    const uint8_t *params;
    size_t plen, off, mlen_off, new_plen, new_total;
    const uint8_t *p_sent, *p_recv;
    uint16_t sent_len = 0, recv_len = 0;
    uint8_t *w;
    int msgid = cp_link_ctrl_msg(pkt, n, &params, &plen);

    if (msgid != CP_IAP2_IDENT_INFO) return 0;          /* not Identify: pass through */

    p_sent = find_param(params, plen, CP_IDENT_MSGS_SENT, &sent_len);
    p_recv = find_param(params, plen, CP_IDENT_MSGS_RECV, &recv_len);
    if (!p_sent || !p_recv) return -1;                  /* unexpected shape: pass through */

    /* already patched? */
    if (find_param(params, plen, CP_IDENT_RG_COMPONENT, NULL) &&
        list_has_id(p_sent + 4, sent_len - 4, 0x5200) &&
        list_has_id(p_recv + 4, recv_len - 4, 0x5201))
        return 0;

    if (out_cap < n + CP_IDENT_PATCH_GROWTH) return -1;

    /* Copy header + payload header (40 40, msg len placeholder, msg id). */
    memcpy(out, pkt, CP_LINK_HDR_LEN + 6);
    mlen_off = CP_LINK_HDR_LEN + 2;                      /* control-session message length field */
    w = out + CP_LINK_HDR_LEN + 6;                      /* write cursor for params */

    /* Rewrite params in order, growing the two message-id lists. The RouteGuidanceDisplayComponent is NOT
     * inserted here; it is appended at the very end (below) so the IdentificationInformation parameters stay
     * in ascending-ID order. */
    off = 0;
    while (off + 4 <= plen) {
        uint16_t l = rd16(params + off);
        uint16_t id = rd16(params + off + 2);
        if (l < 4 || off + l > plen) return -1;
        if (id == CP_IDENT_MSGS_SENT) {
            w += grow_id_list(w, params + off, sent_add, 2);
        } else if (id == CP_IDENT_MSGS_RECV) {
            w += grow_id_list(w, params + off, recv_add, 3);
        } else {
            memcpy(w, params + off, l);
            w += l;
        }
        off += l;
    }
    /* Append RouteGuidanceDisplayComponent at the END, after every stock param. The stock Identify lists its
     * parameters in ascending ID order with the component params (0x0014/0x0016/0x0018) grouped at the tail;
     * 0x001E (30) sorts after 0x0018 (24), so appending keeps the order monotonic. The working reference
     * (mib2q-carplay-rgi rgd_hook.c:1263 and vehicle_hook.c:438) appends components the same way. Inserting
     * it mid-stream (right after 0x0007) put a component among the 0x0008..0x000D scalars, out of order, and
     * iOS accepted the Identify but did not enable route guidance. */
    w += build_rg_component(w, component_id);

    new_plen = (size_t)(w - (out + CP_LINK_HDR_LEN));    /* control-session message length */
    wr16(out + mlen_off, (uint16_t)new_plen);

    /* payload checksum */
    out[CP_LINK_HDR_LEN + new_plen] = cp_link_cksum(out + CP_LINK_HDR_LEN, new_plen);
    new_total = CP_LINK_HDR_LEN + new_plen + 1;

    /* link length + header checksum */
    wr16(out + 2, (uint16_t)new_total);
    out[8] = cp_link_cksum(out, 8);
    return (int)new_total;
}
