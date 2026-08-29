#include "audio/mixer.h"
#include "audio/spatial.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gws::audio {

void Mixer::init(AudioFormat fmt) {
    format_ = fmt;
    voices_.assign(kMaxVoices, Voice{});
    clips_.clear();
    clips_.reserve(1024);          // append-only without realloc while playing
    listener_ = Listener{};
    active_count_ = 0;

    // One scratch block per POSSIBLE bus, not per current bus: buses can be
    // added after init(), and a resize here would have to happen on whichever
    // thread called add() while mix() was reading.
    buses_.reset_to_defaults();
    scratch_.assign(static_cast<size_t>(kMaxBuses) * kMaxBlockFrames * fmt.channels, 0.0f);
    for (float& p : bus_peak_) p = 0.0f;
}

ClipId Mixer::add_clip(Clip clip) {
    clips_.push_back(std::move(clip));
    return static_cast<ClipId>(clips_.size());   // 1-based handle
}

const Clip* Mixer::clip(ClipId id) const {
    if (id == kInvalidClip || id > clips_.size()) return nullptr;
    return &clips_[id - 1];
}

VoiceId Mixer::play(ClipId clip, const VoiceParams& p) {
    if (clip == kInvalidClip || clip > clips_.size()) return kInvalidVoice;
    const VoiceId id = next_voice_.fetch_add(1, std::memory_order_relaxed);
    Command c;
    c.type   = Command::Type::Play;
    c.clip   = clip;
    c.voice  = id;
    c.params = p;
    queue_.push(c);
    return id;
}

void Mixer::stop(VoiceId v) {
    Command c;
    c.type  = Command::Type::Stop;
    c.voice = v;
    queue_.push(c);
}

void Mixer::set_voice(VoiceId v, const VoiceParams& p) {
    Command c;
    c.type   = Command::Type::SetVoice;
    c.voice  = v;
    c.params = p;
    queue_.push(c);
}

void Mixer::set_occlusion(VoiceId v, float occlusion) {
    Command c;
    c.type   = Command::Type::SetOcclusion;
    c.voice  = v;
    c.scalar = occlusion;
    queue_.push(c);
}

void Mixer::set_listener(const Listener& l) {
    Command c;
    c.type     = Command::Type::SetListener;
    c.listener = l;
    queue_.push(c);
}

void Mixer::set_bus(BusId b, const Bus& value) {
    Command c;
    c.type     = Command::Type::SetBus;
    c.bus      = b;
    c.bus_gain = value.gain;
    c.bus_mute = value.mute;
    c.bus_solo = value.solo;
    queue_.push(c);
}

void Mixer::drain_commands() {
    Command c;
    while (queue_.pop(c)) {
        switch (c.type) {
            case Command::Type::Play:
                start_voice(c);
                break;
            case Command::Type::Stop:
                for (Voice& v : voices_)
                    if (v.active && v.id == c.voice) v.active = false;
                break;
            case Command::Type::SetVoice:
                for (Voice& v : voices_)
                    if (v.active && v.id == c.voice) v.params = c.params;
                break;
            case Command::Type::SetOcclusion:
                for (Voice& v : voices_)
                    if (v.active && v.id == c.voice) v.params.occlusion = c.scalar;
                break;
            case Command::Type::SetListener:
                listener_ = c.listener;
                break;
            case Command::Type::SetBus:
                // Only the three mixable fields; name and parent are structure,
                // which the command deliberately cannot carry.
                if (Bus* b = buses_.bus(c.bus)) {
                    b->gain = c.bus_gain < 0.0f ? 0.0f : c.bus_gain;
                    b->mute = c.bus_mute;
                    b->solo = c.bus_solo;
                }
                break;
        }
    }
}

void Mixer::start_voice(const Command& c) {
    for (Voice& v : voices_) {
        if (!v.active) {
            v.clip   = c.clip;
            v.id     = c.voice;
            v.cursor = 0.0;
            v.params = c.params;
            v.active = true;
            v.occ    = c.params.occlusion;   // start settled (no transient)
            v.lp_l   = 0.0f;
            v.lp_r   = 0.0f;
            return;
        }
    }
    // Pool full — drop the request (a voice never starts) rather than stall.
}

void Mixer::mix(float* out, uint32_t frames) {
    const uint32_t ch = format_.channels;
    std::fill(out, out + static_cast<size_t>(frames) * ch, 0.0f);

    drain_commands();

    // Peaks describe THIS call, so they reset here and accumulate across chunks.
    for (float& p : bus_peak_) p = 0.0f;

    const size_t nbus = buses_.count();
    uint32_t     done = 0;
    while (done < frames) {
        const uint32_t n = (frames - done) < kMaxBlockFrames ? (frames - done) : kMaxBlockFrames;
        mix_block(out + static_cast<size_t>(done) * ch, n, nbus);
        done += n;
    }

    // Counted after every chunk, because a voice can end partway through one.
    size_t active = 0;
    for (const Voice& v : voices_) if (v.active) ++active;
    active_count_ = active;

    const float mg = listener_.master_gain;
    if (mg != 1.0f) {
        const size_t n = static_cast<size_t>(frames) * ch;
        for (size_t i = 0; i < n; ++i) out[i] *= mg;
    }
}

