/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: the LD_PRELOAD hook that lives inside mm-ipod (QNX 6.5 armle-v7).
 *
 * What it does, and nothing else:
 *   1. On load, redirect mm-ipod's dlopen GOT slot so we learn when ipod-drvr-iap2.so / ipod-transport-bt.so
 *      are loaded (mm-ipod dlopens its driver/transport by name).
 *   2. After the iAP2 driver loads, VERIFY the reverse-engineered fingerprints (cp_fp_driver). If any differ,
 *      install nothing: the stack runs exactly as stock. Fail-closed.
 *   3. Redirect the driver's libc `memcpy` GOT slot so the recv seam (dispatcher @ base+0x15504) hands us every
 *      decoded control-session message. We copy 0x5201/0x5202/0x5204 to the bridge; all else is passed straight
 *      through to the real memcpy untouched.
 *   4. Redirect the transport's libc `write` GOT slot: rewrite the outgoing IdentificationInformation (0x1D01)
 *      link packet with cp_ident_patch (adds route-guidance ids + the display component), and note when the
 *      factory StartDestinationInformation (0x10DA) goes out -> arm the 0x5200 send.
 *   5. A worker thread: once 0x5200 is armed and the link ctx is known, send StartRouteGuidanceUpdates via the
 *      driver's own link_send_message (seq/ack/checksum handled by the driver). Then, while guidance is on,
 *      push the current maneuver to the KOMBI through the reused NBT-AAD kombi_client (SOME/IP 0xB006).
 *
 * Everything is gated on NBT_CARPLAY_HUD=1 and on argv containing "iap2_wifi" (only the CarPlay Wi-Fi instance
 * of mm-ipod; never the Bluetooth instance). Reverse of a bad run: nothing persists; unset the env / remove the
 * wrapper. This file holds the wiring; the testable logic lives in cp_bridge / ident_patch / cp_got_patch.
 */
#include "cp_got_patch.h"
#include "cp_bridge.h"
#include "ident_patch.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- reverse-engineered offsets (ipod-drvr-iap2.so, NBTevo_Y21432J; p0/RE_FINDINGS.md) ---------------- */
#define DRV_LINK_SEND_MESSAGE   0x14b00
#define DRV_DISPATCHER          0x14c6c
#define DRV_RECV_MEMCPY_SITE     0x15504   /* bl memcpy copying the control params (src = r1 = payload+6) */

/* Fingerprints: 32-bit words we expect in the stock driver. Any mismatch -> do nothing. */
static const cp_fingerprint_t cp_fp_driver[] = {
    { 0x14b00, 0xE92D41F0 },   /* link_send_message prologue   */
    { 0x14c6c, 0xE92D4FF0 },   /* dispatcher prologue          */
    { 0x15500, 0xEBFFC022 },   /* bl memcpy (recv seam)        */
    { 0x15720, 0xEBFFC066 },   /* bl pthread_mutex_lock (ctx A) */
    { 0x157c0, 0xEBFFC03E },   /* bl pthread_mutex_lock (ctx B) */
    { 0x1577c, 0xEBFFC629 }    /* bl iap2_start_features       */
};

/* ---- driver entry points we call (resolved by dlsym once the fingerprints pass) --------------------- */
typedef void* (*sendlist_pop_t)(void* ctx);
typedef int   (*addparam_num_t)(void* ctx, void* pkt, uint16_t id, const void* val, size_t sz);
typedef int   (*addparam_nodata_t)(void* ctx, void* pkt, uint16_t id);
typedef int   (*link_send_t)(void* ctx, void* pkt, uint16_t msgid);
typedef int   (*link_send_raw_t)(void* ctx, const void* data, size_t len);
typedef void  (*sendlist_push_t)(void* ctx, void* pkt);

/* Layout of ipod-drvr-iap2.so's exported iap2_ctrl_msg_table (at 0x303e4, indexed by msgid >> 8).
 * Stock table[0x52] is { NULL, NULL, 0 }, which causes the dispatcher (0x14c6c at 0x15298-0x152dc) to
 * reject 0x5201/0x5202/0x5204 with +22 (EINVAL) BEFORE 0x15504 (hook_memcpy) and BEFORE 0x1550c
 * (packet_recvlist_push). Caller link_recv_pkt_single (0x17980: cmp r6, #0; bgt skip) treats >0 as
 * packet-already-freed and skips packet_recvlist_push, permanently leaking 1 recvlist packet per RG
 * message until all 10 recvlist buffers are gone and mm-ipod deadlocks -> watchdog HU reset!
 * Populating table[0x52] with writable entries routes 0x5201/0x5202/0x5204 through 0x15500 (hook_memcpy)
 * -> 0x1550c (packet_recvlist_push) -> 0x15a0c (free), eliminating the leak completely. */
