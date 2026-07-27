#pragma once

// ============================================================================
// WorldGrid — the spatial partition for streaming. Divides the world into a
// regular grid of square cells (XZ); maps world positions ⇄ cell coords and
// answers "which cells are within radius R of a focus point". (World-streaming
// pillar.) Pure math — the streaming manager builds on this.
// ============================================================================

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace schizo::world {

struct CellCoord {
    int x = 0, z = 0;
    bool operator==(const CellCoord& o) const { return x == o.x && z == o.z; }
};

struct CellCoordHash {
    size_t operator()(const CellCoord& c) const {
        // 2D integer hash (interleave-free, low collisions for world ranges).
        uint64_t h = static_cast<uint32_t>(c.x) * 73856093u
                   ^ static_cast<uint32_t>(c.z) * 19349663u;
        return static_cast<size_t>(h);
    }
};

class WorldGrid {
public:
    explicit WorldGrid(float cell_size = 64.0f) : cell_size_(cell_size) {}
    float cell_size() const { return cell_size_; }

    CellCoord to_cell(const glm::vec3& p) const {
        return {static_cast<int>(std::floor(p.x / cell_size_)),
                static_cast<int>(std::floor(p.z / cell_size_))};
    }

    glm::vec3 cell_center(const CellCoord& c) const {
        return {(c.x + 0.5f) * cell_size_, 0.0f, (c.z + 0.5f) * cell_size_};
    }
    glm::vec2 cell_min(const CellCoord& c) const { return {c.x * cell_size_, c.z * cell_size_}; }
    glm::vec2 cell_max(const CellCoord& c) const {
        return {(c.x + 1) * cell_size_, (c.z + 1) * cell_size_};
    }

    /// XZ distance from `focus` to the nearest point of cell `c` (0 if inside).
    float nearest_dist(const glm::vec3& focus, const CellCoord& c) const {
        const glm::vec2 mn = cell_min(c), mx = cell_max(c);
        const float cx = glm::clamp(focus.x, mn.x, mx.x);
        const float cz = glm::clamp(focus.z, mn.y, mx.y);
        const float dx = focus.x - cx, dz = focus.z - cz;
        return std::sqrt(dx * dx + dz * dz);
    }

    /// Append every cell whose nearest point is within `radius` of `focus`.
    void cells_in_radius(const glm::vec3& focus, float radius,
                         std::vector<CellCoord>& out) const {
        const int x0 = static_cast<int>(std::floor((focus.x - radius) / cell_size_));
        const int x1 = static_cast<int>(std::floor((focus.x + radius) / cell_size_));
        const int z0 = static_cast<int>(std::floor((focus.z - radius) / cell_size_));
        const int z1 = static_cast<int>(std::floor((focus.z + radius) / cell_size_));
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const CellCoord c{x, z};
                if (nearest_dist(focus, c) <= radius) out.push_back(c);
            }
    }

private:
    float cell_size_;
};

} // namespace schizo::world
