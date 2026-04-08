#include "entity.h"
#include "scene.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace schizo::scene {

// ============================================================================
// Entity Implementation
// ============================================================================

uint32_t Entity::next_id_ = 1;

Entity::Entity(const std::string& name) 
    : name_(name), id_(next_id_++) {
    spdlog::debug("Created entity: {} (ID: {})", name_, id_);
}

Entity::~Entity() {
    for (auto& comp : components_) {
        comp->OnDetach();
    }
    components_.clear();
}

void Entity::SetParent(std::shared_ptr<Entity> parent) {
    if (parent_ == parent) {
        return;
    }
    
    // Remove from old parent
    if (parent_) {
        auto it = std::find(parent_->children_.begin(), parent_->children_.end(), shared_from_this());
        if (it != parent_->children_.end()) {
            parent_->children_.erase(it);
        }
    }
    
    // Set new parent
    parent_ = parent;
    
    // Add to new parent's children
    if (parent_) {
        parent_->children_.push_back(shared_from_this());
        transform_.SetParent(parent_->GetTransform());
    } else {
        transform_.SetParent(nullptr);
    }
}

void Entity::AddChild(std::shared_ptr<Entity> child) {
    if (child) {
        child->SetParent(shared_from_this());
    }
}

void Entity::RemoveChild(std::shared_ptr<Entity> child) {
    if (child) {
        child->SetParent(nullptr);
    }
}

