// ====================
// audiobus_check — Phase 4.9 mix-bus routing (headless, no device)
// ====================
//
// Two halves, and the split is deliberate.
//
// The BusGraph half is pure arithmetic over a tree, so every assertion is an
// exact number. The interesting cases are not "does gain multiply" — they are
// the ones that fail SILENTLY in a shipped mixer: a soloed group that mutes its
// own children, a muted parent that lets a child through, and a parent cycle
// that hangs the audio thread instead of returning a wrong number.
//
// The Mixer half proves the routing is actually wired to sound, because a bus
// table that computes perfect gains and is never consulted by mix() would pass
// every test in the first half. Each assertion reads real output samples.
//
// The DC clip (constant 0.5) makes each expected sample exact: a voice at gain
// 1 on a bus chain of total gain g yields exactly 0.5*g.

#include "audio/bus.h"
#include "audio/mixer.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

using namespace gws::audio;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}
static bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static Clip make_dc(uint32_t frames, float value) {
    Clip c;
    c.channels = 1;
    c.sample_rate = 48000;
    c.samples.assign(frames, value);
    return c;
}

// Peak absolute sample in the left channel of an interleaved stereo block.
static float peak_left(const std::vector<float>& buf, uint32_t frames) {
    float p = 0.0f;
    for (uint32_t f = 0; f < frames; ++f) p = std::fmax(p, std::fabs(buf[f * 2]));
    return p;
}

