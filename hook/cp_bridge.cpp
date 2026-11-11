// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI: C++ side of the bridge. Wraps cp::RgiState behind the cp_bridge.h C ABI.
//
// Mutex-guarded so the driver's receive thread (cp_bridge_on_message) and the hook's worker thread
// (cp_bridge_current / cp_bridge_guidance_on) can touch the same RGI state safely. No I/O here; the
// actual SOME/IP push to the KOMBI is done by the hook worker using the reused NBT-AAD kombi_client,
// fed from the cp_maneuver_t this bridge produces.
#include "cp_bridge.h"
#include "../src/cp_rgi_state.hpp"
#include "../src/kombi_defs.hpp"

#include <pthread.h>
#include <string.h>

namespace {

cp::RgiState g_state;
pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

struct Lock {
    Lock()  { pthread_mutex_lock(&g_mu); }
    ~Lock() { pthread_mutex_unlock(&g_mu); }
};

void copyStr(char* dst, size_t cap, const std::string& s) {
    size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    memcpy(dst, s.data(), n);
    dst[n] = 0;
}

} // namespace

extern "C" void cp_bridge_reset(void) {
    Lock lk;
    g_state.reset();
}

extern "C" int cp_bridge_on_message(uint16_t msgid, const uint8_t* params, size_t len) {
    Lock lk;
    return g_state.onMessage(msgid, params, len) ? 1 : 0;
}

extern "C" int cp_bridge_guidance_on(void) {
    Lock lk;
    return g_state.guidanceOn() ? 1 : 0;
}

extern "C" void cp_bridge_current(cp_maneuver_t* out) {
    Lock lk;
    memset(out, 0, sizeof(*out));
    cp::CpView v;
    if (!g_state.currentView(v)) return;

    out->valid = 1;
    out->direction = v.kombi_dir;
    out->roundabout_exit = v.roundabout_exit;
    out->arrival = v.arrival ? 1 : 0;
    out->unit = v.kombi_unit;
    out->has_distance = v.has_dist ? 1 : 0;
    out->distance_m = (long)v.dist_m;
    copyStr(out->dist_text, sizeof(out->dist_text), v.dist_text);
    copyStr(out->road, sizeof(out->road), v.road);

    // Lanes: convert the phone's lanes (0x5204) into KOMBI lane arrows. The RGI state keeps the raw lane
    // angles; here we bucket them. (Lane wire detail is finalised on-car, phase P4; empty for now unless the
    // state provided them.)
    cp::kombi::Lanes lanes;
    if (g_state.kombiLanes(lanes) && !lanes.empty() && lanes.size() <= 8) {
        out->lane_count = (int)lanes.size();
        for (size_t i = 0; i < lanes.size(); ++i) {
            out->lane_kind[i] = lanes[i].kind;
            out->lane_arrow[i] = lanes[i].arrow;
        }
    }
}

extern "C" const char* cp_bridge_describe(void) {
    // describe() returns a std::string; keep a static buffer for the C ABI.
    static char buf[256];
    Lock lk;
    copyStr(buf, sizeof(buf), g_state.describe());
    return buf;
}

extern "C" void cp_bridge_source(char* name_out, int cap, int* supports_known, int* supports) {
    Lock lk;
    const cp::RgiDiag& d = g_state.diag();
    if (name_out && cap > 0) copyStr(name_out, (size_t)cap, d.source_name);
    if (supports_known) *supports_known = d.source_supports_known ? 1 : 0;
    if (supports)       *supports       = d.source_supports ? 1 : 0;
}
