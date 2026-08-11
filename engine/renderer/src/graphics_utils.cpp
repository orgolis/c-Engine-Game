#include "graphics_utils.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace schizo::graphics {

// ============================================================================
// Color Utilities
// ============================================================================

glm::vec3 HSVToRGB(float h, float s, float v) {
    if (s == 0.0f) {
        return glm::vec3(v, v, v);
    }
    
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    float r, g, b;
    if (h < 60.0f) {
        r = c; g = x; b = 0.0f;
    } else if (h < 120.0f) {
        r = x; g = c; b = 0.0f;
    } else if (h < 180.0f) {
        r = 0.0f; g = c; b = x;
    } else if (h < 240.0f) {
        r = 0.0f; g = x; b = c;
    } else if (h < 300.0f) {
        r = x; g = 0.0f; b = c;
    } else {
        r = c; g = 0.0f; b = x;
    }
    
    return glm::vec3(r + m, g + m, b + m);
}

glm::vec3 RGBToHSV(const glm::vec3& rgb) {
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;
    
    float max_val = std::max({r, g, b});
    float min_val = std::min({r, g, b});
    float delta = max_val - min_val;
    
    float h = 0.0f;
    float s = max_val == 0.0f ? 0.0f : delta / max_val;
    float v = max_val;
    
    if (delta != 0.0f) {
        if (max_val == r) {
            h = 60.0f * std::fmod((g - b) / delta + (g < b ? 6.0f : 0.0f), 6.0f);
        } else if (max_val == g) {
            h = 60.0f * ((b - r) / delta + 2.0f);
        } else {
            h = 60.0f * ((r - g) / delta + 4.0f);
        }
    }
    
    return glm::vec3(h, s, v);
}

// ============================================================================
// Texture Utilities
// ============================================================================

uint32_t GetTexelSize(uint32_t format) {
    switch (format) {
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA8): return 4;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB8): return 3;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R8): return 1;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA16F): return 8;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB16F): return 6;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R16F): return 2;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA32F): return 16;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB32F): return 12;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R32F): return 4;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth24): return 3;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth32F): return 4;
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth24Stencil8): return 4;
        default: return 4;
    }
}

uint64_t CalculateTextureSizeBytes(uint32_t width, uint32_t height, uint32_t format, bool with_mipmaps) {
    uint64_t size = static_cast<uint64_t>(width) * height * GetTexelSize(format);
    
    if (with_mipmaps) {
        // Calculate mipmap chain size
        uint32_t mip_count = 1;
        uint32_t mip_w = width;
        uint32_t mip_h = height;
        
        while (mip_w > 1 || mip_h > 1) {
            mip_w = std::max(1u, mip_w / 2);
            mip_h = std::max(1u, mip_h / 2);
            mip_count++;
            
            size += static_cast<uint64_t>(mip_w) * mip_h * GetTexelSize(format);
        }
    }
    
    return size;
}

glm::ivec2 GetMipmapDimensions(uint32_t level, uint32_t base_width, uint32_t base_height) {
    uint32_t w = std::max(1u, base_width >> level);
    uint32_t h = std::max(1u, base_height >> level);
    return glm::ivec2(w, h);
}

// ============================================================================
// Geometry Utilities
// ============================================================================

BoundingBox CalculateBoundingBox(const glm::vec3* positions, uint32_t count) {
    if (count == 0 || !positions) {
        return {{0, 0, 0}, {0, 0, 0}};
    }
    
    glm::vec3 min = positions[0];
    glm::vec3 max = positions[0];
    
    for (uint32_t i = 1; i < count; ++i) {
        min = glm::min(min, positions[i]);
        max = glm::max(max, positions[i]);
    }
    
    return {min, max};
}

void CalculateNormals(glm::vec3* normals, const glm::vec3* positions, const uint32_t* indices, uint32_t index_count) {
    if (!normals || !positions || !indices || index_count == 0) {
        spdlog::warn("CalculateNormals - Invalid input parameters");
        return;
    }
    
    // Zero out normals
    // Note: Would need vertex count to do this properly - simplified here
    
    // Calculate face normals
    for (uint32_t i = 0; i < index_count; i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        glm::vec3 p0 = positions[i0];
        glm::vec3 p1 = positions[i1];
        glm::vec3 p2 = positions[i2];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 face_normal = glm::normalize(glm::cross(edge1, edge2));
        
        normals[i0] += face_normal;
        normals[i1] += face_normal;
        normals[i2] += face_normal;
    }
    
    // Normalize all normals
    // Note: Would need vertex count - simplified here
}

