#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "utils/triangulate/mesh.h"
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GLTFBone {
    std::string name;
    glm::mat4 transform;
    glm::mat4 inverseBindMatrix;
    int parentIndex;
};

struct GLTFMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
    std::vector<GLTFBone> bones;
    std::vector<glm::mat4> boneTransforms; // Current bone transformations
};

// Load a GLTF file and create mesh data
GLTFMesh LoadGLTFMesh(const std::string& filePath);

// Update bone transformations
void UpdateBoneTransform(GLTFMesh& mesh, const std::string& boneName, const glm::mat4& transform);

// Get bone index by name
int GetBoneIndex(const GLTFMesh& mesh, const std::string& boneName);

// Cleanup GLTF mesh resources
void DestroyGLTFMesh(const GLTFMesh& mesh);

#endif // GLTF_LOADER_H
