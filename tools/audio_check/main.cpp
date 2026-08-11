// ====================
// audio_check — Stage 6 audio mixer verification (headless, no device)
// ====================
//
// Drives the device-agnostic Mixer directly (no miniaudio device) with
// deterministic clips so every sample is exactly predictable:
//   - a DC clip (constant 0.5) -> exact gain / summing / master-gain / stop /
//     loop / end-of-clip checks.
//   - a ramp clip (sample[i] = i) -> exact pitch/resample cursor checks.
// This is the offline half of the acceptance; the audible device path is
// confirmed in the editor.

#include "audio/mixer.h"
#include "audio/command_queue.h"
#include "audio/spatial.h"
#include "audio/audio_scene.h"   // ECS bridge (step 5)

#include "ecs/world.h"
#include "ecs/components.h"

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
static bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

static Clip make_dc(uint32_t frames, float value, uint32_t ch = 1, uint32_t sr = 48000) {
    Clip c;
    c.channels = ch;
    c.sample_rate = sr;
    c.samples.assign(static_cast<size_t>(frames) * ch, value);
    return c;
}
static Clip make_ramp(uint32_t frames, uint32_t sr = 48000) {
    Clip c;
    c.channels = 1;
    c.sample_rate = sr;
    c.samples.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) c.samples[i] = static_cast<float>(i);
    return c;
}
// Alternating ±amp — maximum high-frequency (Nyquist) content, so a lowpass's
// effect is obvious in the output energy.
static Clip make_alt(uint32_t frames, float amp = 0.5f, uint32_t sr = 48000) {
    Clip c;
    c.channels = 1;
    c.sample_rate = sr;
    c.samples.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) c.samples[i] = (i & 1u) ? -amp : amp;
    return c;
}

