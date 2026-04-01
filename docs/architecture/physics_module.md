# Advanced Physics Module Architecture

## Overview

The physics module is a **separate, optional layer** built on top of the core math library. This architecture ensures:

1. **Zero-cost abstraction** — core math doesn't know about physics, so non-physics code has zero overhead
2. **Composable systems** — physics features can be mixed and matched
3. **Performance flexibility** — complex simulations can be compiled separately or deferred

## Module Structure

```
engine/core/
├── math/                    ← Pure mathematics (Vec3, Mat4, Quaternion, etc.)
│   ├── vec3.h, vec4.h, etc.
│   └── math.cpp
│
└── physics/                 ← Advanced physics (optional, depends on math)
    ├── vector_field.h       ← Field abstractions & implementations
    ├── numerical_calculus.h ← Gradient, divergence, curl, etc.
    ├── pde_solver.h         ← Diffusion, wave, advection-diffusion
    ├── particle_system.h    ← Particles, constraints, chains
    └── physics.h            ← Aggregated header
```

## Dependency Graph

```
Core Math Module (no deps)
        ↑
Physics Module (depends on core math only)
        ↑
Game Code (can optionally use physics)
```

**Key principle:** Math ≠ Physics. Physics is pure flavor; the engine works perfectly without it.

## Core Concepts

### Vector Fields (vector_field.h)

Represents any function F: ℝ³ → ℝ³ (position → force/velocity).

**Use cases:**
- Wind/gravity affecting particles
- Electromagnetic forces
- Vortex effects (tornados, whirlpools)
- Data-driven environmental forces loaded from files

**Examples:**

```cpp
// Constant wind
auto wind = std::make_shared<gws::physics::ConstantField>(gws::math::Vec3(5.0f, 0.0f, 0.0f));

// Radial gravity (black hole)
auto gravity = std::make_shared<gws::physics::RadialField>(
    gws::math::Vec3(0.0f, -10.0f, 0.0f),  // center
    9.81f,                                  // strength
    2.0f,                                   // falloff exponent
    true                                    // attractive
);

// Composite: wind + gravity
gws::physics::CompositeField environment;
environment.add_field(wind, 1.0f);
environment.add_field(gravity, 1.0f);
gws::math::Vec3 total_force = environment.evaluate(position);
```

### Numerical Calculus (numerical_calculus.h)

Finite-difference approximations for derivatives.

**Operations:**
- `gradient(f)` — ∇f (steepest ascent direction)
- `divergence(F)` — ∇·F (spreading/compression)
- `curl(F)` — ∇×F (rotation)
- `laplacian(f)` — ∇²f (diffusion operator)

**Use case example — Custom force field from potential energy:**

```cpp
// Define a potential energy function
auto potential = [](const gws::math::Vec3& pos) {
    return pos.length_squared() * 0.5f;  // quadratic potential
};

// Get the force field F = -∇φ
gws::math::Vec3 force = gws::physics::NumericalCalculus::gradient(potential, particle_pos);

// The force naturally points downhill in the potential
```

### PDE Solvers (pde_solver.h)

Simulate partial differential equations for continuous phenomena.

**Implemented:**
- **Diffusion** (heat equation): ∂u/∂t = α∇²u
  - Smoke spreading, temperature, density diffusion
- **Wave equation**: ∂²u/∂t² = c²∇²u
  - Water ripples, vibrating surfaces, cloth resonance
- **Advection-Diffusion**: ∂u/∂t + v·∇u = α∇²u
  - Dye in flowing water, temperature in wind

**Example — Diffusing heat:**

```cpp
gws::physics::DiffusionSolver heat_sim(0.1f, 0.01f, 100);
std::vector<float> temperature = initial_temp_grid;

// Each frame
temperature = heat_sim.step(delta_time, temperature);
```

### Particle Systems (particle_system.h)

Verlet-based physics for performance and stability.

**Components:**
- `Particle` — Position, velocity, mass, forces, constraints
- `Constraint` — Enforce relationships (distance, bending)
- `ChainSolver` — Multi-link chains (capes, hair, fabric)

**Example — Cape simulation (procedural secondary motion):**

