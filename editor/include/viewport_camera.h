#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <algorithm>
#include <utility>

namespace schizo::editor {

// ============================================================================
// Editor Viewport Camera
// ============================================================================

/**
 * @class ViewportCamera
 * @brief Simple camera for the editor viewport
 */
class ViewportCamera {
public:
    ViewportCamera() = default;
    ~ViewportCamera() = default;
    
    /**
     * @brief Set camera position
     */
    void SetPosition(const glm::vec3& position) {
        position_ = position;
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Get camera position
     */
    const glm::vec3& GetPosition() const { return position_; }
    
    /**
     * @brief Get camera yaw angle (in degrees)
     */
    float GetYaw() const { return yaw_; }
    
    /**
     * @brief Get camera pitch angle (in degrees)
     */
    float GetPitch() const { return pitch_; }
    
    /**
     * @brief Pan camera (move in X-Y plane)
     */
    void Pan(float dx, float dy) {
        // Move relative to camera's right and up vectors
        position_ += right_ * dx * pan_speed_;
        position_ += up_ * dy * pan_speed_;
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Zoom camera (move along look direction)
     */
    void Zoom(float amount) {
        position_ += front_ * amount * zoom_speed_;
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Rotate camera around target point
     */
    void Rotate(float yaw, float pitch) {
        yaw_   += yaw * rotation_speed_;
        pitch_ += pitch * rotation_speed_;
        
        // Clamp pitch to avoid gimbal lock
        if (pitch_ > 89.0f) pitch_ = 89.0f;
        if (pitch_ < -89.0f) pitch_ = -89.0f;
        
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Get view matrix
     */
    glm::mat4 GetViewMatrix() const { return view_; }
    
    /**
     * @brief Get projection matrix
     */
    glm::mat4 GetProjectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(fov_), aspect, 0.1f, 1000.0f);
    }
    
    /**
     * @brief Reset camera to default position
     */
    void Reset() {
        position_ = glm::vec3(0.0f, 1.0f, 5.0f);
        yaw_ = -90.0f;
        pitch_ = -11.5f;  // Corrected pitch to point toward origin
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Test ray-AABB intersection
     * @return Distance to intersection, -1 if no intersection
     */
    static float RayAABBIntersection(const glm::vec3& ray_origin, const glm::vec3& ray_direction,
                                     const glm::vec3& aabb_min, const glm::vec3& aabb_max) {
        const float epsilon = 0.0001f;
        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::max();
        
        for (int i = 0; i < 3; i++) {
            // Check if ray is parallel to slab
            if (std::abs(ray_direction[i]) < epsilon) {
                if (ray_origin[i] < aabb_min[i] || ray_origin[i] > aabb_max[i]) {
                    return -1.0f;
                }
            } else {
                float inv_d = 1.0f / ray_direction[i];
                float t0 = (aabb_min[i] - ray_origin[i]) * inv_d;
                float t1 = (aabb_max[i] - ray_origin[i]) * inv_d;
                
                if (inv_d < 0.0f) std::swap(t0, t1);
                
                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
                
                if (tmax < tmin) return -1.0f;
            }
        }
        
        return tmin > 0.0001f ? tmin : -1.0f;
    }
    
    /**
     * @brief Move camera in local directions (for flythrough mode)
     * @param forward Forward movement (positive = forward, negative = backward)
     * @param right Right movement (positive = right, negative = left)
     * @param up Up movement (positive = up, negative = down)
     */
    void MoveLocal(float forward, float right, float up) {
        position_ += front_ * forward * movement_speed_;
        position_ += right_ * right * movement_speed_;
        position_ += up * glm::vec3(0.0f, 1.0f, 0.0f) * movement_speed_;
        RecalculateViewMatrix();
    }
    
    /**
     * @brief Set flythrough movement speed
     */
    void SetMovementSpeed(float speed) { movement_speed_ = speed; }
    
    /**
     * @brief Get flythrough movement speed
     */
    float GetMovementSpeed() const { return movement_speed_; }
    
    /**
     * @brief Get a ray through screen coordinates for picking
     * @param screen_x Screen X coordinate (0 to viewport_width)
     * @param screen_y Screen Y coordinate (0 to viewport_height)
     * @param viewport_width Screen/viewport width
     * @param viewport_height Screen/viewport height
     * @return Ray origin and direction (normalized)
     */
    std::pair<glm::vec3, glm::vec3> GetPickingRay(float screen_x, float screen_y, 
                                                   float viewport_width, float viewport_height) const {
        // Normalize screen coordinates to NDC [-1, 1]
        float ndc_x = (2.0f * screen_x) / viewport_width - 1.0f;
        float ndc_y = 1.0f - (2.0f * screen_y) / viewport_height;
        
        // Calculate aspect ratio
        float aspect = viewport_width / viewport_height;
        
        // Get projection matrix with same aspect ratio
        glm::mat4 proj = glm::perspective(glm::radians(fov_), aspect, 0.1f, 1000.0f);
        
        // Inverse transformation: NDC -> eye space -> world space
        glm::mat4 inv_proj = glm::inverse(proj);
        glm::mat4 inv_view = glm::inverse(GetViewMatrix());
        
        // Create NDC ray endpoints
        glm::vec4 ray_ndc_near(ndc_x, ndc_y, -1.0f, 1.0f);
        glm::vec4 ray_ndc_far(ndc_x, ndc_y, 1.0f, 1.0f);
        
        // Transform to eye space
        glm::vec4 ray_eye_near = inv_proj * ray_ndc_near;
        ray_eye_near /= ray_eye_near.w;
        
        glm::vec4 ray_eye_far = inv_proj * ray_ndc_far;
        ray_eye_far /= ray_eye_far.w;
        
        // Transform to world space
        glm::vec4 ray_world_near = inv_view * ray_eye_near;
        glm::vec4 ray_world_far = inv_view * ray_eye_far;
        
        // Ray origin is camera position, direction is from near to far
        glm::vec3 ray_origin = position_;
        glm::vec3 ray_direction = glm::normalize(glm::vec3(ray_world_far) - glm::vec3(ray_world_near));
        
        return {ray_origin, ray_direction};
    }
    
    /**
     * @brief Calculate squared distance from ray to line segment
     * @return Squared distance to line segment
     */
    static float RayLineDistanceSq(const glm::vec3& ray_origin, const glm::vec3& ray_dir,
                                   const glm::vec3& line_start, const glm::vec3& line_end) {
        glm::vec3 line_dir = line_end - line_start;
        glm::vec3 w = ray_origin - line_start;
        
        float a = glm::dot(ray_dir, ray_dir);      // |ray_dir|^2
        float b = glm::dot(ray_dir, line_dir);     // ray_dir · line_dir
        float c = glm::dot(line_dir, line_dir);    // |line_dir|^2
        float d = glm::dot(ray_dir, w);            // ray_dir · w
        float e = glm::dot(line_dir, w);           // line_dir · w
        
        float denom = a * c - b * b;
        if (std::abs(denom) < 0.0001f) {
            // Ray and line are parallel, return distance from ray origin to line start
            return glm::dot(w, w);
        }
        
        float t = (b * e - c * d) / denom;
        float s = (a * e - b * d) / denom;
        
        // Clamp s to [0, 1] to stay on line segment
        s = std::max(0.0f, std::min(1.0f, s));
        
        glm::vec3 closest_on_ray = ray_origin + ray_dir * t;
        glm::vec3 closest_on_line = line_start + line_dir * s;
        
        return glm::dot(closest_on_ray - closest_on_line, closest_on_ray - closest_on_line);
    }
    
private:
    void RecalculateViewMatrix() {
        // Calculate front vector from yaw and pitch
        glm::vec3 front;
        front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        front.y = sin(glm::radians(pitch_));
        front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        front_ = glm::normalize(front);
        
        // Recalculate right and up vectors
        right_ = glm::normalize(glm::cross(front_, glm::vec3(0.0f, 1.0f, 0.0f)));
        up_ = glm::normalize(glm::cross(right_, front_));
        
        // Update view matrix
        view_ = glm::lookAt(position_, position_ + front_, up_);
    }
    
    glm::vec3 position_ = glm::vec3(0.0f, 1.0f, 5.0f);
    glm::vec3 front_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right_ = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    
    float yaw_ = -90.0f;
    float pitch_ = -11.5f;  // Fixed: was -20, now points toward origin at (0,0,0)
    float fov_ = 45.0f;
    
    float pan_speed_ = 0.01f;
    float zoom_speed_ = 0.1f;
    float rotation_speed_ = 0.5f;
    float movement_speed_ = 0.05f;  // For flythrough mode
    
    glm::mat4 view_ = glm::mat4(1.0f);
};

} // namespace schizo::editor
