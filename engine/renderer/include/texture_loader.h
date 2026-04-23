#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>

namespace schizo::renderer {
    class Texture2D;
    class RenderDevice;
}

namespace schizo::asset {

/**
 * @class TextureLoader
 * @brief Simple texture loader using stb_image
 * 
 * Supports: PNG, JPG, TGA, BMP, PSD, GIF, HDR, PIC
 */
class TextureLoader {
public:
    /**
     * Load texture from file
     * Automatically determines format from file extension
     * Caches loaded textures
     */
    static std::shared_ptr<schizo::renderer::Texture2D> Load(
        const std::string& file_path,
        bool srgb = false,
        bool generate_mipmaps = true);
    
    /**
     * Load texture without caching
     */
    static std::shared_ptr<schizo::renderer::Texture2D> LoadDirect(
        const std::string& file_path,
        bool srgb = false,
        bool generate_mipmaps = true);
    
    /**
     * Get all loaded textures
     */
    static std::vector<std::string> GetLoadedTextures();
    
    /**
     * Clear texture cache
     */
    static void ClearCache();
    
    /**
     * Create a placeholder texture (white or error pattern)
     */
    static std::shared_ptr<schizo::renderer::Texture2D> CreatePlaceholder(
        uint32_t width = 256,
        uint32_t height = 256,
        bool error_pattern = false);

private:
    static std::map<std::string, std::shared_ptr<schizo::renderer::Texture2D>> texture_cache_;
};

} // namespace schizo::asset
