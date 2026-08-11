#pragma once

// ====================
// AudioScene — ECS ↔ AudioEngine bridge (Stage 6 — Pillar G, step 5)
// ====================
//
// Drives the AudioEngine from the ECS each frame: one AudioListener entity sets
// the mixer's listener pose (from its Transform), and every AudioSource entity
// with its "playing" bit set starts/updates a voice (clip resolved by GUID,
// position/volume/pitch streamed live). Velocities for Doppler are derived from
// per-entity position deltas, so no Velocity component is required.
//
// Header-only so it needs no link edge between gws_ecs and gws_audio — the
// consumer links both and calls register_core_components(). The game thread
// calls update(); every engine call it makes is a lock-free enqueue.
//
//   AudioScene scene(engine);
//   scene.register_clip(clip_guid, engine.add_clip(...));   // at load time
//   scene.update(world, dt);                                // each frame

#include "audio/audio_engine.h"

#include "ecs/world.h"
#include "ecs/components.h"

#include <glm/glm.hpp>

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace gws::audio {

// AudioSource::flags bits (mirror the ECS component doc).
inline constexpr uint32_t kAudioLooping = 1u << 0;
inline constexpr uint32_t kAudioSpatial = 1u << 1;
inline constexpr uint32_t kAudioPlaying = 1u << 2;

class AudioScene {
public:
    explicit AudioScene(AudioEngine& engine) : engine_(engine) {}

    // Map an asset GUID (AudioSource::clip) to a loaded mixer clip.
    void register_clip(uint64_t guid, ClipId clip) { if (guid) clips_[guid] = clip; }

    // Occlusion hook (Step 6): given listener + source world positions, return
    // 0 (clear line of sight) … 1 (fully blocked). Typically a physics raycast.
    // Kept as a callback so the bridge needs no physics dependency. Applied to
    // spatial voices only.
    using OcclusionFn = std::function<float(const glm::vec3& listener, const glm::vec3& source)>;
    void set_occlusion_fn(OcclusionFn fn) { occlusion_fn_ = std::move(fn); }

    // Drive the engine from the ECS for one frame (`dt` for velocity/Doppler).
    void update(schizo::ecs::World& world, float dt) {
        update_listener(world, dt);
        update_sources(world, dt);
    }

    size_t playing_voices() const { return voices_.size(); }

private:
    using Entity = schizo::ecs::Entity;
    using Key    = uint32_t;
    static Key key_of(Entity e) { return static_cast<Key>(e); }

    // Velocity from frame-to-frame position delta (per tracked entity table).
    glm::vec3 track_velocity(std::unordered_map<Key, glm::vec3>& last,
                             Key key, const glm::vec3& pos, float dt) {
        glm::vec3 vel(0.0f);
        const auto it = last.find(key);
        if (it != last.end() && dt > 0.0f) vel = (pos - it->second) / dt;
        last[key] = pos;
        return vel;
    }

    void update_listener(schizo::ecs::World& w, float dt) {
        bool set = false;
        w.each<schizo::ecs::Transform, schizo::ecs::AudioListener>(
            [&](Entity e, schizo::ecs::Transform& t, schizo::ecs::AudioListener& al) {
                if (set || !(al.flags & 1u)) return;          // first active listener wins
                set = true;
                Listener l;
                l.position    = t.position;
                l.forward     = t.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
                l.up          = t.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
                l.master_gain = al.master_gain;
                l.velocity    = track_velocity(last_listener_, key_of(e), t.position, dt);
                listener_pos_ = t.position;       // for the occlusion raycast
                engine_.set_listener(l);
            });
    }

    void update_sources(schizo::ecs::World& w, float dt) {
        seen_.clear();
        w.each<schizo::ecs::Transform, schizo::ecs::AudioSource>(
            [&](Entity e, schizo::ecs::Transform& t, schizo::ecs::AudioSource& src) {
                const Key  key     = key_of(e);
                const bool playing = (src.flags & kAudioPlaying) != 0;
                const auto it      = voices_.find(key);
                if (!playing) {                                // stopped → release voice
                    if (it != voices_.end()) { engine_.stop(it->second); voices_.erase(it); }
                    return;
                }
                seen_.insert(key);
                VoiceParams p;
                p.gain     = src.volume;
                p.pitch    = src.pitch;
                p.radius   = src.radius;
                p.looping  = (src.flags & kAudioLooping) != 0;
                p.spatial  = (src.flags & kAudioSpatial) != 0;
                p.position = t.position;
                p.velocity = track_velocity(last_source_, key, t.position, dt);
                if (p.spatial && occlusion_fn_)
                    p.occlusion = occlusion_fn_(listener_pos_, t.position);
                if (it == voices_.end()) {                     // newly playing → start
                    const ClipId clip = clip_for(src.clip);
                    if (clip == kInvalidClip) return;          // GUID not registered yet
                    const VoiceId v = engine_.play(clip, p);
                    if (v != kInvalidVoice) voices_[key] = v;
                } else {
                    engine_.set_voice(it->second, p);          // update live params
                }
            });
        // Release voices whose source entity vanished entirely this frame.
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (seen_.find(it->first) == seen_.end()) { engine_.stop(it->second); it = voices_.erase(it); }
            else ++it;
        }
    }

    ClipId clip_for(uint64_t guid) const {
        const auto it = clips_.find(guid);
        return it == clips_.end() ? kInvalidClip : it->second;
    }

    AudioEngine&                            engine_;
    OcclusionFn                             occlusion_fn_;
    glm::vec3                               listener_pos_{0.0f};
    std::unordered_map<uint64_t, ClipId>    clips_;
    std::unordered_map<Key, VoiceId>        voices_;          // entity → playing voice
    std::unordered_map<Key, glm::vec3>      last_source_;
    std::unordered_map<Key, glm::vec3>      last_listener_;
    std::unordered_set<Key>                 seen_;
};

}  // namespace gws::audio
