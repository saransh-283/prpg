#include "gltf_loader.h"
#include <iostream>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

GLTFMesh LoadGLTFMesh(const std::string& filePath) {
    GLTFMesh gltfMesh;
    gltfMesh.triangleCount = 0;
    
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);
    
    if (!warn.empty()) {
        std::cout << "GLTF Warning: " << warn << std::endl;
    }
    
    if (!err.empty()) {
        std::cerr << "GLTF Error: " << err << std::endl;
        return gltfMesh;
    }
    
    if (!ret) {
        std::cerr << "Failed to parse glTF file: " << filePath << std::endl;
        return gltfMesh;
    }

    // Load bone information from skins
    for (const auto& skin : model.skins) {
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            GLTFBone bone;
            int nodeIndex = skin.joints[i];
            bone.name = model.nodes[nodeIndex].name;
            
            // Get node transform
            auto& node = model.nodes[nodeIndex];
            glm::mat4 transform = glm::mat4(1.0f);
            
            if (node.matrix.size() == 16) {
                // Use matrix if available
                transform = glm::make_mat4(node.matrix.data());
            } else {
                // Compose from TRS
                if (node.translation.size() == 3) {
                    transform = glm::translate(transform, glm::vec3(
                        node.translation[0], node.translation[1], node.translation[2]));
                }
                if (node.rotation.size() == 4) {
                    glm::quat rotation(node.rotation[3], node.rotation[0], 
                                     node.rotation[1], node.rotation[2]);
                    transform *= glm::mat4_cast(rotation);
                }
                if (node.scale.size() == 3) {
                    transform = glm::scale(transform, glm::vec3(
                        node.scale[0], node.scale[1], node.scale[2]));
                }
            }
            
            bone.transform = transform;
            bone.parentIndex = -1; // We'll set this up later if needed
            
            // Get inverse bind matrix if available
            if (skin.inverseBindMatrices >= 0) {
                auto& accessor = model.accessors[skin.inverseBindMatrices];
                auto& bufferView = model.bufferViews[accessor.bufferView];
                auto& buffer = model.buffers[bufferView.buffer];
                
                const float* matrices = reinterpret_cast<const float*>(
                    &buffer.data[bufferView.byteOffset + accessor.byteOffset]);
                
                bone.inverseBindMatrix = glm::make_mat4(&matrices[i * 16]);
            } else {
                bone.inverseBindMatrix = glm::mat4(1.0f);
            }
            
            gltfMesh.bones.push_back(bone);
            gltfMesh.boneTransforms.push_back(transform);
        }
    }
    
    // Process each mesh in the GLTF file
    for (const auto& mesh : model.meshes) {
        for (const auto& primitive : mesh.primitives) {
            // Get position accessor
            auto posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
            auto posBufferView = model.bufferViews[posAccessor.bufferView];
            auto posBuffer = model.buffers[posBufferView.buffer];
            
            // Get indices accessor
            auto indicesAccessor = model.accessors[primitive.indices];
            auto indicesBufferView = model.bufferViews[indicesAccessor.bufferView];
            auto indicesBuffer = model.buffers[indicesBufferView.buffer];
            
            // Extract position data
            const float* positions = reinterpret_cast<const float*>(
                &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset]);
            
            // Extract indices data
            const unsigned short* indices = reinterpret_cast<const unsigned short*>(
                &indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]);
            
            // Create triangles from the indexed data
            size_t numTriangles = indicesAccessor.count / 3;
            
            for (size_t i = 0; i < numTriangles; ++i) {
                float triangleVertices[9]; // 3 vertices * 3 components each
                
                for (int j = 0; j < 3; ++j) {
                    unsigned short vertexIndex = indices[i * 3 + j];
                    triangleVertices[j * 3 + 0] = positions[vertexIndex * 3 + 0];
                    triangleVertices[j * 3 + 1] = positions[vertexIndex * 3 + 1];
                    triangleVertices[j * 3 + 2] = positions[vertexIndex * 3 + 2];
                }
                
                TriangulateMesh triangle = CreateTriangulateMesh(
                    triangleVertices, 
                    sizeof(triangleVertices), 
                    3  // 3 vertices per triangle
                );
                
                gltfMesh.triangles.push_back(triangle);
                gltfMesh.triangleCount++;
            }
        }
    }
    
    std::cout << "Loaded GLTF mesh with " << gltfMesh.triangleCount << " triangles and " << gltfMesh.bones.size() << " bones" << std::endl;
    return gltfMesh;
}

void UpdateBoneTransform(GLTFMesh& mesh, const std::string& boneName, const glm::mat4& transform) {
    int boneIndex = GetBoneIndex(mesh, boneName);
    if (boneIndex >= 0) {
        mesh.boneTransforms[boneIndex] = transform;
    }
}

int GetBoneIndex(const GLTFMesh& mesh, const std::string& boneName) {
    for (size_t i = 0; i < mesh.bones.size(); ++i) {
        if (mesh.bones[i].name == boneName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DestroyGLTFMesh(const GLTFMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
