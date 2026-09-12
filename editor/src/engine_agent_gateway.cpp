#include "engine_agent_gateway.h"

#include "entity.h"
#include "entity_factory.h"
#include "mesh_component.h"
#include "scene.h"
#include "script_component.h"
#include "transform.h"
#include "undo_redo_manager.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace schizo::editor {
namespace {

constexpr size_t kMaxActions = 48;
constexpr size_t kMaxScriptBytes = 64 * 1024;
constexpr size_t kMaxObjBytes = 1024 * 1024;

bool finite_vec(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string path_key(const fs::path& p) {
    std::string value = p.lexically_normal().generic_string();
#ifdef _WIN32
    value = lower(std::move(value));
#endif
    while (value.size() > 1 && value.back() == '/') value.pop_back();
    return value;
}

bool is_within(const fs::path& child, const fs::path& parent) {
    const std::string c = path_key(child);
    const std::string p = path_key(parent);
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 &&
                      c[p.size()] == '/');
}

std::optional<fs::path> resolve_allowed_file(const fs::path& project_root,
                                             const std::string& relative,
                                             const fs::path& allowed_subdir,
                                             const std::set<std::string>& extensions,
                                             std::string& error) {
    if (relative.empty()) {
        error = "The plan contains an empty asset path.";
        return std::nullopt;
    }
    fs::path rel(relative);
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
        error = "AI asset paths must be relative to the project.";
        return std::nullopt;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(project_root, ec);
    if (ec || root.empty()) {
        error = "The active project root could not be resolved.";
        return std::nullopt;
    }
    const fs::path allowed = fs::weakly_canonical(root / allowed_subdir, ec);
    if (ec) {
        error = "The protected AI asset folder could not be resolved.";
        return std::nullopt;
    }
    const fs::path candidate = fs::weakly_canonical(root / rel, ec);
    if (ec || !is_within(candidate, allowed)) {
        error = "AI writes are restricted to '" + allowed_subdir.generic_string() + "'.";
        return std::nullopt;
    }
    if (!extensions.count(lower(candidate.extension().string()))) {
        error = "The AI is not allowed to create this file type: " +
                candidate.extension().string();
        return std::nullopt;
    }
    return candidate;
}

std::optional<fs::path> resolve_script(const EngineAgentApplyContext& context,
                                       const std::string& relative,
                                       std::string& error) {
    return resolve_allowed_file(context.project_root, relative,
                                fs::path("assets") / "scripts" / "ai_generated",
                                {".py"}, error);
}

std::optional<fs::path> resolve_model(const EngineAgentApplyContext& context,
                                      const std::string& relative,
                                      std::string& error) {
    return resolve_allowed_file(context.project_root, relative,
                                fs::path("assets") / "generated" / "models",
                                {".obj"}, error);
}

std::optional<fs::path> resolve_existing_mesh(const EngineAgentApplyContext& context,
                                              const std::string& relative,
                                              std::string& error) {
    if (relative.empty()) {
        error = "The mesh path is empty.";
        return std::nullopt;
    }
    fs::path rel(relative);
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory()) {
        error = "Mesh paths must be project-relative.";
        return std::nullopt;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(context.project_root, ec);
    const fs::path assets = fs::weakly_canonical(root / "assets", ec);
    const fs::path candidate = fs::weakly_canonical(root / rel, ec);
    static const std::set<std::string> allowed{".obj", ".gltf", ".glb", ".fbx",
                                                ".usd", ".usda", ".usdc", ".usdz", ".pak"};
    if (ec || !is_within(candidate, assets) || !allowed.count(lower(candidate.extension().string()))) {
        error = "Meshes must be supported files below the project's assets folder.";
        return std::nullopt;
    }
    return candidate;
}

bool safe_python(const std::string& source, std::string& error) {
    if (source.empty() || source.size() > kMaxScriptBytes ||
        source.find('\0') != std::string::npos) {
        error = "AI Python scripts must contain 1 to 65536 text bytes.";
        return false;
    }
    // PocketPy is also constructed with enable_os=false for ai_generated paths.
    // These checks make the refusal understandable before Play is pressed.
    const std::string s = lower(source);
    static const char* forbidden[] = {
        "import os", "from os", "import io", "from io", "open(",
        "__import__", "eval(", "exec(", "compile("
    };
    for (const char* token : forbidden) {
        if (s.find(token) != std::string::npos) {
            error = std::string("Generated scripts cannot use '") + token + "'.";
            return false;
        }
    }
    if (s.find("def on_start(") == std::string::npos &&
        s.find("def on_update(") == std::string::npos) {
        error = "A gameplay script must define on_start(e) or on_update(e, dt).";
        return false;
    }
    return true;
}

bool safe_obj(const std::string& source, std::string& error) {
    if (source.empty() || source.size() > kMaxObjBytes ||
        source.find('\0') != std::string::npos) {
        error = "Generated OBJ models must contain 1 byte to 1 MiB of text.";
        return false;
    }
    std::istringstream in(source);
    std::string line;
    size_t vertices = 0;
    size_t faces = 0;
    while (std::getline(in, line)) {
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        const size_t end = line.find_first_of(" \t\r", first);
        const std::string op = line.substr(first, end - first);
        static const std::set<std::string> allowed{
            "v", "vn", "vt", "f", "o", "g", "s", "usemtl"
        };
        if (!allowed.count(op)) {
            error = "Generated OBJ contains a forbidden directive: " + op;
            return false;
        }
        if (op == "v" && ++vertices > 20000) {
            error = "Generated OBJ exceeds the 20000 vertex limit.";
            return false;
        }
        if (op == "f" && ++faces > 20000) {
            error = "Generated OBJ exceeds the 20000 face limit.";
            return false;
        }
    }
    if (vertices < 3 || faces < 1) {
        error = "Generated OBJ needs at least three vertices and one face.";
        return false;
    }
    return true;
}

glm::vec3 vec3_from_json(const json& value, const glm::vec3& fallback) {
    if (!value.is_array() || value.size() != 3) return fallback;
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

json parse_provider_json(const std::string& text) {
    std::string clean = text;
    const size_t fence = clean.find("```json");
    if (fence != std::string::npos) {
        const size_t begin = clean.find('\n', fence);
        const size_t end = clean.find("```", begin == std::string::npos ? fence + 7 : begin + 1);
        if (begin != std::string::npos && end != std::string::npos)
            clean = clean.substr(begin + 1, end - begin - 1);
    }
    const size_t begin = clean.find('{');
    const size_t end = clean.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end < begin)
        throw std::runtime_error("provider returned no JSON object");
    json root = json::parse(clean.substr(begin, end - begin + 1));

    // Claude --output-format json wraps validated output; support both that
    // format and plain JSON so the gateway is independent of CLI versions.
    if (root.contains("structured_output") && root["structured_output"].is_object())
        root = root["structured_output"];
    else if (root.contains("result") && root["result"].is_string())
        root = json::parse(root["result"].get<std::string>());
    return root;
}

std::shared_ptr<schizo::scene::Entity> create_kind(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const EngineAgentAction& a) {
    const std::string kind = lower(a.primitive);
    if (kind == "cube") return schizo::scene::EntityFactory::CreateCube(scene, a.name);
    if (kind == "sphere") return schizo::scene::EntityFactory::CreateSphere(scene, a.name);
    if (kind == "plane") return schizo::scene::EntityFactory::CreatePlane(scene, a.name);
    if (kind == "capsule") return schizo::scene::EntityFactory::CreateCapsule(scene, a.name);
    if (kind == "cylinder") return schizo::scene::EntityFactory::CreateCylinder(scene, a.name);
    if (kind == "camera") return schizo::scene::EntityFactory::CreateCamera(scene, a.name);
    if (kind == "directional_light")
        return schizo::scene::EntityFactory::CreateDirectionalLight(scene, a.name);
    if (kind == "global_light")
        return schizo::scene::EntityFactory::CreateGlobalLight(scene, a.name);
    auto entity = scene->CreateEntity(a.name);
    if (entity && kind == "asset" && !a.path.empty()) entity->SetMesh(a.path);
    return entity;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write_text(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("[EngineAgent] cannot write {}", path.string());
        return;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::unique_ptr<EditorCommand> file_write_command(
    const fs::path& path, std::string content, std::string description,
    const std::function<void()>& refresh_assets) {
    std::error_code ec;
    const bool existed = fs::is_regular_file(path, ec);
    const std::string before = existed ? read_text(path) : std::string{};
    return std::make_unique<FunctionCommand>(
        [path, content = std::move(content), refresh_assets]() {
            write_text(path, content);
            if (refresh_assets) refresh_assets();
        },
        [path, existed, before, refresh_assets]() {
            if (existed) write_text(path, before);
            else {
                std::error_code remove_ec;
                fs::remove(path, remove_ec);
            }
            if (refresh_assets) refresh_assets();
        },
        std::move(description));
}

}  // namespace

std::string BuildEngineAgentSceneSnapshot(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    uint32_t selected_entity_id) {
    json root;
    root["selected_entity_id"] = selected_entity_id;
    root["coordinate_system"] = "right handed; local transforms; forward is -Z; Euler XYZ degrees";
    root["entities"] = json::array();
    if (!scene) return root.dump(2);
    root["scene_name"] = scene->GetName();
    for (const auto& entity : scene->GetEntities()) {
        if (!entity) continue;
        const auto* transform = entity->GetTransform();
        const glm::vec3 p = transform->GetLocalPosition();
        const glm::vec3 r = glm::degrees(glm::eulerAngles(transform->GetLocalRotation()));
        const glm::vec3 s = transform->GetLocalScale();
        json item{
            {"id", entity->GetId()},
            {"name", entity->GetName()},
            {"tag", entity->GetTag()},
            {"parent_id", entity->GetParent() ? entity->GetParent()->GetId() : 0},
            {"position", {p.x, p.y, p.z}},
            {"rotation_deg", {r.x, r.y, r.z}},
            {"scale", {s.x, s.y, s.z}},
            {"mesh", entity->GetMeshComponent()->mesh_path}
        };
        if (auto script = entity->GetComponent<schizo::scene::ScriptComponent>())
            item["script"] = script->GetScriptPath();
        root["entities"].push_back(std::move(item));
    }
    return root.dump(2);
}

std::string EngineAgentOutputSchemaJson() {
    // Every action has the same required fields. Irrelevant values are empty/0.
    // This is intentionally repetitive: strict structured-output providers are
    // substantially more reliable when there are no action-specific unions.
    return R"JSON({
  "type": "object",
  "additionalProperties": false,
  "required": ["message", "actions"],
  "properties": {
    "message": {"type": "string"},
    "actions": {
      "type": "array",
      "maxItems": 48,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["type", "entity_id", "name", "primitive", "path", "content", "position", "rotation_deg", "scale"],
        "properties": {
          "type": {"type": "string", "enum": ["create_entity", "set_transform", "rename_entity", "delete_entity", "set_tag", "write_script", "attach_script", "write_model_obj", "set_mesh", "select_entity"]},
          "entity_id": {"type": "integer", "minimum": 0},
          "name": {"type": "string"},
          "primitive": {"type": "string", "enum": ["", "empty", "cube", "sphere", "plane", "capsule", "cylinder", "camera", "directional_light", "global_light", "asset"]},
          "path": {"type": "string"},
          "content": {"type": "string"},
          "position": {"type": "array", "minItems": 3, "maxItems": 3, "items": {"type": "number"}},
          "rotation_deg": {"type": "array", "minItems": 3, "maxItems": 3, "items": {"type": "number"}},
          "scale": {"type": "array", "minItems": 3, "maxItems": 3, "items": {"type": "number"}}
        }
      }
    }
  }
})JSON";
}

