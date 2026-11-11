// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// CarPlay -> BMW HUD/KOMBI (NBT Evo ID6). Route-guidance state machine + the app-agnostic gate.
//
// Accumulates the iAP2 Route Guidance messages (0x5201 update, 0x5202 maneuver, 0x5204 lanes) into a
// current view that the (reused, car-proven) NBT-AAD kombi_nav Core turns into cluster/HUD frames.
//
// The Waze-critical gate: route guidance is drawn only while sourceSupportsRouteGuidance (0x5201/0x14)
// is 1. iOS sets that flag from the nav app's mapTemplateShouldProvideNavigationMetadata. Apple Maps,
// Google Maps, AMap (with its setting) and Waze 5.4.0+ send 1; older Waze sends 0, so we draw nothing
// (fail-clean) instead of a stale or wrong arrow. There is no per-app code: any app that opts in works.
//
// Pure, no I/O, strict C++98. Timing (approach, grace, clear, in-place updates) is NOT here; it is the
// reused kombi_nav Core's job. This module only answers "is guidance on?" and "what is the current
// maneuver, in KOMBI terms?" plus diagnostics for the in-car readout.
#ifndef CP_RGI_STATE_HPP
#define CP_RGI_STATE_HPP

#include "iap2_rgi.hpp"
#include "cp_maneuver_map.hpp"
#include "kombi_defs.hpp"

#include <map>

namespace cp {

// One cached maneuver from 0x5202.
struct CachedManeuver {
    bool present;
    int type;            // iAP2 maneuver type
    int driving_side;    // 0 RHT, 1 LHT, -1 unknown
    bool has_exit_angle;
    int exit_angle;
    bool has_dist_between;
    int64_t dist_between_m;
    std::string after_road;
    CachedManeuver() : present(false), type(255), driving_side(-1),
                       has_exit_angle(false), exit_angle(0),
                       has_dist_between(false), dist_between_m(0) {}
};

// The KOMBI-ready view of the current maneuver (fed to the reused kombi_nav Core / kombi_graph).
struct CpView {
    bool valid;
    int kombi_dir;        // KOMBI Direction enum
    int roundabout_exit;  // 1..19, else 0
    bool arrival;
    std::string road;         // road to turn into (after-maneuver road), else current road
    std::string dist_text;    // formatted for the KOMBI locale
    int kombi_unit;           // KOMBI Unit byte
    int64_t dist_m;           // metres to the next maneuver (approach logic lives in the Core)
    bool has_dist;
    CpView() : valid(false), kombi_dir(kombi::DIR_STRAIGHT), roundabout_exit(0), arrival(false),
               kombi_unit(kombi::U_M), dist_m(0), has_dist(false) {}
};

// Diagnostics for the in-car "is my nav app sending guidance?" readout (and logs).
struct RgiDiag {
    std::string source_name;        // nav app name (0x13), e.g. "Apple Maps", "Waze"
    bool source_supports_known;
    bool source_supports;           // 0x14: does this app/version send cluster metadata?
    int route_state;                // last 0x01
    int maneuver_count;             // last 0x0E
    int lane_total;                 // last 0x11
    bool lane_showing;              // last 0x12
    int last_maneuver_type;         // last 0x5202/0x03
    unsigned n_update;              // count of 0x5201 seen
    unsigned n_maneuver;            // count of 0x5202 seen
    unsigned n_lane;                // count of 0x5204 seen
    RgiDiag() : source_supports_known(false), source_supports(false), route_state(iap2::RS_NO_ROUTE),
                maneuver_count(0), lane_total(0), lane_showing(false), last_maneuver_type(255),
                n_update(0), n_maneuver(0), n_lane(0) {}
};

class RgiState {
public:
    RgiState() : cur_index_(-1), last_index_(-1), dist_m_(0), has_dist_(false) {}

    // Feeds one parsed control-session message. Returns true if it was a Route Guidance message.
    bool onMessage(uint16_t msgid, const uint8_t* params, size_t len) {
        switch (msgid) {
            case iap2::MSG_RG_UPDATE:   onUpdate(params, len);   return true;
            case iap2::MSG_RG_MANEUVER: onManeuver(params, len); return true;
            case iap2::MSG_RG_LANE:     onLane(params, len);     return true;
            case iap2::MSG_STOP_RG:     reset();                 return true;
            default:                    return false;
        }
    }

