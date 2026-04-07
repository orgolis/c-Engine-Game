# Material and PBR System

## Overview

The Material and Physically-Based Rendering (PBR) system provides a comprehensive abstraction for managing materials and rendering properties in the GameWorldshaper engine. The system supports multiple material types, advanced PBR workflows, and a flexible material management library.

## Architecture

### Core Components

#### 1. Material Base Class
The foundational interface for all materials, providing:
- Shader program abstraction
- Uniform parameter management
- Texture slot management
- Material type identification

```cpp
class Material {
public:
    virtual MaterialType GetType() const = 0;
    virtual std::shared_ptr<ShaderProgram> GetShader() const = 0;
    
    // Uniform setters for all common types
    virtual void SetUniform1f/3f/4f(...) = 0;
    virtual void SetUniformVec2/3/4(...) = 0;
    virtual void SetUniformMat3/4(...) = 0;
    
    // Texture management
    virtual void SetTexture(const std::string& name, 
                           std::shared_ptr<Texture> texture, 
                           uint32_t unit) = 0;
    virtual void SetTextureSlot(TextureSlot slot, 
                               std::shared_ptr<Texture2D> texture) = 0;
    
    // Binding for rendering
    virtual void Bind(RenderDevice* device) const = 0;
    virtual void Unbind(RenderDevice* device) const = 0;
};
```

#### 2. PBRMaterial Class
Specialized material class for physically-based rendering with built-in PBR parameter management:

```cpp
class PBRMaterial : public Material {
public:
    // PBR parameter setters
    virtual void SetAlbedo(const glm::vec3& color) = 0;
    virtual void SetMetallic(float value) = 0;
    virtual void SetRoughness(float value) = 0;
    virtual void SetAOStrength(float value) = 0;
    virtual void SetNormalStrength(float value) = 0;
    virtual void SetEmissive(const glm::vec3& color, float intensity) = 0;
    
    // Texture management for PBR workflows
    virtual void EnableTextureSlot(TextureSlot slot, bool enabled) = 0;
    
    // Material properties calculation
    virtual glm::vec3 GetDiffuseColor() const = 0;
    virtual glm::vec3 GetF0() const = 0;  // Fresnel at zero degrees
};
```

### Material Types

```cpp
enum class MaterialType {
    Unlit,              // No lighting, emissive only
    Lit,                // Simple Blinn-Phong lighting
    StandardPBR,        // Metallic-Roughness PBR
    MetallicRoughness,  // Disney/Unreal style PBR
    SpecularGlossiness, // Legacy specular-gloss workflow
    Cloth,              // Cloth material with anisotropy
    Subsurface,         // Translucent materials with SSS
    Anisotropic,        // Brushed metal, hair-like materials
    Custom              // Custom shader-based material
};
```

### Texture Slots

Standard PBR material texture slots with specific semantic meanings:

```cpp
enum class TextureSlot {
    Albedo = 0,             // Base color (sRGB)
    Normal = 1,             // Surface normals (tangent space)
    Metallic = 2,           // Metallic intensity [0,1]
    Roughness = 3,          // Surface roughness [0,1]
    AmbientOcclusion = 4,   // Ambient occlusion [0,1]
    Emissive = 5,           // Self-emitted light
    Height = 6,             // Height/displacement map
    CavityMap = 7,          // Cavity/crevice occlusion
    MaxSlots = 8
};
```

## Usage Examples

### Creating Basic Materials

```cpp
using namespace schizo::graphics;

RenderDevice* device = /* get device */;

// Solid color material
auto solid = Material::CreateSolidColor(device, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

// Textured material
auto texture = Texture2D::Load(device, "assets/texture.png");
auto textured = Material::CreateTextured(device, texture);

// Unlit emissive material
auto emission = Material::CreateEmissive(device, glm::vec3(1.0f, 1.0f, 0.0f), 
                                        emissive_texture);
```

### Creating PBR Materials

