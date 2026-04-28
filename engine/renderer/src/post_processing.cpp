#include "post_processing.h"
#include "render_device.h"
#include "shader.h"
#include "texture.h"
#include "framebuffer.h"
#include "mesh.h"
#include <glm/glm.hpp>

namespace schizo::renderer {

// ============================================================================
// PostProcessor Implementation
// ============================================================================

std::unique_ptr<PostProcessor> PostProcessor::Create(RenderDevice* device,
                                                        uint32_t width,
                                                        uint32_t height) {
    return std::make_unique<PostProcessor>(device, width, height);
}

PostProcessor::PostProcessor(RenderDevice* device, uint32_t width, uint32_t height)
    : device_(device), width_(width), height_(height), frame_time_(0.0f) {
}

PostProcessor::~PostProcessor() {
    Shutdown();
}

bool PostProcessor::Initialize() {
    if (!device_) {
        return false;
    }
    
    // Create pingpong framebuffers and textures
    CreateFramebuffers();
    
    // Create fullscreen quad for rendering
    CreateBlitQuad();
    
    // Create default shader for blitting
    std::string blit_vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string blit_fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        uniform sampler2D u_texture;
        layout(location = 0) out vec4 out_color;
        void main() {
            out_color = texture(u_texture, fs_in.uv);
        }
    )";
    
    blit_shader_ = std::make_shared<ShaderProgram>();
    if (!blit_shader_->Compile(device_, blit_vs, blit_fs)) {
        return false;
    }
    
    return true;
}

void PostProcessor::Shutdown() {
    effects_.clear();
    effect_map_.clear();
    pingpong_fb_[0] = nullptr;
    pingpong_fb_[1] = nullptr;
    pingpong_texture_[0] = nullptr;
    pingpong_texture_[1] = nullptr;
    fullscreen_quad_ = nullptr;
    blit_shader_ = nullptr;
}

void PostProcessor::CreateFramebuffers() {
    // Create ping-pong framebuffers
    pingpong_fb_[0] = Framebuffer::Create(device_, width_, height_,
                                          TextureFormat::RGBA16F, TextureFormat::Depth24);
    pingpong_fb_[1] = Framebuffer::Create(device_, width_, height_,
                                          TextureFormat::RGBA16F, TextureFormat::Depth24);
    
    // Get textures from framebuffers
    if (pingpong_fb_[0]) {
        pingpong_texture_[0] = pingpong_fb_[0]->GetColorTexture(0);
    }
    if (pingpong_fb_[1]) {
        pingpong_texture_[1] = pingpong_fb_[1]->GetColorTexture(0);
    }
    
    // Create intermediate textures for effects
    for (uint32_t i = 0; i < 4; ++i) {
        intermediate_textures_[i] = Texture::Create(device_, width_, height_,
                                                    TextureFormat::RGBA16F);
        intermediate_fbs_[i] = Framebuffer::Create(device_, width_, height_,
                                                    TextureFormat::RGBA16F, TextureFormat::Depth24);
    }
}

void PostProcessor::CreateBlitQuad() {
    // Create fullscreen quad for post-processing
    fullscreen_quad_ = Mesh::CreateQuad(device_);
}

bool PostProcessor::AddEffect(std::unique_ptr<PostProcessingEffect> effect,
                                PostProcessingEffectConfig* config) {
    if (!effect || !config) {
        return false;
    }
    
    if (!effect->Initialize(device_, config)) {
        return false;
    }
    
    PostProcessingEffectType type = effect->GetType();
    int type_int = static_cast<int>(type);
    
    // Remove existing effect of same type
    RemoveEffect(type);
    
    // Add new effect
    effect->SetEnabled(config->enabled);
    effect->SetIntensity(config->intensity);
    
    effect_map_[type_int] = effect.get();
    effects_.push_back(std::move(effect));
    
    // Sort by order
    std::sort(effects_.begin(), effects_.end(),
              [](const std::unique_ptr<PostProcessingEffect>& a,
                 const std::unique_ptr<PostProcessingEffect>& b) {
                  return a.get() < b.
              });
    
    return true;
}

