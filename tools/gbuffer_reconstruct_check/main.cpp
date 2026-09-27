// ====================
// gbuffer_reconstruct_check — world-position-from-depth formula (F5, phase 1)
// ====================
//
// Proves the position-from-depth reconstruction formula BEFORE it goes into
// tools/lighting_pass.frag. This is the one place a GLSL convention bug can
// be caught: no `gws test --gpu` check reads back rendered pixels (issue
// #64), so a wrong depth convention would ship as a subtly wrong picture on
// every machine, indefinitely, until someone noticed by eye.
//
// The formula mirrors lighting_pass.frag's `pc.invViewProj * vec4(ndc.xy,
// depth, 1.0)`, homogeneous-divided. Vulkan's depth range is [0,1] (unlike
// OpenGL's [-1,1]) and this engine uses standard, non-reversed depth —
// VK_COMPARE_OP_LESS (vulkan_g_buffer.cpp), clear value 1.0, confirmed by
// main()'s own `gbufferDepth >= 1.0 - 1e-5` sky check, which only makes
// sense if the far plane is depth=1.0. So depth needs NO remapping; only
// screen UV does (*2-1). Group 3 below exists specifically to prove that:
// it computes the WRONG (GL-style) remap and confirms it does NOT
// round-trip, so this check can actually distinguish the two conventions
// rather than passing either way.
//
// Tested by forward/inverse round-trip: project a KNOWN world point through
// the SAME view+proj construction tests/deferred_scene_test.cpp uses
// (glm::lookAt / glm::perspective with the Vulkan Y-flip), producing the
// exact (uv, depth) pair a real depth buffer would hold for that point,
// then feed those into the function under test and require the original
// point back. Project and reconstruct are independent code paths (one is
// plain GLM math nobody wrote by hand), so a convention bug — wrong depth
// remap, wrong divide, a row/column-major mixup — fails this; it is not
// testing the function against itself.

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

// Mirrors tools/lighting_pass.frag's worldPosFromDepth() exactly. Task 2
// ports this body verbatim into GLSL.
glm::vec3 world_pos_from_depth(glm::vec2 uv, float depth_ndc, const glm::mat4& inv_view_proj) {
    const glm::vec2 ndc = uv * 2.0f - 1.0f;
    const glm::vec4 clip(ndc.x, ndc.y, depth_ndc, 1.0f);
    const glm::vec4 world_h = inv_view_proj * clip;
    return glm::vec3(world_h) / world_h.w;
}

int g_failures = 0;
void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}

bool close(const glm::vec3& a, const glm::vec3& b, float eps) {
    return glm::length(a - b) < eps;
}

// Standard (non-reversed) Z packs depth precision toward the NEAR plane and
// starves the far plane: a single float32 ULP of error in a depth value near
// 1.0 can correspond to centimetres of world-space error at long range. This
// is physics, not a formula bug — verified separately (see the tolerance
// note below) to be smaller than the ORIGINAL RGBA16F position-texture's own
// quantization error at the same distance, so reconstruction is not a
// precision regression versus what shipped before. `close()` above uses a
// fixed tight epsilon for the near-camera cases where this does not apply;
// this scaled one is for cases deliberately placed near the far plane.
bool close_at_range(const glm::vec3& a, const glm::vec3& b, const glm::vec3& camera_pos) {
    const float distance = glm::length(a - camera_pos);
    const float eps = std::max(1e-3f, distance * 1e-4f);
    return glm::length(a - b) < eps;
}

// Forward-projects `world`, returning the (uv, depth) pair a real geometry
// pass would leave in the G-buffer for it — the fixture every case below
// hands to the function under test. `glm::vec2(ndc)` truncates the vec3;
// this repo does not define GLM_FORCE_SWIZZLE, so `.xy()` is not available.
struct ScreenSample { glm::vec2 uv; float depth; };
ScreenSample project(const glm::vec3& world, const glm::mat4& view, const glm::mat4& proj) {
    const glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return { glm::vec2(ndc) * 0.5f + 0.5f, ndc.z };
}

}  // namespace

