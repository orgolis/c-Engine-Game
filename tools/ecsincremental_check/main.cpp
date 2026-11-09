// ============================================================================
// ecsincremental_check — the ECS mirror's incremental transforms must give the
// same world matrices as recomputing everything.
//
// When a sync sees the same entities with the same parents as the last one, it
// recomputes only the transforms stamped since the last sync (Transform::
// ChangeStamp) and propagates "world changed" from parent to child; every other
// entity keeps last sync's matrix. The failure this guards against is silent:
// an object that moved, or whose ANCESTOR moved, keeps its old matrix and
// simply stops following on screen.
//
// A forest of ~3000 entities (enough for the job-system path the editor takes
// at scale), up to five levels deep, through 300 frames of random edits: move,
// rotate and scale roots, middles and leaves; re-parent (no list change);
// deactivate and reactivate (list change); frames where nothing changes. After
// every sync, every active entity's mirrored matrix must equal the scene's own.
// It also checks that the incremental path actually ran, and that a still
// frame writes no matrices.
// ============================================================================
#include "ecs_bridge.h"

#include "entity.h"
#include "jobs/job_system.h"
#include "scene.h"
#include "transform.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool same(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-3f * std::max(1.0f, std::fabs(b[c][r]))) return false;
    return true;
}

// Every active entity's mirrored world matrix equals the scene's.
size_t mismatches(const editor::EcsSceneBridge& br,
                  const std::vector<std::shared_ptr<scene::Entity>>& ents) {
    size_t bad = 0;
    for (const auto& e : ents) {
        if (!e->IsActiveInHierarchy()) continue;
        const glm::mat4* m = br.world_matrix(e->GetTransform());
        if (!m || !same(*m, e->GetTransform()->GetWorldMatrix())) ++bad;
    }
    return bad;
}

}  // namespace