int main() {
    std::printf("audiobus_check — Phase 4.9 mix-bus routing\n");

    // ---------------------------------------------------------------
    // Group 1 — BusGraph: the tree arithmetic
    // ---------------------------------------------------------------
    std::printf("\n[group] bus graph defaults\n");
    {
        BusGraph g;
        check("Master is bus 0", g.find("Master") == kMasterBus);
        check("seeds six buses", g.count() == 6);
        check("SFX exists", g.find("SFX") != kInvalidBus);
        check("unknown name -> kInvalidBus", g.find("Nope") == kInvalidBus);
        check("default effective gain is unity", approx(g.effective_gain(g.find("SFX")), 1.0f));
        check("nothing soloed by default", !g.any_solo());
        check("every seeded bus is audible", g.audible(g.find("Music")) && g.audible(kMasterBus));
    }

    std::printf("\n[group] gain folds through the parent chain\n");
    {
        BusGraph g;
        const BusId sfx = g.find("SFX");
        g.set_gain(kMasterBus, 0.5f);
        g.set_gain(sfx, 0.5f);
        check("child * parent = 0.25", approx(g.effective_gain(sfx), 0.25f));
        check("master itself is 0.5", approx(g.effective_gain(kMasterBus), 0.5f));

        // Three levels: a submix under SFX must fold BOTH ancestors.
        const BusId weapons = g.add("Weapons", sfx);
        g.set_gain(weapons, 0.5f);
        check("three levels multiply to 0.125", approx(g.effective_gain(weapons), 0.125f));

        g.set_gain(sfx, -2.0f);
        check("negative gain clamps to 0", approx(g.effective_gain(sfx), 0.0f));
    }

    std::printf("\n[group] mute\n");
    {
        BusGraph g;
        const BusId sfx = g.find("SFX"), music = g.find("Music");
        g.set_mute(sfx, true);
        check("muted bus is inaudible", !g.audible(sfx));
        check("muted bus has zero gain", approx(g.effective_gain(sfx), 0.0f));
        check("sibling is unaffected", g.audible(music) && approx(g.effective_gain(music), 1.0f));

        g.set_mute(sfx, false);
        g.set_mute(kMasterBus, true);
        // The one a per-bus-only mute check misses entirely.
        check("muting Master silences a child", !g.audible(sfx));
    }

    std::printf("\n[group] solo (the semantics that fail silently)\n");
    {
        BusGraph g;
        const BusId sfx = g.find("SFX"), music = g.find("Music");
        g.set_solo(music, true);
        check("any_solo becomes true", g.any_solo());
        check("soloed bus is audible", g.audible(music));
        check("non-soloed bus goes silent", !g.audible(sfx));
        check("non-soloed bus has zero gain", approx(g.effective_gain(sfx), 0.0f));

        // Master is not soloed, yet must still pass the soloed child through —
        // otherwise soloing anything at all mutes the entire mixer.
        check("Master still passes a soloed child", approx(g.effective_gain(music), 1.0f));

        g.set_solo(music, false);
        check("clearing solo restores everyone", g.audible(sfx) && !g.any_solo());
    }

    std::printf("\n[group] soloing a GROUP keeps its children\n");
    {
        BusGraph g;
        const BusId sfx     = g.find("SFX");
        const BusId weapons = g.add("Weapons", sfx);
        const BusId music   = g.find("Music");
        g.set_solo(sfx, true);
        // "Solo = mute every other bus" gets this exactly backwards: Weapons is
        // not itself soloed, so a naive implementation silences the very thing
        // the user soloed the group to hear.
        check("child of a soloed group is audible", g.audible(weapons));
        check("unrelated bus is still silent", !g.audible(music));
    }

    std::printf("\n[group] a parent cycle must not hang the audio thread\n");
    {
        BusGraph g;
        const BusId sfx     = g.find("SFX");
        const BusId weapons = g.add("Weapons", sfx);
        // The UI should never build this; if it ever does, a wrong gain is
        // recoverable and an infinite loop on the audio callback is not.
        g.bus(sfx)->parent = weapons;
        const float gain = g.effective_gain(weapons);   // must simply RETURN
        check("cycle terminates and yields a finite gain", std::isfinite(gain));
        const bool a = g.audible(weapons);
        check("cycle audibility terminates", a || !a);
    }

    std::printf("\n[group] add() refuses what it cannot honour\n");
    {
        BusGraph g;
        check("invalid parent -> kInvalidBus", g.add("Orphan", 200) == kInvalidBus);
        while (g.count() < kMaxBuses) g.add("Filler", kMasterBus);
        check("table fills to kMaxBuses", g.count() == kMaxBuses);
        check("overflow -> kInvalidBus, not a bad id", g.add("TooMany", kMasterBus) == kInvalidBus);
    }

    // ---------------------------------------------------------------
    // Group 2 — Mixer: the table is actually consulted by mix()
    // ---------------------------------------------------------------
    const uint32_t frames = 64;
    std::vector<float> out(frames * 2);

    std::printf("\n[group] mixer honours the routing\n");
    {
        Mixer m;
        m.init(AudioFormat{48000, 2});
        const ClipId dc = m.add_clip(make_dc(4800, 0.5f));

        // A voice that names no bus must behave exactly as it did before 4.9.
        VoiceParams p;
        m.play(dc, p);
        m.mix(out.data(), frames);
        check("default voice routes to Master unchanged", approx(peak_left(out, frames), 0.5f));
    }
    {
        Mixer m;
        m.init(AudioFormat{48000, 2});
        const ClipId dc  = m.add_clip(make_dc(4800, 0.5f));
        const BusId  sfx = m.buses().find("SFX");

        Bus b = *m.buses().bus(sfx);
        b.gain = 0.5f;
        m.set_bus(sfx, b);

        VoiceParams p; p.bus = sfx;
        m.play(dc, p);
        m.mix(out.data(), frames);
        check("bus fader scales the voice", approx(peak_left(out, frames), 0.25f));
    }
    {
        Mixer m;
        m.init(AudioFormat{48000, 2});
        const ClipId dc    = m.add_clip(make_dc(4800, 0.5f));
        const BusId  sfx   = m.buses().find("SFX");
        const BusId  music = m.buses().find("Music");

        Bus b = *m.buses().bus(sfx); b.mute = true; m.set_bus(sfx, b);

        VoiceParams ps; ps.bus = sfx;   m.play(dc, ps);
        m.mix(out.data(), frames);
        check("muted bus contributes nothing", approx(peak_left(out, frames), 0.0f));

        VoiceParams pm; pm.bus = music; m.play(dc, pm);
        m.mix(out.data(), frames);
        check("a voice on another bus still sounds", approx(peak_left(out, frames), 0.5f));
    }
    {
        Mixer m;
        m.init(AudioFormat{48000, 2});
        const ClipId dc    = m.add_clip(make_dc(4800, 0.5f));
        const BusId  sfx   = m.buses().find("SFX");
        const BusId  music = m.buses().find("Music");

        Bus b = *m.buses().bus(music); b.solo = true; m.set_bus(music, b);
        VoiceParams ps; ps.bus = sfx; m.play(dc, ps);
        m.mix(out.data(), frames);
        check("solo silences a non-soloed bus in real output", approx(peak_left(out, frames), 0.0f));
    }

    std::printf("\n[group] per-bus metering\n");
    {
        Mixer m;
        m.init(AudioFormat{48000, 2});
        const ClipId dc  = m.add_clip(make_dc(4800, 0.5f));
        const BusId  sfx = m.buses().find("SFX");

        // Two voices on ONE bus: the meter must show their SUM, which is the
        // whole reason buses need their own accumulation rather than a
        // per-voice max.
        VoiceParams p; p.bus = sfx;
        m.play(dc, p);
        m.play(dc, p);
        m.mix(out.data(), frames);
        check("bus peak is the sum of its voices", approx(m.bus_peak(sfx), 1.0f));
        check("an idle bus reads zero", approx(m.bus_peak(m.buses().find("UI")), 0.0f));

        Bus b = *m.buses().bus(sfx); b.gain = 0.5f; m.set_bus(sfx, b);
        m.mix(out.data(), frames);
        check("meter is post-fader", approx(m.bus_peak(sfx), 0.5f));
    }

    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
