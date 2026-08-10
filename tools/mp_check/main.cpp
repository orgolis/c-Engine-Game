// ====================
// mp_check — editor NetSession integration test (Stage 7 multiplayer)
// ====================
//
// Drives two live NetSession instances over the loopback Transport, verifying
// the first-person model end to end:
//   1. Player avatars: each side reports its player position; each renders the
//      OTHER player (never itself) as a proxy CLONED from the scene "Player"'s
//      visual tree (root + "PlayerMesh" child), positions tracking.
//   2. Host-authoritative props: a dynamic-collider "Ball" moved on the host is
//      mirrored onto the client's matching entity; the client's own player
//      entity is never overwritten.

#include "net_session.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "entity_factory.h"
#include "collider_component.h"
#include "mesh_renderer_component.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace schizo;

static bool approx(const glm::vec3& a, const glm::vec3& b, float eps = 1e-2f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

// Build the identical test scene both sides load: a Player + a dynamic Ball.
static std::shared_ptr<scene::Scene> make_scene(const char* name) {
    auto s = std::make_shared<scene::Scene>(name);
    scene::EntityFactory::CreatePlayer(s, "Player", glm::vec3(0.0f, 1.0f, 0.0f));
    auto ball = scene::EntityFactory::CreateSphere(s, "Ball", 0.5f,
                                                   glm::vec4(0.9f, 0.9f, 0.2f, 1.0f));
    if (ball) {
        auto col = ball->GetComponent<scene::ColliderComponent>();
        if (!col) col = ball->AddComponent<scene::ColliderComponent>(scene::ColliderShape::Sphere);
        if (col) col->SetDynamic(true);
        ball->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 3.0f, 0.0f));
    }
    return s;
}

