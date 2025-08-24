#ifndef CONE_MESH_H
#define CONE_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct ConeMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a cone mesh using triangulation
ConeMesh CreateConeMesh(float centerX, float centerY, float centerZ, float radius, float height, int segments = 12);

// Cleanup cone mesh resources
void DestroyConeMesh(const ConeMesh& mesh);

#endif // CONE_MESH_H
