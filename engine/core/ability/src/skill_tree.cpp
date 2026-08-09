#include <cstdint>
#include "skill_tree.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace engine::ability {

void SkillTree::AddNode(const SkillNode& node) {
    nodes_.push_back(node);
}

const SkillNode* SkillTree::GetNode(uint32_t node_id) const {
    auto it = FindNode(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return &(*it);
}

SkillNode* SkillTree::GetNode(uint32_t node_id) {
    auto it = FindNode(node_id);
    if (it == nodes_.end()) {
        return nullptr;
    }
    return &(*it);
}

bool SkillTree::IsNodeUnlocked(uint32_t node_id) const {
    return unlocked_nodes_.find(node_id) != unlocked_nodes_.end();
}

bool SkillTree::UnlockNode(uint32_t node_id, bool check_resources) {
    // Check if already unlocked
    if (IsNodeUnlocked(node_id)) {
        return false;
    }

    // Check if can unlock
    std::string error = CanUnlockNode(node_id);
    if (!error.empty()) {
        auto logger = spdlog::get("engine");
        if (logger) logger->debug("Cannot unlock node {}: {}", node_id, error);
        return false;
    }

    // Unlock the node
    unlocked_nodes_.insert(node_id);

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Skill node {} unlocked", node_id);
    }

    return true;
}

std::string SkillTree::CanUnlockNode(uint32_t node_id) const {
    // Check if node exists
    const SkillNode* node = GetNode(node_id);
    if (!node) {
        return "Node does not exist";
    }

    // Check if already unlocked
    if (IsNodeUnlocked(node_id)) {
        return "Node already unlocked";
    }

    // Check prerequisites
    if (!PrerequisitesMet(node_id)) {
        return "Prerequisites not met";
    }

    return "";  // Can unlock
}

std::vector<std::string> SkillTree::GetGrantedAbilities() const {
    std::vector<std::string> abilities;

    for (uint32_t node_id : unlocked_nodes_) {
        const SkillNode* node = GetNode(node_id);
        if (node) {
            abilities.insert(abilities.end(), 
                           node->abilities_granted.begin(),
                           node->abilities_granted.end());
        }
    }

    return abilities;
}

std::vector<Modifier> SkillTree::GetGrantedModifiers() const {
    std::vector<Modifier> modifiers;

    for (uint32_t node_id : unlocked_nodes_) {
        const SkillNode* node = GetNode(node_id);
        if (node) {
            modifiers.insert(modifiers.end(),
                           node->modifiers_granted.begin(),
                           node->modifiers_granted.end());
        }
    }

    return modifiers;
}

void SkillTree::Reset() {
    unlocked_nodes_.clear();
}

bool SkillTree::PrerequisitesMet(uint32_t node_id) const {
    const SkillNode* node = GetNode(node_id);
    if (!node) return false;

    // Check if all prerequisites are unlocked
    for (uint32_t prereq_id : node->prerequisites) {
        if (!IsNodeUnlocked(prereq_id)) {
            return false;
        }
    }

    return true;
}

std::vector<SkillNode>::iterator SkillTree::FindNode(uint32_t node_id) {
    return std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const SkillNode& node) { return node.id == node_id; });
}

std::vector<SkillNode>::const_iterator SkillTree::FindNode(uint32_t node_id) const {
    return std::find_if(nodes_.begin(), nodes_.end(),
        [node_id](const SkillNode& node) { return node.id == node_id; });
}

} // namespace engine::ability
