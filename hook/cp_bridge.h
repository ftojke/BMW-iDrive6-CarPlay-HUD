/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: C ABI bridge between the C LD_PRELOAD hook (cp_hook.c) and the C++ route-guidance
 * logic (cp_rgi_state.hpp). The hook is plain C on its hot paths; everything with STL lives behind this surface.
 *
 * Threading: the hook feeds incoming control messages from the driver's receive thread (cp_bridge_on_message).
 * A single worker thread (owned by the hook) periodically calls cp_bridge_poll to push the current maneuver to
 * the KOMBI. The bridge serialises its own state with a mutex; callers pass the driver ctx opaquely.
 */
#ifndef CP_BRIDGE_H
#define CP_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A KOMBI-ready maneuver snapshot the hook's worker sends through the SOME/IP client (kombi_client). All terms are
 * already in KOMBI vocabulary (see src/kombi_defs.hpp). */
typedef struct {
    int      valid;          /* 0 = nothing to show (guidance off / app not supported) */
    int      direction;      /* KOMBI ManeuverDescription enum */
    int      roundabout_exit;/* 1..19, else 0 */
    int      arrival;        /* 1 = destination flag */
    int      unit;           /* KOMBI distance unit byte */
    int      has_distance;
    long     distance_m;     /* metres to the next maneuver */
    char     dist_text[16];  /* formatted distance, KOMBI locale */
    char     road[128];      /* road to turn into */
    int      lane_count;     /* 0..8 */
    uint8_t  lane_kind[8];   /* 1 take, 2 other */
    uint8_t  lane_arrow[8];  /* KOMBI lane arrow enum */
} cp_maneuver_t;

/* Lifecycle: called once when the hook attaches to a fresh iAP2 link session (Identify start), and on teardown. */
void cp_bridge_reset(void);

/* Feeds one decoded control-session message (msgid + params after the 2-byte id). Thread: driver recv thread.
 * Only route-guidance ids (0x5201/0x5202/0x5204) and 0x5203 change state; everything else is ignored.
 * Returns 1 if the message was route guidance (so the hook can log it), else 0. */
int cp_bridge_on_message(uint16_t msgid, const uint8_t* params, size_t len);

/* True if route guidance should be shown right now (app opted in via 0x14 AND the route is guiding). */
int cp_bridge_guidance_on(void);

/* Fills *out with the current maneuver (out->valid = 0 when there is nothing to show). Thread: worker. */
void cp_bridge_current(cp_maneuver_t* out);

/* One-line diagnostic (nav app name + whether it sends metadata + route state), for the hook log. */
const char* cp_bridge_describe(void);

/* Per-app route-guidance capability readout, for the always-on on-car diagnostic log. Copies the nav source
 * name (iAP2 0x5201 TLV 0x13, e.g. "Waze"/"Google Maps") into name_out, and reports the metadata gate:
 *   *supports_known = 1 once the phone has sent the 0x14 TLV for this source, else 0;
 *   *supports       = value of sourceSupportsRouteGuidance (0x14): 1 = app emits semantic cluster guidance,
 *                     0 = it does not (nothing the car can do). Thread: worker. */
void cp_bridge_source(char* name_out, int cap, int* supports_known, int* supports);

#ifdef __cplusplus
}
#endif

#endif
