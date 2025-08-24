#ifndef SQUARE_MESH_H
#define SQUARE_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct SquareMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a square mesh using triangulation
SquareMesh CreateSquareMesh(const float* vertices, size_t vertexCount, const unsigned int* indices, size_t indexCount);

// Cleanup square mesh resources
void DestroySquareMesh(const SquareMesh& mesh);

#endif // SQUARE_MESH_H
