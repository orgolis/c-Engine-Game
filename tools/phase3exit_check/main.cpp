// ============================================================================
// phase3exit_check — Phase 3's exit criterion, run rather than asserted.
//
//   "no verified library is unreferenced by a running scene — an animated
//    character walks a streamed world with an AI agent pathing on baked
//    geometry."
//
// Every clause of that has its own passing check already: streaming, navmesh
// baking, agents, locomotion, the rebase contract. What none of them cover is
// the sentence as a whole, and this phase's recurring lesson is that the bugs
// live between the parts, not inside them. Closing the phase on five green
// checks that never ran together would be closing it on inference.
//
// So this drives one scene through all of it at once: terrain and colliders in,
// navmesh baked from that actual geometry, an agent walking it, locomotion
// driven by the agent's real motion, streaming following a moving camera until
// it forces an origin rebase — and then everything has to keep working.
//
// It is deliberately an INTEGRATION test and asserts end-to-end outcomes. Where
// it overlaps a unit check, the unit check is the better failure message; this
// one exists to catch what neither of them can see alone.
// ============================================================================
#include "locomotion_clip.h"
#include "nav_bake.h"
#include "npc_agents.h"
#include "world_streaming.h"

#include "entity.h"
#include "npc_agent_component.h"
#include "scene.h"
#include "terrain_component.h"
#include "transform.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

float dist_xz(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

int main() {
    std::printf("=== phase3exit_check ===\n\n");
    std::printf("  Phase 3 exit: an animated character walks a streamed world\n"
                "  with an AI agent pathing on baked geometry.\n\n");

    // ---- the world ----------------------------------------------------------
    auto scn = std::make_shared<scene::Scene>("phase3");

    auto ground = scn->CreateEntity("Ground");
    auto terrain = ground->AddComponent<scene::TerrainComponent>();
    terrain->Resize(32, 320.0f);

    auto guard = scn->CreateEntity("Guard");
    guard->GetTransform()->SetLocalPosition({0.0f, 0.0f, 0.0f});
    auto* ag = guard->GetNpcAgentComponent();
    ag->enabled     = true;
    ag->target_name = "Hero";
    ag->sight_range = 60.0f;
    ag->move_speed  = 6.0f;

    auto hero = scn->CreateEntity("Hero");
    hero->GetTransform()->SetLocalPosition({140.0f, 0.0f, 140.0f});   // out of sight: guard patrols

    // ---- 1. the navmesh comes from the scene, not from a demo grid ----------
    ai::NavMesh nav;
    const editor::NavBakeStats stats = editor::bake_navmesh_from_scene(scn, nav);
    check(!nav.empty() && stats.triangles > 0,
          "a navmesh bakes from the scene's real terrain");
    check(nav.contains(glm::vec3(0.0f, 0.0f, 0.0f)),
          "and covers the ground the agent is standing on");

    // ---- 2. the agent walks it ---------------------------------------------
    editor::EditorNpcAgents agents;
    editor::LocomotionState loco;

    const glm::vec3 start = guard->GetTransform()->GetLocalPosition();
    bool ever_moved = false;
    for (int i = 0; i < 180; ++i) {
        agents.update(scn, nav, 1.0f / 60.0f);
        if (editor::locomotion_wants_move(loco, guard->GetTransform()->GetLocalPosition(),
                                          1.0f / 60.0f, 0.15f))
            ever_moved = true;
    }
    const glm::vec3 walked = guard->GetTransform()->GetLocalPosition();

    check(agents.agent_count() == 1, "the agent registered itself from the scene");
    check(dist_xz(walked, start) > 1.0f, "and walked somewhere");
    check(nav.contains(walked), "staying on the baked navmesh while doing it");
    check(ever_moved,
          "and its ANIMATION follows its real motion — locomotion asked for the "
          "move clip because the character actually moved, not because a flag "
          "was set");

    // ---- 3. the world streams around it ------------------------------------
    // The camera flies far enough to force cells out and an origin rebase in.
    // Streaming is the one part that cannot be provoked in a single frame.
    editor::EditorWorldStreaming streaming;
    streaming.set_enabled(true);

    glm::vec3 camera(0.0f, 20.0f, 0.0f);
    glm::vec3 total_shift(0.0f);
    for (int i = 0; i < 400; ++i) {
        camera.x += 4.0f;
        camera.z += 2.0f;
        const glm::vec3 shift = streaming.update(scn, camera);
        if (shift != glm::vec3(0.0f)) {
            camera += shift;
            total_shift += shift;
            // The editor's rebase order, and the contract every system here
            // has to honour.
            nav.translate(shift);
            agents.apply_origin_shift(shift);
            editor::locomotion_apply_origin_shift(loco, shift);
        }
        agents.update(scn, nav, 1.0f / 60.0f);
    }

    check(streaming.active_cells() > 0, "streaming has live cells around the camera");
    check(streaming.rebase_count() > 0,
          "and the camera travelled far enough to force an origin rebase");

    // The camera flew away from the guard, so streaming UNLOADED it -- which is
    // correct, and was my first failure here: an unloaded agent stops walking
    // because it is not in the world any more, not because the rebase broke it.
    // Bring the camera back, the way a player who wandered off and returned
    // would, and the guard has to come back with it.
    check(streaming.streamed_out() > 0,
          "streaming actually unloaded entities the camera left behind");
    check(!guard->IsActive(),
          "including the guard, which is why it stopped walking");

    for (int i = 0; i < 30; ++i)
        streaming.update(scn, guard->GetTransform()->GetWorldPosition() + glm::vec3(0, 20, 0));
    check(guard->IsActive(), "and coming back re-activates it");

    // ---- 4. THE WHOLE SENTENCE, after all of that ---------------------------
    // This is the assertion the phase actually turns on: not that each piece
    // works, but that they still work together once the world has moved under
    // them.
    {
        const glm::vec3 before = guard->GetTransform()->GetLocalPosition();
        bool moving_after = false;
        for (int i = 0; i < 120; ++i) {
            agents.update(scn, nav, 1.0f / 60.0f);
            if (editor::locomotion_wants_move(loco, guard->GetTransform()->GetLocalPosition(),
                                              1.0f / 60.0f, 0.15f))
                moving_after = true;
        }
        const glm::vec3 after = guard->GetTransform()->GetLocalPosition();

        check(dist_xz(after, before) > 0.5f,
              "the agent is STILL walking after the world rebased under it");
        check(nav.contains(after),
              "still on the navmesh, which moved with the world");
        check(moving_after,
              "and still animating from real motion");
        check(total_shift != glm::vec3(0.0f),
              "having survived a rebase of real magnitude, not a no-op");
    }

    std::printf("\n  navmesh: %zu tris (%d shapes skipped) | cells: %zu active | "
                "rebases: %zu | shift: (%.0f, %.0f)\n",
                nav.tri_count(), static_cast<int>(stats.skipped_shapes), streaming.active_cells(),
                streaming.rebase_count(), total_shift.x, total_shift.z);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL phase3exit_check\n");
    return g_fail ? 1 : 0;
}
