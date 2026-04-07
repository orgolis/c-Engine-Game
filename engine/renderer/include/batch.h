#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "mesh.h"
#include "material.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Mesh;
class Material;

// ============================================================================
// Static Batching System
// ============================================================================

/**
 * @struct BatchedMeshInstance
 * @brief Single mesh instance in a batch
 */
struct BatchedMeshInstance {
    glm::mat4 transform = glm::mat4(1.0f);
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    bool visible = true;
    uint32_t id = 0;  // For tracking/updating
};

/**
 * @class StaticBatch
 * @brief Batches multiple meshes for efficient rendering
 * 
 * Combines multiple mesh instances into a single draw call when possible.
 * Meshes must have compatible vertex formats and materials to be batched together.
 * Updates are tracked per-instance for dynamic transforms while keeping the batch intact.
 */
class StaticBatch {
public:
    /**
     * Create a new static batch
     * @param device Graphics device
     * @return Unique pointer to batch
     */
    static std::unique_ptr<StaticBatch> Create(RenderDevice* device);
    
    ~StaticBatch();
    
    StaticBatch(const StaticBatch&) = delete;
    StaticBatch& operator=(const StaticBatch&) = delete;
    StaticBatch(StaticBatch&&) = default;
    StaticBatch& operator=(StaticBatch&&) = default;
    
    /**
     * Initialize the batch
     * @return True if successful
     */
    bool Initialize();
    
    /**
     * Add a mesh instance to the batch
     * @param mesh Mesh to add
     * @param material Material to use
     * @param transform World transform
     * @return Instance ID for later updates
     */
    uint32_t AddInstance(std::shared_ptr<Mesh> mesh,
                        std::shared_ptr<Material> material,
                        const glm::mat4& transform = glm::mat4(1.0f));
    
    /**
     * Remove an instance from the batch
     * @param instance_id ID returned from AddInstance
     * @return True if removed successfully
     */
    bool RemoveInstance(uint32_t instance_id);
    
    /**
     * Update an instance's transform
     * @param instance_id Instance ID
     * @param transform New world transform
     */
    void UpdateInstanceTransform(uint32_t instance_id, const glm::mat4& transform);
    
    /**
     * Set instance visibility
     * @param instance_id Instance ID
     * @param visible Whether to render this instance
     */
    void SetInstanceVisible(uint32_t instance_id, bool visible);
    
    /**
     * Get instance count
     */
    uint32_t GetInstanceCount() const { return instances_.size(); }
    
    /**
     * Get visible instance count
     */
    uint32_t GetVisibleInstanceCount() const;
    
    /**
     * Render the entire batch
     * @param device Graphics device
     */
    void Draw(RenderDevice* device);
    
    /**
     * Render a single instance (breaks batching)
     * @param device Graphics device
     * @param instance_id Instance to render
     */
    void DrawInstance(RenderDevice* device, uint32_t instance_id);
    
    /**
     * Clear all instances
     */
    void Clear();
    
    /**
     * Rebuild batch (call after major changes)
     */
    void Rebuild();
    
    /**
     * Get instance data (for debugging)
     */
    const std::vector<BatchedMeshInstance>& GetInstances() const { return instances_; }
    
    /**
     * Get batch statistics
     */
    struct Stats {
        uint32_t total_instances = 0;
        uint32_t visible_instances = 0;
        uint32_t draw_calls = 0;
        uint32_t triangles_rendered = 0;
    };
    
    Stats GetStats() const;

private:
    StaticBatch(RenderDevice* device);
    
    RenderDevice* device_ = nullptr;
    std::vector<BatchedMeshInstance> instances_;
    uint32_t next_instance_id_ = 0;
    
    /**
     * Group instances by mesh and material for efficient batching
     */
    void GroupInstances();
};

/**
 * @class DynamicBatch
 * @brief Batches frequently changing meshes (skeletal animations, LOD changes)
 * 
 * Similar to StaticBatch but optimized for instances with frequent transforms.
 * Uses indirect rendering for efficient GPU updates.
 */
class DynamicBatch {
public:
    static std::unique_ptr<DynamicBatch> Create(RenderDevice* device, uint32_t max_instances = 10000);
    
    ~DynamicBatch();
    
    DynamicBatch(const DynamicBatch&) = delete;
    DynamicBatch& operator=(const DynamicBatch&) = delete;
    DynamicBatch(DynamicBatch&&) = default;
    DynamicBatch& operator=(DynamicBatch&&) = default;
    
    bool Initialize();
    
    uint32_t AddInstance(std::shared_ptr<Mesh> mesh,
                        std::shared_ptr<Material> material,
                        const glm::mat4& transform = glm::mat4(1.0f));
    
    bool RemoveInstance(uint32_t instance_id);
    
    void UpdateInstanceTransform(uint32_t instance_id, const glm::mat4& transform);
    void SetInstanceVisible(uint32_t instance_id, bool visible);
    
    uint32_t GetInstanceCount() const { return instances_.size(); }
    uint32_t GetVisibleInstanceCount() const;
    
    /**
     * Render using indirect rendering (single draw call for all compatible instances)
     */
    void DrawIndirect(RenderDevice* device);
    
    void Clear();

private:
    DynamicBatch(RenderDevice* device, uint32_t max_instances);
    
    RenderDevice* device_ = nullptr;
    std::vector<BatchedMeshInstance> instances_;
    uint32_t max_instances_ = 0;
    uint32_t next_instance_id_ = 0;
    
    // GPU buffers for indirect rendering
    uint32_t indirect_buffer_ = 0;
    uint32_t instance_data_buffer_ = 0;
    
    void UpdateGPUBuffers();
};

} // namespace schizo::renderer
