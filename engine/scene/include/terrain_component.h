#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace schizo::scene {

/// Number of blendable terrain texture layers (one per splatmap channel).
inline constexpr int kTerrainLayers = 4;

// ============================================================================
// Terrain Component (heightmap landscape)
// ============================================================================
//
// A square heightmap landscape centered on the entity's origin, spanning
// `size_` × `size_` world units with `resolution_` cells per side (so
// (resolution_+1)² height samples, row-major as z*(res+1)+x). `heights_` are
// in world units (the mesh y = height * height_scale_). The editor's
// TerrainMeshCache rebuilds the GPU mesh whenever `version_` changes (bumped by
// every sculpt edit); a Jolt HeightField + the splatmap (later phases) watch
// the same counter.

class TerrainComponent : public Component {
public:
    explicit TerrainComponent(int resolution = 64, float size = 100.0f) {
        Resize(resolution, size);
        ResizeSplat(256);
    }
    virtual ~TerrainComponent() = default;

    TerrainComponent(const TerrainComponent&) = delete;
    TerrainComponent& operator=(const TerrainComponent&) = delete;
    TerrainComponent(TerrainComponent&&) = default;
    TerrainComponent& operator=(TerrainComponent&&) = default;

    // ---- Dimensions ----
    int   GetResolution() const { return resolution_; }
    float GetSize() const { return size_; }
    float GetHeightScale() const { return height_scale_; }
    void  SetHeightScale(float s) { height_scale_ = s; ++version_; }
    int   VertsPerSide() const { return resolution_ + 1; }

    /// Re-allocate the heightmap to `res` cells / `size` units, cleared flat.
    /// Also clears the hole mask and marks the whole terrain dirty.
    void Resize(int res, float size) {
        resolution_ = std::max(1, std::min(res, 2048));
        size_       = (size > 0.1f) ? size : 0.1f;
        const int n = VertsPerSide();
        heights_.assign(static_cast<size_t>(n) * n, 0.0f);
        holes_.assign(static_cast<size_t>(resolution_) * resolution_, 0);
        MarkAllDirty();
    }

    /// Set every height to `h` (flatten / reset).
    void Flatten(float h = 0.0f) {
        std::fill(heights_.begin(), heights_.end(), h);
        MarkAllDirty();
    }

    // ---- Height access (grid coords; row-major z*(res+1)+x) ----
    const std::vector<float>& Heights() const { return heights_; }
    std::vector<float>&       MutableHeights() { return heights_; }   // caller must MarkDirty()

    float HeightAt(int ix, int iz) const {
        ix = std::clamp(ix, 0, resolution_);
        iz = std::clamp(iz, 0, resolution_);
        return heights_[static_cast<size_t>(iz) * VertsPerSide() + ix];
    }
    void SetHeightAt(int ix, int iz, float h) {
        if (ix < 0 || iz < 0 || ix > resolution_ || iz > resolution_) return;
        heights_[static_cast<size_t>(iz) * VertsPerSide() + ix] = h;
    }

    /// Bilinearly sample the surface height at a LOCAL position (x,z relative to
    /// the entity origin, in world units). Returns height*height_scale_.
    float SampleHeightLocal(float lx, float lz) const {
        const float half = size_ * 0.5f;
        // Local [-half,half] → grid [0,res].
        const float gx = (lx + half) / size_ * resolution_;
        const float gz = (lz + half) / size_ * resolution_;
        const int x0 = static_cast<int>(std::floor(gx));
        const int z0 = static_cast<int>(std::floor(gz));
        const float fx = gx - x0;
        const float fz = gz - z0;
        const float h00 = HeightAt(x0,     z0);
        const float h10 = HeightAt(x0 + 1, z0);
        const float h01 = HeightAt(x0,     z0 + 1);
        const float h11 = HeightAt(x0 + 1, z0 + 1);
        const float h0 = h00 + (h10 - h00) * fx;
        const float h1 = h01 + (h11 - h01) * fx;
        return (h0 + (h1 - h0) * fz) * height_scale_;
    }

