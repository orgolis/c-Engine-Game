// ragdoll_check: a character going limp (engine/renderer/include/ragdoll_rig.h
// on engine/core/physics' Jolt ragdolls), headless: a biped standing on a
// floor dies with a push. Its parts are found by name, it falls and comes to
// rest on the floor (not through it), every joint stays joined, knees and
// elbows bend only their way, and the bones follow the bodies.
#include "physics/jolt_physics.h"
#include "ragdoll_rig.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace schizo::renderer;
namespace phys = schizo::physics;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

namespace {
BoneTransform at(float x, float y, float z) {
    BoneTransform t;
    t.position = glm::vec3(x, y, z);
    return t;
}
struct Biped {
    Skeleton sk;
    std::vector<BoneTransform> bind;
    Biped() {
        sk.AddBone("root", "", at(0, 0, 0));
        sk.AddBone("pelvis", "root", at(0, 0.95f, 0));
        sk.AddBone("spine", "pelvis", at(0, 0.15f, 0));
        sk.AddBone("chest", "spine", at(0, 0.2f, 0));
        sk.AddBone("neck", "chest", at(0, 0.25f, 0));
        sk.AddBone("head", "neck", at(0, 0.1f, 0));
        for (int side = 0; side < 2; ++side) {
            const float s = side == 0 ? 1.0f : -1.0f;   // the left is +X (the model faces +Z)
            const std::string k = side == 0 ? ".L" : ".R";
            sk.AddBone("clavicle" + k, "chest", at(0.05f * s, 0.2f, 0));
            sk.AddBone("upper_arm" + k, "clavicle" + k, at(0.12f * s, 0, 0));
            sk.AddBone("forearm" + k, "upper_arm" + k, at(0.28f * s, 0, 0));
            sk.AddBone("hand" + k, "forearm" + k, at(0.25f * s, 0, 0));
            sk.AddBone("thigh" + k, "pelvis", at(0.1f * s, 0, 0));
            sk.AddBone("shin" + k, "thigh" + k, at(0, -0.43f, 0.02f));
            sk.AddBone("foot" + k, "shin" + k, at(0, -0.42f, -0.02f));
            sk.AddBone("toe" + k, "foot" + k, at(0, -0.05f, 0.12f));
        }
        for (const auto& b : sk.GetAllBones()) bind.push_back(b.local_transform);
        sk.UpdateWorldTransforms();
    }
};
phys::RagdollPart to_physics(const RagdollPartStart& s) {
    phys::RagdollPart p;
    p.parent = s.parent;
    p.joint = s.joint;
    p.rotation = s.rotation;
    p.tip = s.tip;
    p.radius = s.radius;
    p.mass = s.mass;
    p.hinge = s.hinge;
    p.hinge_axis = s.hinge_axis;
    p.hinge_min = s.hinge_min;
    p.hinge_max = s.hinge_max;
    p.cone = s.cone;
    p.twist = s.twist;
    return p;
}
}  // namespace