```cpp
// Full PBR with all textures
auto pbr = Material::CreatePBR(device, 
                               albedo_tex, normal_tex, 
                               metallic_tex, roughness_tex, 
                               ao_tex);

// Simple PBR with just albedo and normal
auto simple_pbr = Material::CreateSimplePBR(device, 
                                            albedo_texture, 
                                            normal_texture);

// Parametric PBR with direct values
auto param_pbr = Material::CreateParamPBR(device,
                                          glm::vec3(0.8f, 0.8f, 0.8f),  // albedo
                                          0.2f,  // metallic
                                          0.5f); // roughness
```

### Using PBR Material Presets

```cpp
// Create materials from predefined presets
auto gold = PBRPresets::Gold(device);
auto copper = PBRPresets::Copper(device);
auto plastic = PBRPresets::Plastic(device, glm::vec3(0.2f, 0.3f, 0.9f));

// Preset parameters are based on real-world measurements
// Gold:    Albedo(1.0, 0.766, 0.336), Metallic(1.0), Roughness(0.2)
// Copper:  Albedo(0.955, 0.637, 0.538), Metallic(1.0), Roughness(0.3)
// Steel:   Albedo(0.77, 0.78, 0.78), Metallic(1.0), Roughness(0.25)
// Plastic: Albedo(custom), Metallic(0.0), Roughness(0.6)
```

### Using Material Builder Pattern

```cpp
PBRMaterialBuilder builder(device);

auto custom_material = builder
    .SetName("CustomMaterial")
    .SetAlbedo(glm::vec3(0.9f, 0.8f, 0.7f))
    .SetAlbedoTexture(albedo_texture)
    .SetMetallic(0.1f)
    .SetMetallicTexture(metallic_texture)
    .SetRoughness(0.4f)
    .SetRoughnessTexture(roughness_texture)
    .SetNormalMap(normal_texture, 1.2f)
    .SetAmbientOcclusion(ao_texture, 0.8f)
    .SetEmissive(glm::vec3(0.2f, 0.3f, 0.4f), 0.5f)
    .Build();
```

### Material Library Management

```cpp
// Create a material library
auto library = MaterialLibrary::Create(device);

// Create and register materials
library->RegisterMaterial("Gold", PBRPresets::Gold(device));
library->RegisterMaterial("Copper", PBRPresets::Copper(device));

// Create parametric PBR material
auto red_plastic = library->CreateParametricPBR(
    "RedPlastic",
    glm::vec3(0.9f, 0.1f, 0.1f),  // albedo
    0.0f,   // metallic
    0.6f    // roughness
);

// Retrieve materials
auto material = library->GetMaterial("Gold");
auto pbr_material = library->GetPBRMaterial("RedPlastic");

// List all materials
auto names = library->GetMaterialNames();

// Clean up
library->RemoveMaterial("Gold");
library->Clear();
```

## PBR Concepts

### Metallic-Roughness Workflow

The most common modern PBR workflow used by Unreal Engine and other modern engines.

**Albedo (Base Color):**
- For dielectrics: The color of the material
- For metals: The color of the reflectance (F0)
- RGB in sRGB color space

**Metallic:**
- Binary behavior in theory, blended in practice [0, 1]
- 0.0: Dielectric/non-metal
- 1.0: Full metal
- 0.0-1.0: Blend between dielectric and metallic

**Roughness:**
- Inverse of smoothness [0, 1]
- 0.0: Mirror-like, perfectly smooth
- 1.0: Diffuse surface
- Controls the spread of the highlight

**Ambient Occlusion:**
- Darkening in crevices and cavities [0, 1]
- Multiplied with diffuse and sometimes specular
- Often baked from geometry

## Advanced PBR Features

### F0 (Fresnel at Zero Degrees)

Calculated based on material properties:

```cpp
// For dielectrics (non-metals)
F0 = vec3(0.04);

// For metals
F0 = albedo_color;

// Mixed materials
F0 = mix(vec3(0.04), albedo_color, metallic);
```

### Diffuse Color

```cpp
// In PBR, the diffuse contribution depends on metallicity
diffuse_color = albedo * (1.0 - metallic);
```

### Roughness Clamping

To avoid numerical issues in shader calculations:

```cpp
float clamped_roughness = clamp(roughness, 0.02, 1.0);
```

## Material Utilities

### Color Space Conversions

