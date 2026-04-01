#pragma once

#include "../math/math.h"
#include <functional>

namespace gws::physics {

// ====================
// Numerical Calculus Utilities
// ====================

class NumericalCalculus {
public:
    // Finite difference approximations for derivative operations
    
    // First derivative along x-axis (∂f/∂x)
    static float partial_x(
        const std::function<float(const gws::math::Vec3&)>& f,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        float f_plus = f(pos + gws::math::Vec3(epsilon, 0.0f, 0.0f));
        float f_minus = f(pos - gws::math::Vec3(epsilon, 0.0f, 0.0f));
        return (f_plus - f_minus) / (2.0f * epsilon);
    }
    
    // First derivative along y-axis (∂f/∂y)
    static float partial_y(
        const std::function<float(const gws::math::Vec3&)>& f,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        float f_plus = f(pos + gws::math::Vec3(0.0f, epsilon, 0.0f));
        float f_minus = f(pos - gws::math::Vec3(0.0f, epsilon, 0.0f));
        return (f_plus - f_minus) / (2.0f * epsilon);
    }
    
    // First derivative along z-axis (∂f/∂z)
    static float partial_z(
        const std::function<float(const gws::math::Vec3&)>& f,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        float f_plus = f(pos + gws::math::Vec3(0.0f, 0.0f, epsilon));
        float f_minus = f(pos - gws::math::Vec3(0.0f, 0.0f, epsilon));
        return (f_plus - f_minus) / (2.0f * epsilon);
    }
    
    // Gradient of a scalar field: ∇f = [∂f/∂x, ∂f/∂y, ∂f/∂z]
    static gws::math::Vec3 gradient(
        const std::function<float(const gws::math::Vec3&)>& f,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        return gws::math::Vec3(
            partial_x(f, pos, epsilon),
            partial_y(f, pos, epsilon),
            partial_z(f, pos, epsilon)
        );
    }
    
    // Laplacian of a scalar: ∇²f = ∂²f/∂x² + ∂²f/∂y² + ∂²f/∂z²
    // Used in diffusion, heat equation, etc.
    static float laplacian(
        const std::function<float(const gws::math::Vec3&)>& f,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        float eps_sq = epsilon * epsilon;
        float center = f(pos);
        
        float xx = (f(pos + gws::math::Vec3(epsilon, 0.0f, 0.0f)) +
                    f(pos - gws::math::Vec3(epsilon, 0.0f, 0.0f)) - 2.0f * center) / eps_sq;
        float yy = (f(pos + gws::math::Vec3(0.0f, epsilon, 0.0f)) +
                    f(pos - gws::math::Vec3(0.0f, epsilon, 0.0f)) - 2.0f * center) / eps_sq;
        float zz = (f(pos + gws::math::Vec3(0.0f, 0.0f, epsilon)) +
                    f(pos - gws::math::Vec3(0.0f, 0.0f, epsilon)) - 2.0f * center) / eps_sq;
        
        return xx + yy + zz;
    }
    
    // Divergence of a vector field: ∇·F = ∂F_x/∂x + ∂F_y/∂y + ∂F_z/∂z
    // Measures how much field "spreads out" from a point
    static float divergence(
        const std::function<gws::math::Vec3(const gws::math::Vec3&)>& F,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        gws::math::Vec3 F_pos = F(pos);
        
        // ∂F_x/∂x
        float dFx_dx = (F(pos + gws::math::Vec3(epsilon, 0.0f, 0.0f)).x -
                        F(pos - gws::math::Vec3(epsilon, 0.0f, 0.0f)).x) / (2.0f * epsilon);
        
        // ∂F_y/∂y
        float dFy_dy = (F(pos + gws::math::Vec3(0.0f, epsilon, 0.0f)).y -
                        F(pos - gws::math::Vec3(0.0f, epsilon, 0.0f)).y) / (2.0f * epsilon);
        
        // ∂F_z/∂z
        float dFz_dz = (F(pos + gws::math::Vec3(0.0f, 0.0f, epsilon)).z -
                        F(pos - gws::math::Vec3(0.0f, 0.0f, epsilon)).z) / (2.0f * epsilon);
        
        return dFx_dx + dFy_dy + dFz_dz;
    }
    
    // Curl of a vector field: ∇×F = [∂F_z/∂y - ∂F_y/∂z, ∂F_x/∂z - ∂F_z/∂x, ∂F_y/∂x - ∂F_x/∂y]
    // Measures rotational component of the field
    static gws::math::Vec3 curl(
        const std::function<gws::math::Vec3(const gws::math::Vec3&)>& F,
        const gws::math::Vec3& pos,
        float epsilon = 1e-4f
    ) {
        // Partial derivatives
        float dFz_dy = (F(pos + gws::math::Vec3(0.0f, epsilon, 0.0f)).z -
                        F(pos - gws::math::Vec3(0.0f, epsilon, 0.0f)).z) / (2.0f * epsilon);
        float dFy_dz = (F(pos + gws::math::Vec3(0.0f, 0.0f, epsilon)).y -
                        F(pos - gws::math::Vec3(0.0f, 0.0f, epsilon)).y) / (2.0f * epsilon);
        
        float dFx_dz = (F(pos + gws::math::Vec3(0.0f, 0.0f, epsilon)).x -
                        F(pos - gws::math::Vec3(0.0f, 0.0f, epsilon)).x) / (2.0f * epsilon);
        float dFz_dx = (F(pos + gws::math::Vec3(epsilon, 0.0f, 0.0f)).z -
                        F(pos - gws::math::Vec3(epsilon, 0.0f, 0.0f)).z) / (2.0f * epsilon);
        
        float dFy_dx = (F(pos + gws::math::Vec3(epsilon, 0.0f, 0.0f)).y -
                        F(pos - gws::math::Vec3(epsilon, 0.0f, 0.0f)).y) / (2.0f * epsilon);
        float dFx_dy = (F(pos + gws::math::Vec3(0.0f, epsilon, 0.0f)).x -
                        F(pos - gws::math::Vec3(0.0f, epsilon, 0.0f)).x) / (2.0f * epsilon);
        
        return gws::math::Vec3(
            dFz_dy - dFy_dz,
            dFx_dz - dFz_dx,
            dFy_dx - dFx_dy
        );
    }
};

}  // namespace gws::physics
