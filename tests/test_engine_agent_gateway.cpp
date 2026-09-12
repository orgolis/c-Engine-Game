#include <catch2/catch_test_macros.hpp>

#include "engine_agent_gateway.h"
#include "scene.h"
#include "script_component.h"
#include "undo_redo_manager.h"

#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

schizo::editor::EngineAgentApplyContext make_context(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const fs::path& root,
    schizo::editor::UndoRedoManager& undo) {
    schizo::editor::EngineAgentApplyContext context;
    context.scene = scene;
    context.project_root = root;
    context.undo = &undo;
    return context;
}

}  // namespace

TEST_CASE("Engine agent refuses paths outside its generated script folder",
          "[editor][engine-agent][security]") {
    auto scene = std::make_shared<schizo::scene::Scene>("Agent security test");
    schizo::editor::UndoRedoManager undo;
    const fs::path root = fs::temp_directory_path() / "gws-agent-security-test";
    std::error_code ec;
    fs::create_directories(root, ec);
    auto context = make_context(scene, root, undo);

    schizo::editor::EngineAgentPlan plan;
    plan.actions.push_back({
        .type = "write_script",
        .path = "../../editor/src/main.cpp",
        .content = "def on_start(e):\n    pass\n"
    });

    std::string error;
    REQUIRE_FALSE(schizo::editor::ValidateEngineAgentPlan(plan, context, error));
    REQUIRE_FALSE(error.empty());
    fs::remove_all(root, ec);
}

TEST_CASE("An applied engine agent plan is one undo and redo step",
          "[editor][engine-agent][undo]") {
    auto scene = std::make_shared<schizo::scene::Scene>("Agent undo test");
    schizo::editor::UndoRedoManager undo;
    const fs::path root = fs::temp_directory_path() / "gws-agent-undo-test";
    std::error_code ec;
    fs::create_directories(root / "assets", ec);
    auto context = make_context(scene, root, undo);

    schizo::editor::EngineAgentPlan plan;
    plan.actions.push_back({
        .type = "create_entity",
        .name = "AI Cube",
        .primitive = "cube",
        .position = {1.0f, 2.0f, 3.0f},
        .rotation_deg = {0.0f, 45.0f, 0.0f},
        .scale = {2.0f, 2.0f, 2.0f}
    });

    std::string error;
    REQUIRE(schizo::editor::ApplyEngineAgentPlan(plan, context, error));
    REQUIRE(scene->GetEntityByName("AI Cube") != nullptr);
    REQUIRE(undo.CanUndo());

    undo.Undo();
    REQUIRE(scene->GetEntityByName("AI Cube") == nullptr);
    REQUIRE(undo.CanRedo());

    undo.Redo();
    REQUIRE(scene->GetEntityByName("AI Cube") != nullptr);
    fs::remove_all(root, ec);
}

TEST_CASE("Engine agent can script an entity created earlier in the same plan",
          "[editor][engine-agent][gameplay]") {
    auto scene = std::make_shared<schizo::scene::Scene>("Agent gameplay test");
    schizo::editor::UndoRedoManager undo;
    const fs::path root = fs::temp_directory_path() / "gws-agent-created-target-test";
    std::error_code ec;
    fs::create_directories(root / "assets" / "scripts" / "ai_generated", ec);
    auto context = make_context(scene, root, undo);

    schizo::editor::EngineAgentPlan plan;
    plan.actions.push_back({
        .type = "write_script",
        .path = "assets/scripts/ai_generated/chaser.py",
        .content = "import engine\ndef on_update(e, dt):\n    engine.translate(e, 0, 0, -dt)\n"
    });
    plan.actions.push_back({
        .type = "create_entity",
        .name = "AI Chaser",
        .primitive = "cube",
        .position = {0.0f, 1.0f, 5.0f}
    });
    plan.actions.push_back({
        .type = "attach_script",
        .target_name = "AI Chaser",
        .path = "assets/scripts/ai_generated/chaser.py"
    });

    std::string error;
    REQUIRE(schizo::editor::ApplyEngineAgentPlan(plan, context, error));
    auto chaser = scene->GetEntityByName("AI Chaser");
    REQUIRE(chaser != nullptr);
    REQUIRE(chaser->GetComponent<schizo::scene::ScriptComponent>() != nullptr);
    REQUIRE(chaser->GetComponent<schizo::scene::ScriptComponent>()->GetScriptPath() ==
            "assets/scripts/ai_generated/chaser.py");

    undo.Undo();
    REQUIRE(scene->GetEntityByName("AI Chaser") == nullptr);
    undo.Redo();
    chaser = scene->GetEntityByName("AI Chaser");
    REQUIRE(chaser != nullptr);
    REQUIRE(chaser->GetComponent<schizo::scene::ScriptComponent>() != nullptr);
    fs::remove_all(root, ec);
}
