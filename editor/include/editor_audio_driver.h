#pragma once

// Drives the AudioEngine directly from the OOP scene's AudioSource /
// AudioListener components — the editor's authoring layer — exactly the way
// lights are synced straight from the OOP scene each frame (main.cpp light
// sync). Keeps a per-entity voice map + a decoded-clip cache; no ECS / bridge
// dependency. (The game runtime drives audio from the ECS via
// gws::audio::AudioScene instead.)

#include "audio/audio_engine.h"
#include "entity.h"
#include "scene.h"
#include "transform.h"
#include "audio_components.h"

#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace schizo::editor {

class EditorAudioDriver {
public:
    explicit EditorAudioDriver(gws::audio::AudioEngine& engine) : engine_(engine) {}

    using OcclusionFn = std::function<float(const glm::vec3& listener, const glm::vec3& source)>;

    // Drive one frame. `listener_fallback` (usually the editor camera) is used
    // when the scene has no active AudioListener entity. `play_mode` lets
    // "Play on Start" sources sound during play without mutating the component.
    void update(scene::Scene* scene, const gws::audio::Listener& listener_fallback,
                const OcclusionFn& occ, float dt, bool play_mode) {
        if (!scene || !engine_.running()) return;

        // ---- Listener: first active AudioListener entity, else the fallback ----
        gws::audio::Listener lst = listener_fallback;
        glm::vec3 listener_pos = lst.position;
        for (const auto& e : scene->GetEntities()) {
            if (!e || !e->IsActive()) continue;
            auto al = e->GetComponent<scene::AudioListenerComponent>();
            if (al && al->IsActive()) {
                auto t = e->GetTransform();
                const glm::vec3 pos = t->GetWorldPosition();
                const glm::quat rot = t->GetWorldRotation();
                lst.position    = pos;
                lst.forward     = rot * glm::vec3(0.0f, 0.0f, -1.0f);
                lst.up          = rot * glm::vec3(0.0f, 1.0f, 0.0f);
                lst.master_gain = al->GetMasterGain();
                lst.velocity    = track_vel(listener_last_, e->GetId(), pos, dt);
                listener_pos    = pos;
                break;   // first active listener wins
            }
        }
        engine_.set_listener(lst);

        // ---- Sources ----
        seen_.clear();
        for (const auto& e : scene->GetEntities()) {
            if (!e) continue;
            auto src = e->GetComponent<scene::AudioSourceComponent>();
            if (!src) continue;
            const uint32_t key = e->GetId();
            const bool want_play =
                e->IsActive() && (src->IsPlaying() || (play_mode && src->PlayOnStart()));
            const auto it = voices_.find(key);
            if (!want_play) {
                if (it != voices_.end()) { engine_.stop(it->second); voices_.erase(it); }
                continue;
            }
            seen_.insert(key);
            const glm::vec3 pos = e->GetTransform()->GetWorldPosition();
            gws::audio::VoiceParams p;
            p.gain     = src->GetVolume();
            p.pitch    = src->GetPitch();
            p.radius   = src->GetRadius();
            p.looping  = src->IsLooping();
            p.spatial  = src->IsSpatial();
            p.position = pos;
            p.velocity = track_vel(source_last_, key, pos, dt);
            if (p.spatial && occ) p.occlusion = occ(listener_pos, pos);
            if (it == voices_.end()) {
                const gws::audio::ClipId clip = clip_for(*src);
                if (clip == gws::audio::kInvalidClip) {
                    spdlog::warn("[audio] entity {}: no playable clip '{}' — set a valid audio file",
                                 key, src->GetClipPath());
                    src->SetPlaying(false);   // avoid retrying the bad clip every frame
                    continue;
                }
                const gws::audio::VoiceId v = engine_.play(clip, p);
                if (v != gws::audio::kInvalidVoice) {
                    voices_[key] = v;
                    spdlog::info("[audio] entity {} playing '{}' (gain {:.2f}, radius {:.1f}, {})",
                                 key, src->GetClipPath(), p.gain, p.radius,
                                 p.spatial ? "spatial" : "2D");
                } else {
                    spdlog::warn("[audio] entity {}: mixer returned no voice (pool full?)", key);
                }
            } else {
                engine_.set_voice(it->second, p);   // live param update
            }
        }
        // Release voices whose source stopped / was removed this frame.
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (!seen_.count(it->first)) { engine_.stop(it->second); it = voices_.erase(it); }
            else ++it;
        }
    }

private:
    glm::vec3 track_vel(std::unordered_map<uint32_t, glm::vec3>& last,
                        uint32_t key, const glm::vec3& pos, float dt) {
        glm::vec3 v(0.0f);
        const auto it = last.find(key);
        if (it != last.end() && dt > 0.0f) v = (pos - it->second) / dt;
        last[key] = pos;
        return v;
    }

    // Resolve a source's clip by GUID, decoding the file once and caching it.
    gws::audio::ClipId clip_for(scene::AudioSourceComponent& src) {
        const uint64_t guid = src.GetClipGuid();
        if (!guid || src.GetClipPath().empty()) return gws::audio::kInvalidClip;
        const auto it = clips_.find(guid);
        if (it != clips_.end()) return it->second;   // may be a cached failure
        const gws::audio::ClipId clip = engine_.load_file(src.GetClipPath());
        if (clip == gws::audio::kInvalidClip)
            spdlog::warn("[audio] failed to load clip: {}", src.GetClipPath());
        clips_[guid] = clip;   // cache success OR failure (don't retry every frame)
        return clip;
    }

    gws::audio::AudioEngine&                            engine_;
    std::unordered_map<uint64_t, gws::audio::ClipId>   clips_;
    std::unordered_map<uint32_t, gws::audio::VoiceId>  voices_;
    std::unordered_map<uint32_t, glm::vec3>            source_last_;
    std::unordered_map<uint32_t, glm::vec3>            listener_last_;
    std::unordered_set<uint32_t>                       seen_;
};

}  // namespace schizo::editor