int main() {
    std::printf("=== ecsincremental_check ===\n\n");
    gws::jobs::JobSystem::instance().init();

    std::mt19937 rng(777);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);

    auto scn = std::make_shared<scene::Scene>("incremental");
    std::vector<std::shared_ptr<scene::Entity>> ents;
    std::vector<int> depth;
    for (int i = 0; i < 3000; ++i) {
        auto e = scn->CreateEntity("E" + std::to_string(i));
        e->GetTransform()->SetLocalPosition(glm::vec3(u(rng), u(rng), u(rng)) * 20.0f);
        int d = 0;
        // ~40% are children of an earlier entity that is at most 4 deep.
        if (i > 10 && rng() % 10 < 4) {
            for (int tries = 0; tries < 8; ++tries) {
                const size_t p = rng() % ents.size();
                if (depth[p] < 4) { ents[p]->AddChild(e); d = depth[p] + 1; break; }
            }
        }
        ents.push_back(e);
        depth.push_back(d);
    }

    editor::EcsSceneBridge br;
    br.sync_and_run(scn);
    check(mismatches(br, ents) == 0, "first sync (full path): every matrix matches");

    br.sync_and_run(scn);
    check(br.matrices_written() == 0, "a still frame writes no matrices");
    check(mismatches(br, ents) == 0, "and every matrix still matches");

    size_t frames = 0, bad_frames = 0, incremental_frames = 0, worst = 0;
    for (int frame = 0; frame < 300; ++frame) {
        const int kind = frame % 10;
        if (kind < 6) {
            // A handful of TRS edits at random depths.
            const int edits = 1 + static_cast<int>(rng() % 20);
            for (int k = 0; k < edits; ++k) {
                auto* tf = ents[rng() % ents.size()]->GetTransform();
                switch (rng() % 3) {
                case 0: tf->SetLocalPosition(tf->GetLocalPosition() + glm::vec3(u(rng), u(rng), u(rng))); break;
                case 1: tf->SetLocalRotation(glm::angleAxis(u(rng) * 3.0f, glm::normalize(glm::vec3(u(rng), 1.0f, u(rng))))); break;
                default: tf->SetLocalScale(glm::vec3(1.0f + 0.5f * u(rng))); break;
                }
            }
        } else if (kind == 6) {
            // Re-parent one entity under a shallow one (or to the root): the
            // list does not change, only a parent link.
            const size_t c = rng() % ents.size();
            const size_t p = rng() % ents.size();
            auto* ctf = ents[c]->GetTransform();
            bool would_cycle = false;
            for (auto* t = ents[p]->GetTransform(); t; t = t->GetParent())
                if (t == ctf) { would_cycle = true; break; }
            if (!would_cycle && p != c && rng() % 2) ents[p]->AddChild(ents[c]);
            else                                      ctf->SetParent(nullptr);
        } else if (kind == 7) {
            // Deactivate one, or reactivate it: the active list changes.
            auto& e = ents[rng() % ents.size()];
            e->SetActive(!e->IsActive());
        }
        // kind 8, 9: nothing changes this frame.
        br.sync_and_run(scn);
        ++frames;
        if (br.matrices_written() < br.entity_count()) ++incremental_frames;
        const size_t bad = mismatches(br, ents);
        if (bad) { ++bad_frames; worst = std::max(worst, bad); }
    }
    check(bad_frames == 0, "300 frames of random edits: every matrix matches every frame (" +
                           std::to_string(bad_frames) + " bad frames, worst " + std::to_string(worst) + ")");
    check(incremental_frames > frames / 2,
          "the incremental path actually ran (" + std::to_string(incremental_frames) + " of " +
          std::to_string(frames) + " frames wrote fewer matrices than entities)");

    std::printf("\nFrames where the entity LIST changes stay incremental and exact:\n");
    {
        // Every frame: (de)activate a few, move a few, sometimes remove one
        // and create one -- the reconcile walks the changed list against the
        // last one, and the matrices are re-aligned from the slots.
        size_t lf = 0, lbad = 0, lincr = 0, gone_answered = 0;
        std::vector<std::shared_ptr<scene::Entity>> removed;
        for (int frame = 0; frame < 150; ++frame) {
            for (int k = 0; k < 4; ++k) {
                auto& e = ents[rng() % ents.size()];
                e->SetActive(!e->IsActive());
            }
            for (int k = 0; k < 6; ++k) {
                auto* tf = ents[rng() % ents.size()]->GetTransform();
                tf->SetLocalPosition(tf->GetLocalPosition() + glm::vec3(u(rng), u(rng), u(rng)));
            }
            if (frame % 5 == 0) {
                // Remove a childless one (keeps `ents` honest) and add a new one.
                for (int tries = 0; tries < 20; ++tries) {
                    const size_t r = rng() % ents.size();
                    if (!ents[r]->GetChildren().empty()) continue;
                    removed.push_back(ents[r]);
                    scn->RemoveEntity(ents[r]);
                    ents.erase(ents.begin() + static_cast<long>(r));
                    break;
                }
                auto n = scn->CreateEntity("N" + std::to_string(frame));
                n->GetTransform()->SetLocalPosition(glm::vec3(u(rng), u(rng), u(rng)) * 10.0f);
                if (rng() % 2) ents[rng() % ents.size()]->AddChild(n);
                ents.push_back(n);
            }
            br.sync_and_run(scn);
            ++lf;
            if (br.matrices_written() < br.entity_count()) ++lincr;
            if (mismatches(br, ents)) ++lbad;
            for (const auto& r : removed)
                if (br.world_matrix(r->GetTransform()) != nullptr) ++gone_answered;
        }
        check(lbad == 0, "150 list-changing frames: every matrix matches every frame (" +
                         std::to_string(lbad) + " bad)");
        check(lincr > lf * 3 / 4, "and they stayed incremental (" + std::to_string(lincr) + " of " +
                                  std::to_string(lf) + " wrote fewer matrices than entities)");
        check(gone_answered == 0, "a removed entity is never answered for again");
    }

    std::printf("\nA deep move propagates to every descendant:\n");
    {
        // Root -> 4 levels; move only the root, nothing else stamped.
        auto r = scn->CreateEntity("R");
        std::vector<std::shared_ptr<scene::Entity>> line{r};
        for (int i = 0; i < 4; ++i) {
            auto c = scn->CreateEntity("L" + std::to_string(i));
            line.back()->AddChild(c);
            c->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
            line.push_back(c);
        }
        br.sync_and_run(scn);                    // list changed: full
        br.sync_and_run(scn);                    // still
        r->GetTransform()->SetLocalPosition(glm::vec3(50.0f, 0.0f, 0.0f));
        br.sync_and_run(scn);
        check(br.matrices_written() == line.size(),
              "exactly the root and its four descendants are rewritten (" +
              std::to_string(br.matrices_written()) + ")");
        check(mismatches(br, line) == 0, "and all five match the scene");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL ecsincremental_check\n");
    gws::jobs::JobSystem::instance().shutdown();
    return g_fail ? 1 : 0;
}
