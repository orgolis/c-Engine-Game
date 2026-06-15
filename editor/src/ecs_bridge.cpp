#include "ecs_bridge.h"

#include "ecs/world.h"
#include "ecs/components.h"
#include "ecs/parallel.h"
#include "ecs/snapshot.h"
#include "ecs/render_collect.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "mesh_renderer_component.h"
#include "mesh_component.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

// Shadow-only component: which shadow entity is this one's parent (or
// null_entity for a root). Kept editor-local so it doesn't touch core
// components.h until the migration goes authoritative.
struct ShadowParent {
    ecs::Entity value = ecs::null_entity;
};

// Pimpl: owns the shadow world. Constructed once, cleared+refilled per frame.
struct EcsSceneBridge::Impl {
    ecs::World world;
    // Parallel arrays in creation order. Keeping the OOP Transform* and a
    // resolved parent index alongside each shadow entity lets us build the
    // hierarchy and verify against the OOP world matrix without relying on
    // EnTT's internal storage iteration order.
    // Persistent: OOP Transform* -> its stable shadow entity, kept ACROSS
    // frames. Reconciled each sync (create new / reuse existing / destroy gone)
    // so an entity keeps its identity and components frame to frame.
    std::unordered_map<scene::Transform*, ecs::Entity> tf_to_entity;

    // Per-frame working arrays (active entities this frame, in iteration order).
    std::vector<ecs::Entity>       shadow;
    std::vector<scene::Transform*> oop_tf;
    std::vector<scene::Entity*>    oop_ent;     // parallel to oop_tf (for components)
    std::vector<int>               parent_idx;  // -1 == root
    // Authoritative read path: OOP Transform* -> ECS-computed world matrix,
    // refreshed every sync. Rendering queries this instead of GetWorldMatrix().
    std::unordered_map<scene::Transform*, glm::mat4> result;
};

// TRS compose (defined below; forward-declared for draw_benchmark above the def).
static glm::mat4 compose_trs(const ecs::Transform& t);

const glm::mat4* EcsSceneBridge::world_matrix(
    const schizo::scene::Transform* tf) const {
    auto it = impl_->result.find(const_cast<scene::Transform*>(tf));
    return it == impl_->result.end() ? nullptr : &it->second;
}

bool EcsSceneBridge::snapshot_selfcheck(
    size_t& out_bytes, size_t& out_reloaded_entities) const {
    ecs::World& world = impl_->world;

    std::vector<uint8_t> buf;
    gws::serialize::BinaryWriter w(buf);
    ecs::save_world<ecs::Transform, ecs::LocalToWorld,
                    ecs::Velocity, ecs::Name>(w, world);
    out_bytes = buf.size();

    ecs::World tmp;
    gws::serialize::BinaryReader r(buf.data(), buf.size());
    const bool ok = ecs::load_world<ecs::Transform, ecs::LocalToWorld,
                                    ecs::Velocity, ecs::Name>(r, tmp);
    out_reloaded_entities = tmp.count<ecs::Transform>();

    return ok
        && tmp.count<ecs::Transform>()    == world.count<ecs::Transform>()
        && tmp.count<ecs::LocalToWorld>() == world.count<ecs::LocalToWorld>();
}

size_t EcsSceneBridge::collect_scene_draws(double& out_ms) const {
    std::vector<ecs::DrawRecord> draws;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t n = ecs::collect_draws(impl_->world, draws);
    out_ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0).count();
    return n;
}

size_t EcsSceneBridge::draw_benchmark(size_t n, double& out_ms) {
    ecs::World w;
    for (size_t i = 0; i < n; ++i) {
        ecs::Entity e = w.create();
        ecs::Transform t;
        t.position = {static_cast<float>(i % 1000),
                      static_cast<float>((i / 1000) % 1000), 0.0f};
        w.add<ecs::Transform>(e, t);
        w.add<ecs::LocalToWorld>(e, ecs::LocalToWorld{compose_trs(t)});
        ecs::MeshRenderer mr;
        mr.mesh     = (i % 8) + 1;
        mr.material = static_cast<int32_t>(i % 4);
        w.add<ecs::MeshRenderer>(e, mr);
    }
    std::vector<ecs::DrawRecord> draws;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t got = ecs::collect_draws(w, draws);
    out_ms = std::chrono::duration<double, std::milli>(
                 std::chrono::steady_clock::now() - t0).count();
    return got;
}

