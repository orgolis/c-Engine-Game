// ============================================================================
// presentsync_check — the semaphore a frame's submit signals and its present
// waits on must belong to the swapchain IMAGE, not to the frame in flight.
//
// The editor indexed it by frame in flight. A frame's fence proves its submit
// finished; it does not prove the presentation engine has consumed the
// present's wait. So two frames later the same semaphore could be signalled
// again while that wait was still pending -- VUID-vkQueueSubmit-
// pSignalSemaphores-00067, reported by the validation layers on every run,
// for months, inside a list of "known" VUIDs nobody read. An image cannot be
// acquired again until its previous present has completed, so a semaphore
// per image is free by the time it is signalled again.
//
// Pins both uses to the image index, the growth on swapchain recreation, and
// the absence of a per-frame present semaphore.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / "editor" / "src" / "main.cpp", ec)) return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

}  // namespace

int main(int, char** argv) {
    std::printf("=== presentsync_check ===\n\n");
    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) {
        std::printf("SKIP presentsync_check: needs a source checkout\n");
        return 0;
    }
    std::ifstream f(root / "editor" / "src" / "main.cpp");
    std::stringstream ss; ss << f.rdbuf();
    const std::string src = ss.str();

    check(src.find("submit_info.pSignalSemaphores    = &present_sems[image_index];") != std::string::npos,
          "the frame's submit signals the IMAGE's semaphore");
    check(src.find("swapchain->present_image(image_index, present_sems[image_index]);") != std::string::npos,
          "the present waits on that same semaphore");
    const size_t recreate = src.find("swapchain->recreate(static_cast<uint32_t>(fbw),");
    const size_t grow = src.find("ensure_present_sems(swapchain->get_image_count());", recreate);
    check(recreate != std::string::npos && grow != std::string::npos && grow - recreate < 200,
          "a recreated swapchain grows the semaphore set to its image count");
    check(src.find("render_sems[") == std::string::npos,
          "no per-frame-in-flight present semaphore is left");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL presentsync_check — a present semaphore can be re-signalled while its wait is pending.\n");
    return g_fail ? 1 : 0;
}
