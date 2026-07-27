#pragma once

// ============================================================================
// PathFollower — steer an agent along a navmesh path corridor. (AI Stage 9.)
// Holds a path (from NavMesh::find_path); each tick returns a desired
// horizontal velocity toward the current waypoint, advances the waypoint on
// arrival, and reports the facing yaw + an `arrived` flag when the goal is
// reached. Header-only, pure CPU. The caller integrates position (via the
// character controller or directly) and feeds `speed` to the anim state
// machine. Slowing near the goal ("arrive" behaviour) avoids overshoot.
// ============================================================================

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace schizo::ai {

class PathFollower {
public:
    struct Steer {
        glm::vec3 velocity{0.0f};  // desired world horizontal velocity (y=0)
        float     speed  = 0.0f;   // |velocity| (feed to the locomotion blend)
        float     yaw    = 0.0f;   // facing angle atan2(dir.x, dir.z)
        bool      moving = false;  // has a meaningful direction this tick
        bool      arrived = false; // reached the final waypoint
    };

    void set_path(std::vector<glm::vec3> path) {
        path_ = std::move(path);
        index_ = path_.empty() ? 0 : 1;   // 0 == start (current pos); head to 1
        arrived_ = path_.size() < 2;
    }
    void clear() { path_.clear(); index_ = 0; arrived_ = true; }
    bool active() const { return !arrived_ && index_ < path_.size(); }
    bool arrived() const { return arrived_; }
    const std::vector<glm::vec3>& path() const { return path_; }

    void set_arrive_radius(float r) { arrive_radius_ = r; }
    void set_slowdown_radius(float r) { slowdown_radius_ = r; }

    /// Compute desired velocity toward the current waypoint at up to `max_speed`.
    /// Advances the waypoint when within `arrive_radius`; eases to a stop within
    /// `slowdown_radius` of the FINAL waypoint. `keep_yaw` is returned when
    /// there's no movement direction (so facing doesn't snap to 0).
    Steer steer(const glm::vec3& pos, float max_speed, float keep_yaw = 0.0f) {
        Steer s; s.yaw = keep_yaw;
        if (arrived_ || index_ >= path_.size()) { arrived_ = true; return s; }

        // Advance through any waypoints we're already within arrive_radius of.
        while (index_ < path_.size()) {
            glm::vec3 to = path_[index_] - pos; to.y = 0.0f;
            if (glm::length(to) > arrive_radius_) break;
            ++index_;
        }
        if (index_ >= path_.size()) { arrived_ = true; return s; }

        glm::vec3 to = path_[index_] - pos; to.y = 0.0f;
        const float dist = glm::length(to);
        if (dist < 1e-4f) return s;
        const glm::vec3 dir = to / dist;

        // Ease down only for the final leg.
        float speed = max_speed;
        const bool last_leg = (index_ + 1 >= path_.size());
        if (last_leg && dist < slowdown_radius_)
            speed = max_speed * (dist / slowdown_radius_);

        s.velocity = dir * speed;
        s.speed    = speed;
        s.yaw      = std::atan2(dir.x, dir.z);
        s.moving   = speed > 1e-3f;
        return s;
    }

private:
    std::vector<glm::vec3> path_;
    size_t index_ = 0;
    bool   arrived_ = true;
    float  arrive_radius_   = 0.35f;
    float  slowdown_radius_ = 0.8f;
};

} // namespace schizo::ai
