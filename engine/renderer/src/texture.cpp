#include "texture.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <glm/glm.hpp>

namespace schizo::renderer {

// ============================================================================
// OpenGL Texture2D Implementation
// ============================================================================

class OpenGLTexture2D : public Texture2D {
public:
    OpenGLTexture2D(uint32_t handle, uint32_t width, uint32_t height, TextureFormat format)
        : handle_(handle), width_(width), height_(height), format_(format) {}
    
    ~OpenGLTexture2D() override {
        // GPU cleanup handled by RenderDevice
    }
    
    uint32_t GetHandle() const override { return handle_; }
    TextureFormat GetFormat() const override { return format_; }
    uint32_t GetWidth() const override { return width_; }
    uint32_t GetHeight() const override { return height_; }
    
    void Bind(RenderDevice* device, uint32_t unit) const override {
        device->BindTexture(handle_, unit);
    }
    
    void SetData(RenderDevice* device, const void* data, uint32_t mip_level = 0) override {
        if (!device || !data) {
            spdlog::warn("Texture2D::SetData - Invalid device or data pointer");
            return;
        }
        
        // Calculate offset based on mip level
        uint32_t width = width_ >> mip_level;
        uint32_t height = height_ >> mip_level;
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        
        size_t size = width * height * GetTexelSize(static_cast<uint32_t>(format_));
        // Note: This is a placeholder - actual implementation would call OpenGL glTexSubImage2D
        spdlog::debug("Texture2D::SetData - Updated mip level {}: {}x{}", mip_level, width, height);
    }
    
    void GenerateMipmaps(RenderDevice* device) override {
        if (!device) return;
        spdlog::debug("Texture2D::GenerateMipmaps - Generating mipmaps for {}x{} texture", width_, height_);
        // Note: Actual implementation would call glGenerateMipmap
    }
    
    std::vector<uint8_t> GetData(RenderDevice* device) const override {
        std::vector<uint8_t> data;
        if (!device) return data;
        
        size_t size = width_ * height_ * GetTexelSize(static_cast<uint32_t>(format_));
        data.resize(size);
        
        spdlog::debug("Texture2D::GetData - Read {}x{} texture ({}KB)", 
                      width_, height_, size / 1024);
        
        // Note: Actual implementation would read data from GPU
        return data;
    }
    
    void Resize(RenderDevice* device, uint32_t width, uint32_t height) override {
        if (!device) return;
        
        spdlog::info("Texture2D::Resize - {}x{} -> {}x{}", width_, height_, width, height);
        
        // In actual implementation, would need to delete old texture and create new one
        width_ = width;
        height_ = height;
    }

private:
    uint32_t handle_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    TextureFormat format_ = TextureFormat::RGBA8;
    
    uint32_t GetTexelSize(uint32_t format) const {
        switch (format) {
            case static_cast<uint32_t>(TextureFormat::RGBA8): return 4;
            case static_cast<uint32_t>(TextureFormat::RGB8): return 3;
            case static_cast<uint32_t>(TextureFormat::R8): return 1;
            case static_cast<uint32_t>(TextureFormat::RGBA16F): return 8;
            case static_cast<uint32_t>(TextureFormat::RGB16F): return 6;
            case static_cast<uint32_t>(TextureFormat::R16F): return 2;
            case static_cast<uint32_t>(TextureFormat::RGBA32F): return 16;
            case static_cast<uint32_t>(TextureFormat::RGB32F): return 12;
            case static_cast<uint32_t>(TextureFormat::R32F): return 4;
            case static_cast<uint32_t>(TextureFormat::Depth24): return 3;
            case static_cast<uint32_t>(TextureFormat::Depth32F): return 4;
            default: return 4;
        }
    }
};

// ============================================================================
// OpenGL TextureCube Implementation
// ============================================================================

class OpenGLTextureCube : public TextureCube {
public:
    OpenGLTextureCube(uint32_t handle, uint32_t size, TextureFormat format)
        : handle_(handle), size_(size), format_(format) {}
    
    ~OpenGLTextureCube() override {}
    
    uint32_t GetHandle() const override { return handle_; }
    TextureFormat GetFormat() const override { return format_; }
    uint32_t GetSize() const override { return size_; }
    
    void Bind(RenderDevice* device, uint32_t unit) const override {
        device->BindTexture(handle_, unit);
    }
    
    void SetData(RenderDevice* device, const void* data, uint32_t mip_level = 0) override {
        // SetData for cubemap - updates all faces
        if (!device || !data) {
            spdlog::warn("TextureCube::SetData - Invalid device or data pointer");
            return;
        }
    }
    
    void GenerateMipmaps(RenderDevice* device) override {
        if (!device) return;
        spdlog::debug("TextureCube::GenerateMipmaps - Generating mipmaps for {}x{} cubemap", size_, size_);
    }
    