// Sum every voice into ITS OWN bus, then fold the buses into the output.
//
// A per-voice "multiply by the bus gain" would produce identical audio and no
// meter: a bus's level is the level of the SUM of its voices, and once the
// voices are already mixed into the master there is nothing left to measure.
// The accumulation is also where a per-bus effect would go later.
void Mixer::mix_block(float* out, uint32_t frames, size_t nbus) {
    const uint32_t ch     = format_.channels;
    const size_t   stride = static_cast<size_t>(kMaxBlockFrames) * ch;
    const size_t   used   = static_cast<size_t>(frames) * ch;

    for (size_t b = 0; b < nbus; ++b)
        std::fill(scratch_.begin() + b * stride, scratch_.begin() + b * stride + used, 0.0f);

    for (Voice& v : voices_) {
        if (!v.active) continue;
        if (v.clip == kInvalidClip || v.clip > clips_.size()) { v.active = false; continue; }
        // A voice naming a bus that no longer exists is routed to Master rather
        // than dropped: a sound that vanishes is reported as a missing asset and
        // costs far more to diagnose than one playing on the wrong fader.
        BusId b = v.params.bus;
        if (b >= nbus) b = kMasterBus;
        mix_voice(v, clips_[v.clip - 1], &scratch_[static_cast<size_t>(b) * stride], frames);
    }

    for (size_t b = 0; b < nbus; ++b) {
        const float g = buses_.effective_gain(static_cast<BusId>(b));
        if (g == 0.0f) continue;             // muted or un-soloed: contributes nothing, meters zero
        const float* src  = &scratch_[b * stride];
        float        peak = bus_peak_[b];
        for (size_t i = 0; i < used; ++i) {
            const float sample = src[i] * g;
            out[i] += sample;
            const float mag = sample < 0.0f ? -sample : sample;
            if (mag > peak) peak = mag;
        }
        bus_peak_[b] = peak;
    }
}

void Mixer::mix_voice(Voice& v, const Clip& c, float* out, uint32_t frames) {
    const uint32_t outCh      = format_.channels;
    const uint64_t clipFrames = c.frames();
    if (clipFrames == 0) { v.active = false; return; }

    // Per-channel gain + pitch. For spatial voices, fold in distance
    // attenuation, constant-power pan, and Doppler (computed once per block from
    // this block's listener + source pose — standard per-block spatialisation).
    float  left_gain  = v.params.gain;
    float  right_gain = v.params.gain;
    double doppler    = 1.0;
    if (v.params.spatial) {
        const Spatialization sp =
            compute_spatial(v.params.position, v.params.velocity, v.params.radius, listener_);
        left_gain  = v.params.gain * sp.left_gain;
        right_gain = v.params.gain * sp.right_gain;
        doppler    = static_cast<double>(sp.pitch_scale);
    }

    // Output frame → clip frame advance. Folds pitch, Doppler and sample-rate
    // conversion into one cursor increment (nearest-sample read; exact at rate 1).
    const double rate = static_cast<double>(v.params.pitch) * doppler *
                        static_cast<double>(c.sample_rate) /
                        static_cast<double>(format_.sample_rate);

    // Occlusion: a one-pole lowpass + level duck, ramped per-sample toward the
    // target so blocking/unblocking is click-free. occ 0 ⇒ alpha 1 (passthrough)
    // ⇒ exact bypass; occ 1 ⇒ heavy muffle (~600 Hz cutoff) + 0.4× level.
    constexpr float kAlphaMin   = 0.08f;   // lower = more muffled when occluded
    constexpr float kOccSmooth  = 0.002f;  // per-sample ramp (~10 ms time const)
    const float     target_occ  = std::clamp(v.params.occlusion, 0.0f, 1.0f);

    for (uint32_t f = 0; f < frames; ++f) {
        if (v.cursor >= static_cast<double>(clipFrames)) {
            if (v.params.looping) {
                v.cursor = std::fmod(v.cursor, static_cast<double>(clipFrames));
            } else {
                v.active = false;
                return;
            }
        }
        const uint64_t i = static_cast<uint64_t>(v.cursor);
        float l, r;
        if (c.channels >= 2) {
            l = c.samples[i * c.channels + 0];
            r = c.samples[i * c.channels + 1];
        } else {
            l = r = c.samples[i];
        }
        // Per-sample occlusion: ramp `occ`, lowpass each channel, duck the level.
        v.occ += (target_occ - v.occ) * kOccSmooth;
        const float alpha    = 1.0f - v.occ * (1.0f - kAlphaMin);   // 1 → bypass
        const float occ_gain = 1.0f - v.occ * 0.6f;                 // 1 → 0.4
        v.lp_l += alpha * (l - v.lp_l);
        v.lp_r += alpha * (r - v.lp_r);
        l = v.lp_l * occ_gain;
        r = v.lp_r * occ_gain;
        out[static_cast<size_t>(f) * outCh + 0] += l * left_gain;
        if (outCh > 1) out[static_cast<size_t>(f) * outCh + 1] += r * right_gain;
        v.cursor += rate;
    }
}

}  // namespace gws::audio
