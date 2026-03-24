#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Simple, lightweight mesh collider optimized for XZ-plane circle queries.
//
// Intended use in this codebase:
// - Build from building wall meshes.
// - Query: does a circle at (x,z) of radius r intersect any wall triangle?
//
// Notes:
// - Narrowphase operates in XZ only.
// - Broadphase uses a BVH over triangle XZ AABBs.
class MeshColliderXZ {
public:
    struct Triangle {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        glm::vec3 c{0.0f};
        glm::vec2 minXZ{0.0f};
        glm::vec2 maxXZ{0.0f};
        float minY = 0.0f;
        float maxY = 0.0f;
        float normalYAbs = 0.0f;
    };

    MeshColliderXZ() = default;

    void Clear();

    // Build from one or more indexed triangle meshes.
    // - vertices: packed [x,y,z] floats.
    // - indices: triangle list (3 indices per triangle) referencing vertices.
    // - keepMostlyVertical: if true, keeps only triangles where |normal.y| <= normalYMaxAbs.
    void BuildFromMeshes(
        const std::vector<const std::vector<float>*>& vertexStreams,
        const std::vector<const std::vector<unsigned int>*>& indexStreams,
        bool keepMostlyVertical = true,
        float normalYMaxAbs = 0.25f);

    bool Empty() const { return m_tris.empty(); }

    // Returns overall bounds in XZ; valid only if not Empty().
    glm::vec2 BoundsMinXZ() const { return m_boundsMin; }
    glm::vec2 BoundsMaxXZ() const { return m_boundsMax; }

    // True if any triangle intersects a circle in XZ.
    bool IntersectsCircleXZ(const glm::vec2& center, float radius) const;

    // Same as above, but only considers triangles whose vertical span overlaps [yMin, yMax].
    // Useful for doorway/arch openings where geometry exists above head height.
    bool IntersectsCircleXZ(const glm::vec2& center, float radius, float yMin, float yMax) const;
    
        // Samples walkable floor/ceiling from mesh triangles at an XZ point.
        // - feetY: player's current feet height.
        // - maxStepUp: maximum upward step accepted as floor candidate.
        // - minNormalYAbs: ignores near-vertical triangles (wall faces) for this query.
        // Returns true if at least one qualifying surface triangle projects over the point.
        bool SampleFloorAndCeilingAtXZ(const glm::vec2& point,
                                       float feetY,
                                       float maxStepUp,
                                       float& outFloorY,
                                       float& outCeilingY,
                                       float minNormalYAbs = 0.35f) const;

private:
    struct Node {
        glm::vec2 minXZ{0.0f};
        glm::vec2 maxXZ{0.0f};
        int left = -1;
        int right = -1;
        int start = 0;
        int count = 0;
    };

    std::vector<Triangle> m_tris;
    std::vector<int> m_triOrder;
    std::vector<Node> m_nodes;
    int m_root = -1;

    glm::vec2 m_boundsMin{0.0f};
    glm::vec2 m_boundsMax{0.0f};

    int BuildNode(int start, int count);

    static float Dist2PointAabbXZ(const glm::vec2& p, const glm::vec2& bmin, const glm::vec2& bmax);
    static float Dist2PointSegmentXZ(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b);
    static float Dist2PointTriangleXZ(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c);
    static bool CircleIntersectsAabbXZ(const glm::vec2& center, float r2, const glm::vec2& bmin, const glm::vec2& bmax);
            static bool SampleTriangleYAtXZ(const Triangle& t, const glm::vec2& point, float& outY);

    bool TriangleIntersectsCircleXZ(int triIdx, const glm::vec2& center, float r2) const;
    bool TriangleIntersectsCircleXZ(int triIdx, const glm::vec2& center, float r2, float yMin, float yMax) const;

};
