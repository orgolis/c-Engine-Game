// footik_check: the procedural layers over a sampled pose, headless.
// Feet on the ground (engine/renderer/include/foot_placement.h) on a small
// biped: flat ground, a step up under one foot, a drop under the other, a slope,
// a placed and turned model, a foot the clip lifts, a landing, the weight going
// to nothing. Looking, aiming and leaning (pose_layers.h) on a spine-to-head
// chain, also one whose joints have turned axes (as a rig made in Blender has).
// These fail visibly (feet in the ground, knees snapping backwards, a head
// turned the wrong way) but only in motion -- so they are asserted here.
#include "foot_placement.h"
#include "pose_layers.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace schizo::renderer;

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
// root > pelvis (0.95 up) > thigh (0.1 to the side) > shin (0.43 down, knee a
// little forward) > foot (0.42 down): standing, the ankle is 0.10 m up.
struct Biped {
    Skeleton sk;
    std::vector<BoneTransform> bind;
    Biped() {
        sk.AddBone("root", "", at(0, 0, 0));
        sk.AddBone("pelvis", "root", at(0, 0.95f, 0));
        sk.AddBone("thigh.L", "pelvis", at(0.1f, 0, 0));
        sk.AddBone("shin.L", "thigh.L", at(0, -0.43f, 0.02f));
        sk.AddBone("foot.L", "shin.L", at(0, -0.42f, -0.02f));
        sk.AddBone("toe.L", "foot.L", at(0, -0.05f, 0.12f));
        sk.AddBone("thigh.R", "pelvis", at(-0.1f, 0, 0));
        sk.AddBone("shin.R", "thigh.R", at(0, -0.43f, 0.02f));
        sk.AddBone("foot.R", "shin.R", at(0, -0.42f, -0.02f));
        sk.AddBone("spine", "pelvis", at(0, 0.2f, 0));
        for (const auto& b : sk.GetAllBones()) bind.push_back(b.local_transform);
    }
    // The clip's pose this frame: the bind pose, the left foot lifted by `lift`.
    void sample(float lift = 0.0f) {
        for (size_t b = 0; b < bind.size(); ++b) sk.SetBoneLocalTransform(static_cast<uint16_t>(b), bind[b]);
        if (lift != 0.0f) {
            BoneTransform t = bind[2];   // thigh.L up a little: the foot comes up by `lift`
            t.position.y += lift;
            sk.SetBoneLocalTransform(2, t);
        }
        sk.UpdateWorldTransforms();
    }
    glm::vec3 world(const glm::mat4& model, int b) const { return glm::vec3(model * sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); }
};
// Ground as a height field in the world; rays straight down.
FootGroundFn field(std::function<float(float, float)> h, glm::vec3 normal = {0, 1, 0}) {
    return [h, normal](const glm::vec3& from, const glm::vec3& down, float reach, glm::vec3& hit, glm::vec3& n) {
        if (down.y > -0.99f) return false;
        const float y = h(from.x, from.z);
        if (y > from.y || y < from.y - reach) return false;
        hit = glm::vec3(from.x, y, from.z);
        n = glm::normalize(normal);
        return true;
    };
}
void settle(Biped& b, FootRig& rig, const glm::mat4& model, const FootGroundFn& g, float weight, int frames = 120, float lift = 0.0f) {
    for (int i = 0; i < frames; ++i) {
        b.sample(lift);
        place_feet(b.sk, rig, model, g, weight, 1.0f / 60.0f);
    }
}
float bone_len(const Biped& b, int child) {
    const int p = b.sk.GetBone(static_cast<uint16_t>(child))->parent_id;
    return glm::length(glm::vec3(b.sk.GetWorldMatrix(static_cast<uint16_t>(child))[3]) - glm::vec3(b.sk.GetWorldMatrix(static_cast<uint16_t>(p))[3]));
}
bool near(float a, float b, float eps = 0.01f) { return std::abs(a - b) < eps; }
}  // namespace