    /// World units per cell edge.
    float CellSize() const { return size_ / static_cast<float>(resolution_); }

    // ---- Dirty tracking (mesh / collision caches watch this) ----
    // `version_` bumps on every edit; the accumulated dirty CELL rect lets the
    // chunked mesh cache rebuild only touched chunks. MarkDirty() (legacy)
    // dirties everything; brushes should call MarkDirtyRect with their bounds.
    uint64_t Version() const { return version_; }
    void     MarkDirty() { MarkAllDirty(); }
    void MarkAllDirty() {
        dirty_min_x_ = 0; dirty_min_z_ = 0;
        dirty_max_x_ = resolution_; dirty_max_z_ = resolution_;
        ++version_;
    }
    /// Accumulate a dirty cell rect (inclusive cell coords, clamped).
    void MarkDirtyRect(int x0, int z0, int x1, int z1) {
        x0 = std::clamp(x0, 0, resolution_); z0 = std::clamp(z0, 0, resolution_);
        x1 = std::clamp(x1, 0, resolution_); z1 = std::clamp(z1, 0, resolution_);
        dirty_min_x_ = std::min(dirty_min_x_, x0);
        dirty_min_z_ = std::min(dirty_min_z_, z0);
        dirty_max_x_ = std::max(dirty_max_x_, x1);
        dirty_max_z_ = std::max(dirty_max_z_, z1);
        ++version_;
    }
    /// Read + clear the accumulated dirty rect. Returns false if nothing
    /// accumulated since the last consume. Single consumer: the mesh cache.
    bool ConsumeDirtyRect(int& x0, int& z0, int& x1, int& z1) {
        if (dirty_max_x_ < dirty_min_x_) return false;
        x0 = dirty_min_x_; z0 = dirty_min_z_; x1 = dirty_max_x_; z1 = dirty_max_z_;
        dirty_min_x_ = dirty_min_z_ = INT_MAX;
        dirty_max_x_ = dirty_max_z_ = INT_MIN;
        return true;
    }

    // ---- Holes (caves): per-CELL mask; hole cells are carved out of the
    // mesh AND the collision, so caves / underwater cave entrances can be
    // built through the terrain surface. ----
    bool HasHole(int cx, int cz) const {
        if (cx < 0 || cz < 0 || cx >= resolution_ || cz >= resolution_) return false;
        return holes_[static_cast<size_t>(cz) * resolution_ + cx] != 0;
    }
    void SetHole(int cx, int cz, bool hole) {
        if (cx < 0 || cz < 0 || cx >= resolution_ || cz >= resolution_) return;
        holes_[static_cast<size_t>(cz) * resolution_ + cx] = hole ? 1 : 0;
    }
    bool AnyHoles() const {
        for (uint8_t h : holes_) if (h) return true;
        return false;
    }
    const std::vector<uint8_t>& Holes() const { return holes_; }
    std::vector<uint8_t>&       MutableHoles() { return holes_; }

