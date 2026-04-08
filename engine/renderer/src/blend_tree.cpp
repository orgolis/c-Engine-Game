#include "blend_tree.h"
#include "animation.h"
#include <algorithm>
#include <cmath>

namespace schizo::renderer {

// ============================================================================
// BlendTree
// ============================================================================

BlendTree::BlendTree(const std::string& name, BlendMode mode)
    : name_(name), mode_(mode) {
}

void BlendTree::AddBlendPoint(float param_x, std::shared_ptr<AnimationClip> animation) {
    BlendPoint point;
    point.parameter_x = param_x;
    point.parameter_y = 0.0f;
    point.animation = animation;
    blend_points_.push_back(point);
    
    // Keep sorted
    std::sort(blend_points_.begin(), blend_points_.end(),
              [](const BlendPoint& a, const BlendPoint& b) {
                  return a.parameter_x < b.parameter_x;
              });
}

void BlendTree::AddBlendPoint(float param_x, float param_y, std::shared_ptr<AnimationClip> animation) {
    BlendPoint point;
    point.parameter_x = param_x;
    point.parameter_y = param_y;
    point.animation = animation;
    blend_points_.push_back(point);
}

void BlendTree::GetBlendedAnimations(float param_x, float param_y,
                                    std::vector<std::pair<AnimationClip*, float>>& out_animations) const {
    out_animations.clear();
    
    if (blend_points_.empty()) {
        return;
    }
    
    switch (mode_) {
        case BlendMode::Linear1D:
            GetBlended1D(param_x, out_animations);
            break;
        case BlendMode::Linear2D:
            GetBlended2D(param_x, param_y, out_animations);
            break;
        case BlendMode::Additive:
            // For additive, include all animations with modulated weights
            for (const auto& point : blend_points_) {
                if (point.animation) {
                    out_animations.push_back({point.animation.get(), 0.5f});
                }
            }
            break;
    }
}

void BlendTree::GetBlended1D(float param_x,
                            std::vector<std::pair<AnimationClip*, float>>& out_animations) const {
    if (blend_points_.empty()) {
        return;
    }
    
    // Find adjacent blend points
    int left = -1;
    int right = -1;
    
    for (size_t i = 0; i < blend_points_.size(); ++i) {
        if (blend_points_[i].parameter_x <= param_x) {
            left = i;
        }
        if (blend_points_[i].parameter_x >= param_x && right < 0) {
            right = i;
        }
    }
    
    // Handle edge cases
    if (left < 0 && right >= 0) {
        // Before first point
        if (blend_points_[right].animation) {
            out_animations.push_back({blend_points_[right].animation.get(), 1.0f});
        }
        return;
    }
    
    if (right < 0 && left >= 0) {
        // After last point
        if (blend_points_[left].animation.get()) {
            out_animations.push_back({blend_points_[left].animation.get(), 1.0f});
        }
        return;
    }
    
    if (left == right) {
        // Exactly on a point
        if (blend_points_[left].animation) {
            out_animations.push_back({blend_points_[left].animation.get(), 1.0f});
        }
        return;
    }
    
    // Interpolate between left and right
    float left_param = blend_points_[left].parameter_x;
    float right_param = blend_points_[right].parameter_x;
    float range = right_param - left_param;
    
    if (range > 0.0001f) {
        float t = (param_x - left_param) / range;
        t = std::clamp(t, 0.0f, 1.0f);
        
        if (blend_points_[left].animation) {
            out_animations.push_back({blend_points_[left].animation.get(), 1.0f - t});
        }
        if (blend_points_[right].animation) {
            out_animations.push_back({blend_points_[right].animation.get(), t});
        }
    }
}

void BlendTree::GetBlended2D(float param_x, float param_y,
                            std::vector<std::pair<AnimationClip*, float>>& out_animations) const {
    // Simple 2D blending: find nearest 4 points and blend
    if (blend_points_.size() < 2) {
        GetBlended1D(param_x, out_animations);
        return;
    }
    
    // For simplicity, weight by distance to points
    float total_weight = 0.0f;
    std::vector<std::pair<AnimationClip*, float>> weighted_anims;
    
    for (const auto& point : blend_points_) {
        if (!point.animation) continue;
        
        float dx = point.parameter_x - param_x;
        float dy = point.parameter_y - param_y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        // Inverse distance weighting
        float weight = 1.0f / (distance + 0.1f);
        total_weight += weight;
        weighted_anims.push_back({point.animation.get(), weight});
    }
    
    // Normalize weights
    for (auto& p : weighted_anims) {
        p.second /= total_weight;
        out_animations.push_back(p);
    }
}

// ============================================================================
// LocomotionSystem
// ============================================================================

LocomotionSystem::LocomotionSystem() = default;

AnimationClip* LocomotionSystem::GetCurrentAnimation() const {
    float speed = glm::length(movement_direction_) * movement_speed_;
    
    // Select animation based on speed thresholds
    if (speed < walk_threshold_) {
        return idle_.get();
    } else if (speed < run_threshold_) {
        return walk_.get();
    } else if (speed < sprint_threshold_) {
        return run_.get();
    } else {
        return sprint_.get();
    }
}

} // namespace schizo::renderer
