#pragma once

// ====================
// Audio mixer (Stage 6 — Pillar G1)
// ====================
//
// The device-agnostic heart of the audio system: a fixed pool of voices summed
// into an interleaved output block. The game thread enqueues play/stop/param
// commands (lock-free); the audio thread calls mix() once per block, which
// drains the queue then sums every active voice. No locks, no allocation in
// mix() — so it is safe on a real-time audio callback AND testable offline with
// no device at all (just call mix() and inspect the buffer).
//
// Step 1 mixes gain + pitch (resample) + looping. Spatialisation (distance/pan/
// Doppler) lands in Step 3 — voices already carry the position/velocity params.

#include "audio/audio_types.h"
#include "audio/command_queue.h"

#include <atomic>
#include <vector>
#include <cstdint>

namespace gws::audio {

class Mixer {
public:
    static constexpr uint32_t kMaxVoices = 256;   // acceptance target: 128 concurrent

    void init(AudioFormat fmt);
    AudioFormat format() const { return format_; }

    // ---- Setup (NOT real-time-safe; call before/around playback) ----
    // clips_ is reserved so appends don't reallocate while the audio thread
    // reads it; a voice only references a clip after add_clip has returned.
    ClipId      add_clip(Clip clip);
    const Clip* clip(ClipId id) const;
    size_t      clip_count() const { return clips_.size(); }

    // ---- Game thread (real-time-safe: lock-free enqueue) ----
    VoiceId play(ClipId clip, const VoiceParams& p = {});
    void    stop(VoiceId v);
    void    set_voice(VoiceId v, const VoiceParams& p);
    void    set_occlusion(VoiceId v, float occlusion);   // 0 clear … 1 blocked
    void    set_listener(const Listener& l);

    // ---- Audio thread: fill `frames` of interleaved output ----
    void mix(float* out, uint32_t frames);

    size_t   active_voices() const { return active_count_; }
    Listener listener()      const { return listener_; }

private:
    struct Voice {
        ClipId      clip   = kInvalidClip;
        VoiceId     id     = kInvalidVoice;
        double      cursor = 0.0;     // fractional frame position (pitch/resample)
        VoiceParams params;
        bool        active = false;
        // Occlusion: one-pole lowpass state (per channel) + smoothed amount, so
        // a source moving behind geometry muffles instead of clicking.
        float       occ    = 0.0f;    // smoothed occlusion (tracks params.occlusion)
        float       lp_l   = 0.0f;
        float       lp_r   = 0.0f;
    };

    void drain_commands();
    void start_voice(const Command& c);
    void mix_voice(Voice& v, const Clip& c, float* out, uint32_t frames);

    AudioFormat           format_{};
    std::vector<Clip>     clips_;        // index i ↔ ClipId (i + 1)
    std::vector<Voice>    voices_;       // fixed pool of kMaxVoices
    CommandQueue          queue_;
    Listener              listener_{};
    std::atomic<uint32_t> next_voice_{1};
    size_t                active_count_ = 0;
};

}  // namespace gws::audio
