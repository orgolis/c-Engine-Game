#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace schizo::renderer {

// Forward declarations
class RenderDevice;

/**
 * @enum TextureFormat
 * @brief Describes pixel format of texture data
 */
enum class TextureFormat : uint8_t {
    RGB8,          // 8-bit per channel RGB
    RGBA8,         // 8-bit per channel RGBA
    RGB16F,        // 16-bit float per channel
    RGBA16F,       // 16-bit float per channel
    RGB32F,        // 32-bit float per channel
    RGBA32F,       // 32-bit float per channel
    R8,            // Single 8-bit channel (grayscale)
    R16F,          // Single 16-bit float channel
    R32F,          // Single 32-bit float channel
    Depth16,       // 16-bit depth
    Depth24,       // 24-bit depth
    Depth32F,      // 32-bit float depth
    Depth24Stencil8, // 24-bit depth + 8-bit stencil
    Unknown = 255
};

/**
 * @enum TextureFilter
 * @brief Texture sampling filter mode
 */
enum class TextureFilter : uint8_t {
    Nearest,      // Nearest neighbor (blocky)
    Linear,       // Linear interpolation (smooth)
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear,
};

/**
 * @enum TextureWrap
 * @brief Texture wrapping mode for out-of-bounds coordinates
 */
enum class TextureWrap : uint8_t {
    Repeat,        // Tile texture
    MirroredRepeat, // Tile with mirroring
    ClampToEdge,   // Clamp to edge
    ClampToBorder, // Clamp to border color
};

/**
 * @struct TextureSettings
 * @brief Configuration for texture creation
 */
struct TextureSettings {
    TextureFilter min_filter = TextureFilter::Linear;
    TextureFilter mag_filter = TextureFilter::Linear;
    TextureWrap wrap_u = TextureWrap::Repeat;
    TextureWrap wrap_v = TextureWrap::Repeat;
    TextureWrap wrap_w = TextureWrap::Repeat;
    float anisotropy = 1.0f;
    bool generate_mipmaps = true;
    glm::vec4 border_color = glm::vec4(0.0f);
};

/**
 * @class Texture
 * @brief Base class for GPU texture resources
 * 
 * Abstract interface for texture management.
 * Concrete implementations: Texture2D, TextureCube, Texture3D
 */
class Texture {
public:
    virtual ~Texture() = default;
    
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = default;
    Texture& operator=(Texture&&) = default;
    
    /**
     * Get texture GPU handle
     */
    virtual uint32_t GetHandle() const = 0;
    
    /**
     * Get texture format
     */
    virtual TextureFormat GetFormat() const = 0;
    
    /**
     * Bind texture to texture unit for sampling
     * @param unit Texture unit (0-31 typically)
     */
    virtual void Bind(RenderDevice* device, uint32_t unit) const = 0;
    
    /**
     * Update texture data
     * @param data New pixel data
     * @param mip_level Mipmap level to update (0 = base)
     */
    virtual void SetData(RenderDevice* device, const void* data, uint32_t mip_level = 0) = 0;
    
    /**
     * Generate mipmaps for this texture
     */
    virtual void GenerateMipmaps(RenderDevice* device) = 0;
    
    /**
     * Read texture data back from GPU
     */
    virtual std::vector<uint8_t> GetData(RenderDevice* device) const = 0;

protected:
    Texture() = default;
};

/**
 * @class Texture2D
 * @brief 2D texture resource
 */
class Texture2D : public Texture {
public:
    /**
     * Create a 2D texture
     * @param width Texture width in pixels
     * @param height Texture height in pixels
     * @param format Pixel format
     * @param data Initial pixel data (can be null for empty texture)
     * @param settings Texture sampling and wrapping settings
     */
    static std::unique_ptr<Texture2D> Create(RenderDevice* device,
                                             uint32_t width, uint32_t height,
                                             TextureFormat format,
                                             const void* data = nullptr,
                                             const TextureSettings& settings = TextureSettings());
    
    /**
     * Load texture from file
     * @param file_path Path to image file (PNG, JPG, TGA, etc)
     * @param settings Texture sampling settings
     */
    static std::unique_ptr<Texture2D> LoadFromFile(RenderDevice* device,
                                                   const std::string& file_path,
                                                   const TextureSettings& settings = TextureSettings());
    
    virtual ~Texture2D() override = default;
    
    /**
     * Get texture dimensions
     */
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    
    /**
     * Resize texture
     */
    virtual void Resize(RenderDevice* device, uint32_t width, uint32_t height) = 0;

protected:
    Texture2D() = default;
};

/**
 * @class TextureCube
 * @brief Cubemap texture resource
 */
class TextureCube : public Texture {
public:
    /**
     * Cubemap face indices
     */
    enum Face : uint8_t {
        PositiveX,  // Right
        NegativeX,  // Left
        PositiveY,  // Top
        NegativeY,  // Bottom
        PositiveZ,  // Front
        NegativeZ,  // Back
        FaceCount
    };
    
    /**
     * Create a cubemap texture
     * @param size Dimensions (all faces are square)
     * @param format Pixel format
     * @param faces Array of 6 face data pointers (in order above)
     * @param settings Texture sampling settings
     */
    static std::unique_ptr<TextureCube> Create(RenderDevice* device,
                                               uint32_t size,
                                               TextureFormat format,
                                               const void** faces,
                                               const TextureSettings& settings = TextureSettings());
    
    /**
     * Load cubemap from 6 image files
     * @param dir_path Directory containing face images
     * @param pattern File naming pattern (e.g., "sky_{}.png")
     * @param settings Texture sampling settings
     */
    static std::unique_ptr<TextureCube> LoadFromFiles(RenderDevice* device,
                                                      const std::string& dir_path,
                                                      const std::string& pattern,
                                                      const TextureSettings& settings = TextureSettings());
    
    virtual ~TextureCube() override = default;
    
    /**
     * Get cubemap face size
     */
    virtual uint32_t GetSize() const = 0;
    
    /**
     * Update a single face
     */
    virtual void SetFaceData(RenderDevice* device, Face face, const void* data, uint32_t mip_level = 0) = 0;

protected:
    TextureCube() = default;
};

} // namespace schizo::renderer

#endif // TEXTURE_H
