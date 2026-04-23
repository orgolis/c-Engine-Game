# GameWorldshaper Engine — Code Examples
> Concrete, annotated C++ (and shader) implementations for every researched system.
> These are reference implementations — not final production code — but they are
> architecturally correct and use the same patterns as the described engine design.

---

# TABLE OF CONTENTS

1. [Clustered Deferred Rendering](#1-clustered-deferred-rendering)
2. [Hardware Ray Tracing (Vulkan)](#2-hardware-ray-tracing-vulkan)
3. [Two-Pass HZB Occlusion Culling](#3-two-pass-hzb-occlusion-culling)
4. [LOD System with meshoptimizer](#4-lod-system-with-meshoptimizer)
5. [ECS with EnTT](#5-ecs-with-entt)
6. [Rollback Netcode](#6-rollback-netcode)
7. [Behavior Tree System](#7-behavior-tree-system)
8. [Animation — Blend Tree & FABRIK IK](#8-animation--blend-tree--fabrik-ik)
9. [Open World Chunk Streaming](#9-open-world-chunk-streaming)
10. [Visual Script VM](#10-visual-script-vm)
11. [Stat & Modifier System](#11-stat--modifier-system)
12. [Combat — Frame Data & Hitboxes](#12-combat--frame-data--hitboxes)
13. [Ability System](#13-ability-system)
14. [C# Scripting Host](#14-c-scripting-host)

---

# 1. Clustered Deferred Rendering

## 1a. Cluster Grid Builder (C++)

```cpp
// engine/renderer/passes/cluster_builder.h
#pragma once
#include "engine/core/math/math.h"
#include <vector>
#include <cstdint>

namespace gws::renderer {

// -------------------------------------------------------------------
// Cluster grid divides the view frustum into 3D cells.
// X and Y follow screen tiles; Z slices the depth range exponentially.
// Each cluster stores an offset into a flat light index list.
// -------------------------------------------------------------------

struct ClusterAABB {
    gws::math::Vec3 min_point;
    float _pad0;
    gws::math::Vec3 max_point;
    float _pad1;
};

struct LightGrid {
    uint32_t offset;   // index into flat light_indices[] array
    uint32_t count;    // how many lights are in this cluster
};

struct ClusterConfig {
    uint32_t grid_x   = 16;   // screen-space tile columns
    uint32_t grid_y   =  9;   // screen-space tile rows
    uint32_t grid_z   = 24;   // depth slices (exponential)
    float    near_z   = 0.1f;
    float    far_z    = 2000.0f;
    uint32_t max_lights_per_cluster = 256;
};

class ClusterBuilder {
public:
    explicit ClusterBuilder(const ClusterConfig& cfg) : cfg_(cfg) {
        const uint32_t total = cfg_.grid_x * cfg_.grid_y * cfg_.grid_z;
        cluster_aabbs_.resize(total);
        light_grid_.resize(total);
    }

    // Called once per frame when camera changes.
    // Recomputes world-space AABB for every cluster.
    void rebuild_aabbs(const gws::math::Mat4& inv_proj,
                       float screen_width, float screen_height)
    {
        const float log_ratio = std::log(cfg_.far_z / cfg_.near_z);

        for (uint32_t z = 0; z < cfg_.grid_z; ++z) {
            // Exponential depth slice — more clusters near camera
            float z_near = cfg_.near_z * std::pow(cfg_.far_z / cfg_.near_z,
                                (float)z       / cfg_.grid_z);
            float z_far  = cfg_.near_z * std::pow(cfg_.far_z / cfg_.near_z,
                                (float)(z + 1) / cfg_.grid_z);

            for (uint32_t y = 0; y < cfg_.grid_y; ++y) {
                for (uint32_t x = 0; x < cfg_.grid_x; ++x) {
                    // NDC corners of this screen tile
                    float x0 = ((float) x      / cfg_.grid_x) * 2.0f - 1.0f;
                    float x1 = ((float)(x + 1) / cfg_.grid_x) * 2.0f - 1.0f;
                    float y0 = ((float) y      / cfg_.grid_y) * 2.0f - 1.0f;
                    float y1 = ((float)(y + 1) / cfg_.grid_y) * 2.0f - 1.0f;

                    // Unproject NDC corners to view space at z_near and z_far
                    auto unproject = [&](float nx, float ny, float depth) {
                        gws::math::Vec4 ndc(nx, ny, -1.0f, 1.0f);
                        gws::math::Vec4 view = inv_proj * ndc;
                        view = view * (1.0f / view.w);
                        // Scale to actual depth
                        return gws::math::Vec3(view.x, view.y, view.z) *
                               (depth / std::abs(view.z));
                    };

                    // Build AABB from 8 frustum corners
                    gws::math::Vec3 corners[8] = {
                        unproject(x0, y0, z_near), unproject(x1, y0, z_near),
                        unproject(x0, y1, z_near), unproject(x1, y1, z_near),
                        unproject(x0, y0, z_far),  unproject(x1, y0, z_far),
                        unproject(x0, y1, z_far),  unproject(x1, y1, z_far),
                    };

                    gws::math::Vec3 aabb_min = corners[0];
                    gws::math::Vec3 aabb_max = corners[0];
                    for (int i = 1; i < 8; ++i) {
                        aabb_min.x = std::min(aabb_min.x, corners[i].x);
                        aabb_min.y = std::min(aabb_min.y, corners[i].y);
                        aabb_min.z = std::min(aabb_min.z, corners[i].z);
                        aabb_max.x = std::max(aabb_max.x, corners[i].x);
                        aabb_max.y = std::max(aabb_max.y, corners[i].y);
                        aabb_max.z = std::max(aabb_max.z, corners[i].z);
                    }

                    uint32_t idx = x + y * cfg_.grid_x +
                                   z * cfg_.grid_x * cfg_.grid_y;
                    cluster_aabbs_[idx].min_point = aabb_min;
                    cluster_aabbs_[idx].max_point = aabb_max;
                }
            }
        }
    }

    // Convert a view-space position + depth to a flat cluster index
    uint32_t cluster_index_from_view(const gws::math::Vec3& view_pos,
                                     float screen_width,
                                     float screen_height) const
    {
        // X and Y: screen tile
        // (Assumes view_pos is already in NDC / screen fraction)
        uint32_t x = std::min((uint32_t)(view_pos.x * cfg_.grid_x), cfg_.grid_x - 1);
        uint32_t y = std::min((uint32_t)(view_pos.y * cfg_.grid_y), cfg_.grid_y - 1);

        // Z: exponential depth slice
        float depth = std::abs(view_pos.z);
        uint32_t z = (uint32_t)(std::log(depth / cfg_.near_z) /
                                std::log(cfg_.far_z / cfg_.near_z) * cfg_.grid_z);
        z = std::min(z, cfg_.grid_z - 1);

        return x + y * cfg_.grid_x + z * cfg_.grid_x * cfg_.grid_y;
    }

    const std::vector<ClusterAABB>& aabbs()      const { return cluster_aabbs_; }
    const std::vector<LightGrid>&   light_grid() const { return light_grid_; }
    std::vector<LightGrid>&         light_grid()       { return light_grid_; }

private:
    ClusterConfig               cfg_;
    std::vector<ClusterAABB>    cluster_aabbs_;
    std::vector<LightGrid>      light_grid_;
};

} // namespace gws::renderer
```

## 1b. Cluster Light Assignment (GLSL Compute Shader)

```glsl
// engine/renderer/shaders/cluster_light_assign.comp
#version 450
layout(local_size_x = 16, local_size_y = 9, local_size_z = 1) in;

// ---- Structs -------------------------------------------------------
struct ClusterAABB {
    vec3  min_point; float _p0;
    vec3  max_point; float _p1;
};

struct PointLight {
    vec3  position_view;  // in view space
    float radius;
    vec3  color;
    float intensity;
};

struct LightGrid {
    uint offset;
    uint count;
};

// ---- Buffers -------------------------------------------------------
layout(std430, binding = 0) readonly  buffer ClusterAABBs  { ClusterAABB aabbs[]; };
layout(std430, binding = 1) readonly  buffer Lights        { PointLight  lights[]; };
layout(std430, binding = 2) writeonly buffer LightGridBuf  { LightGrid   grid[]; };
layout(std430, binding = 3) writeonly buffer LightIndices  { uint        indices[]; };
layout(std430, binding = 4)           buffer GlobalCounter { uint        global_index_count; };

layout(push_constant) uniform PushConstants {
    uint light_count;
    uint cluster_count;
    uint max_lights_per_cluster;
};

// ---- Sphere-AABB intersection test ---------------------------------
bool sphere_intersects_aabb(vec3 center, float radius,
                             vec3 aabb_min, vec3 aabb_max)
{
    // Find the closest point on the AABB to the sphere center
    vec3 closest = clamp(center, aabb_min, aabb_max);
    vec3 diff    = center - closest;
    return dot(diff, diff) <= (radius * radius);
}

// ---- Main ----------------------------------------------------------
void main() {
    uint cluster_idx = gl_GlobalInvocationID.x
                     + gl_GlobalInvocationID.y * 16
                     + gl_GlobalInvocationID.z * 16 * 9;

    if (cluster_idx >= cluster_count) return;

    ClusterAABB cluster = aabbs[cluster_idx];

    // Collect all lights that overlap this cluster
    uint local_light_indices[256];
    uint local_count = 0;

    for (uint i = 0; i < light_count && local_count < max_lights_per_cluster; ++i) {
        PointLight light = lights[i];
        if (sphere_intersects_aabb(light.position_view, light.radius,
                                   cluster.min_point, cluster.max_point))
        {
            local_light_indices[local_count++] = i;
        }
    }

    // Atomically reserve space in the global index array
    uint offset = atomicAdd(global_index_count, local_count);

    // Write light grid entry
    grid[cluster_idx].offset = offset;
    grid[cluster_idx].count  = local_count;

    // Write light indices
    for (uint i = 0; i < local_count; ++i) {
        indices[offset + i] = local_light_indices[i];
    }
}
```

## 1c. Clustered Lighting Pass (GLSL Fragment Shader)

```glsl
// engine/renderer/shaders/deferred_lighting.frag
#version 450

layout(location = 0) in  vec2 uv;
layout(location = 0) out vec4 frag_color;

// G-Buffer inputs
layout(set = 0, binding = 0) uniform sampler2D gbuf_albedo_ao;    // RGB=albedo, A=AO
layout(set = 0, binding = 1) uniform sampler2D gbuf_normal;       // RG=oct-encoded normal
layout(set = 0, binding = 2) uniform sampler2D gbuf_roughness;    // R=rough, G=metal, B=emissive
layout(set = 0, binding = 3) uniform sampler2D gbuf_depth;

// Cluster data
layout(std430, set = 1, binding = 0) readonly buffer LightGridBuf  { LightGrid   light_grid[]; };
layout(std430, set = 1, binding = 1) readonly buffer LightIndices  { uint        light_indices[]; };
layout(std430, set = 1, binding = 2) readonly buffer Lights        { PointLight  lights[]; };

layout(push_constant) uniform CameraData {
    mat4  inv_proj;
    mat4  inv_view;
    vec2  screen_size;
    float near_z;
    float far_z;
    uvec3 cluster_grid_size;   // e.g. (16, 9, 24)
};

// ---- Oct-decode normal (2 channels → vec3) -----------------------
vec3 oct_decode(vec2 f) {
    f = f * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
    return normalize(n);
}

// ---- Reconstruct world position from depth -----------------------
vec3 reconstruct_world_pos(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view_pos = inv_proj * ndc;
    view_pos /= view_pos.w;
    return (inv_view * view_pos).xyz;
}

// ---- PBR: GGX BRDF -----------------------------------------------
float distribution_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 pbr_point_light(vec3 P, vec3 N, vec3 V,
                     vec3 albedo, float roughness, float metallic,
                     vec3 light_pos, vec3 light_color, float light_intensity) {
    vec3 L = normalize(light_pos - P);
    vec3 H = normalize(V + L);
    float dist = length(light_pos - P);

    float attenuation = 1.0 / (dist * dist);
    vec3  radiance    = light_color * light_intensity * attenuation;

    vec3  F0    = mix(vec3(0.04), albedo, metallic);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float D   = distribution_ggx(NdotH, roughness);
    float G   = geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
    vec3  F   = fresnel_schlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / 3.14159265 + specular) * radiance * NdotL;
}

// ---- Cluster index from view position ----------------------------
uint cluster_idx_from_view(vec3 view_pos) {
    uint x = uint(gl_FragCoord.x / screen_size.x * cluster_grid_size.x);
    uint y = uint(gl_FragCoord.y / screen_size.y * cluster_grid_size.y);
    uint z = uint(log(abs(view_pos.z) / near_z) /
                  log(far_z / near_z) * cluster_grid_size.z);

    x = min(x, cluster_grid_size.x - 1);
    y = min(y, cluster_grid_size.y - 1);
    z = min(z, cluster_grid_size.z - 1);

    return x + y * cluster_grid_size.x +
               z * cluster_grid_size.x * cluster_grid_size.y;
}

void main() {
    float depth   = texture(gbuf_depth, uv).r;
    if (depth >= 1.0) { frag_color = vec4(0.0); return; } // sky

    vec4  albedo_ao   = texture(gbuf_albedo_ao,  uv);
    vec2  packed_norm = texture(gbuf_normal,      uv).rg;
    vec4  pbr_data    = texture(gbuf_roughness,   uv);

    vec3  albedo    = albedo_ao.rgb;
    float ao        = albedo_ao.a;
    vec3  N         = oct_decode(packed_norm);
    float roughness = pbr_data.r;
    float metallic  = pbr_data.g;

    vec3  world_pos = reconstruct_world_pos(uv, depth);
    vec3  V         = normalize(-world_pos); // camera at origin in view space

    // Determine which cluster this fragment belongs to
    vec4 view_pos4 = (inv_proj * vec4(uv * 2.0 - 1.0, depth, 1.0));
    view_pos4 /= view_pos4.w;
    uint cidx = cluster_idx_from_view(view_pos4.xyz);

    // Accumulate all lights in this cluster
    vec3 Lo = vec3(0.0);
    uint light_offset = light_grid[cidx].offset;
    uint light_count  = light_grid[cidx].count;

    for (uint i = 0; i < light_count; ++i) {
        uint   lid   = light_indices[light_offset + i];
        PointLight L = lights[lid];
        Lo += pbr_point_light(world_pos, N, V, albedo, roughness, metallic,
                              L.position_view, L.color, L.intensity);
    }

    // Ambient (IBL placeholder — proper IBL added in Phase 3g)
    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color   = ambient + Lo;

    // ACES tonemapping
    color = color * (color + 0.0245786f) / (color * (0.983729f * color + 0.432951f) + 0.238081f);

    frag_color = vec4(color, 1.0);
}
```

---

# 2. Hardware Ray Tracing (Vulkan)

## 2a. Acceleration Structure Manager (C++)

```cpp
// engine/renderer/rt/acceleration_structure_manager.h
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include "engine/core/math/math.h"

namespace gws::renderer::rt {

// -------------------------------------------------------------------
// Manages BLAS (one per mesh) and TLAS (rebuilt each frame).
// Static meshes use ALLOW_COMPACTION for 50% memory saving.
// Dynamic (skinned) meshes use ALLOW_UPDATE for fast refits.
// -------------------------------------------------------------------

enum class BLASType { Static, Dynamic };

struct BLASEntry {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer                   buffer = VK_NULL_HANDLE;
    VkDeviceMemory             memory = VK_NULL_HANDLE;
    VkDeviceAddress            device_address = 0;
    BLASType                   type = BLASType::Static;
};

struct TLASInstance {
    VkDeviceAddress blas_address;
    uint32_t        custom_index;   // InstanceID in shaders
    uint32_t        mask;
    gws::math::Mat4 transform;      // 4×4 world transform
};

class AccelerationStructureManager {
public:
    // Create BLAS from vertex + index data.
    // For static meshes: builds with PREFER_FAST_TRACE + ALLOW_COMPACTION.
    // Compaction runs asynchronously after build.
    BLASEntry create_blas(VkDevice device,
                          VkCommandBuffer cmd,
                          VkPhysicalDeviceMemoryProperties mem_props,
                          VkBuffer vertex_buffer, VkDeviceSize vertex_stride,
                          uint32_t vertex_count,
                          VkBuffer index_buffer,  uint32_t index_count,
                          BLASType type)
    {
        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;

        auto& tri = geometry.geometry.triangles;
        tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tri.vertexData.deviceAddress = get_buffer_device_address(device, vertex_buffer);
        tri.vertexStride = vertex_stride;
        tri.maxVertex    = vertex_count - 1;
        tri.indexType    = VK_INDEX_TYPE_UINT32;
        tri.indexData.deviceAddress = get_buffer_device_address(device, index_buffer);

        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

        if (type == BLASType::Static) {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        } else {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }

        build_info.geometryCount = 1;
        build_info.pGeometries   = &geometry;
        build_info.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        uint32_t max_primitive_count = index_count / 3;
        VkAccelerationStructureBuildSizesInfoKHR size_info{};
        size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info, &max_primitive_count, &size_info);

        BLASEntry entry;
        entry.type = type;

        // Allocate buffer for the BLAS
        create_buffer(device, mem_props, size_info.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      entry.buffer, entry.memory);

        // Create the BLAS handle
        VkAccelerationStructureCreateInfoKHR create_info{};
        create_info.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create_info.buffer = entry.buffer;
        create_info.size   = size_info.accelerationStructureSize;
        create_info.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(device, &create_info, nullptr, &entry.handle);

        // Get device address for use in TLAS instances
        VkAccelerationStructureDeviceAddressInfoKHR addr_info{};
        addr_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addr_info.accelerationStructure = entry.handle;
        entry.device_address = vkGetAccelerationStructureDeviceAddressKHR(device, &addr_info);

        // Allocate scratch buffer and build
        VkBuffer scratch_buf; VkDeviceMemory scratch_mem;
        create_buffer(device, mem_props, size_info.buildScratchSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      scratch_buf, scratch_mem);

        build_info.dstAccelerationStructure  = entry.handle;
        build_info.scratchData.deviceAddress = get_buffer_device_address(device, scratch_buf);

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount  = max_primitive_count;
        range.primitiveOffset = 0;
        range.firstVertex     = 0;
        range.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR* p_range = &range;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build_info, &p_range);

        // Barrier before TLAS build
        VkMemoryBarrier barrier{};
        barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr);

        // Cleanup scratch (defer to after GPU finishes in real implementation)
        vkDestroyBuffer(device, scratch_buf, nullptr);
        vkFreeMemory(device, scratch_mem, nullptr);

        return entry;
    }

    // Rebuild TLAS every frame with current instance transforms.
    // Uses PREFER_FAST_BUILD since it changes every frame.
    void rebuild_tlas(VkDevice device,
                      VkCommandBuffer cmd,
                      VkPhysicalDeviceMemoryProperties mem_props,
                      const std::vector<TLASInstance>& instances)
    {
        // Convert to VkAccelerationStructureInstanceKHR
        std::vector<VkAccelerationStructureInstanceKHR> vk_instances;
        vk_instances.reserve(instances.size());

        for (const auto& inst : instances) {
            VkAccelerationStructureInstanceKHR vk_inst{};
            // DXR uses row-major 3x4, so we transpose the 4x4
            const auto& m = inst.transform;
            vk_inst.transform.matrix[0][0] = m(0,0); vk_inst.transform.matrix[0][1] = m(0,1);
            vk_inst.transform.matrix[0][2] = m(0,2); vk_inst.transform.matrix[0][3] = m(0,3);
            vk_inst.transform.matrix[1][0] = m(1,0); vk_inst.transform.matrix[1][1] = m(1,1);
            vk_inst.transform.matrix[1][2] = m(1,2); vk_inst.transform.matrix[1][3] = m(1,3);
            vk_inst.transform.matrix[2][0] = m(2,0); vk_inst.transform.matrix[2][1] = m(2,1);
            vk_inst.transform.matrix[2][2] = m(2,2); vk_inst.transform.matrix[2][3] = m(2,3);
            vk_inst.instanceCustomIndex      = inst.custom_index;
            vk_inst.mask                     = inst.mask;
            vk_inst.instanceShaderBindingTableRecordOffset = 0;
            vk_inst.flags                    = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            vk_inst.accelerationStructureReference = inst.blas_address;
            vk_instances.push_back(vk_inst);
        }

        // Upload instance data to GPU buffer (use a staging buffer in production)
        // ... (buffer upload code omitted for brevity) ...

        VkAccelerationStructureGeometryKHR tlas_geometry{};
        tlas_geometry.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tlas_geometry.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        tlas_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        // tlas_geometry.geometry.instances.data = instance_buffer_device_address;

        VkAccelerationStructureBuildGeometryInfoKHR build_info{};
        build_info.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        build_info.geometryCount = 1;
        build_info.pGeometries   = &tlas_geometry;
        build_info.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        // (size query, buffer allocation, and vkCmdBuildAccelerationStructuresKHR follow same
        //  pattern as BLAS — omitted to avoid repetition)
    }

private:
    VkDeviceAddress get_buffer_device_address(VkDevice device, VkBuffer buffer) {
        VkBufferDeviceAddressInfo info{};
        info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = buffer;
        return vkGetBufferDeviceAddress(device, &info);
    }

    void create_buffer(VkDevice device,
                       VkPhysicalDeviceMemoryProperties mem_props,
                       VkDeviceSize size, VkBufferUsageFlags usage,
                       VkBuffer& out_buf, VkDeviceMemory& out_mem)
    {
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size  = size;
        buf_info.usage = usage;
        vkCreateBuffer(device, &buf_info, nullptr, &out_buf);

        VkMemoryRequirements mem_req;
        vkGetBufferMemoryRequirements(device, out_buf, &mem_req);

        VkMemoryAllocateFlagsInfo flags_info{};
        flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext          = &flags_info;
        alloc_info.allocationSize = mem_req.size;
        // find memory type (standard helper, omitted)
        vkAllocateMemory(device, &alloc_info, nullptr, &out_mem);
        vkBindBufferMemory(device, out_buf, out_mem, 0);
    }
};

} // namespace gws::renderer::rt
```

## 2b. Shadow Ray Generation Shader (GLSL)

```glsl
// engine/renderer/shaders/rt_shadow.rgen
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadEXT float shadow_visibility; // 0=occluded, 1=visible

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 1, r32f) uniform image2D shadow_output;
layout(set = 0, binding = 2) uniform sampler2D gbuf_depth;
layout(set = 0, binding = 3) uniform sampler2D gbuf_normal;

layout(push_constant) uniform ShadowData {
    mat4  inv_proj;
    mat4  inv_view;
    vec3  light_direction;  // normalized, world space
    float shadow_bias;
    vec2  screen_size;
};

vec3 reconstruct_world_pos(ivec2 pixel) {
    vec2  uv    = (vec2(pixel) + 0.5) / screen_size;
    float depth = texture(gbuf_depth, uv).r;
    if (depth >= 1.0) return vec3(0.0);
    vec4 ndc      = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view_pos = inv_proj * ndc;
    view_pos /= view_pos.w;
    return (inv_view * view_pos).xyz;
}

void main() {
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    vec2  uv    = (vec2(pixel) + 0.5) / screen_size;

    vec3 world_pos = reconstruct_world_pos(pixel);
    vec3 normal    = texture(gbuf_normal, uv).xyz * 2.0 - 1.0; // simplified decode

    // Offset origin along normal to avoid self-intersection
    vec3 ray_origin = world_pos + normal * shadow_bias;
    vec3 ray_dir    = -light_direction;

    shadow_visibility = 1.0; // default: lit

    // Trace a shadow ray toward the light
    // Using SKIP_PROCEDURAL_PRIMITIVES for performance (only triangle meshes)
    traceRayEXT(
        tlas,
        gl_RayFlagsTerminateOnFirstHitEXT |         // stop at first hit
        gl_RayFlagsSkipClosestHitShaderEXT  |        // no closest-hit needed for shadow
        gl_RayFlagsOpaqueEXT,                        // treat everything as opaque
        0xFF,                                        // cull mask
        0,                                           // SBT offset
        0,                                           // SBT stride
        0,                                           // miss shader index
        ray_origin,
        0.001,                                       // t_min
        ray_dir,
        10000.0,                                     // t_max (sun is very far)
        0                                            // payload location
    );

    imageStore(shadow_output, pixel, vec4(shadow_visibility));
}
```

```glsl
// engine/renderer/shaders/rt_shadow.rmiss
#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT float shadow_visibility;

void main() {
    // Miss = no occluder found = fragment is lit
    shadow_visibility = 1.0;
}
```

---

# 3. Two-Pass HZB Occlusion Culling

## 3a. HZB Generation (GLSL Compute)

```glsl
// engine/renderer/shaders/hzb_downsample.comp
#version 450
layout(local_size_x = 8, local_size_y = 8) in;

// Source: previous mip level (or full-res depth for mip 0)
layout(set = 0, binding = 0) uniform sampler2D src_depth;
// Destination: current mip level (writable)
layout(set = 0, binding = 1, r32f) uniform writeonly image2D dst_hzb;

layout(push_constant) uniform MipData {
    ivec2 src_size;   // size of source mip
    ivec2 dst_size;   // size of destination mip
};

void main() {
    ivec2 dst_coord = ivec2(gl_GlobalInvocationID.xy);
    if (dst_coord.x >= dst_size.x || dst_coord.y >= dst_size.y) return;

    // Each destination texel covers 2×2 source texels.
    // Store MINIMUM depth (reversed-Z: smaller = farther from camera).
    // Conservative: if ANY of the 4 source texels is closer, the cluster might be visible.
    ivec2 src_base = dst_coord * 2;

    float d00 = texelFetch(src_depth, clamp(src_base + ivec2(0,0), ivec2(0), src_size-1), 0).r;
    float d10 = texelFetch(src_depth, clamp(src_base + ivec2(1,0), ivec2(0), src_size-1), 0).r;
    float d01 = texelFetch(src_depth, clamp(src_base + ivec2(0,1), ivec2(0), src_size-1), 0).r;
    float d11 = texelFetch(src_depth, clamp(src_base + ivec2(1,1), ivec2(0), src_size-1), 0).r;

    // Reversed-Z: 1.0 = near plane, 0.0 = far plane.
    // We want the FARTHEST (smallest value) for conservative occlusion:
    // if an object's nearest Z is > HZB max, it's fully behind visible geometry.
    float hzb_val = min(min(d00, d10), min(d01, d11));

    imageStore(dst_hzb, dst_coord, vec4(hzb_val));
}
```

## 3b. HZB Occlusion Culling + LOD Selection (GLSL Compute)

```glsl
// engine/renderer/shaders/hzb_cull.comp
#version 450
layout(local_size_x = 64) in;

struct InstanceData {
    vec3  aabb_min;    float _p0;
    vec3  aabb_max;    float _p1;
    uint  mesh_id;
    uint  material_id;
    uint  lod_count;   // how many LOD levels exist for this mesh
    float screen_coverage_thresholds[5]; // per-LOD switch distance
};

struct DrawCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int  vertex_offset;
    uint first_instance;
};

layout(std430, binding = 0) readonly  buffer Instances    { InstanceData instances[]; };
layout(std430, binding = 1) writeonly buffer DrawCmds     { DrawCommand  draw_cmds[]; };    // per LOD tier
layout(std430, binding = 2)           buffer DrawCounts   { uint         draw_counts[5]; };  // atomic
layout(set = 1, binding = 0) uniform  sampler2D hzb;

layout(push_constant) uniform CullData {
    mat4  view_proj;
    vec2  screen_size;
    uint  instance_count;
    float near_z;
    uint  hzb_mip_count;
};

// Project a world-space AABB into NDC, return screen coverage (0–1)
float project_aabb(vec3 aabb_min, vec3 aabb_max,
                   out vec2 ndc_min, out vec2 ndc_max, out float ndc_z_min)
{
    ndc_min = vec2( 1.0);
    ndc_max = vec2(-1.0);
    ndc_z_min = 1.0;

    // Transform 8 AABB corners
    for (int i = 0; i < 8; ++i) {
        vec3 corner = vec3(
            (i & 1) != 0 ? aabb_max.x : aabb_min.x,
            (i & 2) != 0 ? aabb_max.y : aabb_min.y,
            (i & 4) != 0 ? aabb_max.z : aabb_min.z
        );
        vec4 clip = view_proj * vec4(corner, 1.0);
        if (clip.w <= 0.001) continue;
        vec3 ndc = clip.xyz / clip.w;
        ndc_min = min(ndc_min, ndc.xy);
        ndc_max = max(ndc_max, ndc.xy);
        ndc_z_min = min(ndc_z_min, ndc.z);
    }

    vec2 size = ndc_max - ndc_min;
    return size.x * size.y * 0.25; // screen coverage fraction
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= instance_count) return;

    InstanceData inst = instances[idx];

    // ---- 1. Project AABB to NDC -----------------------------------
    vec2  ndc_min, ndc_max;
    float ndc_z_min;
    float coverage = project_aabb(inst.aabb_min, inst.aabb_max,
                                   ndc_min, ndc_max, ndc_z_min);

    // ---- 2. Frustum culling (early-out) ---------------------------
    if (ndc_min.x > 1.0 || ndc_max.x < -1.0 ||
        ndc_min.y > 1.0 || ndc_max.y < -1.0 ||
        ndc_z_min > 1.0) return; // outside frustum

    // ---- 3. HZB occlusion test ------------------------------------
    // Convert NDC [-1,1] to UV [0,1]
    vec2 uv_min = ndc_min * 0.5 + 0.5;
    vec2 uv_max = ndc_max * 0.5 + 0.5;

    // Screen-space size in texels → choose mip level
    vec2 texel_size = (uv_max - uv_min) * screen_size;
    float mip = ceil(log2(max(texel_size.x, texel_size.y)));
    mip = clamp(mip, 0.0, float(hzb_mip_count - 1));

    // Sample 4 corners of projected AABB from HZB
    float h00 = textureLod(hzb, uv_min,                mip).r;
    float h10 = textureLod(hzb, vec2(uv_max.x,uv_min.y), mip).r;
    float h01 = textureLod(hzb, vec2(uv_min.x,uv_max.y), mip).r;
    float h11 = textureLod(hzb, uv_max,                mip).r;

    // Reversed-Z: farthest = smallest. HZB stores minimum (farthest).
    float hzb_depth = min(min(h00, h10), min(h01, h11));

    // If the closest corner of the AABB (ndc_z_min) is FARTHER than
    // everything in the HZB region, it's fully occluded.
    if (ndc_z_min < hzb_depth) return; // occluded! (reversed-Z: closer = larger value)

    // ---- 4. LOD selection -----------------------------------------
    // Select the LOD that fits screen coverage
    uint lod = 0;
    for (uint i = 0; i < inst.lod_count - 1; ++i) {
        if (coverage < inst.screen_coverage_thresholds[i])
            lod = i + 1;
    }

    // ---- 5. Write indirect draw command ---------------------------
    uint draw_idx = atomicAdd(draw_counts[lod], 1);
    // Each LOD tier has its own draw buffer region (offset by MAX_INSTANCES per tier)
    uint buffer_idx = lod * 100000 + draw_idx; // MAX_INSTANCES = 100000
    draw_cmds[buffer_idx].index_count    = 0;  // filled from mesh LOD table
    draw_cmds[buffer_idx].instance_count = 1;
    draw_cmds[buffer_idx].first_index    = 0;  // filled from mesh LOD table
    draw_cmds[buffer_idx].vertex_offset  = 0;
    draw_cmds[buffer_idx].first_instance = idx; // instance data index
}
```

---

# 4. LOD System with meshoptimizer

```cpp
// engine/renderer/lod/lod_generator.cpp
#include "lod_generator.h"
#include <meshoptimizer.h>
#include <vector>
#include <cassert>

namespace gws::renderer {

// -------------------------------------------------------------------
// Generates up to 5 LOD levels for a mesh at import time.
// Uses meshoptimizer's quadric error simplification.
// Outputs a chain: each LOD re-simplified from the previous one.
// -------------------------------------------------------------------

struct LODLevel {
    std::vector<uint32_t> indices;
    float                 error;        // normalized deviation from LOD0
    float                 accumulated_error; // sum of errors across chain
};

struct LODMesh {
    std::vector<float>    vertices;     // interleaved: px py pz [nx ny nz] [u v]
    uint32_t              vertex_stride;
    std::vector<LODLevel> lods;
};

static const float kLODTargets[]   = { 1.0f, 0.50f, 0.25f, 0.10f, 0.02f };
static const float kLODErrors[]    = { 0.0f, 0.01f, 0.02f, 0.05f, 0.10f }; // max error per level
static const int   kLODCount       = 5;

LODMesh generate_lod_chain(const float* positions,    // XYZ packed
                            uint32_t     vertex_count,
                            uint32_t     vertex_stride, // bytes
                            const uint32_t* indices,
                            uint32_t     index_count)
{
    LODMesh result;
    result.vertex_stride = vertex_stride;

    // Copy vertex data
    result.vertices.resize(vertex_count * (vertex_stride / sizeof(float)));
    memcpy(result.vertices.data(), positions, vertex_count * vertex_stride);

    result.lods.resize(kLODCount);

    // LOD0 = original
    result.lods[0].indices.assign(indices, indices + index_count);
    result.lods[0].error = 0.0f;
    result.lods[0].accumulated_error = 0.0f;

    // Optimize LOD0 vertex cache before simplification chain
    meshopt_optimizeVertexCache(result.lods[0].indices.data(),
                                result.lods[0].indices.data(),
                                index_count, vertex_count);

    // Generate LOD1–LOD4 by simplifying from the previous LOD
    for (int lod = 1; lod < kLODCount; ++lod) {
        const auto& prev_lod = result.lods[lod - 1];
        uint32_t    prev_count   = (uint32_t)prev_lod.indices.size();
        uint32_t    target_count = (uint32_t)(index_count * kLODTargets[lod]);
        float       max_error    = kLODErrors[lod];

        std::vector<uint32_t> simplified(prev_count);
        float result_error = 0.0f;

        // meshopt_simplify with attribute error and border locking
        size_t new_count = meshopt_simplify(
            simplified.data(),
            prev_lod.indices.data(),
            prev_count,
            positions,              // position data
            vertex_count,
            vertex_stride,
            target_count,
            max_error,
            meshopt_SimplifyLockBorder, // preserve mesh edges (for terrain chunks)
            &result_error
        );

        simplified.resize(new_count);

        // Optimize the simplified mesh for vertex cache
        meshopt_optimizeVertexCache(simplified.data(), simplified.data(),
                                    new_count, vertex_count);

        result.lods[lod].indices = std::move(simplified);
        result.lods[lod].error   = result_error;
        result.lods[lod].accumulated_error =
            prev_lod.accumulated_error + result_error;

        // If simplification got "stuck" (less than 5% reduction), stop early
        if (new_count > (uint32_t)(prev_count * 0.95f)) {
            result.lods.resize(lod + 1);
            break;
        }
    }

    // Final optimization: assemble all LODs into one large index buffer
    // (coarsest first) and run meshopt_optimizeVertexFetch on the whole buffer.
    // This ensures coarser LODs need a smaller vertex range.
    std::vector<uint32_t> combined;
    for (int lod = kLODCount - 1; lod >= 0; --lod) {
        if (lod < (int)result.lods.size())
            combined.insert(combined.end(),
                            result.lods[lod].indices.begin(),
                            result.lods[lod].indices.end());
    }

    std::vector<uint32_t> remap(vertex_count);
    uint32_t unique_count = (uint32_t)meshopt_optimizeVertexFetchRemap(
        remap.data(), combined.data(), combined.size(), vertex_count);

    meshopt_remapVertexBuffer(result.vertices.data(), positions,
                              vertex_count, vertex_stride, remap.data());

    // Remap all LOD index buffers
    size_t offset = 0;
    for (int lod = kLODCount - 1; lod >= 0; --lod) {
        if (lod >= (int)result.lods.size()) continue;
        size_t count = result.lods[lod].indices.size();
        meshopt_remapIndexBuffer(result.lods[lod].indices.data(),
                                  combined.data() + offset, count, remap.data());
        offset += count;
    }

    return result;
}

// Runtime: select LOD level based on screen-space coverage
uint32_t select_lod(const LODMesh& mesh, float screen_coverage) {
    // Coverage thresholds match the error levels used during generation.
    // Use accumulated error to compensate across the LOD chain.
    static const float kScreenThresholds[] = {
        1.00f,  // LOD0: full detail at coverage > 10%
        0.10f,  // LOD1
        0.04f,  // LOD2
        0.01f,  // LOD3
        0.002f  // LOD4
    };

    for (uint32_t lod = 0; lod < (uint32_t)mesh.lods.size() - 1; ++lod) {
        if (screen_coverage >= kScreenThresholds[lod])
            return lod;
    }
    return (uint32_t)mesh.lods.size() - 1;
}

} // namespace gws::renderer
```

---

# 5. ECS with EnTT

```cpp
// engine/ecs/world.h
#pragma once
#include <entt/entt.hpp>
#include "engine/core/math/math.h"

namespace gws::ecs {

// -------------------------------------------------------------------
// Core component types — plain data, no methods.
// -------------------------------------------------------------------

struct TransformComponent {
    gws::math::Vec3       position  = {};
    gws::math::Quaternion rotation  = {};
    gws::math::Vec3       scale     = { 1,1,1 };
};

struct RenderableComponent {
    uint32_t mesh_id;
    uint32_t material_id;
    uint8_t  lod_bias = 0;      // force a LOD offset (-2..+2)
    bool     cast_shadow = true;
    bool     visible     = true;
};

struct VelocityComponent {
    gws::math::Vec3 linear  = {};
    gws::math::Vec3 angular = {};
};

struct HealthComponent {
    float current;
    float max;
    bool  is_dead = false;
};

struct FactionComponent {
    enum Faction : uint8_t { Player, Ally, Enemy, Neutral };
    Faction faction = Neutral;
};

// -------------------------------------------------------------------
// World: thin wrapper around entt::registry providing typed queries.
// -------------------------------------------------------------------

class World {
public:
    entt::registry& registry() { return registry_; }

    entt::entity create_entity() {
        return registry_.create();
    }

    void destroy_entity(entt::entity e) {
        registry_.destroy(e);
    }

    template<typename T, typename... Args>
    T& add_component(entt::entity e, Args&&... args) {
        return registry_.emplace<T>(e, std::forward<Args>(args)...);
    }

    template<typename T>
    T& get_component(entt::entity e) {
        return registry_.get<T>(e);
    }

    template<typename T>
    bool has_component(entt::entity e) const {
        return registry_.all_of<T>(e);
    }

    template<typename T>
    void remove_component(entt::entity e) {
        registry_.remove<T>(e);
    }

    // ---- Optimized group queries (contiguous iteration) -----------

    // Render system: iterate transforms + renderables together.
    // EnTT groups sort storage so both arrays are accessed contiguously.
    auto render_group() {
        return registry_.group<RenderableComponent, TransformComponent>();
    }

    // Movement system: entities with velocity + transform
    auto movement_view() {
        return registry_.view<VelocityComponent, TransformComponent>();
    }

    // Combat system: alive enemies near player
    auto enemy_view() {
        return registry_.view<HealthComponent, TransformComponent, FactionComponent>();
    }

private:
    entt::registry registry_;
};

// -------------------------------------------------------------------
// Example usage — Scene setup
// -------------------------------------------------------------------

inline void example_scene_setup(World& world) {
    // Create a player entity
    auto player = world.create_entity();
    auto& t = world.add_component<TransformComponent>(player);
    t.position = { 0, 0, 0 };
    world.add_component<RenderableComponent>(player, 1001u, 2001u);
    world.add_component<HealthComponent>(player, 100.0f, 100.0f);
    world.add_component<FactionComponent>(player).faction = FactionComponent::Player;
    world.add_component<VelocityComponent>(player);

    // Create 10,000 grass instances (no velocity, no health — just transform + renderable)
    for (int i = 0; i < 10000; ++i) {
        auto grass = world.create_entity();
        auto& gt = world.add_component<TransformComponent>(grass);
        gt.position = { (float)(i % 100) * 2.0f, 0, (float)(i / 100) * 2.0f };
        world.add_component<RenderableComponent>(grass, 5001u, 3001u);
    }
}

// -------------------------------------------------------------------
// Render system (called once per frame by the engine)
// -------------------------------------------------------------------

class RenderSystem {
public:
    void update(World& world, struct RenderContext& ctx) {
        // EnTT group guarantees both arrays are contiguous in memory.
        // This is the hot path — cache efficiency is critical.
        auto group = world.render_group();

        group.each([&](entt::entity e,
                       RenderableComponent& renderable,
                       TransformComponent&  transform)
        {
            if (!renderable.visible) return;

            // Build model matrix and submit draw call
            // ctx.submit_draw(renderable.mesh_id, renderable.material_id,
            //                 build_matrix(transform), renderable.lod_bias);
        });
    }
};

} // namespace gws::ecs
```

---

# 6. Rollback Netcode

```cpp
// engine/network/rollback/rollback_manager.h
#pragma once
#include <array>
#include <vector>
#include <cstring>
#include <cstdint>
#include <functional>

namespace gws::network {

// -------------------------------------------------------------------
// Fixed-point position (avoids float non-determinism across platforms).
// 1 unit = 1/1000 of a meter (millimeter precision).
// -------------------------------------------------------------------

struct FixedVec3 {
    int32_t x, y, z; // millimeters

    FixedVec3 operator+(const FixedVec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    FixedVec3 operator-(const FixedVec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    FixedVec3 operator*(int32_t s)          const { return {x*s,   y*s,   z*s  }; }

    static FixedVec3 from_float(float fx, float fy, float fz) {
        return { (int32_t)(fx * 1000.f), (int32_t)(fy * 1000.f), (int32_t)(fz * 1000.f) };
    }
};

// -------------------------------------------------------------------
// Deterministic RNG — Xorshift32 stepped identically on all clients.
// Seed is derived from frame number + match seed (agreed at session start).
// -------------------------------------------------------------------

struct DeterministicRNG {
    uint32_t state;
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

// -------------------------------------------------------------------
// The entire simulation state that must be snapshot/restored.
// In production: this would be generated from ECS component pools.
// -------------------------------------------------------------------

static constexpr int MAX_PLAYERS = 8;
static constexpr int MAX_PROJECTILES = 512;

struct PlayerSimState {
    FixedVec3 position;
    FixedVec3 velocity;
    int32_t   rotation_deg_x1000; // fixed-point degrees
    float     health;             // floats OK for non-deterministic display state
    uint8_t   animation_state_id;
    bool      is_grounded;
    bool      is_dead;
};

struct ProjectileSimState {
    FixedVec3 position;
    FixedVec3 velocity;
    uint8_t   owner_index;
    bool      active;
};

struct SimulationState {
    PlayerSimState     players[MAX_PLAYERS];
    ProjectileSimState projectiles[MAX_PROJECTILES];
    DeterministicRNG   rng;
    uint32_t           frame;
};

// -------------------------------------------------------------------
// Player input — sent over the network each frame.
// Fits in a small packet (< 8 bytes).
// -------------------------------------------------------------------

struct PlayerInput {
    int8_t  move_x;       // -127 to +127
    int8_t  move_y;
    uint8_t buttons;      // bitmask: jump|attack|dodge|ability0|ability1|ability2|ult|...
    int16_t aim_x;        // signed degrees * 100
    int16_t aim_y;
    uint8_t frame_delta;  // how many frames since last confirmed input
};

// -------------------------------------------------------------------
// RollbackManager: implements the core GGPO loop.
// -------------------------------------------------------------------

static constexpr int MAX_ROLLBACK_FRAMES = 10;
static constexpr int MAX_PLAYERS_CONNECTED = 8;

class RollbackManager {
public:
    using SimulateFn = std::function<void(SimulationState&,
                                          const PlayerInput inputs[MAX_PLAYERS],
                                          int player_count)>;

    RollbackManager() {
        // Initialize state ring buffer
        for (auto& s : state_buffer_) s = {};
    }

    void init(int local_player_index, int player_count, SimulateFn sim_fn) {
        local_player_   = local_player_index;
        player_count_   = player_count;
        simulate_       = std::move(sim_fn);
        current_frame_  = 0;
    }

    // Called every render frame. Returns the frame state to render.
    const SimulationState& advance_frame(const PlayerInput& local_input) {
        // Store local input immediately — no waiting
        set_local_input(current_frame_, local_input);

        // Predict remote inputs (repeat last known)
        for (int p = 0; p < player_count_; ++p) {
            if (p == local_player_) continue;
            if (!has_confirmed_input(current_frame_, p)) {
                predict_input(current_frame_, p);
            }
        }

        // Simulate current frame
        snapshot_state(current_frame_);
        run_simulation(current_frame_);
        current_frame_++;

        return current_state();
    }

    // Called when confirmed remote inputs arrive (potentially from the past).
    void receive_remote_input(int player_index, int frame, const PlayerInput& input) {
        if (frame > current_frame_) {
            // Future input — store for later
            store_input(frame, player_index, input);
            return;
        }

        // Check if this differs from our prediction
        PlayerInput predicted = get_input(frame, player_index);
        if (memcmp(&predicted, &input, sizeof(PlayerInput)) == 0) return; // matches

        // Mismatch: rollback to this frame
        store_input(frame, player_index, input);
        rollback_to(frame);
    }

private:
    // Roll back to 'target_frame' and re-simulate up to current_frame_
    void rollback_to(int target_frame) {
        int rollback_count = current_frame_ - target_frame;
        if (rollback_count <= 0 || rollback_count > MAX_ROLLBACK_FRAMES) return;

        // Restore state snapshot at target_frame
        restore_snapshot(target_frame);

        // Re-simulate all frames from target to current
        for (int frame = target_frame; frame < current_frame_; ++frame) {
            run_simulation(frame);
        }
        // Note: presentation layer (VFX, audio) is NOT re-simulated.
        // Those fire-and-forget events are re-triggered from simulation
        // events if they differ from what was already played.
    }

    void run_simulation(int frame) {
        PlayerInput inputs[MAX_PLAYERS];
        for (int p = 0; p < player_count_; ++p)
            inputs[p] = get_input(frame, p);

        simulate_(get_state(frame), inputs, player_count_);
    }

    void snapshot_state(int frame) {
        int slot = frame % (MAX_ROLLBACK_FRAMES + 1);
        memcpy(&state_buffer_[slot], &get_state(frame - 1), sizeof(SimulationState));
    }

    void restore_snapshot(int frame) {
        int slot = frame % (MAX_ROLLBACK_FRAMES + 1);
        memcpy(&get_state(frame), &state_buffer_[slot], sizeof(SimulationState));
    }

    SimulationState& get_state(int frame) {
        return state_buffer_[frame % (MAX_ROLLBACK_FRAMES + 1)];
    }

    const SimulationState& current_state() const {
        return state_buffer_[(current_frame_ - 1) % (MAX_ROLLBACK_FRAMES + 1)];
    }

    void set_local_input(int frame, const PlayerInput& input) {
        input_buffer_[frame % (MAX_ROLLBACK_FRAMES * 2)][local_player_] = input;
        input_confirmed_[frame % (MAX_ROLLBACK_FRAMES * 2)][local_player_] = true;
    }

    void store_input(int frame, int player, const PlayerInput& input) {
        input_buffer_[frame % (MAX_ROLLBACK_FRAMES * 2)][player] = input;
        input_confirmed_[frame % (MAX_ROLLBACK_FRAMES * 2)][player] = true;
    }

    PlayerInput get_input(int frame, int player) const {
        return input_buffer_[frame % (MAX_ROLLBACK_FRAMES * 2)][player];
    }

    bool has_confirmed_input(int frame, int player) const {
        return input_confirmed_[frame % (MAX_ROLLBACK_FRAMES * 2)][player];
    }

    void predict_input(int frame, int player) {
        // Simplest prediction: repeat the last confirmed input
        int last = frame - 1;
        while (last >= 0 && !has_confirmed_input(last, player)) --last;
        PlayerInput predicted = (last >= 0) ? get_input(last, player) : PlayerInput{};
        store_input(frame, player, predicted);
        // Mark as predicted (not confirmed)
        input_confirmed_[frame % (MAX_ROLLBACK_FRAMES * 2)][player] = false;
    }

    SimulateFn    simulate_;
    int           local_player_  = 0;
    int           player_count_  = 1;
    int           current_frame_ = 0;

    SimulationState state_buffer_[MAX_ROLLBACK_FRAMES + 1];
    PlayerInput     input_buffer_[MAX_ROLLBACK_FRAMES * 2][MAX_PLAYERS];
    bool            input_confirmed_[MAX_ROLLBACK_FRAMES * 2][MAX_PLAYERS] = {};
};

} // namespace gws::network
```

---

# 7. Behavior Tree System

```cpp
// engine/ai/behavior_tree/bt_nodes.h
#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <chrono>

namespace gws::ai {

// -------------------------------------------------------------------
// Return status for all BT nodes
// -------------------------------------------------------------------

enum class BTStatus { Success, Failure, Running };

// -------------------------------------------------------------------
// Blackboard: typed key-value store per AI agent.
// Observer pattern: value changes trigger decorator re-evaluation.
// -------------------------------------------------------------------

using BBValue = std::variant<bool, int, float, std::string>;

class Blackboard {
public:
    template<typename T>
    void set(const std::string& key, T value) {
        data_[key] = BBValue(value);
        notify_observers(key);
    }

    template<typename T>
    T get(const std::string& key, T default_val = T{}) const {
        auto it = data_.find(key);
        if (it == data_.end()) return default_val;
        const T* val = std::get_if<T>(&it->second);
        return val ? *val : default_val;
    }

    bool has(const std::string& key) const {
        return data_.count(key) > 0;
    }

    void register_observer(const std::string& key, std::function<void()> cb) {
        observers_[key].push_back(std::move(cb));
    }

private:
    void notify_observers(const std::string& key) {
        auto it = observers_.find(key);
        if (it != observers_.end())
            for (auto& cb : it->second) cb();
    }

    std::unordered_map<std::string, BBValue>                    data_;
    std::unordered_map<std::string, std::vector<std::function<void()>>> observers_;
};

// -------------------------------------------------------------------
// Base node
// -------------------------------------------------------------------

struct BTContext {
    uint32_t    entity_id;
    Blackboard* blackboard;
    float       dt;
    float       time; // current world time
};

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus tick(BTContext& ctx) = 0;
    virtual void     reset() {}  // reset node to initial state
    std::string name;
};

// -------------------------------------------------------------------
// Composite nodes
// -------------------------------------------------------------------

// Sequence: execute children left-to-right; fail on first failure
class Sequence : public BTNode {
public:
    explicit Sequence(std::vector<std::shared_ptr<BTNode>> children)
        : children_(std::move(children)), current_(0) {}

    BTStatus tick(BTContext& ctx) override {
        while (current_ < children_.size()) {
            BTStatus s = children_[current_]->tick(ctx);
            if (s == BTStatus::Failure) { reset(); return BTStatus::Failure; }
            if (s == BTStatus::Running)              return BTStatus::Running;
            ++current_; // Success: advance to next child
        }
        reset();
        return BTStatus::Success;
    }

    void reset() override {
        for (auto& c : children_) c->reset();
        current_ = 0;
    }

private:
    std::vector<std::shared_ptr<BTNode>> children_;
    size_t current_ = 0;
};

// Selector: execute children until one succeeds
class Selector : public BTNode {
public:
    explicit Selector(std::vector<std::shared_ptr<BTNode>> children)
        : children_(std::move(children)), current_(0) {}

    BTStatus tick(BTContext& ctx) override {
        while (current_ < children_.size()) {
            BTStatus s = children_[current_]->tick(ctx);
            if (s == BTStatus::Success) { reset(); return BTStatus::Success; }
            if (s == BTStatus::Running)              return BTStatus::Running;
            ++current_; // Failure: try next child
        }
        reset();
        return BTStatus::Failure;
    }

    void reset() override {
        for (auto& c : children_) c->reset();
        current_ = 0;
    }

private:
    std::vector<std::shared_ptr<BTNode>> children_;
    size_t current_ = 0;
};

// -------------------------------------------------------------------
// Decorator nodes
// -------------------------------------------------------------------

// Cooldown: prevent a child from running more than once per interval
class CooldownDecorator : public BTNode {
public:
    CooldownDecorator(std::shared_ptr<BTNode> child, float cooldown_secs)
        : child_(std::move(child)), cooldown_(cooldown_secs) {}

    BTStatus tick(BTContext& ctx) override {
        if (ctx.time - last_success_time_ < cooldown_)
            return BTStatus::Failure;

        BTStatus s = child_->tick(ctx);
        if (s == BTStatus::Success)
            last_success_time_ = ctx.time;
        return s;
    }

    void reset() override { child_->reset(); }

private:
    std::shared_ptr<BTNode> child_;
    float cooldown_;
    float last_success_time_ = -9999.0f;
};

// -------------------------------------------------------------------
// Leaf action: wraps a C++ lambda or registered function
// -------------------------------------------------------------------

class ActionNode : public BTNode {
public:
    using ActionFn = std::function<BTStatus(BTContext&)>;
    explicit ActionNode(ActionFn fn, std::string n = "action")
        : fn_(std::move(fn)) { name = std::move(n); }

    BTStatus tick(BTContext& ctx) override { return fn_(ctx); }

private:
    ActionFn fn_;
};

// -------------------------------------------------------------------
// Condition node: wraps a predicate
// -------------------------------------------------------------------

class ConditionNode : public BTNode {
public:
    using CondFn = std::function<bool(BTContext&)>;
    explicit ConditionNode(CondFn fn, std::string n = "condition")
        : fn_(std::move(fn)) { name = std::move(n); }

    BTStatus tick(BTContext& ctx) override {
        return fn_(ctx) ? BTStatus::Success : BTStatus::Failure;
    }

private:
    CondFn fn_;
};

// -------------------------------------------------------------------
// Wait node with "waiting" optimization (lisyarus pattern):
// Returns Running but stores a wake-up time so the AI system can
// skip ticking this agent until the timer expires.
// -------------------------------------------------------------------

class WaitNode : public BTNode {
public:
    explicit WaitNode(float duration) : duration_(duration) {}

    BTStatus tick(BTContext& ctx) override {
        if (start_time_ < 0.0f) start_time_ = ctx.time;
        if (ctx.time - start_time_ >= duration_) {
            start_time_ = -1.0f;
            return BTStatus::Success;
        }
        // Store wake time on blackboard so AISystem can skip this agent
        ctx.blackboard->set<float>("bt_wake_time", start_time_ + duration_);
        return BTStatus::Running;
    }

    void reset() override { start_time_ = -1.0f; }

private:
    float duration_;
    float start_time_ = -1.0f;
};

// -------------------------------------------------------------------
// Example: Enemy patrol + chase + attack tree
// -------------------------------------------------------------------
//
//  Selector
//  ├── Sequence (Attack)
//  │   ├── Condition: "can_see_player"
//  │   ├── Condition: "in_attack_range"
//  │   └── Action: "perform_attack"  [cooldown 1.5s]
//  ├── Sequence (Chase)
//  │   ├── Condition: "can_see_player"
//  │   └── Action: "move_toward_player"
//  └── Sequence (Patrol)
//      ├── Action: "move_to_patrol_point"
//      └── Wait: 2.0s

inline std::shared_ptr<BTNode> build_enemy_tree() {
    // Attack branch
    auto can_see   = std::make_shared<ConditionNode>([](BTContext& ctx) {
        return ctx.blackboard->get<bool>("can_see_player", false);
    }, "can_see_player");

    auto in_range  = std::make_shared<ConditionNode>([](BTContext& ctx) {
        return ctx.blackboard->get<float>("player_distance", 9999.f) < 2.5f;
    }, "in_attack_range");

    auto attack    = std::make_shared<ActionNode>([](BTContext& ctx) -> BTStatus {
        // Trigger attack animation, hitbox, etc.
        ctx.blackboard->set<bool>("trigger_attack", true);
        return BTStatus::Success;
    }, "perform_attack");

    auto attack_seq = std::make_shared<Sequence>(
        std::vector<std::shared_ptr<BTNode>>{can_see, in_range,
            std::make_shared<CooldownDecorator>(attack, 1.5f)});

    // Chase branch
    auto can_see2  = std::make_shared<ConditionNode>([](BTContext& ctx) {
        return ctx.blackboard->get<bool>("can_see_player", false);
    }, "can_see_player_2");

    auto chase     = std::make_shared<ActionNode>([](BTContext& ctx) -> BTStatus {
        ctx.blackboard->set<bool>("nav_to_player", true);
        return BTStatus::Running; // runs until player lost
    }, "move_toward_player");

    auto chase_seq = std::make_shared<Sequence>(
        std::vector<std::shared_ptr<BTNode>>{can_see2, chase});

    // Patrol branch
    auto patrol    = std::make_shared<ActionNode>([](BTContext& ctx) -> BTStatus {
        ctx.blackboard->set<bool>("nav_to_patrol", true);
        return BTStatus::Success;
    }, "move_to_patrol_point");

    auto wait      = std::make_shared<WaitNode>(2.0f);
    auto patrol_seq = std::make_shared<Sequence>(
        std::vector<std::shared_ptr<BTNode>>{patrol, wait});

    return std::make_shared<Selector>(
        std::vector<std::shared_ptr<BTNode>>{attack_seq, chase_seq, patrol_seq});
}

// -------------------------------------------------------------------
// AI System: priority queue, skips sleeping agents
// -------------------------------------------------------------------

struct AIAgent {
    uint32_t                  entity_id;
    std::shared_ptr<BTNode>   tree;
    Blackboard                blackboard;
    float                     wake_time = 0.0f; // from WaitNode optimization
};

class AISystem {
public:
    void register_agent(uint32_t entity_id, std::shared_ptr<BTNode> tree) {
        agents_.push_back({ entity_id, std::move(tree), {}, 0.0f });
    }

    void update(float current_time, float dt) {
        for (auto& agent : agents_) {
            // Skip agents still sleeping (WaitNode optimization)
            if (current_time < agent.wake_time) continue;

            agent.blackboard.set<float>("bt_wake_time", 0.0f); // clear wake time

            BTContext ctx;
            ctx.entity_id  = agent.entity_id;
            ctx.blackboard = &agent.blackboard;
            ctx.dt         = dt;
            ctx.time       = current_time;

            agent.tree->tick(ctx);

            // Check if the tree set a wake time (from WaitNode)
            float wake = agent.blackboard.get<float>("bt_wake_time", 0.0f);
            if (wake > current_time) agent.wake_time = wake;
        }
    }

private:
    std::vector<AIAgent> agents_;
};

} // namespace gws::ai
```

---

# 8. Animation — Blend Tree & FABRIK IK

## 8a. Blend Tree

```cpp
// engine/animation/blend_tree.h
#pragma once
#include "engine/core/math/math.h"
#include <vector>
#include <memory>
#include <functional>

namespace gws::animation {

// -------------------------------------------------------------------
// Bone pose: one transform per joint
// -------------------------------------------------------------------

struct Pose {
    std::vector<gws::math::Transform> joints; // local space, one per joint
};

// -------------------------------------------------------------------
// Animation clip source: returns a pose given a time
// -------------------------------------------------------------------

class AnimationClip {
public:
    virtual ~AnimationClip() = default;
    virtual Pose sample(float time) const = 0;
    float duration() const { return duration_; }
protected:
    float duration_ = 1.0f;
};

// -------------------------------------------------------------------
// Blend node base class
// -------------------------------------------------------------------

class BlendNode {
public:
    virtual ~BlendNode() = default;
    virtual Pose evaluate(float time) const = 0;
};

// -------------------------------------------------------------------
// Clip leaf node: plays a single animation clip
// -------------------------------------------------------------------

class ClipNode : public BlendNode {
public:
    explicit ClipNode(std::shared_ptr<AnimationClip> clip, bool looping = true)
        : clip_(std::move(clip)), looping_(looping) {}

    Pose evaluate(float time) const override {
        float t = looping_
            ? std::fmod(time, clip_->duration())
            : std::min(time, clip_->duration());
        return clip_->sample(t);
    }

private:
    std::shared_ptr<AnimationClip> clip_;
    bool looping_;
};

// -------------------------------------------------------------------
// Linear blend node: lerps between two child poses
// -------------------------------------------------------------------

class BlendNode1D : public BlendNode {
public:
    BlendNode1D(std::shared_ptr<BlendNode> a, std::shared_ptr<BlendNode> b,
                float blend_weight) // 0 = full A, 1 = full B
        : a_(std::move(a)), b_(std::move(b)), weight_(blend_weight) {}

    void set_weight(float w) { weight_ = gws::math::clamp(w, 0.0f, 1.0f); }

    Pose evaluate(float time) const override {
        Pose pa = a_->evaluate(time);
        Pose pb = b_->evaluate(time);
        return blend(pa, pb, weight_);
    }

private:
    Pose blend(const Pose& a, const Pose& b, float t) const {
        Pose result;
        result.joints.resize(a.joints.size());
        for (size_t i = 0; i < a.joints.size(); ++i) {
            result.joints[i].position =
                gws::math::Vec3::lerp(a.joints[i].position, b.joints[i].position, t);
            result.joints[i].rotation =
                gws::math::Quaternion::slerp(a.joints[i].rotation, b.joints[i].rotation, t);
            result.joints[i].scale =
                gws::math::Vec3::lerp(a.joints[i].scale, b.joints[i].scale, t);
        }
        return result;
    }

    std::shared_ptr<BlendNode> a_, b_;
    float weight_;
};

// -------------------------------------------------------------------
// Additive blend: add delta pose on top of base pose.
// Used for aim offset, hit reactions, etc.
// -------------------------------------------------------------------

class AdditiveBlendNode : public BlendNode {
public:
    AdditiveBlendNode(std::shared_ptr<BlendNode> base,
                      std::shared_ptr<BlendNode> additive,
                      float weight = 1.0f)
        : base_(std::move(base)), additive_(std::move(additive)), weight_(weight) {}

    void set_weight(float w) { weight_ = gws::math::clamp(w, 0.0f, 1.0f); }

    Pose evaluate(float time) const override {
        Pose base = base_->evaluate(time);
        Pose add  = additive_->evaluate(time);
        // Apply additive delta: base + weight * add
        for (size_t i = 0; i < base.joints.size(); ++i) {
            base.joints[i].position =
                base.joints[i].position + add.joints[i].position * weight_;
            base.joints[i].rotation =
                gws::math::Quaternion::nlerp(base.joints[i].rotation,
                    base.joints[i].rotation * add.joints[i].rotation, weight_);
        }
        return base;
    }

private:
    std::shared_ptr<BlendNode> base_, additive_;
    float weight_;
};

} // namespace gws::animation
```

## 8b. FABRIK IK Solver

```cpp
// engine/animation/ik/fabrik_solver.h
#pragma once
#include "engine/core/math/math.h"
#include <vector>

namespace gws::animation {

// -------------------------------------------------------------------
// FABRIK (Forward And Backward Reaching IK)
// Aristidou & Lasenby 2009 — no matrix inversions, fast convergence.
// Used for: foot placement, hand placement, aim direction.
// -------------------------------------------------------------------

struct FABRIKChain {
    std::vector<gws::math::Vec3> positions;    // joint world positions
    std::vector<float>           lengths;       // bone lengths [i] = dist(i → i+1)
    gws::math::Vec3              root_position; // root is constrained (pinned)
    gws::math::Vec3              target;        // end effector target
    int                          max_iterations = 10;
    float                        tolerance      = 0.001f; // stop when end-effector within this
};

class FABRIKSolver {
public:
    // Solve the chain to reach `target`.
    // Returns true if convergence was reached.
    bool solve(FABRIKChain& chain) const {
        if (chain.positions.size() < 2) return false;

        // Compute total chain length
        float total_length = 0.0f;
        for (float l : chain.lengths) total_length += l;

        // Check if target is reachable
        float root_to_target = (chain.target - chain.root_position).length();
        if (root_to_target > total_length) {
            // Target is too far — stretch chain toward target
            gws::math::Vec3 dir = (chain.target - chain.root_position).normalized();
            for (size_t i = 1; i < chain.positions.size(); ++i)
                chain.positions[i] = chain.positions[i-1] + dir * chain.lengths[i-1];
            return false;
        }

        const size_t n = chain.positions.size();
        const gws::math::Vec3 root = chain.root_position;

        for (int iter = 0; iter < chain.max_iterations; ++iter) {
            // --- Forward pass: set end effector to target, propagate backward ---
            chain.positions.back() = chain.target;
            for (int i = (int)n - 2; i >= 0; --i) {
                gws::math::Vec3 dir =
                    (chain.positions[i] - chain.positions[i+1]).normalized();
                chain.positions[i] = chain.positions[i+1] + dir * chain.lengths[i];
            }

            // --- Backward pass: fix root, propagate forward ---
            chain.positions[0] = root;
            for (size_t i = 0; i < n - 1; ++i) {
                gws::math::Vec3 dir =
                    (chain.positions[i+1] - chain.positions[i]).normalized();
                chain.positions[i+1] = chain.positions[i] + dir * chain.lengths[i];
            }

            // Check convergence
            float dist = (chain.positions.back() - chain.target).length();
            if (dist < chain.tolerance) return true;
        }
        return false;
    }
};

// -------------------------------------------------------------------
// Foot IK: adapts a character's feet to terrain slope.
// -------------------------------------------------------------------

class FootIKController {
public:
    struct FootTarget {
        gws::math::Vec3 world_target;  // raycast hit point on terrain
        gws::math::Vec3 normal;        // terrain surface normal
        bool            valid;
    };

    void solve(FABRIKChain& leg_chain, const FootTarget& target, float blend) {
        if (!target.valid || blend < 0.001f) return;

        // Blend target toward terrain hit point
        gws::math::Vec3 goal = gws::math::Vec3::lerp(
            leg_chain.positions.back(), target.world_target, blend);
        leg_chain.target = goal;

        solver_.solve(leg_chain);

        // Rotate the foot bone to align with terrain normal
        // (rotate foot joint to match surface normal — rotate_foot_to_normal
        //  is applied as a post-process on the final bone rotation)
    }

private:
    FABRIKSolver solver_;
};

} // namespace gws::animation
```

---

# 9. Open World Chunk Streaming

```cpp
// engine/streaming/world_streamer.h
#pragma once
#include <unordered_map>
#include <queue>
#include <mutex>
#include <future>
#include <functional>
#include <string>
#include "engine/core/math/math.h"

namespace gws::streaming {

// -------------------------------------------------------------------
// Cell coordinate (integer grid)
// -------------------------------------------------------------------

struct CellCoord {
    int32_t x, z;
    bool operator==(const CellCoord& o) const { return x == o.x && z == o.z; }
};

struct CellCoordHash {
    size_t operator()(const CellCoord& c) const {
        return std::hash<int64_t>()((int64_t)c.x << 32 | (uint32_t)c.z);
    }
};

// -------------------------------------------------------------------
// Cell state machine
// DORMANT → QUEUED_LOAD → LOADING → ACTIVE → QUEUED_UNLOAD → UNLOADING → DORMANT
// -------------------------------------------------------------------

enum class CellState {
    Dormant,
    QueuedLoad,
    Loading,
    Active,
    QueuedUnload,
    Unloading,
};

struct CellData {
    CellState state = CellState::Dormant;
    // In production: list of entity handles created by this cell
    // std::vector<entt::entity> entities;
    std::future<bool> load_future;   // async load result
    float             load_priority; // distance to player (lower = more urgent)
};

// -------------------------------------------------------------------
// WorldStreamer
// -------------------------------------------------------------------

class WorldStreamer {
public:
    struct Config {
        float cell_size      = 256.0f;   // world units per cell side
        int   load_radius    = 4;        // cells within this radius are loaded
        int   unload_radius  = 5;        // cells beyond this are unloaded (hysteresis)
        int   max_loads_per_frame = 2;   // activations per main-thread frame
        // Velocity prediction: load cells ahead of player
        float prediction_time = 3.0f;   // seconds of velocity extrapolation
    };

    using LoadFn   = std::function<bool(CellCoord)>;   // called on background thread
    using UnloadFn = std::function<void(CellCoord)>;   // called on main thread

    WorldStreamer(Config cfg, LoadFn load_fn, UnloadFn unload_fn)
        : cfg_(cfg), load_fn_(load_fn), unload_fn_(unload_fn) {}

    // Called every frame on main thread.
    void update(const gws::math::Vec3& player_pos, const gws::math::Vec3& player_vel) {
        // Extrapolate player position to predict which cells to prioritize
        gws::math::Vec3 predicted_pos =
            player_pos + player_vel * cfg_.prediction_time;

        CellCoord player_cell  = world_to_cell(player_pos);
        CellCoord predict_cell = world_to_cell(predicted_pos);

        // Determine desired active set
        std::vector<CellCoord> desired;
        for (int dz = -cfg_.load_radius; dz <= cfg_.load_radius; ++dz) {
            for (int dx = -cfg_.load_radius; dx <= cfg_.load_radius; ++dx) {
                desired.push_back({ player_cell.x + dx, player_cell.z + dz });
            }
        }

        // Enqueue new cells for loading
        for (const auto& coord : desired) {
            auto it = cells_.find(coord);
            if (it == cells_.end() || it->second.state == CellState::Dormant) {
                float dist = cell_distance(coord, player_cell);
                enqueue_load(coord, dist);
            }
        }

        // Enqueue distant cells for unloading
        for (auto& [coord, data] : cells_) {
            if (data.state != CellState::Active) continue;
            float dist = cell_distance(coord, player_cell);
            if (dist > cfg_.unload_radius) {
                data.state = CellState::QueuedUnload;
            }
        }

        // Process completed async loads (main thread activation)
        int activations = 0;
        for (auto& [coord, data] : cells_) {
            if (data.state != CellState::Loading) continue;
            if (!data.load_future.valid()) continue;

            // Check if async load is complete (non-blocking)
            if (data.load_future.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready)
            {
                bool success = data.load_future.get();
                data.state   = success ? CellState::Active : CellState::Dormant;
                ++activations;
                if (activations >= cfg_.max_loads_per_frame) break;
            }
        }

        // Process unloads
        for (auto& [coord, data] : cells_) {
            if (data.state != CellState::QueuedUnload) continue;
            data.state = CellState::Dormant;
            unload_fn_(coord);
        }

        // Clean up dormant cells from the map (free memory)
        for (auto it = cells_.begin(); it != cells_.end(); ) {
            if (it->second.state == CellState::Dormant)
                it = cells_.erase(it);
            else
                ++it;
        }
    }

private:
    CellCoord world_to_cell(const gws::math::Vec3& pos) const {
        return { (int32_t)std::floor(pos.x / cfg_.cell_size),
                 (int32_t)std::floor(pos.z / cfg_.cell_size) };
    }

    float cell_distance(const CellCoord& a, const CellCoord& b) const {
        float dx = (float)(a.x - b.x);
        float dz = (float)(a.z - b.z);
        return std::sqrt(dx*dx + dz*dz);
    }

    void enqueue_load(const CellCoord& coord, float priority) {
        auto& data   = cells_[coord];
        data.state   = CellState::Loading;
        data.load_priority = priority;
        // Launch async load on thread pool
        data.load_future = std::async(std::launch::async,
                                       [this, coord]() { return load_fn_(coord); });
    }

    Config   cfg_;
    LoadFn   load_fn_;
    UnloadFn unload_fn_;

    std::unordered_map<CellCoord, CellData, CellCoordHash> cells_;
};

} // namespace gws::streaming
```

---

# 10. Visual Script VM

```cpp
// engine/scripting/visual/vs_vm.h
#pragma once
#include <vector>
#include <variant>
#include <string>
#include <unordered_map>
#include <functional>
#include <cassert>
#include <cstdint>

namespace gws::scripting::vs {

// -------------------------------------------------------------------
// Stack value: can hold any primitive type or entity reference
// -------------------------------------------------------------------

using StackValue = std::variant<
    std::monostate,     // null / void
    bool,
    int32_t,
    float,
    std::string,
    uint32_t            // entity ID
>;

// -------------------------------------------------------------------
// Bytecode opcodes
// -------------------------------------------------------------------

enum class Opcode : uint8_t {
    PUSH_NULL,           // push null
    PUSH_BOOL,           // push bool literal (1 byte follows)
    PUSH_INT,            // push int literal (4 bytes follow)
    PUSH_FLOAT,          // push float literal (4 bytes follow)
    PUSH_STRING,         // push string literal (uint16 length + bytes follow)
    LOAD_VAR,            // load local variable (1 byte index follows)
    STORE_VAR,           // store to local variable (1 byte index follows)
    LOAD_BB,             // load blackboard key (string on stack → value on stack)
    STORE_BB,            // store blackboard: key+value on stack
    CALL_NATIVE,         // call native function (uint16 function ID follows)
    BRANCH,              // if top-of-stack is false, jump (int16 offset follows)
    JUMP,                // unconditional jump (int16 offset follows)
    RETURN,              // end execution, push return value
    WAIT,                // coroutine yield until condition (uint8 condition type follows)
    ADD, SUB, MUL, DIV, // arithmetic (pop 2, push result)
    NOT, AND, OR,        // boolean
    CMP_EQ, CMP_LT, CMP_GT, // comparison (pop 2, push bool)
};

// -------------------------------------------------------------------
// Execution context (passed to native functions)
// -------------------------------------------------------------------

class Blackboard; // forward from ai module

struct VMContext {
    uint32_t   entity_id;
    Blackboard* blackboard;
    float      dt;
    float      time;
};

// -------------------------------------------------------------------
// Native function registry
// A native function pops its arguments from the stack and pushes a result.
// -------------------------------------------------------------------

using NativeFn = std::function<StackValue(
    std::vector<StackValue>& stack, VMContext& ctx)>;

class NativeRegistry {
public:
    static NativeRegistry& get() { static NativeRegistry r; return r; }

    uint16_t register_fn(const std::string& name, NativeFn fn) {
        uint16_t id = (uint16_t)functions_.size();
        id_map_[name] = id;
        functions_.push_back(std::move(fn));
        return id;
    }

    const NativeFn& get_fn(uint16_t id) const { return functions_[id]; }
    uint16_t        get_id(const std::string& name) const { return id_map_.at(name); }

private:
    std::vector<NativeFn>                  functions_;
    std::unordered_map<std::string, uint16_t> id_map_;
};

// -------------------------------------------------------------------
// VM: simple stack-based interpreter
// -------------------------------------------------------------------

struct VMCoroutineState {
    size_t                 program_counter = 0;
    std::vector<StackValue> locals;          // local variables
    std::vector<StackValue> stack;
    float                  resume_time = 0.0f; // for WAIT
    bool                   waiting     = false;
};

class VM {
public:
    // Execute a bytecode program. If it returns without suspending,
    // coroutine_state is cleared.
    // Returns true if execution completed, false if suspended (WAIT).
    bool execute(const std::vector<uint8_t>& bytecode,
                 VMCoroutineState&           state,
                 VMContext&                  ctx)
    {
        auto& pc    = state.program_counter;
        auto& stack = state.stack;
        auto& local = state.locals;

        // Resume from suspension
        if (state.waiting) {
            if (ctx.time < state.resume_time) return false;
            state.waiting = false;
        }

        while (pc < bytecode.size()) {
            Opcode op = (Opcode)bytecode[pc++];

            switch (op) {
            case Opcode::PUSH_NULL:
                stack.push_back(std::monostate{});
                break;

            case Opcode::PUSH_BOOL: {
                bool v = bytecode[pc++] != 0;
                stack.push_back(v);
                break;
            }

            case Opcode::PUSH_INT: {
                int32_t v;
                memcpy(&v, &bytecode[pc], 4); pc += 4;
                stack.push_back(v);
                break;
            }

            case Opcode::PUSH_FLOAT: {
                float v;
                memcpy(&v, &bytecode[pc], 4); pc += 4;
                stack.push_back(v);
                break;
            }

            case Opcode::LOAD_VAR: {
                uint8_t idx = bytecode[pc++];
                stack.push_back(idx < local.size() ? local[idx] : std::monostate{});
                break;
            }

            case Opcode::STORE_VAR: {
                uint8_t idx = bytecode[pc++];
                if (idx >= local.size()) local.resize(idx + 1);
                local[idx] = stack.back(); stack.pop_back();
                break;
            }

            case Opcode::CALL_NATIVE: {
                uint16_t fn_id;
                memcpy(&fn_id, &bytecode[pc], 2); pc += 2;
                StackValue result =
                    NativeRegistry::get().get_fn(fn_id)(stack, ctx);
                stack.push_back(result);
                break;
            }

            case Opcode::BRANCH: {
                int16_t offset;
                memcpy(&offset, &bytecode[pc], 2); pc += 2;
                bool cond = false;
                if (!stack.empty()) {
                    cond = std::get_if<bool>(&stack.back()) ?
                           *std::get_if<bool>(&stack.back()) : false;
                    stack.pop_back();
                }
                if (!cond) pc += offset;
                break;
            }

            case Opcode::JUMP: {
                int16_t offset;
                memcpy(&offset, &bytecode[pc], 2); pc += 2;
                pc += offset;
                break;
            }

            case Opcode::WAIT: {
                uint8_t cond_type = bytecode[pc++];
                // cond_type 0 = timed wait (float on stack = seconds)
                if (cond_type == 0 && !stack.empty()) {
                    float duration = *std::get_if<float>(&stack.back());
                    stack.pop_back();
                    state.resume_time = ctx.time + duration;
                    state.waiting     = true;
                    return false; // suspend execution
                }
                break;
            }

            case Opcode::ADD: {
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                if (auto* fa = std::get_if<float>(&a))
                    stack.push_back(*fa + std::get<float>(b));
                else if (auto* ia = std::get_if<int32_t>(&a))
                    stack.push_back(*ia + std::get<int32_t>(b));
                break;
            }

            case Opcode::CMP_LT: {
                auto b = stack.back(); stack.pop_back();
                auto a = stack.back(); stack.pop_back();
                if (auto* fa = std::get_if<float>(&a))
                    stack.push_back(*fa < std::get<float>(b));
                else if (auto* ia = std::get_if<int32_t>(&a))
                    stack.push_back(*ia < std::get<int32_t>(b));
                break;
            }

            case Opcode::RETURN:
                return true;

            default:
                assert(false && "Unknown opcode");
                return true;
            }
        }
        return true;
    }
};

// -------------------------------------------------------------------
// Example: Register engine functions for use in visual scripts
// -------------------------------------------------------------------

inline void register_engine_functions() {
    auto& reg = NativeRegistry::get();

    // get_health(entity_id) → float
    reg.register_fn("get_health", [](std::vector<StackValue>& stack, VMContext& ctx) -> StackValue {
        // uint32_t entity_id = std::get<uint32_t>(stack.back()); stack.pop_back();
        // return World::get().get_component<HealthComponent>(entity_id).current;
        return 100.0f; // placeholder
    });

    // apply_damage(entity_id, amount) → void
    reg.register_fn("apply_damage", [](std::vector<StackValue>& stack, VMContext& ctx) -> StackValue {
        float amount = std::get<float>(stack.back()); stack.pop_back();
        // uint32_t target = std::get<uint32_t>(stack.back()); stack.pop_back();
        // CombatSystem::apply_damage(target, amount);
        return std::monostate{};
    });

    // play_vfx(vfx_id, position) → void
    reg.register_fn("play_vfx", [](std::vector<StackValue>& stack, VMContext& ctx) -> StackValue {
        return std::monostate{};
    });
}

} // namespace gws::scripting::vs
```

---

# 11. Stat & Modifier System

```cpp
// engine/game_systems/stats/stat_component.h
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <functional>

namespace gws::game {

// -------------------------------------------------------------------
// Stat types — all stats the game tracks.
// -------------------------------------------------------------------

enum class StatType : uint8_t {
    Health, HealthMax,
    Stamina, StaminaMax,
    Mana, ManaMax,
    Essence, EssenceMax,     // For Solorig cost
    Strength, Agility, Intelligence, Resilience, Speed,
    AttackDamage, AttackSpeed, CritChance, CritMultiplier,
    ArmorRating, ElementalResist,
    AbilityCooldownReduction, AbilityPower,
    MovementSpeed,
    COUNT
};

// -------------------------------------------------------------------
// Modifier: a single stat modification from an item, buff, or ability.
// Stacks correctly: Flat → PercentAdd → PercentMultiply.
// Formula: (Base + ΣFlat) × (1 + ΣPercentAdd) × Π(PercentMultiply)
// -------------------------------------------------------------------

enum class ModifierType : uint8_t {
    Flat,            // +5 damage
    PercentAdd,      // +10% → stacks additively with other PercentAdds
    PercentMultiply, // ×1.2 → stacks multiplicatively
};

struct StatModifier {
    StatType     stat;
    ModifierType type;
    float        value;
    uint32_t     source_id;   // who applied this (item ID, ability ID, etc.)
    float        duration;    // -1 = permanent, >0 = expires after this many seconds
    float        applied_at;  // game time when applied (for duration check)
};

// -------------------------------------------------------------------
// StatComponent: holds base values and modifier list.
// Final values are lazy-computed and cached.
// -------------------------------------------------------------------

class StatComponent {
public:
    StatComponent() {
        base_values_.fill(0.0f);
        cached_values_.fill(0.0f);
        dirty_.fill(true);
    }

    void set_base(StatType stat, float value) {
        base_values_[(int)stat] = value;
        dirty_[(int)stat] = true;
    }

    float get(StatType stat) {
        if (dirty_[(int)stat]) recompute(stat);
        return cached_values_[(int)stat];
    }

    // Add a modifier. Marks the affected stat dirty.
    void add_modifier(const StatModifier& mod) {
        modifiers_.push_back(mod);
        dirty_[(int)mod.stat] = true;
    }

    // Remove modifiers from a specific source (e.g., item unequipped).
    void remove_from_source(uint32_t source_id) {
        auto it = modifiers_.begin();
        while (it != modifiers_.end()) {
            if (it->source_id == source_id) {
                dirty_[(int)it->stat] = true;
                it = modifiers_.erase(it);
            } else ++it;
        }
    }

    // Called each frame to expire duration-based modifiers.
    void tick(float current_time) {
        bool changed = false;
        auto it = modifiers_.begin();
        while (it != modifiers_.end()) {
            if (it->duration > 0.0f &&
                current_time - it->applied_at >= it->duration)
            {
                dirty_[(int)it->stat] = true;
                it = modifiers_.erase(it);
                changed = true;
            } else ++it;
        }
        // Clamp current health/stamina/mana to their max values
        if (changed || dirty_[(int)StatType::HealthMax]) {
            cached_values_[(int)StatType::Health] = std::min(
                get(StatType::Health), get(StatType::HealthMax));
        }
    }

private:
    void recompute(StatType stat) {
        float base = base_values_[(int)stat];
        float flat = 0.0f;
        float percent_add = 0.0f;
        float percent_mul = 1.0f;

        for (const auto& mod : modifiers_) {
            if (mod.stat != stat) continue;
            switch (mod.type) {
            case ModifierType::Flat:            flat        += mod.value; break;
            case ModifierType::PercentAdd:      percent_add += mod.value; break;
            case ModifierType::PercentMultiply: percent_mul *= mod.value; break;
            }
        }

        cached_values_[(int)stat] = (base + flat) * (1.0f + percent_add) * percent_mul;
        dirty_[(int)stat] = false;
    }

    std::array<float, (int)StatType::COUNT> base_values_;
    std::array<float, (int)StatType::COUNT> cached_values_;
    std::array<bool,  (int)StatType::COUNT> dirty_;
    std::vector<StatModifier>               modifiers_;
};

} // namespace gws::game
```

---

# 12. Combat — Frame Data & Hitboxes

```cpp
// engine/game_systems/combat/attack_asset.h
#pragma once
#include "engine/core/math/math.h"
#include <vector>
#include <cstdint>

namespace gws::game::combat {

// -------------------------------------------------------------------
// A hitbox: a capsule attached to a bone, active during specific frames.
// -------------------------------------------------------------------

struct HitboxData {
    std::string bone_name;    // bone to attach to
    float       offset_y;    // local offset along bone
    float       radius;      // capsule radius
    float       half_height; // capsule half-height
    float       damage_multiplier = 1.0f;
};

// -------------------------------------------------------------------
// AttackAsset: defines the frame data for a single attack.
// Startup → Active → Recovery. Combo window for chaining.
// -------------------------------------------------------------------

struct AttackAsset {
    std::string clip_name;         // animation clip to play

    int startup_frames;            // frames before hitbox activates
    int active_frames;             // frames hitbox is active
    int recovery_frames;           // frames after active, can't cancel

    int combo_window_start;        // frame when combo input is accepted
    int combo_window_end;          // frame when combo window closes

    float poise_damage;            // applied to defender on hit
    float hitstop_frames;          // freeze both attacker and defender N frames on hit

    std::vector<HitboxData> hitboxes; // one or more hitboxes during active frames

    // Can this attack be cancelled into dodge at any time during startup?
    bool cancel_to_dodge = true;
    // Can we cancel into another ability during recovery?
    bool cancel_to_ability = false;
};

// -------------------------------------------------------------------
// FrameDataProcessor: drives the attack state machine.
// -------------------------------------------------------------------

class FrameDataProcessor {
public:
    enum class AttackPhase { Idle, Startup, Active, Recovery };

    void start_attack(const AttackAsset* attack) {
        current_ = attack;
        frame_   = 0;
        phase_   = AttackPhase::Startup;
        hit_entities_.clear();
    }

    // Called each game tick (not render frame!) at fixed 60Hz.
    // Returns: list of hitboxes currently active (world-space capsules)
    struct ActiveHitbox {
        gws::math::Vec3 base;   // capsule base world position
        gws::math::Vec3 tip;    // capsule tip world position
        float           radius;
        float           damage_mul;
    };

    void tick(const gws::math::Transform& attacker_transform,
              /* skeleton pose would go here */ float dt_frames)
    {
        if (!current_) return;

        frame_++;

        if (phase_ == AttackPhase::Startup) {
            if (frame_ >= current_->startup_frames) {
                phase_ = AttackPhase::Active;
                frame_ = 0;
            }
        } else if (phase_ == AttackPhase::Active) {
            if (frame_ >= current_->active_frames) {
                phase_ = AttackPhase::Recovery;
                frame_ = 0;
            }
        } else if (phase_ == AttackPhase::Recovery) {
            if (frame_ >= current_->recovery_frames) {
                phase_   = AttackPhase::Idle;
                current_ = nullptr;
                frame_   = 0;
            }
        }
    }

    bool is_active()       const { return phase_ == AttackPhase::Active; }
    bool in_combo_window() const {
        return current_ &&
               frame_ >= current_->combo_window_start &&
               frame_ <= current_->combo_window_end;
    }
    bool can_dodge_cancel() const {
        return current_ && current_->cancel_to_dodge &&
               phase_ == AttackPhase::Startup;
    }

    // Mark an entity as already hit this swing (prevent multi-hit per swing)
    bool try_register_hit(uint32_t entity_id) {
        for (uint32_t e : hit_entities_) if (e == entity_id) return false;
        hit_entities_.push_back(entity_id);
        return true;
    }

    AttackPhase phase() const { return phase_; }

private:
    const AttackAsset* current_ = nullptr;
    int                frame_   = 0;
    AttackPhase        phase_   = AttackPhase::Idle;
    std::vector<uint32_t> hit_entities_; // cleared each new attack
};

// -------------------------------------------------------------------
// Poise system: track poise and stagger threshold
// -------------------------------------------------------------------

struct PoiseComponent {
    float current;
    float max;
    float regen_rate;    // per second
    float stagger_threshold; // if current < 0: trigger stagger

    // Receive poise damage. Returns true if staggered.
    bool apply_poise_damage(float amount) {
        current -= amount;
        return current < 0.0f;
    }

    void tick(float dt) {
        current = std::min(max, current + regen_rate * dt);
    }
};

} // namespace gws::game::combat
```

---

# 13. Ability System

```cpp
// engine/game_systems/abilities/ability_system.h
#pragma once
#include "engine/game_systems/stats/stat_component.h"
#include <vector>
#include <functional>
#include <memory>

namespace gws::game::abilities {

// -------------------------------------------------------------------
// Effect: what an ability does when it triggers.
// Effects are data-driven — defined in JSON, referenced by ID.
// -------------------------------------------------------------------

enum class EffectType : uint8_t {
    Damage,         // deal damage to a target
    Heal,           // restore health
    ApplyModifier,  // add a stat modifier (buff/debuff)
    SpawnProjectile,
    PlayVFX,
    PlayAudio,
    GrantSigil,     // game-specific: grant a Sigil to target
    TitheEssence,   // Solorig: drain Essence and tithe to owner
};

struct AbilityEffect {
    EffectType type;
    float      value;
    float      duration = -1.0f; // for modifier effects
    uint32_t   vfx_id   = 0;
};

// -------------------------------------------------------------------
// ArcaniType: determines cost model and visual theme.
// (Maps directly to the lore's Arcani classification.)
// -------------------------------------------------------------------

enum class ArcaniType : uint8_t {
    Arcanum,    // uses Mana
    Eldritch,   // uses Essence + external energy
    Solorig,    // drains Health over time, tithes to owner
    Agioglossa, // divine, uses Faith resource
    Skothavma,  // forbidden, costs Sanity (game mechanic)
    Runik,      // sigil-based, no cost but cooldown
};

// -------------------------------------------------------------------
// AbilityAsset: the data definition of one ability.
// -------------------------------------------------------------------

struct AbilityAsset {
    std::string name;
    ArcaniType  arcani_type;

    float cooldown;           // seconds
    float cast_time;          // 0 = instant
    float range;              // 0 = self-cast

    float mana_cost   = 0.0f;
    float essence_cost = 0.0f;
    float health_cost  = 0.0f;  // for Solorig

    std::vector<AbilityEffect> on_cast_effects;   // applied immediately
    std::vector<AbilityEffect> on_hit_effects;    // applied on each hit

    // Aspect modifiers (from the equipped Aspect slots in the lore design)
    // These are applied via StatModifiers to the ability's parameters.
    // e.g., an Aspect might add +1 projectile or +30% damage.
};

// -------------------------------------------------------------------
// CooldownState: runtime state for one ability slot.
// -------------------------------------------------------------------

struct CooldownState {
    float  remaining  = 0.0f;
    int    charges    = 1;
    int    max_charges = 1;
    bool   is_active   = false;
    float  active_time = 0.0f; // how long the ability has been active
};

// -------------------------------------------------------------------
// AbilitySlotComponent: the 4 slots on a player/enemy.
// -------------------------------------------------------------------

struct AbilitySlotComponent {
    static constexpr int SLOT_COUNT = 4; // 3 standard + 1 ultimate

    std::shared_ptr<AbilityAsset> slots[SLOT_COUNT];
    CooldownState                 cooldowns[SLOT_COUNT];
};

// -------------------------------------------------------------------
// AbilitySystem: processes activations and ticks active abilities.
// -------------------------------------------------------------------

class AbilitySystem {
public:
    // Try to activate an ability slot. Returns false if on cooldown or no resources.
    bool try_activate(uint32_t entity_id,
                      AbilitySlotComponent& slot_comp,
                      StatComponent&        stats,
                      int                   slot_index,
                      float                 current_time)
    {
        if (slot_index < 0 || slot_index >= AbilitySlotComponent::SLOT_COUNT) return false;
        const auto& ability = slot_comp.slots[slot_index];
        if (!ability) return false;

        CooldownState& cd = slot_comp.cooldowns[slot_index];
        if (cd.remaining > 0.0f) return false;

        // Check resource costs
        if (stats.get(StatType::Mana)    < ability->mana_cost)    return false;
        if (stats.get(StatType::Essence) < ability->essence_cost) return false;
        if (stats.get(StatType::Health)  < ability->health_cost)  return false;

        // Deduct costs
        stats.add_modifier({ StatType::Mana,    ModifierType::Flat, -ability->mana_cost,    entity_id, -1.0f });
        stats.add_modifier({ StatType::Essence, ModifierType::Flat, -ability->essence_cost, entity_id, -1.0f });

        // Special case: Solorig — setup a draining effect
        if (ability->arcani_type == ArcaniType::Solorig) {
            // Create a recurring health drain modifier
            pending_solorig_drains_.push_back({
                entity_id, ability->health_cost, current_time
            });
        }

        // Start cooldown
        cd.remaining = ability->cooldown;
        cd.is_active = true;
        cd.active_time = 0.0f;

        // Apply immediate on_cast effects
        for (const auto& effect : ability->on_cast_effects) {
            apply_effect(entity_id, entity_id, effect);
        }

        return true;
    }

    void tick(float dt) {
        // Tick all entity cooldowns (in production: iterate ECS view)
        // ... (omitted — would iterate AbilitySlotComponent pool)

        // Process Solorig health drains
        for (auto it = pending_solorig_drains_.begin();
             it != pending_solorig_drains_.end(); )
        {
            it->elapsed += dt;
            // Drain logic...
            ++it;
        }
    }

private:
    void apply_effect(uint32_t caster, uint32_t target, const AbilityEffect& effect) {
        switch (effect.type) {
        case EffectType::Damage:
            // CombatSystem::apply_damage(target, effect.value * caster_power);
            break;
        case EffectType::Heal:
            // stats.add_modifier(Flat heal)
            break;
        case EffectType::ApplyModifier:
            // Add stat modifier with duration
            break;
        default: break;
        }
    }

    struct SolorigDrain {
        uint32_t entity_id;
        float    drain_per_second;
        float    elapsed;
    };
    std::vector<SolorigDrain> pending_solorig_drains_;
};

} // namespace gws::game::abilities
```

---

# 14. C# Scripting Host

```cpp
// engine/scripting/csharp/dotnet_host.h
#pragma once
// Uses the .NET 8 hosting API (hostfxr) to embed CoreCLR.
// Reference: https://github.com/dotnet/runtime/blob/main/docs/design/features/native-hosting.md

#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#define HOSTFXR_LIB "hostfxr.dll"
#else
#include <dlfcn.h>
#define HOSTFXR_LIB "libhostfxr.so"
#endif

// hostfxr function pointer types (simplified)
typedef int (*hostfxr_initialize_fn)(const char* host_path, void* params, void** handle);
typedef int (*hostfxr_get_delegate_fn)(void* handle, int type, void** delegate);
typedef int (*hostfxr_close_fn)(void* handle);

namespace gws::scripting::csharp {

// Function pointer type for calling into C# managed code
using ManagedFn = void*(*)(const char* assembly, const char* type,
                             const char* method, const char* delegate_type,
                             void* load_ctx, void** delegate);

class DotNetHost {
public:
    // Initialize the .NET runtime. Call once at engine startup.
    bool initialize(const std::string& runtime_config_path) {
        // 1. Load hostfxr shared library
#ifdef _WIN32
        hostfxr_lib_ = LoadLibraryA(HOSTFXR_LIB);
        if (!hostfxr_lib_) return false;
        auto init_fn = (hostfxr_initialize_fn)GetProcAddress((HMODULE)hostfxr_lib_,
            "hostfxr_initialize_for_runtime_config");
        auto get_delegate_fn = (hostfxr_get_delegate_fn)GetProcAddress((HMODULE)hostfxr_lib_,
            "hostfxr_get_runtime_delegate");
        close_fn_ = (hostfxr_close_fn)GetProcAddress((HMODULE)hostfxr_lib_,
            "hostfxr_close");
#else
        hostfxr_lib_ = dlopen(HOSTFXR_LIB, RTLD_LAZY);
        if (!hostfxr_lib_) return false;
        auto init_fn = (hostfxr_initialize_fn)dlsym(hostfxr_lib_,
            "hostfxr_initialize_for_runtime_config");
        auto get_delegate_fn = (hostfxr_get_delegate_fn)dlsym(hostfxr_lib_,
            "hostfxr_get_runtime_delegate");
        close_fn_ = (hostfxr_close_fn)dlsym(hostfxr_lib_, "hostfxr_close");
#endif

        if (!init_fn || !get_delegate_fn || !close_fn_) return false;

        // 2. Initialize runtime from .runtimeconfig.json
        void* handle = nullptr;
        int rc = init_fn(runtime_config_path.c_str(), nullptr, &handle);
        if (rc != 0 || !handle) return false;

        // 3. Get the load_assembly_and_get_function_pointer delegate
        // Type: hdt_load_assembly_and_get_function_pointer = 5
        rc = get_delegate_fn(handle, 5 /*hdt_load_assembly_and_get_function_pointer*/,
                              (void**)&load_fn_);
        close_fn_(handle);

        return rc == 0 && load_fn_ != nullptr;
    }

    // Load a C# method from an assembly and return a function pointer.
    // The C# method must be static and match the signature of the delegate type.
    template<typename TDelegate>
    TDelegate get_managed_function(const std::string& assembly_path,
                                   const std::string& type_name,     // "Namespace.ClassName, Assembly"
                                   const std::string& method_name,
                                   const std::string& delegate_type) // "Namespace.DelegateType, Assembly"
    {
        void* fn = nullptr;
        int rc = load_fn_(
            assembly_path.c_str(),
            type_name.c_str(),
            method_name.c_str(),
            delegate_type.c_str(),
            nullptr,
            &fn);
        return (rc == 0) ? reinterpret_cast<TDelegate>(fn) : nullptr;
    }

    void shutdown() {
        if (hostfxr_lib_) {
#ifdef _WIN32
            FreeLibrary((HMODULE)hostfxr_lib_);
#else
            dlclose(hostfxr_lib_);
#endif
            hostfxr_lib_ = nullptr;
        }
    }

private:
    void*            hostfxr_lib_ = nullptr;
    ManagedFn        load_fn_     = nullptr;
    hostfxr_close_fn close_fn_    = nullptr;
};

} // namespace gws::scripting::csharp
```

### Corresponding C# Component Attribute System

```csharp
// GameWorldshaper/Scripts/Runtime/ScriptComponent.cs
using System;

namespace GWS.Runtime
{
    // Attribute: marks a class as an engine component (inspectable, attachable to entities)
    [AttributeUsage(AttributeTargets.Class)]
    public class ComponentAttribute : Attribute
    {
        public string DisplayName { get; }
        public ComponentAttribute(string displayName = "") => DisplayName = displayName;
    }

    // Attribute: marks a field as visible in the engine Inspector panel
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
    public class InspectAttribute : Attribute
    {
        public string Label    { get; }
        public float  Min      { get; }
        public float  Max      { get; }
        public InspectAttribute(string label = "", float min = float.MinValue,
                                float max = float.MaxValue)
        {
            Label = label; Min = min; Max = max;
        }
    }

    // Base class for all C# components attached to entities
    public abstract class ScriptComponent
    {
        public uint EntityId { get; internal set; }

        // Lifecycle methods — override as needed
        public virtual void OnStart()          {}
        public virtual void OnUpdate(float dt) {}
        public virtual void OnDestroy()        {}

        // Engine API: access other components on the same entity
        public T GetComponent<T>() where T : class
            => EngineAPI.GetComponent<T>(EntityId);

        public bool HasComponent<T>() where T : class
            => EngineAPI.HasComponent<T>(EntityId);
    }

    // Example: A C# component that implements Solorig behavior
    [Component("Solorig Controller")]
    public class SolorigController : ScriptComponent
    {
        [Inspect("Drain Rate (health/sec)", min: 0.1f, max: 50.0f)]
        public float drain_rate = 5.0f;

        [Inspect("Tithe Recipient Entity ID")]
        public uint tithe_target_entity = 0;

        private float accumulated_tithe = 0.0f;

        public override void OnUpdate(float dt)
        {
            // Drain health from self
            var stats = GetComponent<StatComponentProxy>();
            if (stats == null) return;

            float drain = drain_rate * dt;
            stats.ApplyDamage(EntityId, drain);
            accumulated_tithe += drain;

            // Transfer accumulated essence to tithe target
            if (tithe_target_entity != 0 && accumulated_tithe >= 1.0f)
            {
                stats.RestoreEssence(tithe_target_entity, accumulated_tithe);
                accumulated_tithe = 0.0f;
            }
        }
    }
}
```

---

*These code examples form the reference implementation layer for the engine.
Each example is architecturally correct but simplified for readability —
production implementations will add error handling, SIMD optimization,
thread safety, and full platform abstractions on top of these foundations.*
