#include "audio/audio_engine.h"

#include <miniaudio.h>      // declarations only — impl lives in the miniaudio lib
#include <spdlog/spdlog.h>

#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>        // MultiByteToWideChar for Unicode clip paths
#endif

namespace gws::audio {

struct AudioEngine::Impl {
    ma_device device{};
    bool      device_inited = false;
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

// Real-time audio callback (miniaudio's audio thread). pUserData is the Mixer.
static void data_callback(ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
    auto* mixer = static_cast<Mixer*>(dev->pUserData);
    if (mixer) mixer->mix(static_cast<float*>(out), frames);
}

bool AudioEngine::init(AudioFormat fmt) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = fmt.channels;
    cfg.sampleRate        = fmt.sample_rate;
    cfg.dataCallback      = data_callback;
    cfg.pUserData         = &mixer_;

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        spdlog::warn("[audio] no playback device (headless?); offline mixer still available");
        mixer_.init(fmt);
        return false;
    }
    impl_->device_inited = true;

    // The device may negotiate a different rate/channel count — match the mixer
    // to what we will actually be asked to fill. (Done before ma_device_start,
    // so the callback never fires against an unconfigured mixer.)
    const AudioFormat actual{ impl_->device.sampleRate,
                              static_cast<uint32_t>(impl_->device.playback.channels) };
    mixer_.init(actual);

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        spdlog::error("[audio] ma_device_start failed");
        ma_device_uninit(&impl_->device);
        impl_->device_inited = false;
        return false;
    }
    running_ = true;
    spdlog::info("[audio] device started: {} Hz, {} ch, {} voices",
                 actual.sample_rate, actual.channels, Mixer::kMaxVoices);
    return true;
}

void AudioEngine::shutdown() {
    if (impl_ && impl_->device_inited) {
        ma_device_uninit(&impl_->device);   // stops + joins the audio thread
        impl_->device_inited = false;
    }
    running_ = false;
}

ClipId AudioEngine::load_file(const std::string& path) {
    // Decode to f32, keep the file's native channel count + sample rate (the
    // mixer resamples per-voice). 0/0 ⇒ "native".
    ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
#ifdef _WIN32
    // Open via the wide-char API: ma_decoder_init_file uses fopen with the
    // ANSI codepage, which rejects non-ASCII paths (e.g. Cyrillic filenames)
    // — the most common reason a valid mp3/wav "cannot be played".
    ma_result open_result = MA_ERROR;
    {
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wpath(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
            open_result = ma_decoder_init_file_w(wpath.c_str(), &dcfg, &decoder);
        }
        if (open_result != MA_SUCCESS)   // fall back for non-UTF-8 input
            open_result = ma_decoder_init_file(path.c_str(), &dcfg, &decoder);
    }
    if (open_result != MA_SUCCESS) {
#else
    if (ma_decoder_init_file(path.c_str(), &dcfg, &decoder) != MA_SUCCESS) {
#endif
        spdlog::error("[audio] failed to decode '{}'", path);
        return kInvalidClip;
    }

    Clip clip;
    clip.channels    = decoder.outputChannels;
    clip.sample_rate = decoder.outputSampleRate;

    // Stream the whole file in chunks (length may be unknown for some codecs).
    // This miniaudio build's reader returns the frame count directly.
    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<float> buf(static_cast<size_t>(kChunkFrames) * clip.channels);
    for (;;) {
        const ma_uint64 read = ma_decoder_read_pcm_frames(&decoder, buf.data(), kChunkFrames);
        if (read > 0)
            clip.samples.insert(clip.samples.end(), buf.begin(),
                                buf.begin() + static_cast<size_t>(read) * clip.channels);
        if (read < kChunkFrames) break;
    }
    ma_decoder_uninit(&decoder);

    if (clip.samples.empty()) {
        spdlog::warn("[audio] '{}' decoded to 0 frames", path);
        return kInvalidClip;
    }
    spdlog::info("[audio] loaded '{}': {} frames, {} ch, {} Hz",
                 path, clip.frames(), clip.channels, clip.sample_rate);
    return mixer_.add_clip(std::move(clip));
}

}  // namespace gws::audio
