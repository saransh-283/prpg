#ifndef CUBE_MESH_H
#define CUBE_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct CubeMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a cube mesh using triangulation
CubeMesh CreateCubeMesh(const float* vertices, size_t vertexSize, const unsigned int* indices, size_t indexCount);

// Cleanup cube mesh resources
void DestroyCubeMesh(const CubeMesh& mesh);

#endif // CUBE_MESH_H
