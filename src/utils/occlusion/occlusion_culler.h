#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <utils/frustum/frustum.h>

#include <unordered_map>
#include <vector>

// ============================================================================
// Hardware Occlusion Culling
// ============================================================================
// Uses OpenGL occlusion queries with a one-frame temporal lag:
//   Frame N  : render each chunk inside a query; collect results.
//   Frame N+1: skip chunks whose query returned 0 visible samples.
//
// New / never-queried chunks are treated as visible (conservative).
// A lightweight depth-only AABB proxy is drawn for occluded chunks so they
// can re-appear when the camera moves.
// ============================================================================

class OcclusionCuller {
public:
    OcclusionCuller();
    ~OcclusionCuller();

    // Call at the start of each frame before any geometry rendering.
    // Collects query results from the previous frame and swaps buffers.
    void BeginFrame();

    // Returns true if the chunk identified by |key| should be rendered.
    // Chunks that have never been queried are conservatively assumed visible.
    bool IsChunkVisible(long long key) const;

    // Begin an occlusion query for a chunk.  All draw calls between
    // BeginQuery / EndQuery will be counted.
    void BeginQuery(long long key);
    void EndQuery();

    // Render a depth-only AABB proxy for an occluded chunk so the query
    // updates even when the full geometry is skipped.
    // Requires a valid shader with model/view/projection uniforms already bound.
    void DrawAABBProxy(const AABB& box, GLint modelLoc);

    // Call at the end of the frame (after all queries have been issued).
    void EndFrame();

    // Release all GL resources.
    void Cleanup();

    // One-time initialisation of the shared AABB proxy VAO/VBO.
    void InitProxyMesh();

private:
    // Per-chunk query state.
    struct QueryEntry {
        GLuint queryId = 0;      // GL query object
        bool   visible = true;   // considered visible after grace period
        bool   active  = false;  // query was issued this frame
        int    occludedFrames = 0; // consecutive frames with 0 samples
    };

    // Allocate (or reuse) a query object for a given chunk.
    QueryEntry& GetOrCreate(long long key);

    std::unordered_map<long long, QueryEntry> queries;

    // Pool of reusable GL query IDs so we don't gen/delete every frame.
    std::vector<GLuint> queryPool;

    // Shared unit-cube VAO/VBO for AABB proxy rendering.
    GLuint proxyVAO = 0;
    GLuint proxyVBO = 0;
    bool   proxyReady = false;
};

