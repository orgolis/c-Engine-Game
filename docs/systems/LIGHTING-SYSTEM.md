# Lighting System

## Overview

The Lighting System provides a comprehensive solution for managing all aspects of dynamic and static lighting in the GameWorldshaper engine. It includes support for multiple light types, shadow mapping, image-based lighting (IBL), and advanced lighting utilities.

## Architecture

### Core Components

#### 1. Light Types

```cpp
enum class LightType {
    Directional,  // Sun-like light, infinite distance, parallel rays
    Point,        // Omnidirectional light from a point
    Spot,         // Cone-shaped light with direction
};
```

#### 2. Shadow Quality Levels

```cpp
enum class ShadowQuality {
    None,          // No shadows
    Low,           // 512x512 shadow map
    Medium,        // 1024x1024 shadow map
    High,          // 2048x2048 shadow map
    Ultra,         // 4096x4096 shadow map
};
```

#### 3. Shadow Algorithms

```cpp
enum class ShadowAlgorithm {
    BasicShadowMap,        // Simple depth comparison
    PCF,                   // Percentage Closer Filtering
    PCSS,                  // Percentage Closer Soft Shadows
    VSM,                   // Variance Shadow Maps
    EVSM,                  // Exponential Variance Shadow Maps
};
```

### Light Class

Advanced light class with full property management:

```cpp
class Light {
public:
    // Basic properties
    void SetName(const std::string& name);
    void SetEnabled(bool enabled);
    LightType GetType() const;
    
    // Transform
    void SetPosition(const glm::vec3& position);
    void SetDirection(const glm::vec3& direction);
    void SetRotation(const glm::quat& rotation);
    
    // Color and intensity
    void SetColor(const glm::vec3& color);
    void SetIntensity(float intensity);
    
    // Light-specific properties
    void SetRange(float range);                    // Point/Spot
    void SetAttenuation(float linear, float quadratic);  // Point/Spot
    void SetSpotAngles(float inner, float outer); // Spot only
    void SetSpotFalloff(float falloff);           // Spot only
    
    // Shadow properties
    void SetCastShadow(bool cast_shadow);
    void SetShadowQuality(ShadowQuality quality);
    void SetShadowPlanes(float near, float far);
    void SetShadowBias(float bias, float normal_bias);
    void SetShadowFilter(float radius);
    
    // Advanced lighting features
    void SetTemperature(float kelvin);            // Color temperature
    void SetCookieTexture(std::shared_ptr<Texture2D> texture); // Light mask
    void SetVolumetricIntensity(float intensity); // God rays
    void SetCascadeCount(uint32_t count);         // CSM cascades
};
```

### LightManager

Centralized management of all lights in a scene:

```cpp
class LightManager {
public:
    // Light management
    std::shared_ptr<Light> AddLight(LightType type);
    void RemoveLight(std::shared_ptr<Light> light);
    std::shared_ptr<Light> GetLight(const std::string& name);
    std::vector<std::shared_ptr<Light>> GetLights();
    std::vector<std::shared_ptr<Light>> GetLightsByType(LightType type);
    std::vector<std::shared_ptr<Light>> GetActiveLights();
    std::vector<std::shared_ptr<Light>> GetShadowCastingLights();
    
    // Ambient/global lighting
    void SetAmbientLight(float intensity);
    void SetAmbientColor(const glm::vec3& color);
    
    // Environment mapping
    void SetEnvironmentMap(std::shared_ptr<EnvironmentMap> env_map);
    
    // Lighting calculations
    glm::vec3 CalculateLighting(const glm::vec3& position,
                               const glm::vec3& normal,
                               const glm::vec3& view_dir);
    
    std::vector<std::shared_ptr<Light>> GetNearestLights(const glm::vec3& position,
                                                         uint32_t max_lights);
};
```

## Usage Examples

### Creating Lights

