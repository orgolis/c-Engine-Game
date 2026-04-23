#include "texture_loader.h"
#include "texture.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace schizo::asset {

std::map<std::string, std::shared_ptr<schizo::renderer::Texture2D>> TextureLoader::texture_cache_;

std::shared_ptr<schizo::renderer::Texture2D> TextureLoader::Load(
    const std::string& file_path,
    bool srgb,
    bool generate_mipmaps)
{
    // Check cache first
    auto it = texture_cache_.find(file_path);
    if (it != texture_cache_.end() && it->second) {
        spdlog::debug("Texture from cache: {}", file_path);
        return it->second;
    }
    
    // Load fresh
    auto texture = LoadDirect(file_path, srgb, generate_mipmaps);
    
    if (texture) {
        texture_cache_[file_path] = texture;
    }
    
    return texture;
}

std::shared_ptr<schizo::renderer::Texture2D> TextureLoader::LoadDirect(
    const std::string& file_path,
    bool srgb,
    bool generate_mipmaps)
{
    // Check if file exists
    if (!std::filesystem::exists(file_path)) {
        spdlog::warn("Texture file not found: {}", file_path);
        // Return placeholder
        return CreatePlaceholder(256, 256, true);
    }
    
    try {
        // For now, create placeholder textures
        // In a full implementation, we would use stb_image here
        auto texture = std::make_shared<schizo::renderer::Texture2D>();
        texture->SetName(std::filesystem::path(file_path).filename().string());
        
        // Allocate as white texture (placeholder)
        // In real implementation: load pixel data from file
        uint32_t width = 512;
        uint32_t height = 512;
        
        // Create white pixel data
        std::vector<unsigned char> pixels(width * height * 4, 255);
        
        // Allocate GPU storage
        GLenum internal_format = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        texture->AllocateStorage(width, height, internal_format);
        
        // Upload pixel data
        glBindTexture(GL_TEXTURE_2D, texture->GetHandle());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        
        if (generate_mipmaps) {
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        }
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        spdlog::info("Texture loaded: {} ({}x{})", file_path, width, height);
        return texture;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load texture {}: {}", file_path, e.what());
        return CreatePlaceholder(256, 256, true);
    }
}

std::vector<std::string> TextureLoader::GetLoadedTextures() {
    std::vector<std::string> names;
    for (const auto& [path, _] : texture_cache_) {
        names.push_back(path);
    }
    return names;
}

void TextureLoader::ClearCache() {
    texture_cache_.clear();
    spdlog::info("Texture cache cleared");
}

std::shared_ptr<schizo::renderer::Texture2D> TextureLoader::CreatePlaceholder(
    uint32_t width,
    uint32_t height,
    bool error_pattern)
{
    auto texture = std::make_shared<schizo::renderer::Texture2D>();
    texture->SetName(error_pattern ? "placeholder_error" : "placeholder_white");
    
    // Create pixel data
    std::vector<unsigned char> pixels(width * height * 4);
    
    if (error_pattern) {
        // Magenta/black checkerboard for error
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t idx = (y * width + x) * 4;
                bool checker = ((x / 32) + (y / 32)) % 2 == 0;
                
                pixels[idx + 0] = checker ? 255 : 0;      // R
                pixels[idx + 1] = 0;                       // G
                pixels[idx + 2] = checker ? 255 : 0;      // B
                pixels[idx + 3] = 255;                     // A
            }
        }
    } else {
        // White texture
        for (uint32_t i = 0; i < width * height * 4; i += 4) {
            pixels[i + 0] = 255;      // R
            pixels[i + 1] = 255;      // G
            pixels[i + 2] = 255;      // B
            pixels[i + 3] = 255;      // A
        }
    }
    
    // Allocate GPU storage
    texture->AllocateStorage(width, height, GL_RGBA8);
    
    // Upload pixel data
    glBindTexture(GL_TEXTURE_2D, texture->GetHandle());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return texture;
}

} // namespace schizo::asset
