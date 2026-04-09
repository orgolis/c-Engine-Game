#include "light_component.h"
#include "transform.h"

namespace schizo::scene {

glm::vec3 DirectionalLightComponent::GetDirection() const {
    if (entity_) {
        return entity_->GetTransform()->GetForward();
    }
    return glm::vec3(0.0f, -1.0f, 0.0f);  // Default: down
}

} // namespace schizo::scene
