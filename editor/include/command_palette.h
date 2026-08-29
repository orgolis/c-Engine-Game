#pragma once
// ============================================================================
// command_palette — one entry point for everything the editor can do.
//
// The editor's actions live in nested menus, and the number of them is now well
// past what anyone can hold in their head or find by opening menus one at a
// time. A palette turns "where is that command" into typing three letters of
// its name, and it is the difference between a feature existing and a feature
// being used.
//
// The second reason is in the workflow plan and matters more over time: it is a
// UNIFORM entry point. Every action reachable one way, by name, is what lets an
// agent or a script drive the editor without a bespoke hook per feature.
//
// The registry and the matcher are deliberately free of ImGui so they can be
// tested. Ranking is the part that decides whether a palette feels sharp or
// useless, and it is exactly the part a screenshot cannot check.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

struct Command {
    std::string           title;      // "Save Scene"
    std::string           category;   // "File" — shown dimmed, and searchable
    std::string           shortcut;   // "Ctrl+S", display only
    std::function<void()> run;
    // Which editor extension registered this (Phase 4.8); empty for the
    // editor's own commands. Reloading an extension removes its previous
    // commands by this tag. Without an owner there is no way to tell a script's
    // command from a built-in, so a reload would APPEND a second copy of each
    // one -- and because every copy still works, nothing looks broken until the
    // palette is full of duplicates. That is the failure mode hot reload has by
    // default, so the tag exists from the start rather than after it is seen.
    std::string           owner;
};

/// Score `query` against `text`. Higher is better; 0 means no match.
///
/// A subsequence match, so "ss" finds "Save Scene", with the bonuses that make
/// the ordering feel right rather than merely correct:
///   * a hit at the start of a word beats one mid-word, which is what keeps
///     "Rescale Selection" (s and c both buried) below every real hit for "sc";
///   * consecutive hits beat scattered ones, so a typed prefix wins;
///   * shorter titles win ties, because a query that matches both a specific
///     command and a longer one usually meant the specific one.
///
/// Case-insensitive. An empty query matches everything at score 1 so the
/// palette can list all commands before anything is typed.
inline int command_score(const std::string& query, const std::string& text) {
    if (query.empty()) return 1;
    if (text.empty())  return 0;

    int  score = 0;
    int  run   = 0;                 // current consecutive-match run length
    size_t qi  = 0;

    for (size_t ti = 0; ti < text.size() && qi < query.size(); ++ti) {
        const char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[ti])));
        const char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
        if (tc != qc) { run = 0; continue; }

        const bool word_start =
            ti == 0 || text[ti - 1] == ' ' || text[ti - 1] == '.' || text[ti - 1] == '/';

        score += 1;
        if (word_start) score += 8;      // the dominant signal
        if (run > 0)    score += 4;      // reward a continuing run
        ++run;
        ++qi;
    }

    if (qi < query.size()) return 0;     // not every query char was consumed

    // Shorter is better, but never enough to outrank a real positional bonus.
    score += std::max(0, 16 - static_cast<int>(text.size()) / 4);
    return score;
}

class CommandRegistry {
public:
    void add(std::string title, std::string category, std::string shortcut,
             std::function<void()> run) {
        commands_.push_back({std::move(title), std::move(category),
                             std::move(shortcut), std::move(run)});
    }

    /// Register a command belonging to `owner` (an extension's identity).
    void add_owned(std::string title, std::string category, std::string shortcut,
                   std::function<void()> run, std::string owner) {
        commands_.push_back({std::move(title), std::move(category),
                             std::move(shortcut), std::move(run), std::move(owner)});
    }

    /// Drop every command registered by `owner`. Returns how many went.
    /// Call this BEFORE re-running an extension, never after -- the reverse
    /// order removes the commands the reload just added.
    size_t remove_owned_by(const std::string& owner) {
        if (owner.empty()) return 0;      // never mass-remove the built-ins
        const size_t before = commands_.size();
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                                       [&](const Command& c) { return c.owner == owner; }),
                        commands_.end());
        return before - commands_.size();
    }

    /// Run a command by exact title. Returns false when no such command exists,
    /// which is what lets one extension call another's command by name.
    bool run_by_title(const std::string& title) const {
        for (const Command& c : commands_)
            if (c.title == title) { if (c.run) c.run(); return true; }
        return false;
    }

    void clear() { commands_.clear(); }
    size_t size() const { return commands_.size(); }
    const std::vector<Command>& all() const { return commands_; }

    /// Commands matching `query`, best first, capped at `limit`.
    ///
    /// The category is searched too but at a discount: "file" should surface
    /// the File commands without letting a category match outrank a command
    /// whose own name is what was typed.
    std::vector<const Command*> search(const std::string& query, size_t limit = 12) const {
        std::vector<std::pair<int, const Command*>> hits;
        hits.reserve(commands_.size());

        for (const Command& c : commands_) {
            const int s = std::max(command_score(query, c.title),
                                   command_score(query, c.category) / 2);
            if (s > 0) hits.push_back({s, &c});
        }

        // Stable, so equal scores keep registration order rather than shuffling
        // between keystrokes -- a list that reorders under an unchanged query
        // is worse than a list that ranks imperfectly.
        std::stable_sort(hits.begin(), hits.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<const Command*> out;
        for (size_t i = 0; i < hits.size() && i < limit; ++i) out.push_back(hits[i].second);
        return out;
    }

private:
    std::vector<Command> commands_;
};

// ---- UI (implemented in command_palette.cpp) -------------------------------

/// Open the palette and clear whatever was typed last time.
void open_command_palette(bool& open);

/// Draw the palette. Returns true if a command ran this frame.
///
/// `scene` and `select_entity` are optional; when given, entities are searched
/// with the SAME matcher as commands, so a scene's "Guard" is reachable by
/// typing "gu" exactly as a command is. Without that the palette answers half
/// the question and people go back to hunting through the hierarchy.
bool draw_command_palette(bool& open, const CommandRegistry& registry,
                          const std::shared_ptr<schizo::scene::Scene>& scene,
                          const std::function<void(uint32_t)>& select_entity);

}  // namespace schizo::editor
