// PhysicsWorld implementation over Jolt (Master Plan Stage 4). The only place
// Jolt headers appear — the engine-facing API (jolt_physics.h) is Jolt-free.

#include "jolt_physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Math/Float3.h>

#include <cstdio>
#include <thread>
#include <vector>
#include <cstdint>

JPH_SUPPRESS_WARNINGS

namespace schizo::physics {

using namespace JPH;

namespace {
constexpr ObjectLayer     LAYER_STATIC   = 0;
constexpr ObjectLayer     LAYER_MOVING   = 1;
constexpr uint            NUM_OBJ_LAYERS = 2;
const     BroadPhaseLayer BP_STATIC{0};
const     BroadPhaseLayer BP_MOVING{1};
constexpr uint            NUM_BP_LAYERS  = 2;

// Jolt's process-global state (allocator hooks + the type factory) is set up
// once, lazily, shared by every PhysicsWorld.
bool g_jolt_inited = false;
void ensure_jolt_globals() {
    if (g_jolt_inited) return;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    g_jolt_inited = true;
}

EMotionType to_jph_motion(MotionType m) {
    switch (m) {
        case MotionType::Static:    return EMotionType::Static;
        case MotionType::Kinematic: return EMotionType::Kinematic;
        default:                    return EMotionType::Dynamic;
    }
}
// Single-precision build (DOUBLE_PRECISION OFF) so RVec3 == Vec3 — one overload
// covers both positions and velocities.
glm::vec3 to_glm(Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
glm::quat to_glm(QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
}  // namespace

struct PhysicsWorld::Impl {
    std::unique_ptr<TempAllocatorImpl>   temp_alloc;
    std::unique_ptr<JobSystemThreadPool> jobs;
    // Layer config — referenced by PhysicsSystem, so these outlive it.
    BroadPhaseLayerInterfaceTable        bp_interface{ NUM_OBJ_LAYERS, NUM_BP_LAYERS };
    ObjectLayerPairFilterTable           obj_filter{ NUM_OBJ_LAYERS };
    std::unique_ptr<ObjectVsBroadPhaseLayerFilterTable> obj_vs_bp;
    PhysicsSystem                        system;
    std::vector<RefConst<Shape>>         shapes;   // keep shapes alive
    std::vector<Ref<CharacterVirtual>>   characters;
    glm::vec3                            gravity{0.0f, -9.81f, 0.0f};
    size_t                               n_bodies = 0;
};

PhysicsWorld::PhysicsWorld() {
    // CRITICAL: Jolt's allocator + type factory must exist BEFORE any Jolt
    // object is constructed — Impl's members (BroadPhaseLayerInterfaceTable,
    // PhysicsSystem, …) allocate through Jolt's hooks at construction, so this
    // must run before make_unique<Impl>().
    ensure_jolt_globals();
    impl_ = std::make_unique<Impl>();
}
PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::init(glm::vec3 gravity, uint32_t max_bodies) {
    Impl& m = *impl_;

    m.temp_alloc = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    const int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    m.jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);

    m.bp_interface.MapObjectToBroadPhaseLayer(LAYER_STATIC, BP_STATIC);
    m.bp_interface.MapObjectToBroadPhaseLayer(LAYER_MOVING, BP_MOVING);
    // Moving collides with everything; static-vs-static never collides.
    m.obj_filter.EnableCollision(LAYER_MOVING, LAYER_STATIC);
    m.obj_filter.EnableCollision(LAYER_MOVING, LAYER_MOVING);
    m.obj_filter.DisableCollision(LAYER_STATIC, LAYER_STATIC);
    m.obj_vs_bp = std::make_unique<ObjectVsBroadPhaseLayerFilterTable>(
        m.bp_interface, NUM_BP_LAYERS, m.obj_filter, NUM_OBJ_LAYERS);

    m.system.Init(max_bodies, /*numBodyMutexes*/ 0, /*maxBodyPairs*/ 65536,
                  /*maxContactConstraints*/ 20480, m.bp_interface, *m.obj_vs_bp, m.obj_filter);
    m.system.SetGravity(Vec3(gravity.x, gravity.y, gravity.z));
    m.gravity = gravity;
    return true;
}

BodyId PhysicsWorld::add_body(const BodyDesc& d) {
    Impl& m = *impl_;

    ShapeSettings::ShapeResult res;
    switch (d.shape) {
        case ShapeType::Box:
            res = BoxShapeSettings(Vec3(d.half_extents.x, d.half_extents.y, d.half_extents.z)).Create();
            break;
        case ShapeType::Sphere:
            res = SphereShapeSettings(d.radius).Create();
            break;
        case ShapeType::Capsule:
            // Jolt capsule = cylinder (half-height) + two hemispheres (radius).
            res = CapsuleShapeSettings(0.5f * d.height, d.radius).Create();
            break;
        case ShapeType::Cylinder:
            res = CylinderShapeSettings(0.5f * d.height, d.radius).Create();
            break;
    }
    if (res.HasError()) return kInvalidBody;
    RefConst<Shape> shape = res.Get();
    m.shapes.push_back(shape);

    const bool is_static = (d.motion == MotionType::Static);
    const ObjectLayer layer = is_static ? LAYER_STATIC : LAYER_MOVING;
    BodyCreationSettings bcs(shape,
                             RVec3(d.position.x, d.position.y, d.position.z),
                             Quat(d.rotation.x, d.rotation.y, d.rotation.z, d.rotation.w),
                             to_jph_motion(d.motion), layer);
    bcs.mFriction    = d.friction;
    bcs.mRestitution = d.restitution;
    if (!is_static) {
        bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = d.mass;
    }

    BodyInterface& bi = m.system.GetBodyInterface();
    BodyID id = bi.CreateAndAddBody(bcs, is_static ? EActivation::DontActivate : EActivation::Activate);
    if (id.IsInvalid()) return kInvalidBody;
    ++m.n_bodies;
    return id.GetIndexAndSequenceNumber();
}

BodyId PhysicsWorld::add_mesh_body(const std::vector<glm::vec3>& tris,
                                   const glm::vec3& pos, const glm::quat& rot) {
    Impl& m = *impl_;
    if (tris.size() < 3) return kInvalidBody;
    VertexList          verts;
    IndexedTriangleList itris;
    verts.reserve(tris.size());
    itris.reserve(tris.size() / 3);
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const uint32 base = static_cast<uint32>(verts.size());
        verts.push_back(Float3(tris[i + 0].x, tris[i + 0].y, tris[i + 0].z));
        verts.push_back(Float3(tris[i + 1].x, tris[i + 1].y, tris[i + 1].z));
        verts.push_back(Float3(tris[i + 2].x, tris[i + 2].y, tris[i + 2].z));
        itris.push_back(IndexedTriangle(base, base + 1, base + 2, 0));
    }
    ShapeSettings::ShapeResult res = MeshShapeSettings(verts, itris).Create();
    if (res.HasError()) return kInvalidBody;
    m.shapes.push_back(res.Get());

