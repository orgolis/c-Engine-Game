#include "scene_serializer.h"
#include "entity.h"
#include "transform.h"
#include "light_component.h"
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
            
            // Save light component if present
            auto light_comp = entity->GetComponent<schizo::scene::LightComponent>();
            if (light_comp) {
                // Light type
                int light_type = static_cast<int>(light_comp->GetType());
                file << "LIGHT_TYPE=" << light_type << "\n";
                
                // Light properties
                auto color = light_comp->GetColor();
                file << "LIGHT_COLOR=" << color.r << "," << color.g << "," << color.b << "\n";
                
                file << "LIGHT_INTENSITY=" << light_comp->GetIntensity() << "\n";
                file << "LIGHT_TEMPERATURE=" << light_comp->GetTemperature() << "\n";
                
                // Light-specific properties
                if (light_comp->GetType() != schizo::scene::LightType::Directional) {
                    file << "LIGHT_RANGE=" << light_comp->GetRange() << "\n";
                }
                
                if (light_comp->GetType() == schizo::scene::LightType::Spot) {
                    auto angles = light_comp->GetSpotAngles();
                    file << "LIGHT_SPOT_INNER=" << angles.x << "\n";
                    file << "LIGHT_SPOT_OUTER=" << angles.y << "\n";
                    file << "LIGHT_SPOT_FALLOFF=" << light_comp->GetSpotFalloff() << "\n";
                }
                
                // Shadow properties
                file << "LIGHT_CAST_SHADOW=" << (light_comp->GetCastShadow() ? "1" : "0") << "\n";
                file << "LIGHT_SHADOW_QUALITY=" << static_cast<int>(light_comp->GetShadowQuality()) << "\n";
                
                auto shadow_bias = light_comp->GetShadowBias();
                file << "LIGHT_SHADOW_BIAS=" << shadow_bias.x << "," << shadow_bias.y << "\n";
                
                file << "LIGHT_SHADOW_FILTER=" << light_comp->GetShadowFilterRadius() << "\n";
                
                auto shadow_planes = light_comp->GetShadowPlanes();
                file << "LIGHT_SHADOW_PLANES=" << shadow_planes.x << "," << shadow_planes.y << "\n";
                
                if (light_comp->GetType() == schizo::scene::LightType::Directional) {
                    file << "LIGHT_CASCADE_COUNT=" << light_comp->GetCascadeCount() << "\n";
                }
                
                file << "LIGHT_VOLUMETRIC=" << light_comp->GetVolumetricIntensity() << "\n";
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
        int light_type = -1;
        bool has_light = false;
        
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;  // Skip empty lines and comments
            
            if (line.find("[ENTITY_") == 0) {
                // Start of new entity - save previous if exists and create light if needed
                if (current_entity) {
                    if (has_light && light_type >= 0 && light_type < 3) {
                        current_entity->AddComponent<schizo::scene::LightComponent>(
                            static_cast<schizo::scene::LightType>(light_type),
                            "Light"
                        );
                    }
                    scene->AddEntity(current_entity);
                }
                current_entity = std::make_shared<schizo::scene::Entity>();
                light_type = -1;
                has_light = false;
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
            
            // Parse light component properties
            else if (line.find("LIGHT_TYPE=") == 0) {
                has_light = true;
                light_type = std::stoi(line.substr(11));
            } else if (line.find("LIGHT_COLOR=") == 0) {
                std::string values = line.substr(12);
                float r = 1, g = 1, b = 1;
                sscanf(values.c_str(), "%f,%f,%f", &r, &g, &b);
                if (has_light) {
                    // Will be set after light component is added
                }
            }
        }
        
        // Add last entity
        if (current_entity) {
            if (has_light && light_type >= 0 && light_type < 3) {
                current_entity->AddComponent<schizo::scene::LightComponent>(
                    static_cast<schizo::scene::LightType>(light_type),
                    "Light"
                );
            }
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