    // ---- Integrated water (Unreal-style: water is part of the terrain) ----
    // One water level per terrain, covering the full terrain rect at
    // origin.y + water_level. `water_physical_` splits PHYSICAL water
    // (buoyancy + swimming volumes in Play mode) from VISUAL-only water.
    bool  IsWaterEnabled() const { return water_enabled_; }
    void  SetWaterEnabled(bool e) { water_enabled_ = e; }
    bool  IsWaterPhysical() const { return water_physical_; }
    void  SetWaterPhysical(bool p) { water_physical_ = p; }
    float GetWaterLevel() const { return water_level_; }
    void  SetWaterLevel(float l) { water_level_ = l; }
    const glm::vec3& GetWaterDeepColor() const { return water_deep_; }
    void  SetWaterDeepColor(const glm::vec3& c) { water_deep_ = c; }
    const glm::vec3& GetWaterShallowColor() const { return water_shallow_; }
    void  SetWaterShallowColor(const glm::vec3& c) { water_shallow_ = c; }
    float GetWaterWaveHeight() const { return water_wave_height_; }
    void  SetWaterWaveHeight(float h) { water_wave_height_ = std::clamp(h, 0.0f, 5.0f); }
    float GetWaterWaveSpeed() const { return water_wave_speed_; }
    void  SetWaterWaveSpeed(float s) { water_wave_speed_ = std::clamp(s, 0.0f, 10.0f); }
    float GetWaterWaveScale() const { return water_wave_scale_; }
    void  SetWaterWaveScale(float s) { water_wave_scale_ = std::clamp(s, 0.5f, 200.0f); }
    float GetWaterClarity() const { return water_clarity_; }
    void  SetWaterClarity(float c) { water_clarity_ = std::clamp(c, 0.05f, 50.0f); }
    float GetWaterReflectivity() const { return water_reflectivity_; }
    void  SetWaterReflectivity(float r) { water_reflectivity_ = std::clamp(r, 0.0f, 1.0f); }

    // ========================================================================
    // Texture splat painting (Phase C)
    // ========================================================================
    //
    // Up to `kTerrainLayers` albedo textures (paths), blended per-texel by an
    // RGBA splatmap whose 4 channels are the per-layer weights. The terrain
    // G-buffer shader normalises the 4 weights. `tiling_[i]` is how many times
    // layer i repeats across the whole terrain. The GPU cache rebuilds the
    // splat texture / material when `splat_version_` changes (paint edits) or a
    // layer path / tiling changes.

    // ---- Layers ----
    const std::string& GetLayerPath(int i) const { return layer_paths_[clamp_layer(i)]; }
    void SetLayerPath(int i, const std::string& p) { layer_paths_[clamp_layer(i)] = p; ++splat_version_; }

    /// A layer's material asset (.mat). When set it supplies the layer's albedo,
    /// normal and metal/rough maps plus its tint and PBR factors, and the plain
    /// albedo path above is ignored — the same "an asset wins entirely" rule
    /// MeshRendererComponent follows, so a material behaves identically whether
    /// it is painted on terrain or assigned to an object.
    ///
    /// The legacy albedo path is kept rather than removed: every terrain painted
    /// before materials existed is stored that way, and dropping a single
    /// texture onto a layer is still the fastest way to block something out.
    const std::string& GetLayerMaterial(int i) const { return layer_materials_[clamp_layer(i)]; }
    void SetLayerMaterial(int i, const std::string& p) { layer_materials_[clamp_layer(i)] = p; ++splat_version_; }
    bool HasLayerMaterial(int i) const { return !layer_materials_[clamp_layer(i)].empty(); }

    /// How many times this layer repeats across the terrain.
    ///
    /// Deliberately NOT part of the material asset. The same rock has to tile
    /// perhaps 8 times across a 100 m terrain and 300 times across a 4 km one,
    /// so tiling is a property of this terrain's use of the material, not of the
    /// material — putting it in the .mat would make a shared rock unusable on
    /// two terrains of different sizes.
    float GetTiling(int i) const { return tiling_[clamp_layer(i)]; }
    void  SetTiling(int i, float t) { tiling_[clamp_layer(i)] = std::max(0.01f, t); ++splat_version_; }

