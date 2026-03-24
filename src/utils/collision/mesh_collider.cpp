#include "mesh_collider.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stack>

namespace {
constexpr int kLeafMaxTris = 8;
constexpr float kEps = 1e-5f;

static inline glm::vec2 v2xz(const glm::vec3& v) { return glm::vec2(v.x, v.z); }

static inline glm::vec2 min2(const glm::vec2& a, const glm::vec2& b) {
    return glm::vec2(std::min(a.x, b.x), std::min(a.y, b.y));
}

static inline glm::vec2 max2(const glm::vec2& a, const glm::vec2& b) {
    return glm::vec2(std::max(a.x, b.x), std::max(a.y, b.y));
}

static inline bool nearlyEqual2(const glm::vec2& a, const glm::vec2& b, float eps = 1e-4f) {
    return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps;
}
} // namespace

void MeshColliderXZ::Clear() {
    m_tris.clear();
    m_triOrder.clear();
    m_nodes.clear();
    m_root = -1;
    m_boundsMin = glm::vec2(0.0f);
    m_boundsMax = glm::vec2(0.0f);
}

void MeshColliderXZ::BuildFromMeshes(
    const std::vector<const std::vector<float>*>& vertexStreams,
    const std::vector<const std::vector<unsigned int>*>& indexStreams,
    bool keepMostlyVertical,
    float normalYMaxAbs) {

    Clear();

    if (vertexStreams.size() != indexStreams.size()) return;

    auto addMesh = [&](const std::vector<float>& vertices, const std::vector<unsigned int>& indices) {
        if (vertices.empty() || indices.empty()) return;
        if ((vertices.size() % 3) != 0) return;
        if ((indices.size() % 3) != 0) return;

        const size_t vertCount = vertices.size() / 3;

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const unsigned int ia = indices[i + 0];
            const unsigned int ib = indices[i + 1];
            const unsigned int ic = indices[i + 2];
            if (ia >= vertCount || ib >= vertCount || ic >= vertCount) continue;

            const glm::vec3 a(vertices[ia * 3 + 0], vertices[ia * 3 + 1], vertices[ia * 3 + 2]);
            const glm::vec3 b(vertices[ib * 3 + 0], vertices[ib * 3 + 1], vertices[ib * 3 + 2]);
            const glm::vec3 c(vertices[ic * 3 + 0], vertices[ic * 3 + 1], vertices[ic * 3 + 2]);

            const glm::vec3 ab = b - a;
            const glm::vec3 ac = c - a;
            const glm::vec3 n = glm::cross(ab, ac);
            const float nLen2 = glm::dot(n, n);
            if (nLen2 < 1e-10f) continue;

            if (keepMostlyVertical) {
                const float invLen = 1.0f / std::sqrt(nLen2);
                const float ny = std::abs(n.y * invLen);
                if (ny > normalYMaxAbs) continue;
            }

            Triangle t;
            t.a = a;
            t.b = b;
            t.c = c;

            t.minY = std::min(a.y, std::min(b.y, c.y));
            t.maxY = std::max(a.y, std::max(b.y, c.y));
            t.normalYAbs = std::abs(n.y) / std::sqrt(nLen2);
            const glm::vec2 axz = v2xz(a);
            const glm::vec2 bxz = v2xz(b);
            const glm::vec2 cxz = v2xz(c);
            t.minXZ = min2(axz, min2(bxz, cxz));
            t.maxXZ = max2(axz, max2(bxz, cxz));

            // Tiny padding to avoid missing due to float jitter.
            t.minXZ -= glm::vec2(kEps);
            t.maxXZ += glm::vec2(kEps);

            m_tris.push_back(t);
        }
    };

    for (size_t i = 0; i < vertexStreams.size(); ++i) {
        if (!vertexStreams[i] || !indexStreams[i]) continue;
        addMesh(*vertexStreams[i], *indexStreams[i]);
    }

    if (m_tris.empty()) return;

    // Overall bounds.
    m_boundsMin = m_tris[0].minXZ;
    m_boundsMax = m_tris[0].maxXZ;
    for (const auto& t : m_tris) {
        m_boundsMin = min2(m_boundsMin, t.minXZ);
        m_boundsMax = max2(m_boundsMax, t.maxXZ);
    }

    m_triOrder.resize(m_tris.size());
    std::iota(m_triOrder.begin(), m_triOrder.end(), 0);

    m_nodes.reserve(std::max<size_t>(1, m_tris.size() * 2));
    m_root = BuildNode(0, static_cast<int>(m_triOrder.size()));
}

