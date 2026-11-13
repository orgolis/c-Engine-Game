#pragma once

// ============================================================================
// Saving and loading in Play (C6): F6 quick-saves, F9 quick-loads, and
// scripts save to and load from named slots (checkpoints).
// ============================================================================
//
// A save (ecs::PlaySave, checked by gameplay_check) holds where the player
// stands and looks and every gameplay entity with a SaveId -- the player
// (given SaveId 2 if it has none) and the world flags (SaveId 1) among them,
// with all their gameplay components: health, inventory, ammunition,
// abilities' settings, quests. Positions of other objects are not saved.
// Files go to saves/<slot>.sav under the project.

#include "ecs/gameplay_savegame.h"
#include "ecs_bridge.h"
#include "entity.h"
#include "scene_playback_manager.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace schizo::editor {

/// saves/<slot>.sav, the slot reduced to letters, digits, '-' and '_'.
inline std::string play_save_path(const std::string& slot) {
    std::string s;
    for (char c : slot)
        s += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    if (s.empty()) s = "quick";
    // GWS_SAVE_DIR puts saves elsewhere (tests keep them out of the project).
    const char* dir = std::getenv("GWS_SAVE_DIR");
    return std::string(dir && *dir ? dir : "saves") + "/" + s + ".sav";
}

namespace play_save_detail {
// The player's and the world flags' entities carry the engine's own keys.
inline void ensure_keys(ScenePlaybackManager& play, EcsSceneBridge& bridge) {
    namespace ecs = schizo::ecs;
    ecs::World& w = bridge.world();
    const auto flags = static_cast<ecs::Entity>(bridge.world_flags_holder());
    if (w.valid(flags) && !w.has<ecs::SaveId>(flags)) w.add<ecs::SaveId>(flags, ecs::SaveId{ecs::kSaveKeyWorldFlags});
    if (auto* pl = play.GetPlayerEntity()) {
        const uint32_t id = bridge.ecs_entity_id(pl->GetTransform());
        const auto e = static_cast<ecs::Entity>(id);
        if (id != kNoEcsEntity && w.valid(e) && !w.has<ecs::SaveId>(e)) w.add<ecs::SaveId>(e, ecs::SaveId{ecs::kSaveKeyPlayer});
    }
}
}  // namespace play_save_detail

inline bool save_play(const std::string& slot, ScenePlaybackManager& play, EcsSceneBridge& bridge, std::string* where = nullptr) {
    namespace ecs = schizo::ecs;
    if (!play.IsPlaying()) return false;
    play_save_detail::ensure_keys(play, bridge);
    ecs::PlaySave s;
    s.play_time = play.GetPlaybackTime();
    if (auto* pl = play.GetPlayerEntity()) {
        s.has_player = true;
        s.position = pl->GetTransform()->GetWorldPosition();
        s.rotation = pl->GetTransform()->GetWorldRotation();
        if (auto* cam = play.GetPlaybackCamera()) s.camera = cam->GetTransform()->GetLocalRotation();
    }
    s.game = ecs::SaveGame::capture(bridge.world());
    const std::string path = play_save_path(slot);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << s.to_text();
    if (where) *where = path;
    return static_cast<bool>(f);
}

inline bool load_play(const std::string& slot, ScenePlaybackManager& play, EcsSceneBridge& bridge, std::string* where = nullptr) {
    namespace ecs = schizo::ecs;
    if (!play.IsPlaying()) return false;
    const std::string path = play_save_path(slot);
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    ecs::PlaySave s;
    if (!ecs::PlaySave::from_text(ss.str(), s)) return false;
    play_save_detail::ensure_keys(play, bridge);
    s.game.apply(bridge.world());
    if (s.has_player) play.TeleportPlayer(s.position, s.rotation, &s.camera);
    if (where) *where = path;
    return true;
}

}  // namespace schizo::editor