typedef struct {
    const char* name;
    uint16_t    flags;    /* 0x0001 = outgoing, 0x0002 = incoming; driver mutates bit 0x20 on init/reject */
    uint16_t    unk;
    const void* params;   /* NULL = no parameter descriptor table (handled in print_packet @ 0x2421c) */
} cp_iap2_msg_entry_t;

typedef struct {
    const char*          group_name;
    cp_iap2_msg_entry_t* entries;
    uint32_t             count;
} cp_iap2_msg_group_t;

static cp_iap2_msg_entry_t cp_rg_msg_entries[5] = {
    { "StartRouteGuidanceUpdates",       0x0001, 0x0000, 0 }, /* 0x5200 */
    { "RouteGuidanceUpdate",             0x0002, 0x0000, 0 }, /* 0x5201 */
    { "RouteGuidanceManeuverUpdate",     0x0002, 0x0000, 0 }, /* 0x5202 */
    { "StopRouteGuidanceUpdates",        0x0001, 0x0000, 0 }, /* 0x5203 */
    { "RouteGuidanceLaneGuidanceUpdate", 0x0002, 0x0000, 0 }  /* 0x5204 */
};

static struct {
    int            active;        /* fingerprints passed, seams installed */
    int            msg_table_patched; /* iap2_ctrl_msg_table[0x52] populated -> dispatcher delivers RG */
    void*          drv_base;
    void*          drv_handle;
    sendlist_pop_t sendlist_pop;
    addparam_num_t addparam_num;
    addparam_nodata_t addparam_nodata;
    link_send_t    link_send;
    link_send_raw_t link_send_raw;    /* bypasses the outgoing-id gate that link_send_message applies */
    sendlist_push_t sendlist_push;

    void*          ctx;           /* driver link context (learned at the pthread_mutex_lock seam) */
    volatile void* ctx_candidate; /* ctx seen at the seam; in dry-run we only log it, never use it */
    uint32_t       recv_ret;      /* driver return address of the recv-seam memcpy call (base+0x15504) */
    uint32_t       ctx_ret1;      /* driver return address of the ctx mutex_lock call (base+0x15724) */
    uint32_t       ctx_ret2;      /* driver return address of the ctx mutex_lock call (base+0x157c4) */
    volatile int   send_armed;    /* 0x10DA seen -> send 0x5200 */
    volatile int   sent_5200;
    volatile int   link_dead;

    void*          tr_base;       /* ipod-transport-bt.so base */
    void*          tr_handle;
    volatile int   tr_active;     /* transport write seam installed */

    int            dryrun;        /* NBT_CARPLAY_DRYRUN=1: observe + log only, change/send nothing */
    int            skip_ident;    /* NBT_CARPLAY_NOIDENT=1: do not patch Identify (driver declares via cfg) */
    volatile int   rlog, wlog, tick, rlog5, rxlog;
    volatile int   ident_rejected; /* iPhone sent 0x1D03: stop augmenting Identify, run stock */
    volatile int   got_520x;       /* phone started streaming route guidance -> stop retrying 0x5200 */
    volatile int   auth_done;      /* 0xAA05 AuthenticationSucceeded seen: phone will now process 0x5200 */
    volatile int   want_send;      /* worker sets this when a 0x5200 (re)try is due; the DRIVER thread (hook_write)
                                    * performs the actual send, so sendlist ops never run cross-thread */
    volatile int   last_rg_tick;   /* worker tick at the last incoming 0x5201/2/4 (for stale-arrow clearing) */
    volatile int   start_5200_attempts;
    volatile int   last_5200_tick;

    pthread_t      worker;
    volatile int   quit;
} H;

static int enabled_for_this_process(void) {
    const char* e = getenv("NBT_CARPLAY_HUD");
    if (!e || e[0] != '1') return 0;
    /* The wrapper exports NBT_CARPLAY_HUD=1 only for the CarPlay Wi-Fi mm-ipod instance, so the env var is the
     * authoritative gate. Where a Linux-style /proc/self/cmdline is readable we additionally confirm the argv
     * carries "iap2_wifi"; on QNX 6.5 (no such procfs file) we rely on the env gate alone. */
    FILE* f = fopen("/proc/self/cmdline", "rb");
    if (f) {
        char buf[1024];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n > 0) {
            size_t i;
            buf[n] = 0;
            for (i = 0; i + 9 <= n; ++i)
                if (memcmp(buf + i, "iap2_wifi", 9) == 0) return 1;
            return 0;   /* cmdline readable but not the Wi-Fi instance */
        }
    }
    return 1;   /* cmdline unavailable (QNX): the env gate already scoped us to Wi-Fi */
}

/* Debug gate: per-packet logging does fopen/fclose on /dev/shmem for every
 * incoming iAP2 message (~10-20/sec). On the driver's bt_read thread this
 * syscall storm starved the resmgr and tripped the watchdog HU reset.
 * Default OFF in production; set NBT_CARPLAY_DEBUG=1 to re-enable tracing. */
