#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace schizo::scene {

// ============================================================================
// Water Component (terrain expansion — water surfaces)
// ============================================================================
//
// A rectangular animated water surface centered on the entity's origin: the
// entity's world Y is the water level, `size_` spans the plane in X/Z. Or a
// RIVER: a path of points (entity-local x, z; the level relative to the
// entity; a half width) that the surface follows as one continuous ribbon,
// its level falling along it and its ripples flowing downstream. The
// water pass renders it as a procedural grid displaced by Gerstner-style
// waves, shaded with fresnel environment reflections, sun glint, and
// depth-based shore fade (shallow -> deep color).
//
// Purely visual in v1 — no buoyancy/swimming yet (planned follow-up).

class WaterComponent final : public Component {
public:
    WaterComponent() = default;
    virtual ~WaterComponent() = default;

    // ---- Extent (world units, centered on the entity) ----
    const glm::vec2& GetSize() const { return size_; }
    void SetSize(const glm::vec2& s) { MarkChanged();
        size_ = glm::max(s, glm::vec2(0.1f));
    }

    // ---- Colors ----
    /// Deep-water absorption color (what thick water tints toward).
    const glm::vec3& GetDeepColor() const { return deep_color_; }
    void SetDeepColor(const glm::vec3& c) { MarkChanged(); deep_color_ = c; }
    /// Shallow-water color near shores / thin water.
    const glm::vec3& GetShallowColor() const { return shallow_color_; }
    void SetShallowColor(const glm::vec3& c) { MarkChanged(); shallow_color_ = c; }

    // ---- Waves ----
    float GetWaveHeight() const { return wave_height_; }
    void  SetWaveHeight(float h) { MarkChanged(); wave_height_ = glm::clamp(h, 0.0f, 5.0f); }
    float GetWaveSpeed() const { return wave_speed_; }
    void  SetWaveSpeed(float s) { MarkChanged(); wave_speed_ = glm::clamp(s, 0.0f, 10.0f); }
    /// World-space length of the primary wave (smaller = choppier ripples).
    float GetWaveScale() const { return wave_scale_; }
    void  SetWaveScale(float s) { MarkChanged(); wave_scale_ = glm::clamp(s, 0.5f, 200.0f); }

    // ---- Look ----
    /// Depth (m) over which water fades from shallow to deep color / opaque.
    float GetClarity() const { return clarity_; }
    void  SetClarity(float c) { MarkChanged(); clarity_ = glm::clamp(c, 0.05f, 50.0f); }
    /// Overall reflection strength multiplier.
    float GetReflectivity() const { return reflectivity_; }
    void  SetReflectivity(float r) { MarkChanged(); reflectivity_ = glm::clamp(r, 0.0f, 1.0f); }

    // ---- Physical vs visual water ----
    /// Physical water is a gameplay volume in Play mode: dynamic bodies get
    /// buoyancy + drag, the player swims. Visual water only renders.
    bool IsPhysical() const { return physical_; }
    void SetPhysical(bool p) { MarkChanged(); physical_ = p; }

    // ---- River ----
    /// The river's path, downstream: (local x, local z, level above the
    /// entity, half width). Two points or more make this a river.
    const std::vector<glm::vec4>& GetPath() const { return path_; }
    void SetPath(std::vector<glm::vec4> p) { MarkChanged(); path_ = std::move(p); }
    bool IsRiver() const { return path_.size() >= 2; }

    // ---- Lake shape ----
    /// Which parts of the rect are water: `nx` x `nz` cells over it, row by
    /// row (z, then x), nonzero = water. Without one the whole rect is. A lake
    /// is its basin, not its bounding box: the rect alone flooded every low
    /// place inside it -- a river valley crossing the box, another lake --
    /// and drew water over them.
    int  GetMaskNx() const { return mask_nx_; }
    int  GetMaskNz() const { return mask_nz_; }
    const std::vector<uint8_t>& GetMask() const { return mask_; }
    bool HasMask() const { return mask_nx_ > 0 && mask_nz_ > 0 && mask_.size() == static_cast<size_t>(mask_nx_) * mask_nz_; }
    void SetMask(int nx, int nz, std::vector<uint8_t> m) {
        MarkChanged();
        if (nx <= 0 || nz <= 0 || m.size() != static_cast<size_t>(nx) * nz) { mask_nx_ = mask_nz_ = 0; mask_.clear(); return; }
        mask_nx_ = nx; mask_nz_ = nz; mask_ = std::move(m);
    }
    void ClearMask() { MarkChanged(); mask_nx_ = mask_nz_ = 0; mask_.clear(); }
    /// Water at (dx, dz) from the centre (entity-local)? True everywhere in
    /// the rect without a mask; false outside it.
    bool IsWaterAt(float dx, float dz) const {
        if (std::abs(dx) > size_.x * 0.5f || std::abs(dz) > size_.y * 0.5f) return false;
        if (!HasMask()) return true;
        const int ix = std::clamp(static_cast<int>((dx / size_.x + 0.5f) * mask_nx_), 0, mask_nx_ - 1);
        const int iz = std::clamp(static_cast<int>((dz / size_.y + 0.5f) * mask_nz_), 0, mask_nz_ - 1);
        return mask_[static_cast<size_t>(iz) * mask_nx_ + ix] != 0;
    }

private:
    std::vector<glm::vec4> path_;
    std::vector<uint8_t>   mask_;
    int       mask_nx_       = 0;
    int       mask_nz_       = 0;
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
