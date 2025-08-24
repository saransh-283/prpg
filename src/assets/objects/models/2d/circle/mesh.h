#ifndef CIRCLE_MESH_H
#define CIRCLE_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include <vector>

struct CircleMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a circle mesh using triangulation
CircleMesh CreateCircleMesh(float centerX, float centerY, float centerZ, float radius, int segments = 16);

// Cleanup circle mesh resources
void DestroyCircleMesh(const CircleMesh& mesh);

#endif // CIRCLE_MESH_H
