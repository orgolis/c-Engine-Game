// "Output" log console — captures the editor's own spdlog output (everything
// that scrolls past in the console when running editor.exe) and shows it in a
// dockable panel, colour-coded by level, with a min-level filter and search.

#include "console_panel.h"

#include <imgui.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

namespace schizo::editor {

namespace {

struct Entry {
    int         level;   // spdlog::level::level_enum
    std::string text;    // fully formatted line (timestamp + level + message)
};

// Process-global capture buffer shared by the sink (writer) and the panel
// (reader). Function-local static => safe initialisation order.
struct LogStore {
    std::mutex            mtx;
    std::deque<Entry>     entries;
    std::atomic<uint64_t> version{0};
    static constexpr size_t kCap = 5000;

    void push(int level, std::string text) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            entries.push_back(Entry{level, std::move(text)});
            while (entries.size() > kCap) entries.pop_front();
        }
        version.fetch_add(1, std::memory_order_relaxed);
    }
    void clear() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            entries.clear();
        }
        version.fetch_add(1, std::memory_order_relaxed);
    }
};

LogStore& store() {
    static LogStore s;
    return s;
}

// spdlog sink that copies each formatted record into the global LogStore.
template <typename Mutex>
class EditorCaptureSink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::string text(formatted.data(), formatted.size());
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        store().push(static_cast<int>(msg.level), std::move(text));
    }
    void flush_() override {}
};

ImU32 level_color(int lvl) {
    switch (lvl) {
        case 0: /* trace */
        case 1: return IM_COL32(140, 140, 140, 255);   // debug — gray
        case 2: return IM_COL32(215, 215, 215, 255);   // info  — light
        case 3: return IM_COL32(235, 205, 90, 255);     // warn  — yellow
        case 4: return IM_COL32(240, 95, 80, 255);      // error — red
        case 5: return IM_COL32(255, 120, 160, 255);    // critical — bright
        default: return IM_COL32(215, 215, 215, 255);
    }
}

bool contains_ci(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    std::string n = needle;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(::tolower((unsigned char)c));
        return s;
    };
    return lower(hay).find(lower(n)) != std::string::npos;
}

} // namespace

void install_console_log_sink() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    auto sink = std::make_shared<EditorCaptureSink<std::mutex>>();
    sink->set_pattern("[%H:%M:%S] [%l] %v");
    sink->set_level(spdlog::level::trace);   // capture whatever the logger emits
    if (auto def = spdlog::default_logger())
        def->sinks().push_back(std::move(sink));
}

// ---------------------------------------------------------------------------

struct ConsolePanel::Impl {
    int      min_level   = 2;          // 0=All 2=Info 3=Warning 4=Error
    char     search[128] = {};
    bool     autoscroll  = true;

    // version-cached filtered snapshot (rebuilt only when logs/filter change)
    std::vector<Entry> cache;
    uint64_t cache_version = ~0ull;
    int      cache_min_level = -1;
    std::string cache_search;
    bool     scroll_pending = false;

    void refresh() {
        const uint64_t v = store().version.load(std::memory_order_relaxed);
        if (v == cache_version && min_level == cache_min_level &&
            cache_search == search)
            return;   // nothing changed — keep the cached snapshot

        const bool grew = (v != cache_version);
        cache.clear();
        {
            std::lock_guard<std::mutex> lk(store().mtx);
            for (const Entry& e : store().entries)
                if (e.level >= min_level && contains_ci(e.text, search))
                    cache.push_back(e);
        }
        cache_version   = v;
        cache_min_level = min_level;
        cache_search    = search;
        if (grew && autoscroll) scroll_pending = true;
    }
};

ConsolePanel::ConsolePanel() : impl_(std::make_unique<Impl>()) {}
ConsolePanel::~ConsolePanel() = default;

void ConsolePanel::Render(bool* open) {
    Impl& c = *impl_;
    c.refresh();

    ImGui::Begin("Output", open);   // docked window = child; always End() below
    {
        // ---- toolbar ----
        ImGui::SetNextItemWidth(110.0f);
        // The logger runs at info level, so nothing below info is ever captured.
        // Offer only meaningful thresholds (no misleading "All" that implies
        // debug/trace lines that can never appear here).
        const char* levels[] = { "Info", "Warning", "Error" };
        int sel = (c.min_level <= 2) ? 0 : (c.min_level == 3 ? 1 : 2);
        if (ImGui::Combo("##lvl", &sel, levels, 3))
            c.min_level = (sel == 0) ? 2 : (sel == 1 ? 3 : 4);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { store().clear(); }
        ImGui::SameLine();
        ImGui::Checkbox("Autoscroll", &c.autoscroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "filter...", c.search, sizeof(c.search));
        ImGui::Separator();

        // ---- log lines ----
        ImGui::BeginChild("##log", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(c.cache.size()));
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const Entry& e = c.cache[static_cast<size_t>(i)];
                    ImGui::PushStyleColor(ImGuiCol_Text, level_color(e.level));
                    ImGui::TextUnformatted(e.text.c_str(),
                                           e.text.c_str() + e.text.size());
                    ImGui::PopStyleColor();
                }
            clipper.End();
            ImGui::PopStyleVar();

            if (c.scroll_pending) { ImGui::SetScrollHereY(1.0f); c.scroll_pending = false; }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace schizo::editor