    std::vector<uint8_t> GetData(RenderDevice* device) const override {
        std::vector<uint8_t> data;
        if (!device) return data;
        
        size_t face_size = size_ * size_ * GetTexelSize(static_cast<uint32_t>(format_));
        data.resize(face_size * 6);  // 6 faces
        
        spdlog::debug("TextureCube::GetData - Read {}x{} cubemap ({}KB total)", 
                      size_, size_, (face_size * 6) / 1024);
        
        return data;
    }
    
    void SetFaceData(RenderDevice* device, Face face, const void* data, uint32_t mip_level = 0) override {
        if (!device || !data) {
            spdlog::warn("TextureCube::SetFaceData - Invalid device or data pointer");
            return;
        }
        
        const char* face_names[] = {"PositiveX", "NegativeX", "PositiveY", "NegativeY", "PositiveZ", "NegativeZ"};
        spdlog::debug("TextureCube::SetFaceData - Updated face {} mip {}", face_names[face], mip_level);
    }

private:
    uint32_t handle_ = 0;
    uint32_t size_ = 0;
    TextureFormat format_ = TextureFormat::RGBA8;
    
    uint32_t GetTexelSize(uint32_t format) const {
        switch (format) {
            case static_cast<uint32_t>(TextureFormat::RGBA8): return 4;
            case static_cast<uint32_t>(TextureFormat::RGB8): return 3;
            case static_cast<uint32_t>(TextureFormat::R8): return 1;
            case static_cast<uint32_t>(TextureFormat::RGBA16F): return 8;
            case static_cast<uint32_t>(TextureFormat::RGB16F): return 6;
            case static_cast<uint32_t>(TextureFormat::R16F): return 2;
            case static_cast<uint32_t>(TextureFormat::RGBA32F): return 16;
            case static_cast<uint32_t>(TextureFormat::RGB32F): return 12;
            case static_cast<uint32_t>(TextureFormat::R32F): return 4;
            default: return 4;
        }
    }
};

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<Texture2D> Texture2D::Create(RenderDevice* device,
                                              uint32_t width, uint32_t height,
                                              TextureFormat format,
                                              const void* data,
                                              const TextureSettings& settings) {
    if (!device) {
        spdlog::error("Texture2D::Create - No render device");
        return nullptr;
    }
    
    // Create GPU texture
    uint32_t gl_format = static_cast<uint32_t>(format);
    uint32_t handle = device->CreateTexture2D(width, height, gl_format, data);
    
    if (handle == 0) {
        spdlog::error("Texture2D::Create - Failed to create GPU texture");
        return nullptr;
    }
    
    spdlog::info("Texture2D::Create - Created {}x{} texture (handle={})", width, height, handle);
    
    auto texture = std::make_unique<OpenGLTexture2D>(handle, width, height, format);
    
    if (settings.generate_mipmaps) {
        texture->GenerateMipmaps(device);
    }
    
    return texture;
}

std::unique_ptr<Texture2D> Texture2D::LoadFromFile(RenderDevice* device,
                                                    const std::string& file_path,
                                                    const TextureSettings& settings) {
    spdlog::info("Texture2D::LoadFromFile - Loading from: {}", file_path);
    
    // Note: This would use stb_image to load the file
    // For now, just placeholder
    spdlog::warn("Texture2D::LoadFromFile - Actual file loading not yet implemented");
    
    return nullptr;
}

std::unique_ptr<TextureCube> TextureCube::Create(RenderDevice* device,
                                                  uint32_t size,
                                                  TextureFormat format,
                                                  const void** faces,
                                                  const TextureSettings& settings) {
    if (!device) {
        spdlog::error("TextureCube::Create - No render device");
        return nullptr;
    }
    
    uint32_t gl_format = static_cast<uint32_t>(format);
    uint32_t handle = device->CreateTextureCube(size, gl_format, faces);
    
    if (handle == 0) {
        spdlog::error("TextureCube::Create - Failed to create cubemap");
        return nullptr;
    }
    
    spdlog::info("TextureCube::Create - Created {}x{} cubemap (handle={})", size, size, handle);
    
    auto texture = std::make_unique<OpenGLTextureCube>(handle, size, format);
    
    if (settings.generate_mipmaps) {
        texture->GenerateMipmaps(device);
    }
    
    return texture;
}

std::unique_ptr<TextureCube> TextureCube::LoadFromFiles(RenderDevice* device,
                                                        const std::string& dir_path,
                                                        const std::string& pattern,
                                                        const TextureSettings& settings) {
    spdlog::info("TextureCube::LoadFromFiles - Loading from: {}", dir_path);
    spdlog::warn("TextureCube::LoadFromFiles - Actual file loading not yet implemented");
    
    return nullptr;
}

} // namespace schizo::renderer