int main() {
    std::printf("gbuffer_reconstruct_check -- world-position-from-depth formula (F5)\n");

    // Same construction as tests/deferred_scene_test.cpp: 60 deg FOV,
    // 0.1..100 near/far, camera off-origin, Vulkan's clip-space Y flip
    // applied exactly as the renderer applies it (proj[1][1] *= -1).
    const glm::mat4 view = glm::lookAt(glm::vec3(2.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    const glm::mat4 inv_view_proj = glm::inverse(proj * view);
    const float eps = 1e-3f;  // float32 round-trip through a 4x4 inverse

    const glm::vec3 camera_pos(2.0f, 2.0f, 3.0f);
    const glm::vec3 view_axis = glm::normalize(glm::vec3(0.0f) - camera_pos);

    std::printf("\n[group] round-trips a known world point through project -> reconstruct\n");
    {
        const glm::vec3 points[] = {
            {0.0f, 0.0f, 0.0f},                    // world origin, roughly screen centre
            {1.5f, 0.5f, -0.5f},                   // an ordinary scene point
            camera_pos + view_axis * 99.0f,        // just short of the far plane, ON this
                                                    // camera's own view axis -- a point
                                                    // merely far along -Z (e.g. (0,0,-99.9))
                                                    // can be farther from an off-axis camera
                                                    // than the far plane distance, landing
                                                    // outside the valid depth range entirely
            {1.9f, 1.9f, 2.9f},                    // just in front of the camera
            {-8.0f, 4.0f, -8.0f},                  // toward a screen corner
            {8.0f, -4.0f, 8.0f},
            {5000.0f, 20.0f, -5000.0f},             // far from the origin -- the precision
                                                    // case the shader's own comment (line
                                                    // 585 of lighting_pass.frag) warns about
        };
        for (const auto& p : points) {
            const ScreenSample s = project(p, view, proj);
            if (s.uv.x < 0.0f || s.uv.x > 1.0f || s.uv.y < 0.0f || s.uv.y > 1.0f) {
                continue;  // outside this camera's frustum -- not a case the shader sees
            }
            const glm::vec3 got = world_pos_from_depth(s.uv, s.depth, inv_view_proj);
            char label[96];
            std::snprintf(label, sizeof(label), "(%.1f, %.1f, %.1f) round-trips", p.x, p.y, p.z);
            // Points placed deliberately near the far plane (99 units out of
            // a far=100 camera) hit float32 depth's known precision floor —
            // see close_at_range()'s comment. Everything else uses the tight
            // fixed epsilon; a formula bug fails BOTH kinds of case, a
            // precision artifact fails only the far one.
            const bool near_far_plane = glm::length(p - camera_pos) > 50.0f;
            check(label, near_far_plane ? close_at_range(got, p, camera_pos) : close(got, p, eps));
        }
    }

    std::printf("\n[group] screen centre and the four corners\n");
    {
        // uv=(0.5,0.5) is what main() samples for a straight-ahead pixel;
        // the corners are where a UV-remap sign error shows up first.
        const struct { glm::vec2 uv; float depth; } cases[] = {
            {{0.5f, 0.5f}, 0.5f}, {{0.0f, 0.0f}, 0.5f}, {{1.0f, 0.0f}, 0.5f},
            {{0.0f, 1.0f}, 0.5f}, {{1.0f, 1.0f}, 0.5f},
        };
        for (const auto& c : cases) {
            const glm::vec3 world = world_pos_from_depth(c.uv, c.depth, inv_view_proj);
            const ScreenSample back = project(world, view, proj);
            char label[96];
            std::snprintf(label, sizeof(label),
                          "uv(%.1f,%.1f) reconstructs a point that reprojects to the same uv",
                          c.uv.x, c.uv.y);
            check(label, close(glm::vec3(back.uv, back.depth), glm::vec3(c.uv, c.depth), eps));
        }
    }

    std::printf("\n[group] depth is NOT remapped like OpenGL's [-1,1] convention\n");
    {
        // The bug this whole check exists to catch: treating Vulkan's
        // [0,1] depth like GL's [-1,1] and writing `depth * 2.0 - 1.0`.
        // Deliberately compute it the WRONG way and confirm it does NOT
        // round-trip -- if it did, this check could not tell the two
        // conventions apart, and every other group above would be
        // meaningless.
        const glm::vec3 p(1.5f, 0.5f, -0.5f);
        const ScreenSample s = project(p, view, proj);
        const glm::vec2 ndc = s.uv * 2.0f - 1.0f;
        const glm::vec4 wrong_clip(ndc.x, ndc.y, s.depth * 2.0f - 1.0f, 1.0f);
        const glm::vec4 wrong_h = inv_view_proj * wrong_clip;
        const glm::vec3 wrong = glm::vec3(wrong_h) / wrong_h.w;
        check("the GL-style [-1,1] remap gives a DIFFERENT point "
              "(proves this check can tell the two conventions apart)",
              !close(wrong, p, eps));
        check("the actual (no-remap) formula gives the right point back",
              close(world_pos_from_depth(s.uv, s.depth, inv_view_proj), p, eps));
    }

    std::printf("\n[group] a cleared (sky) pixel reads as the far plane, not garbage\n");
    {
        // Sky pixels hold the depth clear value (1.0 -- VK_COMPARE_OP_LESS,
        // vulkan_g_buffer.cpp) and main() treats `depth >= 1.0 - 1e-5` as
        // sky. Reconstructing AT depth=1.0 must land on the far plane
        // along that ray, not NaN or the origin -- contactShadow()'s
        // replacement sky check (Task 2) depends on this being a
        // well-defined point, not a divide-by-zero.
        const glm::vec3 far_point = world_pos_from_depth({0.5f, 0.5f}, 1.0f, inv_view_proj);
        check("finite", std::isfinite(far_point.x) && std::isfinite(far_point.y) && std::isfinite(far_point.z));
        const float dist_from_camera = glm::length(far_point - glm::vec3(2.0f, 2.0f, 3.0f));
        check("close to the far plane (100 units), not the near one",
              dist_from_camera > 90.0f && dist_from_camera < 110.0f);
    }

    std::printf("\n%s -- %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
