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
    // Images embedded or referenced by the glTF/glb
    struct Image {
        std::string name;
        std::string mimeType;
        int width;
        int height;
        std::vector<unsigned char> data; // raw pixel data (as provided by tinygltf)
    };

    // Textures that reference images
    struct Texture {
        std::string name;
        int sourceImage; // index into images vector
    };

    // Basic subset of material properties useful for the renderer
    struct Material {
        std::string name;
        int baseColorTexture; // index into textures vector or -1
        glm::vec4 baseColorFactor;
        float metallicFactor;
        float roughnessFactor;
    };

    // Simple representation of an animation channel/sampler
    struct AnimationChannel {
        int targetNode;
        std::string targetPath; // "translation", "rotation", "scale", "weights"
        std::vector<float> inputTimes; // keyframe times
        std::vector<float> outputValues; // flattened outputs (e.g., vec3 or quat values)
        std::string interpolation; // e.g., "LINEAR", "STEP", "CUBICSPLINE"
    };

    struct Animation {
        std::string name;
        std::vector<AnimationChannel> channels;
    };

    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Animation> animations;
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