void PostProcessor::RemoveEffect(PostProcessingEffectType type) {
    int type_int = static_cast<int>(type);
    effect_map_.erase(type_int);
    
    auto it = std::find_if(effects_.begin(), effects_.end(),
                          [type](const std::unique_ptr<PostProcessingEffect>& e) {
                              return e->GetType() == type;
                          });
    if (it != effects_.end()) {
        effects_.erase(it);
    }
}

PostProcessingEffect* PostProcessor::GetEffect(PostProcessingEffectType type) {
    return const_cast<PostProcessingEffect*>(
        static_cast<const PostProcessor*>(this)->GetEffect(type));
}

const PostProcessingEffect* PostProcessor::GetEffect(PostProcessingEffectType type) const {
    int type_int = static_cast<int>(type);
    auto it = effect_map_.find(type_int);
    if (it != effect_map_.end()) {
        return it->second;
    }
    return nullptr;
}

void PostProcessor::SetAllEffectsEnabled(bool enabled) {
    for (auto& effect : effects_) {
        effect->SetEnabled(enabled);
    }
}

void PostProcessor::SetEffectEnabled(PostProcessingEffectType type, bool enabled) {
    auto effect = GetEffect(type);
    if (effect) {
        effect->SetEnabled(enabled);
    }
}

uint32_t PostProcessor::GetActiveEffectCount() const {
    uint32_t count = 0;
    for (const auto& effect : effects_) {
        if (effect->IsEnabled()) {
            count++;
        }
    }
    return count;
}

void PostProcessor::Process(std::shared_ptr<Texture> source_texture,
                           Framebuffer* target_framebuffer,
                           float time_delta) {
    frame_time_ = time_delta;
    
    if (!source_texture || !target_framebuffer || effects_.empty()) {
        // No effects, just blit source to target
        if (source_texture && target_framebuffer) {
            target_framebuffer->Bind(device_);
            device_->SetViewport(0, 0, width_, height_);
            device_->Clear(true, true, glm::vec4(0.0f));
            
            blit_shader_->Use(device_);
            blit_shader_->SetInt(device_, "u_texture", 0);
            source_texture->Bind(device_, 0);
            fullscreen_quad_->Draw(device_);
            source_texture->Unbind(device_, 0);
        }
        return;
    }
    
    // Apply effects in sequence
    std::shared_ptr<Texture> current_input = source_texture;
    uint32_t current_pingpong = 0;
    
    for (size_t i = 0; i < effects_.size(); ++i) {
        auto& effect = effects_[i];
        
        if (!effect->IsEnabled()) {
            continue;
        }
        
        // Determine output target
        Framebuffer* output_fb = nullptr;
        if (i == effects_.size() - 1) {
            // Last effect outputs to final target
            output_fb = target_framebuffer;
        } else {
            // Intermediate effects use ping-pong buffers
            output_fb = pingpong_fb_[current_pingpong].get();
            current_pingpong = 1 - current_pingpong;
        }
        
        // Apply effect
        effect->Apply(device_, current_input, output_fb);
        
        // Update input for next effect
        if (output_fb == pingpong_fb_[current_pingpong].get()) {
            current_input = pingpong_texture_[current_pingpong];
        } else if (output_fb == target_framebuffer) {
            current_input = nullptr;  // Last effect already wrote to final target
        }
    }
}

void PostProcessor::Resize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    
    // Recreate framebuffers with new size
    CreateFramebuffers();
}

void PostProcessor::SetupDefaultStack(const ToneMapConfig* tm_config,
                                     const BloomConfig* bloom_config,
                                     const FXAAConfig* fxaa_config) {
    // Add tone mapping
    auto tonemap = std::make_unique<ToneMappingEffect>();
    ToneMapConfig tm_cfg = tm_config ? *tm_config : ToneMapConfig();
    tm_cfg.order = 0;
    AddEffect(std::move(tonemap), &tm_cfg);
    
    // Add bloom
    auto bloom = std::make_unique<BloomEffect>();
    BloomConfig bloom_cfg = bloom_config ? *bloom_config : BloomConfig();
    bloom_cfg.order = 1;
    AddEffect(std::move(bloom), &bloom_cfg);
    
    // Add FXAA
    auto fxaa = std::make_unique<FXAAEffect>();
    FXAAConfig fxaa_cfg = fxaa_config ? *fxaa_config : FXAAConfig();
    fxaa_cfg.order = 2;
    AddEffect(std::move(fxaa), &fxaa_cfg);
}

