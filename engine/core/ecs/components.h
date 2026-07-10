#pragma once

// Core POD components (Master Plan Stage 1.2). Plain structs (no virtuals)
// registered with reflection so the editor inspector + serializer pick them up
// automatically. Every field is a scalar / fixed array / glm type, so each
// component is trivially copyable and serializes via the POD field path (no
// container traits needed). Components for not-yet-built stages (physics,
// animation, audio, gameplay) are defined here as POD now — so the ECS, the
// serializer and the inspector already know them — and get fleshed out (and
// wired to their backends) when their stage lands.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

#include "reflection/reflection.h"

namespace schizo::ecs {

// ---- Spatial ----
// Local transform (authoring). LocalToWorld is the computed world matrix.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};
struct LocalToWorld {
    glm::mat4 value{1.0f};
};
struct Velocity {
    glm::vec3 linear{0.0f};
    glm::vec3 angular{0.0f};
};
// Fixed-buffer name (POD, so it stays trivially reflectable/serializable).
struct Name {
    char value[64] = {0};
};

// ---- Rendering (Stage 1/3) ----
struct MeshRenderer {
    uint64_t mesh      = 0;    // cooked-mesh AssetId (0 = use `primitive`)
    uint32_t primitive = 0;    // primitive type when mesh == 0
    int32_t  material  = -1;   // index into a Material set, -1 = default
    uint32_t flags     = 1;    // bit0 cast_shadow, bit1 double_sided, bit2 hidden
    uint32_t lod_bias  = 0;
};
struct Material {
    glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float     metallic     = 0.0f;
    float     roughness    = 1.0f;
    float     emissive     = 0.0f;
    uint64_t  albedo_tex   = 0;   // texture AssetId, 0 = none
    uint32_t  alpha_mode   = 0;   // 0 opaque, 1 cutout, 2 blend
    float     alpha_cutoff = 0.5f;
};
struct LightComponent {
    uint32_t  type      = 0;    // 0 directional, 1 point, 2 spot
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    float     range     = 10.0f;
    float     spot_inner = 0.0f;
    float     spot_outer = 0.7f;
    uint32_t  flags     = 1;    // bit0 casts_shadow
};

// ---- Physics (Stage 4 — POD now, bound to Jolt later) ----
struct RigidBody {
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};
    float     mass        = 1.0f;
    float     restitution = 0.2f;
    float     friction    = 0.5f;
    uint32_t  flags       = 0;   // bit0 kinematic, bit1 gravity
    uint64_t  body_handle = 0;   // backend handle, assigned at runtime
};
struct Collider {
    uint32_t  shape       = 0;   // 0 box, 1 sphere, 2 capsule, 3 convex, 4 mesh
    glm::vec3 half_extents{0.5f, 0.5f, 0.5f};
    float     radius      = 0.5f;
    float     height      = 1.0f;
    uint32_t  layer       = 0;
};

// ---- Animation (Stage 5 — POD now) ----
struct Skeleton {
    uint64_t asset       = 0;    // cooked-skeleton AssetId
    int32_t  bone_count  = 0;
    uint64_t pose_handle = 0;    // runtime pose buffer handle
};
struct AnimationState {
    uint64_t clip  = 0;
    float    time  = 0.0f;
    float    speed = 1.0f;
    int32_t  state = 0;          // state-machine state index
    uint32_t flags = 0;          // bit0 looping, bit1 playing
};

// ---- Audio (Stage 6 — POD now) ----
struct AudioSource {
    uint64_t clip   = 0;
    float    volume = 1.0f;
    float    pitch  = 1.0f;
    float    radius = 10.0f;
    uint32_t flags  = 0;         // bit0 looping, bit1 spatial, bit2 playing
};
// The "ears": one active listener entity (usually the camera/player) drives
// the audio mixer's pan/attenuation. Pose comes from the entity's Transform.
struct AudioListener {
    float    master_gain = 1.0f;
    uint32_t flags       = 1;    // bit0 active
};

// ---- Networking (Stage 7) ----
struct NetId {
    uint64_t value = 0;          // stable network id
    uint32_t owner = 0;          // owning connection/client
    uint32_t flags = 0;          // bit0 replicated
};

// ---- Gameplay (Stage 10 — POD now) ----
struct AbilityState {
    int32_t  ability  = -1;
    float    cooldown = 0.0f;
    uint32_t charges  = 0;
    uint32_t flags    = 0;       // bit0 active
};
struct Health {
    float current = 100.0f;
    float max     = 100.0f;
    float regen   = 0.0f;
};

}  // namespace schizo::ecs