static int cp_debug_on(void) {
    static int v = -1;
    if (v < 0) {
        const char* d = getenv("NBT_CARPLAY_DEBUG");
        v = (d && d[0] && d[0] != '0') ? 1 : 0;
    }
    return v;
}

/* 0x5200 StartRouteGuidanceUpdates injection: ON by default (set NBT_CARPLAY_SEND_5200=0 to disable).
 * Clean sessions without 0x5200 receive zero RG messages from iOS (log 8B0CE912 L1-60, L161-220).
 * Must be sent from worker_main, NEVER from thread_bt_read (hook_read): link_send_message blocks in
 * 0x1435c (pthread_cond_timedwait) waiting for thread_bt_read itself to receive the iAP2 ACK! */
static int cp_send5200_enabled(void) {
    static int v = -1;
    if (v < 0) {
        const char* d = getenv("NBT_CARPLAY_SEND_5200");
        v = (d && d[0] == '0') ? 0 : 1;   /* default ON; only an explicit "0" disables */
    }
    return v;
}

static void log_line(const char* s) {
    const char* p = getenv("NBT_CARPLAY_LOG");
    if (!p) return;
    FILE* f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "%s\n", s);
    fclose(f);
}

static void log_fmt(const char* fmt, ...) {
    const char* p = getenv("NBT_CARPLAY_LOG");
    char buf[256];
    va_list ap;
    FILE* f;
    if (!p) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "%s\n", buf);
    fclose(f);
}

/* Per-packet / high-frequency tracing. No-op unless NBT_CARPLAY_DEBUG is set. */
static void log_dbg(const char* fmt, ...) {
    const char* p;
    char buf[256];
    va_list ap;
    FILE* f;
    if (!cp_debug_on()) return;
    p = getenv("NBT_CARPLAY_LOG");
    if (!p) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "%s\n", buf);
    fclose(f);
}

/* ---- our GOT replacements ----------------------------------------------------------------------------- */
static void send_start_route_guidance(void);   /* defined below; called from hook_read (driver thread) */
static void cp_try_send_5200(void);             /* defined below; performs a due 0x5200 send on driver thread */
static void* (*real_memcpy)(void*, const void*, size_t) = 0;
static ssize_t (*real_write)(int, const void*, size_t) = 0;
static ssize_t (*real_read)(int, void*, size_t) = 0;
static int (*real_mutex_lock)(pthread_mutex_t*) = 0;

/* Best-effort scan of a raw transport read() buffer for complete iAP2 link packets carrying a control-session
 * message. This is the EARLIEST incoming point (before the driver deframes/dispatches), so it reveals whether
 * the phone accepts (0x1D02) or rejects (0x1D03) our Identify, and whether it ever sends 0x5201 route guidance
 * even if the driver drops it before the dispatcher. Read-only, fully bounds-checked, logging only. */
static void cp_scan_incoming(const uint8_t* buf, size_t rc) {
    size_t i = 0;
    while (i + 15 <= rc) {
        if (buf[i] == 0xFF && buf[i + 1] == 0x5A) {
            size_t len = ((size_t)buf[i + 2] << 8) | buf[i + 3];
            if (len >= 9 && i + len <= rc) {
                if (len > 15 && buf[i + 9] == 0x40 && buf[i + 10] == 0x40) {
                    uint16_t msgid = (uint16_t)((buf[i + 13] << 8) | buf[i + 14]);
                    if (H.rxlog < 150) { H.rxlog++; log_dbg("rx link 0x%04X plen=%u", msgid, (unsigned)len); }
                    if (msgid == 0xAA05 && !H.auth_done) { H.auth_done = 1; log_line("auth succeeded (0xAA05): 0x5200 allowed"); }
                    if (msgid == 0x1D02) log_line("*** identify ACCEPTED (0x1D02 at transport read) ***");
                    if (msgid == 0x1D03) log_line("*** identify REJECTED (0x1D03 at transport read) ***");
                    if (msgid == 0x5201 || msgid == 0x5202 || msgid == 0x5204) {
                        /* When iap2_ctrl_msg_table[0x52] is registered (H.msg_table_patched), the driver's
                         * ring-buffer reassembler delivers every complete RG message to hook_memcpy at
                         * 0x15504. Only feed the bridge here as a fallback if the table was not patched. */
                        size_t mlen = ((size_t)buf[i + 11] << 8) | buf[i + 12];
                        if (mlen >= 6 && i + 9 + mlen <= rc) {
                            H.got_520x = 1;
                            H.last_rg_tick = H.tick;
                            if (H.rlog5 < 40) { H.rlog5++; log_dbg("*** RG 0x%04X at transport read (phone IS sending) ***", msgid); }
                            if (!H.msg_table_patched)
                                cp_bridge_on_message(msgid, buf + i + 15, mlen - 6);
                        }
                    }
                }
                i += len;
                continue;
            }
        }
        ++i;
    }
}

