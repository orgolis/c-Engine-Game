// ============================================================================
// worldgen -- a whole world from a preset, written as a scene, without the
// editor. Driven by `gws world`.
//
//   worldgen --save <file.scene> [--preset <name>] [--size <m>]
//            [--resolution <cells>] [--seed <n>] [--rivers <n>]
//            [--height <m>] [--materials <dir>] [--json]
//
// The same build the editor's World menu makes (scene/world_preset.h): ground
// shaped and worn, rivers run from the high ground and carved (each stretch
// one continuous water surface), biomes, ground textures and foliage. The
// scene holds the terrain and the rivers' objects; the heightmap, splat map,
// biome paint and foliage masks are written beside it, as the editor writes
// them. A bigger --size keeps the preset's cell size unless --resolution is
// given, so the land has the same detail, only more of it.
// ============================================================================

#include "scene_serializer.h"

#include "scene.h"
#include "world_preset.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

const char* opt(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return nullptr;
}
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace schizo::scene;
    const bool json = flag(argc, argv, "--json");
    const char* save = opt(argc, argv, "--save");
    const auto presets = world_presets();
    if (!save || flag(argc, argv, "--help")) {
        std::fprintf(stderr, "usage: worldgen --save <file.scene> [--preset <name>] [--size <m>] [--resolution <cells>]\n"
                             "                [--seed <n>] [--rivers <n>] [--height <m>] [--materials <dir>] [--json]\n"
                             "presets:");
        for (const auto& p : presets) std::fprintf(stderr, " \"%s\"", p.name.c_str());
        std::fprintf(stderr, "\n");
        return save ? 0 : 2;
    }

    WorldPreset p = presets.front();
    if (const char* name = opt(argc, argv, "--preset")) {
        bool found = false;
        for (const auto& q : presets)
            if (lower(q.name) == lower(name)) { p = q; found = true; }
        if (!found) {
            std::fprintf(stderr, "worldgen: no preset \"%s\"\n", name);
            return 2;
        }
    }
    const float cell = p.size / static_cast<float>(p.resolution);
    if (const char* v = opt(argc, argv, "--size")) {
        p.size = std::max(16.0f, static_cast<float>(std::atof(v)));
        p.resolution = std::clamp(static_cast<int>(std::lround(p.size / cell)), 16, 2048);
    }
    if (const char* v = opt(argc, argv, "--resolution")) p.resolution = std::clamp(std::atoi(v), 16, 2048);
    if (const char* v = opt(argc, argv, "--rivers"))     p.rivers = std::clamp(std::atoi(v), 0, 32);
    if (const char* v = opt(argc, argv, "--height"))     p.height = std::max(0.0f, static_cast<float>(std::atof(v)));
    const uint32_t seed = opt(argc, argv, "--seed") ? static_cast<uint32_t>(std::strtoul(opt(argc, argv, "--seed"), nullptr, 10)) : 7u;
    const char* materials = opt(argc, argv, "--materials");

    auto scene = std::make_shared<Scene>(p.name);
    const BuiltWorld w = build_world(*scene, p, seed, materials ? materials : "");
    if (!w.terrain) {
        std::fprintf(stderr, "worldgen: the world could not be built\n");
        return 1;
    }
    if (!schizo::editor::SceneSerializer::SaveScene(save, scene)) {
        std::fprintf(stderr, "worldgen: could not write %s\n", save);
        return 1;
    }
    if (json)
        std::printf("{\"preset\":\"%s\",\"size\":%g,\"resolution\":%d,\"seed\":%u,\"rivers\":%d,\"lakes\":%d,\"objects\":%zu,\"scene\":\"%s\"}\n",
                    p.name.c_str(), p.size, p.resolution, seed, w.rivers, w.lakes, w.made.size(), save);
    else
        std::printf("worldgen: \"%s\" %gm (%d cells a side), seed %u: %d river(s), %d lake(s), %zu objects -> %s\n",
                    p.name.c_str(), p.size, p.resolution, seed, w.rivers, w.lakes, w.made.size(), save);
    return 0;
}
