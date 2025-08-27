#ifndef CYLINDER_MESH_H
#define CYLINDER_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct CylinderMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a cylinder mesh using triangulation
CylinderMesh CreateCylinderMesh(float centerX, float centerY, float centerZ, float radius, float height, int segments = 12);

// Cleanup cylinder mesh resources
void DestroyCylinderMesh(const CylinderMesh& mesh);

#endif // CYLINDER_MESH_H