// L of output frame f (stereo interleaved).
static float L(const std::vector<float>& b, uint32_t f) { return b[static_cast<size_t>(f) * 2 + 0]; }
static float R(const std::vector<float>& b, uint32_t f) { return b[static_cast<size_t>(f) * 2 + 1]; }
// Combined L+R RMS over `frames` (signal energy).
static float rms(const std::vector<float>& b, uint32_t frames) {
    double s = 0.0;
    for (uint32_t f = 0; f < frames; ++f) { s += double(L(b, f)) * L(b, f) + double(R(b, f)) * R(b, f); }
    return static_cast<float>(std::sqrt(s / (frames * 2)));
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("audio_check: Stage 6 mixer (headless, no device)\n");
    const AudioFormat fmt{48000, 2};
    std::vector<float> buf(256 * 2);

    // [1] basic playback: DC 0.5 -> both channels 0.5, one active voice.
    {
        Mixer m; m.init(fmt);
        const ClipId clip = m.add_clip(make_dc(1000, 0.5f));
        const VoiceId v = m.play(clip, VoiceParams{});
        m.mix(buf.data(), 256);
        check("plays a clip (out L/R == 0.5)", L(buf, 0) == 0.5f && R(buf, 0) == 0.5f);
        check("one active voice", m.active_voices() == 1);
        check("play returned a valid voice id", v != kInvalidVoice);
    }

    // [2] gain.
    {
        Mixer m; m.init(fmt);
        VoiceParams p; p.gain = 0.25f;
        m.play(m.add_clip(make_dc(1000, 0.5f)), p);
        m.mix(buf.data(), 256);
        check("gain 0.25 -> 0.125", L(buf, 0) == 0.125f);
    }

    // [3] two voices sum.
    {
        Mixer m; m.init(fmt);
        const ClipId clip = m.add_clip(make_dc(1000, 0.5f));
        m.play(clip); m.play(clip);
        m.mix(buf.data(), 256);
        check("two voices sum (0.5 + 0.5 == 1.0)", L(buf, 0) == 1.0f);
        check("two active voices", m.active_voices() == 2);
    }

    // [4] master gain.
    {
        Mixer m; m.init(fmt);
        Listener l; l.master_gain = 0.5f;
        m.set_listener(l);
        m.play(m.add_clip(make_dc(1000, 0.5f)));
        m.mix(buf.data(), 256);
        check("master gain 0.5 -> 0.25", L(buf, 0) == 0.25f);
    }

    // [5] pitch / resample: ramp clip at pitch 2 advances 2 samples per frame.
    {
        Mixer m; m.init(fmt);
        VoiceParams p; p.pitch = 2.0f;
        m.play(m.add_clip(make_ramp(1000)), p);
        m.mix(buf.data(), 256);
        check("pitch 2: frame0==ramp[0]==0", L(buf, 0) == 0.0f);
        check("pitch 2: frame1==ramp[2]==2", L(buf, 1) == 2.0f);
        check("pitch 2: frame2==ramp[4]==4", L(buf, 2) == 4.0f);
    }

    // [6] non-looping voice ends after the clip and frees itself.
    {
        Mixer m; m.init(fmt);
        m.play(m.add_clip(make_dc(4, 0.5f)));   // only 4 frames long
        m.mix(buf.data(), 256);
        check("frame3 (last) == 0.5", L(buf, 3) == 0.5f);
        check("frame4 (past end) == 0", L(buf, 4) == 0.0f);
        check("voice freed after clip end", m.active_voices() == 0);
    }

    // [7] looping voice repeats and stays active.
    {
        Mixer m; m.init(fmt);
        VoiceParams p; p.looping = true;
        m.play(m.add_clip(make_dc(4, 0.5f)), p);
        m.mix(buf.data(), 256);
        check("looping: frame100 still 0.5", L(buf, 100) == 0.5f);
        check("looping voice stays active", m.active_voices() == 1);
    }

    // [8] stop silences the voice on the next block.
    {
        Mixer m; m.init(fmt);
        const VoiceId v = m.play(m.add_clip(make_dc(48000, 0.5f)));
        m.mix(buf.data(), 256);
        check("playing before stop", m.active_voices() == 1 && L(buf, 0) == 0.5f);
        m.stop(v);
        m.mix(buf.data(), 256);
        check("silent after stop", m.active_voices() == 0 && L(buf, 0) == 0.0f);
    }

    // [9] invalid clip id -> no voice.
    {
        Mixer m; m.init(fmt);
        check("play(invalid clip) -> kInvalidVoice", m.play(999) == kInvalidVoice);
    }

    // [10] lock-free command queue: SPSC push/pop FIFO + full/empty behaviour.
    {
        CommandQueue q;
        Command c; c.type = Command::Type::Play;
        check("pop empty -> false", q.pop(c) == false);
        Command a; a.type = Command::Type::Play; a.voice = 7;
        check("push then pop preserves data", q.push(a) && q.pop(c) && c.voice == 7);
        // Fill until full, then it must reject without blocking.
        int pushed = 0;
        while (q.push(a)) { if (++pushed > 2000) break; }
        check("queue reports full (bounded, lock-free)", pushed > 0 && pushed < 2000);
    }

    // [11] capacity: 128 concurrent voices (the acceptance target).
    {
        Mixer m; m.init(fmt);
        const ClipId clip = m.add_clip(make_dc(48000, 0.01f));
        for (int i = 0; i < 128; ++i) m.play(clip);
        m.mix(buf.data(), 256);
        check("128 concurrent voices all active", m.active_voices() == 128);
        check("128 voices sum (~1.28)", L(buf, 0) > 1.27f && L(buf, 0) < 1.29f);
    }

    // ---- Step 3: spatialisation (distance / pan / Doppler) ----
    std::printf("  -- spatial (G2) --\n");
    Listener lis;  // at origin, forward -Z, up +Y -> right = +X
    {
        // Source to the RIGHT (+X), half the rolloff radius away.
        const Spatialization s = compute_spatial({5, 0, 0}, {0, 0, 0}, 10.0f, lis);
        check("right source: pans right (R>L, L~0)", s.right_gain > s.left_gain && approx(s.left_gain, 0.0f));
        check("right source: distance att 0.5 on R", approx(s.right_gain, 0.5f));
    }
    {
        const Spatialization s = compute_spatial({-5, 0, 0}, {0, 0, 0}, 10.0f, lis);
        check("left source: pans left (L>R, R~0)", s.left_gain > s.right_gain && approx(s.right_gain, 0.0f));
    }
    {
        // Straight ahead: centred -> equal constant-power gains, att 0.5.
        const Spatialization s = compute_spatial({0, 0, -5}, {0, 0, 0}, 10.0f, lis);
        check("front source: centred (L==R)", approx(s.left_gain, s.right_gain));
        check("front source: constant-power center == cos(45)*0.5", approx(s.left_gain, 0.70710678f * 0.5f));
    }
    {
        const Spatialization s = compute_spatial({10, 0, 0}, {0, 0, 0}, 10.0f, lis);
        check("source at radius: silent", approx(s.left_gain, 0.0f) && approx(s.right_gain, 0.0f));
    }
    {
        // Doppler: source ahead moving toward listener -> pitch up; away -> down.
        const Spatialization toward = compute_spatial({0, 0, -5}, {0, 0,  5}, 10.0f, lis);
        const Spatialization away   = compute_spatial({0, 0, -5}, {0, 0, -5}, 10.0f, lis);
        check("Doppler: approaching raises pitch (>1)", toward.pitch_scale > 1.0f);
        check("Doppler: receding lowers pitch (<1)",    away.pitch_scale < 1.0f);
    }
    {
        // End-to-end spatial mix: a spatial voice to the right pans into R.
        Mixer m; m.init(fmt);
        m.set_listener(lis);
        VoiceParams p; p.spatial = true; p.position = {5, 0, 0}; p.radius = 10.0f;
        m.play(m.add_clip(make_dc(48000, 0.5f)), p);
        m.mix(buf.data(), 256);
        check("spatial mix: R loud, L ~0", R(buf, 0) > 0.2f && approx(L(buf, 0), 0.0f));
        check("spatial mix: R == clip*att (0.5*0.5)", approx(R(buf, 0), 0.25f));
    }

    // ---- Step 5: ECS bridge (AudioSource / AudioListener → engine) ----
    std::printf("  -- ECS bridge (step 5) --\n");
    {
        using namespace schizo::ecs;
        register_core_components();

        AudioEngine engine;
        engine.init_offline(AudioFormat{48000, 2});   // no device → render_offline is race-free
        const ClipId clip = engine.add_clip(make_dc(48000, 0.5f));

        AudioScene scene(engine);
        scene.register_clip(0xABCDull, clip);

        World world;
        const Entity le = world.create();        // listener at origin facing -Z
        world.add<Transform>(le, Transform{});
        world.add<AudioListener>(le, AudioListener{1.0f, 1u});

        const Entity se = world.create();        // source to the right, spatial
        Transform st; st.position = glm::vec3(5, 0, 0);
        world.add<Transform>(se, st);
        AudioSource src;
        src.clip = 0xABCDull; src.volume = 1.0f; src.pitch = 1.0f; src.radius = 10.0f;
        src.flags = kAudioSpatial | kAudioPlaying | kAudioLooping;
        world.add<AudioSource>(se, src);

        std::vector<float> b(256 * 2);
        scene.update(world, 1.0f / 60.0f);
        check("bridge starts a voice for a playing source", scene.playing_voices() == 1);
        engine.render_offline(b.data(), 256);
        check("bridge: voice active in the mix", engine.mixer().active_voices() == 1);
        check("bridge: spatial source pans right", R(b, 0) > L(b, 0) && approx(L(b, 0), 0.0f));

        // Clear the playing bit → next update releases the voice.
        world.get<AudioSource>(se).flags = kAudioSpatial | kAudioLooping;
        scene.update(world, 1.0f / 60.0f);
        engine.render_offline(b.data(), 256);
        check("bridge: clearing 'playing' stops the voice",
              scene.playing_voices() == 0 && engine.mixer().active_voices() == 0);

        // Re-arm + move the source to the left → pan follows.
        world.get<AudioSource>(se).flags = kAudioSpatial | kAudioPlaying | kAudioLooping;
        world.get<Transform>(se).position = glm::vec3(-5, 0, 0);
        scene.update(world, 1.0f / 60.0f);
        engine.render_offline(b.data(), 256);
        check("bridge: moved source pans left", L(b, 0) > R(b, 0) && approx(R(b, 0), 0.0f));
    }

    // ---- Step 6: occlusion (one-pole lowpass + level duck) ----
    std::printf("  -- occlusion (G2, step 6) --\n");
    std::vector<float> big(4096 * 2);
    {
        // Clear voice: full Nyquist energy passes through.
        Mixer mc; mc.init(fmt);
        VoiceParams pc;            // occlusion 0
        mc.play(mc.add_clip(make_alt(48000)), pc);
        mc.mix(big.data(), 4096);
        const float clear = rms(big, 4096);

        // Occluded voice: identical signal, occlusion 1 → lowpass kills the highs.
        Mixer mo; mo.init(fmt);
        VoiceParams po; po.occlusion = 1.0f;
        mo.play(mo.add_clip(make_alt(48000)), po);
        mo.mix(big.data(), 4096);
        const float occluded = rms(big, 4096);

        check("clear voice keeps high-freq energy", clear > 0.1f);
        check("occlusion muffles highs (occluded << clear)", occluded < clear * 0.25f);
        std::printf("       (clear rms %.4f vs occluded rms %.4f)\n", clear, occluded);
    }
    {
        // ECS bridge occlusion hook: a blocking fn muffles; a clear fn doesn't.
        using namespace schizo::ecs;
        auto build = [&](float occ_value) -> float {
            AudioEngine eng; eng.init_offline(fmt);
            AudioScene scene(eng);
            scene.register_clip(0x1ull, eng.add_clip(make_alt(48000)));
            scene.set_occlusion_fn([occ_value](const glm::vec3&, const glm::vec3&) { return occ_value; });
            World w;
            const Entity le = w.create();
            w.add<Transform>(le, Transform{});
            w.add<AudioListener>(le, AudioListener{1.0f, 1u});
            const Entity se = w.create();
            Transform st; st.position = glm::vec3(0, 0, -3);
            w.add<Transform>(se, st);
            AudioSource s; s.clip = 0x1ull; s.volume = 1.0f; s.pitch = 1.0f; s.radius = 20.0f;
            s.flags = kAudioSpatial | kAudioPlaying | kAudioLooping;
            w.add<AudioSource>(se, s);
            scene.update(w, 1.0f / 60.0f);
            eng.render_offline(big.data(), 4096);
            return rms(big, 4096);
        };
        const float clear    = build(0.0f);
        const float occluded = build(1.0f);
        check("bridge occlusion fn muffles a blocked source", occluded < clear * 0.5f && clear > 0.05f);
        std::printf("       (bridge clear rms %.4f vs occluded rms %.4f)\n", clear, occluded);
    }

    std::printf("audio_check: %s\n", g_failures == 0 ? "ALL OK" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
