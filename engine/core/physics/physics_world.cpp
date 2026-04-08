#include "physics_world.h"
#include "rigidbody.h"
#include "collision.h"
#include <algorithm>
#include <cmath>
#include <chrono>

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
        
        // Apply gravity to all dynamic bodies
        for (auto* body : rigid_bodies_) {
            if (body && body->GetBodyType() == BodyType::Dynamic) {
                body->ApplyForce(gravity_ * body->GetMass());
            }
        }
        
        // Broad phase: collect potential collisions
        std::vector<std::pair<RigidBody*, RigidBody*>> potential_collisions;
        BroadPhase(potential_collisions);
        
        // Narrow phase: test actual collisions
        PerformCollisionDetection(potential_collisions);
        
        // Integrate velocities and positions
        for (auto* body : rigid_bodies_) {
            if (body) {
                body->OnFixedUpdate(delta_time);
            }
        }
        
        // Apply constraints
        SolveConstraints(delta_time);
        
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
            
            // Simple sphere test for ray-sphere intersection
            glm::vec3 bounds_center = body->GetWorldBoundsCenter();
            float bounds_radius = body->GetBoundsRadius();
            
            // Ray-sphere intersection formula
            glm::vec3 oc = origin - bounds_center;
            
            float a = glm::dot(ray_dir, ray_dir);
            float b = 2.0f * glm::dot(oc, ray_dir);
            float c = glm::dot(oc, oc) - bounds_radius * bounds_radius;
            float discriminant = b * b - 4 * a * c;
            
            if (discriminant >= 0) {
                float t = (-b - std::sqrt(discriminant)) / (2 * a);
                if (t > 0 && t < closest_distance) {
                    closest_distance = t;
                    closest_body = body;
                    closest_point = origin + ray_dir * t;
                }
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
    void BroadPhase(std::vector<std::pair<RigidBody*, RigidBody*>>& potential_pairs) {
        if (!config_.enable_broad_phase) {
            // If broad phase disabled, test all pairs (slow but simple)
            for (size_t i = 0; i < rigid_bodies_.size(); ++i) {
                for (size_t j = i + 1; j < rigid_bodies_.size(); ++j) {
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
                    pos_b, box->GetHalfExtents(),
                    contact);
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Sphere) {
                auto* box = static_cast<BoxShape*>(shape_a);
                auto* sphere = static_cast<SphereShape*>(shape_b);
                colliding = collision::SphereBox(
                    pos_b, sphere->GetRadius(),
                    pos_a, box->GetHalfExtents(),
                    contact);
                // Swap data since we switched bodies
                contact.normal = -contact.normal;
            } else if (shape_a->GetType() == ShapeType::Box && shape_b->GetType() == ShapeType::Box) {
                auto* box_a = static_cast<BoxShape*>(shape_a);
                auto* box_b = static_cast<BoxShape*>(shape_b);
                colliding = collision::BoxBox(
                    pos_a, box_a->GetHalfExtents(),
                    pos_b, box_b->GetHalfExtents(),
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
                // Swap data since we switched bodies
                contact.normal = -contact.normal;
            }
            
            if (colliding) {
                stats_.collisions_found++;
                
                // Store collision info for later resolution
                ResolveCollision(body_a, body_b, contact);
            }
        }
    }
    
    void ResolveCollision(RigidBody* body_a, RigidBody* body_b, const Contact& contact) {
        // Static bodies don't move
        bool a_is_static = body_a->GetBodyType() == BodyType::Static;
        bool b_is_static = body_b->GetBodyType() == BodyType::Static;
        
        if (a_is_static && b_is_static) return;
        
        glm::vec3 normal = contact.normal;
        
        // Separate overlapping bodies
        if (contact.separation < 0) {
            float separation_distance = std::abs(contact.separation);
            
            if (!a_is_static && !b_is_static) {
                glm::vec3 offset = normal * separation_distance * 0.5f;
                body_a->SetWorldPosition(body_a->GetWorldPosition() - offset);
                body_b->SetWorldPosition(body_b->GetWorldPosition() + offset);
            } else if (!a_is_static) {
                body_a->SetWorldPosition(body_a->GetWorldPosition() - normal * separation_distance);
            } else if (!b_is_static) {
                body_b->SetWorldPosition(body_b->GetWorldPosition() + normal * separation_distance);
            }
        }
        
        // Calculate relative velocity
        glm::vec3 vel_a = body_a->GetVelocity();
        glm::vec3 vel_b = body_b->GetVelocity();
        glm::vec3 relative_velocity = vel_a - vel_b;
        
        // Don't resolve if velocities are separating
        float velocity_along_normal = glm::dot(relative_velocity, normal);
        if (velocity_along_normal >= 0) return;
        
        // Calculate impulse
        float restitution = std::min(body_a->GetRestitution(), body_b->GetRestitution());
        float inv_mass_a = a_is_static ? 0 : body_a->GetInverseMass();
        float inv_mass_b = b_is_static ? 0 : body_b->GetInverseMass();
        
        float j = -(1.0f + restitution) * velocity_along_normal / (inv_mass_a + inv_mass_b);
        
        // Apply impulse
        glm::vec3 impulse = normal * j;
        body_a->ApplyImpulse(impulse);
        body_b->ApplyImpulse(-impulse);
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

