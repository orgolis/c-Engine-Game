#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace schizo::scene {

// ============================================================================
// Audio Components (Stage 6 — Pillar G)
// ============================================================================
//
// Authoring-layer (OOP) audio components. The editor's ECS bridge mirrors these
// into the ECS `AudioSource` / `AudioListener` POD components, which the
// gws::audio::AudioScene bridge turns into mixer voices + a listener pose. The
// owning entity's Transform supplies world position (and velocity for Doppler).

/**
 * @class AudioSourceComponent
 * @brief A sound emitter attached to an entity.
 */
class AudioSourceComponent : public Component {
public:
    AudioSourceComponent() = default;
    virtual ~AudioSourceComponent() = default;

    AudioSourceComponent(const AudioSourceComponent&) = delete;
    AudioSourceComponent& operator=(const AudioSourceComponent&) = delete;
    AudioSourceComponent(AudioSourceComponent&&) = default;
    AudioSourceComponent& operator=(AudioSourceComponent&&) = default;

    // Clip: `clip_path_` is the serialized/human reference; `clip_guid_` is the
    // content GUID the audio engine registers the decoded clip under. Setting
    // the path auto-derives the GUID (FNV-1a 64 with the low bit set — matches
    // schizo::assets::asset_id_from_path) so the two never drift apart.
    const std::string& GetClipPath() const { return clip_path_; }
    void               SetClipPath(const std::string& p) {
        clip_path_ = p;
        clip_guid_ = p.empty() ? 0 : guid_from_path(p);
    }
    uint64_t           GetClipGuid() const { return clip_guid_; }
    void               SetClipGuid(uint64_t g) { clip_guid_ = g; }

    float GetVolume() const { return volume_; }
    void  SetVolume(float v) { volume_ = v < 0.0f ? 0.0f : v; }
    float GetPitch() const { return pitch_; }
    void  SetPitch(float p) { pitch_ = p < 0.01f ? 0.01f : p; }
    float GetRadius() const { return radius_; }
    void  SetRadius(float r) { radius_ = r < 0.0f ? 0.0f : r; }

    bool IsLooping() const { return looping_; }
    void SetLooping(bool b) { looping_ = b; }
    bool IsSpatial() const { return spatial_; }
    void SetSpatial(bool b) { spatial_ = b; }
    bool PlayOnStart() const { return play_on_start_; }
    void SetPlayOnStart(bool b) { play_on_start_ = b; }

    // Runtime-only: whether the source should currently sound (edit-mode preview
    // or Play mode). Not serialized.
    bool IsPlaying() const { return playing_; }
    void SetPlaying(bool b) { playing_ = b; }

private:
    static uint64_t guid_from_path(const std::string& p) {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : p) { h ^= c; h *= 1099511628211ull; }
        return h | 1ull;
    }

    std::string clip_path_;
    uint64_t    clip_guid_     = 0;
    float       volume_        = 1.0f;
    float       pitch_         = 1.0f;
    float       radius_        = 10.0f;
    bool        looping_       = false;
    bool        spatial_       = true;
    // Default ON (Unity's "Play On Awake"): a freshly added source with a clip
    // is audible as soon as Play starts. Defaulting this off was the #1 cause
    // of "audio not working" reports — the source sat silent with no feedback.
    bool        play_on_start_ = true;
    bool        playing_       = false;   // runtime only — not serialized
};

/**
 * @class AudioListenerComponent
 * @brief The "ears" — one active listener (usually the camera/player) drives the
 * mixer's pan/attenuation. Pose comes from the entity's Transform.
 */
class AudioListenerComponent : public Component {
public:
    AudioListenerComponent() = default;
    virtual ~AudioListenerComponent() = default;

    AudioListenerComponent(const AudioListenerComponent&) = delete;
    AudioListenerComponent& operator=(const AudioListenerComponent&) = delete;
    AudioListenerComponent(AudioListenerComponent&&) = default;
    AudioListenerComponent& operator=(AudioListenerComponent&&) = default;

    float GetMasterGain() const { return master_gain_; }
    void  SetMasterGain(float g) { master_gain_ = glm::clamp(g, 0.0f, 1.0f); }
    bool  IsActive() const { return active_; }
    void  SetActive(bool b) { active_ = b; }

private:
    float master_gain_ = 1.0f;
    bool  active_      = true;
};

} // namespace schizo::scene
