#pragma once
// ============================================================================
// scene_components — the ECS half of the scene's component set (3.6).
//
// The editor's OOP scene has thirteen kinds of component. Before this, FOUR of
// them reached the ECS: Transform, LocalToWorld, MeshRenderer and WorldFlags.
// Everything else -- lights, colliders, cameras, terrain, water, audio,
// scripts, skinned meshes, NPC agents, particle emitters -- existed only on the
// OOP side, so an ECS system could not see a light, could not tell whether an
// entity had a collider, and could not read a camera's FOV. That is what "the
// ECS is a shadow" meant in practice, and it is why gameplay had to be written
// against the editor's object model.
//
// These are the counterparts that did not exist. Some ECS types were already
// declared and simply never populated (LightComponent, Collider, AudioSource);
// those live in components.h and are filled in by the bridge rather than
// redefined here.
//
// POD, public fields, no accessors: offset reflection needs standard layout,
// and every one of these has to survive save/load and the network snapshot.
// Enums are stored as uint32_t for the same reason -- a reflected field's type
// has to be one the serializer knows.
// ============================================================================

#include <glm/glm.hpp>

#include <cstdint>

namespace schizo::ecs {

// ---- Camera ---------------------------------------------------------------
// Position and orientation come from the entity's Transform; only the lens
// lives here. A camera that carried its own position would be a second source
// of truth for something Transform already owns.
struct CameraComponent {
    uint32_t projection    = 0;      // 0 perspective, 1 orthographic
    float    fov           = 60.0f;  // degrees, perspective only
    float    ortho_size    = 10.0f;  // half-height, orthographic only
    float    near_plane    = 0.1f;
    float    far_plane     = 1000.0f;
    uint32_t flags         = 1;      // bit0 active
};

// ---- Water ----------------------------------------------------------------
struct WaterComponent {
    float    wave_height  = 0.2f;
    float    wave_speed   = 1.0f;
    float    wave_scale   = 1.0f;
    float    clarity      = 0.5f;
    float    reflectivity = 0.5f;
    // Physical water drives buoyancy and swimming; visual water only draws.
    // The distinction is load-bearing -- a visual-only surface an actor tries
    // to swim in is a fall through the world.
    uint32_t flags        = 0;       // bit0 physical
};

// ---- Terrain --------------------------------------------------------------
// The heightfield itself stays on the OOP side: it is megabytes of data with
// its own editing tools, and copying it per frame would cost more than the
// whole bridge. This is the description a gameplay system needs.
struct TerrainComponent {
    float    size          = 100.0f;
    float    height_scale  = 10.0f;
    float    water_level   = 0.0f;
    uint32_t resolution    = 0;
    uint32_t flags         = 0;      // bit0 has_water
};

// ---- Script ---------------------------------------------------------------
// The path is an asset id rather than a string: reflected fields must be
// trivially copyable, and a std::string here would silently drop out of both
// the save file and the network snapshot -- which is exactly the failure this
// phase kept finding.
struct ScriptComponent {
    uint64_t script_asset = 0;
    uint32_t flags        = 1;       // bit0 enabled
};

// ---- Particle emitter -----------------------------------------------------
struct ParticleEmitter {
    float     spawn_rate    = 20.0f;
    uint32_t  max_particles = 1024;
    glm::vec4 color_start{1.0f, 0.6f, 0.15f, 1.0f};
    glm::vec4 color_end{0.8f, 0.1f, 0.0f, 0.0f};
    uint32_t  flags         = 0;     // bit0 enabled, bit1 emitting
};

// ---- NPC agent ------------------------------------------------------------
struct NpcAgent {
    float    sight_range   = 20.0f;
    float    sight_fov_deg = 90.0f;
    float    move_speed    = 3.5f;
    float    patrol_radius = 8.0f;
    float    attack_range  = 0.0f;
    int32_t  ability_index = -1;
    uint32_t flags         = 0;      // bit0 enabled
};

// ---- Skinned mesh ---------------------------------------------------------
// Distinct from Skeleton/AnimationState, which describe the rig and the clip
// currently playing. This is the authoring-level choice: which clips stand for
// idle and move, and how fast the character has to be going to count as moving.
struct SkinnedMesh {
    uint64_t mesh_asset      = 0;
    int32_t  clip_index      = 0;
    int32_t  idle_clip       = 0;
    int32_t  move_clip       = 0;
    float    speed           = 1.0f;
    float    move_threshold  = 0.15f;
    uint32_t flags           = 1;    // bit0 playing, bit1 drive_from_motion
};

// ---- flag helpers ---------------------------------------------------------
// Bit fiddling at every call site is where these go wrong: bit1 read as bit0
// is a component that looks populated and behaves as though it were not.
inline bool     flag_get(uint32_t flags, uint32_t bit) { return (flags & (1u << bit)) != 0u; }
inline uint32_t flag_set(uint32_t flags, uint32_t bit, bool on) {
    return on ? (flags | (1u << bit)) : (flags & ~(1u << bit));
}

}  // namespace schizo::ecs