static ssize_t hook_read(int fd, void* buf, size_t n) {
    ssize_t rc;
    if (!real_read) real_read = read;
    rc = real_read(fd, buf, n);
    if (H.tr_active && rc > 0) cp_scan_incoming((const uint8_t*)buf, (size_t)rc);
    /* Never call send_start_route_guidance() here on thread_bt_read: link_send_message waits for an iAP2
     * ACK that thread_bt_read itself must read, causing a 10s self-deadlock (rc=260 ETIMEDOUT). */
    return rc;
}

/* The driver's recv dispatcher memcpy's the control-session params; the msg id sits 2 bytes before src. We
 * gate strictly on the return address of the reverse-engineered call site (base+0x15504) so we never inspect
 * an unrelated memcpy, and we refuse to read src[-2] across a page boundary (fail-safe, cannot fault). */
static void* hook_memcpy(void* dst, const void* src, size_t n) {
    if (!real_memcpy) real_memcpy = memcpy;
    if (H.active && n >= 2 &&
        (uint32_t)(uintptr_t)__builtin_return_address(0) == H.recv_ret &&
        (((uintptr_t)src & 0xFFFu) >= 2u)) {
        const uint8_t* p = (const uint8_t*)src;
        uint16_t msgid = (uint16_t)((p[-2] << 8) | p[-1]);
        if (H.dryrun && H.rlog < 60) { H.rlog++; log_fmt("DRYRUN recv msgid=0x%04X n=%u", msgid, (unsigned)n); }
        /* Live census: log every incoming control msgid the dispatcher copies (capped). Lets us see whether
         * 0x5201 ever reaches the driver's dispatcher, and whether 0x1D02/0x1D03 identify accept/reject pass
         * through here. This is the exact path that caught 0x5001/0x4E0x/0xAA02 in Stage A. */
        if (!H.dryrun && H.rlog < 150) { H.rlog++; log_dbg("rx disp 0x%04X n=%u", msgid, (unsigned)n); }
        if (msgid == 0x1D02) log_line("identify ACCEPTED by phone (0x1D02 at dispatcher)");
        if (msgid == 0x1D03 && !H.ident_rejected) {
            H.ident_rejected = 1;
            log_line("iPhone rejected Identify (0x1D03): augmentation disabled, stock from here");
        }
        if (msgid == 0x5201 || msgid == 0x5202 || msgid == 0x5204 || msgid == 0x5203) {
            if (msgid != 0x5203) {
                H.got_520x = 1;            /* phone is streaming guidance: stop retrying 0x5200 */
                H.last_rg_tick = H.tick;   /* RG also arrives here (dispatcher seam), not only at the transport
                                            * read seam; the stale-arrow timer MUST see it on this path too, or
                                            * it fires 30 s after the single early transport-read capture and
                                            * then flaps every tick (visible flicker + repeated 0x5200 churn). */
            }
            if (H.rlog5 < 40) { H.rlog5++; log_dbg("recv route-guidance 0x%04X n=%u", msgid, (unsigned)n); }
            cp_bridge_on_message(msgid, p, n);
        }
    }
    return real_memcpy(dst, src, n);
}

/* ctx seam: the driver locks a mutex at two known call sites whose enclosing struct is the link ctx
 * (ctx = &mutex - 0x4c). We gate on the call-site return address. In dry-run we only record + log the
 * candidate and NEVER set H.ctx, so the worker never sends. Always calls the real lock. */
static int hook_mutex_lock(pthread_mutex_t* m) {
    if (!real_mutex_lock) real_mutex_lock = pthread_mutex_lock;
    if (H.active) {
        uint32_t ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
        if (ret == H.ctx_ret1 || ret == H.ctx_ret2) {
            void* cand = (void*)((char*)m - 0x4c);
            if (ret == H.ctx_ret1) H.auth_done = 1;   /* 0x15724 is inside case 0xAA05 (AuthSucceeded) */
            if (cand != (void*)H.ctx_candidate) {
                H.ctx_candidate = cand;
                if (!H.dryrun) H.ctx = cand;
                log_fmt("ctx seam: mutex=%p ctx=%p (ret=0x%08X)", (void*)m, cand, ret);
            }
        }
    }
    return real_mutex_lock(m);
}

/* The transport write()s one whole link packet. Rewrite an outgoing 0x1D01 Identify; watch for 0x10DA.
 * In dry-run we detect + log but write the ORIGINAL bytes unchanged and never arm the send. Non-link writes
 * (cp_link_ctrl_msg < 0) always pass straight through. */
