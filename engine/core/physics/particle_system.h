#pragma once

#include "../math/math.h"
#include <vector>

namespace gws::physics {

// ====================
// Particle System - Lightweight physics simulation
// Generic particles with custom forces and constraints
// ====================

struct Particle {
    gws::math::Vec3 position;
    gws::math::Vec3 velocity;
    float mass;
    float lifetime;
    bool active;
    
    // Custom data for physics calculations
    gws::math::Vec3 force;
    gws::math::Vec3 acceleration;
    
    Particle()
        : position(0.0f, 0.0f, 0.0f),
          velocity(0.0f, 0.0f, 0.0f),
          mass(1.0f),
          lifetime(1.0f),
          active(true),
          force(0.0f, 0.0f, 0.0f),
          acceleration(0.0f, 0.0f, 0.0f) {}
    
    // Verlet integration - more stable than Euler
    void update_verlet(float dt, const gws::math::Vec3& next_acceleration) {
        gws::math::Vec3 new_velocity = velocity + (acceleration + next_acceleration) * (0.5f * dt);
        position += velocity * dt + acceleration * (0.5f * dt * dt);
        velocity = new_velocity;
        acceleration = next_acceleration;
    }
};

// ====================
// Constraint-based Physics
// Used for cloth, rope, chains (better than pure force-based for stability)
// ====================

struct Constraint {
    virtual ~Constraint() = default;
    virtual void solve(Particle& p1, Particle& p2) = 0;
};

// Distance constraint: keep two particles at fixed distance
class DistanceConstraint : public Constraint {
private:
    float rest_distance;
    float stiffness;  // 0-1, how strictly to enforce
    
public:
    DistanceConstraint(float dist, float stiff = 1.0f)
        : rest_distance(dist), stiffness(stiff) {}
    
    void solve(Particle& p1, Particle& p2) override {
        gws::math::Vec3 delta = p2.position - p1.position;
        float current_distance = delta.length();
        
        if (gws::math::approximately_zero(current_distance)) {
            return;
        }
        
        float error = (current_distance - rest_distance);
        gws::math::Vec3 correction_dir = delta.normalized();
        gws::math::Vec3 correction = correction_dir * error * stiffness * 0.5f;
        
        // Move particles apart/together
        p1.position += correction;
        p2.position -= correction;
    }
};

// Bending constraint: for cloth, prevents mesh from folding unnaturally
class BendingConstraint : public Constraint {
private:
    float target_angle;
    float stiffness;
    
public:
    BendingConstraint(float angle, float stiff = 0.5f)
        : target_angle(angle), stiffness(stiff) {}
    
    void solve(Particle& p1, Particle& p2) override {
        // Placeholder for bend constraint implementation
        // In full implementation, would track 4 particles for dihedral angle
    }
};

// ====================
// Chain Solver - For procedural secondary motion (capes, hair, cloth)
// ====================

class ChainSolver {
private:
    std::vector<Particle> particles;
    std::vector<DistanceConstraint> constraints;
    
public:
    ChainSolver(int num_links, float segment_length, float mass_per_link = 1.0f) {
        particles.resize(num_links);
        for (int i = 0; i < num_links; ++i) {
            particles[i].mass = mass_per_link;
            particles[i].position = gws::math::Vec3(0.0f, -i * segment_length, 0.0f);
        }
        
        // Create distance constraints between adjacent links
        for (int i = 0; i < num_links - 1; ++i) {
            constraints.emplace_back(segment_length, 1.0f);
        }
    }
    
    // Update chain physics with gravity and other forces
    void update(float dt, const gws::math::Vec3& gravity, const gws::math::Vec3& wind = gws::math::Vec3(0.0f)) {
        // Gravity + wind forces
        for (auto& p : particles) {
            if (!p.active) continue;
            p.force = (gravity + wind) * p.mass;
            p.acceleration = p.force / p.mass;
        }
        
        // Update positions (Verlet)
        for (size_t i = 1; i < particles.size(); ++i) {
            particles[i].update_verlet(dt, particles[i].acceleration);
        }
        
        // Constraint solving (multiple iterations for stability)
        for (int iteration = 0; iteration < 3; ++iteration) {
            for (size_t i = 0; i < constraints.size(); ++i) {
                constraints[i].solve(particles[i], particles[i + 1]);
            }
        }
        
        // Damping to prevent endless oscillation
        for (auto& p : particles) {
            p.velocity *= 0.99f;
        }
    }
    
    const std::vector<Particle>& get_particles() const { return particles; }
    
    // Set root position (first link follows this)
    void set_root_position(const gws::math::Vec3& pos) {
        if (!particles.empty()) {
            particles[0].position = pos;
            particles[0].velocity = gws::math::Vec3(0.0f, 0.0f, 0.0f);
        }
    }
};

}  // namespace gws::physics
