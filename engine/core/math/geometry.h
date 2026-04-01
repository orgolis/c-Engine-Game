#pragma once

#include "vec3.h"
#include <algorithm>

namespace gws::math {

// ====================
// Geometric Primitives and Intersection Tests
// ====================

// Ray - infinite line from origin in direction
struct Ray {
    Vec3 origin;
    Vec3 direction;  // should be normalized
    
    Ray() : origin(0.0f), direction(0.0f, 0.0f, 1.0f) {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(d.normalized()) {}
    
    Vec3 at(float t) const { return origin + direction * t; }
};

// Plane - defined by normal and distance from origin
struct Plane {
    Vec3 normal;
    float distance;
    
    Plane() : normal(0.0f, 1.0f, 0.0f), distance(0.0f) {}
    Plane(const Vec3& n, float d) : normal(n.normalized()), distance(d) {}
    
    // Signed distance from point to plane
    float distance_to_point(const Vec3& p) const {
        return normal.dot(p) - distance;
    }
};

// Axis-Aligned Bounding Box
struct AABB {
    Vec3 min;
    Vec3 max;
    
    AABB() : min(0.0f), max(0.0f) {}
    AABB(const Vec3& minimum, const Vec3& maximum) : min(minimum), max(maximum) {}
    
    // Center of the box
    Vec3 center() const { return (min + max) * 0.5f; }
    
    // Half-extents (from center to corner)
    Vec3 extents() const { return (max - min) * 0.5f; }
    
    // Expand to include a point
    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }
    
    // Check if point is inside
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
};

// Sphere - center and radius
struct Sphere {
    Vec3 center;
    float radius;
    
    Sphere() : center(0.0f), radius(1.0f) {}
    Sphere(const Vec3& c, float r) : center(c), radius(r) {}
    
    // Check if point is inside
    bool contains(const Vec3& p) const {
        return (p - center).length_squared() <= radius * radius;
    }
};

// Capsule - cylinder with hemispherical ends (common for character collision)
struct Capsule {
    Vec3 start;
    Vec3 end;
    float radius;
    
    Capsule() : start(0.0f), end(0.0f, 1.0f, 0.0f), radius(0.5f) {}
    Capsule(const Vec3& s, const Vec3& e, float r) : start(s), end(e), radius(r) {}
    
    Vec3 center() const { return (start + end) * 0.5f; }
    float height() const { return (end - start).length(); }
};

// Frustum - 6 planes defining a view frustum (from camera matrix)
struct Frustum {
    Plane planes[6];  // near, far, left, right, top, bottom
    
    bool contains_sphere(const Sphere& s) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].distance_to_point(s.center) < -s.radius) {
                return false;
            }
        }
        return true;
    }
};

// ====================
// Intersection Tests
// ====================

// Ray-Sphere intersection
// Returns true and sets t_out to the closest intersection distance
inline bool ray_intersects_sphere(const Ray& ray, const Sphere& sphere, float& t_out) {
    Vec3 oc = ray.origin - sphere.center;
    float a = ray.direction.dot(ray.direction);
    float b = 2.0f * oc.dot(ray.direction);
    float c = oc.dot(oc) - sphere.radius * sphere.radius;
    
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) {
        return false;
    }
    
    float sqrt_disc = std::sqrt(discriminant);
    float t1 = (-b - sqrt_disc) / (2.0f * a);
    float t2 = (-b + sqrt_disc) / (2.0f * a);
    
    if (t1 >= 0.0f) {
        t_out = t1;
        return true;
    }
    if (t2 >= 0.0f) {
        t_out = t2;
        return true;
    }
    
    return false;
}

// Ray-AABB intersection (slab method)
inline bool ray_intersects_aabb(const Ray& ray, const AABB& box, float& t_out) {
    float t_min = -std::numeric_limits<float>::max();
    float t_max = std::numeric_limits<float>::max();
    
    for (int i = 0; i < 3; ++i) {
        float ray_dir = (&ray.direction.x)[i];
        float ray_org = (&ray.origin.x)[i];
        
        if (approximately_zero(ray_dir)) {
            if (ray_org < (&box.min.x)[i] || ray_org > (&box.max.x)[i]) {
                return false;
            }
        } else {
            float t1 = ((&box.min.x)[i] - ray_org) / ray_dir;
            float t2 = ((&box.max.x)[i] - ray_org) / ray_dir;
            
            if (t1 > t2) std::swap(t1, t2);
            
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
            
            if (t_min > t_max) {
                return false;
            }
        }
    }
    
    t_out = t_min > 0.0f ? t_min : t_max;
    return t_out >= 0.0f;
}

// AABB-AABB intersection
inline bool aabb_intersects_aabb(const AABB& a, const AABB& b) {
    return !(a.max.x < b.min.x || a.min.x > b.max.x ||
             a.max.y < b.min.y || a.min.y > b.max.y ||
             a.max.z < b.min.z || a.min.z > b.max.z);
}

// Sphere-Sphere intersection
inline bool sphere_intersects_sphere(const Sphere& a, const Sphere& b) {
    float dist_sq = (a.center - b.center).length_squared();
    float combined_radius = a.radius + b.radius;
    return dist_sq <= combined_radius * combined_radius;
}

// Sphere-AABB intersection
inline bool sphere_intersects_aabb(const Sphere& sphere, const AABB& box) {
    // Find closest point on AABB to sphere center
    Vec3 closest = sphere.center;
    closest.x = clamp(closest.x, box.min.x, box.max.x);
    closest.y = clamp(closest.y, box.min.y, box.max.y);
    closest.z = clamp(closest.z, box.min.z, box.max.z);
    
    float dist_sq = (closest - sphere.center).length_squared();
    return dist_sq <= sphere.radius * sphere.radius;
}

// Point in Capsule (approximate)
inline bool capsule_contains_point(const Capsule& capsule, const Vec3& point) {
    // Project point onto capsule line
    Vec3 axis = (capsule.end - capsule.start).normalized();
    float t = (point - capsule.start).dot(axis);
    
    // Clamp to capsule segment
    t = clamp(t, 0.0f, (capsule.end - capsule.start).length());
    
    Vec3 closest = capsule.start + axis * t;
    return (point - closest).length() <= capsule.radius;
}

}  // namespace gws::math
