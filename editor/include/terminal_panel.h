#pragma once

#include <memory>

namespace schizo::editor {

/**
 * @class TerminalPanel
 * @brief A VS Code-style embedded OS shell terminal docked in the editor.
 *
 * Spawns a real interactive shell (PowerShell, fallback cmd) attached to a
 * Windows pseudo-console (ConPTY), streams its output into a dockable ImGui
 * window, and sends typed commands back to it. Run git, build scripts, dir/ls,
 * etc. without leaving the editor.
 *
 * Scope: line-oriented rendering with ANSI colour support — normal command
 * output looks right. Full-screen TUIs (vim/less) are not emulated.
 *
 * All Win32 / ConPTY / threading state lives behind the pimpl so this header
 * stays free of <windows.h>.
 */
class TerminalPanel {
public:
    TerminalPanel();
    ~TerminalPanel();

    TerminalPanel(const TerminalPanel&) = delete;
    TerminalPanel& operator=(const TerminalPanel&) = delete;

    /// Draw the "Terminal" ImGui window. `open` is the show/close flag (the
    /// window's close button writes through it). Safe to call every frame even
    /// when the window is an inactive dock tab.
    void Render(bool* open);

    // pimpl — declared public only so the free ImGui input-history callback in
    // the .cpp can name the type; the instance pointer stays private.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace schizo::editor