int main() {
    Biped b;
    const RagdollRig rig = find_ragdoll_rig(b.sk, b.bind);
    char what[200];
    std::snprintf(what, sizeof what, "find_ragdoll_rig: 12 bodies (pelvis, spine, chest, head, arms, forearms, thighs, shins): %zu", rig.parts.size());
    check(rig.valid() && rig.parts.size() == 12, what);
    if (!rig.valid()) { std::printf("FAILED\n"); return 1; }
    auto bone_of = [&](const char* name) { return b.sk.GetBoneId(name); };
    check(rig.parts[0].bone == bone_of("pelvis") && rig.parts[0].parent < 0, "the pelvis is the root body");
    int knee = -1, elbow = -1, thigh = -1, upper = -1;
    for (size_t i = 0; i < rig.parts.size(); ++i) {
        if (rig.parts[i].bone == bone_of("shin.L")) knee = static_cast<int>(i);
        if (rig.parts[i].bone == bone_of("forearm.L")) elbow = static_cast<int>(i);
        if (rig.parts[i].bone == bone_of("thigh.L")) thigh = static_cast<int>(i);
        if (rig.parts[i].bone == bone_of("upper_arm.L")) upper = static_cast<int>(i);
    }
    check(knee >= 0 && rig.parts[knee].hinge == 1 && rig.parts[knee].parent == thigh && elbow >= 0 && rig.parts[elbow].hinge == 2 &&
              rig.parts[elbow].parent == upper,
          "knees and elbows are hinges on their thighs and upper arms");

    phys::PhysicsWorld world;
    world.init();
    phys::BodyDesc floor;
    floor.half_extents = glm::vec3(50, 0.5f, 50);
    floor.position = glm::vec3(0, -0.5f, 0);
    floor.motion = phys::MotionType::Static;
    world.add_body(floor);
    // A character's own capsule (a moving body) stands where the ragdoll starts: the
    // ragdoll must not collide with it.
    phys::BodyDesc capsule;
    capsule.shape = phys::ShapeType::Capsule;
    capsule.radius = 0.35f;
    capsule.height = 1.1f;
    capsule.position = glm::vec3(0, 0.9f, 0);
    capsule.motion = phys::MotionType::Kinematic;
    world.add_body(capsule);

    const glm::mat4 model(1.0f);
    const std::vector<RagdollPartStart> start = ragdoll_start(b.sk, rig, model);
    std::vector<phys::RagdollPart> parts;
    for (const auto& s : start) parts.push_back(to_physics(s));
    const phys::RagdollId id = world.add_ragdoll(parts, glm::vec3(0.0f, 0.0f, -0.5f));
    check(id != 0 && world.ragdoll_count() == 1, "the ragdoll is made");
    // A shot in the chest from the front: it goes over backwards.
    world.ragdoll_impulse(id, 2, glm::vec3(0.0f, 0.0f, -30.0f));
    std::vector<glm::vec3> pos;
    std::vector<glm::quat> rot;
    float lowest = 1e9f;
    bool finite = true, asleep = false;
    int frames = 0;
    for (; frames < 600; ++frames) {
        world.step(1.0f / 60.0f);
        world.ragdoll_pose(id, pos, rot);
        for (size_t i = 0; i < pos.size(); ++i) {
            finite = finite && std::isfinite(pos[i].x) && std::isfinite(pos[i].y) && std::isfinite(pos[i].z) && std::isfinite(rot[i].w);
            lowest = std::min(lowest, pos[i].y - parts[i].radius);
        }
        if (frames > 60 && world.ragdoll_asleep(id)) { asleep = true; break; }
    }
    check(finite, "every body stays finite");
    std::snprintf(what, sizeof what, "it falls onto the floor, not through it (lowest point %.3f m)", lowest);
    check(lowest > -0.06f, what);
    std::snprintf(what, sizeof what, "it lies on the floor (the chest %.2f m up) and went over backwards (%.2f m)", pos[2].y, pos[2].z);
    check(pos[2].y < 0.4f && pos[2].z < -0.3f, what);
    std::snprintf(what, sizeof what, "it comes to rest (asleep after %.1f s)", frames / 60.0f);
    check(asleep, what);
    // Every joint joined: a part's pivot where its parent's body carries it.
    float worst = 0.0f;
    for (size_t i = 1; i < parts.size(); ++i) {
        const int p = parts[i].parent;
        const glm::vec3 offset = glm::inverse(parts[static_cast<size_t>(p)].rotation) * (parts[i].joint - parts[static_cast<size_t>(p)].joint);
        worst = std::max(worst, glm::distance(pos[static_cast<size_t>(p)] + rot[static_cast<size_t>(p)] * offset, pos[i]));
    }
    std::snprintf(what, sizeof what, "every joint stays joined (at most %.3f m apart)", worst);
    check(worst < 0.03f, what);
    // Knees and elbows bend only their way: the bend about their hinge never goes past straight backwards.
    auto bend_sign = [&](int part) {
        const int p = parts[static_cast<size_t>(part)].parent;
        const glm::vec3 axis = rot[static_cast<size_t>(p)] * glm::inverse(parts[static_cast<size_t>(p)].rotation) * parts[static_cast<size_t>(part)].hinge_axis;
        const glm::vec3 a = glm::normalize(pos[static_cast<size_t>(part)] - pos[static_cast<size_t>(p)]);
        const glm::vec3 tip = pos[static_cast<size_t>(part)] + rot[static_cast<size_t>(part)] * glm::inverse(parts[static_cast<size_t>(part)].rotation) *
                                                                    (parts[static_cast<size_t>(part)].tip - parts[static_cast<size_t>(part)].joint);
        const glm::vec3 c = glm::normalize(tip - pos[static_cast<size_t>(part)]);
        return glm::dot(glm::cross(a, c), axis);
    };
    std::snprintf(what, sizeof what, "the knee bends only back (%.3f) and the elbow only forward (%.3f): neither past straight", bend_sign(knee), bend_sign(elbow));
    check(bend_sign(knee) > -0.06f && bend_sign(elbow) > -0.06f, what);
    // The bones follow the bodies.
    apply_ragdoll_pose(b.sk, rig, model, pos, rot);
    const glm::vec3 pelvis_bone(b.sk.GetWorldMatrix(static_cast<uint16_t>(bone_of("pelvis")))[3]);
    const glm::vec3 hand(b.sk.GetWorldMatrix(static_cast<uint16_t>(bone_of("hand.L")))[3]);
    const glm::vec3 fore(b.sk.GetWorldMatrix(static_cast<uint16_t>(bone_of("forearm.L")))[3]);
    std::snprintf(what, sizeof what, "the pelvis bone is where its body is (%.4f m off); the hand stays on its forearm (%.3f m, as built)",
                  glm::distance(pelvis_bone, pos[0]), glm::distance(hand, fore));
    check(glm::distance(pelvis_bone, pos[0]) < 1e-3f && std::abs(glm::distance(hand, fore) - 0.25f) < 1e-3f, what);
    world.remove_ragdoll(id);
    check(world.ragdoll_count() == 0, "removed");
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
