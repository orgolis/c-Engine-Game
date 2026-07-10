#pragma once

#include <memory>

namespace schizo::editor {

/**
 * Install a spdlog sink on the default logger that captures every log record
 * into an in-memory ring buffer the ConsolePanel renders. Call ONCE, early in
 * main() (before the heavy startup logging) so those lines are captured too.
 */
void install_console_log_sink();

/**
 * @class ConsolePanel
 * @brief Dockable "Output" panel showing the editor's own log output — i.e.
 * everything that scrolls past in the console when running editor.exe — with
 * colour-coding by level, a minimum-level filter, and text search. Read-only.
 *
 * The capture sink (install_console_log_sink) feeds a process-global buffer;
 * this panel just reads it, so the panel can be created lazily and still see
 * all logs since startup.
 */
class ConsolePanel {
public:
    ConsolePanel();
    ~ConsolePanel();
    ConsolePanel(const ConsolePanel&) = delete;
    ConsolePanel& operator=(const ConsolePanel&) = delete;

    /// Draw the "Output" ImGui window. `open` is the show/close flag.
    void Render(bool* open);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace schizo::editor
