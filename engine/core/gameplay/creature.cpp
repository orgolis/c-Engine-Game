#include "creature.h"
#include <spdlog/spdlog.h>

namespace schizo::gameplay {

BehaviorNode::Result SelectorNode::Tick(Creature* creature, float delta_time) {
    if (!creature) return BehaviorNode::Result::Failure;
    
    for (auto& child : children_) {
        auto result = child->Tick(creature, delta_time);
        
        if (result == BehaviorNode::Result::Success) {
            current_child_ = 0;  // Reset for next frame
            return BehaviorNode::Result::Success;
        }
        
        if (result == BehaviorNode::Result::Running) {
            return BehaviorNode::Result::Running;
        }
    }
    
    current_child_ = 0;
    return BehaviorNode::Result::Failure;
}

BehaviorNode::Result SequenceNode::Tick(Creature* creature, float delta_time) {
    if (!creature) return BehaviorNode::Result::Failure;
    
    for (auto& child : children_) {
        auto result = child->Tick(creature, delta_time);
        
        if (result == BehaviorNode::Result::Failure) {
            return BehaviorNode::Result::Failure;
        }
        
        if (result == BehaviorNode::Result::Running) {
            return BehaviorNode::Result::Running;
        }
    }
    
    return BehaviorNode::Result::Success;
}

} // namespace schizo::gameplay
