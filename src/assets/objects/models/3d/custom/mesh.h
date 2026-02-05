#ifndef CUSTOM_MESH_H
#define CUSTOM_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <utils/gltf/gltf_loader.h>
#include <vector>
#include <glm/glm.hpp>

struct CustomMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;

    bool hasAabb = false;
    glm::vec3 aabbMin = glm::vec3(0.0f);
    glm::vec3 aabbMax = glm::vec3(0.0f);
};

// Creates a custom mesh using GLTF file at the provided path
CustomMesh CreateCustomMesh(const std::string& modelPath);

// Cleanup custom mesh resources
void DestroyCustomMesh(const CustomMesh& mesh);

#endif // CUSTOM_MESH_H
