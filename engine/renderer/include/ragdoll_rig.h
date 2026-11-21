#pragma once
// ============================================================================
// ragdoll_rig.h -- a character's ragdoll: which bones become bodies, how big
// and heavy, how their joints bend, and the pose carried back onto the
// skeleton while it is limp.
//
// Any biped rig, by its bones' names: the legs as foot_placement.h finds them,
// the spine from the pelvis up to the head (pose_layers.h), the arms by their
// hands (`hand.L` / `LeftHand`...). Twelve bodies: pelvis, spine, chest, head,
// upper arms, forearms, thighs, shins. Knees and elbows are hinges that bend
// only the way they do, limited from the bend they have when the character
// falls; the rest are cones with a little twist. Hands, feet, fingers and
// clavicles keep their last pose on the body that carries them.
//
// Pure math on a Skeleton: the physics (engine/core/physics, Jolt's Ragdoll)
// takes the parts this makes. Checked by tools/ragdoll_check.
// ============================================================================
#include "skeleton.h"
#include "foot_placement.h"
#include "pose_layers.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::renderer {

struct RagdollRigPart {
    int bone = -1;          // the bone it moves (its joint is the body's pivot)
    int tip_bone = -1;      // the bone whose joint ends its capsule (-1: `tip_reach` along the bone)
    float tip_reach = 0.0f; // with no tip bone (the head): how far past the joint
    float extend = 0.0f;    // and how far past the tip bone (a hand on the forearm, a foot on the shin)
    int parent = -1;        // its parent part (-1: the root, the pelvis)
    float radius = 0.08f, mass = 5.0f;
    int hinge = 0;          // 0 a cone; 1 a knee (bends back); 2 an elbow (bends forward)
    float cone = 0.5f, twist = 0.3f;
};
struct RagdollRig {
    std::vector<RagdollRigPart> parts;
    int chest = -1;   // the part the head hangs from (a shot to the body pushes it)
    bool valid() const { return parts.size() >= 8; }
};

