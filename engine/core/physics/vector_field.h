#pragma once

#include "../math/math.h"
#include <functional>
#include <memory>
#include <vector>

namespace gws::physics {

// ====================
// Vector Fields - Abstract representation of 3D vector functions
// F: R³ → R³ (position → force/velocity/etc)
// ====================

// Base interface for any evaluable vector field
class VectorField {
public:
    virtual ~VectorField() = default;
    
    // Evaluate the field at a point in 3D space
    virtual gws::math::Vec3 evaluate(const gws::math::Vec3& position) const = 0;
    
    // Optional: compute divergence ∇·F (rate of flux expansion)
    // If not overridden, uses numerical approximation
    virtual float divergence(const gws::math::Vec3& position, float epsilon = 1e-4f) const;
    
    // Optional: compute curl ∇×F (rotational component)
    virtual gws::math::Vec3 curl(const gws::math::Vec3& position, float epsilon = 1e-4f) const;
};

// ====================
// Scalar Fields - R³ → R (for potential functions, density, etc.)
// ====================

class ScalarField {
public:
    virtual ~ScalarField() = default;
    
    // Evaluate the scalar field at a point
    virtual float evaluate(const gws::math::Vec3& position) const = 0;
    
    // Compute gradient ∇f (points in direction of steepest increase)
    virtual gws::math::Vec3 gradient(const gws::math::Vec3& position, float epsilon = 1e-4f) const;
    
    // Compute Laplacian ∇²f (diffusion operator)
    virtual float laplacian(const gws::math::Vec3& position, float epsilon = 1e-4f) const;
};

// ====================
// Concrete Field Implementations
// ====================

// Constant vector field (uniform force, wind, etc.)
class ConstantField : public VectorField {
private:
    gws::math::Vec3 value;
    
public:
    explicit ConstantField(const gws::math::Vec3& v) : value(v) {}
    
    gws::math::Vec3 evaluate(const gws::math::Vec3& position) const override {
        return value;
    }
    
    // Analytical: constant field has zero divergence and curl
    float divergence(const gws::math::Vec3& position, float epsilon = 1e-4f) const override {
        return 0.0f;
    }
    
    gws::math::Vec3 curl(const gws::math::Vec3& position, float epsilon = 1e-4f) const override {
        return gws::math::Vec3(0.0f, 0.0f, 0.0f);
    }
};

// Radial field (gravity, repulsion, attraction)
// F(r) = strength / |r|^n in direction of r or -r
class RadialField : public VectorField {
private:
    gws::math::Vec3 center;
    float strength;
    float falloff;  // exponent for distance decay
    bool attractive;  // true = toward center, false = away
    
public:
    RadialField(const gws::math::Vec3& c, float s, float f = 2.0f, bool att = true)
        : center(c), strength(s), falloff(f), attractive(att) {}
    
    gws::math::Vec3 evaluate(const gws::math::Vec3& position) const override {
        gws::math::Vec3 delta = position - center;
        float dist_sq = delta.length_squared();
        
        if (gws::math::approximately_zero(dist_sq)) {
            return gws::math::Vec3(0.0f, 0.0f, 0.0f);
        }
        
        float dist = std::sqrt(dist_sq);
        float mag = strength / std::pow(dist, falloff);
        gws::math::Vec3 direction = delta.normalized();
        
        return attractive ? -direction * mag : direction * mag;
    }
};

// Gradient field: derived from a scalar potential
// F(p) = -∇φ(p) (or +∇φ depending on convention)
class GradientField : public VectorField {
private:
    std::shared_ptr<const ScalarField> potential;
    float epsilon;
    
public:
    GradientField(std::shared_ptr<const ScalarField> pot, float eps = 1e-4f)
        : potential(pot), epsilon(eps) {}
    
    virtual ~GradientField() = default;
    
    gws::math::Vec3 evaluate(const gws::math::Vec3& position) const override {
        if (!potential) {
            return gws::math::Vec3(0.0f, 0.0f, 0.0f);
        }
        return potential->gradient(position, epsilon);
    }
};

// Vorticity field: rotation around an axis
// Used for tornadoes, whirlpools, swirling wind
class VorticityField : public VectorField {
private:
    gws::math::Vec3 axis;  // rotation axis (should be normalized)
    float angular_velocity;
    gws::math::Vec3 center;  // center of rotation
    
public:
    VorticityField(const gws::math::Vec3& ax, float omega, const gws::math::Vec3& c = gws::math::Vec3(0.0f))
        : axis(ax.normalized()), angular_velocity(omega), center(c) {}
    
    gws::math::Vec3 evaluate(const gws::math::Vec3& position) const override {
        gws::math::Vec3 delta = position - center;
        // v = ω × r
        gws::math::Vec3 omega_vec = axis * angular_velocity;
        return omega_vec.cross(delta);
    }
};

// ====================
// Field Composition - Combine multiple fields
// ====================

class CompositeField : public VectorField {
private:
    std::vector<std::shared_ptr<VectorField>> fields;
    std::vector<float> weights;
    
public:
    CompositeField() = default;
    
    virtual ~CompositeField() = default;
    
    // Add a field with optional weight
    void add_field(std::shared_ptr<VectorField> field, float weight = 1.0f) {
        fields.push_back(field);
        weights.push_back(weight);
    }
    
    gws::math::Vec3 evaluate(const gws::math::Vec3& position) const override {
        gws::math::Vec3 result(0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < fields.size(); ++i) {
            result += fields[i]->evaluate(position) * weights[i];
        }
        return result;
    }
};

}  // namespace gws::physics
