// springbones_check: secondary motion (spring_bones.h), headless. A 4-bone
// antenna standing on a body: it keeps its bones' lengths, sags a little under
// gravity and comes to rest; the body moved sideways and stopped, it trails
// behind, swings past and settles; a sphere keeps it out; the same motion at
// 30 and 144 frames a second ends the same; the .springs file is read.
#include "spring_bones.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace schizo::renderer;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
std::string f3(float x) { char b[32]; std::snprintf(b, sizeof b, "%.3f", x); return b; }

BoneTransform at(float x, float y, float z) {
    BoneTransform t;
    t.position = glm::vec3(x, y, z);
    return t;
}
// root > body (1 m up) > antenna_01..04, each 0.12 m up.
struct Rig {
    Skeleton sk;
    Rig() {
        sk.AddBone("root", "", at(0, 0, 0));
        sk.AddBone("body", "root", at(0, 1.0f, 0));
        sk.AddBone("antenna_01", "body", at(0, 0.1f, 0));
        sk.AddBone("antenna_02", "antenna_01", at(0, 0.12f, 0));
        sk.AddBone("antenna_03", "antenna_02", at(0, 0.12f, 0));
        sk.AddBone("antenna_04", "antenna_03", at(0, 0.12f, 0));
        sk.UpdateWorldTransforms();
        for (const auto& b : sk.GetAllBones()) bind.push_back(b.local_transform);
    }
    std::vector<BoneTransform> bind;
    void pose() {   // the clip's pose: the bind pose
        for (size_t i = 0; i < bind.size(); ++i) sk.SetBoneLocalTransform(static_cast<uint16_t>(i), bind[i]);
        sk.UpdateWorldTransforms();
    }
    int find(const std::string& n) const {
        for (const auto& b : sk.GetAllBones()) if (b.name == n) return static_cast<int>(b.id);
        return -1;
    }
    glm::vec3 world(const glm::mat4& m, int b) const { return glm::vec3(m * sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); }
};
SpringFile antenna_file(float gravity = 0.6f) {
    SpringFile f;
    SpringChainDef c;
    c.first = "antenna_01";
    c.last = "antenna_04";
    c.stiffness = 0.08f;
    c.damping = 0.12f;
    c.gravity = gravity;
    f.chains.push_back(c);
    return f;
}
}  // namespace

