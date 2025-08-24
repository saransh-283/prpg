#include "mesh.h"

CubeMesh CreateCubeMesh(const float* vertices, size_t vertexSize, const unsigned int* indices, size_t indexCount) {
    CubeMesh mesh;
    mesh.triangleCount = 12; // 6 faces * 2 triangles per face
    mesh.triangles.resize(12);
    
    // Create triangles from indices
    for (int i = 0; i < 12; i++) {
        int idx1 = indices[i * 3];
        int idx2 = indices[i * 3 + 1];
        int idx3 = indices[i * 3 + 2];
        
        float triangle[] = {
            vertices[idx1 * 3], vertices[idx1 * 3 + 1], vertices[idx1 * 3 + 2],
            vertices[idx2 * 3], vertices[idx2 * 3 + 1], vertices[idx2 * 3 + 2],
            vertices[idx3 * 3], vertices[idx3 * 3 + 1], vertices[idx3 * 3 + 2]
        };
        
        mesh.triangles[i] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    return mesh;
}

void DestroyCubeMesh(const CubeMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
