#pragma once

// ====================
// Audio engine (Stage 6 — Pillar G1)
// ====================
//
// Owns a miniaudio playback device + a Mixer. The device's real-time callback
// runs on miniaudio's own audio thread and pulls blocks from the mixer; the
// game thread only ever enqueues commands (play/stop/listener) — never touches
// the audio thread. miniaudio is fully hidden behind a pimpl so the heavy header
// never leaks into consumers.

#include "audio/audio_types.h"
#include "audio/mixer.h"

#include <memory>
#include <string>
#include <cstdint>

namespace gws::audio {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Open the default playback device + start its thread. Returns false if no
    // device is available (headless/CI) — the mixer still works for offline
    // rendering via render_offline(). The mixer is configured to the device's
    // *actual* negotiated format.
    bool init(AudioFormat fmt = {});
    // Initialise the mixer WITHOUT opening a device — for tests, a headless
    // server, or offline rendering via render_offline(). No audio thread runs,
    // so render_offline() is the only producer (no data race with a callback).
    void init_offline(AudioFormat fmt = {}) { mixer_.init(fmt); running_ = false; }
    void shutdown();
    bool        running() const { return running_; }
    AudioFormat format()  const { return mixer_.format(); }

    // Decode a sound file (WAV/FLAC/MP3 via miniaudio) into a resident clip.
    ClipId load_file(const std::string& path);
    ClipId add_clip(Clip clip) { return mixer_.add_clip(std::move(clip)); }

    VoiceId play(ClipId clip, const VoiceParams& p = {}) { return mixer_.play(clip, p); }
    void    stop(VoiceId v)                              { mixer_.stop(v); }
    void    set_voice(VoiceId v, const VoiceParams& p)   { mixer_.set_voice(v, p); }
    void    set_occlusion(VoiceId v, float occlusion)    { mixer_.set_occlusion(v, occlusion); }
    void    set_listener(const Listener& l)              { mixer_.set_listener(l); }

    Mixer&  mixer() { return mixer_; }

    // Offline render (no device) — for tests + WAV export. The device path calls
    // mixer_.mix() on its own thread; do NOT call this while a device is running.
    void render_offline(float* out, uint32_t frames) { mixer_.mix(out, frames); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Mixer                 mixer_;
    bool                  running_ = false;
};

}  // namespace gws::audio
