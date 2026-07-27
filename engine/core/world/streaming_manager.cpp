// ============================================================================
// streaming_manager.cpp — load/unload diff with hysteresis. See header.
// ============================================================================

#include "world/streaming_manager.h"

namespace schizo::world {

void StreamingManager::update(const glm::vec3& focus) {
    last_load_.clear();
    last_unload_.clear();

    // 1) Load every cell within the load radius that isn't already active.
    std::vector<CellCoord> desired;
    grid_.cells_in_radius(focus, load_r_, desired);
    for (const CellCoord& c : desired) {
        auto it = cells_.find(c);
        if (it == cells_.end() || it->second == CellState::Unloaded) {
            cells_[c] = CellState::Loading;
            last_load_.push_back(c);
            if (on_load_) on_load_(c);
        }
    }

    // 2) Unload active cells that have drifted beyond the unload radius. Cells
    //    between load_r and unload_r are left untouched (hysteresis).
    std::vector<CellCoord> drop;
    for (const auto& [c, st] : cells_) {
        if (st == CellState::Unloaded) continue;
        if (grid_.nearest_dist(focus, c) > unload_r_) drop.push_back(c);
    }
    for (const CellCoord& c : drop) {
        cells_.erase(c);
        last_unload_.push_back(c);
        if (on_unload_) on_unload_(c);
    }
}

void StreamingManager::mark_loaded(const CellCoord& c) {
    auto it = cells_.find(c);
    if (it != cells_.end() && it->second == CellState::Loading)
        it->second = CellState::Loaded;
}

CellState StreamingManager::state(const CellCoord& c) const {
    auto it = cells_.find(c);
    return it == cells_.end() ? CellState::Unloaded : it->second;
}

size_t StreamingManager::loaded_count() const {
    size_t n = 0;
    for (const auto& [c, st] : cells_) { (void)c; if (st == CellState::Loaded) ++n; }
    return n;
}

} // namespace schizo::world