EcsSceneBridge::EcsSceneBridge() : impl_(std::make_unique<Impl>()) {
    ecs::register_core_components();
}

EcsSceneBridge::~EcsSceneBridge() = default;

// TRS compose — how a flat LocalToWorld system turns a Transform into its
// LOCAL matrix (the world matrix for a root; combined with parents below).
static glm::mat4 compose_trs(const ecs::Transform& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

void EcsSceneBridge::sync_and_run(
    const std::shared_ptr<schizo::scene::Scene>& scene) {

    entity_count_ = matrices_written_ = checked_ = verified_ = 0;
    created_ = reused_ = destroyed_ = 0;

    auto& world        = impl_->world;
    auto& tf_to_entity = impl_->tf_to_entity;
    auto& shadow       = impl_->shadow;
    auto& oop_tf       = impl_->oop_tf;
    auto& oop_ent      = impl_->oop_ent;
    auto& parent_idx   = impl_->parent_idx;

    shadow.clear();
    oop_tf.clear();
    oop_ent.clear();
    parent_idx.clear();
    impl_->result.clear();
    if (!scene) {
        // Scene gone: tear down all persistent shadow entities.
        for (auto& kv : tf_to_entity) world.destroy(kv.second);
        destroyed_ = tf_to_entity.size();
        tf_to_entity.clear();
        return;
    }

    // --- Pass 1: gather active entities (active-in-hierarchy implies every
    // ancestor is also active, so the parent map below resolves for non-roots).
    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;
        scene::Transform* tf = ent->GetTransform();
        if (!tf) continue;
        oop_tf.push_back(tf);
        oop_ent.push_back(ent.get());
    }

    // --- Pass 2: reconcile persistent map. Destroy shadow entities whose OOP
    // transform is no longer active, create entities for new ones, reuse the
    // rest (keeping their identity + components across frames).
    std::unordered_map<scene::Transform*, int> tf_to_index;
    tf_to_index.reserve(oop_tf.size());
    for (size_t i = 0; i < oop_tf.size(); ++i)
        tf_to_index.emplace(oop_tf[i], static_cast<int>(i));

    std::vector<scene::Transform*> to_erase;
    for (auto& kv : tf_to_entity)
        if (tf_to_index.find(kv.first) == tf_to_index.end()) {
            world.destroy(kv.second);
            to_erase.push_back(kv.first);
        }
    for (scene::Transform* tf : to_erase) tf_to_entity.erase(tf);
    destroyed_ = to_erase.size();

    // Create-or-reuse, in active order, refreshing the Transform component.
    shadow.resize(oop_tf.size());
    for (size_t i = 0; i < oop_tf.size(); ++i) {
        scene::Transform* tf = oop_tf[i];
        ecs::Entity e;
        auto found = tf_to_entity.find(tf);
        if (found != tf_to_entity.end()) {
            e = found->second;
            ++reused_;
        } else {
            e = world.create();
            world.add<ecs::LocalToWorld>(e, ecs::LocalToWorld{});
            world.add<ShadowParent>(e, ShadowParent{});
            tf_to_entity.emplace(tf, e);
            ++created_;
        }
        // (Re)write the mirrored local transform — this is also where an
        // external owner's writes would land once the ECS is authoritative.
        ecs::Transform t;
        t.position = tf->GetLocalPosition();
        t.rotation = tf->GetLocalRotation();
        t.scale    = tf->GetLocalScale();
        world.add<ecs::Transform>(e, t);  // emplace_or_replace
        shadow[i] = e;

        // Mirror the OOP entity's drawable info into an ECS MeshRenderer so the
        // scene's drawables live in the ECS (Stage 1.4) and collect_draws()
        // runs on the real scene. Non-authoritative: rendering still uses the
        // OOP path; this proves the data-oriented draw hand-off on real data.
        scene::Entity* oent = oop_ent[i];
        ecs::MeshRenderer mr{};
        bool drawable = false;
        if (auto m = oent->GetComponent<scene::MeshRendererComponent>()) {
            mr.primitive = static_cast<uint32_t>(m->GetMeshType());
            drawable = true;
        }
        if (const auto* mc = oent->GetMeshComponent()) {
            if (!mc->mesh_path.empty()) {
                mr.mesh = static_cast<uint64_t>(std::hash<std::string>{}(mc->mesh_path));
                drawable = true;
            }
        }
        if (drawable) world.add<ecs::MeshRenderer>(e, mr);
        else if (world.has<ecs::MeshRenderer>(e)) world.remove<ecs::MeshRenderer>(e);
    }
    entity_count_ = shadow.size();

    // --- Pass 3: wire parents (resolve OOP parent -> shadow index / entity).
    parent_idx.assign(shadow.size(), -1);
    for (size_t i = 0; i < shadow.size(); ++i) {
        scene::Transform* p = oop_tf[i]->GetParent();
        if (!p) continue;
        auto it = tf_to_index.find(p);
        if (it != tf_to_index.end()) {
            parent_idx[i] = it->second;
            world.get<ShadowParent>(shadow[i]).value = shadow[it->second];
        }
    }

    // --- System phase A (PARALLEL): LocalToWorld.value = local matrix.
    ecs::parallel_each<ecs::Transform, ecs::LocalToWorld>(
        world, [](ecs::Entity, ecs::Transform& t, ecs::LocalToWorld& l) {
            l.value = compose_trs(t);
        });

    // --- System phase B (hierarchy resolve): world = parent_world * local.
    // Dependency-ordered, so serial with memoisation here; the real engine
    // parallelises this by depth-wave (noted for step 2). Reads the local
    // matrices written above, writes world matrices back into LocalToWorld.
    std::vector<glm::mat4> local(shadow.size());
    for (size_t i = 0; i < shadow.size(); ++i)
        local[i] = world.get<ecs::LocalToWorld>(shadow[i]).value;

    std::vector<glm::mat4> world_mat(shadow.size());
    std::vector<char>      done(shadow.size(), 0);
    // Iterative resolve up the parent chain (no recursion depth limit risk).
    for (size_t i = 0; i < shadow.size(); ++i) {
        if (done[i]) continue;
        // Walk up to the highest unresolved ancestor, recording the chain.
        std::vector<int> chain;
        int cur = static_cast<int>(i);
        while (cur != -1 && !done[cur]) { chain.push_back(cur); cur = parent_idx[cur]; }
        glm::mat4 acc = (cur == -1) ? glm::mat4(1.0f) : world_mat[cur];
        // Apply from the top of the chain down.
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            acc = acc * local[*it];
            world_mat[*it] = acc;
            done[*it] = 1;
        }
    }
    for (size_t i = 0; i < shadow.size(); ++i) {
        world.get<ecs::LocalToWorld>(shadow[i]).value = world_mat[i];
        impl_->result.emplace(oop_tf[i], world_mat[i]);
    }
    matrices_written_ = shadow.size();

    // --- Verify: EVERY entity's ECS world matrix must equal the OOP one. A
    // full match validates components + storage + parallel TRS + hierarchy.
    for (size_t i = 0; i < shadow.size(); ++i) {
        ++checked_;
        const glm::mat4 expected = oop_tf[i]->GetWorldMatrix();
        const float* a = &world_mat[i][0][0];
        const float* b = &expected[0][0];
        float max_diff = 0.0f;
        for (int k = 0; k < 16; ++k)
            max_diff = std::max(max_diff, std::fabs(a[k] - b[k]));
        if (max_diff < 1e-3f) ++verified_;
    }
}

}  // namespace schizo::editor
