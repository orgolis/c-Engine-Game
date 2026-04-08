#include "deferred_renderer.h"
#include "g_buffer.h"
#include "render_device.h"
#include "framebuffer.h"
#include "texture.h"
#include "shader.h"
#include "mesh.h"
#include "material.h"
#include "light.h"
#include "lighting.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <GL/gl.h>
#include <algorithm>

namespace schizo::renderer {

// ============================================================================
// GBuffer Implementation
// ============================================================================

GBuffer::GBuffer() = default;

GBuffer::~GBuffer() = default;

std::unique_ptr<GBuffer> GBuffer::Create(RenderDevice* device, const GBufferConfig& config) {
    auto gbuffer = std::make_unique<GBuffer>();
    gbuffer->config_ = config;
    
    // Determine actual texture format based on GBufferFormat enum
    auto format_to_gl = [](GBufferFormat fmt) -> std::pair<GLenum, GLenum> {
        switch (fmt) {
        case GBufferFormat::RGBA8:    return {GL_RGBA8, GL_RGBA};
        case GBufferFormat::RGBA16F:  return {GL_RGBA16F, GL_RGBA};
        case GBufferFormat::RGBA32F:  return {GL_RGBA32F, GL_RGBA};
        case GBufferFormat::RGB16F:   return {GL_RGB16F, GL_RGB};
        case GBufferFormat::RG16F:    return {GL_RG16F, GL_RG};
        case GBufferFormat::R32F:     return {GL_R32F, GL_RED};
        case GBufferFormat::R8:       return {GL_R8, GL_RED};
        default:                       return {GL_RGBA16F, GL_RGBA};
        }
    };
    
    // Create position texture
    {
        auto [internal_fmt, format] = format_to_gl(config.position_format);
        gbuffer->position_texture_ = std::make_shared<Texture2D>();
        gbuffer->position_texture_->AllocateStorage(config.width, config.height, internal_fmt);
        gbuffer->position_texture_->SetName("GBuffer_Position");
    }
    
    // Create normal texture
    {
        auto [internal_fmt, format] = format_to_gl(config.normal_format);
        gbuffer->normal_texture_ = std::make_shared<Texture2D>();
        gbuffer->normal_texture_->AllocateStorage(config.width, config.height, internal_fmt);
        gbuffer->normal_texture_->SetName("GBuffer_Normal");
    }
    
    // Create albedo texture
    {
        auto [internal_fmt, format] = format_to_gl(config.albedo_format);
        gbuffer->albedo_texture_ = std::make_shared<Texture2D>();
        gbuffer->albedo_texture_->AllocateStorage(config.width, config.height, internal_fmt);
        gbuffer->albedo_texture_->SetName("GBuffer_Albedo");
    }
    
    // Create material texture
    {
        auto [internal_fmt, format] = format_to_gl(config.material_format);
        gbuffer->material_texture_ = std::make_shared<Texture2D>();
        gbuffer->material_texture_->AllocateStorage(config.width, config.height, internal_fmt);
        gbuffer->material_texture_->SetName("GBuffer_Material");
    }
    
    // Create depth texture
    {
        auto [internal_fmt, format] = format_to_gl(config.depth_format);
        gbuffer->depth_texture_ = std::make_shared<Texture2D>();
        gbuffer->depth_texture_->AllocateStorage(config.width, config.height, internal_fmt);
        gbuffer->depth_texture_->SetName("GBuffer_Depth");
    }
    
    // Create framebuffer with color and depth attachments
    gbuffer->framebuffer_ = Framebuffer::Create(device);
    gbuffer->framebuffer_->AttachColorTexture(gbuffer->position_texture_, 0);
    gbuffer->framebuffer_->AttachColorTexture(gbuffer->normal_texture_, 1);
    gbuffer->framebuffer_->AttachColorTexture(gbuffer->albedo_texture_, 2);
    gbuffer->framebuffer_->AttachColorTexture(gbuffer->material_texture_, 3);
    gbuffer->framebuffer_->AttachDepthTexture(gbuffer->depth_texture_);
    
    if (!gbuffer->framebuffer_->IsComplete()) {
        spdlog::error("GBuffer framebuffer is incomplete!");
        return nullptr;
    }
    
    spdlog::info("GBuffer created: {}x{}", config.width, config.height);
    return gbuffer;
}

void GBuffer::BeginGeometryPass(RenderDevice* device) {
    if (!framebuffer_) return;
    framebuffer_->Bind(device);
    
    // Set clear color and clear buffers
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
}

void GBuffer::EndGeometryPass(RenderDevice* device) {
    if (!framebuffer_) return;
    framebuffer_->Unbind(device);
}

void GBuffer::BeginLightingPass(RenderDevice* device) {
    // Lighting pass typically renders to screen or intermediate target
    // Bind G-Buffer textures for reading
    if (position_texture_) position_texture_->Bind(0);
    if (normal_texture_) normal_texture_->Bind(1);
    if (albedo_texture_) albedo_texture_->Bind(2);
    if (material_texture_) material_texture_->Bind(3);
    if (depth_texture_) depth_texture_->Bind(4);
}

void GBuffer::EndLightingPass(RenderDevice* device) {
    // Unbind all textures
    for (uint32_t i = 0; i < 5; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void GBuffer::Clear(RenderDevice* device, const glm::vec4& clear_color) {
    if (!framebuffer_) return;
    
    framebuffer_->Bind(device);
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    framebuffer_->Unbind(device);
}

void GBuffer::Resize(RenderDevice* device, uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;  // No resize needed
    }
    
    config_.width = width;
    config_.height = height;
    
    // Recreate textures with new dimensions
    if (position_texture_) {
        position_texture_->Resize(width, height);
    }
    if (normal_texture_) {
        normal_texture_->Resize(width, height);
    }
    if (albedo_texture_) {
        albedo_texture_->Resize(width, height);
    }
    if (material_texture_) {
        material_texture_->Resize(width, height);
    }
    if (depth_texture_) {
        depth_texture_->Resize(width, height);
    }
    
    spdlog::info("GBuffer resized to {}x{}", width, height);
}

std::vector<std::shared_ptr<Texture2D>> GBuffer::GetColorAttachments() const {
    return {position_texture_, normal_texture_, albedo_texture_, material_texture_};
}

std::shared_ptr<Texture2D> GBuffer::GetAttachment(uint32_t index) const {
    switch (index) {
    case 0: return position_texture_;
    case 1: return normal_texture_;
    case 2: return albedo_texture_;
    case 3: return material_texture_;
    case 4: return depth_texture_;
    default: return nullptr;
    }
}

std::string GBuffer::GetAttachmentName(uint32_t index) const {
    switch (index) {
    case 0: return "Position";
    case 1: return "Normal";
    case 2: return "Albedo";
    case 3: return "Material";
    case 4: return "Depth";
    default: return "Unknown";
    }
}

// ============================================================================
// DeferredRenderer Implementation
// ============================================================================

DeferredRenderer::DeferredRenderer() = default;

DeferredRenderer::~DeferredRenderer() = default;

std::unique_ptr<DeferredRenderer> DeferredRenderer::Create(RenderDevice* device, const DeferredConfig& config) {
    auto renderer = std::make_unique<DeferredRenderer>();
    renderer->device_ = device;
    renderer->config_ = config;
    
    // Create G-Buffer
    GBufferConfig gbuffer_config;
    gbuffer_config.width = config.width;
    gbuffer_config.height = config.height;
    renderer->gbuffer_ = GBuffer::Create(device, gbuffer_config);
    
    if (!renderer->gbuffer_) {
        spdlog::error("Failed to create G-Buffer!");
        return nullptr;
    }
    
    // Create output framebuffer (screen target)
    renderer->output_framebuffer_ = Framebuffer::Create(device);
    
    // Initialize shaders and buffers
    renderer->Initialize(device);
    
    spdlog::info("DeferredRenderer created: {}x{}, mode={}", 
                 config.width, config.height, 
                 static_cast<int>(config.mode));
    
    return renderer;
}

void DeferredRenderer::Initialize(RenderDevice* device) {
    CreateShaders(device);
    CreateBuffers(device);
    
    // Create fullscreen quad for lighting pass
    fullscreen_quad_ = Mesh::CreateQuad(device);
    
    spdlog::info("DeferredRenderer initialized");
}

void DeferredRenderer::Shutdown(RenderDevice* device) {
    // Cleanup buffers
    if (light_index_buffer_) {
        glDeleteBuffers(1, &light_index_buffer_);
        light_index_buffer_ = 0;
    }
    
    for (auto& buffer : light_list_buffers_) {
        if (buffer) {
            glDeleteBuffers(1, &buffer);
        }
    }
    light_list_buffers_.clear();
    
    for (auto& buffer : light_grid_buffers_) {
        if (buffer) {
            glDeleteBuffers(1, &buffer);
        }
    }
    light_grid_buffers_.clear();
    
    spdlog::info("DeferredRenderer shutdown complete");
}

void DeferredRenderer::Resize(RenderDevice* device, uint32_t width, uint32_t height) {
    if (width == config_.width && height == config_.height) {
        return;
    }
    
    config_.width = width;
    config_.height = height;
    
    if (gbuffer_) {
        gbuffer_->Resize(device, width, height);
    }
    
    spdlog::info("DeferredRenderer resized to {}x{}", width, height);
}

void DeferredRenderer::BeginFrame(RenderDevice* device) {
    is_rendering_ = true;
    visible_light_count_ = 0;
    total_light_count_ = 0;
    
    geometry_pass_time_ = 0.0f;
    light_culling_time_ = 0.0f;
    lighting_pass_time_ = 0.0f;
}

void DeferredRenderer::EndFrame(RenderDevice* device) {
    is_rendering_ = false;
}

void DeferredRenderer::GeometryPass(RenderDevice* device, 
                                   const std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>>& objects) {
    if (!gbuffer_) return;
    
    // Begin G-Buffer rendering
    gbuffer_->BeginGeometryPass(device);
    
    // Render all objects to G-Buffer
    if (geometry_shader_) {
        geometry_shader_->Bind();
        
        for (const auto& [mesh, material] : objects) {
            if (!mesh) continue;
            
            // Bind material properties
            if (material) {
                // Set material uniforms
                geometry_shader_->SetVec3("u_Albedo", material->GetAlbedo());
                geometry_shader_->SetFloat("u_Metallic", material->GetMetallic());
                geometry_shader_->SetFloat("u_Roughness", material->GetRoughness());
            }
            
            // Render mesh
            mesh->Draw(device);
        }
        
        geometry_shader_->Unbind();
    }
    
    gbuffer_->EndGeometryPass(device);
}

void DeferredRenderer::LightCullingPass(RenderDevice* device, LightManager* light_manager) {
    if (!light_manager || !config_.enable_light_culling) return;
    
    auto active_lights = light_manager->GetActiveLights();
    visible_light_count_ = active_lights.size();
    total_light_count_ = active_lights.size();
}

void DeferredRenderer::LightingPass(RenderDevice* device, LightManager* light_manager, 
                                   const glm::mat4& view_matrix, const glm::mat4& proj_matrix) {
    if (!gbuffer_ || !light_manager) return;
    
    gbuffer_->BeginLightingPass(device);
    
    if (lighting_shader_) {
        lighting_shader_->Bind();
        
        // Set G-Buffer textures
        lighting_shader_->SetInt("u_Position", 0);
        lighting_shader_->SetInt("u_Normal", 1);
        lighting_shader_->SetInt("u_Albedo", 2);
        lighting_shader_->SetInt("u_Material", 3);
        lighting_shader_->SetInt("u_Depth", 4);
        
        // Set camera matrices
        lighting_shader_->SetMat4("u_ViewMatrix", view_matrix);
        lighting_shader_->SetMat4("u_ProjMatrix", proj_matrix);
        lighting_shader_->SetMat4("u_InvViewMatrix", glm::inverse(view_matrix));
        lighting_shader_->SetMat4("u_InvProjMatrix", glm::inverse(proj_matrix));
        
        // Set light data
        auto active_lights = light_manager->GetActiveLights();
        lighting_shader_->SetInt("u_LightCount", static_cast<int>(active_lights.size()));
        
        // Bind shadow maps and render lighting
        if (fullscreen_quad_) {
            fullscreen_quad_->Draw(device);
        }
        
        lighting_shader_->Unbind();
    }
    
    gbuffer_->EndLightingPass(device);
}

void DeferredRenderer::ForwardPass(RenderDevice* device, 
                                  const std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>>& transparent_objects) {
    if (config_.mode == RenderingMode::Deferred) return;
    
    // Setup forward rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    if (forward_shader_) {
        forward_shader_->Bind();
        
        for (const auto& [mesh, material] : transparent_objects) {
            if (!mesh) continue;
            
            if (material) {
                forward_shader_->SetVec3("u_Albedo", material->GetAlbedo());
                forward_shader_->SetFloat("u_Opacity", material->GetOpacity());
            }
            
            mesh->Draw(device);
        }
        
        forward_shader_->Unbind();
    }
    
    glDisable(GL_BLEND);
}

void DeferredRenderer::CompositePass(RenderDevice* device) {
    // Now rendering to screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    if (composite_shader_ && fullscreen_quad_) {
        composite_shader_->Bind();
        
        // Bind lighting result texture
        if (gbuffer_) {
            auto position = gbuffer_->GetPositionTexture();
            if (position) {
                position->Bind(0);
                composite_shader_->SetInt("u_SceneTexture", 0);
            }
        }
        
        fullscreen_quad_->Draw(device);
        composite_shader_->Unbind();
    }
}

