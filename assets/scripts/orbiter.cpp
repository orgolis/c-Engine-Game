// orbiter.cpp — native C++ script: orbits this entity around its start
// position and cycles its color. Attach via Inspector -> Add Component ->
// Script, path: assets/scripts/orbiter.cpp (compiled live by g++; edits
// hot-reload during Play).
#include "schizo_script.h"
#include <cmath>

// File-scope state — REPLACED on every hot reload (the DLL is swapped).
static float g_cx, g_cy, g_cz;   // orbit center (captured at start)
static float g_angle = 0.0f;

SCHIZO_SCRIPT void on_start(const SchizoScriptApi* api, unsigned e) {
    float p[3] = {0, 0, 0};
    api->get_position(api->ctx, e, p);
    g_cx = p[0]; g_cy = p[1]; g_cz = p[2];
    g_angle = 0.0f;
    api->log(api->ctx, "orbiter (C++) started");
}

SCHIZO_SCRIPT void on_update(const SchizoScriptApi* api, unsigned e, float dt) {
    g_angle += dt * 1.5f;   // rad/s
    const float r = 2.5f;
    const float p[3] = {g_cx + r * std::cos(g_angle),
                        g_cy,
                        g_cz + r * std::sin(g_angle)};
    api->set_position(api->ctx, e, p);

    const float c[4] = {0.5f + 0.5f * std::sin(g_angle),
                        0.5f + 0.5f * std::sin(g_angle + 2.1f),
                        0.5f + 0.5f * std::sin(g_angle + 4.2f), 1.0f};
    api->set_color(api->ctx, e, c);
}