PostProcessingEffect* PostProcessor::FindEffect(PostProcessingEffectType type) {
    int type_int = static_cast<int>(type);
    auto it = effect_map_.find(type_int);
    if (it != effect_map_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Texture> PostProcessor::GetIntermediateTexture(uint32_t index) {
    if (index < 4) {
        return intermediate_textures_[index];
    }
    return nullptr;
}

Framebuffer* PostProcessor::GetPingPongBuffer(uint32_t index) {
    if (index < 2) {
        return pingpong_fb_[index].get();
    }
    return nullptr;
}

// ============================================================================
// Tone Mapping Effect Implementation
// ============================================================================

bool ToneMappingEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<ToneMapConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_hdr_texture;
        uniform int u_tonemap_op;
        uniform float u_exposure;
        uniform float u_gamma;
        uniform float u_white_point;
        
        layout(location = 0) out vec4 out_color;
        
        vec3 Reinhard(vec3 color) {
            return color / (color + vec3(1.0));
        }
        
        vec3 ReinhardLuminance(vec3 color) {
            float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
            float reinhard = lum / (lum + 1.0);
            return color * (reinhard / lum);
        }
        
        vec3 FilmicALU(vec3 color) {
            color = max(vec3(0.0), color - vec3(0.004));
            color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
            return color;
        }
        
        vec3 Uncharted2Tonemap(vec3 x) {
            float A = 0.15;
            float B = 0.50;
            float C = 0.10;
            float D = 0.20;
            float E = 0.02;
            float F = 0.30;
            
            return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
        }
        
        vec3 Uncharted2(vec3 color) {
            float W = 11.2;
            color = Uncharted2Tonemap(color * 2.0);
            vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
            return color * whiteScale;
        }
        
        vec3 ACESTonemap(vec3 color) {
            const mat3 input_matrix = mat3(
                0.59719, 0.35191, 0.04892,
                0.07600, 0.90834, 0.01566,
                0.02840, 0.13383, 0.83777
            );
            
            const mat3 output_matrix = mat3(
                 1.60475, -0.53108, -0.07367,
                -0.10208,  1.10813, -0.00605,
                -0.00327, -0.07276,  1.07602
            );
            
            color = input_matrix * color;
            
            const float A = 0.2590235;
            const float B = 0.2468905;
            const float C = 0.2427592;
            const float D = 0.0821812;
            const float E = 0.0048741;
            const float F = 0.6435069;
            
            color = (color * (A * color + B)) / (color * (C * color + D) + E);
            color = clamp(color, 0.0, 1.0);
            color = output_matrix * color;
            
            return color;
        }
        
        void main() {
            vec3 hdr_color = texture(u_hdr_texture, fs_in.uv).rgb;
            
            // Apply exposure
            hdr_color *= u_exposure;
            
            // Tonemap
            vec3 tone_mapped;
            if (u_tonemap_op == 0) { // Reinhard
                tone_mapped = Reinhard(hdr_color);
            } else if (u_tonemap_op == 1) { // ReinhardLuminance
                tone_mapped = ReinhardLuminance(hdr_color);
            } else if (u_tonemap_op == 2) { // FilmicALU
                tone_mapped = FilmicALU(hdr_color);
            } else if (u_tonemap_op == 3) { // Uncharted2
                tone_mapped = Uncharted2(hdr_color);
            } else if (u_tonemap_op == 4) { // ACES
                tone_mapped = ACESTonemap(hdr_color);
            } else { // Linear
                tone_mapped = hdr_color;
            }
            
            // Apply gamma correction
            tone_mapped = pow(tone_mapped, vec3(1.0 / u_gamma));
            
            out_color = vec4(tone_mapped, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void ToneMappingEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                             Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_hdr_texture", 0);
    shader_->SetInt(device, "u_tonemap_op", static_cast<int>(config_->op));
    shader_->SetFloat(device, "u_exposure", config_->exposure * intensity_);
    shader_->SetFloat(device, "u_gamma", config_->gamma);
    shader_->SetFloat(device, "u_white_point", config_->white_point);
    
    source_texture->Bind(device, 0);
    
    // Draw fullscreen quad
    // Note: This requires mesh to be available from post processor
}

void ToneMappingEffect::SetOperator(TonemapOperator op) {
    if (config_) {
        config_->op = op;
    }
}

void ToneMappingEffect::SetExposure(float exposure) {
    if (config_) {
        config_->exposure = exposure;
    }
}

void ToneMappingEffect::SetGamma(float gamma) {
    if (config_) {
        config_->gamma = gamma;
    }
}

// ============================================================================
// Bloom Effect Implementation
// ============================================================================

bool BloomEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<BloomConfig*>(config);
    if (!config_) {
        return false;
    }
    
    // Threshold shader
    std::string threshold_vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string threshold_fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform float u_threshold;
        uniform float u_knee;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            float brightness = max(max(color.r, color.g), color.b);
            
            // Soft threshold with knee
            float soft = smoothstep(u_threshold - u_knee, u_threshold + u_knee, brightness);
            out_color = vec4(color * soft, 1.0);
        }
    )";
    
    threshold_shader_ = std::make_shared<ShaderProgram>();
    if (!threshold_shader_->Compile(device, threshold_vs, threshold_fs)) {
        return false;
    }
    
    // Gaussian blur shader
    std::string blur_vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string blur_fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform vec2 u_direction;
        uniform float u_radius;
        uniform int u_samples;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 result = vec3(0.0);
            float weight_sum = 0.0;
            
            for (int i = -u_samples/2; i <= u_samples/2; i++) {
                float offset = float(i);
                float weight = exp(-(offset * offset) / (2.0 * u_radius * u_radius));
                vec2 uv = fs_in.uv + u_direction * offset * u_radius / 4.0;
                result += texture(u_texture, uv).rgb * weight;
                weight_sum += weight;
            }
            
            out_color = vec4(result / weight_sum, 1.0);
        }
    )";
    
    blur_shader_ = std::make_shared<ShaderProgram>();
    if (!blur_shader_->Compile(device, blur_vs, blur_fs)) {
        return false;
    }
    
    // Composite shader
    std::string comp_vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string comp_fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_original;
        uniform sampler2D u_bloom;
        uniform float u_strength;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 original = texture(u_original, fs_in.uv).rgb;
            vec3 bloom = texture(u_bloom, fs_in.uv).rgb;
            out_color = vec4(original + bloom * u_strength, 1.0);
        }
    )";
    
    composite_shader_ = std::make_shared<ShaderProgram>();
    return composite_shader_->Compile(device, comp_vs, comp_fs);
}

void BloomEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                       Framebuffer* target_framebuffer) {
    if (!threshold_shader_ || !blur_shader_ || !composite_shader_ || 
        !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    // Note: Full bloom implementation would require intermediate framebuffers
    // This is a simplified version showing the shader structure
}

void BloomEffect::SetThreshold(float threshold) {
    if (config_) {
        config_->threshold = threshold;
    }
}

void BloomEffect::SetStrength(float strength) {
    if (config_) {
        config_->strength = strength;
    }
}

// ============================================================================
// FXAA Effect Implementation
// ============================================================================

bool FXAAEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<FXAAConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform vec2 u_texel_size;
        uniform float u_edge_threshold;
        uniform float u_edge_threshold_min;
        uniform bool u_show_edges;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            
            // FXAA implementation
            vec3 luma = vec3(0.299, 0.587, 0.114);
            float lum = dot(color, luma);
            
            float lumNW = dot(texture(u_texture, fs_in.uv + vec2(-1, -1) * u_texel_size).rgb, luma);
            float lumNE = dot(texture(u_texture, fs_in.uv + vec2(1, -1) * u_texel_size).rgb, luma);
            float lumSW = dot(texture(u_texture, fs_in.uv + vec2(-1, 1) * u_texel_size).rgb, luma);
            float lumSE = dot(texture(u_texture, fs_in.uv + vec2(1, 1) * u_texel_size).rgb, luma);
            
            float lumMin = min(lum, min(min(lumNW, lumNE), min(lumSW, lumSE)));
            float lumMax = max(lum, max(max(lumNW, lumNE), max(lumSW, lumSE)));
            
            float range = lumMax - lumMin;
            if (range < max(u_edge_threshold_min, lumMax * u_edge_threshold)) {
                out_color = vec4(color, 1.0);
                return;
            }
            
            if (u_show_edges) {
                out_color = vec4(vec3(1.0, 0.0, 0.0), 1.0);
                return;
            }
            
            out_color = vec4(color, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void FXAAEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                      Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_edge_threshold", config_->edge_threshold);
    shader_->SetFloat(device, "u_edge_threshold_min", config_->edge_threshold_min);
    shader_->SetBool(device, "u_show_edges", config_->show_edges);
    
    source_texture->Bind(device, 0);
    
    // Draw quad
}

// ============================================================================
// Vignette Effect Implementation
// ============================================================================

bool VignetteEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<VignetteConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform float u_radius;
        uniform float u_softness;
        uniform float u_darkness;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            
            vec2 center = fs_in.uv - 0.5;
            float vignette = length(center) / u_radius;
            vignette = smoothstep(u_radius, u_radius - u_softness, length(center));
            vignette = mix(1.0, vignette, u_darkness);
            
            out_color = vec4(color * vignette, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void VignetteEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                          Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_radius", config_->radius);
    shader_->SetFloat(device, "u_softness", config_->softness);
    shader_->SetFloat(device, "u_darkness", config_->darkness * intensity_);
    
    source_texture->Bind(device, 0);
}

// ============================================================================
// Sharpen Effect Implementation
// ============================================================================

bool SharpenEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<SharpenConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform vec2 u_texel_size;
        uniform float u_amount;
        uniform float u_radius;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            
            // Unsharp mask
            vec3 sum = vec3(0.0);
            float radius = u_radius;
            
            sum += texture(u_texture, fs_in.uv + vec2(-radius, -radius) * u_texel_size).rgb * 0.075;
            sum += texture(u_texture, fs_in.uv + vec2(0, -radius) * u_texel_size).rgb * 0.123;
            sum += texture(u_texture, fs_in.uv + vec2(radius, -radius) * u_texel_size).rgb * 0.075;
            
            sum += texture(u_texture, fs_in.uv + vec2(-radius, 0) * u_texel_size).rgb * 0.123;
            sum += texture(u_texture, fs_in.uv).rgb * 0.201;
            sum += texture(u_texture, fs_in.uv + vec2(radius, 0) * u_texel_size).rgb * 0.123;
            
            sum += texture(u_texture, fs_in.uv + vec2(-radius, radius) * u_texel_size).rgb * 0.075;
            sum += texture(u_texture, fs_in.uv + vec2(0, radius) * u_texel_size).rgb * 0.123;
            sum += texture(u_texture, fs_in.uv + vec2(radius, radius) * u_texel_size).rgb * 0.075;
            
            vec3 sharp = color + (color - sum) * u_amount;
            out_color = vec4(sharp, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void SharpenEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                         Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_amount", config_->amount * intensity_);
    shader_->SetFloat(device, "u_radius", config_->radius);
    
    source_texture->Bind(device, 0);
}

// ============================================================================
// Chromatic Aberration Effect Implementation
// ============================================================================

bool ChromaticAberrationEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<ChromaticAberrationConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform float u_amount;
        uniform bool u_use_depth;
        uniform sampler2D u_depth;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec2 center = fs_in.uv - 0.5;
            vec2 direction = normalize(center);
            
            float dist = length(center) * 2.0 * u_amount;
            
            vec4 r = texture(u_texture, fs_in.uv + direction * dist * 0.1);
            vec4 g = texture(u_texture, fs_in.uv);
            vec4 b = texture(u_texture, fs_in.uv - direction * dist * 0.1);
            
            out_color = vec4(r.r, g.g, b.b, g.a);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void ChromaticAberrationEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                                      Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_amount", config_->amount * intensity_);
    shader_->SetBool(device, "u_use_depth", config_->use_depth);
    
    source_texture->Bind(device, 0);
}

