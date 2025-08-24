#ifndef PRISM_MESH_H
#define PRISM_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct PrismMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
};

// Creates a triangular prism mesh using center, base radius and height.
// The prism will have an equilateral triangular base in the XZ plane centered at (centerX, centerY - height/2, centerZ)
PrismMesh CreatePrismMesh(float centerX, float centerY, float centerZ, float baseRadius, float height);

// Cleanup prism mesh resources
void DestroyPrismMesh(const PrismMesh& mesh);

#endif // PRISM_MESH_H
