#include "light.h"
#include "shadow.h"
#include "ibl.h"
#include "lighting.h"
#include "render_device.h"
#include "framebuffer.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// Light Implementation
// ============================================================================

class OpenGLLight : public Light {
public:
    explicit OpenGLLight(LightType type, const std::string& name = "Light")
        : type_(type), name_(name), enabled_(true),
          position_(0.0f), direction_(0.0f, -1.0f, 0.0f),
          color_(1.0f), intensity_(1.0f),
          range_(10.0f), linear_attenuation_(0.045f), quadratic_attenuation_(0.0075f),
          inner_angle_(30.0f), outer_angle_(45.0f), falloff_(2.0f),
          cast_shadow_(false), shadow_quality_(ShadowQuality::Medium),
          shadow_near_(0.1f), shadow_far_(100.0f),
          shadow_bias_(0.005f), shadow_normal_bias_(0.01f), shadow_filter_(1.0f),
          temperature_(6500.0f), volumetric_intensity_(0.0f), cascade_count_(1) {
        
        rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        spdlog::debug("Created {} light: {}", type == LightType::Directional ? "directional" : 
                                              type == LightType::Point ? "point" : "spot", name);
    }
    
    ~OpenGLLight() override = default;
    
    void SetName(const std::string& name) override { name_ = name; }
    std::string GetName() const override { return name_; }
    
    LightType GetType() const override { return type_; }
    
    void SetEnabled(bool enabled) override { enabled_ = enabled; }
    bool IsEnabled() const override { return enabled_; }
    
    void SetPosition(const glm::vec3& position) override { position_ = position; }
    glm::vec3 GetPosition() const override { return position_; }
    
    void SetDirection(const glm::vec3& direction) override { 
        direction_ = glm::normalize(direction);
    }
    glm::vec3 GetDirection() const override { return direction_; }
    
    void SetRotation(const glm::quat& rotation) override { rotation_ = rotation; }
    glm::quat GetRotation() const override { return rotation_; }
    
    void SetColor(const glm::vec3& color) override { color_ = color; }
    glm::vec3 GetColor() const override { return color_; }
    
    void SetIntensity(float intensity) override { intensity_ = glm::max(0.0f, intensity); }
    float GetIntensity() const override { return intensity_; }
    
    void SetRange(float range) override { range_ = glm::max(0.1f, range); }
    float GetRange() const override { return range_; }
    
    void SetAttenuation(float linear, float quadratic) override {
        linear_attenuation_ = glm::max(0.0f, linear);
        quadratic_attenuation_ = glm::max(0.0f, quadratic);
    }
    glm::vec2 GetAttenuation() const override {
        return glm::vec2(linear_attenuation_, quadratic_attenuation_);
    }
    
    void SetSpotAngles(float inner_angle, float outer_angle) override {
        inner_angle_ = glm::clamp(inner_angle, 0.0f, 180.0f);
        outer_angle_ = glm::clamp(outer_angle, inner_angle_, 180.0f);
    }
    glm::vec2 GetSpotAngles() const override {
        return glm::vec2(inner_angle_, outer_angle_);
    }
    
    void SetSpotFalloff(float falloff) override {
        falloff_ = glm::max(0.1f, falloff);
    }
    float GetSpotFalloff() const override { return falloff_; }
    
    void SetCastShadow(bool cast_shadow) override { cast_shadow_ = cast_shadow; }
    bool GetCastShadow() const override { return cast_shadow_; }
    
    void SetShadowQuality(ShadowQuality quality) override { shadow_quality_ = quality; }
    ShadowQuality GetShadowQuality() const override { return shadow_quality_; }
    
    void SetShadowPlanes(float near_plane, float far_plane) override {
        shadow_near_ = glm::max(0.01f, near_plane);
        shadow_far_ = glm::max(shadow_near_ + 1.0f, far_plane);
    }
    glm::vec2 GetShadowPlanes() const override {
        return glm::vec2(shadow_near_, shadow_far_);
    }
    
