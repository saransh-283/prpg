#include "mesh.h"
#include <iostream>

CustomMesh CreateCustomMesh()
{
    CustomMesh mesh;
    // Fallback: Load the custom model from GLTF file if no vertices provided
    std::string gltfPath = "objects/models/3d/custom/Custom.gltf";
    GLTFMesh gltfMesh = LoadGLTFMesh(gltfPath);

    // Copy triangles from GLTF mesh to custom mesh
    mesh.triangles = gltfMesh.triangles;
    mesh.triangleCount = gltfMesh.triangleCount;

    std::cout << "Created custom mesh with " << mesh.triangleCount << " triangles from GLTF" << std::endl;

    return mesh;
}

void DestroyCustomMesh(const CustomMesh &mesh)
{
    for (const auto &triangle : mesh.triangles)
    {
        DestroyTriangulateMesh(triangle);
    }
}