std::string BuildEngineAgentPrompt(const std::string& user_request,
                                   const std::string& scene_snapshot) {
    std::ostringstream out;
    out << "You are the WorldShaper Engine Author Agent. You edit a GAME PROJECT, never the engine.\n"
        << "Return exactly one JSON object matching the supplied schema and no markdown.\n"
        << "You cannot run commands, access files, use a terminal, edit engine/editor source, create native code, or create editor extensions.\n"
        << "Only propose the actions listed below. The editor validates everything and the user applies it manually.\n\n"
        << "ACTION RULES\n"
        << "- create_entity: primitive is empty/cube/sphere/plane/capsule/cylinder/camera/directional_light/global_light/asset. For asset, path is an existing project-relative mesh.\n"
        << "- set_transform: entity_id must be from the snapshot. Supply the complete LOCAL position, Euler XYZ rotation in degrees, and scale.\n"
        << "- rename_entity/delete_entity/set_tag/select_entity: entity_id must be from the snapshot.\n"
        << "- write_script: only assets/scripts/ai_generated/*.py. PocketPy has the engine module but no OS/file access. Define on_start(e) or on_update(e, dt).\n"
        << "- attach_script: path must be an AI-generated Python path.\n"
        << "- write_model_obj: only assets/generated/models/*.obj. Produce a small self-contained Wavefront OBJ using v/vt/vn/f/o/g/s/usemtl only; never mtllib.\n"
        << "- set_mesh: path must be a supported mesh below assets/.\n"
        << "- If a field is irrelevant use 0, empty string, position [0,0,0], rotation_deg [0,0,0], scale [1,1,1].\n"
        << "- A newly created entity cannot be targeted by a later action in the same plan; include its final transform/path in create_entity.\n"
        << "- Prefer the fewest actions. Explain the proposed result briefly in message.\n\n"
        << "AVAILABLE SAFE PYTHON API\n"
        << "import engine; engine.log, dt, time, find, get_position, set_position, get_rotation, set_rotation, get_scale, set_scale, key_down, mouse_down, mouse_delta, spawn_cube, spawn_sphere, destroy, set_velocity, add_impulse, raycast, set_color, set_emissive, audio_play, audio_stop, get_attribute, set_attribute, adjust_attribute, has_tag, add_tag, remove_tag, emit_event, distance, translate, get_forward.\n\n"
        << "CURRENT SCENE SNAPSHOT\n" << scene_snapshot << "\n\n"
        << "USER REQUEST\n" << user_request << '\n';
    return out.str();
}