int MeshColliderXZ::BuildNode(int start, int count) {
    Node node;
    node.start = start;
    node.count = count;

    // Bounds.
    glm::vec2 bmin( 1e30f);
    glm::vec2 bmax(-1e30f);
    glm::vec2 cmin( 1e30f);
    glm::vec2 cmax(-1e30f);

    for (int i = 0; i < count; ++i) {
        const Triangle& t = m_tris[m_triOrder[start + i]];
        bmin = min2(bmin, t.minXZ);
        bmax = max2(bmax, t.maxXZ);
        const glm::vec2 centroid = (t.minXZ + t.maxXZ) * 0.5f;
        cmin = min2(cmin, centroid);
        cmax = max2(cmax, centroid);
    }

    node.minXZ = bmin;
    node.maxXZ = bmax;

    const int myIndex = static_cast<int>(m_nodes.size());
    m_nodes.push_back(node);

    if (count <= kLeafMaxTris) {
        return myIndex;
    }

    const glm::vec2 extent = cmax - cmin;
    const int axis = (extent.x >= extent.y) ? 0 : 1;

    // If centroids are degenerate, make a leaf.
    if ((axis == 0 && extent.x < 1e-6f) || (axis == 1 && extent.y < 1e-6f)) {
        return myIndex;
    }

    const int mid = start + count / 2;
    std::nth_element(
        m_triOrder.begin() + start,
        m_triOrder.begin() + mid,
        m_triOrder.begin() + (start + count),
        [&](int a, int b) {
            const Triangle& ta = m_tris[a];
            const Triangle& tb = m_tris[b];
            const glm::vec2 ca = (ta.minXZ + ta.maxXZ) * 0.5f;
            const glm::vec2 cb = (tb.minXZ + tb.maxXZ) * 0.5f;
            return axis == 0 ? (ca.x < cb.x) : (ca.y < cb.y);
        });

    const int leftCount = mid - start;
    const int rightCount = (start + count) - mid;

    const int leftIdx = BuildNode(start, leftCount);
    const int rightIdx = BuildNode(mid, rightCount);

    m_nodes[myIndex].left = leftIdx;
    m_nodes[myIndex].right = rightIdx;
    m_nodes[myIndex].count = 0; // mark as internal

    return myIndex;
}

float MeshColliderXZ::Dist2PointAabbXZ(const glm::vec2& p, const glm::vec2& bmin, const glm::vec2& bmax) {
    const float cx = std::clamp(p.x, bmin.x, bmax.x);
    const float cz = std::clamp(p.y, bmin.y, bmax.y);
    const float dx = p.x - cx;
    const float dz = p.y - cz;
    return dx * dx + dz * dz;
}

bool MeshColliderXZ::CircleIntersectsAabbXZ(const glm::vec2& center, float r2, const glm::vec2& bmin, const glm::vec2& bmax) {
    return Dist2PointAabbXZ(center, bmin, bmax) <= r2;
}

float MeshColliderXZ::Dist2PointSegmentXZ(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float ab2 = glm::dot(ab, ab);
    if (ab2 <= 1e-12f) {
        const glm::vec2 d = p - a;
        return glm::dot(d, d);
    }
    float t = glm::dot(p - a, ab) / ab2;
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec2 q = a + ab * t;
    const glm::vec2 d = p - q;
    return glm::dot(d, d);
}

