#include "scene_serializer.h"
#include "entity.h"
#include "transform.h"
#include "transform_component.h"
#include "mesh_renderer_component.h"
#include "mesh_component.h"
#include "light_component.h"
#include "collider_component.h"
#include "camera_component.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace schizo::editor {

// Line-based key=value format. Each entity is its own [ENTITY_n] block.
// Keys added in this revision (saved-but-not-required-on-load — older files
// missing them just inherit defaults):
//   PARENT_ID, MESH_PATH, MESH_USE_ASSET,
//   HAS_MESH_RENDERER, MESH_RENDERER_TYPE, MESH_RENDERER_COLOR,
//   HAS_TRANSFORM_COMP
// LIGHT_* keys are now both written AND applied via setters on load.

// ============================================================================
// Save
// ============================================================================

bool SceneSerializer::SaveScene(const std::string& filepath,
                                const std::shared_ptr<schizo::scene::Scene>& scene) {
    if (!scene) {
        spdlog::error("Cannot save null scene");
        return false;
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open file for writing: {}", filepath);
        return false;
    }

    try {
        file << "SCENE_NAME=" << scene->GetName() << "\n";
        file << "ENTITY_COUNT=" << scene->GetEntityCount() << "\n";
        file << "\n";

        const auto& entities = scene->GetEntities();
        for (size_t i = 0; i < entities.size(); ++i) {
            const auto& entity = entities[i];
            if (!entity) continue;

            file << "[ENTITY_" << i << "]\n";
            file << "NAME=" << entity->GetName() << "\n";
            file << "ID=" << entity->GetId() << "\n";
            file << "ACTIVE=" << (entity->IsActive() ? "1" : "0") << "\n";
            file << "TAG=" << entity->GetTag() << "\n";

            if (auto parent = entity->GetParent()) {
                file << "PARENT_ID=" << parent->GetId() << "\n";
            }

            if (auto t = entity->GetTransform()) {
                auto pos   = t->GetLocalPosition();
                auto rot   = t->GetLocalRotation();
                auto scale = t->GetLocalScale();
                file << "TRANSFORM_POS="   << pos.x   << "," << pos.y   << "," << pos.z   << "\n";
                file << "TRANSFORM_ROT="   << rot.x   << "," << rot.y   << "," << rot.z   << "," << rot.w << "\n";
                file << "TRANSFORM_SCALE=" << scale.x << "," << scale.y << "," << scale.z << "\n";
            }

            // Built-in MeshComponent (always present on Entity; only emit when
            // a mesh path is actually assigned to keep saves terse).
            if (auto mc = entity->GetMeshComponent()) {
                if (!mc->mesh_path.empty()) {
                    file << "MESH_PATH="       << mc->mesh_path << "\n";
                    file << "MESH_USE_ASSET="  << (mc->use_asset_mesh ? "1" : "0") << "\n";
                }
            }

            // TransformComponent (separate Component-derived wrapper users can
            // explicitly Add; presence-only marker, no extra data).
            if (entity->GetComponent<schizo::scene::TransformComponent>()) {
                file << "HAS_TRANSFORM_COMP=1\n";
            }

            // MeshRendererComponent — primitive type + color, used by the
            // editor's "+ Add Entity → Cube/Sphere/..." path.
            if (auto mr = entity->GetComponent<schizo::scene::MeshRendererComponent>()) {
                const auto& col = mr->GetColor();
                file << "HAS_MESH_RENDERER=1\n";
                file << "MESH_RENDERER_TYPE="  << static_cast<int>(mr->GetMeshType()) << "\n";
                file << "MESH_RENDERER_COLOR=" << col.r << "," << col.g << "," << col.b << "," << col.a << "\n";
                file << "MESH_RENDERER_ALPHA_MODE="   << static_cast<int>(mr->GetAlphaMode()) << "\n";
                // Preserve the old key for one cycle so scenes saved before
                // the AlphaMode enum still load — handled in the parser too.
                file << "MESH_RENDERER_TRANSPARENT=" << (mr->IsTransparent() ? "1" : "0") << "\n";
                file << "MESH_RENDERER_ALPHA_CUTOFF=" << mr->GetAlphaCutoff() << "\n";
                file << "MESH_RENDERER_METALLIC="    << mr->GetMetallic()    << "\n";
                file << "MESH_RENDERER_ROUGHNESS="   << mr->GetRoughness()   << "\n";
                file << "MESH_RENDERER_OCCLUSION="   << mr->GetOcclusion()   << "\n";
                auto e = mr->GetEmissive();
                file << "MESH_RENDERER_EMISSIVE="    << e.r << "," << e.g << "," << e.b << "\n";
            }

            // LightComponent — full property emission (previous version emitted
            // these keys but the load path ignored them).
            if (auto light_comp = entity->GetComponent<schizo::scene::LightComponent>()) {
                file << "LIGHT_TYPE="        << static_cast<int>(light_comp->GetType()) << "\n";
                auto color = light_comp->GetColor();
                file << "LIGHT_COLOR="       << color.r << "," << color.g << "," << color.b << "\n";
                file << "LIGHT_INTENSITY="   << light_comp->GetIntensity() << "\n";
                file << "LIGHT_TEMPERATURE=" << light_comp->GetTemperature() << "\n";

                if (light_comp->GetType() != schizo::scene::LightType::Directional) {
                    file << "LIGHT_RANGE=" << light_comp->GetRange() << "\n";
                }

                if (light_comp->GetType() == schizo::scene::LightType::Spot) {
                    auto angles = light_comp->GetSpotAngles();
                    file << "LIGHT_SPOT_INNER="   << angles.x << "\n";
                    file << "LIGHT_SPOT_OUTER="   << angles.y << "\n";
                    file << "LIGHT_SPOT_FALLOFF=" << light_comp->GetSpotFalloff() << "\n";
                }

                file << "LIGHT_CAST_SHADOW="    << (light_comp->GetCastShadow() ? "1" : "0") << "\n";
                file << "LIGHT_SHADOW_QUALITY=" << static_cast<int>(light_comp->GetShadowQuality()) << "\n";
                auto shadow_bias = light_comp->GetShadowBias();
                file << "LIGHT_SHADOW_BIAS="    << shadow_bias.x << "," << shadow_bias.y << "\n";
                file << "LIGHT_SHADOW_FILTER="  << light_comp->GetShadowFilterRadius() << "\n";
                auto shadow_planes = light_comp->GetShadowPlanes();
                file << "LIGHT_SHADOW_PLANES="  << shadow_planes.x << "," << shadow_planes.y << "\n";

                if (light_comp->GetType() == schizo::scene::LightType::Directional) {
                    file << "LIGHT_CASCADE_COUNT=" << light_comp->GetCascadeCount() << "\n";
                }

                file << "LIGHT_VOLUMETRIC=" << light_comp->GetVolumetricIntensity() << "\n";
            }

            // CameraComponent — intrinsic camera attached to the entity.
            if (auto cam = entity->GetComponent<schizo::scene::CameraComponent>()) {
                file << "HAS_CAMERA=1\n";
                file << "CAMERA_PROJECTION=" << static_cast<int>(cam->GetProjection()) << "\n";
                file << "CAMERA_FOV="        << cam->GetFOV()                << "\n";
                file << "CAMERA_NEAR="       << cam->GetNearPlane()          << "\n";
                file << "CAMERA_FAR="        << cam->GetFarPlane()           << "\n";
                file << "CAMERA_ORTHO_SIZE=" << cam->GetOrthographicSize()   << "\n";
                const auto& cc = cam->GetClearColor();
                file << "CAMERA_CLEAR_COLOR=" << cc.r << "," << cc.g << "," << cc.b << "," << cc.a << "\n";
            }

            // ColliderComponent — authoring data for Phase 2 physics. Only
            // fields relevant to the chosen shape are written; the loader
            // ignores irrelevant ones for that shape.
            if (auto col = entity->GetComponent<schizo::scene::ColliderComponent>()) {
                file << "HAS_COLLIDER=1\n";
                file << "COLLIDER_SHAPE=" << static_cast<int>(col->GetShape()) << "\n";
                auto off = col->GetOffset();
                file << "COLLIDER_OFFSET=" << off.x << "," << off.y << "," << off.z << "\n";
                file << "COLLIDER_DYNAMIC=" << (col->IsDynamic() ? "1" : "0") << "\n";
                file << "COLLIDER_MASS=" << col->GetMass() << "\n";
                file << "COLLIDER_TRIGGER=" << (col->IsTrigger() ? "1" : "0") << "\n";
                file << "COLLIDER_LAYER=" << static_cast<int>(col->GetLayer()) << "\n";
                file << "COLLIDER_MASK=" << col->GetMask() << "\n";

                switch (col->GetShape()) {
                    case schizo::scene::ColliderShape::Box: {
                        auto he = col->GetHalfExtents();
                        file << "COLLIDER_HALF_EXTENTS=" << he.x << "," << he.y << "," << he.z << "\n";
                        break;
                    }
                    case schizo::scene::ColliderShape::Sphere:
                        file << "COLLIDER_RADIUS=" << col->GetRadius() << "\n";
                        break;
                    case schizo::scene::ColliderShape::Capsule:
                        file << "COLLIDER_RADIUS=" << col->GetRadius() << "\n";
                        file << "COLLIDER_HEIGHT=" << col->GetHeight() << "\n";
                        break;
                    case schizo::scene::ColliderShape::Plane: {
                        auto n = col->GetPlaneNormal();
                        file << "COLLIDER_PLANE_NORMAL=" << n.x << "," << n.y << "," << n.z << "\n";
                        break;
                    }
                    default: break;
                }
            }

            file << "\n";
        }

        file.close();
        spdlog::info("Scene saved successfully to: {}", filepath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception while saving scene: {}", e.what());
        return false;
    }
}

// ============================================================================
// Load
// ============================================================================

namespace {

// Holds everything parsed for one entity until we can construct + wire it.
struct ParsedEntity {
    uint32_t saved_id = 0;
    uint32_t parent_id = 0;       // 0 = no parent
    std::string name = "Entity";
    std::string tag;
    bool active = true;

    glm::vec3 local_pos{0.0f};
    glm::quat local_rot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 local_scale{1.0f};

    std::string mesh_path;
    bool mesh_use_asset = false;

    bool has_transform_comp = false;

    bool has_mesh_renderer = false;
    int  mesh_renderer_type = 0;
    glm::vec4 mesh_renderer_color{1.0f};
    bool mesh_renderer_transparent = false;       // legacy key
    int  mesh_renderer_alpha_mode  = 0;            // 0=Opaque, 1=Cutout, 2=Blend
    bool mesh_renderer_has_alpha_mode = false;
    float mesh_renderer_alpha_cutoff = 0.5f;
    float mesh_renderer_metallic   = 0.05f;
    float mesh_renderer_roughness  = 0.7f;
    float mesh_renderer_occlusion  = 1.0f;
    glm::vec3 mesh_renderer_emissive{0.0f};

    bool has_light = false;
    int  light_type = 0;
    glm::vec3 light_color{1.0f};
    float light_intensity = 1.0f;
    float light_temperature = 0.0f;
    float light_range = 10.0f;
    glm::vec2 light_spot_angles{15.0f, 30.0f};
    float light_spot_falloff = 1.0f;
    bool  light_cast_shadow = false;
    int   light_shadow_quality = 0;
    glm::vec2 light_shadow_bias{0.005f, 0.0f};
    float light_shadow_filter = 2.0f;
    glm::vec2 light_shadow_planes{0.1f, 100.0f};
    int   light_cascade_count = 1;
    float light_volumetric = 0.0f;

    bool has_collider = false;
    int  collider_shape = 0;                       // Box
    glm::vec3 collider_half_extents{0.5f};
    float collider_radius = 0.5f;
    float collider_height = 1.0f;
    glm::vec3 collider_plane_normal{0.0f, 1.0f, 0.0f};
    glm::vec3 collider_offset{0.0f};
    bool collider_dynamic = false;
    float collider_mass = 1.0f;
    bool collider_trigger = false;
    int  collider_layer = 0;
    uint32_t collider_mask = 0xFFFFFFFFu;

    bool      has_camera         = false;
    int       camera_projection  = 0;   // 0=Perspective, 1=Orthographic
    float     camera_fov         = 60.0f;
    float     camera_near        = 0.1f;
    float     camera_far         = 1000.0f;
    float     camera_ortho_size  = 5.0f;
    glm::vec4 camera_clear_color = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};

// Helper: does `line` start with `key=`? If so, return the value portion.
bool starts_with(const std::string& line, const char* key, std::string& out_value) {
    size_t key_len = std::strlen(key);
    if (line.size() < key_len + 1) return false;
    if (line.compare(0, key_len, key) != 0) return false;
    if (line[key_len] != '=') return false;
    out_value = line.substr(key_len + 1);
    return true;
}

void apply_line_to_entity(ParsedEntity& p, const std::string& line) {
    std::string v;
    if (starts_with(line, "NAME", v)) { p.name = v; return; }
    if (starts_with(line, "ID", v))   { p.saved_id = static_cast<uint32_t>(std::stoul(v)); return; }
    if (starts_with(line, "ACTIVE", v)) { p.active = (v == "1"); return; }
    if (starts_with(line, "TAG", v))    { p.tag = v; return; }
    if (starts_with(line, "PARENT_ID", v)) { p.parent_id = static_cast<uint32_t>(std::stoul(v)); return; }

    if (starts_with(line, "TRANSFORM_POS", v)) {
        float x = 0, y = 0, z = 0;
        std::sscanf(v.c_str(), "%f,%f,%f", &x, &y, &z);
        p.local_pos = {x, y, z};
        return;
    }
    if (starts_with(line, "TRANSFORM_ROT", v)) {
        float x = 0, y = 0, z = 0, w = 1;
        std::sscanf(v.c_str(), "%f,%f,%f,%f", &x, &y, &z, &w);
        p.local_rot = glm::quat(w, x, y, z);  // glm::quat ctor is (w, x, y, z)
        return;
    }
    if (starts_with(line, "TRANSFORM_SCALE", v)) {
        float x = 1, y = 1, z = 1;
        std::sscanf(v.c_str(), "%f,%f,%f", &x, &y, &z);
        p.local_scale = {x, y, z};
        return;
    }

    if (starts_with(line, "MESH_PATH", v))      { p.mesh_path = v; return; }
    if (starts_with(line, "MESH_USE_ASSET", v)) { p.mesh_use_asset = (v == "1"); return; }

    if (starts_with(line, "HAS_TRANSFORM_COMP", v)) { p.has_transform_comp = (v == "1"); return; }

    if (starts_with(line, "HAS_MESH_RENDERER", v))    { p.has_mesh_renderer = (v == "1"); return; }
    if (starts_with(line, "MESH_RENDERER_TYPE", v))   { p.mesh_renderer_type = std::stoi(v); return; }
    if (starts_with(line, "MESH_RENDERER_COLOR", v)) {
        float r = 1, g = 1, b = 1, a = 1;
        std::sscanf(v.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
        p.mesh_renderer_color = {r, g, b, a};
        return;
    }
    if (starts_with(line, "MESH_RENDERER_ALPHA_MODE", v))   { p.mesh_renderer_alpha_mode = std::stoi(v); p.mesh_renderer_has_alpha_mode = true; return; }
    if (starts_with(line, "MESH_RENDERER_TRANSPARENT", v))  { p.mesh_renderer_transparent = (v == "1"); return; }
    if (starts_with(line, "MESH_RENDERER_ALPHA_CUTOFF", v)) { p.mesh_renderer_alpha_cutoff = std::stof(v); return; }
    if (starts_with(line, "MESH_RENDERER_METALLIC", v))     { p.mesh_renderer_metallic = std::stof(v); return; }
    if (starts_with(line, "MESH_RENDERER_ROUGHNESS", v))    { p.mesh_renderer_roughness = std::stof(v); return; }
    if (starts_with(line, "MESH_RENDERER_OCCLUSION", v))    { p.mesh_renderer_occlusion = std::stof(v); return; }
    if (starts_with(line, "MESH_RENDERER_EMISSIVE", v)) {
        float r = 0, g = 0, b = 0;
        std::sscanf(v.c_str(), "%f,%f,%f", &r, &g, &b);
        p.mesh_renderer_emissive = {r, g, b};
        return;
    }

    if (starts_with(line, "LIGHT_TYPE", v))        { p.has_light = true; p.light_type = std::stoi(v); return; }
    if (starts_with(line, "LIGHT_COLOR", v)) {
        float r = 1, g = 1, b = 1;
        std::sscanf(v.c_str(), "%f,%f,%f", &r, &g, &b);
        p.light_color = {r, g, b};
        return;
    }
    if (starts_with(line, "LIGHT_INTENSITY", v))   { p.light_intensity = std::stof(v); return; }
    if (starts_with(line, "LIGHT_TEMPERATURE", v)) { p.light_temperature = std::stof(v); return; }
    if (starts_with(line, "LIGHT_RANGE", v))       { p.light_range = std::stof(v); return; }
    if (starts_with(line, "LIGHT_SPOT_INNER", v))  { p.light_spot_angles.x = std::stof(v); return; }
    if (starts_with(line, "LIGHT_SPOT_OUTER", v))  { p.light_spot_angles.y = std::stof(v); return; }
    if (starts_with(line, "LIGHT_SPOT_FALLOFF", v)) { p.light_spot_falloff = std::stof(v); return; }
    if (starts_with(line, "LIGHT_CAST_SHADOW", v))  { p.light_cast_shadow = (v == "1"); return; }
    if (starts_with(line, "LIGHT_SHADOW_QUALITY", v)) { p.light_shadow_quality = std::stoi(v); return; }
    if (starts_with(line, "LIGHT_SHADOW_BIAS", v)) {
        float x = 0.005f, y = 0.0f;
        std::sscanf(v.c_str(), "%f,%f", &x, &y);
        p.light_shadow_bias = {x, y};
        return;
    }
    if (starts_with(line, "LIGHT_SHADOW_FILTER", v)) { p.light_shadow_filter = std::stof(v); return; }
    if (starts_with(line, "LIGHT_SHADOW_PLANES", v)) {
        float x = 0.1f, y = 100.0f;
        std::sscanf(v.c_str(), "%f,%f", &x, &y);
        p.light_shadow_planes = {x, y};
        return;
    }
    if (starts_with(line, "LIGHT_CASCADE_COUNT", v)) { p.light_cascade_count = std::stoi(v); return; }
    if (starts_with(line, "LIGHT_VOLUMETRIC", v))    { p.light_volumetric = std::stof(v); return; }

    if (starts_with(line, "HAS_COLLIDER", v))         { p.has_collider = (v == "1"); return; }
    if (starts_with(line, "COLLIDER_SHAPE", v))       { p.collider_shape = std::stoi(v); return; }
    if (starts_with(line, "COLLIDER_HALF_EXTENTS", v)) {
        float x = 0.5f, y = 0.5f, z = 0.5f;
        std::sscanf(v.c_str(), "%f,%f,%f", &x, &y, &z);
        p.collider_half_extents = {x, y, z};
        return;
    }
    if (starts_with(line, "COLLIDER_RADIUS", v))      { p.collider_radius = std::stof(v); return; }
    if (starts_with(line, "COLLIDER_HEIGHT", v))      { p.collider_height = std::stof(v); return; }
    if (starts_with(line, "COLLIDER_PLANE_NORMAL", v)) {
        float x = 0, y = 1, z = 0;
        std::sscanf(v.c_str(), "%f,%f,%f", &x, &y, &z);
        p.collider_plane_normal = {x, y, z};
        return;
    }
    if (starts_with(line, "COLLIDER_OFFSET", v)) {
        float x = 0, y = 0, z = 0;
        std::sscanf(v.c_str(), "%f,%f,%f", &x, &y, &z);
        p.collider_offset = {x, y, z};
        return;
    }
    if (starts_with(line, "COLLIDER_DYNAMIC", v)) { p.collider_dynamic = (v == "1"); return; }
    if (starts_with(line, "COLLIDER_MASS", v))    { p.collider_mass = std::stof(v); return; }
    if (starts_with(line, "COLLIDER_TRIGGER", v)) { p.collider_trigger = (v == "1"); return; }
    if (starts_with(line, "COLLIDER_LAYER", v))   { p.collider_layer = std::stoi(v); return; }
    if (starts_with(line, "COLLIDER_MASK", v))    {
        p.collider_mask = static_cast<uint32_t>(std::stoul(v));
        return;
    }

    if (starts_with(line, "HAS_CAMERA", v))         { p.has_camera = (v == "1"); return; }
    if (starts_with(line, "CAMERA_PROJECTION", v))  { p.camera_projection = std::stoi(v); return; }
    if (starts_with(line, "CAMERA_FOV", v))         { p.camera_fov = std::stof(v); return; }
    if (starts_with(line, "CAMERA_NEAR", v))        { p.camera_near = std::stof(v); return; }
    if (starts_with(line, "CAMERA_FAR", v))         { p.camera_far = std::stof(v); return; }
    if (starts_with(line, "CAMERA_ORTHO_SIZE", v))  { p.camera_ortho_size = std::stof(v); return; }
    if (starts_with(line, "CAMERA_CLEAR_COLOR", v)) {
        float r = 0.1f, g = 0.1f, b = 0.1f, a = 1.0f;
        std::sscanf(v.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
        p.camera_clear_color = {r, g, b, a};
        return;
    }
    // Unknown key — silently ignore for forward compatibility.
}

std::shared_ptr<schizo::scene::Entity> construct_entity(const ParsedEntity& p) {
    using namespace schizo::scene;

    auto e = std::make_shared<Entity>(p.name);
    e->SetTag(p.tag);
    e->SetActive(p.active);

    if (auto t = e->GetTransform()) {
        t->SetLocalPosition(p.local_pos);
        t->SetLocalRotation(p.local_rot);
        t->SetLocalScale(p.local_scale);
    }

    if (auto mc = e->GetMeshComponent(); !p.mesh_path.empty() && mc) {
        mc->mesh_path       = p.mesh_path;
        mc->use_asset_mesh  = p.mesh_use_asset;
    }

    if (p.has_transform_comp) {
        e->AddComponent<TransformComponent>();
    }

    if (p.has_mesh_renderer) {
        auto mr = e->AddComponent<MeshRendererComponent>(
            static_cast<MeshType>(p.mesh_renderer_type),
            p.mesh_renderer_color);
        if (mr) {
            // Prefer the new MESH_RENDERER_ALPHA_MODE key if present; fall
            // back to the legacy transparent bool so old saves still work.
            if (p.mesh_renderer_has_alpha_mode &&
                p.mesh_renderer_alpha_mode >= 0 &&
                p.mesh_renderer_alpha_mode <= 2) {
                mr->SetAlphaMode(static_cast<AlphaMode>(p.mesh_renderer_alpha_mode));
            } else {
                mr->SetTransparent(p.mesh_renderer_transparent);
            }
            mr->SetAlphaCutoff(p.mesh_renderer_alpha_cutoff);
            mr->SetMetallic(p.mesh_renderer_metallic);
            mr->SetRoughness(p.mesh_renderer_roughness);
            mr->SetOcclusion(p.mesh_renderer_occlusion);
            mr->SetEmissive(p.mesh_renderer_emissive);
        }
    }

    // Bounds-check the saved light type against the enum range to avoid
    // constructing a LightComponent with garbage that would UB downstream.
    if (p.has_light && p.light_type >= 0 &&
        p.light_type < static_cast<int>(LightType::MAX_TYPES)) {
        auto lc = e->AddComponent<LightComponent>(
            static_cast<LightType>(p.light_type), "Light");
        if (lc) {
            lc->SetColor(p.light_color);
            lc->SetIntensity(p.light_intensity);
            lc->SetTemperature(p.light_temperature);
            lc->SetRange(p.light_range);
            lc->SetSpotAngles(p.light_spot_angles.x, p.light_spot_angles.y);
            lc->SetSpotFalloff(p.light_spot_falloff);
            lc->SetCastShadow(p.light_cast_shadow);
            lc->SetShadowQuality(static_cast<ShadowQuality>(p.light_shadow_quality));
            lc->SetShadowBias(p.light_shadow_bias.x, p.light_shadow_bias.y);
            lc->SetShadowFilterRadius(p.light_shadow_filter);
            lc->SetShadowPlanes(p.light_shadow_planes.x, p.light_shadow_planes.y);
            lc->SetCascadeCount(static_cast<uint32_t>(std::max(1, p.light_cascade_count)));
            lc->SetVolumetricIntensity(p.light_volumetric);
        }
    }

    if (p.has_collider &&
        p.collider_shape >= 0 &&
        p.collider_shape < static_cast<int>(ColliderShape::MAX_SHAPES))
    {
        auto col = e->AddComponent<ColliderComponent>(
            static_cast<ColliderShape>(p.collider_shape));
        if (col) {
            col->SetHalfExtents(p.collider_half_extents);
            col->SetRadius(p.collider_radius);
            col->SetHeight(p.collider_height);
            col->SetPlaneNormal(p.collider_plane_normal);
            col->SetOffset(p.collider_offset);
            col->SetDynamic(p.collider_dynamic);
            col->SetMass(p.collider_mass);
            col->SetTrigger(p.collider_trigger);
            col->SetLayer(static_cast<uint8_t>(p.collider_layer));
            col->SetMask(p.collider_mask);
        }
    }

    if (p.has_camera) {
        auto cam = e->AddComponent<CameraComponent>();
        if (cam) {
            cam->SetProjection(p.camera_projection == 1
                                   ? CameraProjection::Orthographic
                                   : CameraProjection::Perspective);
            cam->SetFOV(p.camera_fov);
            cam->SetNearPlane(p.camera_near);
            cam->SetFarPlane(p.camera_far);
            cam->SetOrthographicSize(p.camera_ortho_size);
            cam->SetClearColor(p.camera_clear_color);
        }
    }

    return e;
}

} // namespace

std::shared_ptr<schizo::scene::Scene> SceneSerializer::LoadScene(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open file for reading: {}", filepath);
        return nullptr;
    }

    try {
        std::string scene_name = "Loaded_Scene";
        std::string line;

        // Read scene header. Stop after SCENE_NAME so we don't accidentally
        // consume the first entity's lines.
        while (std::getline(file, line)) {
            std::string v;
            if (starts_with(line, "SCENE_NAME", v)) { scene_name = v; break; }
        }

        auto scene = std::make_shared<schizo::scene::Scene>(scene_name);

        // Pass 1: parse every entity block into a ParsedEntity. We can't wire
        // parent_id yet — parent entity may appear later in the file.
        std::vector<ParsedEntity> parsed;
        ParsedEntity current;
        bool in_entity = false;

        auto finish_current = [&]() {
            if (in_entity) parsed.push_back(current);
            current = ParsedEntity{};
            in_entity = false;
        };

        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.compare(0, 8, "[ENTITY_") == 0) {
                finish_current();
                in_entity = true;
                continue;
            }
            if (in_entity) apply_line_to_entity(current, line);
        }
        finish_current();
        file.close();

        // Pass 2a: build entities + register them in the saved-id map.
        std::unordered_map<uint32_t, std::shared_ptr<schizo::scene::Entity>> id_map;
        std::vector<std::shared_ptr<schizo::scene::Entity>> entities;
        entities.reserve(parsed.size());
        for (const auto& p : parsed) {
            auto e = construct_entity(p);
            if (p.saved_id != 0) id_map[p.saved_id] = e;
            entities.push_back(e);
        }

        // Pass 2b: resolve parent references. Done after all entities exist
        // so the saved-id graph can be wired regardless of file order.
        for (size_t i = 0; i < parsed.size(); ++i) {
            uint32_t pid = parsed[i].parent_id;
            if (pid == 0) continue;
            auto it = id_map.find(pid);
            if (it == id_map.end()) {
                spdlog::warn("Entity '{}' refers to unknown parent_id {} — left at scene root",
                             parsed[i].name, pid);
                continue;
            }
            entities[i]->SetParent(it->second);
        }

        // Pass 3: register with the scene. Order doesn't affect hierarchy.
        for (auto& e : entities) scene->AddEntity(e);

        scene->Initialize();
        spdlog::info("Scene loaded successfully from: {} ({} entities)",
                     filepath, entities.size());
        return scene;
    } catch (const std::exception& e) {
        spdlog::error("Exception while loading scene: {}", e.what());
        return nullptr;
    }
}

} // namespace schizo::editor
