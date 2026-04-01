#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/math/math.h"

using namespace gws::math;
using Catch::Matchers::WithinAbs;

constexpr float EPSILON_MAT = 1e-5f;

TEST_CASE("Mat3 construction", "[math][mat3]") {
    Mat3 m;
    m[0][0] = 1.0f;
    m[1][1] = 2.0f;
    m[2][2] = 3.0f;
    
    REQUIRE_THAT(m[0][0], WithinAbs(1.0f, EPSILON_MAT));
    REQUIRE_THAT(m[1][1], WithinAbs(2.0f, EPSILON_MAT));
    REQUIRE_THAT(m[2][2], WithinAbs(3.0f, EPSILON_MAT));
}

TEST_CASE("Mat4 construction", "[math][mat4]") {
    Mat4 m;
    m[0][0] = 5.0f;
    m[1][1] = 6.0f;
    m[2][2] = 7.0f;
    m[3][3] = 8.0f;
    
    REQUIRE_THAT(m[0][0], WithinAbs(5.0f, EPSILON_MAT));
    REQUIRE_THAT(m[1][1], WithinAbs(6.0f, EPSILON_MAT));
}

TEST_CASE("Mat4 identity", "[math][mat4]") {
    Mat4 m;
    m[0][0] = 1.0f;
    m[0][1] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = 1.0f;
    
    REQUIRE_THAT(m[0][0], WithinAbs(1.0f, EPSILON_MAT));
    REQUIRE_THAT(m[0][1], WithinAbs(0.0f, EPSILON_MAT));
    REQUIRE_THAT(m[1][0], WithinAbs(0.0f, EPSILON_MAT));
    REQUIRE_THAT(m[1][1], WithinAbs(1.0f, EPSILON_MAT));
}
