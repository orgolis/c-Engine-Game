#pragma once

// ============================================================================
// AnimationStateMachine — data-driven animation graph. (Animation Stage 5.)
//
// States reference either a single clip or a blend tree (driven by named
// parameters); transitions fire when their conditions hold, cross-fading the
// outgoing and incoming poses over a blend duration. Gameplay sets parameters
// (speed, grounded, is_moving, jump trigger, …); the machine produces the
// final skeleton pose each frame plus an accumulated root-motion delta the
// character controller consumes. Pure math — no GPU/physics deps.
// ============================================================================

#include "skeleton.h"
#include "animation.h"
#include "blend_tree.h"
#include "anim_pose.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::renderer {

enum class AnimParamType { Float, Bool, Trigger };

/// A state's pose source is EITHER `clip` OR `blend_tree` (with the parameter
/// names that drive it).
struct AnimState {
    std::string name;
    std::shared_ptr<AnimationClip> clip;
    std::shared_ptr<BlendTree>     blend_tree;
    std::string blend_param_x;
    std::string blend_param_y;
    bool  loop             = true;
    float speed            = 1.0f;
    bool  apply_root_motion = false;
};

enum class AnimCondOp {
    Greater, GreaterEqual, Less, LessEqual, Equals, NotEquals,
    True, False, TriggerSet
};

struct AnimCondition {
    std::string param;
    AnimCondOp  op        = AnimCondOp::Greater;
    float       threshold = 0.0f;
};

struct AnimTransition {
    int from = -1;   // source state index; -1 = "Any State"
    int to   = -1;   // destination state index
    std::vector<AnimCondition> conditions;   // ALL must hold to fire
    float blend_duration = 0.2f;
    bool  has_exit_time  = false;  // if set, only fire once the source clip
    float exit_time      = 0.9f;   // reaches this fraction [0,1] of its cycle
};

class AnimationStateMachine {
public:
    explicit AnimationStateMachine(std::shared_ptr<Skeleton> skeleton);

    // ---- Authoring -------------------------------------------------------
    int  AddState(const AnimState& state);          // returns state index
    void AddTransition(const AnimTransition& t);
    void SetEntryState(int state_index);
    void SetRootBone(uint16_t bone_id) { root_bone_ = bone_id; }

    // ---- Parameters (set by gameplay each frame) -------------------------
    void  SetFloat(const std::string& name, float v);
    void  SetBool(const std::string& name, bool v);
    void  SetTrigger(const std::string& name);       // one-shot; auto-reset after Update
    float GetFloat(const std::string& name) const;
    bool  GetBool(const std::string& name) const;

    // ---- Runtime ---------------------------------------------------------
    /// Advance time, evaluate transitions, and write the resulting pose into
    /// the skeleton (world transforms refreshed). Call once per frame.
    void Update(float dt);

    int         CurrentState() const { return current_; }
    std::string CurrentStateName() const;
    bool        IsTransitioning() const { return in_transition_; }

    /// Root-motion translation accumulated since the last call (and reset).
    /// Zero unless the driving state has apply_root_motion. In the skeleton's
    /// root-bone space; the caller rotates it into world by the entity yaw.
    glm::vec3 ConsumeRootMotionDelta();

    Skeleton* GetSkeleton() { return skeleton_.get(); }

private:
    float StateCycle(int state_index) const;                 // representative duration
    void  SampleState(int state_index, float time, Pose& out) const;
    bool  EvalConditions(const AnimTransition& t) const;
    bool  ExitTimeSatisfied(const AnimTransition& t) const;
    void  ExtractRootMotion(int state_index, float time);

    std::shared_ptr<Skeleton> skeleton_;
    Pose bind_pose_;                       // base for non-animated bones
    std::vector<AnimState>      states_;
    std::vector<AnimTransition> transitions_;

    struct Param { AnimParamType type = AnimParamType::Float; float value = 0.0f; };
    std::unordered_map<std::string, Param> params_;

    int   entry_   = -1;
    int   current_ = -1;
    float time_    = 0.0f;      // time in the current state

    bool  in_transition_ = false;
    int   trans_to_      = -1;
    float trans_time_    = 0.0f;   // elapsed within the blend
    float trans_dur_     = 0.0f;
    float trans_to_time_ = 0.0f;   // time in the destination state during blend

    // Root motion.
    uint16_t  root_bone_       = 0;
    bool      have_last_root_  = false;
    float     last_root_phase_ = 0.0f;
    glm::vec3 last_root_pos_   = glm::vec3(0.0f);
    glm::vec3 root_accum_      = glm::vec3(0.0f);
};

} // namespace schizo::renderer