GWS_REFLECT_BEGIN(schizo::ecs::Transform)
    GWS_REFLECT_FIELD(position)
    GWS_REFLECT_FIELD(rotation)
    GWS_REFLECT_FIELD(scale)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::LocalToWorld)
    GWS_REFLECT_FIELD(value)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Velocity)
    GWS_REFLECT_FIELD(linear)
    GWS_REFLECT_FIELD(angular)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Name)
    GWS_REFLECT_FIELD(value)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::MeshRenderer)
    GWS_REFLECT_FIELD(mesh)
    GWS_REFLECT_FIELD(primitive)
    GWS_REFLECT_FIELD(material)
    GWS_REFLECT_FIELD(flags)
    GWS_REFLECT_FIELD(lod_bias)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Material)
    GWS_REFLECT_FIELD(base_color)
    GWS_REFLECT_FIELD(metallic)
    GWS_REFLECT_FIELD(roughness)
    GWS_REFLECT_FIELD(emissive)
    GWS_REFLECT_FIELD(albedo_tex)
    GWS_REFLECT_FIELD(alpha_mode)
    GWS_REFLECT_FIELD(alpha_cutoff)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::LightComponent)
    GWS_REFLECT_FIELD(type)
    GWS_REFLECT_FIELD(color)
    GWS_REFLECT_FIELD(intensity)
    GWS_REFLECT_FIELD(range)
    GWS_REFLECT_FIELD(spot_inner)
    GWS_REFLECT_FIELD(spot_outer)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::RigidBody)
    GWS_REFLECT_FIELD(velocity)
    GWS_REFLECT_FIELD(angular_velocity)
    GWS_REFLECT_FIELD(mass)
    GWS_REFLECT_FIELD(restitution)
    GWS_REFLECT_FIELD(friction)
    GWS_REFLECT_FIELD(flags)
    GWS_REFLECT_FIELD(body_handle)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Collider)
    GWS_REFLECT_FIELD(shape)
    GWS_REFLECT_FIELD(half_extents)
    GWS_REFLECT_FIELD(radius)
    GWS_REFLECT_FIELD(height)
    GWS_REFLECT_FIELD(layer)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Skeleton)
    GWS_REFLECT_FIELD(asset)
    GWS_REFLECT_FIELD(bone_count)
    GWS_REFLECT_FIELD(pose_handle)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::AnimationState)
    GWS_REFLECT_FIELD(clip)
    GWS_REFLECT_FIELD(time)
    GWS_REFLECT_FIELD(speed)
    GWS_REFLECT_FIELD(state)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::AudioSource)
    GWS_REFLECT_FIELD(clip)
    GWS_REFLECT_FIELD(volume)
    GWS_REFLECT_FIELD(pitch)
    GWS_REFLECT_FIELD(radius)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::AudioListener)
    GWS_REFLECT_FIELD(master_gain)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::NetId)
    GWS_REFLECT_FIELD(value)
    GWS_REFLECT_FIELD(owner)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::AbilityState)
    GWS_REFLECT_FIELD(ability)
    GWS_REFLECT_FIELD(cooldown)
    GWS_REFLECT_FIELD(charges)
    GWS_REFLECT_FIELD(flags)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::ecs::Health)
    GWS_REFLECT_FIELD(current)
    GWS_REFLECT_FIELD(max)
    GWS_REFLECT_FIELD(regen)
GWS_REFLECT_END()

namespace schizo::ecs {
// Register every core component with reflection (so they are findable by name
// for deserialization / inspector / snapshot). Idempotent.
inline void register_core_components() {
    gws::reflect::reflect<Transform>();
    gws::reflect::reflect<LocalToWorld>();
    gws::reflect::reflect<Velocity>();
    gws::reflect::reflect<Name>();
    gws::reflect::reflect<MeshRenderer>();
    gws::reflect::reflect<Material>();
    gws::reflect::reflect<LightComponent>();
    gws::reflect::reflect<RigidBody>();
    gws::reflect::reflect<Collider>();
    gws::reflect::reflect<Skeleton>();
    gws::reflect::reflect<AnimationState>();
    gws::reflect::reflect<AudioSource>();
    gws::reflect::reflect<AudioListener>();
    gws::reflect::reflect<NetId>();
    gws::reflect::reflect<AbilityState>();
    gws::reflect::reflect<Health>();
}
}  // namespace schizo::ecs
