#ifndef CUBE_MESH_H
#define CUBE_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct CubeMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
};

// Creates a cuboid mesh using center and dimensions (length, breadth, height).
// If breadth or height are omitted (pass 0), they default to 'length' to create a cube.
CubeMesh CreateCubeMesh(float centerX, float centerY, float centerZ, float length, float breadth = 0.0f, float height = 0.0f);

// Cleanup cube mesh resources
void DestroyCubeMesh(const CubeMesh& mesh);

#endif // CUBE_MESH_H