void DeferredRenderer::CreateShaders(RenderDevice* device) {
    // These are placeholder shader creations - actual shader compilation would happen here
    // For now, we create dummy shaders that can be filled in later
    
    spdlog::debug("Creating deferred rendering shaders...");
    
    // Geometry pass shader - renders G-Buffer
    // Lighting pass shader - performs lighting calculations
    // Forward shader - transparent objects
    // Composite shader - final output
    // Debug shader - visualization
}

void DeferredRenderer::CreateBuffers(RenderDevice* device) {
    spdlog::debug("Creating deferred rendering buffers...");
    
    // Create light list buffer
    glGenBuffers(1, &light_index_buffer_);
    glBindBuffer(GL_COPY_READ_BUFFER, light_index_buffer_);
    glBufferData(GL_COPY_READ_BUFFER, sizeof(GLuint) * 65536, nullptr, GL_DYNAMIC_COPY);
}

void DeferredRenderer::SetupGeometryPass(RenderDevice* device) {
    // Setup for geometry pass
}

void DeferredRenderer::SetupLightingPass(RenderDevice* device) {
    // Setup for lighting pass
}

void DeferredRenderer::PerformLightCulling(RenderDevice* device) {
    // Perform light culling
}

void DeferredRenderer::ComputeLightTiles(RenderDevice* device) {
    // Compute tile-based light binning
}

