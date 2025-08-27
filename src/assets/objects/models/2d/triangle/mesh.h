#ifndef TRIANGLE_MESH_H
#define TRIANGLE_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct TriangleMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a triangle mesh using triangulation
TriangleMesh CreateTriangleMesh(const float* vertices, size_t vertexCount, const unsigned int* indices, size_t indexCount);

// Cleanup triangle mesh resources
void DestroyTriangleMesh(const TriangleMesh& mesh);

#endif // TRIANGLE_MESH_H
