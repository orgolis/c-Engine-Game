#include "play_mode_changes.h"

#include "ecs_bridge.h"
#include "entity.h"
#include "scene.h"
#include "transform.h"

#include "ecs/authorable_components.h"
#include "ecs/world.h"
#include "reflection/reflection.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace schizo::editor {
namespace {

// Field types this can render in a diff summary. Anything else still DIFFS
// correctly (the byte compare is exact) — it just reports "changed" instead of
// showing the values, which is the honest thing to print for a type we cannot
// format.
const auto TID_FLOAT = gws::reflect::type_id_of<float>();
const auto TID_I32   = gws::reflect::type_id_of<int32_t>();
const auto TID_U32   = gws::reflect::type_id_of<uint32_t>();
const auto TID_BOOL  = gws::reflect::type_id_of<bool>();

std::string fmt_f(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
    return buf;
}

// Format one reflected field's value, or "" when the type is not one we render.
std::string field_value(const gws::reflect::FieldInfo& f, const void* base) {
    const void* p = static_cast<const char*>(base) + f.offset;
    if (f.type_id == TID_FLOAT) return fmt_f(*static_cast<const float*>(p));
    if (f.type_id == TID_I32)   return std::to_string(*static_cast<const int32_t*>(p));
    if (f.type_id == TID_U32)   return std::to_string(*static_cast<const uint32_t*>(p));
    if (f.type_id == TID_BOOL)  return *static_cast<const bool*>(p) ? "true" : "false";
    return {};
}

// Per-field summary for a POD component, e.g. "current 100 -> 42". Falls back to
// naming the changed fields when their types are not renderable, and to a byte
// count for data-driven components with no reflected layout.
std::string summarize(const schizo::ecs::AuthorableComponent& ct,
                      const std::vector<uint8_t>& before_bytes,
                      const void* after_obj) {
    if (!ct.type) {
        return "contents changed (" + std::to_string(before_bytes.size()) +
               " bytes authored)";
    }
    // Rebuild the authored value so fields can be compared in typed form rather
    // than as opaque bytes.
    std::vector<uint8_t> scratch(ct.type->size, 0);
    schizo::ecs::deserialize_authorable(ct, scratch.data(),
                                        before_bytes.data(), before_bytes.size());

    std::string out;
    int shown = 0;
    for (const auto& f : ct.type->fields) {
        const void* a = scratch.data() + f.offset;
        const void* b = static_cast<const char*>(after_obj) + f.offset;
        if (std::memcmp(a, b, f.size) == 0) continue;

        if (shown++) out += ", ";
        if (shown > 3) { out += "..."; break; }

        const std::string va = field_value(f, scratch.data());
        const std::string vb = field_value(f, after_obj);
        out += f.name;
        if (!va.empty() && !vb.empty()) out += " " + va + " -> " + vb;
        else                            out += " changed";
    }
    return out.empty() ? "changed" : out;
}

bool vec_differs(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return std::fabs(a.x - b.x) > eps || std::fabs(a.y - b.y) > eps ||
           std::fabs(a.z - b.z) > eps;
}

bool quat_differs(const glm::quat& a, const glm::quat& b, float eps = 1e-4f) {
    // q and -q are the same rotation; compare on the absolute dot product so a
    // sign flip during play is not reported as a change the developer made.
    const float d = std::fabs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    return d < 1.0f - eps;
}

}  // namespace

void PlayModeChanges::Capture(const std::shared_ptr<schizo::scene::Scene>& scene,
                              EcsSceneBridge* bridge) {
    baseline_.clear();
    has_baseline_ = false;
    if (!scene) return;

    for (const auto& ent : scene->GetEntities()) {
        if (!ent) continue;
        auto* t = ent->GetTransform();
        if (!t) continue;

        Baseline b;
        b.entity_name = ent->GetName();
        b.position    = t->GetLocalPosition();
        b.rotation    = t->GetLocalRotation();
        b.scale       = t->GetLocalScale();

        if (bridge) {
            const uint32_t eid = bridge->ecs_entity_id(t);
            if (eid != kNoEcsEntity) {
                auto& world = bridge->world();
                const auto e = static_cast<schizo::ecs::Entity>(eid);
                for (const auto& ct : schizo::ecs::authorable_components()) {
                    if (!ct.has(world, e)) continue;
                    if (const void* comp = ct.get(world, e))
                        b.components[ct.name] = schizo::ecs::serialize_authorable(ct, comp);
                }
            }
        }
        baseline_[ent->GetId()] = std::move(b);
    }

    has_baseline_ = true;
    if (auto log = spdlog::get("editor"))
        log->debug("[play-changes] baselined {} entities", baseline_.size());
}

