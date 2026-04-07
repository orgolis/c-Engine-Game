#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace schizo::graphics {

/**
 * @namespace graphics_utils
 * @brief Graphics utility functions and helpers
 */

// ============================================================================
// Color Utilities
// ============================================================================

/**
 * Convert sRGB to linear color space
 */
inline glm::vec3 SRGBToLinear(const glm::vec3& srgb) {
    return glm::vec3(
        (srgb.r <= 0.04045) ? srgb.r / 12.92 : glm::pow((srgb.r + 0.055) / 1.055, 2.4),
        (srgb.g <= 0.04045) ? srgb.g / 12.92 : glm::pow((srgb.g + 0.055) / 1.055, 2.4),
        (srgb.b <= 0.04045) ? srgb.b / 12.92 : glm::pow((srgb.b + 0.055) / 1.055, 2.4)
    );
}

/**
 * Convert linear to sRGB color space
 */
inline glm::vec3 LinearToSRGB(const glm::vec3& linear) {
    return glm::vec3(
        (linear.r <= 0.0031308) ? 12.92 * linear.r : 1.055 * glm::pow(linear.r, 1.0 / 2.4) - 0.055,
        (linear.g <= 0.0031308) ? 12.92 * linear.g : 1.055 * glm::pow(linear.g, 1.0 / 2.4) - 0.055,
        (linear.b <= 0.0031308) ? 12.92 * linear.b : 1.055 * glm::pow(linear.b, 1.0 / 2.4) - 0.055
    );
}

/**
 * Convert HSV to RGB
 */
glm::vec3 HSVToRGB(float h, float s, float v);

/**
 * Convert RGB to HSV
 */
glm::vec3 RGBToHSV(const glm::vec3& rgb);

// ============================================================================
// Texture Utilities
// ============================================================================

/**
 * Get the byte size of a texture format
 * @param format Texture format
 * @return Size in bytes per pixel
 */
uint32_t GetTexelSize(uint32_t format);

/**
 * Calculate texture memory size
 * @param width Texture width
 * @param height Texture height
 * @param format Texture format
 * @param with_mipmaps Include mipmap chain in calculation
 */
uint64_t CalculateTextureSizeBytes(uint32_t width, uint32_t height, uint32_t format, bool with_mipmaps = false);

/**
 * Generate mipmap level dimensions
 * @param level Mipmap level (0 = full resolution)
 * @param base_width Base width
 * @param base_height Base height
 */
glm::ivec2 GetMipmapDimensions(uint32_t level, uint32_t base_width, uint32_t base_height);

// ============================================================================
// Geometry Utilities
// ============================================================================

/**
 * Calculate bounding box from vertex positions
 */
struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;
    
    glm::vec3 GetCenter() const { return (min + max) * 0.5f; }
    glm::vec3 GetExtents() const { return (max - min) * 0.5f; }
    float GetRadius() const { return glm::length(GetExtents()); }
};

BoundingBox CalculateBoundingBox(const glm::vec3* positions, uint32_t count);

/**
 * Calculate vertex normals from triangle indices
 */
void CalculateNormals(glm::vec3* normals, const glm::vec3* positions, const uint32_t* indices, uint32_t index_count);

/**
 * Calculate tangent and bitangent vectors for normal mapping
 */
void CalculateTangentSpace(glm::vec4* tangents, glm::vec3* bitangents,
                           const glm::vec3* positions, const glm::vec3* normals,
                           const glm::vec2* texcoords, const uint32_t* indices,
                           uint32_t index_count);

// ============================================================================
// Matrix Utilities
// ============================================================================

/**
 * Create a view matrix from eye position and target
 */
glm::mat4 CreateViewMatrix(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

/**
 * Create an orthogonal projection matrix
 */
glm::mat4 CreateOrthogonalMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane);

/**
 * Create a perspective projection matrix
 */
glm::mat4 CreatePerspectiveMatrix(float fov_degrees, float aspect_ratio, float near_plane, float far_plane);

/**
 * Create a light view-projection matrix (for shadow mapping)
 */
glm::mat4 CreateLightMatrix(const glm::vec3& light_dir, const BoundingBox& scene_bounds);

// ============================================================================
// Format Conversions
// ============================================================================

/**
 * Get string representation of format
 */
std::string FormatToString(uint32_t format);

/**
 * Get string representation of texture wrap mode
 */
std::string WrapModeToString(uint32_t wrap_mode);

/**
 * Get string representation of texture filter mode
 */
std::string FilterModeToString(uint32_t filter_mode);

} // namespace schizo::graphics

#endif // GRAPHICS_UTILS_H
