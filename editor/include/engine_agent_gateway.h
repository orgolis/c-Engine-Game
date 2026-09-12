#pragma once

// ============================================================================
// Engine Agent Gateway
//
// This is the hard boundary between an AI model and the editor. A model never
// receives EditorState, a terminal, a filesystem handle, or an engine source
// path. It can only PROPOSE the typed actions below. The gateway validates the
// complete plan first, then translates it to ordinary undoable EditorCommands.
// ============================================================================

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace schizo::scene {
class Scene;
}

namespace schizo::editor {

class UndoRedoManager;

struct EngineAgentAction {
    // Supported values:
    // create_entity, set_transform, rename_entity, delete_entity, set_tag,
    // write_script, attach_script, write_model_obj, set_mesh, select_entity.
    std::string type;

    uint32_t entity_id = 0;
    std::string name;
    std::string primitive;   // empty/cube/sphere/plane/capsule/cylinder/...
    std::string path;        // always project-relative
    std::string content;     // safe Python or OBJ text

    // Local transform. The model always returns all three values so applying a
    // plan is deterministic and does not depend on a stale partial snapshot.
    glm::vec3 position{0.0f};
    glm::vec3 rotation_deg{0.0f};
    glm::vec3 scale{1.0f};
};

struct EngineAgentPlan {
    std::string message;
    std::vector<EngineAgentAction> actions;
};

struct EngineAgentApplyContext {
    std::shared_ptr<schizo::scene::Scene> scene;
    std::filesystem::path project_root;
    UndoRedoManager* undo = nullptr;
    std::function<void()> mark_scene_modified;
    std::function<void(uint32_t)> select_entity;
    std::function<void()> refresh_assets;
};

// The provider receives only this snapshot, never the serialized scene file or
// an absolute project/engine path.
std::string BuildEngineAgentSceneSnapshot(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    uint32_t selected_entity_id);

// Shared prompt/schema for Codex and Claude. Both providers therefore have the
// same capabilities and the same restrictions.
std::string BuildEngineAgentPrompt(const std::string& user_request,
                                   const std::string& scene_snapshot);
std::string EngineAgentOutputSchemaJson();

bool ParseEngineAgentPlan(const std::string& provider_output,
                          EngineAgentPlan& out,
                          std::string& error);

bool ValidateEngineAgentPlan(const EngineAgentPlan& plan,
                             const EngineAgentApplyContext& context,
                             std::string& error);

bool ApplyEngineAgentPlan(const EngineAgentPlan& plan,
                          const EngineAgentApplyContext& context,
                          std::string& error);

std::string DescribeEngineAgentAction(const EngineAgentAction& action);

}  // namespace schizo::editor
