#include "physics_world.h"
#include "rigidbody.h"
#include "collision.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <unordered_map>

namespace schizo::physics {

// ============================================================================
// DefaultPhysicsWorld Implementation
// ============================================================================

class DefaultPhysicsWorld : public PhysicsWorld {
private:
    PhysicsConfig config_;
    std::vector<RigidBody*> rigid_bodies_;
    std::vector<std::shared_ptr<Constraint>> constraints_;
    Stats stats_;
    float accumulated_time_ = 0;
    // Per-body contact list for the most recent Step(). Cleared and refilled
    // each frame from PerformCollisionDetection so callers (e.g. the editor's
    // grounded check) can read the actual contact normals instead of relying
    // on the loose bounding-sphere Raycast.
    std::unordered_map<RigidBody*, std::vector<Contact>> body_contacts_;

public:
    DefaultPhysicsWorld(const PhysicsConfig& config) 
        : config_(config) {
        //  Initialize inherited protected members
        gravity_ = config.gravity;
        time_scale_ = config.time_scale;
        enabled_ = true;
        
        stats_.body_count = 0;
        stats_.active_bodies = 0;
        stats_.sleeping_bodies = 0;
        stats_.collision_tests = 0;
        stats_.collisions_found = 0;
        stats_.simulation_time_ms = 0;
    }
    
    ~DefaultPhysicsWorld() override {
        Clear();
    }

public:
    
    // ========== World Management ==========
    
    void AddRigidBody(RigidBody* body) override {
        if (!body) return;
        
        auto it = std::find(rigid_bodies_.begin(), rigid_bodies_.end(), body);
        if (it == rigid_bodies_.end()) {
            rigid_bodies_.push_back(body);
            stats_.body_count = rigid_bodies_.size();
        }
    }

    void RemoveRigidBody(RigidBody* body) override {
        if (!body) return;
        
        auto it = std::find(rigid_bodies_.begin(), rigid_bodies_.end(), body);
        if (it != rigid_bodies_.end()) {
            rigid_bodies_.erase(it);
            stats_.body_count = rigid_bodies_.size();
        }
    }

    void Clear() override {
        rigid_bodies_.clear();
        constraints_.clear();
        stats_.body_count = 0;
        stats_.active_bodies = 0;
        stats_.sleeping_bodies = 0;
    }

    uint32_t GetBodyCount() const override {
        return rigid_bodies_.size();
    }
    
// ========== Simulation ==========
    
