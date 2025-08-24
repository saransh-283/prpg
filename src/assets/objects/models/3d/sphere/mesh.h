#ifndef SPHERE_MESH_H
#define SPHERE_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct SphereMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
    unsigned int vertexCount; // non-indexed vertex count when created without indices
};

// Creates a sphere mesh using triangulation
SphereMesh CreateSphereMesh(float centerX, float centerY, float centerZ, float radius, int stacks = 8, int slices = 12);

// Cleanup sphere mesh resources
void DestroySphereMesh(const SphereMesh& mesh);

#endif // SPHERE_MESH_H