std::shared_ptr<Entity> Entity::FindChild(const std::string& name) {
    for (const auto& child : children_) {
        if (child->GetName() == name) {
            return child;
        }
        
        auto found = child->FindChild(name);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

void Entity::Detach() {
    SetParent(nullptr);
}

void Entity::SetActive(bool active) {
    if (is_active_ == active) {
        return;
    }
    
    is_active_ = active;
    
    if (active) {
        for (auto& comp : components_) {
            comp->OnEnable();
        }
    } else {
        for (auto& comp : components_) {
            comp->OnDisable();
        }
    }
    
    // Set all children active/inactive too
    for (auto& child : children_) {
        child->SetActive(active);
    }
}

bool Entity::IsActiveInHierarchy() const {
    if (!is_active_) {
        return false;
    }
    
    if (parent_) {
        return parent_->IsActiveInHierarchy();
    }
    
    return true;
}

void Entity::RemoveComponent(std::shared_ptr<Component> component) {
    auto it = std::find(components_.begin(), components_.end(), component);
    if (it != components_.end()) {
        (*it)->OnDetach();
        components_.erase(it);
    }
}

void Entity::FixedUpdate(float delta_time) {
    if (!IsActiveInHierarchy()) {
        return;
    }
    
    for (auto& comp : components_) {
        comp->OnFixedUpdate(delta_time);
    }
}

void Entity::Update(float delta_time) {
    if (!IsActiveInHierarchy()) {
        return;
    }
    
    for (auto& comp : components_) {
        comp->OnUpdate(delta_time);
    }
}

void Entity::LateUpdate(float delta_time) {
    if (!IsActiveInHierarchy()) {
        return;
    }
    
    for (auto& comp : components_) {
        comp->OnLateUpdate(delta_time);
    }
}

void Entity::Destroy() {
    is_destroyed_ = true;
}

// ============================================================================
// Scene Implementation (partial - main file has full implementation)
// ============================================================================

Scene::Scene(const std::string& name) : name_(name) {
    spdlog::debug("Scene constructor: {}", name_);
}

Scene::~Scene() {
    Shutdown();
    spdlog::debug("Scene destructor: {}", name_);
}

std::shared_ptr<Scene> Scene::Create(const SceneConfig& config) {
    auto scene = std::make_shared<Scene>(config.name);
    scene->config_ = config;
    
    // Octree spatial partitioning disabled for MVP
    
    spdlog::info("Created scene: {}", config.name);
    return scene;
}

std::shared_ptr<Entity> Scene::CreateEntity(const std::string& name) {
    auto entity = std::make_shared<Entity>(name);
    AddEntity(entity);
    return entity;
}

void Scene::AddEntity(std::shared_ptr<Entity> entity) {
    if (!entity) return;
    
    entities_.push_back(entity);
    entities_by_id_[entity->GetId()] = entity;
    
    const auto& tag = entity->GetTag();
    if (!tag.empty()) {
        entities_by_tag_[tag].push_back(entity);
    }
    
    spdlog::debug("Added entity to scene: {} (ID: {})", entity->GetName(), entity->GetId());
}

void Scene::RemoveEntity(std::shared_ptr<Entity> entity) {
    if (!entity) return;
    
    auto it = std::find(entities_.begin(), entities_.end(), entity);
    if (it != entities_.end()) {
        entities_.erase(it);
    }
    
    entities_by_id_.erase(entity->GetId());
    
    const auto& tag = entity->GetTag();
    if (!tag.empty()) {
        auto& tagged = entities_by_tag_[tag];
        auto tag_it = std::find(tagged.begin(), tagged.end(), entity);
        if (tag_it != tagged.end()) {
            tagged.erase(tag_it);
        }
    }
    
    spdlog::debug("Removed entity from scene: {} (ID: {})", entity->GetName(), entity->GetId());
}

std::shared_ptr<Entity> Scene::GetEntityById(uint32_t id) const {
    auto it = entities_by_id_.find(id);
    return it != entities_by_id_.end() ? it->second : nullptr;
}

std::shared_ptr<Entity> Scene::GetEntityByName(const std::string& name) const {
    for (const auto& entity : entities_) {
        if (entity->GetName() == name) {
            return entity;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<Entity>> Scene::GetEntitiesByTag(const std::string& tag) const {
    auto it = entities_by_tag_.find(tag);
    return it != entities_by_tag_.end() ? it->second : std::vector<std::shared_ptr<Entity>>();
}

std::vector<std::shared_ptr<Entity>> Scene::QueryAABB(const glm::vec3& min, const glm::vec3& max) const {
    // Spatial queries disabled for MVP
    return {};
}

std::vector<std::shared_ptr<Entity>> Scene::QuerySphere(const glm::vec3& center, float radius) const {
    // Spatial queries disabled for MVP
    return {};
}

std::vector<std::shared_ptr<Entity>> Scene::QueryFrustum(const glm::mat4& view_proj) const {
    // Simplified frustum culling - would need proper frustum extraction in production
    return entities_;
}

void Scene::Initialize() {
    if (is_initialized_) return;
    
    is_initialized_ = true;
    spdlog::info("Initialized scene: {}", name_);
}

void Scene::Update(float delta_time) {
    // Cap frame time
    delta_time = std::min(delta_time, config_.max_frame_time);
    
    // Accumulate time for fixed updates
    accumulated_time_ += delta_time;
    
    // Perform fixed updates
    while (accumulated_time_ >= config_.fixed_timestep) {
        for (auto& entity : entities_) {
            entity->FixedUpdate(config_.fixed_timestep);
        }
        accumulated_time_ -= config_.fixed_timestep;
    }
    
    // Perform variable updates
    UpdateEntities(delta_time);
    
    // Late updates
    for (auto& entity : entities_) {
        entity->LateUpdate(delta_time);
    }
    
    // Update spatial partitioning
    UpdateSpatialPartitioning();
    
    // Process destroyed entities
    ProcessDestroyedEntities();
}

void Scene::Shutdown() {
    Clear();
    is_initialized_ = false;
    spdlog::info("Shutdown scene: {}", name_);
}

void Scene::Reload() {
    Shutdown();
    Initialize();
}

void Scene::Clear() {
    entities_.clear();
    entities_by_id_.clear();
    entities_by_tag_.clear();
    
    spdlog::info("Cleared scene: {}", name_);
}

void Scene::PrintHierarchy() const {
    spdlog::info("Scene hierarchy for '{}':", name_);
    
    auto print_entity = [this](const auto& print_func, const std::shared_ptr<Entity>& entity, int depth) -> void {
        std::string indent(depth * 2, ' ');
        spdlog::info("{}[{}] {} (Active: {})", indent, entity->GetId(), entity->GetName(), entity->IsActive());
        
        for (const auto& child : entity->GetChildren()) {
            print_func(print_func, child, depth + 1);
        }
    };
    
    // Find root entities (no parent)
    for (const auto& entity : entities_) {
        if (!entity->GetParent()) {
            print_entity(print_entity, entity, 0);
        }
    }
}

Scene::Stats Scene::GetStats() const {
    Stats stats;
    stats.entity_count = static_cast<uint32_t>(entities_.size());
    
    for (const auto& entity : entities_) {
        if (entity->IsActiveInHierarchy()) {
            stats.active_entity_count++;
        }
        stats.component_count += static_cast<uint32_t>(entity->GetComponents().size());
    }
    
    stats.octree_nodes = 0;
    stats.octree_depth = 0;
    
    return stats;
}

void Scene::UpdateSpatialPartitioning() {
    // DISABLED: octree spatial partitioning temporarily disabled
    // if (!octree_) return;
    // octree_->Clear();
    // for (const auto& entity : entities_) {
    //     octree_->Insert(entity);
    // }
}

void Scene::ProcessDestroyedEntities() {
    std::vector<std::shared_ptr<Entity>> to_remove;
    
    for (const auto& entity : entities_) {
        if (entity->IsDestroyed()) {
            to_remove.push_back(entity);
        }
    }
    
    for (const auto& entity : to_remove) {
        RemoveEntity(entity);
    }
}

void Scene::UpdateEntities(float delta_time) {
    for (auto& entity : entities_) {
        entity->Update(delta_time);
    }
}

} // namespace schizo::scene
