#pragma once

#include "entity.h"
#include <glm/glm.hpp>

namespace schizo::scene {

// ============================================================================
// Water Component (terrain expansion — water surfaces)
// ============================================================================
//
// A rectangular animated water surface centered on the entity's origin: the
// entity's world Y is the water level, `size_` spans the plane in X/Z. The
// water pass renders it as a procedural grid displaced by Gerstner-style
// waves, shaded with fresnel environment reflections, sun glint, and
// depth-based shore fade (shallow -> deep color).
//
// Purely visual in v1 — no buoyancy/swimming yet (planned follow-up).

class WaterComponent : public Component {
public:
    WaterComponent() = default;
    virtual ~WaterComponent() = default;

    // ---- Extent (world units, centered on the entity) ----
    const glm::vec2& GetSize() const { return size_; }
    void SetSize(const glm::vec2& s) {
        size_ = glm::max(s, glm::vec2(0.1f));
    }

    // ---- Colors ----
    /// Deep-water absorption color (what thick water tints toward).
    const glm::vec3& GetDeepColor() const { return deep_color_; }
    void SetDeepColor(const glm::vec3& c) { deep_color_ = c; }
    /// Shallow-water color near shores / thin water.
    const glm::vec3& GetShallowColor() const { return shallow_color_; }
    void SetShallowColor(const glm::vec3& c) { shallow_color_ = c; }

    // ---- Waves ----
    float GetWaveHeight() const { return wave_height_; }
    void  SetWaveHeight(float h) { wave_height_ = glm::clamp(h, 0.0f, 5.0f); }
    float GetWaveSpeed() const { return wave_speed_; }
    void  SetWaveSpeed(float s) { wave_speed_ = glm::clamp(s, 0.0f, 10.0f); }
    /// World-space length of the primary wave (smaller = choppier ripples).
    float GetWaveScale() const { return wave_scale_; }
    void  SetWaveScale(float s) { wave_scale_ = glm::clamp(s, 0.5f, 200.0f); }

    // ---- Look ----
    /// Depth (m) over which water fades from shallow to deep color / opaque.
    float GetClarity() const { return clarity_; }
    void  SetClarity(float c) { clarity_ = glm::clamp(c, 0.05f, 50.0f); }
    /// Overall reflection strength multiplier.
    float GetReflectivity() const { return reflectivity_; }
    void  SetReflectivity(float r) { reflectivity_ = glm::clamp(r, 0.0f, 1.0f); }

    // ---- Physical vs visual water ----
    /// Physical water is a gameplay volume in Play mode: dynamic bodies get
    /// buoyancy + drag, the player swims. Visual water only renders.
    bool IsPhysical() const { return physical_; }
    void SetPhysical(bool p) { physical_ = p; }

private:
    bool      physical_      = true;
    glm::vec2 size_          = glm::vec2(60.0f, 60.0f);
    glm::vec3 deep_color_    = glm::vec3(0.02f, 0.12f, 0.18f);
    glm::vec3 shallow_color_ = glm::vec3(0.10f, 0.45f, 0.50f);
    float     wave_height_   = 0.18f;
    float     wave_speed_    = 1.0f;
    float     wave_scale_    = 14.0f;
    float     clarity_       = 4.0f;
    float     reflectivity_  = 0.8f;
};

} // namespace schizo::scene
