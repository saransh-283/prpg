#ifndef PRISM_MESH_H
#define PRISM_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct PrismMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a triangular prism mesh using triangulation
PrismMesh CreatePrismMesh(const float* vertices, size_t vertexCount, const unsigned int* indices, size_t indexCount);

// Cleanup prism mesh resources
void DestroyPrismMesh(const PrismMesh& mesh);

#endif // PRISM_MESH_H