    void Step(float delta_time) override {
        if (!enabled_ || delta_time <= 0) return;

        auto start_time = std::chrono::high_resolution_clock::now();

        delta_time *= time_scale_;
        body_contacts_.clear();

        // Gravity is applied by RigidBody::IntegrateVelocity using its per-body
        // gravity_scale_, so we do NOT re-apply it here. Previously this loop
        // also wrote gravity into force_, which (combined with force_ never
        // being cleared) compounded into runaway acceleration across frames.

        // ---- CCD via sub-stepping ----
        // Pick a sub-step count from the fastest Dynamic body's "move per
        // frame" relative to its bounding radius. A body that would move
        // more than ~half its radius per frame is a tunneling risk; we
        // subdivide so each sub-step's motion stays within bounds.
        // Kinematic bodies are not sub-stepped — they don't auto-integrate,
        // their position is set externally. Capped at kMaxSubsteps to keep
        // per-frame cost bounded.
        constexpr int   kMaxSubsteps          = 8;
        constexpr float kSubstepSafetyFactor  = 0.5f;  // fraction of radius per step
        int substeps = 1;
        for (auto* body : rigid_bodies_) {
            if (!body || body->GetBodyType() != BodyType::Dynamic) continue;
            float radius = body->GetBoundsRadius();
            if (radius <= 0.0f) continue;
            float speed = glm::length(body->GetVelocity());
            if (speed <= 0.0f) continue;
            float move_per_frame = speed * delta_time;
            int needed = static_cast<int>(std::ceil(move_per_frame
                                                    / (radius * kSubstepSafetyFactor)));
            if (needed > substeps) substeps = needed;
        }
        if (substeps > kMaxSubsteps) substeps = kMaxSubsteps;
        float sub_dt = delta_time / static_cast<float>(substeps);

        for (int s = 0; s < substeps; ++s) {
            // Broad phase: collect potential collisions
            std::vector<std::pair<RigidBody*, RigidBody*>> potential_collisions;
            BroadPhase(potential_collisions);

            // Narrow phase: test actual collisions
            PerformCollisionDetection(potential_collisions);

            // Integrate velocities and positions, then clear per-frame
            // forces so ApplyForce() during the next sub-step / frame
            // doesn't accumulate on top of the residual.
            for (auto* body : rigid_bodies_) {
                if (body) {
                    body->OnFixedUpdate(sub_dt);
                    body->ClearForces();
                }
            }

            // Constraints run once per sub-step so springs / joints stay
            // stable during high-speed motion too.
            SolveConstraints(sub_dt);
        }
        
        // Update sleeping state
        stats_.active_bodies = 0;
        stats_.sleeping_bodies = 0;
        for (auto* body : rigid_bodies_) {
            if (body) {
                if (body->IsSleeping()) {
                    stats_.sleeping_bodies++;
                } else {
                    stats_.active_bodies++;
                }
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.simulation_time_ms = duration.count() / 1000.0;
    }

    void SetGravity(const glm::vec3& gravity) override {
        gravity_ = gravity;
        config_.gravity = gravity;
    }
    
    void SetTimeScale(float scale) override {
        time_scale_ = std::max(0.0f, scale);
    }
    
    void SetEnabled(bool enabled) override {
        enabled_ = enabled;
    }
    
    // Queries
    bool Raycast(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float max_distance,
        RigidBody*& out_hit,
        glm::vec3& out_point,
        glm::vec3& out_normal) const override {
        
        glm::vec3 ray_dir = glm::normalize(direction);
        float closest_distance = max_distance;
        RigidBody* closest_body = nullptr;
        glm::vec3 closest_point = origin;

        for (auto* body : rigid_bodies_) {
            if (!body || !body->GetCollisionShape()) continue;

            // Ray-vs-bounding-sphere intersection. We check BOTH roots so an
            // origin already inside the bounding sphere (typical for a large
            // floor whose bounds radius covers the player) still returns the
            // exit point as a valid hit. The previous code only took the
            // near root, so it silently missed every "ray cast from inside"
            // case and the grounded check would always fail.
            glm::vec3 bounds_center = body->GetWorldBoundsCenter();
            float bounds_radius = body->GetBoundsRadius();
            glm::vec3 oc = origin - bounds_center;

            float a = glm::dot(ray_dir, ray_dir);
            float b = 2.0f * glm::dot(oc, ray_dir);
            float c = glm::dot(oc, oc) - bounds_radius * bounds_radius;
            float discriminant = b * b - 4 * a * c;
            if (discriminant < 0) continue;

            float sqrt_disc = std::sqrt(discriminant);
            float t_near = (-b - sqrt_disc) / (2 * a);
            float t_far  = (-b + sqrt_disc) / (2 * a);
            float t = (t_near > 0) ? t_near : t_far;
            if (t > 0 && t < closest_distance) {
                closest_distance = t;
                closest_body = body;
                closest_point = origin + ray_dir * t;
            }
        }
        
        if (closest_body) {
            out_hit = closest_body;
            out_point = closest_point;
            out_normal = glm::normalize(closest_point - closest_body->GetWorldBoundsCenter());
            return true;
        }
        
        out_hit = nullptr;
        return false;
    }
    
    std::vector<RigidBody*> GetBodiesInAABB(
        const glm::vec3& min,
        const glm::vec3& max) const override {
        
        std::vector<RigidBody*> result;
        
        for (auto* body : rigid_bodies_) {
            if (!body) continue;
            
            glm::vec3 body_center = body->GetWorldBoundsCenter();
            float radius = body->GetBoundsRadius();
            
            glm::vec3 body_min = body_center - glm::vec3(radius);
            glm::vec3 body_max = body_center + glm::vec3(radius);
            
            // AABB overlap test
            if (!(max.x < body_min.x || max.y < body_min.y || max.z < body_min.z ||
                  min.x > body_max.x || min.y > body_max.y || min.z > body_max.z)) {
                result.push_back(body);
            }
        }
        
        return result;
    }
    
    std::vector<RigidBody*> GetBodiesInSphere(
        const glm::vec3& center,
        float radius) const override {

        std::vector<RigidBody*> result;

        for (auto* body : rigid_bodies_) {
            if (!body) continue;

            glm::vec3 body_center = body->GetWorldBoundsCenter();
            float distance = glm::distance(center, body_center);

            if (distance <= radius + body->GetBoundsRadius()) {
                result.push_back(body);
            }
        }

        return result;
    }

    const std::vector<Contact>* GetBodyContacts(RigidBody* body) const override {
        auto it = body_contacts_.find(body);
        return (it != body_contacts_.end()) ? &it->second : nullptr;
    }
    
    void SetSolverIterations(uint32_t iterations) override {
        config_.solver_iterations = std::max(1u, iterations);
    }
    
    void SetBroadPhaseEnabled(bool enabled) override {
        config_.enable_broad_phase = enabled;
    }
    
    void SetSleepingEnabled(bool enabled) override {
        config_.enable_sleeping = enabled;
    }
    
    // Statistics
    Stats GetStats() const override {
        return stats_;
    }
    
    void ResetStats() override {
        stats_.body_count = rigid_bodies_.size();
        stats_.active_bodies = 0;
        stats_.sleeping_bodies = 0;
        stats_.collision_tests = 0;
        stats_.collisions_found = 0;
        stats_.simulation_time_ms = 0;
    }

private:
    // Layer + mask filter. A pair survives only if both sides accept each
    // other's layer. With the default (layer=0, mask=0xFFFFFFFF) every body
    // collides with every other body, so layer-unaware setups are unchanged.
    static bool LayersAccept(RigidBody* a, RigidBody* b) {
        uint32_t bit_a = 1u << a->GetLayer();
        uint32_t bit_b = 1u << b->GetLayer();
        return (bit_a & b->GetCollisionMask()) != 0u
            && (bit_b & a->GetCollisionMask()) != 0u;
    }

    void BroadPhase(std::vector<std::pair<RigidBody*, RigidBody*>>& potential_pairs) {
        if (!config_.enable_broad_phase) {
            // If broad phase disabled, test all pairs (slow but simple)
            for (size_t i = 0; i < rigid_bodies_.size(); ++i) {
                for (size_t j = i + 1; j < rigid_bodies_.size(); ++j) {
                    if (!LayersAccept(rigid_bodies_[i], rigid_bodies_[j])) continue;
                    potential_pairs.push_back({rigid_bodies_[i], rigid_bodies_[j]});
                }
            }
            return;
        }

        // Simple broad phase: sphere-based culling
        for (size_t i = 0; i < rigid_bodies_.size(); ++i) {
            auto* body_a = rigid_bodies_[i];
            if (!body_a) continue;

            glm::vec3 center_a = body_a->GetWorldBoundsCenter();
            float radius_a = body_a->GetBoundsRadius();

            for (size_t j = i + 1; j < rigid_bodies_.size(); ++j) {
                auto* body_b = rigid_bodies_[j];
                if (!body_b) continue;
                if (!LayersAccept(body_a, body_b)) continue;

                glm::vec3 center_b = body_b->GetWorldBoundsCenter();
                float radius_b = body_b->GetBoundsRadius();

                float distance = glm::distance(center_a, center_b);
                if (distance < radius_a + radius_b + 1.0f) { // Add small margin
                    potential_pairs.push_back({body_a, body_b});
                }
            }
        }
    }
    
    void PerformCollisionDetection(
        const std::vector<std::pair<RigidBody*, RigidBody*>>& potential_pairs) {
        
        stats_.collision_tests = 0;
        stats_.collisions_found = 0;
        
        for (const auto& [body_a, body_b] : potential_pairs) {
            if (!body_a || !body_b) continue;
            
            // Skip if both are sleeping
            if (body_a->IsSleeping() && body_b->IsSleeping()) continue;
            
            // Skip if both are static
            if (body_a->GetBodyType() == BodyType::Static && 
                body_b->GetBodyType() == BodyType::Static) continue;
            
            stats_.collision_tests++;
            
            auto shape_a = body_a->GetCollisionShape();
            auto shape_b = body_b->GetCollisionShape();
            
            if (!shape_a || !shape_b) continue;
            
            // Perform shape-specific collision tests
            glm::vec3 pos_a = body_a->GetWorldBoundsCenter();
            glm::vec3 pos_b = body_b->GetWorldBoundsCenter();
            
            Contact contact;
            bool colliding = false;
            
            // Body world rotations — passed to box-side tests so collision
            // honours entity rotation, and used to derive capsule endpoints
            // (the capsule axis is local Y; rotating it gives a world spine).
            glm::quat rot_a = body_a->GetWorldRotation();
            glm::quat rot_b = body_b->GetWorldRotation();
            auto capsule_endpoints = [](const glm::vec3& pos, const glm::quat& rot,
                                        float hh, glm::vec3& a, glm::vec3& b) {
                glm::vec3 axis = rot * glm::vec3(0.0f, 1.0f, 0.0f);
                a = pos - axis * hh;
                b = pos + axis * hh;
            };

            // Test collision based on shape types
            if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Sphere) {
                auto* sphere_a = static_cast<SphereShape*>(shape_a);
                auto* sphere_b = static_cast<SphereShape*>(shape_b);
                colliding = collision::SphereSphere(
                    pos_a, sphere_a->GetRadius(),
                    pos_b, sphere_b->GetRadius(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Box) {
                auto* sphere = static_cast<SphereShape*>(shape_a);
                auto* box = static_cast<BoxShape*>(shape_b);
                colliding = collision::SphereBox(
                    pos_a, sphere->GetRadius(),
                    pos_b, box->GetHalfExtents(), rot_b,
                    contact);
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Sphere) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                colliding = collision::SphereBox(
                    pos_b, sphere->GetRadius(),
                    pos_a, box->GetHalfExtents(), rot_a,
                    contact);
                // Swap data since we switched bodies
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Box) {
                auto* box_a = static_cast<BoxShape*>(shape_a);
                auto* box_b = static_cast<BoxShape*>(shape_b);
                colliding = collision::BoxBox(
                    pos_a, box_a->GetHalfExtents(), rot_a,
                    pos_b, box_b->GetHalfExtents(), rot_b,
                    contact);
            } else if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Plane) {
                auto* sphere = static_cast<SphereShape*>(shape_a);
                auto* plane = static_cast<PlaneShape*>(shape_b);
                colliding = collision::SpherePlane(
                    pos_a, sphere->GetRadius(),
                    pos_b, plane->GetNormal(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Plane && shape_b->GetType() == ShapeType::Sphere) {
                auto* plane = static_cast<PlaneShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                colliding = collision::SpherePlane(
                    pos_b, sphere->GetRadius(),
                    pos_a, plane->GetNormal(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Plane) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* plane = static_cast<PlaneShape*>(shape_b);
                colliding = collision::BoxPlane(
                    pos_a, box->GetHalfExtents(), rot_a,
                    pos_b, plane->GetNormal(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Plane && shape_b->GetType() == ShapeType::Box) {
                auto* plane = static_cast<PlaneShape*>(shape_a);
                auto* box = static_cast<BoxShape*>(shape_b);
                colliding = collision::BoxPlane(
                    pos_b, box->GetHalfExtents(), rot_b,
                    pos_a, plane->GetNormal(),
                    contact);
                contact.normal = -contact.normal;
            }
            // ---- Capsule pairs (capsule axis follows body rotation) ----
            else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Sphere) {
                auto* cap = static_cast<CapsuleShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                glm::vec3 a0, a1;
                capsule_endpoints(pos_a, rot_a, cap->GetHalfHeight(), a0, a1);
                colliding = collision::CapsuleSphere(
                    a0, a1, cap->GetRadius(),
                    pos_b, sphere->GetRadius(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Capsule) {
                auto* sphere = static_cast<SphereShape*>(shape_a);
                auto* cap = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 b0, b1;
                capsule_endpoints(pos_b, rot_b, cap->GetHalfHeight(), b0, b1);
                colliding = collision::CapsuleSphere(
                    b0, b1, cap->GetRadius(),
                    pos_a, sphere->GetRadius(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Box) {
                auto* cap = static_cast<CapsuleShape*>(shape_a);
                auto* box = static_cast<BoxShape*>(shape_b);
                glm::vec3 a0, a1;
                capsule_endpoints(pos_a, rot_a, cap->GetHalfHeight(), a0, a1);
                colliding = collision::CapsuleBox(
                    a0, a1, cap->GetRadius(),
                    pos_b, box->GetHalfExtents(), rot_b,
                    contact);
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Capsule) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* cap = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 b0, b1;
                capsule_endpoints(pos_b, rot_b, cap->GetHalfHeight(), b0, b1);
                colliding = collision::CapsuleBox(
                    b0, b1, cap->GetRadius(),
                    pos_a, box->GetHalfExtents(), rot_a,
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Plane) {
                auto* cap = static_cast<CapsuleShape*>(shape_a);
                auto* plane = static_cast<PlaneShape*>(shape_b);
                glm::vec3 a0, a1;
                capsule_endpoints(pos_a, rot_a, cap->GetHalfHeight(), a0, a1);
                colliding = collision::CapsulePlane(
                    a0, a1, cap->GetRadius(),
                    pos_b, plane->GetNormal(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Plane && shape_b->GetType() == ShapeType::Capsule) {
                auto* plane = static_cast<PlaneShape*>(shape_a);
                auto* cap = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 b0, b1;
                capsule_endpoints(pos_b, rot_b, cap->GetHalfHeight(), b0, b1);
                colliding = collision::CapsulePlane(
                    b0, b1, cap->GetRadius(),
                    pos_a, plane->GetNormal(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Capsule) {
                auto* cap_a = static_cast<CapsuleShape*>(shape_a);
                auto* cap_b = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 a0, a1, b0, b1;
                capsule_endpoints(pos_a, rot_a, cap_a->GetHalfHeight(), a0, a1);
                capsule_endpoints(pos_b, rot_b, cap_b->GetHalfHeight(), b0, b1);
                colliding = collision::CapsuleCapsule(
                    a0, a1, cap_a->GetRadius(),
                    b0, b1, cap_b->GetRadius(),
                    contact);
            }
            // ---- Cylinder pairs (Y-axis cylinder, rotated by body) ----
            else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Sphere) {
                auto* cyl = static_cast<CylinderShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                colliding = collision::CylinderSphere(
                    pos_a, cyl->GetRadius(), cyl->GetHalfHeight(), rot_a,
                    pos_b, sphere->GetRadius(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Cylinder) {
                auto* sphere = static_cast<SphereShape*>(shape_a);
                auto* cyl = static_cast<CylinderShape*>(shape_b);
                colliding = collision::CylinderSphere(
                    pos_b, cyl->GetRadius(), cyl->GetHalfHeight(), rot_b,
                    pos_a, sphere->GetRadius(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Plane) {
                auto* cyl = static_cast<CylinderShape*>(shape_a);
                auto* plane = static_cast<PlaneShape*>(shape_b);
                colliding = collision::CylinderPlane(
                    pos_a, cyl->GetRadius(), cyl->GetHalfHeight(), rot_a,
                    pos_b, plane->GetNormal(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Plane && shape_b->GetType() == ShapeType::Cylinder) {
                auto* plane = static_cast<PlaneShape*>(shape_a);
                auto* cyl = static_cast<CylinderShape*>(shape_b);
                colliding = collision::CylinderPlane(
                    pos_b, cyl->GetRadius(), cyl->GetHalfHeight(), rot_b,
                    pos_a, plane->GetNormal(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Box) {
                auto* cyl = static_cast<CylinderShape*>(shape_a);
                auto* box = static_cast<BoxShape*>(shape_b);
                colliding = collision::CylinderBox(
                    pos_a, cyl->GetRadius(), cyl->GetHalfHeight(), rot_a,
                    pos_b, box->GetHalfExtents(), rot_b,
                    contact);
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Cylinder) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* cyl = static_cast<CylinderShape*>(shape_b);
                colliding = collision::CylinderBox(
                    pos_b, cyl->GetRadius(), cyl->GetHalfHeight(), rot_b,
                    pos_a, box->GetHalfExtents(), rot_a,
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Capsule) {
                auto* cyl = static_cast<CylinderShape*>(shape_a);
                auto* cap = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 b0, b1;
                capsule_endpoints(pos_b, rot_b, cap->GetHalfHeight(), b0, b1);
                colliding = collision::CylinderCapsule(
                    pos_a, cyl->GetRadius(), cyl->GetHalfHeight(), rot_a,
                    b0, b1, cap->GetRadius(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Cylinder) {
                auto* cap = static_cast<CapsuleShape*>(shape_a);
                auto* cyl = static_cast<CylinderShape*>(shape_b);
                glm::vec3 a0, a1;
                capsule_endpoints(pos_a, rot_a, cap->GetHalfHeight(), a0, a1);
                colliding = collision::CylinderCapsule(
                    pos_b, cyl->GetRadius(), cyl->GetHalfHeight(), rot_b,
                    a0, a1, cap->GetRadius(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Cylinder) {
                auto* cyl_a = static_cast<CylinderShape*>(shape_a);
                auto* cyl_b = static_cast<CylinderShape*>(shape_b);
                colliding = collision::CylinderCylinder(
                    pos_a, cyl_a->GetRadius(), cyl_a->GetHalfHeight(), rot_a,
                    pos_b, cyl_b->GetRadius(), cyl_b->GetHalfHeight(), rot_b,
                    contact);
            }
            // ---- Mesh pairs (triangle soup; mesh is always the "B" side
            // in our convention so the contact normal points into the
            // mesh's solid side) ----
            else if (shape_a->GetType() == ShapeType::Sphere && shape_b->GetType() == ShapeType::Mesh) {
                auto* sphere = static_cast<SphereShape*>(shape_a);
                auto* mesh = static_cast<MeshShape*>(shape_b);
                colliding = collision::SphereMesh(
                    pos_a, sphere->GetRadius(),
                    pos_b, rot_b, mesh->GetTriangles(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Mesh && shape_b->GetType() == ShapeType::Sphere) {
                auto* mesh = static_cast<MeshShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                colliding = collision::SphereMesh(
                    pos_b, sphere->GetRadius(),
                    pos_a, rot_a, mesh->GetTriangles(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Capsule && shape_b->GetType() == ShapeType::Mesh) {
                auto* cap = static_cast<CapsuleShape*>(shape_a);
                auto* mesh = static_cast<MeshShape*>(shape_b);
                glm::vec3 a0, a1;
                capsule_endpoints(pos_a, rot_a, cap->GetHalfHeight(), a0, a1);
                colliding = collision::CapsuleMesh(
                    a0, a1, cap->GetRadius(),
                    pos_b, rot_b, mesh->GetTriangles(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Mesh && shape_b->GetType() == ShapeType::Capsule) {
                auto* mesh = static_cast<MeshShape*>(shape_a);
                auto* cap = static_cast<CapsuleShape*>(shape_b);
                glm::vec3 b0, b1;
                capsule_endpoints(pos_b, rot_b, cap->GetHalfHeight(), b0, b1);
                colliding = collision::CapsuleMesh(
                    b0, b1, cap->GetRadius(),
                    pos_a, rot_a, mesh->GetTriangles(),
                    contact);
                contact.normal = -contact.normal;
            }
            // Box and Cylinder vs Mesh reduce to a bounding capsule along
            // the body's local Y. Captures the common case (player- or
            // crate-sized object on level geometry) without writing dedicated
            // OBB-vs-triangle / cylinder-vs-triangle code yet.
            else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Mesh) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* mesh = static_cast<MeshShape*>(shape_b);
                glm::vec3 he = box->GetHalfExtents();
                float radius = std::max(he.x, he.z);
                float hh = std::max(0.0f, he.y - radius);
                glm::vec3 axis = rot_a * glm::vec3(0, 1, 0);
                glm::vec3 a0 = pos_a - axis * hh;
                glm::vec3 a1 = pos_a + axis * hh;
                colliding = collision::CapsuleMesh(
                    a0, a1, radius,
                    pos_b, rot_b, mesh->GetTriangles(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Mesh && shape_b->GetType() == ShapeType::Box) {
                auto* mesh = static_cast<MeshShape*>(shape_a);
                auto* box = static_cast<BoxShape*>(shape_b);
                glm::vec3 he = box->GetHalfExtents();
                float radius = std::max(he.x, he.z);
                float hh = std::max(0.0f, he.y - radius);
                glm::vec3 axis = rot_b * glm::vec3(0, 1, 0);
                glm::vec3 b0 = pos_b - axis * hh;
                glm::vec3 b1 = pos_b + axis * hh;
                colliding = collision::CapsuleMesh(
                    b0, b1, radius,
                    pos_a, rot_a, mesh->GetTriangles(),
                    contact);
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Cylinder && shape_b->GetType() == ShapeType::Mesh) {
                auto* cyl = static_cast<CylinderShape*>(shape_a);
                auto* mesh = static_cast<MeshShape*>(shape_b);
                glm::vec3 axis = rot_a * glm::vec3(0, 1, 0);
                float hh = std::max(0.0f, cyl->GetHalfHeight() - cyl->GetRadius());
                glm::vec3 a0 = pos_a - axis * hh;
                glm::vec3 a1 = pos_a + axis * hh;
                colliding = collision::CapsuleMesh(
                    a0, a1, cyl->GetRadius(),
                    pos_b, rot_b, mesh->GetTriangles(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Mesh && shape_b->GetType() == ShapeType::Cylinder) {
                auto* mesh = static_cast<MeshShape*>(shape_a);
                auto* cyl = static_cast<CylinderShape*>(shape_b);
                glm::vec3 axis = rot_b * glm::vec3(0, 1, 0);
                float hh = std::max(0.0f, cyl->GetHalfHeight() - cyl->GetRadius());
                glm::vec3 b0 = pos_b - axis * hh;
                glm::vec3 b1 = pos_b + axis * hh;
                colliding = collision::CapsuleMesh(
                    b0, b1, cyl->GetRadius(),
                    pos_a, rot_a, mesh->GetTriangles(),
                    contact);
                contact.normal = -contact.normal;
            }
            
            if (colliding) {
                stats_.collisions_found++;

                // Record this contact on both bodies so callers can query
                // GetBodyContacts(). On each body we store the normal in the
                // "self → other" direction, so a body lying on a floor sees
                // a contact whose normal points downward — exactly what a
                // grounded check wants.
                Contact contact_for_a = contact;             // normal A→B
                Contact contact_for_b = contact;
                contact_for_b.normal = -contact.normal;      // normal B→A
                body_contacts_[body_a].push_back(contact_for_a);
                body_contacts_[body_b].push_back(contact_for_b);

                // Triggers: still emit a contact so game code can listen,
                // but skip position correction / impulse so bodies overlap.
                if (body_a->IsTrigger() || body_b->IsTrigger()) continue;

                ResolveCollision(body_a, body_b, contact);
            }
        }
    }
    
    void ResolveCollision(RigidBody* body_a, RigidBody* body_b, const Contact& contact) {
        // Movement priority by body type:
        //   Static    — never displaces.
        //   Kinematic — script-driven; pushed out of Static, but does NOT
        //               yield when colliding with Dynamic (a Kinematic player
        //               should not be shoved around by a falling sphere).
        //   Dynamic   — moves whenever the other side is immovable.
        auto type_a = body_a->GetBodyType();
        auto type_b = body_b->GetBodyType();
        if (type_a == BodyType::Static && type_b == BodyType::Static) return;

        auto movable = [](BodyType self, BodyType other) {
            if (self == BodyType::Dynamic) return true;
            // Kinematic only gives ground to Static (e.g., walls/floors).
            if (self == BodyType::Kinematic && other == BodyType::Static) return true;
            return false;
        };
        bool a_movable = movable(type_a, type_b);
        bool b_movable = movable(type_b, type_a);

        glm::vec3 normal = contact.normal;

        // Separate overlapping bodies
        if (contact.separation < 0) {
            float separation_distance = std::abs(contact.separation);
            if (a_movable && b_movable) {
                glm::vec3 offset = normal * separation_distance * 0.5f;
                body_a->SetWorldPosition(body_a->GetWorldPosition() - offset);
                body_b->SetWorldPosition(body_b->GetWorldPosition() + offset);
            } else if (a_movable) {
                body_a->SetWorldPosition(body_a->GetWorldPosition() - normal * separation_distance);
            } else if (b_movable) {
                body_b->SetWorldPosition(body_b->GetWorldPosition() + normal * separation_distance);
            }
        }
        
        // Calculate relative velocity
        glm::vec3 vel_a = body_a->GetVelocity();
        glm::vec3 vel_b = body_b->GetVelocity();
        glm::vec3 relative_velocity = vel_a - vel_b;

        // Skip impulse if bodies are already separating along the normal.
        // With normal A→B, (v_a - v_b)·normal > 0 means A is moving toward
        // B (approaching), so that's the case we WANT to bounce. The
        // previous `>= 0` check returned early on approach, which is why
        // a falling Dynamic body would accumulate velocity until it
        // tunneled through a Static collider in one frame.
        float velocity_along_normal = glm::dot(relative_velocity, normal);
        if (velocity_along_normal <= 0) return;
        
        // Calculate impulse — only Dynamic bodies actually receive it
        // (RigidBody::ApplyImpulse is a no-op for Static/Kinematic). Treat
        // Kinematic as inv_mass=0 so the impulse formula doesn't compute a
        // useless contribution that gets discarded.
        auto inv_mass = [](RigidBody* b) {
            return (b->GetBodyType() == BodyType::Dynamic) ? b->GetInverseMass() : 0.0f;
        };
        float inv_mass_a = inv_mass(body_a);
        float inv_mass_b = inv_mass(body_b);
        float inv_mass_sum = inv_mass_a + inv_mass_b;
        if (inv_mass_sum <= 0.0f) return;   // both immobile in impulse terms

        float restitution = std::min(body_a->GetRestitution(), body_b->GetRestitution());
        float j = -(1.0f + restitution) * velocity_along_normal / inv_mass_sum;
        glm::vec3 impulse = normal * j;
        body_a->ApplyImpulse(impulse);
        body_b->ApplyImpulse(-impulse);

        // Friction impulse (Coulomb model). The relative velocity has a
        // tangential component v_t = v_rel - (v_rel·n)*n. We try to zero
        // that out, but the impulse magnitude is capped by μ|j| — beyond
        // that the surfaces slip. Without this, Dynamic bodies slide
        // indefinitely after they finish bouncing.
        glm::vec3 tangent_vel = relative_velocity - normal * velocity_along_normal;
        float tangent_speed = glm::length(tangent_vel);
        if (tangent_speed > 1e-4f) {
            glm::vec3 tangent = tangent_vel / tangent_speed;
            float jt = -tangent_speed / inv_mass_sum;
            // Combine the two bodies' friction coefficients by geometric mean.
            float mu = std::sqrt(body_a->GetFriction() * body_b->GetFriction());
            float jt_cap = mu * std::abs(j);
            if (jt < -jt_cap) jt = -jt_cap;
            glm::vec3 t_impulse = tangent * jt;
            body_a->ApplyImpulse(t_impulse);
            body_b->ApplyImpulse(-t_impulse);
        }
    }
    
    void SolveConstraints(float delta_time) {
        for (uint32_t iteration = 0; iteration < config_.solver_iterations; ++iteration) {
            for (auto& constraint : constraints_) {
                if (constraint && constraint->IsEnabled()) {
                    constraint->Solve(delta_time);
                }
            }
        }
    }

    // Internal constraint management (not part of public PhysicsWorld interface)
    void AddConstraint(std::shared_ptr<Constraint> constraint) {
        if (constraint) {
            constraints_.push_back(constraint);
        }
    }

    void RemoveConstraint(std::shared_ptr<Constraint> constraint) {
        auto it = std::find(constraints_.begin(), constraints_.end(), constraint);
        if (it != constraints_.end()) {
            constraints_.erase(it);
        }
    }

    void ClearConstraints() {
        constraints_.clear();
    }
};

// Factory function
std::unique_ptr<PhysicsWorld> PhysicsWorld::Create(const PhysicsConfig& config) {
    return std::make_unique<DefaultPhysicsWorld>(config);
}

} // namespace schizo::physics

