#ifndef CUSTOM_MESH_H
#define CUSTOM_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include "utils/gltf/gltf_loader.h"
#include <vector>

struct CustomMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a custom mesh using GLTF file
CustomMesh CreateCustomMesh();

// Cleanup custom mesh resources
void DestroyCustomMesh(const CustomMesh& mesh);

#endif // CUSTOM_MESH_H
