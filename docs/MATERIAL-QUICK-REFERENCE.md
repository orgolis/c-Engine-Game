# Material and PBR Quick Reference

## Quick API Reference

### Creating Materials

```cpp
// Basic solid color
auto mat = Material::CreateSolidColor(device, glm::vec4(r,g,b,a));

// Textured
auto mat = Material::CreateTextured(device, texture);

// Simple PBR (albedo + normal only)
auto mat = Material::CreateSimplePBR(device, albedo_tex, normal_tex);

// Full PBR (all 5 textures)
auto mat = Material::CreatePBR(device, albedo, normal, metallic, roughness, ao);

// Parametric PBR (no textures)
auto mat = Material::CreateParamPBR(device, albedo_color, metallic, roughness);

// Emissive/unlit
auto mat = Material::CreateEmissive(device, emissive_color, intensity, texture);
```

### PBR Materials

```cpp
auto pbr = PBRMaterial::Create(device, "MyMaterial");

// Set parameters
pbr->SetAlbedo(glm::vec3(r,g,b));
pbr->SetMetallic(0.5f);        // [0,1]
pbr->SetRoughness(0.4f);       // [0,1]
pbr->SetAOStrength(0.8f);      // [0,1]
pbr->SetNormalStrength(1.2f);  // [0,2]
pbr->SetEmissive(glm::vec3(1,1,1), 0.5f);

// Get values
auto albedo = pbr->GetAlbedo();
float metallic = pbr->GetMetallic();
auto f0 = pbr->GetF0();

// Texture assignment
pbr->SetTextureSlot(TextureSlot::Albedo, albedo_tex);
pbr->SetTextureSlot(TextureSlot::Normal, normal_tex);
pbr->SetTextureSlot(TextureSlot::Metallic, metallic_tex);
pbr->SetTextureSlot(TextureSlot::Roughness, roughness_tex);
pbr->SetTextureSlot(TextureSlot::AmbientOcclusion, ao_tex);
```

### Material Builder

```cpp
auto mat = PBRMaterialBuilder(device)
    .SetName("CustomMat")
    .SetAlbedo(glm::vec3(0.8f))
    .SetMetallic(0.2f)
    .SetRoughness(0.5f)
    .SetNormalMap(normal_tex, 1.0f)
    .SetAmbientOcclusion(ao_tex)
    .SetEmissive(glm::vec3(1,0,0), 0.3f)
    .Build();
```

### Presets

```cpp
auto gold = PBRPresets::Gold(device);
auto copper = PBRPresets::Copper(device);
auto steel = PBRPresets::Steel(device);
auto aluminum = PBRPresets::Aluminum(device);
auto brass = PBRPresets::Brass(device);

auto plastic = PBRPresets::Plastic(device, glm::vec3(0.2f, 0.8f, 0.9f));
auto fabric = PBRPresets::Fabric(device, glm::vec3(0.8f, 0.1f, 0.1f));
auto ceramic = PBRPresets::Ceramic(device, glm::vec3(0.9f, 0.9f, 0.9f));
auto rubber = PBRPresets::Rubber(device, glm::vec3(0.1f, 0.1f, 0.1f));

auto glass = PBRPresets::Glass(device);
auto skin = PBRPresets::Skin(device, glm::vec3(0.76f, 0.65f, 0.58f));
auto wood = PBRPresets::Wood(device, glm::vec3(0.5f, 0.3f, 0.15f));

auto mirror = PBRPresets::Mirror(device);
auto diffuse = PBRPresets::DiffuseLambert(device, glm::vec3(0.8f));
```

### Material Library

```cpp
auto library = MaterialLibrary::Create(device);

// Register
library->RegisterMaterial("Gold", PBRPresets::Gold(device));

// Retrieve
auto mat = library->GetMaterial("Gold");
auto pbr = library->GetPBRMaterial("Gold");

// Create and store
library->CreateParametricPBR("RedPlastic", 
                             glm::vec3(0.9,0.1,0.1), 0.0, 0.6);

// Query
if (library->HasMaterial("Gold")) { }
auto names = library->GetMaterialNames();

// Remove
library->RemoveMaterial("Gold");
library->Clear();
```

### Binding for Rendering

```cpp
material->Bind(device);
mesh->Draw(device);
material->Unbind(device);
```

### Uniforms

```cpp
material->SetUniform1f("name", value);
material->SetUniform2f("name", x, y);
material->SetUniform3f("name", x, y, z);
material->SetUniform4f("name", x, y, z, w);
material->SetUniformVec2("name", glm::vec2(...));
material->SetUniformVec3("name", glm::vec3(...));
material->SetUniformVec4("name", glm::vec4(...));
material->SetUniformMat3("name", glm::mat3(...));
material->SetUniformMat4("name", glm::mat4(...));
```

### Texture Slots

```cpp
TextureSlot::Albedo               // Base color
TextureSlot::Normal               // Tangent space normals
TextureSlot::Metallic             // Metallic intensity
TextureSlot::Roughness            // Surface roughness
TextureSlot::AmbientOcclusion     // Ambient occlusion
TextureSlot::Emissive             // Self-emitted light
TextureSlot::Height               // Heightfield/displacement
TextureSlot::CavityMap            // Cavity occlusion
```

## Preset Parameters

### Metals (Metallic = 1.0)

| Material  | Albedo (RGB)        | Roughness |
|-----------|-------------------|-----------|
| Gold      | (1.0, 0.77, 0.34) | 0.2       |
| Copper    | (0.96, 0.64, 0.54)| 0.3       |
| Steel     | (0.77, 0.78, 0.78)| 0.25      |
| Aluminum  | (0.91, 0.92, 0.92)| 0.35      |
| Brass     | (0.91, 0.85, 0.60)| 0.28      |