static ssize_t hook_write(int fd, const void* buf, size_t len) {
    if (!real_write) real_write = write;
    if (H.tr_active && len >= 9) {
        const uint8_t* pkt = (const uint8_t*)buf;
        const uint8_t* params; size_t plen;
        int msgid = cp_link_ctrl_msg(pkt, len, &params, &plen);
        if (msgid == CP_IAP2_IDENT_INFO) {
            static uint8_t patched[2048];
            int r;
            /* New iAP2 identification round -> reset per-session subscription state. */
            H.send_armed = 0;
            H.sent_5200 = 0;
            H.got_520x = 0;
            H.start_5200_attempts = 0;
            H.last_5200_tick = 0;
            H.last_rg_tick = 0;
            cp_bridge_reset();
            if (H.skip_ident) {
                /* driver declares the RGI message IDs natively via [ident] sendmsgs/recvmsgs in iap2_Wifi.cfg;
                 * leave its Identify untouched (purest path, no on-wire byte surgery) */
                if (H.wlog < 40) { H.wlog++; log_fmt("ident 0x1D01 len=%u passed through (NOIDENT; cfg declares RGI)", (unsigned)len); }
                return real_write(fd, buf, len);
            }
            r = H.ident_rejected ? 0 : cp_ident_patch(pkt, len, patched, sizeof(patched), CP_RG_COMPONENT_ID);
            if (H.dryrun) {
                log_fmt("DRYRUN ident 0x1D01 len=%u -> cp_ident_patch=%d (writing ORIGINAL)",
                        (unsigned)len, r);
            } else if (r > 0) {
                log_fmt("ident: rewritten %u -> %d bytes (route guidance declared)", (unsigned)len, r);
                { static int dumped = 0;
                  if (!dumped) { FILE* df;
                    dumped = 1;
                    df = fopen("/dev/shmem/ident_orig.bin", "wb");
                    if (df) { fwrite(pkt, 1, len, df); fclose(df); }
                    df = fopen("/dev/shmem/ident_patched.bin", "wb");
                    if (df) { fwrite(patched, 1, (size_t)r, df); fclose(df); }
                  } }
                return real_write(fd, patched, (size_t)r) == r ? (ssize_t)len : -1;
            }
        } else if (msgid == 0x10DA) {
            if (H.dryrun) log_line("DRYRUN outgoing 0x10DA seen (would arm 0x5200)");
            else {
                /* 0x10DA (destination_startinfo) is sent by iap2_start_features only after BOTH 0x1D02 and
                 * 0xAA05 have succeeded, so seeing 0x10DA guarantees authentication is complete. */
                H.auth_done = 1;
                H.send_armed = 1;
                log_line("0x10DA seen: arming 0x5200 StartRouteGuidanceUpdates");
            }
        } else if (msgid >= 0 && H.dryrun && H.wlog < 40) {
            H.wlog++;
            log_fmt("DRYRUN out ctrl msgid=0x%04X len=%u", (unsigned)msgid, (unsigned)len);
        }
    }
    {
        ssize_t wr = real_write(fd, buf, len);
        return wr;
    }
}

/* ---- sending 0x5200 through the driver's own path ---------------------------------------------------- */

static void send_start_route_guidance(void) {
    uint16_t cid = CP_RG_COMPONENT_ID;
    void* pkt;
    int rc;
    if (!H.ctx || !H.sendlist_pop || !H.link_send) return;
    /* Must be called from worker_main (NEVER from thread_bt_read): packet_sendlist_pop, link_send_message
     * (0x1435c) and packet_sendlist_push take the driver's internal locks (ctx+0x4c and link_state) and
     * 0x1435c blocks on pkt+0x10 condvar until thread_bt_read receives the phone's iAP2 ACK. */
    pkt = H.sendlist_pop(H.ctx);
    if (!pkt) return;
    /* packet_addparam_num(size 2) loads a HOST-order u16 (ldrh) and writes it big-endian itself, so pass the
     * component id in host order; pre-swapping put 0x4000 on the wire vs the Identify's 0x0040 and the phone
     * never matched the RouteGuidanceDisplayComponent. */
    H.addparam_num(H.ctx, pkt, 0x0000, &cid, 2);    /* component id */
    H.addparam_nodata(H.ctx, pkt, 0x0001);          /* request source name     */
    H.addparam_nodata(H.ctx, pkt, 0x0002);          /* request source-supports */
    H.addparam_nodata(H.ctx, pkt, 0x0003);          /* request exit info       */
    rc = H.link_send(H.ctx, pkt, 0x5200);
    H.sendlist_push(H.ctx, pkt);
    if (rc == 0) H.sent_5200 = 1;
    log_fmt("link_send_message(0x5200) rc=%d ctx=%p", rc, H.ctx);
}

static void cp_try_send_5200(void) {
    if (!cp_send5200_enabled()) return;
    if (H.tr_active && !H.dryrun && H.ctx && H.auth_done && H.send_armed && !H.sent_5200 && !H.got_520x) {
        send_start_route_guidance();
    }
}