/// The parts of a biped's ragdoll; an invalid rig when the legs, the spine or
/// the arms cannot be found.
inline RagdollRig find_ragdoll_rig(const Skeleton& sk, const std::vector<BoneTransform>& bind) {
    RagdollRig rig;
    const FootRig feet = find_foot_rig(sk, bind);
    if (!feet.valid() || feet.pelvis < 0) return rig;
    auto lower = [](std::string s) {
        if (const size_t at = s.find("__"); at != std::string::npos) s.resize(at);
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto parent = [&](int b) { const Bone* bone = b >= 0 ? sk.GetBone(static_cast<uint16_t>(b)) : nullptr; return bone ? bone->parent_id : -1; };
    const int n = static_cast<int>(sk.GetBoneCount());
    // The head, and the arms by their hands.
    int head = -1, hand[2] = {-1, -1};
    for (int b = 0; b < n; ++b) {
        const std::string nm = lower(sk.GetBone(static_cast<uint16_t>(b))->name);
        if (nm.find("ik") != std::string::npos || nm.find("target") != std::string::npos || nm.find("end") != std::string::npos) continue;
        if (head < 0 && nm.find("head") != std::string::npos && nm.find("top") == std::string::npos) head = b;
        if (nm.find("hand") == std::string::npos) continue;
        auto ends = [&](const char* x) { const std::string e(x); return nm.size() >= e.size() && nm.compare(nm.size() - e.size(), e.size(), e) == 0; };
        const int side = (nm.find("left") != std::string::npos || ends(".l") || ends("_l") || nm.rfind("l_", 0) == 0) ? 0
                       : (nm.find("right") != std::string::npos || ends(".r") || ends("_r") || nm.rfind("r_", 0) == 0) ? 1 : -1;
        if (side >= 0 && hand[side] < 0) hand[side] = b;
    }
    if (head < 0 || hand[0] < 0 || hand[1] < 0) return rig;
    // The spine from the pelvis' child up to the head.
    int base = head;
    while (base >= 0 && parent(base) != feet.pelvis) base = parent(base);
    if (base < 0) return rig;
    const std::vector<int> chain = pose_chain(sk, base, head);   // e.g. spine, chest, neck, head
    if (chain.size() < 2) return rig;
    // The torso: the pelvis, then the chain's bones up to (not with) the neck/head
    // as one or two bodies, then the head (on the neck when there is one).
    const int neck = chain.size() >= 3 ? chain[chain.size() - 2] : head;
    std::vector<int> torso(chain.begin(), chain.end() - (chain.size() >= 3 ? 2 : 1));
    if (torso.empty()) torso.push_back(chain.front());
    if (torso.size() > 2) torso = {torso.front(), torso.back()};
    RagdollRigPart pelvis;
    pelvis.bone = feet.pelvis;
    pelvis.tip_bone = torso.front();
    pelvis.radius = 0.13f;
    pelvis.mass = 12.0f;
    rig.parts.push_back(pelvis);
    int chest_part = 0;
    for (size_t i = 0; i < torso.size(); ++i) {
        RagdollRigPart p;
        p.bone = torso[i];
        p.tip_bone = i + 1 < torso.size() ? torso[i + 1] : neck;
        p.parent = static_cast<int>(rig.parts.size()) - 1;
        p.radius = i + 1 < torso.size() ? 0.12f : 0.14f;
        p.mass = i + 1 < torso.size() ? 8.0f : 12.0f;
        p.cone = 0.35f;
        p.twist = 0.3f;
        chest_part = static_cast<int>(rig.parts.size());
        rig.parts.push_back(p);
    }
    RagdollRigPart hd;
    hd.bone = neck;
    hd.tip_bone = neck == head ? -1 : head;
    hd.extend = 0.18f;
    hd.tip_reach = 0.25f;
    hd.parent = chest_part;
    hd.radius = 0.1f;
    hd.mass = 5.0f;
    hd.cone = 0.6f;
    hd.twist = 0.6f;
    rig.parts.push_back(hd);
    rig.chest = chest_part;
    for (int side = 0; side < 2; ++side) {
        const int fore = parent(hand[side]), upper = fore >= 0 ? parent(fore) : -1;
        if (upper < 0) return RagdollRig{};
        RagdollRigPart u;
        u.bone = upper; u.tip_bone = fore; u.parent = chest_part; u.radius = 0.05f; u.mass = 2.5f; u.cone = 1.3f; u.twist = 0.8f;
        rig.parts.push_back(u);
        RagdollRigPart f;
        f.bone = fore; f.tip_bone = hand[side]; f.extend = 0.1f; f.parent = static_cast<int>(rig.parts.size()) - 1;
        f.radius = 0.045f; f.mass = 1.5f; f.hinge = 2;
        rig.parts.push_back(f);
    }
    for (int side = 0; side < 2; ++side) {
        const FootLeg& L = feet.legs[side];
        RagdollRigPart t;
        t.bone = L.hip; t.tip_bone = L.knee; t.parent = 0; t.radius = 0.08f; t.mass = 7.0f; t.cone = 0.9f; t.twist = 0.4f;
        rig.parts.push_back(t);
        RagdollRigPart s;
        s.bone = L.knee; s.tip_bone = L.ankle; s.extend = 0.08f; s.parent = static_cast<int>(rig.parts.size()) - 1;
        s.radius = 0.06f; s.mass = 4.0f; s.hinge = 1;
        rig.parts.push_back(s);
    }
    return rig;
}

/// A part as it starts, in the world (what the physics takes; see
/// physics::RagdollPart, field for field).
struct RagdollPartStart {
    int parent = -1;
    glm::vec3 joint{0.0f}, tip{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius = 0.08f, mass = 5.0f;
    bool hinge = false;
    glm::vec3 hinge_axis{1.0f, 0.0f, 0.0f};
    float hinge_min = 0.0f, hinge_max = 0.0f, cone = 0.5f, twist = 0.3f;
};

/// The parts where the skeleton's pose (world transforms current) puts them,
/// `model` placing it in the world (+Z its forward, +Y up). A knee or an elbow
/// gets its hinge from the bend it has now: about the line square to both its
/// bones, or -- straight -- about the model's side axis, and may bend from
/// nearly straight to 150 degrees (a knee) or 145 (an elbow).
inline std::vector<RagdollPartStart> ragdoll_start(const Skeleton& sk, const RagdollRig& rig, const glm::mat4& model) {
    std::vector<RagdollPartStart> out;
    out.reserve(rig.parts.size());
    auto rot = [](const glm::mat4& m) {
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                       glm::normalize(glm::vec3(m[2])))));
    };
    const glm::quat model_rot = rot(model);
    auto world = [&](int b) { return glm::vec3(model * sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); };
    for (const RagdollRigPart& p : rig.parts) {
        RagdollPartStart s;
        s.parent = p.parent;
        s.joint = world(p.bone);
        s.rotation = glm::normalize(model_rot * rot(sk.GetWorldMatrix(static_cast<uint16_t>(p.bone))));
        if (p.tip_bone >= 0) {
            s.tip = world(p.tip_bone);
            const glm::vec3 d = s.tip - s.joint;
            if (p.extend > 0.0f && glm::length(d) > 1e-4f) s.tip += glm::normalize(d) * p.extend;
        } else {
            s.tip = s.joint + s.rotation * glm::vec3(0.0f, p.tip_reach, 0.0f);
        }
        s.radius = p.radius;
        s.mass = p.mass;
        s.cone = p.cone;
        s.twist = p.twist;
        if (p.hinge && p.parent >= 0) {
            const RagdollPartStart& up = out[static_cast<size_t>(p.parent)];
            const glm::vec3 a = glm::normalize(s.joint - up.joint), b = glm::normalize(s.tip - s.joint);
            const float bend = std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f));
            glm::vec3 axis = glm::cross(a, b);
            // Straight: a knee folds about the model's +X (its shin swings back), an elbow about -X (forward).
            if (bend < glm::radians(5.0f) || glm::length(axis) < 1e-4f)
                axis = glm::mat3(model) * glm::vec3(p.hinge == 1 ? 1.0f : -1.0f, 0.0f, 0.0f);
            s.hinge = true;
            s.hinge_axis = glm::normalize(axis);
            s.hinge_min = -bend + glm::radians(2.0f);
            s.hinge_max = glm::radians(p.hinge == 1 ? 150.0f : 145.0f) - bend;
        }
        out.push_back(s);
    }
    return out;
}

