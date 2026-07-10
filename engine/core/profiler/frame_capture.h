#pragma once

// ====================
// Frame capture (Stage 14 — Pillar N5)
// ====================
//
// A lightweight in-engine snapshot of ONE frame's draw/dispatch list for
// offline inspection (the in-house counterpart to a RenderDoc capture — no
// resource bytes, just the submission list + counts). Generic on purpose: the
// caller translates its own draw structures into CapturedDraw entries, so core
// stays decoupled from the renderer.
//
// Lifecycle (one-shot): UI calls arm(); the frame-build site calls begin(frame)
// — if it returns true, push add(...) per draw, then end(). The finished
// snapshot is held in last() until the next capture. arm() captures exactly one
// frame, so it costs nothing on normal frames.

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace gws::profile {

struct CapturedDraw {
    std::string pass;        // "Shadow" / "Geometry" / "Transparent" ...
    std::string label;       // mesh / material identifier
    uint32_t    submesh  = 0;
    uint32_t    vertices = 0;
    uint32_t    indices  = 0;
    bool        blend    = false;
};

struct CapturedFrame {
    uint64_t                  frame_index = 0;
    std::vector<CapturedDraw> draws;

    size_t   draw_count() const { return draws.size(); }
    uint64_t total_indices() const {
        uint64_t n = 0;
        for (const CapturedDraw& d : draws) n += d.indices;
        return n;
    }
    uint64_t total_triangles() const { return total_indices() / 3; }
};

class FrameCapture {
public:
    static FrameCapture& instance() { static FrameCapture c; return c; }

    void arm()               { armed_ = true; }
    bool armed()       const { return armed_; }
    bool capturing()   const { return capturing_; }
    bool has_capture() const { return has_capture_; }

    // Returns true if this frame is being captured (and opens collection).
    // Cheap no-op (one bool test) when not armed.
    bool begin(uint64_t frame_index) {
        if (!armed_) return false;
        armed_     = false;
        capturing_ = true;
        pending_   = CapturedFrame{};
        pending_.frame_index = frame_index;
        return true;
    }
    void add(CapturedDraw d) { if (capturing_) pending_.draws.push_back(std::move(d)); }
    void end() {
        if (!capturing_) return;
        capturing_   = false;
        last_        = std::move(pending_);
        pending_     = CapturedFrame{};
        has_capture_ = true;
    }

    const CapturedFrame& last() const { return last_; }

    // Formatted text dump for file export / logging.
    std::string to_text() const {
        std::string out;
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "Frame capture #%llu — %zu draws, %llu triangles\n",
                      static_cast<unsigned long long>(last_.frame_index),
                      last_.draws.size(),
                      static_cast<unsigned long long>(last_.total_triangles()));
        out += buf;
        out += "  pass            label                submesh    verts    indices  blend\n";
        for (const CapturedDraw& d : last_.draws) {
            std::snprintf(buf, sizeof buf,
                          "  %-14s  %-18s  %5u  %8u  %9u  %s\n",
                          d.pass.c_str(), d.label.c_str(), d.submesh,
                          d.vertices, d.indices, d.blend ? "yes" : "no");
            out += buf;
        }
        return out;
    }

private:
    bool          armed_       = false;
    bool          capturing_   = false;
    bool          has_capture_ = false;
    CapturedFrame pending_;
    CapturedFrame last_;
};

}  // namespace gws::profile