/* ---- worker: send 0x5200 when armed, push maneuvers to the KOMBI while guidance is on ---------------- */
extern int  cp_kombi_open(void);          /* in cp_kombi.cpp: connect the SOME/IP client (reused NBT-AAD) */
extern void cp_kombi_send(const cp_maneuver_t* m);
extern void cp_kombi_clear(void);
extern void cp_kombi_close(void);

static void* worker_main(void* arg) {
    int kombi_ready = 0, was_on = 0;
    static char last_sent_man[256] = "";
    (void)arg;
    while (!H.quit) {
        usleep(200 * 1000);
        H.tick++;
        if (H.dryrun) {
            /* Stage A: observe only. No 0x5200, no KOMBI connection, no maneuver push. */
            if ((H.tick % 25) == 0)
                log_fmt("DRYRUN state: tr=%d drv=%d armed=%d ctx=%p guid=%d",
                        H.tr_active, H.active, H.send_armed, (void*)H.ctx_candidate,
                        cp_bridge_guidance_on());
            continue;
        }
        if (H.link_dead) continue;
        /* Send 0x5200 from worker_main once armed (after 0x10DA): link_send_message is thread-safe and waits
         * for thread_bt_read to receive the phone's iAP2 ACK (rc=0). Once ACKed (H.sent_5200=1) or streaming
         * (H.got_520x=1), the session-wide subscription is active and we do not re-send. */
        if (cp_send5200_enabled() &&
            H.ctx && H.auth_done && H.send_armed && !H.sent_5200 && !H.got_520x && H.start_5200_attempts < 3) {
            if (H.start_5200_attempts == 0 || (H.tick - H.last_5200_tick) >= 15) {
                H.last_5200_tick = H.tick;
                H.start_5200_attempts++;
                cp_try_send_5200();
            }
        }
        if (!kombi_ready) kombi_ready = cp_kombi_open();
        {   /* log every change of the RG source/state and of the shown maneuver (uncapped, transitions only) */
            static char last_desc[256] = "", last_man[256] = "";
            char man[256];
            const char* d = cp_bridge_describe();
            cp_maneuver_t cm;
            if (strcmp(d, last_desc) != 0) {
                strncpy(last_desc, d, sizeof(last_desc) - 1);
                log_dbg("[t=%d] RG state: %s guidanceOn=%d", H.tick, d, cp_bridge_guidance_on());
            }
            cp_bridge_current(&cm);
            snprintf(man, sizeof(man), "valid=%d dir=%d rbx=%d arr=%d road='%s'", cm.valid, cm.direction,
                     cm.roundabout_exit, cm.arrival, cm.road);
            if (strcmp(man, last_man) != 0) {
                strncpy(last_man, man, sizeof(last_man) - 1);
                log_dbg("[t=%d] maneuver: %s dist='%s'", H.tick, man, cm.dist_text);
            }
        }
        {   /* ALWAYS-ON per-app capability readout: the moment the phone sends sourceSupportsRouteGuidance
             * (0x5201 TLV 0x14) for a nav source, log it once. This is the single on-car line that answers
             * "does Waze / Google Maps actually emit semantic cluster guidance on this phone?" - sourceSupportsRG=NO
             * means the phone refuses the data and no car-side change can help; YES means our path should show it. */
            static char last_src[160] = "";
            char nm[128]; int known = 0, sup = 0; char cur[160];
            cp_bridge_source(nm, (int)sizeof(nm), &known, &sup);
            snprintf(cur, sizeof(cur), "%d|%d|%s", known, sup, nm);
            if (known && strcmp(cur, last_src) != 0) {
                strncpy(last_src, cur, sizeof(last_src) - 1);
                last_src[sizeof(last_src) - 1] = '\0';
                log_fmt("nav source: app='%s' sourceSupportsRG=%s (iAP2 0x5201 TLV 0x14)",
                        nm[0] ? nm : "?", sup ? "YES" : "NO");
            }
        }
        if (!kombi_ready) continue;
        /* Show guidance only while the route is guiding AND route-guidance is actually flowing (RG seen within
         * 30 s). Gating the show on freshness - instead of clearing in a separate branch - makes the stale
         * case self-limiting: once RG goes stale we clear exactly once and stay cleared until genuinely new RG
         * arrives (last_rg_tick advances), so there is no ON/cleared flap. Apple Maps sends updates sparsely
         * between maneuvers (highway cruise can exceed 10 s with no 0x5201), hence 30 s, not a tight window. A
         * clean end (route_state=0 / 0x5203) drops guidance_on immediately and clears below. */
        {
            int rg_fresh = (H.last_rg_tick != 0) && ((H.tick - H.last_rg_tick) <= 150);  /* 150*200ms = 30s */
            if (cp_bridge_guidance_on() && rg_fresh) {
                cp_maneuver_t m;
                cp_bridge_current(&m);
                if (m.valid) {
                    char cur[256];
                    /* Push to the KOMBI only when the maneuver/distance actually changes; resending the same
                     * frame every 200ms floods the cluster's NavNBTEvoController for no visual benefit. */
                    snprintf(cur, sizeof(cur), "%d|%d|%d|%d|%s|%s", m.direction, m.roundabout_exit, m.arrival,
                             m.valid, m.road, m.dist_text);
                    if (!was_on) log_fmt("guidance ON -> KOMBI: %s", cp_bridge_describe());
                    if (strcmp(cur, last_sent_man) != 0) {
                        cp_kombi_send(&m);
                        strncpy(last_sent_man, cur, sizeof(last_sent_man) - 1);
                        last_sent_man[sizeof(last_sent_man) - 1] = '\0';
                    }
                    was_on = 1;
                }
            } else if (was_on) {
                cp_kombi_clear();
                log_line(rg_fresh ? "guidance cleared"
                                  : "guidance cleared (no RG update for 30s: nav app closed)");
                was_on = 0;
                last_sent_man[0] = '\0';
            }
        }
    }
    if (kombi_ready) { cp_kombi_clear(); cp_kombi_close(); }
    return 0;
}