    /// Per-layer surface response, used when the layer has NO material asset.
    ///
    /// Without these a layer could only be dielectric and almost fully rough,
    /// because the fallback material synthesised for a plain albedo texture hard
    /// coded metallic = 0 and roughness = 0.95. Terrain therefore could not be
    /// made to reflect anything at all unless you first authored a whole .mat
    /// file, which is a long way to go to make ground look wet.
    ///
    /// A layer WITH a material asset ignores these: an asset still wins
    /// entirely, so the same material looks identical on terrain and on an
    /// object. These are the fast path, not a competing one.
    ///
    /// Deliberately NOT bumping splat_version_. These land in the terrain
    /// uniform block, which the render bridge diffs separately -- so dragging a
    /// roughness slider stays a 144-byte memcpy instead of rebuilding the splat
    /// texture and the descriptor set.
    float GetLayerMetallic(int i) const { return layer_metallic_[clamp_layer(i)]; }
    void  SetLayerMetallic(int i, float v) { layer_metallic_[clamp_layer(i)] = std::clamp(v, 0.0f, 1.0f); }

    /// Clamped to a floor rather than 0: a perfectly smooth surface is a mirror
    /// the BRDF divides by, which the material asset loader guards the same way.
    float GetLayerRoughness(int i) const { return layer_roughness_[clamp_layer(i)]; }
    void  SetLayerRoughness(int i, float v) { layer_roughness_[clamp_layer(i)] = std::clamp(v, 0.02f, 1.0f); }

    /// Triplanar projection for this terrain's layers.
    ///
    /// A heightfield is UV-mapped from directly above, so the steeper a slope
    /// the further its texture is stretched -- a cliff is a smear of the few
    /// texels the top-down projection gave it. Per terrain rather than per
    /// layer: the stretch is a property of the surface, not of the texture on
    /// it, and four independent toggles would only produce terrain where some
    /// layers smear and others do not.
    ///
    /// Off by default. It triples the texture fetches where the blend is
    /// active, and a flat terrain gains nothing from paying that.
    bool  GetTriplanar() const { return triplanar_; }
    void  SetTriplanar(bool on) { triplanar_ = on; }

    /// How sharply one projection gives way to the next. Higher keeps each
    /// dominant over more of the surface and narrows the cross-fade.
    float GetTriplanarSharpness() const { return triplanar_sharpness_; }
    void  SetTriplanarSharpness(float v) { triplanar_sharpness_ = std::clamp(v, 1.0f, 16.0f); }

    float GetLayerNormalScale(int i) const { return layer_normal_scale_[clamp_layer(i)]; }
    void  SetLayerNormalScale(int i, float v) { layer_normal_scale_[clamp_layer(i)] = std::clamp(v, 0.0f, 4.0f); }

    // ---- Splatmap (RGBA8, row-major; channel = layer weight) ----
    int SplatResolution() const { return splat_res_; }
    const std::vector<uint8_t>& Splat() const { return splat_; }
    std::vector<uint8_t>&       MutableSplat() { return splat_; }   // caller must ++SplatVersion via MarkSplatDirty
    uint64_t SplatVersion() const { return splat_version_; }
    void     MarkSplatDirty() { ++splat_version_; }

    /// Re-allocate the splatmap to `res`×`res`, reset so layer 0 has full weight
    /// (R=255, others 0) — i.e. the whole terrain shows the base layer.
    void ResizeSplat(int res) {
        splat_res_ = std::clamp(res, 8, 2048);
        splat_.assign(static_cast<size_t>(splat_res_) * splat_res_ * 4, 0);
        for (size_t t = 0; t < static_cast<size_t>(splat_res_) * splat_res_; ++t)
            splat_[t * 4 + 0] = 255; // layer 0 base coat
        ++splat_version_;
    }

