#pragma once

#include <glm/glm.hpp>

// Axis-aligned bounding box (min corner, max corner).
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// View-frustum extracted from a combined projection*view matrix.
// Supports AABB and sphere intersection tests for culling.
class Frustum {
public:
    Frustum() = default;

    // Extract the six frustum planes from a VP (projection * view) matrix.
    // Each plane is stored as (A, B, C, D) with the convention Ax+By+Cz+D >= 0
    // meaning "inside or on the plane".
    explicit Frustum(const glm::mat4& vp);

    // Returns true if the AABB is at least partially inside the frustum.
    bool TestAABB(const AABB& box) const;

    // Returns true if the sphere (center, radius) is at least partially inside.
    bool TestSphere(const glm::vec3& center, float radius) const;

private:
    // Left, Right, Bottom, Top, Near, Far
    glm::vec4 planes[6]{};
};