    // Convenience: parse a 40 40-framed message and feed it.
    bool onFramedMessage(const uint8_t* buf, size_t n) {
        uint16_t msgid; const uint8_t* params; size_t plen;
        if (!iap2::parseControlMessage(buf, n, msgid, params, plen)) return false;
        return onMessage(msgid, params, plen);
    }

    // The gate. Guidance is drawn only when the app opts in AND the route is in a guiding state.
    bool guidanceOn() const {
        if (!diag_.source_supports_known || !diag_.source_supports) return false;
        int s = diag_.route_state;
        return s == iap2::RS_ROUTE_SET || s == iap2::RS_REROUTING || s == iap2::RS_PROCEED ||
               s == iap2::RS_ARRIVED;   // ARRIVED draws the destination flag once, then the app clears
    }

    // The current maneuver as a KOMBI-ready view. False when there is nothing to show.
    bool currentView(CpView& out) const {
        out = CpView();
        if (!guidanceOn()) return false;
        const CachedManeuver* man = currentManeuver();
        if (!man || !man->present) return false;

        ManeuverIn mi;
        mi.type = man->type;
        mi.driving_side = man->driving_side;
        mi.has_exit_angle = man->has_exit_angle;
        mi.exit_angle = man->exit_angle;

        out.valid = true;
        out.kombi_dir = cpManeuverToKombi(mi);
        out.roundabout_exit = cpRoundaboutExit(man->type);
        out.arrival = cpIsArrival(man->type);
        out.road = !man->after_road.empty() ? man->after_road : current_road_;
        if (has_dist_) {
            out.dist_m = dist_m_;
            out.has_dist = true;
        } else if (man->has_dist_between) {
            out.dist_m = man->dist_between_m;
            out.has_dist = true;
        }
        if (out.has_dist) {
            std::string text; int unit;
            kombi::metricDisplay(out.dist_m, text, unit);
            out.dist_text = text;
            out.kombi_unit = unit;
        }
        return true;
    }

    const RgiDiag& diag() const { return diag_; }

    // The current maneuver's KOMBI lane row. The 0x5204 lane wire layout is confirmed on-car (phase P4); until
    // then this returns false (no lane row) so the arrow still shows without a possibly-wrong lane row. The
    // bucketing from a lane turn-angle is cp::cpLaneArrowFromAngle (unit-tested) and wires in once the capture
    // from the car confirms the TLV layout.
    bool kombiLanes(kombi::Lanes& out) const {
        out.clear();
        return false;
    }

    // One-line diagnostic string for the in-car readout / log.
    std::string describe() const {
        std::string s = "nav='";
        s += diag_.source_name.empty() ? "?" : diag_.source_name;
        s += "' supports=";
        s += !diag_.source_supports_known ? "unknown" : (diag_.source_supports ? "yes" : "no");
        s += " state=";
        s += routeStateName(diag_.route_state);
        s += diag_.source_supports_known && !diag_.source_supports
                 ? "  (app/version does not send cluster guidance; update it)" : "";
        return s;
    }

    void reset() {
        maneuvers_.clear();
        maneuver_list_.clear();
        cur_index_ = -1;
        last_index_ = -1;
        current_road_.clear();
        has_dist_ = false;
        dist_m_ = 0;
        // keep diag source fields; just mark route inactive
        diag_.route_state = iap2::RS_NO_ROUTE;
    }