// ============================================================================
// Film Grain Effect Implementation
// ============================================================================

bool FilmGrainEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<FilmGrainConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform float u_amount;
        uniform float u_scale;
        uniform bool u_colored;
        uniform float u_time;
        
        layout(location = 0) out vec4 out_color;
        
        // Simple pseudo-random function
        float rand(vec2 co) {
            return fract(sin(dot(co.xy, vec2(12.9898, 78.233)) + u_time) * 43758.5453);
        }
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            
            if (u_colored) {
                vec3 grain = vec3(
                    rand(fs_in.uv * u_scale),
                    rand(fs_in.uv * u_scale + 0.5),
                    rand(fs_in.uv * u_scale + 1.0)
                );
                color += (grain - 0.5) * u_amount;
            } else {
                float grain = rand(fs_in.uv * u_scale);
                color += (grain - 0.5) * u_amount;
            }
            
            out_color = vec4(color, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void FilmGrainEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                           Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_amount", config_->amount * intensity_);
    shader_->SetFloat(device, "u_scale", config_->scale);
    shader_->SetBool(device, "u_colored", config_->colored);
    
    source_texture->Bind(device, 0);
}

// ============================================================================
// Color Grading Effect Implementation
// ============================================================================

bool ColorGradingEffect::Initialize(RenderDevice* device, PostProcessingEffectConfig* config) {
    config_ = dynamic_cast<ColorGradingConfig*>(config);
    if (!config_) {
        return false;
    }
    
    std::string vs = R"(
        #version 450 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        out VS_OUT {
            vec2 uv;
        } vs_out;
        void main() {
            gl_Position = vec4(position, 1.0);
            vs_out.uv = uv;
        }
    )";
    
    std::string fs = R"(
        #version 450 core
        in VS_OUT {
            vec2 uv;
        } fs_in;
        
        uniform sampler2D u_texture;
        uniform sampler3D u_lut;
        uniform float u_saturation;
        uniform float u_contrast;
        uniform vec3 u_color_filter;
        
        layout(location = 0) out vec4 out_color;
        
        void main() {
            vec3 color = texture(u_texture, fs_in.uv).rgb;
            
            // Apply color filter
            color *= u_color_filter;
            
            // Apply contrast
            color = (color - 0.5) * u_contrast + 0.5;
            
            // Apply saturation
            float gray = dot(color, vec3(0.299, 0.587, 0.114));
            color = mix(vec3(gray), color, u_saturation);
            
            // Clamp for LUT lookup
            color = clamp(color, 0.0, 1.0);
            
            // Apply 3D LUT (if available)
            // color = texture(u_lut, color).rgb;
            
            out_color = vec4(color, 1.0);
        }
    )";
    
    shader_ = std::make_shared<ShaderProgram>();
    return shader_->Compile(device, vs, fs);
}

void ColorGradingEffect::Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
                              Framebuffer* target_framebuffer) {
    if (!shader_ || !source_texture || !target_framebuffer || !config_) {
        return;
    }
    
    target_framebuffer->Bind(device);
    device->Clear(true, true, glm::vec4(0.0f));
    
    shader_->Use(device);
    shader_->SetInt(device, "u_texture", 0);
    shader_->SetFloat(device, "u_saturation", config_->saturation);
    shader_->SetFloat(device, "u_contrast", config_->contrast);
    shader_->SetVec3(device, "u_color_filter", config_->color_filter);
    
    source_texture->Bind(device, 0);
    
    if (config_->lut_texture) {
        shader_->SetInt(device, "u_lut", 1);
        config_->lut_texture->Bind(device, 1);
    }
}

void ColorGradingEffect::SetLUT(std::shared_ptr<Texture> lut) {
    if (config_) {
        config_->lut_texture = lut;
    }
}

} // namespace schizo::renderer