/* ---- install on the driver once it is loaded -------------------------------------------------------- */
/* Find a loaded module's .got bounds from its link map. Minimal; QNX dlinfo provides the base. */
static void install_on_driver(void* handle) {
    Dl_info info;
    void* base;
    if (H.active) return;
    /* resolve an exported function to locate the module and its base */
    H.link_send = (link_send_t)dlsym(handle, "link_send_message");
    if (!H.link_send || !dladdr((void*)H.link_send, &info)) { log_line("driver: no link_send_message"); return; }
    base = info.dli_fbase;

    if (!cp_verify_fingerprints(base, cp_fp_driver, sizeof(cp_fp_driver) / sizeof(cp_fp_driver[0]))) {
        log_line("driver fingerprints differ: hook disabled (stock behaviour)");
        return;
    }
    H.drv_base = base;
    H.drv_handle = handle;
    H.sendlist_pop    = (sendlist_pop_t)dlsym(handle, "packet_sendlist_pop");
    H.addparam_num    = (addparam_num_t)dlsym(handle, "packet_addparam_num");
    H.addparam_nodata = (addparam_nodata_t)dlsym(handle, "packet_addparam_nodata");
    H.sendlist_push   = (sendlist_push_t)dlsym(handle, "packet_sendlist_push");
    H.link_send_raw   = (link_send_raw_t)dlsym(handle, "link_send_raw_message");
    if (!H.sendlist_pop || !H.addparam_num || !H.addparam_nodata || !H.sendlist_push) {
        log_line("driver: missing packet_* exports; hook disabled");
        return;
    }
    /* return addresses of the reverse-engineered call sites (used to gate the hot-path interposers) */
    H.recv_ret = (uint32_t)(uintptr_t)base + DRV_RECV_MEMCPY_SITE;
    H.ctx_ret1 = (uint32_t)(uintptr_t)base + 0x15724;
    H.ctx_ret2 = (uint32_t)(uintptr_t)base + 0x157c4;
    /* real libc targets to call through (resolved via dlsym, never the lazy PLT stub) */
    real_memcpy = (void* (*)(void*, const void*, size_t))dlsym(handle, "memcpy");
    if (!real_memcpy) real_memcpy = memcpy;
    real_mutex_lock = (int (*)(pthread_mutex_t*))dlsym(handle, "pthread_mutex_lock");
    if (!real_mutex_lock) real_mutex_lock = pthread_mutex_lock;
    /* Locate the .got.plt slots by SYMBOL (binding-agnostic), then redirect those exact slots. */
    {
        void** mc_slot = 0;
        void** ml_slot = 0;
        if (cp_hook_find_got_sym(base, "memcpy", &mc_slot)) {
            if (H.dryrun) log_fmt("DIAG drv memcpy slot=%p *slot=%p real=%p", (void*)mc_slot, *mc_slot,
                                  (void*)real_memcpy);
            if (cp_redirect_slot(mc_slot, (void*)hook_memcpy, 0)) {
                H.active = 1;
                log_line("driver recv seam installed");
            } else {
                log_line("driver: memcpy slot redirect failed");
            }
        } else {
            log_line("driver: memcpy GOT slot not found (JMPREL)");
        }
        if (H.active) {
            cp_iap2_msg_group_t* tbl = (cp_iap2_msg_group_t*)dlsym(handle, "iap2_ctrl_msg_table");
            if (tbl && !H.dryrun) {
                tbl[0x52].group_name = "Route Guidance";
                tbl[0x52].entries    = cp_rg_msg_entries;
                tbl[0x52].count      = 5;
                H.msg_table_patched  = 1;
                log_line("driver iap2_ctrl_msg_table[0x52] registered (fixes recvlist pkt leak)");
            } else if (!tbl) {
                log_line("driver: iap2_ctrl_msg_table symbol not found");
            }
        }
        /* The ctx seam (pthread_mutex_lock redirect) captures ctx for the 0x5200 send. */
        if (H.active && cp_send5200_enabled()) {
            if (cp_hook_find_got_sym(base, "pthread_mutex_lock", &ml_slot)) {
                if (H.dryrun) log_fmt("DIAG drv mutex slot=%p *slot=%p", (void*)ml_slot, *ml_slot);
                if (cp_redirect_slot(ml_slot, (void*)hook_mutex_lock, 0))
                    log_line("driver ctx seam installed");
                else
                    log_line("driver: ctx seam redirect failed");
            } else {
                log_line("driver: mutex_lock GOT slot not found");
            }
        }
    }
}