    BodyCreationSettings bcs(res.Get(), RVec3(pos.x, pos.y, pos.z),
                             Quat(rot.x, rot.y, rot.z, rot.w), EMotionType::Static, LAYER_STATIC);
    BodyID id = m.system.GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);
    if (id.IsInvalid()) return kInvalidBody;
    ++m.n_bodies;
    return id.GetIndexAndSequenceNumber();
}

BodyId PhysicsWorld::add_convex_body(const std::vector<glm::vec3>& points, const BodyDesc& d) {
    Impl& m = *impl_;
    if (points.size() < 4) return kInvalidBody;   // need a non-degenerate 3D hull
    Array<Vec3> pts;
    pts.reserve(points.size());
    for (const glm::vec3& p : points) pts.push_back(Vec3(p.x, p.y, p.z));

    ShapeSettings::ShapeResult res = ConvexHullShapeSettings(pts).Create();
    if (res.HasError()) return kInvalidBody;
    m.shapes.push_back(res.Get());

    const bool is_static = (d.motion == MotionType::Static);
    const ObjectLayer layer = is_static ? LAYER_STATIC : LAYER_MOVING;
    BodyCreationSettings bcs(res.Get(),
                             RVec3(d.position.x, d.position.y, d.position.z),
                             Quat(d.rotation.x, d.rotation.y, d.rotation.z, d.rotation.w),
                             to_jph_motion(d.motion), layer);
    bcs.mFriction    = d.friction;
    bcs.mRestitution = d.restitution;
    if (!is_static) {
        bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = d.mass;
    }
    BodyID id = m.system.GetBodyInterface().CreateAndAddBody(
        bcs, is_static ? EActivation::DontActivate : EActivation::Activate);
    if (id.IsInvalid()) return kInvalidBody;
    ++m.n_bodies;
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::remove_body(BodyId id) {
    if (id == kInvalidBody) return;
    BodyInterface& bi = impl_->system.GetBodyInterface();
    BodyID bid(id);
    bi.RemoveBody(bid);
    bi.DestroyBody(bid);
    if (impl_->n_bodies) --impl_->n_bodies;
}

void PhysicsWorld::step(float dt, int collision_steps) {
    impl_->system.Update(dt, collision_steps, impl_->temp_alloc.get(), impl_->jobs.get());
}

BodyState PhysicsWorld::get_state(BodyId id) const {
    BodyState s;
    if (id == kInvalidBody) return s;
    const BodyInterface& bi = impl_->system.GetBodyInterface();
    BodyID bid(id);
    s.position        = to_glm(bi.GetPosition(bid));
    s.rotation        = to_glm(bi.GetRotation(bid));
    s.linear_velocity = to_glm(bi.GetLinearVelocity(bid));
    return s;
}

void PhysicsWorld::set_transform(BodyId id, const glm::vec3& pos, const glm::quat& rot) {
    if (id == kInvalidBody) return;
    impl_->system.GetBodyInterface().SetPositionAndRotation(
        BodyID(id), RVec3(pos.x, pos.y, pos.z), Quat(rot.x, rot.y, rot.z, rot.w),
        EActivation::Activate);
}

void PhysicsWorld::translate_world(const glm::vec3& d) {
    if (d == glm::vec3(0.0f)) return;
    const RVec3 off(d.x, d.y, d.z);

    BodyIDVector ids;
    impl_->system.GetBodies(ids);
    BodyInterface& bi = impl_->system.GetBodyInterface();
    for (const BodyID& id : ids)
        bi.SetPosition(id, bi.GetPosition(id) + off, EActivation::DontActivate);

    // CharacterVirtual is not a body and is not in that list -- the player
    // would be the one thing left behind.
    for (Ref<CharacterVirtual>& c : impl_->characters)
        if (c) c->SetPosition(c->GetPosition() + off);
}

void PhysicsWorld::move_kinematic(BodyId id, const glm::vec3& pos, const glm::quat& rot, float dt) {
    if (id == kInvalidBody || dt <= 0.0f) return;
    impl_->system.GetBodyInterface().MoveKinematic(
        BodyID(id), RVec3(pos.x, pos.y, pos.z), Quat(rot.x, rot.y, rot.z, rot.w), dt);
}

void PhysicsWorld::set_linear_velocity(BodyId id, const glm::vec3& v) {
    if (id == kInvalidBody) return;
    impl_->system.GetBodyInterface().SetLinearVelocity(BodyID(id), Vec3(v.x, v.y, v.z));
}

glm::vec3 PhysicsWorld::get_linear_velocity(BodyId id) const {
    if (id == kInvalidBody) return glm::vec3(0.0f);
    return to_glm(impl_->system.GetBodyInterface().GetLinearVelocity(BodyID(id)));
}

void PhysicsWorld::add_impulse(BodyId id, const glm::vec3& impulse) {
    if (id == kInvalidBody) return;
    impl_->system.GetBodyInterface().AddImpulse(
        BodyID(id), Vec3(impulse.x, impulse.y, impulse.z));
}

RayHit PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                             float max_distance) const {
    RayHit out;
    const float len = glm::length(direction);
    if (len < 1e-6f || max_distance <= 0.0f) return out;
    const glm::vec3 dir = direction / len;                 // unit
    const Vec3 seg(dir.x * max_distance, dir.y * max_distance, dir.z * max_distance);

    // RRayCast = origin + the full segment vector; result fraction is along it.
    RRayCast ray(RVec3(origin.x, origin.y, origin.z), seg);
    RayCastResult result;
    if (impl_->system.GetNarrowPhaseQuery().CastRay(ray, result)) {
        out.hit      = true;
        out.distance = result.mFraction * max_distance;
        out.position = origin + dir * out.distance;
        out.body     = static_cast<BodyId>(result.mBodyID.GetIndexAndSequenceNumber());
    }
    return out;
}