bool ParseEngineAgentPlan(const std::string& provider_output,
                          EngineAgentPlan& out,
                          std::string& error) {
    try {
        const json root = parse_provider_json(provider_output);
        if (!root.is_object() || !root.contains("message") || !root.contains("actions") ||
            !root["message"].is_string() || !root["actions"].is_array()) {
            error = "The provider response does not contain message and actions.";
            return false;
        }
        EngineAgentPlan parsed;
        parsed.message = root["message"].get<std::string>();
        for (const json& item : root["actions"]) {
            if (!item.is_object()) throw std::runtime_error("an action is not an object");
            EngineAgentAction a;
            a.type = item.value("type", "");
            a.entity_id = item.value("entity_id", 0u);
            a.name = item.value("name", "");
            a.primitive = item.value("primitive", "");
            a.path = item.value("path", "");
            a.content = item.value("content", "");
            if (item.contains("position"))
                a.position = vec3_from_json(item["position"], glm::vec3(0.0f));
            if (item.contains("rotation_deg"))
                a.rotation_deg = vec3_from_json(item["rotation_deg"], glm::vec3(0.0f));
            if (item.contains("scale"))
                a.scale = vec3_from_json(item["scale"], glm::vec3(1.0f));
            parsed.actions.push_back(std::move(a));
        }
        out = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        error = std::string("Could not parse the provider plan: ") + e.what();
        return false;
    }
}