static void pump(editor::NetSession& host, editor::NetSession& client,
                 const std::shared_ptr<scene::Scene>& hs,
                 const std::shared_ptr<scene::Scene>& cs,
                 uint32_t h_pid, const glm::vec3& host_pos,
                 uint32_t c_pid, const glm::vec3& cli_pos, int iters) {
    for (int i = 0; i < iters; ++i) {
        host.set_local_player(h_pid, host_pos);
        client.set_local_player(c_pid, cli_pos);
        host.tick_frame(hs, 0.016f);
        client.tick_frame(cs, 0.016f);
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("mp_check: NetSession first-person avatars + host-authoritative props\n");

    auto hs = make_scene("host");
    auto cs = make_scene("client");
    auto h_player = hs->GetEntityByName("Player");
    auto c_player = cs->GetEntityByName("Player");
    auto h_ball   = hs->GetEntityByName("Ball");
    auto c_ball   = cs->GetEntityByName("Ball");
    if (!h_player || !c_player || !h_ball || !c_ball) { std::printf("  FAIL: scene setup\n"); return 1; }

    editor::NetSession host, client;
    if (!host.host(17801))                { std::printf("  FAIL: host.host\n");   return 1; }
    if (!client.join("127.0.0.1", 17801)) { std::printf("  FAIL: client.join\n"); return 1; }

    // --- avatars ---
    const glm::vec3 hp(5.0f, 1.0f, 5.0f), cp(-3.0f, 1.0f, -3.0f);
    pump(host, client, hs, cs, h_player->GetId(), hp, c_player->GetId(), cp, 300);

    auto host_on_client = cs->GetEntityByName("[net] Player 0");
    auto cli_on_host    = hs->GetEntityByName("[net] Player 1");
    const bool ok_h = host_on_client && approx(host_on_client->GetTransform()->GetWorldPosition(), hp);
    const bool ok_c = cli_on_host && approx(cli_on_host->GetTransform()->GetWorldPosition(), cp);
    const bool no_self = cs->GetEntityByName("[net] Player 1") == nullptr;
    // Player-model clone: the proxy must carry the cloned "PlayerMesh" child
    // with the capsule MeshRenderer (not be a bare cube).
    auto clone_child = cs->GetEntityByName("[net] Player 0:PlayerMesh");
    bool clone_ok = false;
    if (clone_child) {
        auto mr = clone_child->GetComponent<scene::MeshRendererComponent>();
        clone_ok = mr && mr->GetMeshType() == scene::MeshType::Capsule;
    }
    std::printf("  avatars: host-on-client %s | client-on-host %s | no-self %s | player-model clone %s\n",
                ok_h ? "OK" : "FAIL", ok_c ? "OK" : "FAIL",
                no_self ? "OK" : "FAIL", clone_ok ? "OK" : "FAIL");

    // --- host-authoritative props ---
    const glm::vec3 ball_target(4.0f, 0.5f, -6.0f);
    h_ball->GetTransform()->SetWorldPosition(ball_target);
    // Keep re-asserting the host ball position (as the host's physics would).
    for (int i = 0; i < 200; ++i) {
        h_ball->GetTransform()->SetWorldPosition(ball_target);
        host.set_local_player(h_player->GetId(), hp);
        client.set_local_player(c_player->GetId(), cp);
        host.tick_frame(hs, 0.016f);
        client.tick_frame(cs, 0.016f);
    }
    const glm::vec3 got = c_ball->GetTransform()->GetWorldPosition();
    const bool prop_ok = approx(got, ball_target);
    // The client's own player must NOT have been dragged to the host's player.
    const bool player_safe = approx(c_player->GetTransform()->GetWorldPosition(),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
    std::printf("  props: client ball (%.2f,%.2f,%.2f) vs (%.2f,%.2f,%.2f) -> %s | own player untouched %s\n",
                got.x, got.y, got.z, ball_target.x, ball_target.y, ball_target.z,
                prop_ok ? "OK" : "FAIL", player_safe ? "OK" : "FAIL");

    const bool assigned = client.status().my_avatar == 1'000'001ull;

    // --- session restart: shutdown + re-host must leave NO ghosts ---
    // Regression for the net_world leak: stale avatar/prop entities surviving
    // shutdown were re-broadcast by the next session, resurrecting proxies of
    // the previous session's players and corrupting the new sync.
    host.shutdown();
    client.shutdown();
    if (!host.host(17802))                { std::printf("  FAIL: re-host\n"); return 1; }
    if (!client.join("127.0.0.1", 17802)) { std::printf("  FAIL: re-join\n"); return 1; }
    const glm::vec3 hp2(-8.0f, 1.0f, 4.0f), cp2(6.0f, 1.0f, 6.0f);
    const glm::vec3 ball2(-2.0f, 0.5f, 8.0f);
    for (int i = 0; i < 300; ++i) {
        h_ball->GetTransform()->SetWorldPosition(ball2);
        host.set_local_player(h_player->GetId(), hp2);
        client.set_local_player(c_player->GetId(), cp2);
        host.tick_frame(hs, 0.016f);
        client.tick_frame(cs, 0.016f);
    }
    // Exactly ONE root proxy per side (the other player), at the NEW position.
    auto count_root_proxies = [](const std::shared_ptr<scene::Scene>& s) {
        size_t n = 0;
        for (const auto& e : s->GetEntities())
            if (e && e->GetName().rfind("[net] Player", 0) == 0 &&
                e->GetName().find(':') == std::string::npos)
                ++n;
        return n;
    };
    const size_t cli_proxies  = count_root_proxies(cs);
    const size_t host_proxies = count_root_proxies(hs);
    auto h2_on_c = cs->GetEntityByName("[net] Player 0");
    const bool r_pos  = h2_on_c && approx(h2_on_c->GetTransform()->GetWorldPosition(), hp2);
    const bool r_ball = approx(c_ball->GetTransform()->GetWorldPosition(), ball2);
    const bool restart_ok = (cli_proxies == 1) && (host_proxies == 1) && r_pos && r_ball;
    std::printf("  restart: proxies client=%zu host=%zu (want 1/1) | new pos %s | new ball %s -> %s\n",
                cli_proxies, host_proxies, r_pos ? "OK" : "FAIL",
                r_ball ? "OK" : "FAIL", restart_ok ? "OK" : "FAIL");

    // --- peers with DIFFERENT floating origins still agree ---
    // Replicated positions are absolute, and every peer rebases on its own
    // camera -- so once one client streams into a distant part of the map, the
    // two are not in the same coordinate frame at all. Remote players then
    // appear displaced by the difference between the origins, which grows over
    // a session and is worst exactly when players are far apart and least able
    // to work out what is wrong. The wire frame is the un-rebased one.
    //
    // The client rebases; the host does not. Each must still see the other
    // where it actually is, expressed in its OWN frame.
    const glm::vec3 kShift(-512.0f, 0.0f, 256.0f);
    client.set_origin_offset(kShift);

    const glm::vec3 hp3(-3.0f, 1.0f, 9.0f);            // host frame
    const glm::vec3 cp3 = glm::vec3(7.0f, 1.0f, -2.0f) + kShift;   // client frame
    for (int i = 0; i < 200; ++i) {
        host.set_local_player(h_player->GetId(), hp3);
        client.set_local_player(c_player->GetId(), cp3);
        host.tick_frame(hs, 0.016f);
        client.tick_frame(cs, 0.016f);
    }

    auto h_on_c = cs->GetEntityByName("[net] Player 0");   // host, seen by client
    auto c_on_h = hs->GetEntityByName("[net] Player 1");   // client, seen by host

    // The client is shifted, so it should see the host at hp3 + kShift.
    const bool see_host = h_on_c &&
        approx(h_on_c->GetTransform()->GetWorldPosition(), hp3 + kShift);
    // The host is not shifted, so it should see the client at cp3 - kShift.
    const bool see_client = c_on_h &&
        approx(c_on_h->GetTransform()->GetWorldPosition(), cp3 - kShift);

    // And the ghost bodies fed to Jolt must be in the LOCAL frame too, or
    // remote players would collide with empty air half a kilometre away.
    std::vector<glm::vec3> ghosts;
    client.remote_player_positions(ghosts);
    const bool ghost_ok = ghosts.size() == 1 && approx(ghosts[0], hp3 + kShift);

    const bool origin_ok = see_host && see_client && ghost_ok;
    if (h_on_c) {
        const glm::vec3 g = h_on_c->GetTransform()->GetWorldPosition();
        std::printf("  origins: client sees host at (%.1f,%.1f,%.1f) want (%.1f,%.1f,%.1f) -> %s",
                    g.x, g.y, g.z, (hp3 + kShift).x, (hp3 + kShift).y, (hp3 + kShift).z,
                    see_host ? "OK" : "FAIL");
    }
    std::printf(" | host sees client %s | jolt ghosts %s -> %s\n",
                see_client ? "OK" : "FAIL", ghost_ok ? "OK" : "FAIL",
                origin_ok ? "OK" : "FAIL");

    client.set_origin_offset(glm::vec3(0.0f));


    host.shutdown();
    client.shutdown();

    const bool ok = ok_h && ok_c && no_self && clone_ok && prop_ok && player_safe &&
                    assigned && restart_ok && origin_ok;
    std::printf("mp_check: %s\n", ok ? "ALL OK" : "FAIL");
    return ok ? 0 : 1;
}
