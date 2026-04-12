#pragma once

#include <vector>
#include <unordered_set>
#include <string>
#include <memory>
#include <glm/glm.hpp>

#include "modifier.h"

namespace engine::ability {

class Ability;

/**
 * @struct SkillNode
 * @brief A single node in the skill tree
 */
struct SkillNode {
    uint32_t id = 0;                           // Unique ID
    std::string name;
    std::string description;
    
    glm::vec2 position = glm::vec2(0.0f);      // Visual position in tree
    
    std::vector<std::string> abilities_granted;     // Abilities unlocked by this node
    std::vector<Modifier> modifiers_granted;       // Stat bonuses
    
    std::vector<uint32_t> prerequisites;            // Node IDs that must be unlocked first
    
    uint32_t required_level = 1;                // Minimum character level to unlock
    uint64_t required_experience = 0;           // Experience cost to unlock
    
    // Visual/UI
    std::string icon_path;
    int tier = 0;                              // Depth in tree (0 = start)
};

/**
 * @class SkillTree
 * @brief Manages character skill progression and ability unlocks
 *
 * Features:
 * - Unlock nodes by spending experience/resources
 * - Check prerequisites before unlocking
 * - Provide abilities and stat bonuses when unlocked
 * - Visualize progression (tier-based, branching paths)
 */
class SkillTree {
public:
    SkillTree() = default;
    ~SkillTree() = default;

    /**
     * Add a node to the skill tree
     */
    void AddNode(const SkillNode& node);

    /**
     * Get a node by ID
     */
    const SkillNode* GetNode(uint32_t node_id) const;
    SkillNode* GetNode(uint32_t node_id);

    /**
     * Get all nodes
     */
    const std::vector<SkillNode>& GetAllNodes() const { return nodes_; }

    /**
     * Check if a node has been unlocked
     */
    bool IsNodeUnlocked(uint32_t node_id) const;

    /**
     * Try to unlock a node
     * @param node_id Node to unlock
     * @param require_resources Check if character has resources (optional)
     * @return true if successfully unlocked
     */
    bool UnlockNode(uint32_t node_id, bool check_resources = false);

    /**
     * Check if node can be unlocked
     * @param node_id Node to check
     * @return Error message if cannot unlock, empty string if can unlock
     */
    std::string CanUnlockNode(uint32_t node_id) const;

    /**
     * Get all unlocked nodes
     */
    const std::unordered_set<uint32_t>& GetUnlockedNodes() const { return unlocked_nodes_; }

    /**
     * Get abilities granted by all unlocked nodes
     */
    std::vector<std::string> GetGrantedAbilities() const;

    /**
     * Get modifiers granted by all unlocked nodes
     */
    std::vector<Modifier> GetGrantedModifiers() const;

    /**
     * Reset skill tree (for respec)
     */
    void Reset();

    /**
     * Get total number of nodes
     */
    size_t GetNodeCount() const { return nodes_.size(); }

    /**
     * Get unlocked node count
     */
    size_t GetUnlockedCount() const { return unlocked_nodes_.size(); }

private:
    std::vector<SkillNode> nodes_;
    std::unordered_set<uint32_t> unlocked_nodes_;

    /**
     * Check if prerequisites are met
     */
    bool PrerequisitesMet(uint32_t node_id) const;

    /**
     * Find node by ID (internal helper)
     */
    std::vector<SkillNode>::iterator FindNode(uint32_t node_id);
    std::vector<SkillNode>::const_iterator FindNode(uint32_t node_id) const;
};

} // namespace engine::ability