// ============================================================================
// TileLightBinner Implementation
// ============================================================================

TileLightBinner::TileLightBinner(uint32_t width, uint32_t height, uint32_t tile_size)
    : width_(width), height_(height), tile_size_(tile_size) {
    tiles_x_ = (width + tile_size - 1) / tile_size;
    tiles_y_ = (height + tile_size - 1) / tile_size;
    tiles_.resize(tiles_x_ * tiles_y_);
}

void TileLightBinner::BinLights(const std::vector<std::shared_ptr<Light>>& lights, 
                               const glm::mat4& proj_matrix) {
    // Clear tiles
    for (auto& tile : tiles_) {
        tile.light_indices.clear();
        tile.light_count = 0;
    }
    
    // Bin each light into tiles based on projected bounds
    for (uint32_t light_idx = 0; light_idx < lights.size(); ++light_idx) {
        const auto& light = lights[light_idx];
        if (!light) continue;
        
        auto pos = light->GetPosition();
        auto range = light->GetRange();
        
        // Project light sphere bounds to screen space
        // For simplicity, approximate with bounding box
        
        // Determine affected tiles
        uint32_t min_tile_x = 0, max_tile_x = tiles_x_ - 1;
        uint32_t min_tile_y = 0, max_tile_y = tiles_y_ - 1;
        
        // Add light to affected tiles
        for (uint32_t ty = min_tile_y; ty <= max_tile_y; ++ty) {
            for (uint32_t tx = min_tile_x; tx <= max_tile_x; ++tx) {
                uint32_t tile_idx = ty * tiles_x_ + tx;
                if (tile_idx < tiles_.size()) {
                    tiles_[tile_idx].light_indices.push_back(light_idx);
                    tiles_[tile_idx].light_count++;
                }
            }
        }
    }
}

