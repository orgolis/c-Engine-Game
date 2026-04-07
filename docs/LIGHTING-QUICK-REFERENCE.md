# Lighting Quick Reference

## Creating Lights

```cpp
auto light_mgr = LightManager::Create(device, config);

// Directional (Sun)
auto sun = light_mgr->AddLight(LightType::Directional);

// Point (Omnidirectional)
auto point = light_mgr->AddLight(LightType::Point);

// Spot (Cone-shaped)
auto spot = light_mgr->AddLight(LightType::Spot);
```

## Essential Properties

### All Lights
```cpp
light->SetColor(glm::vec3(r, g, b));
light->SetIntensity(intensity);
light->SetEnabled(true/false);
light->SetName("LightName");

light->GetColor();
light->GetIntensity();
light->GetType();
```

### Directional Lights (Sun)
```cpp
sun->SetDirection(direction_vec3);

sun->SetShadowQuality(ShadowQuality::High);
sun->SetCascadeCount(4);
sun->SetMainLight(true);
```

### Point Lights
```cpp
light->SetPosition(position_vec3);
light->SetRange(radius);
light->SetAttenuation(linear, quadratic);

light->SetCastShadow(true);
light->SetShadowQuality(ShadowQuality::Medium);
```

### Spot Lights
```cpp
light->SetPosition(position_vec3);
light->SetDirection(direction_vec3);
light->SetSpotAngles(inner_angle, outer_angle);
light->SetSpotFalloff(falloff_power);
light->SetRange(radius);

light->SetCastShadow(true);
light->SetShadowQuality(ShadowQuality::High);
```

## Shadow Configuration

```cpp
light->SetCastShadow(true);
light->SetShadowQuality(ShadowQuality::High);    // 512/1024/2048/4096
light->SetShadowBias(bias, normal_bias);
light->SetShadowFilter(filter_radius);           // PCF softness
light->SetShadowPlanes(near_plane, far_plane);
```

## Light Management

```cpp
// Query lights
auto all_lights = light_mgr->GetLights();
auto point_lights = light_mgr->GetLightsByType(LightType::Point);
auto active_lights = light_mgr->GetActiveLights();
auto shadow_lights = light_mgr->GetShadowCastingLights();
auto light = light_mgr->GetLight("LightName");

// Nearest lights (for culling)
auto near = light_mgr->GetNearestLights(position, max_count);

// Remove light
light_mgr->RemoveLight(light);
```

## Advanced Features

```cpp
// Color temperature (Kelvin)
light->SetTemperature(6500.0f);  // Daylight
light->SetTemperature(2700.0f);  // Warm incandescent

// Light projection mask
light->SetCookieTexture(texture);

// Volumetric/god rays
light->SetVolumetricIntensity(0.3f);

// Cascade shadow mapping
sun->SetCascadeCount(4);
```

## Lighting Calculations

```cpp
// Calculate scene lighting
glm::vec3 color = light_mgr->CalculateLighting(position, normal, view_dir);

// Utility functions
auto diffuse = LightingUtils::Lambertian(color, normal, light_dir, intensity);
auto specular = LightingUtils::BlinnPhong(color, normal, light_dir, 
                                          view_dir, shininess, intensity);
auto atten = LightingUtils::PointLightAttenuation(distance, range);
auto falloff = LightingUtils::SpotLightFalloff(cos_angle, cos_inner, cos_outer);
```

## Ambient & Environment

```cpp
// Global ambient
light_mgr->SetAmbientLight(0.15f);
light_mgr->SetAmbientColor(glm::vec3(0.5f));

// Image-based lighting
auto env = EnvironmentMap::LoadEquirectangular(device, "sky.hdr");
light_mgr->SetEnvironmentMap(env);
```

## Configuration

```cpp
LightingConfig config;
config.max_lights = 64;
config.max_shadow_casting_lights = 8;
config.global_ambient = 0.15f;
config.ambient_color = glm::vec3(0.5f);
config.enable_ibl = true;
config.enable_dynamic_shadows = true;

auto light_mgr = LightManager::Create(device, config);
```

## Enums

### LightType
```cpp
Directional   // Sun/infinite distance
Point         // Omni light
Spot          // Cone light
```

### ShadowQuality
```cpp
None          // 0 (no shadow)
Low           // 512
Medium        // 1024
High          // 2048
Ultra         // 4096
```

### ShadowAlgorithm
```cpp
BasicShadowMap    // Simple depth test
PCF               // Percentage Closer Filtering
PCSS              // Soft shadows
VSM               // Variance Shadow Maps
EVSM              // Exponential Variance
```

## Common Color Temperatures

```cpp
1000K  - Candlelight
2700K  - Incandescent bulb
3000K  - Warm white LED
5600K  - Noon sunlight
6500K  - Cool daylight
9000K  - Blue sky
```

## Quick Setup Example

```cpp
// Initialize
auto light_mgr = LightManager::Create(device, LightingConfig());

// Create sun
auto sun = light_mgr->AddLight(LightType::Directional, "Sun");
sun->SetDirection(glm::normalize(glm::vec3(1, -1, 1)));
sun->SetColor(glm::vec3(1, 0.9, 0.8));
sun->SetIntensity(1.5f);
sun->SetCastShadow(true);
sun->SetShadowQuality(ShadowQuality::High);
light_mgr->SetMainLight(sun);

// Create some fill lights
auto point1 = light_mgr->AddLight(LightType::Point, "Fill1");
point1->SetPosition(glm::vec3(-5, 2, -5));
point1->SetColor(glm::vec3(0.8, 0.8, 1.0));
point1->SetIntensity(0.5f);
point1->SetRange(15.0f);

// Set environment lighting
light_mgr->SetAmbientLight(0.2f);
auto env = EnvironmentMap::LoadEquirectangular(device, "sky.hdr");
light_mgr->SetEnvironmentMap(env);
```

## Header Locations

- **Core Lighting**: `engine/renderer/light.h`
- **Light Management**: `engine/renderer/lighting.h`
- **Shadow Mapping**: `engine/renderer/shadow.h`
- **Image-Based Lighting**: `engine/renderer/ibl.h`
- **Implementations**: `engine/renderer/src/lighting.cpp`