    void SetShadowBias(float bias, float normal_bias) override {
        shadow_bias_ = glm::max(0.0001f, bias);
        shadow_normal_bias_ = glm::max(0.0f, normal_bias);
    }
    glm::vec2 GetShadowBias() const override {
        return glm::vec2(shadow_bias_, shadow_normal_bias_);
    }
    
    void SetShadowFilter(float filter_radius) override {
        shadow_filter_ = glm::max(0.0f, filter_radius);
    }
    float GetShadowFilter() const override { return shadow_filter_; }
    
    std::shared_ptr<Framebuffer> GetShadowFramebuffer() const override {
        return shadow_framebuffer_;
    }
    
    std::shared_ptr<Texture2D> GetShadowMap() const override {
        return shadow_map_;
    }
    
    glm::mat4 GetViewMatrix() const override {
        return glm::lookAt(position_, position_ + direction_, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    
    glm::mat4 GetProjectionMatrix() const override {
        if (type_ == LightType::Directional) {
            // Orthographic projection for directional light
            float extent = 50.0f;
            return glm::ortho(-extent, extent, -extent, extent, shadow_near_, shadow_far_);
        } else {
            // Perspective projection for point/spot lights
            float aspect = 1.0f;
            float fov = (type_ == LightType::Spot) ? 
                       glm::radians(outer_angle_ * 2.0f) : 
                       glm::radians(90.0f);
            return glm::perspective(fov, aspect, shadow_near_, shadow_far_);
        }
    }
    
    glm::mat4 GetViewProjectionMatrix() const override {
        return GetProjectionMatrix() * GetViewMatrix();
    }
    
    void SetTemperature(float kelvin) override {
        temperature_ = glm::clamp(kelvin, 1000.0f, 20000.0f);
    }
    float GetTemperature() const override { return temperature_; }
    
    static glm::vec3 TemperatureToColor(float kelvin) {
        // Convert Kelvin temperature to RGB color
        // Based on Tanner Helland's algorithm
        kelvin /= 100.0f;
        
        float red, green, blue;
        
        if (kelvin <= 66.0f) {
            red = 1.0f;
            green = (0.39008157876f * std::log(kelvin) - 0.64184144378f);
        } else {
            red = 0.55040944831f * std::pow(kelvin - 60.0f, -0.20042880632f);
            green = 0.54883271241f * std::pow(kelvin - 60.0f, 0.07381422255f);
        }
        
        if (kelvin >= 66.0f) {
            blue = 1.0f;
        } else if (kelvin <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = 0.35298683506f * std::log(kelvin - 10.0f) - 0.69316600789f;
        }
        
        return glm::vec3(
            glm::clamp(red, 0.0f, 1.0f),
            glm::clamp(green, 0.0f, 1.0f),
            glm::clamp(blue, 0.0f, 1.0f)
        );
    }
    
    void SetCookieTexture(std::shared_ptr<Texture2D> texture) override {
        cookie_texture_ = texture;
    }
    std::shared_ptr<Texture2D> GetCookieTexture() const override {
        return cookie_texture_;
    }
    
    void SetVolumetricIntensity(float intensity) override {
        volumetric_intensity_ = glm::max(0.0f, intensity);
    }
    float GetVolumetricIntensity() const override { return volumetric_intensity_; }
    
    void SetCascadeCount(uint32_t count) override {
        cascade_count_ = glm::clamp(count, 1u, 4u);
    }
    uint32_t GetCascadeCount() const override { return cascade_count_; }

private:
    LightType type_;
    std::string name_;
    bool enabled_;
    
    glm::vec3 position_;
    glm::vec3 direction_;
    glm::quat rotation_;
    glm::vec3 color_;
    float intensity_;
    
    float range_;
    float linear_attenuation_;
    float quadratic_attenuation_;
    float inner_angle_;
    float outer_angle_;
    float falloff_;
    
    bool cast_shadow_;
    ShadowQuality shadow_quality_;
    float shadow_near_;
    float shadow_far_;
    float shadow_bias_;
    float shadow_normal_bias_;
    float shadow_filter_;
    
    std::shared_ptr<Framebuffer> shadow_framebuffer_;
    std::shared_ptr<Texture2D> shadow_map_;
    
    float temperature_;
    std::shared_ptr<Texture2D> cookie_texture_;
    float volumetric_intensity_;
    uint32_t cascade_count_;
};

std::unique_ptr<Light> Light::Create(LightType type, const std::string& name) {
    return std::make_unique<OpenGLLight>(type, name);
}

glm::vec3 Light::TemperatureToColor(float kelvin) {
    return OpenGLLight::TemperatureToColor(kelvin);
}

// ============================================================================
// LightManager Implementation
// ============================================================================

class OpenGLLightManager : public LightManager {
public:
    explicit OpenGLLightManager(RenderDevice* device, const LightingConfig& config)
        : device_(device), config_(config), ambient_light_(0.1f),
          ambient_color_(0.5f), main_light_(nullptr) {
        
        spdlog::info("LightManager created with max {} lights", config.max_lights);
    }
    
    ~OpenGLLightManager() override = default;
    
    std::shared_ptr<Light> AddLight(LightType type, const std::string& name) override {
        if (lights_.size() >= config_.max_lights) {
            spdlog::warn("LightManager::AddLight - Max lights reached");
            return nullptr;
        }
        
        auto light = Light::Create(type, name);
        lights_.push_back(light);
        
        spdlog::debug("Added light: {} (total: {})", name, lights_.size());
        return light;
    }
    
    void RemoveLight(std::shared_ptr<Light> light) override {
        auto it = std::find(lights_.begin(), lights_.end(), light);
        if (it != lights_.end()) {
            lights_.erase(it);
            if (main_light_ == light) {
                main_light_ = nullptr;
            }
            spdlog::debug("Removed light (total: {})", lights_.size());
        }
    }
    
    std::shared_ptr<Light> GetLight(const std::string& name) const override {
        for (const auto& light : lights_) {
            if (light->GetName() == name) {
                return light;
            }
        }
        return nullptr;
    }
    
    std::vector<std::shared_ptr<Light>> GetLights() const override {
        return lights_;
    }
    
    std::vector<std::shared_ptr<Light>> GetLightsByType(LightType type) const override {
        std::vector<std::shared_ptr<Light>> result;
        for (const auto& light : lights_) {
            if (light->GetType() == type) {
                result.push_back(light);
            }
        }
        return result;
    }
    
    std::vector<std::shared_ptr<Light>> GetActiveLights() const override {
        std::vector<std::shared_ptr<Light>> result;
        for (const auto& light : lights_) {
            if (light->IsEnabled()) {
                result.push_back(light);
            }
        }
        return result;
    }
    
    std::vector<std::shared_ptr<Light>> GetShadowCastingLights() const override {
        std::vector<std::shared_ptr<Light>> result;
        for (const auto& light : lights_) {
            if (light->IsEnabled() && light->GetCastShadow()) {
                result.push_back(light);
            }
        }
        return result;
    }
    
    uint32_t GetLightCount() const override { return lights_.size(); }
    
    void SetMainLight(std::shared_ptr<Light> light) override {
        main_light_ = light;
        spdlog::debug("Set main light: {}", light ? light->GetName() : "none");
    }
    std::shared_ptr<Light> GetMainLight() const override { return main_light_; }
    
    void SetAmbientLight(float intensity) override {
        ambient_light_ = glm::max(0.0f, intensity);
    }
    float GetAmbientLight() const override { return ambient_light_; }
    
    void SetAmbientColor(const glm::vec3& color) override { ambient_color_ = color; }
    glm::vec3 GetAmbientColor() const override { return ambient_color_; }
    
    void SetEnvironmentMap(std::shared_ptr<EnvironmentMap> env_map) override {
        env_map_ = env_map;
    }
    std::shared_ptr<EnvironmentMap> GetEnvironmentMap() const override {
        return env_map_;
    }
    
    void SetConfig(const LightingConfig& config) override {
        config_ = config;
    }
    LightingConfig GetConfig() const override { return config_; }
    
    void SetDynamicShadows(bool enabled) override {
        config_.enable_dynamic_shadows = enabled;
    }
    bool GetDynamicShadows() const override {
        return config_.enable_dynamic_shadows;
    }
    
    glm::vec3 CalculateLighting(const glm::vec3& position,
                               const glm::vec3& normal,
                               const glm::vec3& view_dir) const override {
        glm::vec3 result = ambient_color_ * ambient_light_;
        
        for (const auto& light : lights_) {
            if (light->IsEnabled()) {
                result += CalculateLightContribution(light, position, normal, view_dir);
            }
        }
        
        return result;
    }
    
    glm::vec3 CalculateLightContribution(std::shared_ptr<Light> light,
                                        const glm::vec3& position,
                                        const glm::vec3& normal,
                                        const glm::vec3& view_dir) const override {
        if (!light || !light->IsEnabled()) {
            return glm::vec3(0.0f);
        }
        
        glm::vec3 light_dir = glm::normalize(light->GetPosition() - position);
        float distance = glm::distance(light->GetPosition(), position);
        
        // Basic Lambertian + Blinn-Phong
        glm::vec3 diffuse = LightingUtils::Lambertian(
            light->GetColor(), normal, light_dir, light->GetIntensity()
        );
        
        glm::vec3 specular = LightingUtils::BlinnPhong(
            light->GetColor(), normal, light_dir, view_dir, 32.0f, light->GetIntensity()
        );
        
        // Attenuation for point/spot lights
        float attenuation = 1.0f;
        if (light->GetType() == LightType::Point) {
            attenuation = LightingUtils::PointLightAttenuation(distance, light->GetRange());
        }
        
        return (diffuse + specular) * attenuation;
    }
    
    std::vector<std::shared_ptr<Light>> GetNearestLights(const glm::vec3& position,
                                                        uint32_t max_lights) const override {
        std::vector<std::pair<float, std::shared_ptr<Light>>> distance_lights;
        
        for (const auto& light : lights_) {
            if (light->IsEnabled() && light->GetType() != LightType::Directional) {
                float dist = glm::distance(light->GetPosition(), position);
                if (dist < light->GetRange()) {
                    distance_lights.push_back({dist, light});
                }
            }
        }
        
        // Directional light is always included
        if (main_light_ && main_light_->IsEnabled() && main_light_->GetType() == LightType::Directional) {
            distance_lights.push_back({0.0f, main_light_});
        }
        
        std::sort(distance_lights.begin(), distance_lights.end());
        
        std::vector<std::shared_ptr<Light>> result;
        for (uint32_t i = 0; i < std::min((uint32_t)distance_lights.size(), max_lights); ++i) {
            result.push_back(distance_lights[i].second);
        }
        
        return result;
    }

private:
    RenderDevice* device_;
    LightingConfig config_;
    std::vector<std::shared_ptr<Light>> lights_;
    std::shared_ptr<Light> main_light_;
    std::shared_ptr<EnvironmentMap> env_map_;
    float ambient_light_;
    glm::vec3 ambient_color_;
};

std::unique_ptr<LightManager> LightManager::Create(RenderDevice* device,
                                                   const LightingConfig& config) {
    if (!device) {
        spdlog::error("LightManager::Create - No render device");
        return nullptr;
    }
    
    return std::make_unique<OpenGLLightManager>(device, config);
}

// ============================================================================
// Lighting Utilities Implementation
// ============================================================================

glm::vec3 LightingUtils::Lambertian(const glm::vec3& light_color,
                                   const glm::vec3& normal,
                                   const glm::vec3& light_dir,
                                   float intensity) {
    float ndotl = glm::max(0.0f, glm::dot(normal, light_dir));
    return light_color * ndotl * intensity;
}

glm::vec3 LightingUtils::BlinnPhong(const glm::vec3& light_color,
                                   const glm::vec3& normal,
                                   const glm::vec3& light_dir,
                                   const glm::vec3& view_dir,
                                   float shininess,
                                   float intensity) {
    glm::vec3 half_dir = glm::normalize(light_dir + view_dir);
    float spec = std::pow(glm::max(0.0f, glm::dot(normal, half_dir)), shininess);
    return light_color * spec * intensity;
}

float LightingUtils::PointLightAttenuation(float distance,
                                          float range,
                                          float linear,
                                          float quadratic) {
    float attenuation = 1.0f / (1.0f + linear * distance + quadratic * distance * distance);
    return attenuation * glm::smoothstep(range, 0.0f, distance);
}

float LightingUtils::SpotLightFalloff(float cos_theta,
                                     float cos_inner,
                                     float cos_outer,
                                     float falloff) {
    if (cos_theta < cos_outer) return 0.0f;
    if (cos_theta >= cos_inner) return 1.0f;
    
    float epsilon = cos_inner - cos_outer;
    float interpolated = (cos_theta - cos_outer) / epsilon;
    return std::pow(interpolated, falloff);
}

float LightingUtils::SmoothCone(float cos_theta,
                              float cos_inner,
                              float cos_outer) {
    return glm::smoothstep(cos_outer, cos_inner, cos_theta);
}

float LightingUtils::InverseSquareLaw(float distance,
                                     float falloff_start) {
    return 1.0f / (distance * distance + falloff_start);
}

float LightingUtils::RangeToRadius(float intensity,
                                  float min_brightness) {
    if (intensity <= 0.0f) return 0.0f;
    return std::sqrt(intensity / min_brightness);
}

glm::vec2 LightingUtils::CalculateShadowBias(LightType light_type,
                                            float normal_offset) {
    float bias = 0.005f;
    switch (light_type) {
        case LightType::Directional:
            bias = 0.00002f;
            break;
        case LightType::Point:
            bias = 0.005f;
            break;
        case LightType::Spot:
            bias = 0.005f;
            break;
    }
    return glm::vec2(bias, normal_offset);
}

glm::mat4 LightingUtils::CalculateLightSpaceMatrix(const glm::vec3& light_pos,
                                                  const glm::vec3& light_dir,
                                                  float view_distance) {
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(light_dir, up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    glm::mat4 view = glm::lookAt(light_pos, light_pos + light_dir, up);
    glm::mat4 projection = glm::ortho(-view_distance, view_distance, 
                                      -view_distance, view_distance,
                                      0.1f, view_distance * 2.0f);
    
    return projection * view;
}

bool LightingUtils::LightAffectsAABB(std::shared_ptr<Light> light,
                                   const glm::vec3& aabb_min,
                                   const glm::vec3& aabb_max) {
    if (!light) return false;
    
    glm::vec3 center = (aabb_min + aabb_max) * 0.5f;
    glm::vec3 extents = (aabb_max - aabb_min) * 0.5f;
    float radius = glm::length(extents);
    
    return LightAffectsSphere(light, center, radius);
}

bool LightingUtils::LightAffectsSphere(std::shared_ptr<Light> light,
                                     const glm::vec3& sphere_center,
                                     float sphere_radius) {
    if (!light) return false;
    
    if (light->GetType() == LightType::Directional) {
        return true;  // Directional lights affect everything
    }
    
    float distance = glm::distance(light->GetPosition(), sphere_center);
    return distance < (light->GetRange() + sphere_radius);
}

} // namespace schizo::renderer