float MeshColliderXZ::Dist2PointTriangleXZ(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    // If inside triangle, distance is 0.
    const glm::vec2 v0 = b - a;
    const glm::vec2 v1 = c - a;
    const glm::vec2 v2 = p - a;

    const float den = v0.x * v1.y - v0.y * v1.x;
    if (std::abs(den) > 1e-12f) {
        const float invDen = 1.0f / den;
        const float u = (v2.x * v1.y - v2.y * v1.x) * invDen;
        const float v = (v0.x * v2.y - v0.y * v2.x) * invDen;
        if (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f) {
            return 0.0f;
        }
    }

    // Else, min distance to edges.
    float d2 = Dist2PointSegmentXZ(p, a, b);
    d2 = std::min(d2, Dist2PointSegmentXZ(p, b, c));
    d2 = std::min(d2, Dist2PointSegmentXZ(p, c, a));
    return d2;
}

bool MeshColliderXZ::TriangleIntersectsCircleXZ(int triIdx, const glm::vec2& center, float r2) const {
    const Triangle& t = m_tris[triIdx];

    // AABB quick reject.
    if (!CircleIntersectsAabbXZ(center, r2, t.minXZ, t.maxXZ)) return false;

    const glm::vec2 axz = v2xz(t.a);
    const glm::vec2 bxz = v2xz(t.b);
    const glm::vec2 cxz = v2xz(t.c);

    // Deduplicate projected points (walls are typically degenerate in XZ).
    glm::vec2 p0 = axz;
    glm::vec2 p1 = bxz;
    glm::vec2 p2 = cxz;

    std::vector<glm::vec2> uniq;
    uniq.reserve(3);
    auto pushUniq = [&](const glm::vec2& p) {
        for (const auto& q : uniq) {
            if (nearlyEqual2(p, q)) return;
        }
        uniq.push_back(p);
    };

    pushUniq(p0);
    pushUniq(p1);
    pushUniq(p2);

    float d2 = 1e30f;
    if (uniq.size() == 1) {
        const glm::vec2 d = center - uniq[0];
        d2 = glm::dot(d, d);
    } else if (uniq.size() == 2) {
        d2 = Dist2PointSegmentXZ(center, uniq[0], uniq[1]);
    } else {
        d2 = Dist2PointTriangleXZ(center, uniq[0], uniq[1], uniq[2]);
    }

    return d2 <= r2;
}

bool MeshColliderXZ::TriangleIntersectsCircleXZ(int triIdx, const glm::vec2& center, float r2, float yMin, float yMax) const {
    const Triangle& t = m_tris[triIdx];

    if (yMin > yMax) std::swap(yMin, yMax);

    // Vertical span reject. Treat touching as overlap to avoid tiny gaps.
    // If caller uses infinities, this naturally becomes a no-op.
    if (t.maxY < yMin || t.minY > yMax) return false;

    return TriangleIntersectsCircleXZ(triIdx, center, r2);
}

bool MeshColliderXZ::IntersectsCircleXZ(const glm::vec2& center, float radius) const {
    if (m_root < 0 || m_nodes.empty() || m_tris.empty()) return false;

    const float r = std::max(0.0f, radius);
    const float r2 = r * r;

    // Root AABB quick reject.
    if (!CircleIntersectsAabbXZ(center, r2, m_nodes[m_root].minXZ, m_nodes[m_root].maxXZ)) return false;

    std::stack<int> st;
    st.push(m_root);

    while (!st.empty()) {
        const int ni = st.top();
        st.pop();

        const Node& n = m_nodes[ni];
        if (!CircleIntersectsAabbXZ(center, r2, n.minXZ, n.maxXZ)) continue;

        const bool isLeaf = (n.left < 0 && n.right < 0) || (n.count > 0);
        if (isLeaf) {
            const int start = n.start;
            const int count = (n.count > 0) ? n.count : 0;
            for (int i = 0; i < count; ++i) {
                const int triIdx = m_triOrder[start + i];
                if (TriangleIntersectsCircleXZ(triIdx, center, r2)) return true;
            }
        } else {
            if (n.left >= 0) st.push(n.left);
            if (n.right >= 0) st.push(n.right);
        }
    }

    return false;
}

