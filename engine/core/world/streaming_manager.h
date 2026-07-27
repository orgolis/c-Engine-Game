#pragma once

// ============================================================================
// StreamingManager — decides which world cells load/unload around a focus point
// (the camera/player), with radius hysteresis to prevent boundary thrash.
// (World-streaming pillar.) The DECISION is pure logic + verifiable; the actual
// async asset load runs behind the load/unload callbacks (wired to the job
// system in the editor, driven synchronously in tests). A cell goes
// Unloaded → Loading (callback fires) → Loaded (loader calls mark_loaded).
// ============================================================================

#include "world/world_grid.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace schizo::world {

enum class CellState { Unloaded, Loading, Loaded };

class StreamingManager {
public:
    using CellFn = std::function<void(const CellCoord&)>;

    StreamingManager(WorldGrid grid, float load_radius, float unload_radius)
        : grid_(grid), load_r_(load_radius),
          unload_r_(std::max(unload_radius, load_radius + grid.cell_size())) {}

    /// on_load fires when a cell enters Loading (begin the async load);
    /// on_unload fires when a cell is dropped (release its content).
    void set_callbacks(CellFn on_load, CellFn on_unload) {
        on_load_ = std::move(on_load);
        on_unload_ = std::move(on_unload);
    }

    /// Diff the desired set (cells within load radius) against the tracked set:
    /// start loading newly-needed cells, unload cells beyond the unload radius.
    /// Cells in the [load, unload] band keep their current state (hysteresis).
    void update(const glm::vec3& focus);

    /// The async loader calls this when a cell's content finished loading.
    void mark_loaded(const CellCoord& c);

    CellState state(const CellCoord& c) const;
    size_t    active_count() const { return cells_.size(); }   // Loading + Loaded
    size_t    loaded_count() const;                            // Loaded only

    const std::vector<CellCoord>& last_loads()   const { return last_load_; }
    const std::vector<CellCoord>& last_unloads() const { return last_unload_; }
    const WorldGrid& grid() const { return grid_; }

private:
    WorldGrid grid_;
    float     load_r_, unload_r_;
    std::unordered_map<CellCoord, CellState, CellCoordHash> cells_;
    CellFn    on_load_, on_unload_;
    std::vector<CellCoord> last_load_, last_unload_;
};

} // namespace schizo::world
