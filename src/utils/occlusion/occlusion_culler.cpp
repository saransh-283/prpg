#include "occlusion_culler.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

// ============================================================================
// Cube geometry for AABB proxy (36 verts, position-only, CCW winding)
// ============================================================================
static const float kCubeVerts[] = {
    // Back face
    -0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,   0.5f, -0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,  -0.5f,  0.5f, -0.5f,
    // Front face
    -0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,
    // Left face
    -0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,  -0.5f, -0.5f, -0.5f,
    -0.5f, -0.5f, -0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f,  0.5f,  0.5f,
    // Right face
     0.5f,  0.5f,  0.5f,   0.5f, -0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,   0.5f,  0.5f,  0.5f,   0.5f, -0.5f,  0.5f,
    // Bottom face
    -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f,   0.5f, -0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,  -0.5f, -0.5f,  0.5f,  -0.5f, -0.5f, -0.5f,
    // Top face
    -0.5f,  0.5f, -0.5f,   0.5f,  0.5f,  0.5f,   0.5f,  0.5f, -0.5f,
     0.5f,  0.5f,  0.5f,  -0.5f,  0.5f, -0.5f,  -0.5f,  0.5f,  0.5f,
};

// ============================================================================
// Construction / destruction
// ============================================================================

OcclusionCuller::OcclusionCuller() = default;

OcclusionCuller::~OcclusionCuller() {
    Cleanup();
}

void OcclusionCuller::Cleanup() {
    for (auto& [key, entry] : queries) {
        if (entry.queryId) glDeleteQueries(1, &entry.queryId);
    }
    queries.clear();

    for (GLuint q : queryPool) {
        glDeleteQueries(1, &q);
    }
    queryPool.clear();

    if (proxyVAO) { glDeleteVertexArrays(1, &proxyVAO); proxyVAO = 0; }
    if (proxyVBO) { glDeleteBuffers(1, &proxyVBO); proxyVBO = 0; }
    proxyReady = false;
}

// ============================================================================
// Proxy mesh initialisation
// ============================================================================

void OcclusionCuller::InitProxyMesh() {
    if (proxyReady) return;

    glGenVertexArrays(1, &proxyVAO);
    glGenBuffers(1, &proxyVBO);

    glBindVertexArray(proxyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, proxyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
    proxyReady = true;
}

// ============================================================================
// Frame lifecycle
// ============================================================================

void OcclusionCuller::BeginFrame() {
    // Collect results from queries that were active last frame.
    for (auto& [key, entry] : queries) {
        if (!entry.active) continue;

        GLuint result = 0;
        // GL_QUERY_RESULT blocks until the GPU has finished – acceptable for
        // one-frame-old queries since the GPU is well ahead by now.
        glGetQueryObjectuiv(entry.queryId, GL_QUERY_RESULT, &result);

        if (result > 0) {
            // Chunk had visible samples – immediately mark visible.
            entry.visible = true;
            entry.occludedFrames = 0;
        } else {
            // Chunk had zero samples.  Require several consecutive
            // occluded frames before actually hiding it to avoid
            // flickering from rendering-order artifacts.
            ++entry.occludedFrames;
            static constexpr int kGraceFrames = 3;
            entry.visible = (entry.occludedFrames < kGraceFrames);
        }

        entry.active = false;

        // Return the query object to the pool for reuse.
        queryPool.push_back(entry.queryId);
        entry.queryId = 0;
    }
}

bool OcclusionCuller::IsChunkVisible(long long key) const {
    auto it = queries.find(key);
    if (it == queries.end()) return true; // never queried → visible
    return it->second.visible;
}

// ============================================================================
// Query issuing
// ============================================================================

OcclusionCuller::QueryEntry& OcclusionCuller::GetOrCreate(long long key) {
    auto& entry = queries[key];
    // Allocate a query object from the pool (or create a new one).
    if (entry.queryId == 0) {
        if (!queryPool.empty()) {
            entry.queryId = queryPool.back();
            queryPool.pop_back();
        } else {
            glGenQueries(1, &entry.queryId);
        }
    }
    return entry;
}

void OcclusionCuller::BeginQuery(long long key) {
    auto& entry = GetOrCreate(key);
    glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, entry.queryId);
    entry.active = true;
}

void OcclusionCuller::EndQuery() {
    glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
}

// ============================================================================
// AABB proxy rendering (depth-only)
// ============================================================================

void OcclusionCuller::DrawAABBProxy(const AABB& box, GLint modelLoc) {
    if (!proxyReady) return;

    // Build a model matrix that maps the unit cube [-0.5, 0.5]³ → the AABB.
    const glm::vec3 center = (box.min + box.max) * 0.5f;
    const glm::vec3 size   = box.max - box.min;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);

    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    // Draw with colour AND depth writes disabled so the proxy is purely
    // a query test – it never pollutes the G-buffer or depth buffer.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(proxyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

// ============================================================================
// End of frame
// ============================================================================

void OcclusionCuller::EndFrame() {
    // Remove entries for chunks that haven't been active for a while.
    // For now this is a no-op; stale entries are cheap (a few bytes each).
    // A periodic sweep could be added if memory becomes a concern.
}

