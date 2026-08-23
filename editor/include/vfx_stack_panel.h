#pragma once
// ============================================================================
// vfx_stack_panel — the editor surface for a VFX document (item 4.3).
//
// A per-stage module stack is a LIST, not a graph, so this is an ordered-row
// panel rather than a NodeCanvas. Unity's Shuriken is a stack; Niagara is a
// stack whose MODULES are internally graphs. The canvas earns its place one
// level down, later, when a module's parameters become computed rather than
// constant -- forcing it in now would be the wrong UI for the architecture.
//
// Order is an EDIT, not a display preference: swapping Gravity and Drag is
// different motion. The panel therefore shows explicit up/down controls rather
// than relying on a drag whose drop target is ambiguous.
// ============================================================================

#include "vfx/vfx_graph.h"

#include <string>

namespace schizo::editor {

class VfxStackPanel {
public:
    void draw(bool* open);

    schizo::vfx::VfxGraph&       graph()       { return graph_; }
    const schizo::vfx::VfxGraph& graph() const { return graph_; }
    void set_graph(const schizo::vfx::VfxGraph& g) { graph_ = g; dirty_ = false; }

    const std::string& path() const { return path_; }
    void set_path(const std::string& p) { path_ = p; }

private:
    void draw_stage(schizo::vfx::VfxStage stage, const char* label);
    void draw_module_params(schizo::vfx::VfxModule& m);

    schizo::vfx::VfxGraph graph_ = schizo::vfx::VfxGraph::default_stack();
    std::string           path_;
    bool                  dirty_ = false;
};

}  // namespace schizo::editor