### Dielectrics (Metallic = 0.0)

| Material  | Albedo (RGB)   | Roughness | Notes               |
|-----------|---------------|-----------|-------------------|
| Plastic   | Custom        | 0.6       | Color-dependent    |
| Fabric    | Custom        | 0.8       | High roughness     |
| Ceramic   | Custom        | 0.2       | Smooth, glossy     |
| Rubber    | Custom        | 0.9       | Very dull          |
| Glass     | (1, 1, 1)     | 0.05      | Near-transparent   |
| Wood      | Custom        | 0.5       | Organic variation  |
| Skin      | Custom        | 0.35      | SSS appearance     |

## Common Patterns

### Full PBR Material from Textures

```cpp
PBRTextureSet textures;
textures.albedo = load_texture("_color.png");
textures.normal = load_texture("_normal.png");
textures.metallic = load_texture("_metallic.png");
textures.roughness = load_texture("_roughness.png");
textures.ao = load_texture("_ao.png");

auto material = Material::CreatePBR(device,
                                   textures.albedo,
                                   textures.normal,
                                   textures.metallic,
                                   textures.roughness,
                                   textures.ao);
```

### Simple Colored Material

```cpp
auto material = Material::CreateParamPBR(device,
                                        glm::vec3(0.8f, 0.2f, 0.1f),  // red
                                        0.0f,    // non-metallic
                                        0.5f);   // medium roughness
```

### Metallic Surface

```cpp
auto material = Material::CreateParamPBR(device,
                                        glm::vec3(0.9f),    // bright (metal color)
                                        1.0f,               // fully metallic
                                        0.3f);              // somewhat rough
```

### Glowing Material

```cpp
auto material = PBRMaterial::Create(device, "Glowing");
material->SetAlbedo(glm::vec3(0.0f));        // no reflection
material->SetEmissive(glm::vec3(1,1,0), 1.5f);  // yellow glow
material->SetMetallic(0.0f);
material->SetRoughness(1.0f);
```

## Texture Slot Reference

```cpp
setTextureSlot(TextureSlot::Albedo, tex)
    -> "uAlbedo" uniform, sRGB format, RGB8 or RGBA8

setTextureSlot(TextureSlot::Normal, tex)
    -> "uNormal" uniform, tangent space, RGB8 or RGBA8

setTextureSlot(TextureSlot::Metallic, tex)
    -> "uMetallic" uniform, R channel only, R8 or R32F

setTextureSlot(TextureSlot::Roughness, tex)
    -> "uRoughness" uniform, R channel only, R8 or R32F

setTextureSlot(TextureSlot::AmbientOcclusion, tex)
    -> "uAO" uniform, R channel only, R8 or R32F

setTextureSlot(TextureSlot::Emissive, tex)
    -> "uEmissive" uniform, sRGB format, RGB8 or RGBA8

setTextureSlot(TextureSlot::Height, tex)
    -> "uHeight" uniform, heightfield, R8 or R32F

setTextureSlot(TextureSlot::CavityMap, tex)
    -> "uCavity" uniform, R channel only, R8 or R32F
```

## Material Type Enum

```cpp
MaterialType::Unlit              // No lighting/shadows
MaterialType::Lit                // Blinn-Phong
MaterialType::StandardPBR        // Metallic-Roughness PBR
MaterialType::MetallicRoughness  // Disney/Unreal PBR
MaterialType::SpecularGlossiness // Legacy spec-gloss
MaterialType::Cloth              // Anisotropic cloth
MaterialType::Subsurface         // Translucent SSS
MaterialType::Anisotropic        // Brushed surfaces
MaterialType::Custom             // Custom shader
```

## Utilities

```cpp
// Color space conversion
auto linear = PBRMaterialUtils::SRGBToLinear(srgb);
auto srgb = PBRMaterialUtils::LinearToSRGB(linear);

// Fresnel from IOR
auto f0 = PBRMaterialUtils::CalculateF0(1.5f);  // Glass

// Roughness clamping
auto safe = PBRMaterialUtils::ClampRoughness(roughness);

// Specular to roughness conversion
auto rough = PBRMaterialUtils::SpecularToRoughness(specular);

// Material queries
bool is_metal = PBRMaterialUtils::IsMetallic(0.8f);

// Real-world parameters
glm::vec3 albedo;
float metallic, roughness;
PBRMaterialUtils::GetMaterialParams("Gold", albedo, metallic, roughness);
```

## Tips & Tricks

1. **Metallic-Roughness vs Specular-Gloss**: Use metallic-roughness for modern rendering
2. **Normal Map Strength**: 0.5-1.5 is typical range, 1.0 is neutral
3. **AO Multiplier**: Usually 0.5-1.0, lower for more occlusion effect
4. **Roughness Clamping**: Clamp to [0.02, 1.0] in shaders
5. **F0 for Dielectrics**: Use 0.04 (4% reflectance)
6. **F0 for Metals**: Use the albedo color
7. **Emissive Intensity**: 0.0-2.0+ range depending on lighting
8. **Texture Formats**: 
   - Albedo: sRGB (automatic conversion)
   - Normal: Linear (no conversion)
   - Metallic/Roughness/AO: Linear single channel
   - Height: Linear single channel

## Common Issues

**Problem**: Material looks too dark
**Solution**: Check if albedo is in sRGB space, increase lighting

**Problem**: Normal map looks wrong
**Solution**: Ensure normal texture is NOT in sRGB (set to Linear)

**Problem**: Specularity disappears
**Solution**: Check metallic/roughness values, F0 calculation

**Problem**: Material looks plastic-y
**Solution**: Increase roughness slightly, ensure AO is baked

**Problem**: Reflections too strong/weak
**Solution**: Adjust metallic value, check ambient lighting
