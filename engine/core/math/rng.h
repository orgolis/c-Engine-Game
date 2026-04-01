#pragma once

#include <cstdint>

namespace gws::math {

// ====================
// Deterministic Random Number Generator - Xorshift32
// Fast, lightweight, seeded for reproducibility
// Critical for: procedural generation, rollback netcode, replicated gameplay
// ====================

class Xorshift32 {
private:
    uint32_t state;
    
public:
    // Construct with seed (0 defaults to 1)
    explicit Xorshift32(uint32_t seed = 1u) : state(seed ? seed : 1u) {}
    
    // Reseed the generator
    void seed(uint32_t s) {
        state = s ? s : 1u;
    }
    
    // Get current seed
    uint32_t get_seed() const {
        return state;
    }
    
    // Generate next 32-bit unsigned integer
    uint32_t next_uint() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    
    // Generate next float in [0, 1)
    float next_float() {
        return (next_uint() >> 8) * (1.0f / 16777216.0f);
    }
    
    // Generate next float in [min, max)
    float next_float(float min, float max) {
        return min + next_float() * (max - min);
    }
    
    // Generate next integer in [0, max)
    uint32_t next_uint(uint32_t max) {
        if (max <= 1) return 0;
        return next_uint() % max;
    }
    
    // Generate next integer in [min, max)
    int next_int(int min, int max) {
        if (min >= max) return min;
        return min + static_cast<int>(next_uint(max - min));
    }
    
    // Generate next bool with given probability of true (0-1)
    bool next_bool(float probability = 0.5f) {
        return next_float() < probability;
    }
};

// ====================
// Global Default RNG (optional convenience)
// Thread-safe with thread-local storage in production
// ====================

// Create a global RNG instance for general use
inline Xorshift32& default_rng() {
    static Xorshift32 rng;
    return rng;
}

}  // namespace gws::math
