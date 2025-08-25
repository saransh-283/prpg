#include "mesh.h"
#include <iostream>

CustomMesh CreateCustomMesh(const std::string& modelPath)
{
    CustomMesh mesh;
    // Load the custom model from provided path (supports .gltf and .glb)
    GLTFMesh gltfMesh = LoadGLTFMesh(modelPath);

    // Copy triangles from GLTF mesh to custom mesh
    mesh.triangles = gltfMesh.triangles;
    mesh.triangleCount = gltfMesh.triangleCount;

    std::cout << "Created custom mesh with " << mesh.triangleCount << " triangles from " << modelPath << std::endl;

    return mesh;
}

void DestroyCustomMesh(const CustomMesh &mesh)
{
    for (const auto &triangle : mesh.triangles)
    {
        DestroyTriangulateMesh(triangle);
    }
}