const TileLightBinner::Tile& TileLightBinner::GetTile(uint32_t x, uint32_t y) const {
    static Tile empty_tile;
    uint32_t tx = x / tile_size_;
    uint32_t ty = y / tile_size_;
    
    if (tx >= tiles_x_ || ty >= tiles_y_) {
        return empty_tile;
    }
    
    return tiles_[ty * tiles_x_ + tx];
}

uint32_t TileLightBinner::GetMaxTileLightCount() const {
    uint32_t max_count = 0;
    for (const auto& tile : tiles_) {
        max_count = std::max(max_count, tile.light_count);
    }
    return max_count;
}

// ============================================================================
// DeferredDebugger Implementation
// ============================================================================

void DeferredDebugger::VisualizeGBuffer(RenderDevice* device, GBuffer* gbuffer, uint32_t attachment_index) {
    if (!gbuffer) return;
    
    auto texture = gbuffer->GetAttachment(attachment_index);
    if (!texture) return;
    
    spdlog::info("Visualizing G-Buffer attachment: {}", gbuffer->GetAttachmentName(attachment_index));
    
    // Render texture to screen
    if (texture) {
        texture->Bind(0);
        // Render fullscreen quad with visualization shader
    }
}

void DeferredDebugger::VisualizeLightCount(RenderDevice* device, const std::vector<glm::vec4>& light_count_data,
                                         uint32_t width, uint32_t height) {
    spdlog::info("Visualizing light count heatmap: {}x{}", width, height);
}

void DeferredDebugger::VisualizeTileHeatmap(RenderDevice* device, const TileLightBinner& binner,
                                           uint32_t width, uint32_t height) {
    spdlog::info("Visualizing tile-based lighting heatmap");
}

} // namespace schizo::renderer