PlayChangeReport PlayModeChanges::Diff(
        const std::shared_ptr<schizo::scene::Scene>& scene,
        EcsSceneBridge* bridge) const {
    PlayChangeReport report;
    if (!scene || !has_baseline_) return report;

    std::unordered_map<uint32_t, bool> seen;
    seen.reserve(baseline_.size());

    for (const auto& ent : scene->GetEntities()) {
        if (!ent) continue;
        auto* t = ent->GetTransform();
        if (!t) continue;

        const uint32_t id = ent->GetId();
        auto it = baseline_.find(id);
        if (it == baseline_.end()) { ++report.entities_spawned; continue; }
        seen[id] = true;
        const Baseline& base = it->second;

        // --- transform ---
        const glm::vec3 pos = t->GetLocalPosition();
        const glm::quat rot = t->GetLocalRotation();
        const glm::vec3 scl = t->GetLocalScale();
        if (vec_differs(base.position, pos) || quat_differs(base.rotation, rot) ||
            vec_differs(base.scale, scl)) {
            PlayChange c;
            c.entity_id    = id;
            c.entity_name  = ent->GetName();
            c.component    = "Transform";
            c.is_transform = true;
            c.position = pos; c.rotation = rot; c.scale = scl;

            std::string s;
            if (vec_differs(base.position, pos)) {
                s = "moved to (" + fmt_f(pos.x) + ", " + fmt_f(pos.y) + ", " +
                    fmt_f(pos.z) + ")";
            }
            if (quat_differs(base.rotation, rot)) s += (s.empty() ? "" : ", ") + std::string("rotated");
            if (vec_differs(base.scale, scl))     s += (s.empty() ? "" : ", ") + std::string("rescaled");
            c.summary = s;
            report.changes.push_back(std::move(c));
        }

        // --- authorable ECS components ---
        if (!bridge) continue;
        const uint32_t eid = bridge->ecs_entity_id(t);
        if (eid == kNoEcsEntity) continue;
        auto& world = bridge->world();
        const auto e = static_cast<schizo::ecs::Entity>(eid);

        for (const auto& ct : schizo::ecs::authorable_components()) {
            const bool had = base.components.count(ct.name) != 0;
            const bool has = ct.has(world, e);
            if (!had && !has) continue;

            if (had && !has) {
                PlayChange c;
                c.entity_id   = id;
                c.entity_name = ent->GetName();
                c.component   = ct.name;
                c.summary     = "removed during play (keeping this removes it from the scene)";
                report.changes.push_back(std::move(c));
                continue;
            }

            const void* comp = ct.get(world, e);
            if (!comp) continue;
            std::vector<uint8_t> now = schizo::ecs::serialize_authorable(ct, comp);

            if (!had) {
                PlayChange c;
                c.entity_id   = id;
                c.entity_name = ent->GetName();
                c.component   = ct.name;
                c.summary     = "added during play";
                c.bytes       = std::move(now);
                report.changes.push_back(std::move(c));
                continue;
            }

            const auto& before = base.components.at(ct.name);
            if (before == now) continue;

            PlayChange c;
            c.entity_id   = id;
            c.entity_name = ent->GetName();
            c.component   = ct.name;
            c.summary     = summarize(ct, before, comp);
            c.bytes       = std::move(now);
            report.changes.push_back(std::move(c));
        }
    }

    for (const auto& [id, b] : baseline_)
        if (!seen.count(id)) ++report.entities_destroyed;

    return report;
}

size_t PlayModeChanges::Apply(const std::vector<PlayChange>& changes,
                              const std::shared_ptr<schizo::scene::Scene>& scene,
                              EcsSceneBridge* bridge) {
    if (!scene) return 0;

    // id -> entity, so applying N changes does not rescan the scene N times.
    std::unordered_map<uint32_t, schizo::scene::Entity*> by_id;
    for (const auto& ent : scene->GetEntities())
        if (ent) by_id[ent->GetId()] = ent.get();

    size_t applied = 0;
    for (const auto& c : changes) {
        if (!c.keep) continue;
        auto it = by_id.find(c.entity_id);
        if (it == by_id.end()) continue;          // destroyed since the diff
        auto* t = it->second->GetTransform();
        if (!t) continue;

        if (c.is_transform) {
            t->SetLocalPosition(c.position);
            t->SetLocalRotation(c.rotation);
            t->SetLocalScale(c.scale);
            ++applied;
            continue;
        }

        if (!bridge) continue;
        const auto* ct = schizo::ecs::find_authorable(c.component.c_str());
        if (!ct) continue;
        const uint32_t eid = bridge->ecs_entity_id(t);
        if (eid == kNoEcsEntity) continue;
        auto& world = bridge->world();
        const auto e = static_cast<schizo::ecs::Entity>(eid);

        if (c.bytes.empty()) {                    // the "removed during play" row
            if (ct->has(world, e)) { ct->remove(world, e); ++applied; }
            continue;
        }
        if (!ct->has(world, e)) ct->add(world, e);
        if (void* comp = ct->get(world, e)) {
            schizo::ecs::deserialize_authorable(*ct, comp, c.bytes.data(), c.bytes.size());
            ++applied;
        }
    }

    if (auto log = spdlog::get("editor"))
        log->info("[play-changes] kept {} of {} change(s) from play mode",
                  applied, changes.size());
    return applied;
}

}  // namespace schizo::editor
