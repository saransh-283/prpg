#include "mesh.h"
#include <cmath>

PrismMesh CreatePrismMesh(const float* vertices, size_t vertexCount, const unsigned int* indices, size_t indexCount) {
    PrismMesh mesh;
    mesh.triangleCount = indexCount / 3; // Each triangle has 3 indices
    mesh.triangles.resize(mesh.triangleCount);
    
    // Process triangles based on indices
    for (size_t i = 0; i < mesh.triangleCount; ++i) {
        float triangleVertices[9]; // 3 vertices * 3 components (x, y, z)
        
        // Extract vertices for this triangle using indices
        for (int j = 0; j < 3; ++j) {
            unsigned int vertexIndex = indices[i * 3 + j];
            triangleVertices[j * 3 + 0] = vertices[vertexIndex * 3 + 0]; // x
            triangleVertices[j * 3 + 1] = vertices[vertexIndex * 3 + 1]; // y
            triangleVertices[j * 3 + 2] = vertices[vertexIndex * 3 + 2]; // z
        }

        mesh.triangles[i] = CreateTriangulateMesh(triangleVertices, sizeof(triangleVertices), 3);
    }
    
    return mesh;
}

void DestroyPrismMesh(const PrismMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
