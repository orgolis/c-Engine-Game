#pragma once

// ============================================================================
// Behavior Tree — composites / decorators / leaf tasks + a typed Blackboard.
// (AI Stage 9.) A tree is ticked each frame with a Blackboard of shared state;
// each node returns Success / Failure / Running. Sequences and Selectors
// remember a Running child and resume there next tick (standard reactive BT).
// Header-only, pure CPU — gameplay/AI wires Action/Condition leaves via
// std::function; the navmesh + perception feed the blackboard.
// ============================================================================

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace schizo::ai {

enum class BtStatus { Success, Failure, Running };

/// Typed key/value store shared across a tree tick.
class Blackboard {
public:
    using Value = std::variant<bool, int, float, glm::vec3, void*>;

    template <class T> void set(const std::string& k, T v) { values_[k] = v; }
    template <class T> T get(const std::string& k, T def = T{}) const {
        auto it = values_.find(k);
        if (it != values_.end()) if (auto* p = std::get_if<T>(&it->second)) return *p;
        return def;
    }
    bool has(const std::string& k) const { return values_.count(k) != 0; }
    void erase(const std::string& k) { values_.erase(k); }

private:
    std::unordered_map<std::string, Value> values_;
};

class BtNode {
public:
    virtual ~BtNode() = default;
    virtual BtStatus tick(Blackboard& bb, float dt) = 0;
    virtual void reset() {}
};
using BtNodePtr = std::unique_ptr<BtNode>;

// ---- Composites ------------------------------------------------------------

/// Runs children in order; fails on the first child that fails, is Running on a
/// Running child (resumes there next tick), succeeds when all succeed.
class Sequence : public BtNode {
public:
    Sequence& add(BtNodePtr c) { children_.push_back(std::move(c)); return *this; }
    BtStatus tick(Blackboard& bb, float dt) override {
        for (; current_ < children_.size(); ++current_) {
            const BtStatus s = children_[current_]->tick(bb, dt);
            if (s == BtStatus::Running) return BtStatus::Running;
            if (s == BtStatus::Failure) { reset(); return BtStatus::Failure; }
        }
        reset();
        return BtStatus::Success;
    }
    void reset() override { current_ = 0; for (auto& c : children_) c->reset(); }
private:
    std::vector<BtNodePtr> children_;
    size_t current_ = 0;
};

/// Runs children in order; succeeds on the first child that succeeds, is
/// Running on a Running child (resumes there), fails when all fail.
class Selector : public BtNode {
public:
    Selector& add(BtNodePtr c) { children_.push_back(std::move(c)); return *this; }
    BtStatus tick(Blackboard& bb, float dt) override {
        for (; current_ < children_.size(); ++current_) {
            const BtStatus s = children_[current_]->tick(bb, dt);
            if (s == BtStatus::Running) return BtStatus::Running;
            if (s == BtStatus::Success) { reset(); return BtStatus::Success; }
        }
        reset();
        return BtStatus::Failure;
    }
    void reset() override { current_ = 0; for (auto& c : children_) c->reset(); }
private:
    std::vector<BtNodePtr> children_;
    size_t current_ = 0;
};

/// Ticks all children each tick. Succeeds once `success_threshold` children
/// have succeeded; fails if enough have failed that the threshold is
/// unreachable; otherwise Running.
class Parallel : public BtNode {
public:
    explicit Parallel(int success_threshold) : threshold_(success_threshold) {}
    Parallel& add(BtNodePtr c) { children_.push_back(std::move(c)); return *this; }
    BtStatus tick(Blackboard& bb, float dt) override {
        int succ = 0, fail = 0;
        for (auto& c : children_) {
            const BtStatus s = c->tick(bb, dt);
            if (s == BtStatus::Success) ++succ;
            else if (s == BtStatus::Failure) ++fail;
        }
        const int need = threshold_ <= 0 ? static_cast<int>(children_.size()) : threshold_;
        if (succ >= need) { reset(); return BtStatus::Success; }
        if (static_cast<int>(children_.size()) - fail < need) { reset(); return BtStatus::Failure; }
        return BtStatus::Running;
    }
    void reset() override { for (auto& c : children_) c->reset(); }
private:
    std::vector<BtNodePtr> children_;
    int threshold_;
};

// ---- Decorators ------------------------------------------------------------

/// Swaps Success<->Failure (Running passes through).
class Inverter : public BtNode {
public:
    explicit Inverter(BtNodePtr c) : child_(std::move(c)) {}
    BtStatus tick(Blackboard& bb, float dt) override {
        const BtStatus s = child_->tick(bb, dt);
        if (s == BtStatus::Success) return BtStatus::Failure;
        if (s == BtStatus::Failure) return BtStatus::Success;
        return s;
    }
    void reset() override { child_->reset(); }
private:
    BtNodePtr child_;
};

/// After the child SUCCEEDS, blocks (returns Failure) for `cooldown` seconds.
class Cooldown : public BtNode {
public:
    Cooldown(float cooldown, BtNodePtr c) : cd_(cooldown), child_(std::move(c)) {}
    BtStatus tick(Blackboard& bb, float dt) override {
        if (timer_ > 0.0f) { timer_ -= dt; return BtStatus::Failure; }
        const BtStatus s = child_->tick(bb, dt);
        if (s == BtStatus::Success) timer_ = cd_;
        return s;
    }
    void reset() override { child_->reset(); }   // note: does NOT clear the cooldown
private:
    float cd_, timer_ = 0.0f;
    BtNodePtr child_;
};

// ---- Leaves ----------------------------------------------------------------

/// Action leaf — user function returns the status (can be Running across ticks).
class Action : public BtNode {
public:
    using Fn = std::function<BtStatus(Blackboard&, float)>;
    explicit Action(Fn fn) : fn_(std::move(fn)) {}
    BtStatus tick(Blackboard& bb, float dt) override { return fn_(bb, dt); }
private:
    Fn fn_;
};

/// Condition leaf — predicate → Success / Failure.
class Condition : public BtNode {
public:
    using Fn = std::function<bool(Blackboard&)>;
    explicit Condition(Fn fn) : fn_(std::move(fn)) {}
    BtStatus tick(Blackboard& bb, float /*dt*/) override {
        return fn_(bb) ? BtStatus::Success : BtStatus::Failure;
    }
private:
    Fn fn_;
};

// ---- Tree ------------------------------------------------------------------

class BehaviorTree {
public:
    void set_root(BtNodePtr root) { root_ = std::move(root); }
    BtStatus tick(Blackboard& bb, float dt) {
        return root_ ? root_->tick(bb, dt) : BtStatus::Failure;
    }
    void reset() { if (root_) root_->reset(); }
    BtNode* root() const { return root_.get(); }
private:
    BtNodePtr root_;
};

// Convenience makers.
inline BtNodePtr make_action(Action::Fn fn) { return std::make_unique<Action>(std::move(fn)); }
inline BtNodePtr make_condition(Condition::Fn fn) { return std::make_unique<Condition>(std::move(fn)); }

} // namespace schizo::ai
