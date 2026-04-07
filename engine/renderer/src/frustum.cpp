#include "frustum.h"
#include "renderer.h"
#include <glm/gtc/matrix_access.hpp>

namespace schizo::renderer {

void Frustum::ExtractFromViewProjection(const glm::mat4& vp) {
    // Extract planes from view-projection matrix
    // Method: extract from rows of the matrix after normalization
    
    glm::vec4 row0 = glm::row(vp, 0);
    glm::vec4 row1 = glm::row(vp, 1);
    glm::vec4 row2 = glm::row(vp, 2);
    glm::vec4 row3 = glm::row(vp, 3);
    
    // Left plane: row3 + row0
    planes_[static_cast<int>(FrustumPlane::Left)] = Plane(
        glm::normalize(glm::vec3(row3 + row0)),
        row3.w + row0.w
    );
    
    // Right plane: row3 - row0
    planes_[static_cast<int>(FrustumPlane::Right)] = Plane(
        glm::normalize(glm::vec3(row3 - row0)),
        row3.w - row0.w
    );
    
    // Bottom plane: row3 + row1
    planes_[static_cast<int>(FrustumPlane::Bottom)] = Plane(
        glm::normalize(glm::vec3(row3 + row1)),
        row3.w + row1.w
    );
    
    // Top plane: row3 - row1
    planes_[static_cast<int>(FrustumPlane::Top)] = Plane(
        glm::normalize(glm::vec3(row3 - row1)),
        row3.w - row1.w
    );
    
    // Near plane: row2
    planes_[static_cast<int>(FrustumPlane::Near)] = Plane(
        glm::normalize(glm::vec3(row2)),
        row2.w
    );
    
    // Far plane: row3 - row2
    planes_[static_cast<int>(FrustumPlane::Far)] = Plane(
        glm::normalize(glm::vec3(row3 - row2)),
        row3.w - row2.w
    );
    
    // Calculate corners
    CalculateCorners(vp);
}

void Frustum::ExtractFromCamera(const Camera& camera) {
    glm::mat4 view_projection = camera.GetViewProjectionMatrix();
    ExtractFromViewProjection(view_projection);
}

bool Frustum::ContainsPoint(const glm::vec3& point) const {
    for (const auto& plane : planes_) {
        if (plane.GetDistance(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::IntersectsSphere(const glm::vec3& center, float radius) const {
    for (const auto& plane : planes_) {
        float distance = plane.GetDistance(center);
        if (distance < -radius) {
            return false;  // Sphere is completely behind the plane
        }
    }
    return true;
}

bool Frustum::IntersectsAABB(const glm::vec3& min, const glm::vec3& max) const {
    for (const auto& plane : planes_) {
        // Calculate the positive extent of the AABB along the plane normal
        float extent = 0.0f;
        extent += std::abs(plane.normal.x) * (max.x - min.x) * 0.5f;
        extent += std::abs(plane.normal.y) * (max.y - min.y) * 0.5f;
        extent += std::abs(plane.normal.z) * (max.z - min.z) * 0.5f;
        
        // Calculate the center of the AABB
        glm::vec3 center = (min + max) * 0.5f;
        float distance = plane.GetDistance(center);
        
        if (distance < -extent) {
            return false;  // AABB is completely behind the plane
        }
    }
    return true;
}

void Frustum::CalculateCorners(const glm::mat4& vp) {
    glm::mat4 inv_vp = glm::inverse(vp);
    
    // Define 8 corners in NDC space (-1 to 1)
    float corners_ndc[8][3] = {
        // Near plane corners
        {-1.0f, -1.0f, -1.0f},  // 0: bottom-left-near
        { 1.0f, -1.0f, -1.0f},  // 1: bottom-right-near
        { 1.0f,  1.0f, -1.0f},  // 2: top-right-near
        {-1.0f,  1.0f, -1.0f},  // 3: top-left-near
        // Far plane corners
        {-1.0f, -1.0f,  1.0f},  // 4: bottom-left-far
        { 1.0f, -1.0f,  1.0f},  // 5: bottom-right-far
        { 1.0f,  1.0f,  1.0f},  // 6: top-right-far
        {-1.0f,  1.0f,  1.0f},  // 7: top-left-far
    };
    
    // Transform each corner from NDC to world space
    for (int i = 0; i < 8; ++i) {
        glm::vec4 ndc = glm::vec4(corners_ndc[i][0], corners_ndc[i][1], corners_ndc[i][2], 1.0f);
        glm::vec4 world = inv_vp * ndc;
        corners_[i] = glm::vec3(world) / world.w;
    }
}

} // namespace schizo::renderer
