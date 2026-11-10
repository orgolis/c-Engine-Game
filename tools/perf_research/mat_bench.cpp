// mat4 * mat4 and batched AABB culling kernels: GLM as the engine uses it
// (packed types, no intrinsics) against hand-written SSE on the same memory.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <immintrin.h>
#include <glm/glm.hpp>

volatile uint64_t g_sink;

template <class F>
double best_ms(F&& f, int reps = 300) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        f();
        auto t1 = std::chrono::high_resolution_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

// out = a * b, column-major, exactly glm's result (same operation order per
// column: a0*b.x + a1*b.y + a2*b.z + a3*b.w evaluated left to right).
static inline void mul_sse(const float* a, const float* b, float* out) {
    const __m128 a0 = _mm_loadu_ps(a), a1 = _mm_loadu_ps(a + 4), a2 = _mm_loadu_ps(a + 8), a3 = _mm_loadu_ps(a + 12);
    for (int c = 0; c < 4; ++c) {
        const __m128 x = _mm_set1_ps(b[c * 4 + 0]);
        const __m128 y = _mm_set1_ps(b[c * 4 + 1]);
        const __m128 z = _mm_set1_ps(b[c * 4 + 2]);
        const __m128 w = _mm_set1_ps(b[c * 4 + 3]);
        __m128 r = _mm_mul_ps(a0, x);
        r = _mm_add_ps(r, _mm_mul_ps(a1, y));
        r = _mm_add_ps(r, _mm_mul_ps(a2, z));
        r = _mm_add_ps(r, _mm_mul_ps(a3, w));
        _mm_storeu_ps(out + c * 4, r);
    }
}

int main() {
    const size_t N = 20000;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<glm::mat4> model(N), out(N), out2(N);
    for (auto& m : model) for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m[i][j] = u(rng);
    glm::mat4 vp;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) vp[i][j] = u(rng);

    double t_glm = best_ms([&] { for (size_t i = 0; i < N; ++i) out[i] = vp * model[i]; g_sink = (uint64_t)out[N / 2][1][1]; });
    double t_sse = best_ms([&] { for (size_t i = 0; i < N; ++i) mul_sse(&vp[0][0], &model[i][0][0], &out2[i][0][0]); g_sink = (uint64_t)out2[N / 2][1][1]; });
    size_t diff = 0;
    for (size_t i = 0; i < N; ++i) if (std::memcmp(&out[i], &out2[i], 64) != 0) ++diff;
    std::printf("mat4*mat4: glm %.2f ns   hand SSE %.2f ns   (%zu of %zu results differ bitwise)\n",
                t_glm * 1e6 / N, t_sse * 1e6 / N, diff, N);

    // ---- culling: AABB vs 6 planes. glm scalar vs centre/extent form in SSE, 4 boxes at a time (SoA).
    struct AABB { glm::vec3 mn, mx; };
    std::vector<AABB> boxes(N);
    for (auto& bx : boxes) { glm::vec3 c(u(rng) * 100, u(rng) * 100, u(rng) * 100); bx.mn = c - 1.0f; bx.mx = c + 1.0f; }
    glm::vec4 planes[6];
    for (auto& p : planes) p = glm::vec4(glm::normalize(glm::vec3(u(rng), u(rng), u(rng))), u(rng) * 50);
    std::vector<uint8_t> vis(N), vis2(N);
    double t_cull = best_ms([&] {
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
    });

    // SoA centre/extent, padded to a multiple of 4.
    const size_t M = (N + 3) & ~size_t(3);
    std::vector<float> cx(M), cy(M), cz(M), ex(M), ey(M), ez(M);
    for (size_t i = 0; i < N; ++i) {
        const glm::vec3 c = (boxes[i].mn + boxes[i].mx) * 0.5f, e = (boxes[i].mx - boxes[i].mn) * 0.5f;
        cx[i] = c.x; cy[i] = c.y; cz[i] = c.z; ex[i] = e.x; ey[i] = e.y; ez[i] = e.z;
    }
    double t_cull_sse = best_ms([&] {
        const __m128 sign = _mm_set1_ps(-0.0f);
        for (size_t i = 0; i < M; i += 4) {
            const __m128 X = _mm_loadu_ps(&cx[i]), Y = _mm_loadu_ps(&cy[i]), Z = _mm_loadu_ps(&cz[i]);
            const __m128 EX = _mm_loadu_ps(&ex[i]), EY = _mm_loadu_ps(&ey[i]), EZ = _mm_loadu_ps(&ez[i]);
            __m128 inside = _mm_castsi128_ps(_mm_set1_epi32(-1));
            for (int p = 0; p < 6; ++p) {
                const __m128 nx = _mm_set1_ps(planes[p].x), ny = _mm_set1_ps(planes[p].y), nz = _mm_set1_ps(planes[p].z), d = _mm_set1_ps(planes[p].w);
                // distance of the centre plus the projected radius: >= 0 means the far corner is in front
                __m128 dist = _mm_add_ps(_mm_add_ps(_mm_mul_ps(nx, X), _mm_mul_ps(ny, Y)), _mm_add_ps(_mm_mul_ps(nz, Z), d));
                __m128 rad  = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_andnot_ps(sign, nx), EX), _mm_mul_ps(_mm_andnot_ps(sign, ny), EY)),
                                         _mm_mul_ps(_mm_andnot_ps(sign, nz), EZ));
                inside = _mm_and_ps(inside, _mm_cmpge_ps(_mm_add_ps(dist, rad), _mm_setzero_ps()));
            }
            const int mask = _mm_movemask_ps(inside);
            for (int k = 0; k < 4 && i + k < N; ++k) vis2[i + k] = (mask >> k) & 1;
        }
        g_sink = vis2[N / 3];
    });
    size_t vd = 0, nvis = 0;
    for (size_t i = 0; i < N; ++i) { vd += vis[i] != vis2[i]; nvis += vis[i]; }
    std::printf("AABB vs 6 planes: glm scalar %.2f ns   SoA SSE x4 %.2f ns   (%zu visible, %zu disagree)\n",
                t_cull * 1e6 / N, t_cull_sse * 1e6 / N, nvis, vd);
    return 0;
}