```cpp
auto linear = PBRMaterialUtils::SRGBToLinear(srgb_color);
auto srgb = PBRMaterialUtils::LinearToSRGB(linear_color);
```

### Real-World Parameter Lookup

```cpp
glm::vec3 albedo;
float metallic, roughness;

PBRMaterialUtils::GetMaterialParams("Gold", albedo, metallic, roughness);
// albedo = (1.0, 0.766, 0.336)
// metallic = 1.0
// roughness = 0.2
```

### Fresnel Calculation

```cpp
// Calculate F0 from refractive index
float ior = 1.5f;  // Glass refractive index
auto f0 = PBRMaterialUtils::CalculateF0(ior);
```

### Roughness Clamping

```cpp
float safe_roughness = PBRMaterialUtils::ClampRoughness(roughness);
```

## Material Presets Reference

All presets are based on real-world measurements from various sources (Unreal Engine, Marmoset, etc).

### Metals
- **Gold**: Highly reflective, warm color, low roughness
- **Copper**: Reddish reflectance, moderate roughness
- **Steel**: Neutral gray reflectance, low-moderate roughness
- **Aluminum**: Very bright reflectance, moderate roughness
- **Brass**: Yellowish metal, moderate roughness

### Dielectrics
- **Plastic**: Non-reflective colored material, high roughness
- **Fabric**: Textured surface, very high roughness
- **Ceramic**: Smooth, slightly glossy surface
- **Rubber**: Very dull, extremely high roughness
- **Glass**: Transparent-ish, very low roughness
- **Wood**: Organic material with variation

### Specialized
- **Skin**: Subsurface scattering appearance, moderate roughness
- **Cloth**: Anisotropic surface with directional properties
- **Mirror**: Perfectly polished metal, zero roughness
- **DiffuseLambert**: Pure diffuse, no specular highlight

## Rendering Integration

### Binding Materials for Drawing

```cpp
material->Bind(device);
mesh->Draw(device);
material->Unbind(device);
```

### Material State Management

Materials cache uniform values and texture bindings, so modifications persist:

```cpp
auto material = PBRMaterial::Create(device);

// Set initial values
material->SetAlbedo(glm::vec3(1.0f, 0.0f, 0.0f));
material->SetMetallic(0.5f);

// Bind uses cached values
material->Bind(device);
mesh->Draw(device);

// Values persist - no need to set again for next draw
material->Bind(device);
mesh->Draw(device);

// Modify values for next draw
material->SetAlbedo(glm::vec3(0.0f, 1.0f, 0.0f));
material->Bind(device);
mesh->Draw(device);
```

## Performance Considerations

### Texture Memory
- Use mipmap chains for distant materials
- Consider texture compression (BC3 for normals, BC4/BC5 for single-channel)
- Atlasing textures reduces state changes

### Shader Variants
- Create different shader variants for different material types
- Pre-compile common material combinations
- Use conditional compilation for optional features

### Material Caching
- Store frequently used materials in MaterialLibrary
- Avoid reallocating materials every frame
- Use material references for multiple objects with same material

## Best Practices

1. **Use Metallic-Roughness Workflow**: Most flexible for games
2. **Normalize Texture Values**: Ensure proper sRGB/linear conversions
3. **Clamp Roughness**: Avoid division by zero in shader
4. **Validate F0**: Use appropriate F0 for material type
5. **Test in Multiple Lighting**: Different lights reveal PBR issues
6. **Use Material Presets**: Real-world values give consistent results
7. **Profile Texture Lookups**: Many textures = many samples
8. **Plan Shader Variants**: Not all materials need all features

## API Reference

See [MATERIAL-QUICK-REFERENCE.md](MATERIAL-QUICK-REFERENCE.md) for quick API reference.

## Future Extensions

1. **Cloth Simulation**: Specialized cloth material with anisotropy
2. **Subsurface Scattering**: Translucent materials support
3. **Clear Coat**: Multi-layer material support
4. **Anisotropic Reflections**: Brushed metal and hair materials
5. **Texture Streaming**: Dynamic texture management
6. **Material Variants**: Runtime parameter overrides
7. **Procedural Materials**: Shader-generated textures
8. **Hair BSDF**: Specialized hair rendering
