#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/math/math.h"

using namespace gws::math;
using Catch::Matchers::WithinAbs;

constexpr float EPSILON_XFORM = 1e-5f;

TEST_CASE("Transform construction", "[math][transform]") {
    Transform t;
    REQUIRE(t.position.x == 0.0f);
    REQUIRE(t.position.y == 0.0f);
    REQUIRE(t.position.z == 0.0f);
    REQUIRE(t.scale.x == 1.0f);
    REQUIRE(t.scale.y == 1.0f);
    REQUIRE(t.scale.z == 1.0f);
}

TEST_CASE("Transform with position", "[math][transform]") {
    Transform t(Vec3(1.0f, 2.0f, 3.0f));
    REQUIRE_THAT(t.position.x, WithinAbs(1.0f, EPSILON_XFORM));
    REQUIRE_THAT(t.position.y, WithinAbs(2.0f, EPSILON_XFORM));
    REQUIRE_THAT(t.position.z, WithinAbs(3.0f, EPSILON_XFORM));
}

TEST_CASE("Transform comparison", "[math][transform]") {
    Transform t1(Vec3(1.0f, 2.0f, 3.0f));
    Transform t2(Vec3(1.0f, 2.0f, 3.0f));
    Transform t3(Vec3(4.0f, 5.0f, 6.0f));
    
    REQUIRE(t1 == t2);
    REQUIRE(t1 != t3);
}

TEST_CASE("Transform directions", "[math][transform]") {
    Transform t;
    
    Vec3 fwd = t.forward();
    REQUIRE_THAT(fwd.z, WithinAbs(-1.0f, EPSILON_XFORM));
    
    Vec3 right = t.right();
    REQUIRE_THAT(right.x, WithinAbs(1.0f, EPSILON_XFORM));
    
    Vec3 up = t.up();
    REQUIRE_THAT(up.y, WithinAbs(1.0f, EPSILON_XFORM));
}