int main() {
    {
        Rig r;
        SpringBones sb;
        sb.init(r.sk, antenna_file(), [&](const std::string& n) { return r.find(n); });
        check(sb.chain_count() == 1 && sb.chain_bones(0).size() == 4, "a chain from antenna_01 to antenna_04: four bones");
        // Leaning 20 degrees (an antenna straight up balances on its own: gravity along it).
        const glm::mat4 M = glm::rotate(glm::mat4(1.0f), glm::radians(20.0f), glm::vec3(0, 0, 1));
        r.pose();
        const glm::vec3 tip_rest = glm::vec3(M * r.sk.GetWorldMatrix(5) * glm::vec4(0, 0.12f, 0, 1));
        float worst_len = 0.0f;
        glm::vec3 last_tip(0.0f), tip(0.0f);
        float still = 1.0f;
        for (int i = 0; i < 600; ++i) {   // 10 s
            r.pose();
            sb.update(r.sk, M, 1.0f / 60.0f);
            for (int b = 3; b <= 5; ++b) worst_len = std::max(worst_len, std::abs(glm::length(r.world(M, b) - r.world(M, b - 1)) - 0.12f));
            last_tip = tip;
            tip = sb.tips(0).back();
            if (i == 599) still = glm::length(tip - last_tip) * 60.0f;
        }
        check(worst_len < 1e-3f, "its bones keep their lengths (" + f3(worst_len) + " m off at most)");
        const float sag = tip_rest.y - tip.y;
        check(sag > 0.003f && sag < 0.2f && still < 1e-3f,
              "under gravity it sags " + f3(sag) + " m at the tip (a little, not flat) and comes to rest (" + f3(still) + " m/s)");

        // The body carried 1 m to +X in 0.25 s, then still: the tip trails, swings past, settles.
        sb.reset();
        float trail = 0.0f, past = 0.0f;
        glm::vec3 settled(0.0f);
        for (int i = 0; i < 600; ++i) {
            const float x = std::min(1.0f, static_cast<float>(i) / 15.0f);
            const glm::mat4 Mx = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
            r.pose();
            sb.update(r.sk, Mx, 1.0f / 60.0f);
            const float off = sb.tips(0).back().x - (x + 0.0f);
            if (i < 15) trail = std::min(trail, off);
            else past = std::max(past, off);
            settled = sb.tips(0).back() - glm::vec3(x, 0.0f, 0.0f);
        }
        check(trail < -0.02f && past > 0.005f && std::abs(settled.x) < 2e-3f,
              "the body moved 1 m and stopped: the tip trails " + f3(-trail) + " m, swings " + f3(past) + " m past, settles (" + f3(settled.x) + ")");
    }
    {
        // Carried at a steady 8 m/s it stands as it does at rest (no drag): only a change of speed bends it.
        Rig r;
        SpringBones sb;
        sb.init(r.sk, antenna_file(), [&](const std::string& n) { return r.find(n); });
        glm::vec3 rest(0.0f), moving(0.0f);
        for (int i = 0; i < 300; ++i) { r.pose(); sb.update(r.sk, glm::mat4(1.0f), 1.0f / 60.0f); }
        rest = sb.tips(0).back();
        sb.reset();
        float x = 0.0f;
        for (int i = 0; i < 300; ++i) {
            x += 8.0f / 60.0f * std::min(1.0f, static_cast<float>(i) / 15.0f);   // up to speed in a quarter second
            r.pose();
            sb.update(r.sk, glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)), 1.0f / 60.0f);
        }
        moving = sb.tips(0).back() - glm::vec3(x, 0.0f, 0.0f);
        check(glm::length(moving - rest) < 0.01f, "carried at a steady 8 m/s it stands as at rest (" + f3(glm::length(moving - rest)) + " m off): no drag");
    }
    {
        // A sphere where the tip would sag to: it stays outside.
        Rig r;
        SpringFile f = antenna_file(3.0f);
        f.colliders.push_back({"body", glm::vec3(0.25f, 0.5f, 0.0f), 0.2f});
        f.chains[0].stiffness = 0.01f;
        SpringBones sb;
        sb.init(r.sk, f, [&](const std::string& n) { return r.find(n); });
        const glm::mat4 M = glm::rotate(glm::mat4(1.0f), glm::radians(-60.0f), glm::vec3(0, 0, 1));   // leaning toward the sphere
        float inside = 0.0f;
        for (int i = 0; i < 300; ++i) {
            r.pose();
            sb.update(r.sk, M, 1.0f / 60.0f);
            const glm::vec3 c = glm::vec3(M * r.sk.GetWorldMatrix(1) * glm::vec4(0.25f, 0.5f, 0.0f, 1.0f));
            for (const auto& p : sb.tips(0)) inside = std::max(inside, 0.2f + 0.01f - glm::length(p - c));
        }
        check(inside < 2e-3f, "a collider sphere keeps the chain out (" + f3(std::max(0.0f, inside)) + " m in at most)");
    }
    {
        // Frame-rate independent: the same motion at 30 and at 144 frames a second.
        auto run = [](float fps) {
            Rig r;
            SpringBones sb;
            sb.init(r.sk, antenna_file(), [&](const std::string& n) { return r.find(n); });
            // (the body's motion turns at 1/3 s, a frame boundary at both rates: both see the same motion)
            const float dt = 1.0f / fps;
            const int frames = static_cast<int>(std::lround(1.5f * fps));
            for (int k = 1; k <= frames; ++k) {
                const float t = static_cast<float>(k) * dt;
                const glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(std::min(1.0f, t * 3.0f), 0.0f, 0.0f));
                r.pose();
                sb.update(r.sk, M, dt);
            }
            return sb.shown_tips(0).back();
        };
        const float d = glm::length(run(30.0f) - run(144.0f));
        check(d < 0.01f, "the same motion at 30 and 144 fps ends within " + f3(d) + " m");
    }
    {
        const SpringFile f = parse_spring_file("# a radio\nchain antenna_01 antenna_04 0.1 0.2 0.5 0.02\ncollider chest 0 0.05 0.12 0.2\nnonsense here\n");
        check(f.chains.size() == 1 && f.chains[0].last == "antenna_04" && std::abs(f.chains[0].damping - 0.2f) < 1e-6f &&
                  f.colliders.size() == 1 && std::abs(f.colliders[0].centre.z - 0.12f) < 1e-6f && std::abs(f.colliders[0].radius - 0.2f) < 1e-6f,
              "a .springs file: a chain and a collider read, a line it does not know skipped");
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
