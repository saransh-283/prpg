#pragma once

#include <array>
#include <cmath>

#include <glm/glm.hpp>

// Minimal view-frustum helpers (plane extraction + AABB/sphere intersection).
// Planes use the convention: dot(n, p) + d >= 0 means "inside".

namespace FrustumUtil {

struct Plane {
    glm::vec3 n{0.0f};
    float d{0.0f};
};

struct Frustum {
    // Order: left, right, bottom, top, near, far
    std::array<Plane, 6> planes;
};

inline glm::vec4 Row(const glm::mat4& m, int r) {
    return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
}

inline Plane NormalizePlane(const glm::vec4& p) {
    Plane out;
    out.n = glm::vec3(p);
    out.d = p.w;

    const float len = std::sqrt(glm::dot(out.n, out.n));
    if (len > 1e-8f) {
        const float inv = 1.0f / len;
        out.n *= inv;
        out.d *= inv;
    }
    return out;
}

inline Frustum ExtractFrustum(const glm::mat4& viewProj) {
    const glm::vec4 r0 = Row(viewProj, 0);
    const glm::vec4 r1 = Row(viewProj, 1);
    const glm::vec4 r2 = Row(viewProj, 2);
    const glm::vec4 r3 = Row(viewProj, 3);

    Frustum f;
    f.planes[0] = NormalizePlane(r3 + r0); // left
    f.planes[1] = NormalizePlane(r3 - r0); // right
    f.planes[2] = NormalizePlane(r3 + r1); // bottom
    f.planes[3] = NormalizePlane(r3 - r1); // top
    f.planes[4] = NormalizePlane(r3 + r2); // near
    f.planes[5] = NormalizePlane(r3 - r2); // far
    return f;
}

inline bool IntersectsAabb(const Frustum& f, const glm::vec3& bmin, const glm::vec3& bmax) {
    for (const auto& plane : f.planes) {
        const glm::vec3 p(
            (plane.n.x >= 0.0f) ? bmax.x : bmin.x,
            (plane.n.y >= 0.0f) ? bmax.y : bmin.y,
            (plane.n.z >= 0.0f) ? bmax.z : bmin.z);

        if (glm::dot(plane.n, p) + plane.d < 0.0f) {
            return false;
        }
    }
    return true;
}

inline bool IntersectsSphere(const Frustum& f, const glm::vec3& center, float radius) {
    for (const auto& plane : f.planes) {
        const float dist = glm::dot(plane.n, center) + plane.d;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

} // namespace FrustumUtil
