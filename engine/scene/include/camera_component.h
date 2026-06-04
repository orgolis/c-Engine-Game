#pragma once

#include "entity.h"
#include <glm/glm.hpp>

namespace schizo::scene {

/**
 * @enum CameraProjection
 * @brief Selects perspective vs orthographic projection on a CameraComponent.
 */
enum class CameraProjection : uint8_t {
    Perspective  = 0,
    Orthographic = 1,
};

/**
 * @class CameraComponent
 * @brief Intrinsic camera attached to an entity. Exposes the standard
 *        editable camera knobs (projection, FOV, clip planes, ortho size,
 *        background clear colour) and computes view + projection matrices
 *        from the entity's transform.
 *
 * The renderer/editor are responsible for picking which CameraComponent
 * (if any) drives rendering. Camera intrinsics here are decoupled from
 * the viewport's aspect ratio — pass aspect into GetProjectionMatrix().
 */
class CameraComponent : public Component {
public:
    CameraComponent() = default;
    virtual ~CameraComponent() = default;

    CameraComponent(const CameraComponent&) = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;
    CameraComponent(CameraComponent&&) = default;
    CameraComponent& operator=(CameraComponent&&) = default;

    // ---- Projection type --------------------------------------------------
    CameraProjection GetProjection() const { return projection_; }
    void SetProjection(CameraProjection p)  { projection_ = p; }

    // ---- Perspective parameters ------------------------------------------
    /// Vertical field-of-view in degrees. Clamped to (1, 179).
    float GetFOV() const { return fov_degrees_; }
    void  SetFOV(float degrees) { fov_degrees_ = glm::clamp(degrees, 1.0f, 179.0f); }

    // ---- Orthographic parameters -----------------------------------------
    /// Half-height of the orthographic frustum (world units).
    float GetOrthographicSize() const { return ortho_size_; }
    void  SetOrthographicSize(float size) { ortho_size_ = glm::max(0.001f, size); }

    // ---- Clip planes ------------------------------------------------------
    float GetNearPlane() const { return near_plane_; }
    void  SetNearPlane(float n) {
        near_plane_ = glm::max(0.001f, n);
        if (far_plane_ <= near_plane_) far_plane_ = near_plane_ + 0.1f;
    }
    float GetFarPlane()  const { return far_plane_; }
    void  SetFarPlane(float f) { far_plane_ = glm::max(near_plane_ + 0.001f, f); }

    // ---- Clear colour -----------------------------------------------------
    /// RGBA linear background colour used by the renderer when this camera
    /// drives the active viewport. Pre-multiplied alpha not assumed.
    const glm::vec4& GetClearColor() const { return clear_color_; }
    void SetClearColor(const glm::vec4& c) { clear_color_ = c; }

    // ---- Derived matrices -------------------------------------------------
    /// Returns the projection matrix for the supplied aspect ratio. Aspect
    /// is width/height; pass 1.0f when rendering to a square target.
    glm::mat4 GetProjectionMatrix(float aspect_ratio) const;

    /// World-to-camera view matrix derived from the entity transform. The
    /// camera looks down its local -Z axis with +Y up, matching the glTF
    /// convention. Returns identity if the component is unattached.
    glm::mat4 GetViewMatrix() const;

private:
    CameraProjection projection_   = CameraProjection::Perspective;
    float            fov_degrees_  = 60.0f;
    float            ortho_size_   = 5.0f;
    float            near_plane_   = 0.1f;
    float            far_plane_    = 1000.0f;
    glm::vec4        clear_color_  = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
};

} // namespace schizo::scene
