#include "frustum.h"

#include <algorithm>

// Gribb/Hartmann method: extract planes directly from the rows of VP.
Frustum::Frustum(const glm::mat4& vp) {
    // Row vectors of the transposed VP matrix (GLM is column-major).
    const glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    const glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    const glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    const glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    planes[0] = r3 + r0; // Left
    planes[1] = r3 - r0; // Right
    planes[2] = r3 + r1; // Bottom
    planes[3] = r3 - r1; // Top
    planes[4] = r3 + r2; // Near
    planes[5] = r3 - r2; // Far

    // Normalize so that (A,B,C) is unit-length – makes distance checks correct.
    for (auto& p : planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 1e-8f) p /= len;
    }
}

bool Frustum::TestAABB(const AABB& box) const {
    for (const auto& p : planes) {
        // Pick the corner of the AABB that is most in the direction of the
        // plane normal (the "positive vertex").
        glm::vec3 pv;
        pv.x = (p.x >= 0.0f) ? box.max.x : box.min.x;
        pv.y = (p.y >= 0.0f) ? box.max.y : box.min.y;
        pv.z = (p.z >= 0.0f) ? box.max.z : box.min.z;

        // If the positive vertex is outside this plane the AABB is fully outside.
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f)
            return false;
    }
    return true;
}

bool Frustum::TestSphere(const glm::vec3& center, float radius) const {
    for (const auto& p : planes) {
        float dist = glm::dot(glm::vec3(p), center) + p.w;
        if (dist < -radius)
            return false;
    }
    return true;
}
