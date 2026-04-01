#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/math/math.h"

using namespace gws::math;
using Catch::Matchers::WithinAbs;

constexpr float EPSILON_VEC = 1e-5f;

TEST_CASE("Vec2 basic operations", "[math][vec2]") {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(3.0f, 4.0f);
    
    Vec2 c = a + b;
    REQUIRE_THAT(c.x, WithinAbs(4.0f, EPSILON_VEC));
    REQUIRE_THAT(c.y, WithinAbs(6.0f, EPSILON_VEC));
}

TEST_CASE("Vec2 subtraction", "[math][vec2]") {
    Vec2 a(5.0f, 6.0f);
    Vec2 b(1.0f, 2.0f);
    
    Vec2 c = a - b;
    REQUIRE_THAT(c.x, WithinAbs(4.0f, EPSILON_VEC));
    REQUIRE_THAT(c.y, WithinAbs(4.0f, EPSILON_VEC));
}

TEST_CASE("Vec3 basic operations", "[math][vec3]") {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    
    Vec3 c = a + b;
    REQUIRE_THAT(c.x, WithinAbs(5.0f, EPSILON_VEC));
    REQUIRE_THAT(c.y, WithinAbs(7.0f, EPSILON_VEC));
    REQUIRE_THAT(c.z, WithinAbs(9.0f, EPSILON_VEC));
}

TEST_CASE("Vec3 magnitude", "[math][vec3]") {
    Vec3 v(3.0f, 4.0f, 0.0f);
    float mag = v.length();
    REQUIRE_THAT(mag, WithinAbs(5.0f, EPSILON_VEC));
}

TEST_CASE("Vec4 construction", "[math][vec4]") {
    Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE_THAT(v.x, WithinAbs(1.0f, EPSILON_VEC));
    REQUIRE_THAT(v.y, WithinAbs(2.0f, EPSILON_VEC));
    REQUIRE_THAT(v.z, WithinAbs(3.0f, EPSILON_VEC));
    REQUIRE_THAT(v.w, WithinAbs(4.0f, EPSILON_VEC));
}
