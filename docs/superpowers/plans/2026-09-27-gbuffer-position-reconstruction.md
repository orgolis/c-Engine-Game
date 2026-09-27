# G-Buffer Position Reconstruction (F5, Phase 1: the lighting pass) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the lighting pass — 94% of GPU frame time per F0 — from sampling the G-buffer's position
attachment, by reconstructing world-space position from the depth buffer it already samples, using the
inverse view-projection matrix it already has. No G-buffer, descriptor-layout, or C++ change.

**Architecture:** `tools/lighting_pass.frag` gains one small helper, `worldPosFromDepth(uv, depthNdc)`, built
from `depthTex` (already bound at set=0 binding=4) and `pc.invViewProj` (already in the push-constant block).
Its two existing reads of `positionTex` are replaced with calls to it. The `positionTex` binding itself is
left declared but unused — the G-buffer still writes that attachment for the other 13 consumers listed in
`docs/EngineMasterPlan/PERFORMANCE_AUDIT.md` F5, and removing the attachment is out of scope here (see
**Not in this plan** below). The formula is proven correct on the host, in C++, before it goes anywhere near
GLSL — there is no automated way to read back a rendered pixel in this project yet (`gws screenshot` is
issue #64), so a wrong convention in a shader would ship silently.

**Tech Stack:** C++20, GLM (column-major `mat4`, no swizzle operators — `GLM_FORCE_SWIZZLE` is not defined
anywhere in this repo, so `.xy()`-style access does not compile; truncate with `glm::vec2(v3)` instead),
GLSL 460 compiled by `glslangValidator` to SPIR-V embedded as C headers, this project's `*_check` convention
(a `main()` that prints `[OK]`/`[FAIL]` per assertion and returns the failure count).

**Spec:** `docs/EngineMasterPlan/PERFORMANCE_AUDIT.md`, finding **F5** ("The G-buffer is 28 bytes per pixel, 8
of them redundant") and its done-criterion under **Work plan**.

## Global Constraints

- **Versioning continues in the 0.8.n series** — this ships as **v0.8.6**, not v0.9.0 (explicit project rule;
  see `CMakeLists.txt` line 3 for the current value before bumping).
- **Every behavioural change ships with a check** (project convention; see `README.md` § Contributing). The
  check for this change is Task 1, written and passing *before* Task 2 touches the shader.
- **SPIR-V is precompiled and manually regenerated.** `tools/lighting_pass.frag` has two compiled variants
  (ray-query and non-ray-query) that **must be regenerated together** via `tools/regen_lighting_spirv.sh` —
  regenerating only one is how they drift apart (see that script's own header comment).
- **No GLM swizzle operators.** Use `glm::vec2(someVec3)` to truncate, `glm::vec3(someVec4)` likewise — never
  `.xy()` or `.xyz()`, which are GLSL syntax, not default GLM.
- **Build/run from the repository root** with the pinned toolchain: `cmake --preset windows-debug` (already
  configured in this tree), then `cmake --build build/windows-debug --target <name>` per task, ninja at
  `C:/Strawberry/c/bin/ninja.exe`.
- **Commit messages end with** `Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>` (current session
  attribution — check the latest system reminder before committing in case it has changed since this plan was
  written).

## Not in this plan

The audit's F5 lists **14** shader consumers of the position attachment; this plan converts **1** (the
lighting pass — also the single most expensive pass in the entire frame per F0, so the highest-value one to
do first). The other 13 (`ddgi_composite.frag`, `froxel_composite.frag`, `volumetric_composite.frag`,
`water.frag`, `gtao.comp`, `hbao.comp`, `hdao.comp`, `ssao.comp`, `ssao_rt.comp`, `ssr.comp`, `ssr_rt.comp`,
`volumetric_light.comp`, `vxao.comp`) were surveyed while scoping this plan and **12 of them have no camera
matrix (view/proj/invViewProj) bound at all today** — only `water.frag` does. Converting those needs a new
per-pass UBO or push-constant addition (C++ descriptor-set changes, not just a GLSL swap), and 3 of the 13
(`froxel_composite.frag`, `volumetric_composite.frag`, `vxao.comp`) also lack a depth-texture binding and
need one added. None of the 14 are covered by `shadersource_check`, so there is no existing drift check for
any of them. That is a materially bigger and differently-shaped task — do not fold it into this plan; it
needs its own scoping pass once this slice has shipped and been measured. Task 4 records this in
`PERFORMANCE_AUDIT.md` so it is not lost.

Dropping the G-buffer's position **attachment** (the "20 B/px" and "Geometry measurably cheaper" parts of
F5's done-criterion) requires *all 14* consumers converted first — it is not attempted here. This plan's
measurable win is the lighting pass no longer reading it, not the attachment's removal.

---

## File Structure

- **Create** `tools/gbuffer_reconstruct_check/main.cpp` — proves the reconstruction formula on the host,
  independent of any GPU, before it is ported to GLSL.
- **Modify** `tools/CMakeLists.txt` — register the new check (copy the `curve_check` pattern: an
  `add_executable` + `target_link_libraries(... PRIVATE glm)` block).
- **Modify** `tools/lighting_pass.frag` — add `worldPosFromDepth()`, replace its two `positionTex` reads.
- **Modify** `.github/workflows/ci.yml` — add `gbuffer_reconstruct_check` to both jobs' target lists (it is a
  headless, GPU-free check, so it runs like `userdirs_check` does, not gated behind `--gpu`).
- **Modify** `docs/EngineMasterPlan/PERFORMANCE_AUDIT.md` — mark F5 phase 1 done, correct the stale "v0.8.5 —
  footprint" version label (v0.8.5 shipped as the Linux-support release instead), record the 13-consumer
  follow-up scope from **Not in this plan** above.
- **Modify** `CMakeLists.txt` line 3 — version bump to 0.8.6.
- **Regenerate** (not a source edit) `engine/renderer/gpu/vulkan/lighting_pass_spirv.h` and
  `lighting_pass_nort_spirv.h`, via `tools/regen_lighting_spirv.sh`.

---

### Task 1: Prove the reconstruction formula on the host

**Files:**
- Create: `tools/gbuffer_reconstruct_check/main.cpp`
- Modify: `tools/CMakeLists.txt` (append after the `curve_check` block, tools/CMakeLists.txt:250)

**Interfaces:**
- Produces: `world_pos_from_depth(glm::vec2 uv, float depth_ndc, const glm::mat4& inv_view_proj) -> glm::vec3`
  — a free function local to this check's `main.cpp` (anonymous namespace). Task 2 ports its body verbatim
  into GLSL; the exact formula it proves is the exact formula that goes into the shader.

- [ ] **Step 1: Write the check with the formula already in it, and run it once to see it pass**

  There is no separate "red" state to chase here — this is a mathematical fact being verified, not a
  future behaviour being implemented, so the useful TDD move is different from usual: the very first
  assertion (**deliberately-wrong formula, group 3 below**) is written to fail if the check *cannot tell a
  wrong convention from a right one* — that is the check's own self-test, and it is confirmed as its own
  step (Step 3) before trusting the rest.

  Write `tools/gbuffer_reconstruct_check/main.cpp`:

  ```cpp
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

      std::printf("\n[group] round-trips a known world point through project -> reconstruct\n");
      {
          const glm::vec3 points[] = {
              {0.0f, 0.0f, 0.0f},          // world origin, roughly screen centre
              {1.5f, 0.5f, -0.5f},         // an ordinary scene point
              {0.0f, 0.0f, -99.9f},        // just short of the far plane
              {1.9f, 1.9f, 2.9f},          // just in front of the camera
              {-8.0f, 4.0f, -8.0f},        // toward a screen corner
              {8.0f, -4.0f, 8.0f},
              {5000.0f, 20.0f, -5000.0f},  // far from the origin -- the precision
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
              check(label, close(got, p, eps));
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
  ```

- [ ] **Step 2: Register the check in the build**

  In `tools/CMakeLists.txt`, immediately after the `curve_check` block (ends at line 250 with `endif()`),
  insert:

  ```cmake
  # gbuffer_reconstruct_check: the world-position-from-depth formula used to
  # remove tools/lighting_pass.frag's read of the G-buffer position
  # attachment (F5, phase 1). Proven here, on the host, because no `gws test
  # --gpu` check reads back a rendered pixel (issue #64) -- a wrong depth
  # convention in the shader would otherwise ship silently.
  add_executable(gbuffer_reconstruct_check gbuffer_reconstruct_check/main.cpp)
  target_link_libraries(gbuffer_reconstruct_check PRIVATE glm)
  target_compile_features(gbuffer_reconstruct_check PRIVATE cxx_std_20)
  if(MINGW)
      target_link_options(gbuffer_reconstruct_check PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  ```

  Reconfigure and build:

  ```sh
  cmake --preset windows-debug
  cmake --build build/windows-debug --target gbuffer_reconstruct_check
  ```

  Expected: links and produces `build/windows-debug/bin/gbuffer_reconstruct_check.exe`.

- [ ] **Step 3: Run it and confirm every group passes, including the deliberately-wrong one**

  ```sh
  ./build/windows-debug/bin/gbuffer_reconstruct_check.exe
  ```

  Expected: `PASSED -- 0 failure(s)`, and specifically these two lines both read `[OK  ]` (if either reads
  `[FAIL]`, stop — either the formula or the check itself is wrong, and Task 2 must not proceed):
  ```
  [OK  ] the GL-style [-1,1] remap gives a DIFFERENT point (proves this check can tell the two conventions apart)
  [OK  ] the actual (no-remap) formula gives the right point back
  ```

- [ ] **Step 4: Mutation-test it — prove the check can actually fail**

  This project's convention (see `README.md` § Contributing: "assertions target the failure mode, not the
  happy path") is that a new check must be shown capable of catching a real bug, not just capable of
  passing. Temporarily break the formula and confirm the check goes red, then restore it.

  ```sh
  cd tools/gbuffer_reconstruct_check
  cp main.cpp main.cpp.bak
  ```

  Edit `world_pos_from_depth` in the copy to use the wrong depth remap (the same mistake group 3 checks
  for), by changing:
  ```cpp
      const glm::vec4 clip(ndc.x, ndc.y, depth_ndc, 1.0f);
  ```
  to:
  ```cpp
      const glm::vec4 clip(ndc.x, ndc.y, depth_ndc * 2.0f - 1.0f, 1.0f);
  ```

  Rebuild and run:
  ```sh
  cmake --build ../../build/windows-debug --target gbuffer_reconstruct_check
  ../../build/windows-debug/bin/gbuffer_reconstruct_check.exe
  ```

  Expected: multiple `[FAIL]` lines and a nonzero failure count — most of group 1 and group 2, and the
  "actual (no-remap) formula" line in group 3.

  Restore the correct version and rebuild:
  ```sh
  mv main.cpp.bak main.cpp
  cd ../..
  cmake --build build/windows-debug --target gbuffer_reconstruct_check
  ./build/windows-debug/bin/gbuffer_reconstruct_check.exe
  ```
  Expected: back to `PASSED -- 0 failure(s)`.

- [ ] **Step 5: Commit**

  ```sh
  git add tools/gbuffer_reconstruct_check/main.cpp tools/CMakeLists.txt
  git commit -m "test(perf): prove the G-buffer position-from-depth formula (F5 phase 1)

Host-side check for the reconstruction formula that will replace
tools/lighting_pass.frag's read of the position attachment. Written and
mutation-verified before the shader is touched: no gws check reads back a
rendered pixel (issue #64), so a wrong depth convention would otherwise
ship silently. Confirms Vulkan's [0,1] depth needs no GL-style [-1,1]
remap, and that the check can tell the two conventions apart.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
  ```

---

### Task 2: Apply the proven formula to the lighting pass

**Files:**
- Modify: `tools/lighting_pass.frag:70-72` (new helper, after the push-constant block)
- Modify: `tools/lighting_pass.frag:601` (main()'s primary `worldPos`)
- Modify: `tools/lighting_pass.frag:342-348` (`contactShadow()`'s raymarch sample)
- Regenerate: `engine/renderer/gpu/vulkan/lighting_pass_spirv.h`,
  `engine/renderer/gpu/vulkan/lighting_pass_nort_spirv.h`

**Interfaces:**
- Consumes: the exact formula proven in Task 1 (`world_pos_from_depth`), ported to GLSL as
  `worldPosFromDepth(vec2 uv, float depthNdc)`.
- Produces: `tools/lighting_pass.frag` no longer samples `positionTex` anywhere. The binding
  (`layout(set = 0, binding = 0) uniform sampler2D positionTex;`) is left in place, unused — removing it
  needs a matching C++ descriptor-set-layout change, which is out of scope (see **Not in this plan**).

- [ ] **Step 1: Add the helper function**

  In `tools/lighting_pass.frag`, immediately after the push-constant block's closing `} pc;` (line 70,
  right before `bool flagSet(int bit) { ... }` at line 72), insert:

  ```glsl
  // Reconstructs world-space position from a screen UV and this pixel's
  // Vulkan depth-buffer value. Vulkan's depth range is [0,1] (not OpenGL's
  // [-1,1]), so depthNdc needs no remap -- only the screen UV does.
  // Proven against tools/gbuffer_reconstruct_check; do not "fix" the
  // missing depth remap without reading that check first.
  vec3 worldPosFromDepth(vec2 uv, float depthNdc) {
      vec2 ndc = uv * 2.0 - 1.0;
      vec4 worldH = pc.invViewProj * vec4(ndc, depthNdc, 1.0);
      return worldH.xyz / worldH.w;
  }
  ```

- [ ] **Step 2: Replace the primary sample in `main()`**

  Change (currently line 601):
  ```glsl
      vec3 worldPos = texture(positionTex, inTexCoord).xyz;
  ```
  to:
  ```glsl
      vec3 worldPos = worldPosFromDepth(inTexCoord, gbufferDepth);
  ```

  (`gbufferDepth` is already sampled at line 583 for the sky check, before this line — no new sample added.)

- [ ] **Step 3: Replace the raymarch sample in `contactShadow()`**

  Change (currently lines 342-348):
  ```glsl
          vec4 clip = viewProj * vec4(p, 1.0);
          if (clip.w <= 0.0) break;
          vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
          if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
          vec4 sp = texture(positionTex, uv);
          if (sp.w < 0.5) continue;                       // sky pixel
          float sceneDist  = length(sp.xyz - pc.cameraPos);
  ```
  to:
  ```glsl
          vec4 clip = viewProj * vec4(p, 1.0);
          if (clip.w <= 0.0) break;
          vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
          if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
          float sampleDepth = texture(depthTex, uv).r;
          if (sampleDepth >= 1.0 - 1e-5) continue;        // sky pixel (same test main() uses)
          vec3 scenePos = worldPosFromDepth(uv, sampleDepth);
          float sceneDist  = length(scenePos - pc.cameraPos);
  ```

  (The following line, `float sampleDist = length(p - pc.cameraPos);`, and the occlusion comparison after it
  are unchanged — `sceneDist` is still a world-space distance, just sourced from depth instead of the
  position texture. `sp.w < 0.5` and `sampleDepth >= 1.0 - 1e-5` are the same "nothing was drawn here" test:
  `outPosition = vec4(inWorldPos, 1.0)` in `tools/gbuffer_scene.frag:160` always writes w=1 for real
  geometry, so a cleared pixel — where nothing drew — is the only way `sp.w` reads below 0.5, and it reads
  the depth clear value (1.0) at exactly the same pixels, confirmed by `vulkan_g_buffer.cpp`'s
  `VK_COMPARE_OP_LESS` depth test, which only makes sense with a far-plane clear value of 1.0.)

- [ ] **Step 4: Confirm `positionTex` is now unused, and leave its binding in place**

  ```sh
  grep -n "positionTex" tools/lighting_pass.frag
  ```

  Expected: exactly one line — the declaration at line 9 (`layout(set = 0, binding = 0) uniform sampler2D
  positionTex;`). If any other line still references it, Steps 2 or 3 were not applied correctly.

  Do **not** remove the declaration. The G-buffer still writes this attachment for the other 13 consumers
  (see **Not in this plan**), and the C++ side (`vulkan_lighting_pass.cpp`) still binds it at descriptor set
  0, binding 0 — removing the GLSL declaration without also changing the C++ binding count would be a
  layout mismatch. An unused-but-bound sampler is valid Vulkan and costs nothing at runtime.

- [ ] **Step 5: Regenerate both SPIR-V variants together**

  ```sh
  sh tools/regen_lighting_spirv.sh
  ```

  Expected output: `both lighting variants regenerated`. This overwrites
  `engine/renderer/gpu/vulkan/lighting_pass_spirv.h` and `lighting_pass_nort_spirv.h`. If `glslangValidator`
  reports a compile error, stop — do not hand-edit the generated headers.

  Confirm both headers actually changed (a no-op regen would mean Steps 1-3 did not save, or the script
  found a stale `.spv` from a previous run):
  ```sh
  git diff --stat -- engine/renderer/gpu/vulkan/lighting_pass_spirv.h engine/renderer/gpu/vulkan/lighting_pass_nort_spirv.h
  ```
  Expected: both files show changed line counts (the byte arrays are large; an unchanged count-of-lines with
  changed content is normal SPIR-V churn from the same source recompiling to the same instruction count —
  what matters is that git shows *some* diff, proving the regen picked up the edit).

- [ ] **Step 6: Build the editor and run the full check suite, including GPU checks**

  ```sh
  cmake --build build/windows-debug --target editor
  ./build/windows-debug/bin/gws.exe test --gpu
  ```

  Expected: the same **66 passed, 2 failed** (`shadersource_check`, `terrainmat_check` — both pre-existing
  and unrelated), **0 skipped** as before this change, plus the new `gbuffer_reconstruct_check` now appearing
  in the passed count (67 passed). No check that passed before this change may now fail.

- [ ] **Step 7: Confirm the editor still starts and renders with the new SPIR-V**

  ```sh
  ./build/windows-debug/bin/editor.exe --frames 60
  ```

  Expected exit code 0, and in its output: `✅ Swapchain created`, no new `[error]` or validation-layer
  lines beyond what already appears on an unmodified build (the pre-existing swapchain-semaphore-reuse
  warning from the Linux merge is expected and unrelated — do not treat it as a regression here), and
  `[exit] clean` at the end.

- [ ] **Step 8: Measure — the lighting pass should be no more expensive, ideally less**

  `tests/deferred_scene_test.cpp` already builds a lit scene (cube + tessellated grid, camera at
  `(2, 2, 3)` looking at the origin) through the full render graph and prints per-stage GPU microseconds.
  Run it 5 times, discard the first (cold-cache) run, and note the `lighting=` value's typical range:

  ```sh
  cmake --build build/windows-debug --target deferred_scene_test
  for i in 1 2 3 4 5; do ./build/windows-debug/bin/deferred_scene_test.exe | grep "GPU timings"; done
  ```

  Expected: `=== DEFERRED SCENE TEST PASSED ===` on every run, and the `lighting=` figure not meaningfully
  higher than the pre-change baseline (captured while scoping this plan, on this machine: **20-27
  microseconds** across warm runs, with a ~92 µs first/cold run — expect similar warm-run variance; this is
  a 2-mesh scene so the absolute win from one fewer texture fetch may be small, but it must not regress, and
  the direction should not be worse).

- [ ] **Step 9: Commit**

  ```sh
  git add tools/lighting_pass.frag engine/renderer/gpu/vulkan/lighting_pass_spirv.h engine/renderer/gpu/vulkan/lighting_pass_nort_spirv.h
  git commit -m "perf(F5): stop the lighting pass reading the G-buffer position attachment

Reconstructs world-space position from the already-bound depth texture and
the already-available inverse view-projection matrix instead of sampling a
dedicated position texture, in both places the lighting pass used it: the
primary per-pixel worldPos in main(), and contactShadow()'s raymarch
occlusion test. The lighting pass is 94% of GPU frame time (F0), so this is
the single highest-value of the 14 consumers listed in F5's audit.

No C++ or descriptor-layout change: the positionTex binding stays declared
but unused, since the G-buffer still writes that attachment for the other
13 consumers (audit updated with the remaining scope). Both SPIR-V variants
regenerated together via tools/regen_lighting_spirv.sh.

Formula proven on the host first (gbuffer_reconstruct_check, previous
commit) because no check here reads back a rendered pixel (#64) -- verified
by gws test --gpu (67 passed, the same 2 pre-existing failures), editor
--frames 60, and tests/deferred_scene_test's per-stage GPU timings.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
  ```

---

### Task 3: Wire the new check into CI

**Files:**
- Modify: `.github/workflows/ci.yml` (both `windows-build-and-check` and `linux-build-and-check` jobs)

**Interfaces:**
- Consumes: `gbuffer_reconstruct_check`, registered in Task 1.

- [ ] **Step 1: Add the check to both jobs' target lists**

  `gbuffer_reconstruct_check` needs no GPU (pure host math, like `userdirs_check`), so it belongs in the
  `cmake --build ... --target` list both jobs already build, not behind a `--gpu` flag. In
  `.github/workflows/ci.yml`, find the line:
  ```
              rendersettings_check userdirs_check \
  ```
  (it appears twice — once per job) and change **both** occurrences to:
  ```
              rendersettings_check userdirs_check gbuffer_reconstruct_check \
  ```

- [ ] **Step 2: Validate the YAML and target lists**

  ```sh
  python -c "
  import yaml
  d = yaml.safe_load(open('.github/workflows/ci.yml', encoding='utf-8'))
  for jn, j in d['jobs'].items():
      for st in j['steps']:
          r = st.get('run', '')
          if '--target' in r:
              names = [t for t in r.replace(chr(92), ' ').split() if t.endswith('_check')]
              print(jn, 'gbuffer_reconstruct_check' in names, len(names))
  "
  ```

  Expected: `True` printed for both jobs.

- [ ] **Step 3: Confirm locally, then commit**

  ```sh
  ./build/windows-debug/bin/gws.exe test | grep gbuffer_reconstruct_check
  ```
  Expected: `PASS   gbuffer_reconstruct_check   PASSED -- 0 failure(s)`

  ```sh
  git add .github/workflows/ci.yml
  git commit -m "ci: run gbuffer_reconstruct_check on every push

No GPU needed -- pure host-side math, same as userdirs_check -- so it runs
unconditionally in both CI jobs rather than behind --gpu.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
  ```

---

### Task 4: Update the audit doc, bump the version, and release

**Files:**
- Modify: `docs/EngineMasterPlan/PERFORMANCE_AUDIT.md`
- Modify: `CMakeLists.txt:3`

**Interfaces:**
- Consumes: nothing new — this is documentation and the release step.

- [ ] **Step 1: Mark F5 phase 1 done and correct the stale version plan**

  In `docs/EngineMasterPlan/PERFORMANCE_AUDIT.md`, under `### F5 — The G-buffer is 28 bytes per pixel, 8 of
  them redundant 🟠`, add a status line directly under the heading:

  ```markdown
  **Phase 1 shipped in v0.8.6:** the lighting pass (94% of frame per F0, and the only one of the 14
  consumers that already had both a depth binding and an invViewProj available) no longer reads the
  position attachment — reconstructs from depth instead. `gbuffer_reconstruct_check` proves the formula.
  The attachment itself is not yet removed: the other 13 consumers still read it, and 12 of them have no
  camera matrix bound at all today (only `water.frag` does), so converting them needs new per-pass
  UBO/push-constant plumbing, not just a shader math swap — a materially bigger task, scoped separately.
  ```

  In the `## Work plan` section, under `### v0.8.5 — footprint`, change the heading and add a note (v0.8.5
  shipped as the Linux-support release instead, so this row never happened under that version number):

  ```markdown
  ### ~~v0.8.5~~ v0.8.7+ — footprint (renumbered: v0.8.5 shipped as the Linux-support release instead)
  ```

  Directly below the existing `| # | Item | Done when |` table for that section, add:

  ```markdown
  **F5 is a prerequisite in practice, not just in the recommended order** — the position attachment can
  only be dropped (needed for both F5's own "20 B/px" target and for F10's aliasing to consider a smaller
  set of targets) once all 14 consumers stop reading it. v0.8.6 converted 1 of 14 (see F5 above); the
  remaining 13 are unscoped past "which shaders" and belong in their own plan before this row is started.
  ```

- [ ] **Step 2: Bump the version**

  In `CMakeLists.txt`, line 3:
  ```cmake
      VERSION 0.8.5
  ```
  becomes:
  ```cmake
      VERSION 0.8.6
  ```

- [ ] **Step 3: Rebuild clean and re-run the full suite at the new version**

  ```sh
  cmake --preset windows-debug
  cmake --build build/windows-debug
  ./build/windows-debug/bin/gws.exe version
  ./build/windows-debug/bin/gws.exe test --gpu
  ```

  Expected: `GameWorldshaper engine 0.8.6`, and **67 passed, 2 failed, 0 skipped** (the 2 are the
  pre-existing `shadersource_check`/`terrainmat_check`, unrelated to this change).

- [ ] **Step 4: Commit, tag, and push**

  ```sh
  git add docs/EngineMasterPlan/PERFORMANCE_AUDIT.md CMakeLists.txt
  git commit -m "release: v0.8.6 -- the lighting pass stops reading the G-buffer position attachment

F5 phase 1 (see the two preceding commits): the single most expensive pass
in the frame (94% per F0) no longer samples a dedicated position texture,
reconstructing from depth + invViewProj instead. gws test --gpu: 67
passed, 2 failed (pre-existing, excluded from CI by name), 0 skipped.

Audit doc corrected: v0.8.5 shipped as the Linux-support release, not the
'footprint' (F9/F10) release the plan had pencilled in -- that work is
renumbered and still not started. The remaining 13 of F5's 14 consumers
need per-pass camera-matrix plumbing most of them don't have today and are
scoped as a separate follow-up, not attempted here.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
  git tag -a v0.8.6 -m "v0.8.6 -- lighting pass reconstructs position from depth (F5 phase 1)"
  git fetch origin
  git merge-base --is-ancestor origin/main HEAD && git push origin main || echo "DIVERGED -- do not push, pull and re-check first"
  git push origin v0.8.6
  ```

- [ ] **Step 5: Confirm the release build succeeds and is Hub-installable**

  The user launches everything through the Hub (see project conventions) — a tag push triggers
  `.github/workflows/release-engine.yml`, which builds and uploads `engine-v0.8.6-win64.zip`. Poll for it
  rather than assuming:

  ```sh
  curl -s -o /dev/null -w '%{http_code}\n' -I \
    "https://github.com/orgolis/c-Engine-Game/releases/download/v0.8.6/engine-v0.8.6-win64.zip"
  ```

  Expected eventually: `302` (redirect to the asset). This can take up to ~20 minutes; check every 3-5
  minutes rather than polling tightly. Also confirm CI passed on the tag commit:

  ```sh
  curl -s "https://api.github.com/repos/orgolis/c-Engine-Game/actions/runs?head_sha=$(git rev-parse HEAD)&per_page=5" \
    | python -c "import json,sys; d=json.load(sys.stdin); [print(r['name'], r['status'], r['conclusion']) for r in d.get('workflow_runs', [])]"
  ```

  Expected: `CI` and `Release Engine` both `completed` / `success`.

---

## Self-Review

- **Spec coverage:** F5's done-criterion has four parts — (a) "reconstructed from depth in all 14
  consumers", (b) "G-buffer is 20 B/px", (c) "output visually unchanged", (d) "Geometry and Lighting
  measurably cheaper." This plan delivers 1 of 14 for (a), explicitly does not attempt (b) (needs all 14),
  covers (c) for the one converted pass via the proven-formula check plus a full check-suite and editor
  smoke run (no automated pixel comparison exists yet — issue #64 — so this is the honest limit of what can
  be verified today), and delivers half of (d) (Lighting; Geometry is untouched because the attachment is
  still written). This partial coverage is intentional and stated up front in **Not in this plan** — see
  that section for why converting the rest is a separate, bigger effort.
- **Placeholder scan:** no TBD/TODO, no "add error handling", no "similar to Task N" — every step has
  complete code or an exact command.
- **Type consistency:** `world_pos_from_depth` (Task 1, C++) and `worldPosFromDepth` (Task 2, GLSL) take the
  same three logical arguments (uv, depth, invViewProj — the last one implicit via `pc.invViewProj` in GLSL
  since it is already in scope) in the same order, and both return the reconstructed world position by
  value. Task 1's version is never called from Task 2's code — Task 2 restates its body directly in GLSL, as
  documented in Task 1's Interfaces block ("Task 2 ports its body verbatim").
