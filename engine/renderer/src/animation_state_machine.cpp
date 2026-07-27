// ============================================================================
// animation_state_machine.cpp — see animation_state_machine.h.
// ============================================================================

#include "animation_state_machine.h"

#include <algorithm>
#include <cmath>

namespace schizo::renderer {

AnimationStateMachine::AnimationStateMachine(std::shared_ptr<Skeleton> skeleton)
    : skeleton_(std::move(skeleton)) {
    if (skeleton_) PoseFromSkeleton(*skeleton_, bind_pose_);
}

int AnimationStateMachine::AddState(const AnimState& state) {
    states_.push_back(state);
    if (entry_ < 0) entry_ = 0;
    return static_cast<int>(states_.size()) - 1;
}

void AnimationStateMachine::AddTransition(const AnimTransition& t) {
    transitions_.push_back(t);
}

void AnimationStateMachine::SetEntryState(int idx) { entry_ = idx; }

void AnimationStateMachine::SetFloat(const std::string& n, float v) {
    params_[n] = {AnimParamType::Float, v};
}
void AnimationStateMachine::SetBool(const std::string& n, bool v) {
    params_[n] = {AnimParamType::Bool, v ? 1.0f : 0.0f};
}
void AnimationStateMachine::SetTrigger(const std::string& n) {
    params_[n] = {AnimParamType::Trigger, 1.0f};
}
float AnimationStateMachine::GetFloat(const std::string& n) const {
    auto it = params_.find(n);
    return it != params_.end() ? it->second.value : 0.0f;
}
bool AnimationStateMachine::GetBool(const std::string& n) const {
    return GetFloat(n) != 0.0f;
}

std::string AnimationStateMachine::CurrentStateName() const {
    return (current_ >= 0 && current_ < (int)states_.size()) ? states_[current_].name
                                                             : std::string();
}

float AnimationStateMachine::StateCycle(int idx) const {
    if (idx < 0 || idx >= (int)states_.size()) return 1.0f;
    const AnimState& s = states_[idx];
    if (s.clip) return std::max(s.clip->GetDuration(), 1e-3f);
    if (s.blend_tree) {
        // Representative cycle = longest contributing clip at the current params.
        std::vector<std::pair<AnimationClip*, float>> blend;
        s.blend_tree->GetBlendedAnimations(GetFloat(s.blend_param_x),
                                           GetFloat(s.blend_param_y), blend);
        float dur = 0.0f;
        for (auto& [c, w] : blend) if (c) dur = std::max(dur, c->GetDuration());
        return std::max(dur, 1e-3f);
    }
    return 1.0f;
}

void AnimationStateMachine::SampleState(int idx, float time, Pose& out) const {
    out = bind_pose_;
    if (idx < 0 || idx >= (int)states_.size()) return;
    const AnimState& s = states_[idx];
    if (s.clip) {
        SampleClipToPose(*s.clip, time, s.loop, out);
    } else if (s.blend_tree) {
        const float cycle = StateCycle(idx);
        const float phase = cycle > 0.0f
            ? (s.loop ? std::fmod(std::fmod(time, cycle) + cycle, cycle) / cycle
                      : glm::clamp(time / cycle, 0.0f, 1.0f))
            : 0.0f;
        SampleBlendTreeToPose(*s.blend_tree, GetFloat(s.blend_param_x),
                              GetFloat(s.blend_param_y), phase, out);
    }
}

bool AnimationStateMachine::EvalConditions(const AnimTransition& t) const {
    for (const AnimCondition& c : t.conditions) {
        const float v = GetFloat(c.param);
        bool ok = false;
        switch (c.op) {
            case AnimCondOp::Greater:      ok = v >  c.threshold; break;
            case AnimCondOp::GreaterEqual: ok = v >= c.threshold; break;
            case AnimCondOp::Less:         ok = v <  c.threshold; break;
            case AnimCondOp::LessEqual:    ok = v <= c.threshold; break;
            case AnimCondOp::Equals:       ok = std::abs(v - c.threshold) < 1e-4f; break;
            case AnimCondOp::NotEquals:    ok = std::abs(v - c.threshold) >= 1e-4f; break;
            case AnimCondOp::True:         ok = v != 0.0f; break;
            case AnimCondOp::False:        ok = v == 0.0f; break;
            case AnimCondOp::TriggerSet:   ok = v != 0.0f; break;
        }
        if (!ok) return false;
    }
    // All conditions held (or there were none → an unconditional transition,
    // typically paired with has_exit_time).
    return true;
}

bool AnimationStateMachine::ExitTimeSatisfied(const AnimTransition& t) const {
    if (!t.has_exit_time) return true;
    const float cycle = StateCycle(current_);
    const float phase = cycle > 0.0f ? std::fmod(time_, cycle) / cycle : 1.0f;
    return phase >= t.exit_time;
}

void AnimationStateMachine::ExtractRootMotion(int idx, float time) {
    if (idx < 0 || idx >= (int)states_.size() || !states_[idx].apply_root_motion) {
        have_last_root_ = false;
        return;
    }
    // Sample just the root bone's animated position at this phase.
    Pose p = bind_pose_;
    const AnimState& s = states_[idx];
    if (s.clip) SampleClipToPose(*s.clip, time, s.loop, p);
    else return;  // root motion from blend trees not supported in v1

    if (root_bone_ >= p.size()) { have_last_root_ = false; return; }
    const glm::vec3 cur = p[root_bone_].position;
    const float cycle = StateCycle(idx);
    const float phase = cycle > 0.0f ? std::fmod(std::fmod(time, cycle) + cycle, cycle) : 0.0f;

    if (have_last_root_) {
        // Skip the frame where the clip loops (phase jumps backward) to avoid a
        // large negative delta; a one-frame stall per loop is imperceptible.
        if (phase >= last_root_phase_) root_accum_ += (cur - last_root_pos_);
    }
    last_root_pos_   = cur;
    last_root_phase_ = phase;
    have_last_root_  = true;
}

glm::vec3 AnimationStateMachine::ConsumeRootMotionDelta() {
    const glm::vec3 d = root_accum_;
    root_accum_ = glm::vec3(0.0f);
    return d;
}

void AnimationStateMachine::Update(float dt) {
    if (states_.empty() || !skeleton_) return;
    if (current_ < 0) { current_ = entry_ >= 0 ? entry_ : 0; time_ = 0.0f; }

    // 1) Advance clocks.
    time_ += dt * states_[current_].speed;
    if (in_transition_) {
        trans_time_    += dt;
        trans_to_time_ += dt * states_[trans_to_].speed;
        if (trans_time_ >= trans_dur_) {
            current_ = trans_to_;
            time_    = trans_to_time_;
            in_transition_ = false;
            have_last_root_ = false;   // root-motion continuity resets on state change
        }
    }

    // 2) Pick a transition (skip if already blending). "Any State" (from == -1)
    //    and explicit from-current transitions are both considered; first match
    //    in declaration order wins.
    if (!in_transition_) {
        for (const AnimTransition& t : transitions_) {
            if (t.to < 0 || t.to >= (int)states_.size()) continue;
            if (t.from != -1 && t.from != current_) continue;
            if (t.to == current_) continue;
            if (!EvalConditions(t) || !ExitTimeSatisfied(t)) continue;
            in_transition_ = true;
            trans_to_      = t.to;
            trans_time_    = 0.0f;
            trans_dur_     = std::max(t.blend_duration, 1e-3f);
            trans_to_time_ = 0.0f;
            break;
        }
    }

    // 3) Sample + commit the pose.
    Pose pose;
    SampleState(current_, time_, pose);
    if (in_transition_) {
        Pose dst;
        SampleState(trans_to_, trans_to_time_, dst);
        const float t = glm::clamp(trans_time_ / trans_dur_, 0.0f, 1.0f);
        BlendPoses(pose, dst, t, pose);
    }

    // 4) Root motion from the dominant state, THEN zero the root's planar
    //    translation in the committed pose so the mesh doesn't also slide.
    //    NB: gate on in_transition_ — after a transition completes, trans_to_
    //    still equals current_, so keying off (root_state==trans_to_) would
    //    wrongly read the frozen trans_to_time_ (root motion stalls).
    int   root_state = current_;
    float root_time  = time_;
    if (in_transition_ && trans_time_ > trans_dur_ * 0.5f) {
        root_state = trans_to_;
        root_time  = trans_to_time_;
    }
    ExtractRootMotion(root_state, root_time);
    if (root_state >= 0 && states_[root_state].apply_root_motion &&
        root_bone_ < pose.size()) {
        pose[root_bone_].position.x = bind_pose_[root_bone_].position.x;
        pose[root_bone_].position.z = bind_pose_[root_bone_].position.z;
    }

    ApplyPoseToSkeleton(*skeleton_, pose);

    // 5) Triggers are one-shot — clear after this frame's evaluation.
    for (auto& [name, p] : params_)
        if (p.type == AnimParamType::Trigger) p.value = 0.0f;
}

} // namespace schizo::renderer
