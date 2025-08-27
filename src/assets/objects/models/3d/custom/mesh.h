#ifndef CUSTOM_MESH_H
#define CUSTOM_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <utils/gltf/gltf_loader.h>
#include <vector>

struct CustomMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a custom mesh using GLTF file at the provided path
CustomMesh CreateCustomMesh(const std::string& modelPath);

// Cleanup custom mesh resources
void DestroyCustomMesh(const CustomMesh& mesh);

#endif // CUSTOM_MESH_H
