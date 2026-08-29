#pragma once

// ====================
// Audio bus graph (Stage 6 — Pillar G1, Phase 4.9)
// ====================
//
// A shallow tree of mix buses sitting between the voices and the output. Every
// voice names a bus; a bus applies gain/mute/solo and folds into its parent,
// and Master (id 0) is the root everything reaches.
//
// WHY THIS EXISTS AT ALL. The workflow plan listed 4.9 as "audio mixer UI —
// rides the Stage-6 bus graph". There was no bus graph: the mixer summed all
// 256 voices straight into the output block, and `VoiceParams` carried gain,
// pitch and spatialisation but nothing about routing. A mixer panel needs
// something to mix, so the routing layer is the feature and the panel is the
// part you can see.
//
// TWO PROPERTIES THIS FILE IS BUILT AROUND:
//
// 1. `effective_gain()` walks to the root rather than requiring buses to be
//    folded in a particular order. With gain/mute/solo and no per-bus DSP, the
//    audible result is identical either way, and a walk cannot be broken by
//    someone inserting a bus in the middle of the table. The walk is bounded by
//    kMaxBuses so a parent cycle -- which the UI must not be able to create,
//    but might -- costs a wrong gain rather than a hung audio thread. An audio
//    callback that never returns is silence plus a watchdog kill, and it is the
//    single worst failure this file could have.
//
// 2. Solo is not "mute everything else". Soloing a group must keep its children
//    audible, so audibility asks whether the bus OR ANY ANCESTOR is soloed.
//    Getting this wrong is invisible until someone solos a group and hears
//    nothing, which reads as "solo is broken" rather than as a routing bug.
//
// ImGui-free and device-free, so the whole thing is testable headless
// (`audiobus_check`) -- the same reason the Mixer is.

#include "audio/audio_types.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gws::audio {

// A bus's display name is fixed-capacity because the mixer owns buses by value
// and the audio thread reads the table; nothing in mix() touches the name, but
// keeping the struct trivially copyable keeps that guarantee cheap to hold.
struct Bus {
    std::string name;
    float       gain   = 1.0f;    // linear, 0 .. 2 (the UI fader's range)
    bool        mute   = false;
    bool        solo   = false;
    BusId       parent = kMasterBus;   // Master's parent is itself; the walk stops there
};

/// The bus table. Index == BusId; 0 is always Master.
class BusGraph {
public:
    // Capacity is reserved ONCE so that add() can never reallocate. The audio
    // thread reads this table every block while the editor may append to it, and
    // a reallocation would pull the storage out from under that read. With the
    // reserve, a concurrent add() is benign: the audio thread sees either the old
    // count or the new one, and the Mixer's scratch is already sized for kMaxBuses
    // either way. Gain/mute/solo still go through the command queue -- this covers
    // topology, which the UI changes rarely and the fader changes constantly.
    BusGraph() { buses_.reserve(kMaxBuses); reset_to_defaults(); }

    /// Master plus the five buses a game actually separates. Seeded rather than
    /// left empty because an empty mixer teaches nobody what a bus is for, and
    /// because AudioSourceComponent's default routing has to name something.
    void reset_to_defaults() {
        buses_.clear();
        buses_.reserve(kMaxBuses);
        buses_.push_back(Bus{"Master", 1.0f, false, false, kMasterBus});
        add("Music",    kMasterBus);
        add("SFX",      kMasterBus);
        add("UI",       kMasterBus);
        add("Voice",    kMasterBus);
        add("Ambience", kMasterBus);
    }

    /// Append a bus. Returns kInvalidBus when the table is full or the parent
    /// is not a real bus -- never a silently-wrong id.
    BusId add(const std::string& name, BusId parent) {
        if (buses_.size() >= kMaxBuses) return kInvalidBus;
        if (!valid(parent)) return kInvalidBus;
        buses_.push_back(Bus{name, 1.0f, false, false, parent});
        return static_cast<BusId>(buses_.size() - 1);
    }

    /// Drop the last bus. Only ever shrinks, so the audio thread reading a stale
    /// count sees a bus that still exists; a voice left pointing past the end is
    /// routed to Master by mix_block rather than dropped. Used by layout loading,
    /// which rebuilds the table from scratch.
    void remove_last() { if (buses_.size() > 1) buses_.pop_back(); }

    bool     valid(BusId b) const { return b < buses_.size(); }
    size_t   count() const { return buses_.size(); }
    Bus*       bus(BusId b)       { return valid(b) ? &buses_[b] : nullptr; }
    const Bus* bus(BusId b) const { return valid(b) ? &buses_[b] : nullptr; }

    /// Look a bus up by name. Case-sensitive, kInvalidBus when absent -- the
    /// scene file stores bus NAMES, not ids, so that inserting a bus cannot
    /// silently re-route every sound authored before it.
    BusId find(const std::string& name) const {
        for (size_t i = 0; i < buses_.size(); ++i)
            if (buses_[i].name == name) return static_cast<BusId>(i);
        return kInvalidBus;
    }

    void set_gain(BusId b, float g) { if (Bus* x = bus(b)) x->gain = g < 0.0f ? 0.0f : g; }
    void set_mute(BusId b, bool m)  { if (Bus* x = bus(b)) x->mute = m; }
    void set_solo(BusId b, bool s)  { if (Bus* x = bus(b)) x->solo = s; }

    /// True when ANY bus is soloed -- the switch that changes what audible()
    /// means for every other bus.
    bool any_solo() const {
        for (const Bus& b : buses_) if (b.solo) return true;
        return false;
    }

    /// Whether `b` reaches the output at all: not muted anywhere up the chain,
    /// and -- when something is soloed -- soloed itself or under a soloed
    /// ancestor.
    bool audible(BusId b) const {
        if (!valid(b)) return false;
        const bool solo_active = any_solo();
        bool       under_solo  = false;

        BusId cur = b;
        for (uint32_t hops = 0; hops < kMaxBuses; ++hops) {
            const Bus& x = buses_[cur];
            if (x.mute) return false;
            if (x.solo) under_solo = true;
            if (x.parent == cur || !valid(x.parent)) break;   // reached Master (or a stop)
            cur = x.parent;
        }
        return solo_active ? under_solo : true;
    }

    /// Linear gain from `b` all the way to the output, folding every ancestor's
    /// fader. 0 when the bus is inaudible.
    float effective_gain(BusId b) const {
        if (!audible(b)) return 0.0f;

        float g   = 1.0f;
        BusId cur = b;
        for (uint32_t hops = 0; hops < kMaxBuses; ++hops) {
            const Bus& x = buses_[cur];
            g *= x.gain;
            if (x.parent == cur || !valid(x.parent)) break;
            cur = x.parent;
        }
        return g;
    }

private:
    std::vector<Bus> buses_;
};

}  // namespace gws::audio