void CalculateTangentSpace(glm::vec4* tangents, glm::vec3* bitangents,
                           const glm::vec3* positions, const glm::vec3* normals,
                           const glm::vec2* texcoords, const uint32_t* indices,
                           uint32_t index_count) {
    if (!tangents || !bitangents || !positions || !normals || !texcoords || !indices) {
        spdlog::warn("CalculateTangentSpace - Invalid input parameters");
        return;
    }
    
    // Simplified tangent space calculation
    spdlog::debug("CalculateTangentSpace - Computing tangent space for {} indices", index_count);
    
    for (uint32_t i = 0; i < index_count; i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        glm::vec3 p0 = positions[i0];
        glm::vec3 p1 = positions[i1];
        glm::vec3 p2 = positions[i2];
        
        glm::vec2 uv0 = texcoords[i0];
        glm::vec2 uv1 = texcoords[i1];
        glm::vec2 uv2 = texcoords[i2];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec2 duv1 = uv1 - uv0;
        glm::vec2 duv2 = uv2 - uv0;
        
        float f = 1.0f / (duv1.x * duv2.y - duv2.x * duv1.y);
        
        glm::vec3 tangent = (duv2.y * edge1 - duv1.y * edge2) * f;
        glm::vec3 bitangent = (duv1.x * edge2 - duv2.x * edge1) * f;
        
        tangent = glm::normalize(tangent);
        bitangent = glm::normalize(bitangent);
        
        // Store tangent with handedness in w component
        float handedness = glm::dot(glm::cross(normals[i0], tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        
        tangents[i0] = glm::vec4(tangent, handedness);
        tangents[i1] = glm::vec4(tangent, handedness);
        tangents[i2] = glm::vec4(tangent, handedness);
        
        bitangents[i0] = bitangent;
        bitangents[i1] = bitangent;
        bitangents[i2] = bitangent;
    }
}

// ============================================================================
// Matrix Utilities
// ============================================================================

glm::mat4 CreateViewMatrix(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    return glm::lookAt(eye, target, up);
}

glm::mat4 CreateOrthogonalMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    return glm::ortho(left, right, bottom, top, near_plane, far_plane);
}

glm::mat4 CreatePerspectiveMatrix(float fov_degrees, float aspect_ratio, float near_plane, float far_plane) {
    return glm::perspective(glm::radians(fov_degrees), aspect_ratio, near_plane, far_plane);
}

glm::mat4 CreateLightMatrix(const glm::vec3& light_dir, const BoundingBox& scene_bounds) {
    glm::vec3 center = scene_bounds.GetCenter();
    glm::vec3 extents = scene_bounds.GetExtents();
    float radius = glm::length(extents);
    
    // Create light view matrix
    glm::vec3 light_pos = center - light_dir * radius;
    glm::mat4 view = glm::lookAt(light_pos, center, glm::vec3(0, 1, 0));
    
    // Create orthogonal projection
    glm::mat4 projection = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 2.0f);
    
    return projection * view;
}

// ============================================================================
// Format Conversions
// ============================================================================

std::string FormatToString(uint32_t format) {
    switch (format) {
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB8): return "RGB8";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA8): return "RGBA8";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB16F): return "RGB16F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA16F): return "RGBA16F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGB32F): return "RGB32F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::RGBA32F): return "RGBA32F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R8): return "R8";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R16F): return "R16F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::R32F): return "R32F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth16): return "Depth16";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth24): return "Depth24";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth32F): return "Depth32F";
        case static_cast<uint32_t>(schizo::renderer::TextureFormat::Depth24Stencil8): return "Depth24Stencil8";
        default: return "Unknown";
    }
}

std::string WrapModeToString(uint32_t wrap_mode) {
    switch (wrap_mode) {
        case static_cast<uint32_t>(schizo::renderer::TextureWrap::Repeat): return "Repeat";
        case static_cast<uint32_t>(schizo::renderer::TextureWrap::MirroredRepeat): return "MirroredRepeat";
        case static_cast<uint32_t>(schizo::renderer::TextureWrap::ClampToEdge): return "ClampToEdge";
        case static_cast<uint32_t>(schizo::renderer::TextureWrap::ClampToBorder): return "ClampToBorder";
        default: return "Unknown";
    }
}

std::string FilterModeToString(uint32_t filter_mode) {
    switch (filter_mode) {
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::Nearest): return "Nearest";
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::Linear): return "Linear";
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::NearestMipmapNearest): return "NearestMipmapNearest";
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::LinearMipmapNearest): return "LinearMipmapNearest";
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::NearestMipmapLinear): return "NearestMipmapLinear";
        case static_cast<uint32_t>(schizo::renderer::TextureFilter::LinearMipmapLinear): return "LinearMipmapLinear";
        default: return "Unknown";
    }
}

} // namespace schizo::graphics