bool ValidateEngineAgentPlan(const EngineAgentPlan& plan,
                             const EngineAgentApplyContext& context,
                             std::string& error) {
    if (!context.scene || !context.undo || context.project_root.empty()) {
        error = "Open a project and scene before applying an AI plan.";
        return false;
    }
    if (plan.actions.size() > kMaxActions) {
        error = "The plan contains more than 48 actions.";
        return false;
    }

    static const std::set<std::string> action_types{
        "create_entity", "set_transform", "rename_entity", "delete_entity", "set_tag",
        "write_script", "attach_script", "write_model_obj", "set_mesh", "select_entity"
    };
    static const std::set<std::string> primitives{
        "empty", "cube", "sphere", "plane", "capsule", "cylinder", "camera",
        "directional_light", "global_light", "asset"
    };

    std::unordered_set<uint32_t> deleted;
    std::set<std::string> planned_files;
    for (size_t i = 0; i < plan.actions.size(); ++i) {
        const EngineAgentAction& a = plan.actions[i];
        const std::string prefix = "Action " + std::to_string(i + 1) + ": ";
        if (!action_types.count(a.type)) {
            error = prefix + "unknown action '" + a.type + "'.";
            return false;
        }
        if (!finite_vec(a.position) || !finite_vec(a.rotation_deg) || !finite_vec(a.scale) ||
            a.scale.x <= 0.0001f || a.scale.y <= 0.0001f || a.scale.z <= 0.0001f ||
            glm::any(glm::greaterThan(glm::abs(a.position), glm::vec3(100000.0f))) ||
            glm::any(glm::greaterThan(glm::abs(a.scale), glm::vec3(10000.0f)))) {
            error = prefix + "transform is outside the safe numeric limits.";
            return false;
        }

        const bool targets_entity = a.type == "set_transform" || a.type == "rename_entity" ||
            a.type == "delete_entity" || a.type == "set_tag" || a.type == "attach_script" ||
            a.type == "set_mesh" || a.type == "select_entity";
        if (targets_entity && (!a.entity_id || !context.scene->GetEntityById(a.entity_id))) {
            error = prefix + "entity id does not exist in the current scene.";
            return false;
        }
        if (targets_entity && deleted.count(a.entity_id)) {
            error = prefix + "targets an entity deleted earlier in the plan.";
            return false;
        }
        if (a.type == "delete_entity") deleted.insert(a.entity_id);

        if (a.type == "create_entity") {
            if (a.name.empty() || a.name.size() > 128 || !primitives.count(lower(a.primitive))) {
                error = prefix + "invalid entity name or primitive.";
                return false;
            }
            if (lower(a.primitive) == "asset") {
                auto p = resolve_existing_mesh(context, a.path, error);
                if (!p || (!fs::is_regular_file(*p) && !planned_files.count(path_key(*p)))) {
                    if (error.empty()) error = prefix + "asset mesh does not exist.";
                    return false;
                }
            }
        } else if (a.type == "rename_entity" && (a.name.empty() || a.name.size() > 128)) {
            error = prefix + "entity names must contain 1 to 128 characters.";
            return false;
        } else if (a.type == "set_tag" && a.name.size() > 128) {
            error = prefix + "tags may contain at most 128 characters.";
            return false;
        } else if (a.type == "write_script") {
            auto p = resolve_script(context, a.path, error);
            if (!p || !safe_python(a.content, error)) return false;
            planned_files.insert(path_key(*p));
        } else if (a.type == "attach_script") {
            auto p = resolve_script(context, a.path, error);
            if (!p || (!fs::is_regular_file(*p) && !planned_files.count(path_key(*p)))) {
                if (error.empty()) error = prefix + "script does not exist and is not created by this plan.";
                return false;
            }
        } else if (a.type == "write_model_obj") {
            auto p = resolve_model(context, a.path, error);
            if (!p || !safe_obj(a.content, error)) return false;
            planned_files.insert(path_key(*p));
        } else if (a.type == "set_mesh") {
            auto p = resolve_existing_mesh(context, a.path, error);
            if (!p || (!fs::is_regular_file(*p) && !planned_files.count(path_key(*p)))) {
                if (error.empty()) error = prefix + "mesh does not exist and is not created by this plan.";
                return false;
            }
        }
    }
    return true;
}

