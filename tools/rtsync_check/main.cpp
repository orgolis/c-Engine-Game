// ============================================================================
// rtsync_check — the ray-tracing scene must not touch memory a frame in
// flight is still using.
//
// The editor keeps two frames in flight: before recording frame N it waits
// only for frame N-2. VulkanRtScene rebuilt its TLAS in place, shared one
// scratch buffer across builds, and wrote its instance records and the
// reflection/DDGI shading SSBO straight into single host-visible buffers --
// all of which frame N-1 could still be reading. The visible symptom was that
// identical runs ended in different images (3 distinct results in 3 runs at
// 20k entities; after the fix, 1). When the scene grew, the old buffers were
// destroyed on the spot, under frame N-1 -- a use-after-free that fires while
// a project loads (PERFORMANCE_AUDIT.md F16).
//
// None of this shows in a single frame, so the contract is pinned in the
// source:
//   - the host ring is deeper than the frames in flight (read from BOTH files:
//     raising kMaxFrames must fail here until the ring grows with it);
//   - a barrier orders each frame's builds after the previous frame's ray
//     queries and builds, BEFORE any build is recorded;
//   - replacing a grown TLAS or SSBO first drains the GPU;
//   - the SSBO the passes bind stays device-local and is filled by a GPU copy
//     ordered after the previous frame's reads.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / "engine" / "renderer" / "gpu" / "vulkan" / "vulkan_rt_scene.cpp", ec)) return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

int read_int(const std::string& src, const std::string& pattern) {
    std::smatch m;
    if (std::regex_search(src, m, std::regex(pattern))) return std::stoi(m[1].str());
    return -1;
}

}  // namespace

int main(int, char** argv) {
    std::printf("=== rtsync_check ===\n\n");
    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) {
        std::printf("SKIP rtsync_check: needs a source checkout\n");
        return 0;
    }
    const fs::path vk = root / "engine" / "renderer" / "gpu" / "vulkan";
    const std::string cpp = read_file(vk / "vulkan_rt_scene.cpp");
    const std::string hdr = read_file(vk / "vulkan_rt_scene.h");
    const std::string editor = read_file(root / "editor" / "src" / "main.cpp");

    std::printf("Ring depth against the frames in flight:\n");
    const int frames = read_int(editor, R"(constexpr uint32_t kMaxFrames = (\d+);)");
    const int ring = read_int(hdr, R"(static constexpr uint32_t kHostRing = (\d+);)");
    check(frames > 0 && ring > 0, "found kMaxFrames (editor) = " + std::to_string(frames) +
                                  " and kHostRing (RT scene) = " + std::to_string(ring));
    check(ring > frames, "the host ring is deeper than the frames in flight (a slot is reused only "
                         "after the frame that wrote it has retired)");

    std::printf("\nThe pre-build barrier:\n");
    const size_t update_at = cpp.find("void VulkanRtScene::update(VkCommandBuffer cmd");
    const size_t barrier_at = cpp.find("VkMemoryBarrier pre{};", update_at);
    const size_t first_build = cpp.find("ensure_blas(cmd, items[i].mesh);", update_at);
    check(update_at != std::string::npos && barrier_at != std::string::npos && first_build != std::string::npos &&
          barrier_at < first_build,
          "update() records a barrier BEFORE any BLAS/TLAS build");
    const std::string barrier = barrier_at == std::string::npos ? "" : cpp.substr(barrier_at, first_build - barrier_at);
    check(barrier.find("VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |") != std::string::npos &&
          barrier.find("VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |") != std::string::npos &&
          barrier.find("VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,\n") != std::string::npos,
          "it waits for the previous frame's ray queries (fragment, compute) and its builds");

    std::printf("\nHost-written buffers come from the ring:\n");
    check(cpp.find("HostSlot& inst_slot = instance_ring_[rt_frame_ % kHostRing];") != std::string::npos,
          "the TLAS instance records go to this frame's ring slot");
    check(cpp.find("HostSlot& staging = instance_data_staging_[rt_frame_ % kHostRing];") != std::string::npos,
          "the shading records go to this frame's staging slot");

    std::printf("\nThe bound SSBO stays put and is filled on the GPU timeline:\n");
    check(cpp.find("VK_BUFFER_USAGE_TRANSFER_DST_BIT |") != std::string::npos &&
          cpp.find("VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,\n                               instance_data_buffer_") != std::string::npos,
          "the instance-data SSBO is device-local and a copy destination");
    const size_t copy_at = cpp.find("vkCmdCopyBuffer(cmd, staging.buffer, instance_data_buffer_");
    const size_t before_at = cpp.rfind("before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;", copy_at);
    const size_t after_at = cpp.find("after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;", copy_at);
    check(copy_at != std::string::npos && before_at != std::string::npos && after_at != std::string::npos,
          "the copy is fenced: after the previous frame's shader reads, before this frame's");

    std::printf("\nGrowth drains the GPU before replacing what a frame may be using:\n");
    check(cpp.find("if (tlas_.handle != VK_NULL_HANDLE) vkDeviceWaitIdle(vk);\n        tlas_.destroy(vk, fns_);") != std::string::npos,
          "a grown TLAS waits before the old one is destroyed");
    check(cpp.find("if (instance_data_buffer_ != VK_NULL_HANDLE) vkDeviceWaitIdle(vk);\n            destroy_buffer(instance_data_buffer_") != std::string::npos,
          "a grown instance-data SSBO waits before the old one is destroyed");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL rtsync_check — the RT scene can write or free memory a frame in flight still uses.\n");
    return g_fail ? 1 : 0;
}