/// While limp: each part's body (its pivot and turn, world) moves its bone; the
/// bones between keep their pose on the bone that carries them. `model` as at
/// ragdoll_start (the character does not move: its bones go where the bodies are).
inline void apply_ragdoll_pose(Skeleton& sk, const RagdollRig& rig, const glm::mat4& model, const std::vector<glm::vec3>& positions,
                               const std::vector<glm::quat>& rotations) {
    if (positions.size() != rig.parts.size() || rotations.size() != rig.parts.size()) return;
    const size_t n = sk.GetBoneCount();
    std::vector<int> driven(n, -1);
    for (size_t i = 0; i < rig.parts.size(); ++i)
        if (rig.parts[i].bone >= 0 && static_cast<size_t>(rig.parts[i].bone) < n) driven[static_cast<size_t>(rig.parts[i].bone)] = static_cast<int>(i);
    const glm::mat4 inv_model = glm::inverse(model);
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));
    const auto& bones = sk.GetAllBones();
    for (size_t b = 0; b < n; ++b) {
        const int p = bones[b].parent_id;
        const glm::mat4 parent_w = p >= 0 && static_cast<size_t>(p) < b ? world[static_cast<size_t>(p)] : glm::mat4(1.0f);
        BoneTransform local = bones[b].local_transform;
        if (driven[b] >= 0) {
            const size_t i = static_cast<size_t>(driven[b]);
            glm::mat4 w = inv_model * glm::translate(glm::mat4(1.0f), positions[i]) * glm::mat4_cast(rotations[i]);
            // keep the bone's own scale
            w[0] *= local.scale.x;
            w[1] *= local.scale.y;
            w[2] *= local.scale.z;
            const glm::mat4 l = glm::inverse(parent_w) * w;
            local.position = glm::vec3(l[3]);
            local.rotation = glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(l[0])), glm::normalize(glm::vec3(l[1])),
                                                                     glm::normalize(glm::vec3(l[2])))));
            sk.SetBoneLocalTransform(static_cast<uint16_t>(b), local);
        }
        world[b] = parent_w * local.ToMatrix();
    }
    sk.UpdateWorldTransforms();
}

}  // namespace schizo::renderer