    /// Paint `layer`'s weight at LOCAL position (lx,lz in world units relative to
    /// the entity origin) with a radial brush. `amount` in [0,1] is how strongly
    /// to push toward this layer this call (the other channels are reduced so the
    /// 4 weights stay normalised). Bumps splat_version_.
    void PaintSplatLocal(float lx, float lz, float radius, int layer,
                         float amount, float falloff) {
        layer = clamp_layer(layer);
        const float half = size_ * 0.5f;
        // Local [-half,half] → splat texel space [0,res).
        const float cx = (lx + half) / size_ * splat_res_;
        const float cz = (lz + half) / size_ * splat_res_;
        const float tr = radius / size_ * splat_res_;   // brush radius in texels
        if (tr <= 0.0f) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - tr)));
        const int x1 = std::min(splat_res_ - 1, static_cast<int>(std::ceil(cx + tr)));
        const int z0 = std::max(0, static_cast<int>(std::floor(cz - tr)));
        const int z1 = std::min(splat_res_ - 1, static_cast<int>(std::ceil(cz + tr)));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = (x + 0.5f) - cx;
                const float dz = (z + 0.5f) - cz;
                const float d = std::sqrt(dx * dx + dz * dz) / tr;
                if (d > 1.0f) continue;
                const float fall = std::pow(std::max(0.0f, 1.0f - d), 0.5f + falloff * 2.0f);
                const float k = std::clamp(amount * fall, 0.0f, 1.0f);
                if (k <= 0.0f) continue;
                uint8_t* px = &splat_[(static_cast<size_t>(z) * splat_res_ + x) * 4];
                float w[4] = { px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f, px[3] / 255.0f };
                // Push target layer toward 1, scale the rest down, renormalise.
                w[layer] = w[layer] + (1.0f - w[layer]) * k;
                float others = w[0] + w[1] + w[2] + w[3] - w[layer];
                if (others > 1e-6f) {
                    const float keep = 1.0f - w[layer];
                    const float s = keep / others;
                    for (int c = 0; c < 4; ++c) if (c != layer) w[c] *= s;
                }
                for (int c = 0; c < 4; ++c)
                    px[c] = static_cast<uint8_t>(std::clamp(w[c], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        ++splat_version_;
    }

private:
    static int clamp_layer(int i) { return std::clamp(i, 0, kTerrainLayers - 1); }

    int                resolution_   = 64;
    float              size_         = 100.0f;
    float              height_scale_ = 1.0f;
    std::vector<float> heights_;
    uint64_t           version_      = 1;   // non-zero so a fresh cache (built==0) rebuilds

    // Dirty cell rect accumulated since the last ConsumeDirtyRect.
    int dirty_min_x_ = 0, dirty_min_z_ = 0;
    int dirty_max_x_ = 1 << 20, dirty_max_z_ = 1 << 20;   // "everything" initially

    std::vector<uint8_t> holes_;            // per-cell, 1 = carved out

    // Integrated water.
    bool      water_enabled_      = false;
    bool      water_physical_     = true;
    float     water_level_        = 0.5f;   // relative to the entity origin Y
    glm::vec3 water_deep_         = glm::vec3(0.02f, 0.12f, 0.18f);
    glm::vec3 water_shallow_      = glm::vec3(0.10f, 0.45f, 0.50f);
    float     water_wave_height_  = 0.18f;
    float     water_wave_speed_   = 1.0f;
    float     water_wave_scale_   = 14.0f;
    float     water_clarity_      = 4.0f;
    float     water_reflectivity_ = 0.8f;

    std::array<std::string, kTerrainLayers> layer_paths_{};       // legacy albedo-only
    std::array<std::string, kTerrainLayers> layer_materials_{};   // .mat, wins over the above
    std::array<float, kTerrainLayers>       tiling_{ 16.0f, 16.0f, 16.0f, 16.0f };
    // Defaults reproduce the old hard-coded fallback exactly -- dielectric and
    // 0.95 rough -- so every terrain authored before this looks unchanged.
    std::array<float, kTerrainLayers>       layer_metallic_{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, kTerrainLayers>       layer_roughness_{ 0.95f, 0.95f, 0.95f, 0.95f };
    std::array<float, kTerrainLayers>       layer_normal_scale_{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool                                   triplanar_ = false;
    float                                  triplanar_sharpness_ = 4.0f;
    int                  splat_res_     = 256;
    std::vector<uint8_t> splat_;            // RGBA8, res*res*4
    uint64_t             splat_version_ = 1;
};

} // namespace schizo::scene
