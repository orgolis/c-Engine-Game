#include "camera_component.h"
#include "transform.h"

#include <glm/gtc/matrix_transform.hpp>

namespace schizo::scene {

glm::mat4 CameraComponent::GetProjectionMatrix(float aspect_ratio) const {
    if (aspect_ratio <= 0.0f) aspect_ratio = 1.0f;
    if (projection_ == CameraProjection::Perspective) {
        return glm::perspective(glm::radians(fov_degrees_),
                                aspect_ratio,
                                near_plane_,
                                far_plane_);
    }
    const float half_h = ortho_size_;
    const float half_w = half_h * aspect_ratio;
    return glm::ortho(-half_w, half_w, -half_h, half_h, near_plane_, far_plane_);
}

glm::mat4 CameraComponent::GetViewMatrix() const {
    if (entity_ == nullptr) return glm::mat4(1.0f);
    const Transform* t = entity_->GetTransform();
    if (t == nullptr) return glm::mat4(1.0f);

    const glm::vec3 pos     = t->GetWorldPosition();
    const glm::vec3 forward = t->GetForward();
    // GetForward() returns world-space forward. Convention: camera looks
    // down its -Z, so use lookAt at (pos + forward) with +Y up.
    return glm::lookAt(pos, pos + forward, glm::vec3(0.0f, 1.0f, 0.0f));
}

} // namespace schizo::scene