    static const char* routeStateName(int s) {
        switch (s) {
            case iap2::RS_NO_ROUTE:  return "no-route";
            case iap2::RS_ROUTE_SET: return "guiding";
            case iap2::RS_ARRIVED:   return "arrived";
            case iap2::RS_LOADING:   return "loading";
            case iap2::RS_LOCATING:  return "locating";
            case iap2::RS_REROUTING: return "rerouting";
            case iap2::RS_PROCEED:   return "proceed-to-route";
            default:                 return "?";
        }
    }

private:
    void onUpdate(const uint8_t* p, size_t n) {
        diag_.n_update++;
        uint32_t u;
        if (iap2::tlvU(p, n, iap2::RGU_ROUTE_STATE, u)) diag_.route_state = (int)u;
        std::string road = iap2::tlvStr(p, n, iap2::RGU_CURRENT_ROAD);
        if (!road.empty()) current_road_ = road;
        std::string src = iap2::tlvStr(p, n, iap2::RGU_SOURCE_NAME);
        if (!src.empty()) diag_.source_name = src;
        if (iap2::tlvU(p, n, iap2::RGU_SOURCE_SUPPORTS, u)) {
            diag_.source_supports_known = true;
            diag_.source_supports = (u != 0);
        }
        if (iap2::tlvU(p, n, iap2::RGU_MANEUVER_COUNT, u)) diag_.maneuver_count = (int)u;
        if (iap2::tlvU(p, n, iap2::RGU_LANE_TOTAL, u)) diag_.lane_total = (int)u;
        if (iap2::tlvU(p, n, iap2::RGU_LANE_SHOWING, u)) diag_.lane_showing = (u != 0);
        bool got_dist = iap2::tlvU(p, n, iap2::RGU_DIST_MANEUVER, u);
        if (got_dist) { dist_m_ = (int64_t)u; has_dist_ = true; }
        // Current maneuver index list (0x0D): keep full order so we can fall back if slot 0 hasn't arrived yet.
        iap2::Tlv list;
        if (iap2::tlvFind(p, n, iap2::RGU_MANEUVER_LIST, list)) {
            maneuver_list_.clear();
            for (size_t i = 0; i + 1 < list.len; i += 2)
                maneuver_list_.push_back((int)iap2::rdBE16(list.data + i));
            if (!maneuver_list_.empty()) {
                int next_idx = maneuver_list_[0];
                if (next_idx != cur_index_ && !got_dist) has_dist_ = false;
                cur_index_ = next_idx;
            }
        }
    }

    void onManeuver(const uint8_t* p, size_t n) {
        diag_.n_maneuver++;
        uint32_t u;
        int idx = 0;
        if (iap2::tlvU(p, n, iap2::RGM_INDEX, u)) idx = (int)u;
        CachedManeuver m = maneuvers_[idx];
        m.present = true;
        if (iap2::tlvU(p, n, iap2::RGM_TYPE, u)) m.type = (int)u;
        if (iap2::tlvU(p, n, iap2::RGM_DRIVING_SIDE, u)) m.driving_side = (int)u;
        if (iap2::tlvU(p, n, iap2::RGM_DIST_BETWEEN, u)) { m.has_dist_between = true; m.dist_between_m = (int64_t)u; }
        int a;
        if (iap2::tlvI16(p, n, iap2::RGM_EXIT_ANGLE, a)) { m.has_exit_angle = true; m.exit_angle = a; }
        std::string ar = iap2::tlvStr(p, n, iap2::RGM_AFTER_ROAD);
        if (!ar.empty()) m.after_road = ar;
        maneuvers_[idx] = m;
        diag_.last_maneuver_type = m.type;
        if (cur_index_ < 0) cur_index_ = idx;   // no list yet: track the latest
        last_index_ = idx;
    }

    void onLane(const uint8_t* p, size_t n) {
        (void)p; (void)n;
        diag_.n_lane++;
        // 0x5204 lane wire layout is confirmed on-car (P0/P4). The 0x5201 lane counters
        // (total/showing) already feed diagnostics; per-lane KOMBI arrows are wired after capture.
    }

    const CachedManeuver* currentManeuver() const {
        for (size_t i = 0; i < maneuver_list_.size(); ++i) {
            std::map<int, CachedManeuver>::const_iterator it = maneuvers_.find(maneuver_list_[i]);
            if (it != maneuvers_.end() && it->second.present) return &it->second;
        }
        int idx = cur_index_ >= 0 ? cur_index_ : last_index_;
        std::map<int, CachedManeuver>::const_iterator it = maneuvers_.find(idx);
        if (it != maneuvers_.end() && it->second.present) return &it->second;
        return 0;
    }

    std::map<int, CachedManeuver> maneuvers_;
    std::vector<int> maneuver_list_;
    int cur_index_;
    int last_index_;
    std::string current_road_;
    int64_t dist_m_;
    bool has_dist_;
    RgiDiag diag_;
};

} // namespace cp

#endif
