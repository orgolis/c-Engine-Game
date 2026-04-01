#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/math/math.h"

using namespace gws::math;
using Catch::Matchers::WithinAbs;

constexpr float EPSILON_QUAT = 1e-5f;

TEST_CASE("Quaternion construction", "[math][quat]") {
    Quaternion q;
    REQUIRE_THAT(q.x, WithinAbs(0.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.y, WithinAbs(0.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.z, WithinAbs(0.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.w, WithinAbs(1.0f, EPSILON_QUAT));
}

TEST_CASE("Quaternion custom", "[math][quat]") {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE_THAT(q.x, WithinAbs(1.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.y, WithinAbs(2.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.z, WithinAbs(3.0f, EPSILON_QUAT));
    REQUIRE_THAT(q.w, WithinAbs(4.0f, EPSILON_QUAT));
}

TEST_CASE("Quaternion length", "[math][quat]") {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    float len = q.length();
    
    float expected = sqrtf(1.0f + 4.0f + 9.0f + 16.0f);
    REQUIRE_THAT(len, WithinAbs(expected, EPSILON_QUAT));
}

TEST_CASE("Quaternion normalization", "[math][quat]") {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion normalized = q.normalized();
    
    float mag = sqrtf(normalized.x * normalized.x + 
                      normalized.y * normalized.y + 
                      normalized.z * normalized.z + 
                      normalized.w * normalized.w);
    
    REQUIRE_THAT(mag, WithinAbs(1.0f, EPSILON_QUAT));
}

TEST_CASE("Quaternion conjugate", "[math][quat]") {
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion conj = q.conjugate();
    
    REQUIRE_THAT(conj.x, WithinAbs(-1.0f, EPSILON_QUAT));
    REQUIRE_THAT(conj.y, WithinAbs(-2.0f, EPSILON_QUAT));
    REQUIRE_THAT(conj.z, WithinAbs(-3.0f, EPSILON_QUAT));
    REQUIRE_THAT(conj.w, WithinAbs(4.0f, EPSILON_QUAT));
}