```cpp
using namespace schizo::graphics;

RenderDevice* device = /* get device */;
LightingConfig config;
config.max_lights = 64;

auto light_mgr = LightManager::Create(device, config);

// Create directional light (sun)
auto sun = light_mgr->AddLight(LightType::Directional, "Sun");
sun->SetDirection(glm::normalize(glm::vec3(1.0f, -1.0f, 1.0f)));
sun->SetColor(glm::vec3(1.0f, 0.9f, 0.8f));
sun->SetIntensity(1.5f);
sun->SetCastShadow(true);
sun->SetShadowQuality(ShadowQuality::High);

light_mgr->SetMainLight(sun);

// Create point light
auto point = light_mgr->AddLight(LightType::Point, "LampLight");
point->SetPosition(glm::vec3(0.0f, 2.0f, 0.0f));
point->SetColor(glm::vec3(1.0f, 0.8f, 0.6f));
point->SetIntensity(1.0f);
point->SetRange(15.0f);

// Create spot light
auto spot = light_mgr->AddLight(LightType::Spot, "SpotLight");
spot->SetPosition(glm::vec3(5.0f, 5.0f, 5.0f));
spot->SetDirection(glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f)));
spot->SetSpotAngles(15.0f, 45.0f);
spot->SetColor(glm::vec3(0.5f, 0.5f, 1.0f));
spot->SetCastShadow(true);
```

### Shadow Configuration

```cpp
// Configure shadow quality
sun->SetShadowQuality(ShadowQuality::Ultra);

// Fine-tune shadow bias
sun->SetShadowBias(0.00002f, 0.01f);

// Set shadow filter radius for softer shadows
sun->SetShadowFilter(2.0f);

// Cascade shadow mapping for directional light
sun->SetCascadeCount(4);
```

### Color Temperature

```cpp
// Set light color from temperature
float kelvin = 6500.0f;  // Daylight
auto color = Light::TemperatureToColor(kelvin);
light->SetColor(color);

// Common temperature values:
// 1000K - Candlelight (warm orange)
// 2700K - Incandescent bulb (warm white)
// 5600K - Noon sunlight
// 6500K - Daylight (cool white)
// 9000K - Blue sky
```

### Advanced Features

```cpp
// Light cookies (animated textures for light shapes)
auto cookie = LoadTexture("light_cookie.png");
light->SetCookieTexture(cookie);

// Volumetric/god rays
light->SetVolumetricIntensity(0.3f);

// Attenuation control
point_light->SetAttenuation(0.045f, 0.0075f);
```

### Querying Lights

```cpp
// Get all active lights
auto active_lights = light_mgr->GetActiveLights();

// Get lights by type
auto point_lights = light_mgr->GetLightsByType(LightType::Point);

// Find nearest lights to a position
auto nearest = light_mgr->GetNearestLights(position, 4);

// Calculate lighting at a point
glm::vec3 position = glm::vec3(0.0f, 1.0f, 0.0f);
glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
glm::vec3 view_dir = glm::normalize(camera_pos - position);
auto lighting = light_mgr->CalculateLighting(position, normal, view_dir);
```

## Light Categories

### Directional Lights

Sun-like lights at infinite distance with parallel rays:

```cpp
auto sun = light_mgr->AddLight(LightType::Directional, "Sun");
sun->SetDirection(glm::vec3(-0.3f, -0.7f, -0.2f));
sun->SetColor(glm::vec3(1.0f, 0.95f, 0.8f));
sun->SetIntensity(1.5f);

// Directional lights use orthographic projection
// Great for outdoor scenes and large environments
```

### Point Lights

Omnidirectional lights from a single point:

```cpp
auto lamp = light_mgr->AddLight(LightType::Point, "Lamp");
lamp->SetPosition(glm::vec3(3.0f, 2.0f, 1.0f));
lamp->SetColor(glm::vec3(1.0f, 0.8f, 0.6f));
lamp->SetIntensity(1.0f);
lamp->SetRange(20.0f);
lamp->SetAttenuation(0.045f, 0.0075f);

// Point lights affect all directions from their position
// Shadow mapping requires cubemap rendering for full coverage
```

### Spot Lights

Cone-shaped lights with direction:

```cpp
auto spotlight = light_mgr->AddLight(LightType::Spot, "FlashLight");
spotlight->SetPosition(camera_pos);
spotlight->SetDirection(camera_dir);
spotlight->SetSpotAngles(20.0f, 40.0f);  // Inner 20°, outer 40°
spotlight->SetColor(glm::vec3(1.0f));
spotlight->SetIntensity(2.0f);
spotlight->SetRange(50.0f);
spotlight->SetSpotFalloff(2.0f);  // Controls smoothness of cone edge

// Spot lights are useful for flashlights, street lights, etc.
```

## Shadow Mapping

### Shadow Map Resolution

```cpp
shadow_config.resolution = 2048;  // Resolution in pixels
shadow_config.algorithm = ShadowAlgorithm::PCF;
```

### Common Configurations