bool ApplyEngineAgentPlan(const EngineAgentPlan& plan,
                          const EngineAgentApplyContext& context,
                          std::string& error) {
    if (!ValidateEngineAgentPlan(plan, context, error)) return false;

    auto transaction = std::make_unique<CompositeCommand>(
        "AI plan (" + std::to_string(plan.actions.size()) + " actions)");
    const auto mark = context.mark_scene_modified;

    for (const EngineAgentAction& a : plan.actions) {
        if (a.type == "create_entity") {
            auto made = std::make_shared<std::vector<std::shared_ptr<schizo::scene::Entity>>>();
            auto root_id = std::make_shared<uint32_t>(0);
            const auto scene = context.scene;
            const auto select = context.select_entity;
            transaction->Add(std::make_unique<FunctionCommand>(
                [scene, a, made, root_id, mark, select]() {
                    if (made->empty()) {
                        std::unordered_set<uint32_t> before;
                        for (const auto& e : scene->GetEntities()) if (e) before.insert(e->GetId());
                        auto root = create_kind(scene, a);
                        if (root) {
                            root->GetTransform()->SetLocalPosition(a.position);
                            root->GetTransform()->SetLocalRotation(glm::quat(glm::radians(a.rotation_deg)));
                            root->GetTransform()->SetLocalScale(a.scale);
                            *root_id = root->GetId();
                        }
                        for (const auto& e : scene->GetEntities())
                            if (e && !before.count(e->GetId())) made->push_back(e);
                    } else {
                        for (const auto& e : *made)
                            if (e && !scene->GetEntityById(e->GetId())) scene->AddEntity(e);
                    }
                    if (select && *root_id) select(*root_id);
                    if (mark) mark();
                },
                [scene, made, root_id, mark]() {
                    for (auto it = made->rbegin(); it != made->rend(); ++it)
                        if (*it && scene->GetEntityById((*it)->GetId())) scene->RemoveEntity(*it);
                    (void)root_id;
                    if (mark) mark();
                },
                "AI: create " + a.name));
        } else if (a.type == "set_transform") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            const glm::vec3 old_p = entity->GetTransform()->GetLocalPosition();
            const glm::quat old_r = entity->GetTransform()->GetLocalRotation();
            const glm::vec3 old_s = entity->GetTransform()->GetLocalScale();
            transaction->Add(std::make_unique<FunctionCommand>(
                [entity, a, mark]() {
                    entity->GetTransform()->SetLocalPosition(a.position);
                    entity->GetTransform()->SetLocalRotation(glm::quat(glm::radians(a.rotation_deg)));
                    entity->GetTransform()->SetLocalScale(a.scale);
                    if (mark) mark();
                },
                [entity, old_p, old_r, old_s, mark]() {
                    entity->GetTransform()->SetLocalPosition(old_p);
                    entity->GetTransform()->SetLocalRotation(old_r);
                    entity->GetTransform()->SetLocalScale(old_s);
                    if (mark) mark();
                },
                "AI: transform " + entity->GetName()));
        } else if (a.type == "rename_entity") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            const std::string before = entity->GetName();
            transaction->Add(std::make_unique<FunctionCommand>(
                [entity, name = a.name, mark]() { entity->SetName(name); if (mark) mark(); },
                [entity, before, mark]() { entity->SetName(before); if (mark) mark(); },
                "AI: rename " + before));
        } else if (a.type == "set_tag") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            const std::string before = entity->GetTag();
            transaction->Add(std::make_unique<FunctionCommand>(
                [entity, tag = a.name, mark]() { entity->SetTag(tag); if (mark) mark(); },
                [entity, before, mark]() { entity->SetTag(before); if (mark) mark(); },
                "AI: tag " + entity->GetName()));
        } else if (a.type == "delete_entity") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            const auto scene = context.scene;
            transaction->Add(std::make_unique<FunctionCommand>(
                [scene, entity, mark]() { scene->RemoveEntity(entity); if (mark) mark(); },
                [scene, entity, mark]() { scene->AddEntity(entity); if (mark) mark(); },
                "AI: delete " + entity->GetName()));
        } else if (a.type == "write_script") {
            auto path = resolve_script(context, a.path, error);
            transaction->Add(file_write_command(*path, a.content,
                                                "AI: write safe script", context.refresh_assets));
        } else if (a.type == "write_model_obj") {
            auto path = resolve_model(context, a.path, error);
            transaction->Add(file_write_command(*path, a.content,
                                                "AI: write OBJ model", context.refresh_assets));
        } else if (a.type == "attach_script") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            auto previous = entity->GetComponent<schizo::scene::ScriptComponent>();
            const bool created = !previous;
            if (!previous) previous = std::make_shared<schizo::scene::ScriptComponent>();
            const std::string before = previous->GetScriptPath();
            transaction->Add(std::make_unique<FunctionCommand>(
                [entity, previous, path = a.path, created, mark]() {
                    if (created && !entity->GetComponent<schizo::scene::ScriptComponent>())
                        entity->AddComponent<schizo::scene::ScriptComponent>(*previous);
                    auto current = entity->GetComponent<schizo::scene::ScriptComponent>();
                    if (current) current->SetScriptPath(path);
                    if (mark) mark();
                },
                [entity, before, created, mark]() {
                    auto current = entity->GetComponent<schizo::scene::ScriptComponent>();
                    if (current) {
                        if (created) entity->RemoveComponent(current);
                        else current->SetScriptPath(before);
                    }
                    if (mark) mark();
                },
                "AI: attach safe script"));
        } else if (a.type == "set_mesh") {
            auto entity = context.scene->GetEntityById(a.entity_id);
            const std::string before = entity->GetMeshComponent()->mesh_path;
            transaction->Add(std::make_unique<FunctionCommand>(
                [entity, path = a.path, mark]() { entity->SetMesh(path); if (mark) mark(); },
                [entity, before, mark]() { entity->SetMesh(before); if (mark) mark(); },
                "AI: set mesh on " + entity->GetName()));
        } else if (a.type == "select_entity") {
            const auto select = context.select_entity;
            transaction->Add(std::make_unique<FunctionCommand>(
                [select, id = a.entity_id]() { if (select) select(id); },
                [] {}, "AI: select entity"));
        }
    }

    if (transaction->Empty()) {
        error = "The AI returned no changes to apply.";
        return false;
    }
    context.undo->ExecuteCommand(std::move(transaction));
    spdlog::info("[EngineAgent] applied {} validated actions as one undo step",
                 plan.actions.size());
    return true;
}

std::string DescribeEngineAgentAction(const EngineAgentAction& a) {
    if (a.type == "create_entity") return "Create " + a.primitive + " '" + a.name + "'";
    if (a.type == "set_transform") return "Set transform of entity " + std::to_string(a.entity_id);
    if (a.type == "rename_entity") return "Rename entity " + std::to_string(a.entity_id) + " to '" + a.name + "'";
    if (a.type == "delete_entity") return "Delete entity " + std::to_string(a.entity_id);
    if (a.type == "set_tag") return "Set tag of entity " + std::to_string(a.entity_id) + " to '" + a.name + "'";
    if (a.type == "write_script") return "Write safe Python script " + a.path;
    if (a.type == "attach_script") return "Attach " + a.path + " to entity " + std::to_string(a.entity_id);
    if (a.type == "write_model_obj") return "Generate OBJ model " + a.path;
    if (a.type == "set_mesh") return "Set mesh of entity " + std::to_string(a.entity_id) + " to " + a.path;
    if (a.type == "select_entity") return "Select entity " + std::to_string(a.entity_id);
    return a.type;
}

}  // namespace schizo::editor