/* ---- install the write seam on ipod-transport-bt.so once it loads ------------------------------------ */
static void install_on_transport(void* handle) {
    Dl_info info;
    void* base;
    void** w_slot = 0;
    void** r_slot = 0;
    void* sym;
    if (H.tr_active) return;
    sym = dlsym(handle, "ipod_module");            /* transport exports almost nothing; ipod_module is data */
    if (!sym) sym = dlsym(handle, "_btext");
    if (!sym || !dladdr(sym, &info)) { log_line("transport: base not found"); return; }
    base = info.dli_fbase;
    real_write = (ssize_t (*)(int, const void*, size_t))dlsym(handle, "write");
    if (!real_write) real_write = write;
    real_read = (ssize_t (*)(int, void*, size_t))dlsym(handle, "read");
    if (!real_read) real_read = read;
    if (cp_hook_find_got_sym(base, "write", &w_slot)) {
        if (H.dryrun) log_fmt("DIAG tr write slot=%p *slot=%p real=%p base=%p", (void*)w_slot, *w_slot,
                              (void*)real_write, base);
        if (cp_redirect_slot(w_slot, (void*)hook_write, 0)) {
            H.tr_base = base;
            H.tr_handle = handle;
            H.tr_active = 1;
            log_fmt("transport write seam installed (base=%p)", base);
            /* Also redirect the transport's libc read() GOT slot, to observe incoming link packets at the
             * earliest point (identify accept/reject, and any 0x5201 the driver might drop). Diagnostic. */
            if (cp_hook_find_got_sym(base, "read", &r_slot) && cp_redirect_slot(r_slot, (void*)hook_read, 0))
                log_line("transport read seam installed");
            else
                log_line("transport: read GOT slot not redirected (diagnostic only; harmless)");
        } else {
            log_line("transport: write slot redirect failed");
        }
    } else {
        log_line("transport: write GOT slot not found (JMPREL)");
    }
}

/* ---- dlopen interposer: learn when the driver/transport load ---------------------------------------- */
static void* (*real_dlopen)(const char*, int) = 0;

void* dlopen(const char* path, int mode) {
    void* h;
    if (!real_dlopen) real_dlopen = (void* (*)(const char*, int))dlsym(RTLD_NEXT, "dlopen");
    h = real_dlopen(path, mode);
    if (h && path) {
        if (strstr(path, "ipod-drvr-iap2")) install_on_driver(h);
        if (strstr(path, "ipod-transport"))  install_on_transport(h);
    }
    return h;
}

/* ---- constructor: arm only in the CarPlay Wi-Fi mm-ipod instance ------------------------------------ */
__attribute__((constructor))
static void cp_hook_init(void) {
    /* The wrapper first runs this .so through a throwaway /bin/uname with NBT_CARPLAY_SELFTEST=1. If the QNX
     * loader cannot map/relocate it, uname exits non-zero and the wrapper falls back to the stock mm-ipod, so
     * CarPlay keeps working. In self-test mode we must do nothing: no worker thread, no driver, no KOMBI. */
    if (getenv("NBT_CARPLAY_SELFTEST")) return;
    if (!enabled_for_this_process()) return;
    memset(&H, 0, sizeof(H));
    H.dryrun = getenv("NBT_CARPLAY_DRYRUN") ? 1 : 0;
    H.skip_ident = getenv("NBT_CARPLAY_NOIDENT") ? 1 : 0;
    /* Do NOT touch the C++ bridge here: this constructor can run before cp_bridge.o / cp_kombi.o global
     * objects are constructed (init_array order across translation units). g_state is default-constructed
     * clean by its own init_array entry, and the worker only reads it after a delay, by which point dlopen
     * has finished running every init_array entry. Touching it now null-derefs the not-yet-built std::string. */
    log_line("cp_hook: armed for CarPlay Wi-Fi instance");
    log_line(cp_send5200_enabled() ? "cp_hook: mode=ACTIVE (0x5200 send + ctx seam)"
                                   : "cp_hook: mode=PASSIVE (read-only, no driver locking/send)");
    pthread_create(&H.worker, 0, worker_main, 0);
}

__attribute__((destructor))
static void cp_hook_fini(void) {
    if (H.worker) { H.quit = 1; pthread_join(H.worker, 0); }
}
