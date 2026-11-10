// Measures what the engine's "walk every entity and read a few fields" loops
// cost under different memory layouts, on this machine. Not part of the engine.
//
//   A  heap objects of 632 bytes (sizeof(Entity) 616 + shared_ptr control block),
//      allocated in scene order, reached through a vector of pointers; the loop
//      reads the fields the gather reads (is_active @104, content_stamp @112,
//      change_stamp @264, world_stamp @272): two cache lines per entity.
//   B  the same objects, but allocated interleaved with other allocations
//      (what a scene looks like after editing: neighbours are not adjacent).
//   C  a dense "hot" array: one 24-byte record per entity holding those fields.
//   D  struct-of-arrays: one array per field.
//
// Also: 4x4 matrix multiply throughput (view_proj * model for every draw).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
#include <glm/glm.hpp>

struct FakeEntity {
    char     pad0[104];
    bool     is_active;          // @104
    char     pad1[7];
    uint64_t content_stamp;      // @112
    char     pad2[144];
    uint64_t change_stamp;       // @264
    uint64_t world_stamp;        // @272
    char     pad3[336];          // -> 616
};
static_assert(sizeof(FakeEntity) == 616, "matches sizeof(schizo::scene::Entity)");

struct Hot { uint64_t content_stamp, change_stamp, world_stamp; };   // 24 bytes (active packed into a bitset)

template <class F>
double best_ms(F&& f, int reps = 25) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        f();
        auto t1 = std::chrono::high_resolution_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

volatile uint64_t g_sink;

static void evict() {
    // Touch 64 MB so the next timed pass starts with the data out of cache --
    // which is the engine's situation: a whole frame of other work runs
    // between two walks of the scene.
    static std::vector<char> junk(64u << 20, 1);
    uint64_t s = 0;
    for (size_t i = 0; i < junk.size(); i += 64) s += junk[i];
    g_sink = s;
}

int main(int argc, char** argv) {
    const size_t N = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20000;
    std::mt19937 rng(42);

    // A: allocated back to back
    std::vector<std::shared_ptr<FakeEntity>> a(N);
    for (auto& p : a) { p = std::make_shared<FakeEntity>(); p->is_active = true; p->content_stamp = 1; p->change_stamp = 2; p->world_stamp = 3; }

    // B: allocated with other allocations in between, then visited in shuffled order
    std::vector<std::shared_ptr<FakeEntity>> b(N);
    std::vector<std::unique_ptr<char[]>> noise;
    for (auto& p : b) {
        p = std::make_shared<FakeEntity>(); p->is_active = true; p->content_stamp = 1; p->change_stamp = 2; p->world_stamp = 3;
        noise.emplace_back(new char[64 + rng() % 1024]);
    }
    std::shuffle(b.begin(), b.end(), rng);

    std::vector<Hot> c(N, Hot{1, 2, 3});
    std::vector<uint64_t> d0(N, 1), d1(N, 2), d2(N, 3);
    std::vector<uint8_t> active(N, 1);

    auto walk_ptr = [&](std::vector<std::shared_ptr<FakeEntity>>& v) {
        uint64_t s = 0;
        for (const auto& p : v) {
            const FakeEntity* e = p.get();
            if (e->is_active) s += e->content_stamp + e->change_stamp + e->world_stamp;
        }
        g_sink = s;
    };
    auto walk_hot = [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < N; ++i)
            if (active[i]) s += c[i].content_stamp + c[i].change_stamp + c[i].world_stamp;
        g_sink = s;
    };
    auto walk_soa = [&] {
        uint64_t s = 0;
        for (size_t i = 0; i < N; ++i)
            if (active[i]) s += d0[i] + d1[i] + d2[i];
        g_sink = s;
    };

    auto cold = [&](auto&& f) {
        double best = 1e30;
        for (int r = 0; r < 9; ++r) {
            evict();
            auto t0 = std::chrono::high_resolution_clock::now();
            f();
            auto t1 = std::chrono::high_resolution_clock::now();
            best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        return best;
    };

    std::printf("entities: %zu   (heap objects total %.1f MB)\n", N, N * 632.0 / 1048576.0);
    std::printf("%-46s %10s %10s\n", "layout", "warm ms", "cold ms");
    std::printf("%-46s %10.4f %10.4f\n", "A  632-byte heap objects, in allocation order", best_ms([&] { walk_ptr(a); }), cold([&] { walk_ptr(a); }));
    std::printf("%-46s %10.4f %10.4f\n", "B  632-byte heap objects, scattered",          best_ms([&] { walk_ptr(b); }), cold([&] { walk_ptr(b); }));
    std::printf("%-46s %10.4f %10.4f\n", "C  dense 24-byte hot records",                 best_ms(walk_hot),              cold(walk_hot));
    std::printf("%-46s %10.4f %10.4f\n", "D  struct of arrays",                          best_ms(walk_soa),              cold(walk_soa));

    // ---- matrix multiply: view_proj * model for N draws -------------------
    std::vector<glm::mat4> model(N), out(N);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (auto& m : model) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m[i][j] = u(rng);
    glm::mat4 vp;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) vp[i][j] = u(rng);
    double mm = best_ms([&] { for (size_t i = 0; i < N; ++i) out[i] = vp * model[i]; g_sink = (uint64_t)out[N / 2][1][1]; }, 200);
    std::printf("mat4 * mat4 x %zu: %.4f ms  (%.2f ns each)\n", N, mm, mm * 1e6 / N);

    // ---- AABB vs 6 frustum planes, the culling inner loop ----------------
    struct AABB { glm::vec3 mn, mx; };
    std::vector<AABB> boxes(N);
    for (auto& bx : boxes) { glm::vec3 cpos(u(rng) * 100, u(rng) * 100, u(rng) * 100); bx.mn = cpos - 1.0f; bx.mx = cpos + 1.0f; }
    glm::vec4 planes[6];
    for (auto& p : planes) p = glm::vec4(glm::normalize(glm::vec3(u(rng), u(rng), u(rng))), u(rng) * 50);
    std::vector<uint8_t> vis(N);
    double cull = best_ms([&] {
        for (size_t i = 0; i < N; ++i) {
            bool in = true;
            for (int p = 0; p < 6; ++p) {
                const glm::vec3 n(planes[p]);
                const glm::vec3 v(n.x >= 0 ? boxes[i].mx.x : boxes[i].mn.x,
                                  n.y >= 0 ? boxes[i].mx.y : boxes[i].mn.y,
                                  n.z >= 0 ? boxes[i].mx.z : boxes[i].mn.z);
                if (glm::dot(n, v) + planes[p].w < 0) { in = false; break; }
            }
            vis[i] = in;
        }
        g_sink = vis[N / 3];
    }, 200);
    std::printf("AABB vs 6 planes x %zu: %.4f ms  (%.2f ns each)\n", N, cull, cull * 1e6 / N);
    return 0;
}
