// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI: the KOMBI output sink used by the hook worker.
//
// This is the integration point for the reused, car-verified NBT-AAD KOMBI stack
// (github.com/Sq00ky/Supra-iDrive6-Android-Auto, milestone-7/src: kombi_client, kombi_graph,
// kombi_graph_template). Those files are vendored at build time (phase P3b) and drive the SOME/IP
// route-guidance service 0xB006 on 160.48.199.96:30502 to draw the arrow on the HUD and cluster.
//
// Build-time switch CP_KOMBI_REAL:
//   - undefined (default here): a self-contained stub that only logs the maneuver, so the hook .so builds
//     and runs end to end without the vendored files. Used for the QNX/QEMU smoke build.
//   - defined: calls the vendored nbt::kombinav::Graph + KombiClient. The adapter below maps our
//     cp_maneuver_t (already in KOMBI vocabulary) onto Graph::start/maneuver/distance + client.send.
#include "cp_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cp_kombi_log(const char* s) {
    const char* p = getenv("NBT_CARPLAY_LOG");
    if (!p) return;
    FILE* f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "%s\n", s);
    fclose(f);
}

#ifdef CP_KOMBI_REAL
// ---- real integration (vendored NBT-AAD KOMBI stack) ------------------------------------------------
#include "kombi/kombi_client.hpp"
#include "kombi/kombi_graph.hpp"

using namespace nbt::kombinav;

namespace {
Graph*       g_graph  = 0;
KombiClient* g_client = 0;
std::string  g_host;
uint16_t     g_port = 30502;
bool         g_shown = false;
bool         g_inplaceOk = true;
std::string  g_lastRoad;
std::string  g_lastDist;
int          g_lastDir = -1;
int          g_lastUnit = -1;

// cp_maneuver_t -> the KombiLanes the graph builder expects.
KombiLanes toLanes(const cp_maneuver_t* m) {
    KombiLanes l;
    for (int i = 0; i < m->lane_count && i < 8; ++i)
        l.push_back(KombiLane(m->lane_kind[i], m->lane_arrow[i]));
    return l;
}
} // namespace

extern "C" int cp_kombi_open(void) {
    if (g_client) return 1;
    const char* host = getenv("NBT_KOMBI_HOST");
    g_host = host ? host : "160.48.199.96";
    g_graph  = new Graph(0xC8, 255);
    g_client = new KombiClient(g_host, g_port);
    cp_kombi_log("kombi: client created");
    return 1;
}

extern "C" void cp_kombi_send(const cp_maneuver_t* m) {
    if (!g_client || !g_graph || !m->valid) return;
    // Match NBT-AAD nav_core.cpp + j29_display.cpp:
    //   1. An unnamed road is " " (never "", which leaves "Main Street" from the Supra template).
    //   2. A missing distance keeps the last shown distance, else "-" (never "", which leaves "600 ft").
    //   3. If the KOMBI rejects an in-place maneuver() update, fall back to a full start() graph.
    std::string road = (m->road[0] != '\0') ? utf8Prefix(std::string(m->road), 40) : std::string(" ");
    std::string dist = (m->dist_text[0] != '\0') ? utf8Prefix(std::string(m->dist_text), 8) : std::string();
    int unit = m->unit;
    if (dist.empty()) {
        if (g_shown && !g_lastDist.empty() && g_lastDist != "-") {
            dist = g_lastDist;
            unit = g_lastUnit;
        } else {
            dist = "-";
            unit = 3;   // m
        }
    }

    std::vector<Frame> frames;
    KombiLanes lanes = toLanes(m);
    const KombiLanes* lp = lanes.empty() ? 0 : &lanes;
    bool sameManeuver = (m->direction == g_lastDir && g_lastRoad == road);
    std::string detail;

    if (g_shown && sameManeuver) {
        if (dist == g_lastDist && unit == g_lastUnit) return;
        if (!g_graph->distance(dist, unit, frames)) return;
        if (g_client->send(frames, &detail) == KombiClient::kOk) {
            g_lastDist = dist;
            g_lastUnit = unit;
        }
        return;
    }

    if (g_shown && g_inplaceOk && g_graph->maneuver(m->direction, road, dist, unit, frames, lp)) {
        KombiClient::Result r = g_client->send(frames, &detail);
        if (r == KombiClient::kOk) {
            g_lastDir  = m->direction;
            g_lastRoad = road;
            g_lastDist = dist;
            g_lastUnit = unit;
            return;
        }
        if (r == KombiClient::kRejected) {
            cp_kombi_log("kombi: in-place maneuver update rejected, falling back to full graph");
            g_inplaceOk = false;
        } else {
            return;
        }
    }

    if (!g_graph->start(m->direction, road, dist, unit, frames, lp)) return;
    if (g_client->send(frames, &detail) == KombiClient::kOk) {
        g_shown    = true;
        g_lastDir  = m->direction;
        g_lastRoad = road;
        g_lastDist = dist;
        g_lastUnit = unit;
    }
}

extern "C" void cp_kombi_clear(void) {
    if (!g_client || !g_graph || !g_shown) return;
    g_client->cancel(*g_graph);
    g_shown     = false;
    g_inplaceOk = true;
    g_lastDir   = -1;
    g_lastUnit  = -1;
    g_lastRoad.clear();
    g_lastDist.clear();
    cp_kombi_log("kombi: cleared");
}

extern "C" void cp_kombi_close(void) {
    delete g_client; g_client = 0;
    delete g_graph;  g_graph = 0;
}

#else
// ---- stub: log only (the hook .so still builds and runs end to end) ---------------------------------
extern "C" int cp_kombi_open(void) {
    cp_kombi_log("kombi(stub): open");
    return 1;
}
extern "C" void cp_kombi_send(const cp_maneuver_t* m) {
    char buf[256];
    if (!m->valid) return;
    snprintf(buf, sizeof(buf), "kombi(stub): dir=%d exit=%d arrival=%d %s %s road='%s' lanes=%d",
             m->direction, m->roundabout_exit, m->arrival, m->dist_text,
             m->unit == 0 ? "km" : m->unit == 1 ? "mi" : m->unit == 3 ? "m" : m->unit == 4 ? "yd" : "ft",
             m->road, m->lane_count);
    cp_kombi_log(buf);
}
extern "C" void cp_kombi_clear(void) { cp_kombi_log("kombi(stub): clear"); }
extern "C" void cp_kombi_close(void) { cp_kombi_log("kombi(stub): close"); }
#endif