int main() {
    const int L = 4, R = 8, KL = 3, KR = 7, HL = 2;
    const glm::mat4 I(1.0f);
    {
        Biped b;
        FootRig rig = find_foot_rig(b.sk, b.bind);
        check(rig.valid() && rig.legs[0].ankle == L && rig.legs[1].ankle == R && rig.legs[0].knee == KL && rig.legs[0].hip == HL &&
                  rig.pelvis == 1,
              "find_foot_rig: foot.L / foot.R, their knees and hips, the pelvis (not the toe)");
        check(near(rig.legs[0].rest_height, 0.10f, 1e-4f), "standing ankle height from the bind pose: 0.10 m");

        settle(b, rig, I, field([](float, float) { return 0.0f; }), 1.0f);
        check(near(b.world(I, L).y, 0.10f) && near(b.world(I, R).y, 0.10f) && near(rig.pelvis_off, 0.0f),
              "flat ground: the feet where the clip has them, the pelvis where it was");

        // A step 0.15 m up under the left foot (x > 0).
        FootRig r2 = find_foot_rig(b.sk, b.bind);
        const float len_thigh = 0.43f, len_shin = 0.42f;
        settle(b, r2, I, field([](float x, float) { return x > 0.0f ? 0.15f : 0.0f; }), 1.0f);
        char what[160];
        std::snprintf(what, sizeof what, "a step up under the left foot: it stands on it (%.3f m), the right stays (%.3f), the pelvis does not rise (%.3f)",
                      b.world(I, L).y, b.world(I, R).y, r2.pelvis_off);
        check(near(b.world(I, L).y, 0.25f) && near(b.world(I, R).y, 0.10f) && r2.pelvis_off <= 1e-4f, what);
        check(near(bone_len(b, KL), std::sqrt(len_thigh * len_thigh + 0.02f * 0.02f), 1e-3f) &&
                  near(bone_len(b, L), std::sqrt(len_shin * len_shin + 0.02f * 0.02f), 1e-3f),
              "...the bones keep their lengths");
        const glm::vec3 hip = b.world(I, HL), knee = b.world(I, KL), ankle = b.world(I, L);
        check(knee.z > (hip.z + ankle.z) * 0.5f + 0.02f, "...and the left knee bends forward, as a knee does");

        // A drop of 0.2 m under the right foot (x < 0).
        FootRig r3 = find_foot_rig(b.sk, b.bind);
        settle(b, r3, I, field([](float x, float) { return x < 0.0f ? -0.2f : 0.0f; }), 1.0f);
        std::snprintf(what, sizeof what, "a drop under the right foot: the pelvis goes down for it (%.3f), the right foot reaches (%.3f), the left stays (%.3f)",
                      r3.pelvis_off, b.world(I, R).y, b.world(I, L).y);
        check(near(r3.pelvis_off, -0.2f) && near(b.world(I, R).y, -0.10f) && near(b.world(I, L).y, 0.10f), what);

        // No snapping: the frame a step appears, the foot moves only part of the way.
        FootRig r4 = find_foot_rig(b.sk, b.bind);
        settle(b, r4, I, field([](float, float) { return 0.0f; }), 1.0f, 30);
        b.sample();
        place_feet(b.sk, r4, I, field([](float x, float) { return x > 0.0f ? 0.2f : 0.0f; }), 1.0f, 1.0f / 60.0f);
        const float first = b.world(I, L).y - 0.10f;
        std::snprintf(what, sizeof what, "a kerb appears: the first frame the foot moves %.3f of 0.2 m (a spring, no snap)", first);
        check(first > 0.0f && first < 0.06f, what);

        // The weight to nothing: back to the clip.
        settle(b, r4, I, field([](float x, float) { return x > 0.0f ? 0.2f : 0.0f; }), 1.0f);
        settle(b, r4, I, field([](float x, float) { return x > 0.0f ? 0.2f : 0.0f; }), 0.0f, 120);
        check(near(b.world(I, L).y, 0.10f, 0.005f) && near(r4.pelvis_off, 0.0f, 0.005f), "weight 0 (in the air, dead): the pose is the clip's again");

        // A foot the clip lifts mid-step goes over the step by as much, and is not laid on the slope.
        FootRig r5 = find_foot_rig(b.sk, b.bind);
        settle(b, r5, I, field([](float x, float) { return x > 0.0f ? 0.15f : 0.0f; }), 1.0f, 120, 0.2f);
        std::snprintf(what, sizeof what, "a lifted foot (0.2 m in the clip) over a 0.15 m step: %.3f m up, not planted (%.2f)",
                      b.world(I, L).y, r5.legs[0].plant);
        check(near(b.world(I, L).y, 0.45f, 0.02f) && r5.legs[0].plant < 0.01f, what);
    }
    {
        // On a slope rising 0.3 m a metre toward +Z: a planted foot lies on it.
        Biped b;
        FootRig rig = find_foot_rig(b.sk, b.bind);
        const glm::vec3 n = glm::normalize(glm::vec3(0.0f, 1.0f, -0.3f));
        settle(b, rig, I, field([](float, float z) { return 0.3f * z; }, n), 1.0f);
        const glm::vec3 foot_up = glm::normalize(glm::mat3(b.sk.GetWorldMatrix(L)) * glm::vec3(0, 1, 0));
        const float tilt = glm::degrees(std::acos(std::clamp(glm::dot(foot_up, glm::vec3(0, 1, 0)), -1.0f, 1.0f)));
        char what[140];
        std::snprintf(what, sizeof what, "a slope of 0.3: the planted foot laid on it (tilted %.1f of 16.7 degrees, toward the slope)", tilt);
        check(near(tilt, 16.7f, 1.0f) && foot_up.z < 0.0f, what);
    }
    {
        // A model placed and turned in the world (90 degrees, at 10, 5, 3): the same.
        Biped b;
        FootRig rig = find_foot_rig(b.sk, b.bind);
        const glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(10, 5, 3)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
        // Its left (model +X) is world -Z here: the step under it.
        settle(b, rig, M, field([](float, float z) { return z < 3.0f ? 5.15f : 5.0f; }), 1.0f);
        char what[140];
        std::snprintf(what, sizeof what, "a placed, turned model: the step under its left foot found (%.3f m above its ground), the right on its ground (%.3f)",
                      b.world(M, L).y - 5.0f, b.world(M, R).y - 5.0f);
        check(near(b.world(M, L).y - 5.0f, 0.25f) && near(b.world(M, R).y - 5.0f, 0.10f), what);
    }
    {
        // A landing (3 m/s, a jump's): the legs give and come back (critically damped: no bounce above).
        Biped b;
        FootRig rig = find_foot_rig(b.sk, b.bind);
        settle(b, rig, I, field([](float, float) { return 0.0f; }), 1.0f, 30);
        rig.land_v -= 3.0f;
        float lowest = 0.0f, highest = -1.0f;
        for (int i = 0; i < 120; ++i) {
            b.sample();
            place_feet(b.sk, rig, I, field([](float, float) { return 0.0f; }), 1.0f, 1.0f / 60.0f);
            lowest = std::min(lowest, rig.pelvis_off + rig.land_off);
            highest = std::max(highest, rig.pelvis_off + rig.land_off);
        }
        char what[140];
        std::snprintf(what, sizeof what, "a landing (3 m/s): the legs give %.3f m and come back without rising above (%.4f)", lowest, highest);
        check(lowest < -0.07f && lowest > -0.2f && highest <= 1e-4f && near(rig.pelvis_off + rig.land_off, 0.0f, 0.005f), what);
        // ...and the feet stay where they stand while it does.
        rig.land_v -= 3.0f;
        float worst = 0.0f;
        for (int i = 0; i < 30; ++i) {
            b.sample();
            place_feet(b.sk, rig, I, field([](float, float) { return 0.0f; }), 1.0f, 1.0f / 60.0f);
            worst = std::max(worst, std::abs(b.world(I, L).y - 0.10f));
        }
        std::snprintf(what, sizeof what, "...the feet stay on the ground through it (off by at most %.4f m)", worst);
        check(worst < 0.005f, what);
    }
    {
        // Looking and aiming, on pelvis > spine > chest > neck > head (facing +Z).
        // `turned`: the joints carry Blender's axes (each turned -90 degrees about X,
        // its child offsets along its own +Y) -- the turns must not care.
        for (int turned = 0; turned < 2; ++turned) {
            Skeleton sk;
            const glm::quat tilt = turned ? glm::angleAxis(glm::radians(-90.0f), glm::vec3(1, 0, 0)) : glm::quat(1, 0, 0, 0);
            auto bone = [&](const char* name, const char* parent, float up) {
                BoneTransform t;
                // Each joint up from its parent by `up` in the model; with turned axes the
                // offset is in the parent's turned frame, and the joint itself turned back.
                t.position = turned && std::string(parent) != "root" && std::string(parent) != "pelvis"
                                 ? glm::inverse(tilt) * glm::vec3(0, up, 0) : glm::vec3(0, up, 0);
                t.rotation = std::string(parent) == "pelvis" ? tilt : glm::quat(1, 0, 0, 0);
                if (turned && std::string(parent) == "pelvis") t.position = glm::vec3(0, up, 0);
                sk.AddBone(name, parent, t);
            };
            sk.AddBone("root", "", at(0, 0, 0));
            sk.AddBone("pelvis", "root", at(0, 1.0f, 0));
            bone("spine", "pelvis", 0.15f);
            bone("chest", "spine", 0.2f);
            bone("neck", "chest", 0.25f);
            bone("head", "neck", 0.1f);
            std::vector<BoneTransform> rest;
            for (const auto& b : sk.GetAllBones()) rest.push_back(b.local_transform);
            auto reset = [&] {
                for (size_t b = 0; b < rest.size(); ++b) sk.SetBoneLocalTransform(static_cast<uint16_t>(b), rest[b]);
                sk.UpdateWorldTransforms();
            };
            reset();
            const glm::vec3 head0(sk.GetWorldMatrix(5)[3]);
            check(std::abs(head0.y - 1.7f) < 1e-4f && std::abs(head0.z) < 1e-4f, turned ? "turned axes: the head stands where it should (1.7 m)" : "the chain stands 1.7 m tall");
            const std::vector<int> chain = pose_chain(sk, 2, 5);
            check(chain.size() == 4 && chain.front() == 2 && chain.back() == 5, "pose_chain: spine, chest, neck, head");
            // Where a bone faces in the model: its rest +Z (forward) carried by its turn from rest.
            auto facing = [&](int b) {
                const glm::mat3 now(sk.GetWorldMatrix(static_cast<uint16_t>(b)));
                glm::mat4 rest_world(1.0f);
                for (int q = b; q >= 0; q = sk.GetBone(static_cast<uint16_t>(q))->parent_id) rest_world = rest[static_cast<size_t>(q)].ToMatrix() * rest_world;
                return glm::normalize(now * glm::inverse(glm::mat3(rest_world)) * glm::vec3(0, 0, 1));
            };
            auto deg = [](glm::vec3 a, glm::vec3 b) { return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f))); };
            const glm::vec3 target(1.5f, 2.6f, 4.0f);   // up and to the left (+X), ahead
            LookState glance;
            for (int i = 0; i < 90; ++i) { reset(); look_at(sk, chain, glance, glm::mat4(1.0f), target, 1.0f, 0.0f, 1.0f / 60.0f); }
            const float head_err = deg(facing(5), target - glm::vec3(sk.GetWorldMatrix(5)[3]));
            const float spine_turn = deg(facing(2), glm::vec3(0, 0, 1)), head_turn = deg(facing(5), glm::vec3(0, 0, 1));
            char what[180];
            std::snprintf(what, sizeof what, "%sa glance: the head faces the point (%.1f degrees off), the spine turned less than the head (%.1f < %.1f)",
                          turned ? "turned axes: " : "", head_err, spine_turn, head_turn);
            check(head_err < 6.0f && spine_turn < head_turn * 0.5f, what);
            LookState aimed;
            for (int i = 0; i < 90; ++i) { reset(); look_at(sk, chain, aimed, glm::mat4(1.0f), target, 1.0f, 1.0f, 1.0f / 60.0f); }
            const float chest_err = deg(facing(3), target - glm::vec3(sk.GetWorldMatrix(5)[3]));
            std::snprintf(what, sizeof what, "%san aim: the chest (what it holds) faces the point (%.1f degrees off)", turned ? "turned axes: " : "", chest_err);
            check(chest_err < 6.0f, what);
            for (int i = 0; i < 120; ++i) { reset(); look_at(sk, chain, aimed, glm::mat4(1.0f), target, 0.0f, 1.0f, 1.0f / 60.0f); }
            check(deg(facing(5), glm::vec3(0, 0, 1)) < 0.5f, "weight 0: it eases back to the clip's pose");
            if (!turned) {
                LookState behind;
                for (int i = 0; i < 120; ++i) { reset(); look_at(sk, chain, behind, glm::mat4(1.0f), glm::vec3(0.3f, 1.7f, -4.0f), 1.0f, 0.0f, 1.0f / 60.0f); }
                const float yaw = glm::degrees(std::atan2(facing(5).x, facing(5).z));
                std::snprintf(what, sizeof what, "a point behind: the head turns at most about 70 degrees (%.1f)", yaw);
                check(yaw > 60.0f && yaw < 72.0f, what);
                // A model placed and turned: the angles are its own.
                const glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, -2)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
                LookState placed;
                for (int i = 0; i < 90; ++i) { reset(); look_at(sk, chain, placed, M, glm::vec3(M * glm::vec4(target, 1.0f)), 1.0f, 0.0f, 1.0f / 60.0f); }
                std::snprintf(what, sizeof what, "a placed, turned model looks at the same point of its own (%.1f degrees off)",
                              deg(facing(5), target - glm::vec3(sk.GetWorldMatrix(5)[3])));
                check(deg(facing(5), target - glm::vec3(sk.GetWorldMatrix(5)[3])) < 6.0f, what);
            }
            // Leaning into an acceleration: forward (+Z) at half a g, then stopping.
            LeanState lean;
            // The chest segment (chest to neck) carries the whole lean (the spine below has half).
            auto chest_up = [&] { return glm::normalize(glm::vec3(sk.GetWorldMatrix(4)[3]) - glm::vec3(sk.GetWorldMatrix(3)[3])); };
            for (int i = 0; i < 90; ++i) { reset(); lean_into(sk, chain, lean, glm::mat4(1.0f), glm::vec3(0, 0, 4.9f), 1.0f, 1.0f / 60.0f); }
            const glm::vec3 up_fwd = chest_up();
            const float lean_deg = glm::degrees(std::atan2(up_fwd.z, up_fwd.y));
            std::snprintf(what, sizeof what, "%sspeeding up: it leans forward, at most 12 degrees (%.1f)", turned ? "turned axes: " : "", lean_deg);
            check(lean_deg > 11.0f && lean_deg < 12.5f, what);
            for (int i = 0; i < 90; ++i) { reset(); lean_into(sk, chain, lean, glm::mat4(1.0f), glm::vec3(-1.5f, 0, 0), 0.5f, 1.0f / 60.0f); }
            const glm::vec3 up_side = chest_up();
            std::snprintf(what, sizeof what, "%sa turn to the right (-X): it leans into it (%.2f toward -X) and no longer forward (%.2f)",
                          turned ? "turned axes: " : "", up_side.x, up_side.z);
            check(up_side.x < -0.02f && std::abs(up_side.z) < 0.01f, what);
            for (int i = 0; i < 90; ++i) { reset(); lean_into(sk, chain, lean, glm::mat4(1.0f), glm::vec3(0.0f), 0.5f, 1.0f / 60.0f); }
            check(deg(chest_up(), glm::vec3(0, 1, 0)) < 0.5f, "steady: upright again");
            // A blow from the front (pushing back, -Z): it tips back about 10 degrees and comes back.
            flinch(lean, glm::mat4(1.0f), glm::vec3(0, 0, -1), 5.5f);
            float most = 0.0f;
            for (int i = 0; i < 60; ++i) {
                reset();
                lean_into(sk, chain, lean, glm::mat4(1.0f), glm::vec3(0.0f), 0.5f, 1.0f / 60.0f);
                const glm::vec3 u = chest_up();
                most = std::max(most, glm::degrees(std::atan2(-u.z, u.y)));
            }
            std::snprintf(what, sizeof what, "%sa blow from the front: it tips back (%.1f degrees at most) and is upright again after a second (%.2f)",
                          turned ? "turned axes: " : "", most, deg(chest_up(), glm::vec3(0, 1, 0)));
            check(most > 7.0f && most < 13.0f && deg(chest_up(), glm::vec3(0, 1, 0)) < 1.0f, what);
        }
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
