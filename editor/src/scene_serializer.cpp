#include "scene_serializer.h"
#include "entity.h"
#include "transform.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <iostream>
#include <glm/glm.hpp>

namespace schizo::editor {

// Simple JSON-like serialization (for MVP, without external JSON library)
// Format: Simple line-based key-value pairs

bool SceneSerializer::SaveScene(const std::string& filepath, const std::shared_ptr<schizo::scene::Scene>& scene) {
    if (!scene) {
        spdlog::error("Cannot save null scene");
        return false;
    }
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open file for writing: {}", filepath);
        return false;
    }
    
    try {
        // Write scene header
        file << "SCENE_NAME=" << scene->GetName() << "\n";
        file << "ENTITY_COUNT=" << scene->GetEntityCount() << "\n";
        file << "\n";
        
        // Save each entity
        const auto& entities = scene->GetEntities();
        for (size_t i = 0; i < entities.size(); ++i) {
            const auto& entity = entities[i];
            if (!entity) continue;
            
            file << "[ENTITY_" << i << "]\n";
            file << "NAME=" << entity->GetName() << "\n";
            file << "ID=" << entity->GetId() << "\n";
            file << "ACTIVE=" << (entity->IsActive() ? "1" : "0") << "\n";
            file << "TAG=" << entity->GetTag() << "\n";
            
            // Save transform
            auto transform = entity->GetTransform();
            if (transform) {
                auto pos = transform->GetLocalPosition();
                auto rot = transform->GetLocalRotation();
                auto scale = transform->GetLocalScale();
                
                file << "TRANSFORM_POS=" << pos.x << "," << pos.y << "," << pos.z << "\n";
                file << "TRANSFORM_ROT=" << rot.x << "," << rot.y << "," << rot.z << "," << rot.w << "\n";
                file << "TRANSFORM_SCALE=" << scale.x << "," << scale.y << "," << scale.z << "\n";
            }
            
            file << "\n";
        }
        
        file.close();
        spdlog::info("Scene saved successfully to: {}", filepath);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception while saving scene: {}", e.what());
        return false;
    }
}

std::shared_ptr<schizo::scene::Scene> SceneSerializer::LoadScene(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Failed to open file for reading: {}", filepath);
        return nullptr;
    }
    
    try {
        std::string scene_name = "Loaded_Scene";
        std::string line;
        
        // Read scene header
        while (std::getline(file, line)) {
            if (line.find("SCENE_NAME=") == 0) {
                scene_name = line.substr(11);  // Skip "SCENE_NAME="
                break;
            }
        }
        
        // Create new scene
        auto scene = std::make_shared<schizo::scene::Scene>(scene_name);
        
        // Read entities
        std::shared_ptr<schizo::scene::Entity> current_entity = nullptr;
        
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;  // Skip empty lines and comments
            
            if (line.find("[ENTITY_") == 0) {
                // Start of new entity - save previous if exists
                if (current_entity) {
                    scene->AddEntity(current_entity);
                }
                current_entity = std::make_shared<schizo::scene::Entity>();
                continue;
            }
            
            if (!current_entity) continue;
            
            // Parse entity properties
            if (line.find("NAME=") == 0) {
                current_entity->SetName(line.substr(5));
            } else if (line.find("ACTIVE=") == 0) {
                current_entity->SetActive(line.substr(7) == "1");
            } else if (line.find("TAG=") == 0) {
                current_entity->SetTag(line.substr(4));
            } else if (line.find("TRANSFORM_POS=") == 0) {
                // Parse position
                std::string values = line.substr(14);
                float x = 0, y = 0, z = 0;
                sscanf(values.c_str(), "%f,%f,%f", &x, &y, &z);
                current_entity->GetTransform()->SetLocalPosition(glm::vec3(x, y, z));
            } else if (line.find("TRANSFORM_ROT=") == 0) {
                // Parse rotation (quaternion)
                std::string values = line.substr(14);
                float x = 0, y = 0, z = 0, w = 1;
                sscanf(values.c_str(), "%f,%f,%f,%f", &x, &y, &z, &w);
                current_entity->GetTransform()->SetLocalRotation(glm::quat(w, x, y, z));
            } else if (line.find("TRANSFORM_SCALE=") == 0) {
                // Parse scale
                std::string values = line.substr(16);
                float x = 1, y = 1, z = 1;
                sscanf(values.c_str(), "%f,%f,%f", &x, &y, &z);
                current_entity->GetTransform()->SetLocalScale(glm::vec3(x, y, z));
            }
        }
        
        // Add last entity
        if (current_entity) {
            scene->AddEntity(current_entity);
        }
        
        file.close();
        scene->Initialize();
        spdlog::info("Scene loaded successfully from: {}", filepath);
        return scene;
    } catch (const std::exception& e) {
        spdlog::error("Exception while loading scene: {}", e.what());
        return nullptr;
    }
}

} // namespace schizo::editor