```cpp
// Create a chain: 20 links, 0.1 units apart, 0.5 mass each
gws::physics::ChainSolver cape(20, 0.1f, 0.5f);

// Each frame
gws::math::Vec3 gravity(0.0f, -9.81f, 0.0f);
gws::math::Vec3 wind(2.0f, 0.0f, 0.0f);

cape.set_root_position(character_shoulder_pos);
cape.update(delta_time, gravity, wind);

// Extract chain positions for rendering
const auto& particles = cape.get_particles();
for (size_t i = 0; i < particles.size(); ++i) {
    render_bone(particles[i].position);
}
```

## Integration Patterns

### Pattern 1: Standalone Physics (No Coupling)

Use physics without affecting core engine:

```cpp
// game.cpp
#include "engine/core/physics/physics.h"

class ParticleEffect {
    gws::physics::ChainSolver cloth;
    
public:
    void update(float dt) {
        cloth.update(dt, gravity, wind);
    }
};
```

**Performance:** Zero overhead to non-physics systems.

### Pattern 2: Physics-Aware Game Logic

Physics outputs affect game behavior:

```cpp
// Combat system reads particle simulation
class CharacterRagdoll {
    std::vector<gws::physics::Particle> bones;
    
public:
    bool is_grounded() {
        // Check if particles near feet collide with ground
        return ground_distance < threshold;
    }
};
```

### Pattern 3: Field-Driven Gameplay

Environmental fields influence mechanics:

```cpp
// World has a persistent wind field
gws::physics::CompositeField weather;

// Query in movement controller
gws::math::Vec3 wind_force = weather.evaluate(character_position);
character_velocity += wind_force * dt;
```

## Performance Considerations

### Memory

- **Vector fields**: Virtual function calls + shared_ptr overhead
  - Mitigation: Cache field evaluations, reuse results
- **PDE solvers**: Grid-based, ~O(N) memory per grid cell
  - Mitigation: Use coarse grids, limit solver resolution
- **Particles**: ~80 bytes per particle (position, velocity, mass, etc.)
  - Mitigation: Use object pools, update only active particles

### CPU

- **Numerical derivatives**: 6-8 function evaluations per derivative
  - Mitigation: Use larger epsilon for speed, smaller for accuracy trade-off
- **Constraint solving**: Iterative, typically 3-5 iterations per frame
  - Mitigation: Use Gauss-Seidel ordering for convergence
- **PDE solving**: O(N) per step, implicit solvers are O(N³) (rare)
  - Mitigation: Use explicit solvers (faster, less stable), adaptive time-stepping

### Optimization Opportunities (Deferred to Phase 3+)

1. **SIMD vectorization** — Pack 4 particles per SIMD lane
2. **GPU compute** — Off-load grids to compute shaders
3. **Spatial hashing** — Fast constraint lookups
4. **BVH acceleration** — Neighbor queries for particles
5. **Compiled PDE kernels** — Generate C++ code from solver specs

## When to Use What

| System | Use When | Avoid When |
|--------|----------|-----------|
| **Vector Fields** | Environmental forces, data-driven effects | Simple constant forces (use Vec3 directly) |
| **Calculus** | Need derivatives of custom functions | Pre-computed gradients exist |
| **PDE Solvers** | Continuous phenomena (heat, waves, smoke) | Single-frame effects (use particles) |
| **Particles** | Secondary motion, cloth, hair, ragdoll | Rigid body physics (use Jolt in Phase 7) |
| **Constraints** | Cloth, rope, chains with natural deformation | Exact geometric constraints (use IK) |

## Future Expansion (Deferred)

- **Phase 7 (Physics):** Integrate Jolt Physics for rigid bodies
  - Rigid bodies coexist with particle systems
  - Fields can apply forces to both
- **Phase 3 (Renderer):** Visualize fields as glyphs/streamlines
- **Phase 5 (Editor):** GUI to compose and tune fields in-engine

## No Performance Penalty for Unlisted Features

If you don't use:
- Vector fields → Zero overhead (header-only abstractions, zero virtual calls)
- PDE solvers → Zero overhead (not linked unless explicitly used)
- Particle constraints → Zero overhead (only instantiated when created)

The core math library remains **pristine and fast**, and physics features are **entirely optional**.
