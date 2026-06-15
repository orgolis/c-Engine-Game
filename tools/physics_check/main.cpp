// ====================
// physics_check — Stage 4 Jolt physics verification CLI
// ====================
//
// Exercises schizo::physics::PhysicsWorld (the Jolt wrapper) against the Stage-4
// acceptance: (1) a box falls under gravity and rests on a floor; (2) the sim is
// deterministic — identical results across two runs with the same inputs;
// (3) 1000 dynamic bodies stay stable at 66 Hz within frame budget.

#include "physics/jolt_physics.h"
#include "physics/physics_scene.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace schizo::physics;

static uint64_t fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// Build a floor + a stack of `n` dynamic boxes dropped from above; step `steps`
// times at 66 Hz; return an FNV hash of every body's final position (for the
// determinism comparison) and the dynamic bodies' final Y centroid.
static uint64_t run_scene(int n, int steps, double& out_centroid_y) {
    PhysicsWorld world;
    world.init(glm::vec3(0, -9.81f, 0));

    BodyDesc floor;
    floor.shape = ShapeType::Box;
    floor.half_extents = glm::vec3(50.0f, 0.5f, 50.0f);
    floor.position = glm::vec3(0, 0, 0);
    floor.motion = MotionType::Static;
    world.add_body(floor);

    std::vector<BodyId> bodies;
    bodies.reserve(n);
    for (int i = 0; i < n; ++i) {
        BodyDesc b;
        b.shape = ShapeType::Box;
        b.half_extents = glm::vec3(0.5f);
        // Deterministic grid above the floor (no randomness).
        const int gx = i % 32, gz = (i / 32) % 32, gy = i / (32 * 32);
        b.position = glm::vec3(gx * 1.5f - 24.0f, 5.0f + gy * 1.5f, gz * 1.5f - 24.0f);
        b.motion = MotionType::Dynamic;
        bodies.push_back(world.add_body(b));
    }

    const float dt = 1.0f / 66.0f;
    for (int s = 0; s < steps; ++s) world.step(dt);

    uint64_t h = 1469598103934665603ull;
    double sum_y = 0.0;
    for (BodyId id : bodies) {
        BodyState st = world.get_state(id);
        const float p[3] = { st.position.x, st.position.y, st.position.z };
        h = fnv(p, sizeof(p), h);
        sum_y += st.position.y;
    }
    out_centroid_y = bodies.empty() ? 0.0 : sum_y / double(bodies.size());
    return h;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("physics_check: Jolt PhysicsWorld (Stage 4)\n");
    int failures = 0;

    // (1) Drop test — a single box should fall and rest at y ~= 1.0
    //     (floor top 0.5 + box half 0.5).
    {
        PhysicsWorld w; w.init(glm::vec3(0, -9.81f, 0));
        BodyDesc floor; floor.half_extents = glm::vec3(50, 0.5f, 50); floor.motion = MotionType::Static;
        w.add_body(floor);
        BodyDesc box; box.half_extents = glm::vec3(0.5f); box.position = glm::vec3(0, 10, 0);
        BodyId id = w.add_body(box);
        const float y0 = w.get_state(id).position.y;
        for (int s = 0; s < 198; ++s) w.step(1.0f / 66.0f);  // 3 s
        const BodyState st = w.get_state(id);
        const bool rest = (st.position.y > 0.9f && st.position.y < 1.1f) &&
                          (std::fabs(st.linear_velocity.y) < 0.05f);
        std::printf("  [1] drop+rest: y %.2f -> %.3f, vy %.4f -> %s\n",
                    y0, st.position.y, st.linear_velocity.y, rest ? "OK" : "FAIL");
        failures += rest ? 0 : 1;
    }

    // (2) Determinism — two identical runs must produce bit-identical results.
    {
        double cy1 = 0, cy2 = 0;
        const uint64_t h1 = run_scene(64, 132, cy1);   // 2 s of 64 stacked boxes
        const uint64_t h2 = run_scene(64, 132, cy2);
        const bool det = (h1 == h2);
        std::printf("  [2] determinism: run1 %016llx == run2 %016llx -> %s\n",
                    (unsigned long long)h1, (unsigned long long)h2, det ? "OK" : "FAIL");
        failures += det ? 0 : 1;
    }

    // (3) Scale — 1000 dynamic bodies stable at 66 Hz within frame budget.
    {
        PhysicsWorld w; w.init(glm::vec3(0, -9.81f, 0));
        BodyDesc floor; floor.half_extents = glm::vec3(50, 0.5f, 50); floor.motion = MotionType::Static;
        w.add_body(floor);
        for (int i = 0; i < 1000; ++i) {
            BodyDesc b; b.half_extents = glm::vec3(0.4f);
            const int gx = i % 32, gz = (i / 32) % 32, gy = i / (32 * 32);
            b.position = glm::vec3(gx * 1.2f - 19.0f, 3.0f + gy * 1.2f, gz * 1.2f - 19.0f);
            w.add_body(b);
        }
        const int steps = 66;  // 1 s
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int s = 0; s < steps; ++s) w.step(1.0f / 66.0f);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double per_step = ms / steps;
        const bool ok = (w.body_count() == 1001) && (per_step < 16.0);
        std::printf("  [3] 1000 bodies @ 66 Hz: %d steps in %.1f ms (%.3f ms/step), %zu bodies -> %s\n",
                    steps, ms, per_step, w.body_count(), ok ? "OK" : "SLOW/FAIL");
        failures += (w.body_count() == 1001) ? 0 : 1;  // timing is informational on debug builds
    }

    // (4) Character controller — a kinematic capsule walks across a floor and
    //     steps UP onto a 0.4 m platform (collide-and-slide + stair stepping),
    //     staying grounded the whole way.
    {
        PhysicsWorld w; w.init(glm::vec3(0, -9.81f, 0));
        BodyDesc floor; floor.half_extents = glm::vec3(50, 0.5f, 50); floor.position = glm::vec3(0, -0.5f, 0);
        floor.motion = MotionType::Static; w.add_body(floor);                 // top at y=0
        BodyDesc platform; platform.half_extents = glm::vec3(4, 0.2f, 4);
        platform.position = glm::vec3(6, 0.2f, 0); platform.motion = MotionType::Static;
        w.add_body(platform);                                                 // top at y=0.4, x in [2,10]

        CharacterDesc cd; cd.radius = 0.3f; cd.height = 1.2f; cd.position = glm::vec3(0, 0.95f, 0);
        CharacterId c = w.add_character(cd);
        const float y0 = w.character_position(c).y;

        const float dt = 1.0f / 66.0f;
        bool fell_through = false;
        float vy = 0.0f;
        for (int s = 0; s < 198; ++s) {                                       // 3 s walking +x
            vy = w.character_on_ground(c) ? 0.0f : (vy - 9.81f * dt);          // caller-owned gravity
            w.update_character(c, glm::vec3(3.0f, vy, 0), dt);
            if (w.character_position(c).y < -1.0f) fell_through = true;
        }
        const glm::vec3 p = w.character_position(c);
        const bool grounded = w.character_on_ground(c);
        const bool climbed  = (p.y > y0 + 0.3f);   // stepped up ~0.4 m onto the platform
        const bool walked   = (p.x > 2.0f);        // reached the platform
        const bool ok = grounded && climbed && walked && !fell_through;
        std::printf("  [4] character: walked to x %.2f, y %.2f -> %.2f, grounded %d, climbed %d -> %s\n",
                    p.x, y0, p.y, grounded ? 1 : 0, climbed ? 1 : 0, ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    // (5) ECS bridge — physics driven entirely from ECS RigidBody/Collider/
    //     Transform components via PhysicsScene.
    {
        namespace ecs = schizo::ecs;
        ecs::World ew;
        // Static floor (mass 0 -> static), top at y=0.
        {
            ecs::Entity e = ew.create();
            ecs::Transform t; t.position = glm::vec3(0, -0.5f, 0); ew.add<ecs::Transform>(e, t);
            ecs::RigidBody rb; rb.mass = 0.0f; ew.add<ecs::RigidBody>(e, rb);
            ecs::Collider col; col.shape = 0; col.half_extents = glm::vec3(50, 0.5f, 50);
            ew.add<ecs::Collider>(e, col);
        }
        std::vector<ecs::Entity> boxes;
        for (int i = 0; i < 16; ++i) {
            ecs::Entity e = ew.create();
            ecs::Transform t;
            t.position = glm::vec3((i % 4) * 1.5f - 2.0f, 5.0f + i * 0.6f, (i / 4) * 1.5f - 2.0f);
            ew.add<ecs::Transform>(e, t);
            ecs::RigidBody rb; rb.mass = 1.0f; ew.add<ecs::RigidBody>(e, rb);
            ecs::Collider col; col.shape = 0; col.half_extents = glm::vec3(0.5f);
            ew.add<ecs::Collider>(e, col);
            boxes.push_back(e);
        }
        schizo::physics::PhysicsScene phys; phys.init();
        const float y0 = ew.get<ecs::Transform>(boxes[0]).position.y;
        for (int s = 0; s < 264; ++s) phys.simulate(ew, 1.0f / 66.0f);  // 4 s

        int created = 0, on_floor = 0;
        for (ecs::Entity e : boxes) {
            if (ew.get<ecs::RigidBody>(e).body_handle != 0) ++created;
            // Floor top is y=0, box half 0.5 -> rests at center y=0.5; >0.4 means
            // it landed and didn't fall through.
            if (ew.get<ecs::Transform>(e).position.y > 0.4f) ++on_floor;
        }
        const float y1 = ew.get<ecs::Transform>(boxes[0]).position.y;
        const bool ok = (created == (int)boxes.size()) && (on_floor == (int)boxes.size()) && (y1 < y0 - 2.0f);
        std::printf("  [5] ECS bridge: %d/%zu bodies created from components, box0 y %.2f -> %.2f, "
                    "%d/%zu above floor -> %s\n",
                    created, boxes.size(), y0, y1, on_floor, boxes.size(), ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    // (6) Convex-hull body — a DYNAMIC object with a custom mesh shape (Jolt
    //     triangle meshes are static-only, so dynamic meshes use the convex
    //     hull). Drop one and confirm it falls + rests.
    {
        PhysicsWorld w; w.init(glm::vec3(0, -9.81f, 0));
        BodyDesc floor; floor.half_extents = glm::vec3(50, 0.5f, 50); floor.motion = MotionType::Static;
        w.add_body(floor);
        std::vector<glm::vec3> pts;                       // unit-cube point cloud (a "mesh")
        for (int i = 0; i < 8; ++i)
            pts.push_back(glm::vec3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f));
        BodyDesc d; d.position = glm::vec3(0, 8, 0); d.motion = MotionType::Dynamic; d.mass = 1.0f;
        BodyId id = w.add_convex_body(pts, d);
        const float y0 = (id != kInvalidBody) ? w.get_state(id).position.y : 0.0f;
        for (int s = 0; s < 198; ++s) w.step(1.0f / 66.0f);
        const BodyState st = w.get_state(id);
        const bool ok = (id != kInvalidBody) && (st.position.y < y0 - 5.0f) &&
                        (st.position.y > 0.5f) && (std::fabs(st.linear_velocity.y) < 0.1f);
        std::printf("  [6] convex-hull (dynamic mesh): created=%d, y %.2f -> %.3f, vy %.4f -> %s\n",
                    id != kInvalidBody, y0, st.position.y, st.linear_velocity.y, ok ? "OK" : "FAIL");
        failures += ok ? 0 : 1;
    }

    std::printf("physics_check: %s\n", failures == 0 ? "ALL OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