// --- Character controller ---

CharacterId PhysicsWorld::add_character(const CharacterDesc& d) {
    Impl& m = *impl_;
    ShapeSettings::ShapeResult res = CapsuleShapeSettings(0.5f * d.height, d.radius).Create();
    if (res.HasError()) return kInvalidBody;
    m.shapes.push_back(res.Get());

    CharacterVirtualSettings cs;
    cs.mShape         = res.Get();
    cs.mMaxSlopeAngle = DegreesToRadians(d.max_slope_deg);
    cs.mUp            = Vec3::sAxisY();
    Ref<CharacterVirtual> ch = new CharacterVirtual(
        &cs, RVec3(d.position.x, d.position.y, d.position.z), Quat::sIdentity(), &m.system);
    m.characters.push_back(ch);
    return static_cast<CharacterId>(m.characters.size() - 1);
}

void PhysicsWorld::update_character(CharacterId id, const glm::vec3& velocity, float dt) {
    Impl& m = *impl_;
    if (id >= m.characters.size()) return;
    CharacterVirtual* ch = m.characters[id];
    // Caller owns the full velocity (gravity + jump in .y); we just move it and
    // resolve collisions (collide-and-slide + stair-step + stick-to-floor).
    ch->SetLinearVelocity(Vec3(velocity.x, velocity.y, velocity.z));
    CharacterVirtual::ExtendedUpdateSettings settings;
    ch->ExtendedUpdate(dt, Vec3(m.gravity.x, m.gravity.y, m.gravity.z), settings,
                       m.system.GetDefaultBroadPhaseLayerFilter(LAYER_MOVING),
                       m.system.GetDefaultLayerFilter(LAYER_MOVING),
                       BodyFilter{}, ShapeFilter{}, *m.temp_alloc);
}

void PhysicsWorld::set_character_position(CharacterId id, const glm::vec3& pos) {
    if (id >= impl_->characters.size()) return;
    impl_->characters[id]->SetPosition(RVec3(pos.x, pos.y, pos.z));
}

glm::vec3 PhysicsWorld::character_position(CharacterId id) const {
    if (id >= impl_->characters.size()) return glm::vec3(0.0f);
    return to_glm(impl_->characters[id]->GetPosition());
}

bool PhysicsWorld::character_on_ground(CharacterId id) const {
    if (id >= impl_->characters.size()) return false;
    return impl_->characters[id]->GetGroundState() == CharacterBase::EGroundState::OnGround;
}

size_t PhysicsWorld::body_count() const { return impl_->n_bodies; }

}  // namespace schizo::physics
