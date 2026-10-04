#include <catch2/catch_test_macros.hpp>

#include "engine_agent_gateway.h"
#include "camera_component.h"
#include "scene.h"
#include "script_component.h"
#include "undo_redo_manager.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

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

TEST_CASE("AI reloads attached generated scripts after restart and rejects aliases",
          "[editor][engine-agent][ai-context][security]") {
    auto scene = std::make_shared<schizo::scene::Scene>("Reload context test");
    schizo::editor::UndoRedoManager undo;
    const fs::path root = fs::temp_directory_path() / "gws-agent-reload-context-test";
    std::error_code ec;
    fs::create_directories(root / "assets/scripts/ai_generated", ec);
    auto context = make_context(scene, root, undo);
    schizo::editor::EngineAgentPlan plan;
    schizo::editor::EngineAgentAction write;
    write.type = "write_script";
    write.path = "assets/scripts/ai_generated/combat.py";
    write.content = "import engine\ndef on_update(e, dt):\n    engine.translate(e, 0, 0, dt)\n";
    plan.actions.push_back(write);
    schizo::editor::EngineAgentAction create;
    create.type = "create_entity";
    create.name = "Combat controller";
    create.primitive = "empty";
    plan.actions.push_back(create);
    schizo::editor::EngineAgentAction attach;
    attach.type = "attach_script";
    attach.target_name = create.name;
    attach.path = write.path;
    plan.actions.push_back(attach);
    std::string error;
    REQUIRE(schizo::editor::ApplyEngineAgentPlan(plan, context, error));
    // Loading uses only the saved scene and project, no panel/session state.
    auto loaded = schizo::editor::LoadEngineAgentGeneratedScripts(context, error);
    REQUIRE(error.empty());
    REQUIRE(loaded.actions.size() == 1);
    REQUIRE(loaded.actions[0].content == write.content);
    auto outside = std::make_shared<schizo::scene::Entity>("User script");
    outside->AddComponent<schizo::scene::ScriptComponent>()->SetScriptPath("assets/scripts/player.py");
    scene->AddEntity(outside);
    REQUIRE(schizo::editor::LoadEngineAgentGeneratedScripts(context, error).actions.size() == 1);
    fs::create_symlink(root / write.path, root / "assets/scripts/ai_generated/alias.py", ec);
    if (!ec) {
        auto alias = std::make_shared<schizo::scene::Entity>("Alias");
        alias->AddComponent<schizo::scene::ScriptComponent>()->SetScriptPath("assets/scripts/ai_generated/alias.py");
        scene->AddEntity(alias);
        REQUIRE(schizo::editor::LoadEngineAgentGeneratedScripts(context, error).actions.size() == 1);
        REQUIRE_FALSE(error.empty());
    }
    fs::remove_all(root, ec);
}

TEST_CASE("AI prompt describes actual script semantics rather than silent no-ops",
          "[editor][engine-agent][ai-prompt]") {
    const std::string prompt = schizo::editor::BuildEngineAgentPrompt(
        "Create enemies that chase the player", "{\"selected_entity_id\":0}");
    REQUIRE(prompt.find("INTEGER entity id") != std::string::npos);
    REQUIRE(prompt.find("WORLD space") != std::string::npos);
    REQUIRE(prompt.find("OWN VM/module globals") != std::string::npos);
    REQUIRE(prompt.find("pre-existing ECS components") != std::string::npos);
    REQUIRE(prompt.find("attach_script REPLACES") != std::string::npos);
    REQUIRE(prompt.find("not necessarily the player") != std::string::npos);
    REQUIRE(prompt.find("Create enemies that chase the player") != std::string::npos);
}

TEST_CASE("AI scene metadata identifies cameras and disabled scripts without reading files",
          "[editor][engine-agent][ai-context]") {
    auto scene = std::make_shared<schizo::scene::Scene>("AI metadata test");
    auto camera = std::make_shared<schizo::scene::Entity>("Main Camera");
    camera->AddComponent<schizo::scene::CameraComponent>();
    camera->GetTransform()->SetLocalPosition({1.0f, 2.0f, 3.0f});
    auto script = camera->AddComponent<schizo::scene::ScriptComponent>();
    script->SetScriptPath("assets/scripts/player.py");
    script->SetEnabled(false);
    scene->AddEntity(camera);
    const auto snapshot = nlohmann::json::parse(
        schizo::editor::BuildEngineAgentSceneSnapshot(scene, camera->GetId()));
    REQUIRE(snapshot["selected_entity_id"] == camera->GetId());
    const auto& item = snapshot["entities"][0];
    REQUIRE(item["has_camera"] == true);
    REQUIRE(item["active"] == true);
    REQUIRE(item["script_enabled"] == false);
    REQUIRE(item["world_position"] == nlohmann::json::array({1.0f, 2.0f, 3.0f}));
    REQUIRE_FALSE(item.contains("content"));
}

TEST_CASE("AI correction preserves request and safety boundary",
          "[editor][engine-agent][ai-repair]") {
    const std::string prompt = schizo::editor::BuildEngineAgentPrompt("Move my cube", "{}");
    const std::string repaired = schizo::editor::BuildEngineAgentRepairPrompt(
        prompt, "Action 2: target must be an existing entity id.");
    REQUIRE(repaired.starts_with(prompt));
    REQUIRE(repaired.find("Action 2: target must be an existing entity id.") != std::string::npos);
    REQUIRE(repaired.find("COMPLETE replacement JSON") != std::string::npos);
    REQUIRE(repaired.find("safety limits remain unchanged") != std::string::npos);
}

TEST_CASE("AI follow-ups receive only previous generated scripts, never engine code",
          "[editor][engine-agent][ai-context][security]") {
    schizo::editor::EngineAgentPlan previous;
    previous.actions.push_back({.type = "write_script", .path = "assets/scripts/ai_generated/combat.py",
                                .content = "import engine\ndef on_start(e):\n    engine.log('combat ready')\n"});
    previous.actions.push_back({.type = "write_script", .path = "editor/src/main.cpp",
                                .content = "PRIVATE_ENGINE_SOURCE"});
    previous.actions.push_back({.type = "write_script", .path = "assets/scripts/player.py",
                                .content = "PRIVATE_USER_SCRIPT"});
    const std::string prompt = schizo::editor::BuildEngineAgentPrompt("Add pursuit", "{}", &previous);
    REQUIRE(prompt.find("combat ready") != std::string::npos);
    REQUIRE(prompt.find("PRIVATE_ENGINE_SOURCE") == std::string::npos);
    REQUIRE(prompt.find("PRIVATE_USER_SCRIPT") == std::string::npos);
    REQUIRE(prompt.find("SAME generated script path") != std::string::npos);
}

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