bool MeshColliderXZ::IntersectsCircleXZ(const glm::vec2& center, float radius, float yMin, float yMax) const {
    if (m_root < 0 || m_nodes.empty() || m_tris.empty()) return false;

    const float r = std::max(0.0f, radius);
    const float r2 = r * r;

    if (yMin > yMax) std::swap(yMin, yMax);

    // Root AABB quick reject.
    if (!CircleIntersectsAabbXZ(center, r2, m_nodes[m_root].minXZ, m_nodes[m_root].maxXZ)) return false;

    std::stack<int> st;
    st.push(m_root);

    while (!st.empty()) {
        const int ni = st.top();
        st.pop();

        const Node& n = m_nodes[ni];
        if (!CircleIntersectsAabbXZ(center, r2, n.minXZ, n.maxXZ)) continue;

        const bool isLeaf = (n.left < 0 && n.right < 0) || (n.count > 0);
        if (isLeaf) {
            const int start = n.start;
            const int count = (n.count > 0) ? n.count : 0;
            for (int i = 0; i < count; ++i) {
                const int triIdx = m_triOrder[start + i];
                if (TriangleIntersectsCircleXZ(triIdx, center, r2, yMin, yMax)) return true;
            }
        } else {
            if (n.left >= 0) st.push(n.left);
            if (n.right >= 0) st.push(n.right);
        }
    }

    return false;
}

bool MeshColliderXZ::SampleTriangleYAtXZ(const Triangle& t, const glm::vec2& point, float& outY) {
    const glm::vec2 a = v2xz(t.a);
    const glm::vec2 b = v2xz(t.b);
    const glm::vec2 c = v2xz(t.c);

    // Barycentric in XZ plane.
    const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(den) <= 1e-8f) return false;

    const float w1 = ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / den;
    const float w2 = ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / den;
    const float w3 = 1.0f - w1 - w2;

    constexpr float kInsideEps = 1e-4f;
    if (w1 < -kInsideEps || w2 < -kInsideEps || w3 < -kInsideEps) return false;

    outY = w1 * t.a.y + w2 * t.b.y + w3 * t.c.y;
    return std::isfinite(outY);
}

bool MeshColliderXZ::SampleFloorAndCeilingAtXZ(const glm::vec2& point,
                                               float feetY,
                                               float maxStepUp,
                                               float& outFloorY,
                                               float& outCeilingY,
                                               float minNormalYAbs) const {
    outFloorY = -std::numeric_limits<float>::infinity();
    outCeilingY = std::numeric_limits<float>::infinity();

    if (m_root < 0 || m_nodes.empty() || m_tris.empty()) return false;
    if (minNormalYAbs < 0.0f) minNormalYAbs = 0.0f;

    // Point vs root bounds quick reject.
    const Node& root = m_nodes[m_root];
    if (point.x < root.minXZ.x || point.x > root.maxXZ.x ||
        point.y < root.minXZ.y || point.y > root.maxXZ.y) {
        return false;
    }

    std::stack<int> st;
    st.push(m_root);

    const float maxAcceptFloor = feetY + std::max(0.0f, maxStepUp);
    bool foundAnySurface = false;

    while (!st.empty()) {
        const int ni = st.top();
        st.pop();

        const Node& n = m_nodes[ni];
        if (point.x < n.minXZ.x || point.x > n.maxXZ.x ||
            point.y < n.minXZ.y || point.y > n.maxXZ.y) {
            continue;
        }

        const bool isLeaf = (n.left < 0 && n.right < 0) || (n.count > 0);
        if (isLeaf) {
            const int start = n.start;
            const int count = (n.count > 0) ? n.count : 0;
            for (int i = 0; i < count; ++i) {
                const Triangle& t = m_tris[m_triOrder[start + i]];
                if (t.normalYAbs < minNormalYAbs) continue;
                if (point.x < t.minXZ.x || point.x > t.maxXZ.x ||
                    point.y < t.minXZ.y || point.y > t.maxXZ.y) {
                    continue;
                }

                float y = 0.0f;
                if (!SampleTriangleYAtXZ(t, point, y)) continue;
                foundAnySurface = true;

                if (y <= maxAcceptFloor && y > outFloorY) {
                    outFloorY = y;
                }
                if (y > feetY + 1e-3f && y < outCeilingY) {
                    outCeilingY = y;
                }
            }
        } else {
            if (n.left >= 0) st.push(n.left);
            if (n.right >= 0) st.push(n.right);
        }
    }

    return foundAnySurface;
}