**Low Quality** (fast, mobile):
```cpp
light->SetShadowQuality(ShadowQuality::Low);    // 512x512
light->SetShadowBias(0.01f, 0.05f);
light->SetShadowFilter(0.5f);
```

**High Quality** (console/desktop):
```cpp
light->SetShadowQuality(ShadowQuality::Ultra);  // 4096x4096
light->SetShadowBias(0.00002f, 0.01f);
light->SetShadowFilter(2.0f);
```

### Cascade Shadow Mapping

For directional lights covering large distances:

```cpp
auto sun = light_mgr->GetMainLight();
sun->SetCascadeCount(4);

// 4 cascade levels with different resolutions
// Near: highest detail
// Far: lower detail but covers larger area
```

## Lighting Utilities

### Light Calculations

```cpp
// Lambertian diffuse
auto diffuse = LightingUtils::Lambertian(light_color, normal, light_dir, intensity);

// Blinn-Phong specular
auto specular = LightingUtils::BlinnPhong(light_color, normal, light_dir, 
                                          view_dir, shininess, intensity);

// Point light attenuation
float attenuation = LightingUtils::PointLightAttenuation(distance, range);

// Spot light falloff
float falloff = LightingUtils::SpotLightFalloff(cos_theta, cos_inner, cos_outer);

// Inverse square law
float intensity_falloff = LightingUtils::InverseSquareLaw(distance);
```

### Culling

```cpp
// Check if light affects AABB
bool affects_aabb = LightingUtils::LightAffectsAABB(light, aabb_min, aabb_max);

// Check if light affects sphere
bool affects_sphere = LightingUtils::LightAffectsSphere(light, sphere_center, radius);
```

## Image-Based Lighting (IBL)

### Environment Maps

```cpp
// Load HDR equirectangular image
auto env = EnvironmentMap::LoadEquirectangular(device, "environment.hdr");

// Load cubemap directly
auto env = EnvironmentMap::LoadCubemap(device,
    "px.hdr", "nx.hdr", 
    "py.hdr", "ny.hdr", 
    "pz.hdr", "nz.hdr");

// Create simple gradient
auto env = EnvironmentMap::CreateGradient(device,
    glm::vec3(0.5f, 0.8f, 1.0f),    // Sky color
    glm::vec3(0.1f, 0.1f, 0.1f));   // Ground color

// Set in light manager
light_mgr->SetEnvironmentMap(env);
```

### Light Probes

```cpp
// Create light probe
auto probe = LightProbe::Create(device, glm::vec3(0.0f, 1.0f, 0.0f));

// Capture lighting at position
probe->Capture(device);

// Get spherical harmonics for efficient storage
auto sh_coeffs = probe->GetSHCoefficients();
```

### Skybox Rendering

```cpp
// Create skybox from environment
auto skybox = SkyBox::Create(device, env->GetEnvironmentCubemap());

// Render it
skybox->Render(device);

// Configure skybox
skybox->SetIntensity(1.2f);
skybox->SetRotation(45.0f);
```

## Performance Tips

1. **Light Culling**: Use spatial partitioning to limit lights per object
2. **Shadow Caching**: Keep shadow maps across frames when possible
3. **Distance Attenuation**: Use point light ranges to limit calculations
4. **Cascade Adjustment**: Adapt cascade levels based on view distance
5. **Batch Lights**: Group similar lights for shader optimization

## Best Practices

1. **Use Directional Light for Sun**: Most efficient for outdoor lighting
2. **Limit Shadow-Casting Lights**: Usually 1-3 per scene frame
3. **Set Proper Light Ranges**: Prevents unnecessary calculations
4. **Adjust Shadow Bias**: Prevents shadow acne and peter panning
5. **Use Temperature Colors**: Creates more realistic lighting
6. **Enable IBL**: Provides natural ambient lighting
7. **Optimize Spotlight Angles**: Balance realism with performance

## API Reference

See [LIGHTING-QUICK-REFERENCE.md](LIGHTING-QUICK-REFERENCE.md) for quick API reference.

## Future Extensions

1. **Area Lights**: Rectangular/linear light sources
2. **IES Light Profiles**: Photometric light distributions
3. **Light Link System**: Control which lights affect which objects
4. **Baked Lighting**: Pre-computed static light maps
5. **Light Probes Volume**: 3D grid of light probes
6. **LPVS**: Light Propagation Volumes for GI
7. **Voxel Cone Tracing**: Real-time global illumination
8. **Ray-Traced Shadows**: Photorealistic shadow quality
