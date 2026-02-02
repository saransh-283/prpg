#include "mesh.h"
#include <vector>

PrismMesh CreatePrismMeshFromPolygon(const float* vertices, size_t vertexCount,
                                      float centerX, float centerY, float centerZ, 
                                      float height) {
    PrismMesh prismMesh;
    
    if (vertexCount < 3) {
        // Invalid polygon, return empty mesh
        prismMesh.mesh.VAO = 0;
        prismMesh.mesh.VBO = 0;
        prismMesh.mesh.EBO = 0;
        prismMesh.mesh.vertexCount = 0;
        prismMesh.mesh.indexCount = 0;
        prismMesh.triangleCount = 0;
        return prismMesh;
    }
    
    float halfHeight = height * 0.5f;
    
    // Create vertices for both top and bottom faces
    std::vector<float> vertices3D;
    vertices3D.reserve(vertexCount * 2 * 3);
    
    // Bottom face vertices (centerY - halfHeight)
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices3D.push_back(centerX + vertices[i * 2]);       // x
        vertices3D.push_back(centerY - halfHeight);             // y (bottom)
        vertices3D.push_back(centerZ + vertices[i * 2 + 1]);    // z
    }
    
    // Top face vertices (centerY + halfHeight)
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices3D.push_back(centerX + vertices[i * 2]);       // x
        vertices3D.push_back(centerY + halfHeight);             // y (top)
        vertices3D.push_back(centerZ + vertices[i * 2 + 1]);    // z
    }
    
    std::vector<unsigned int> indices;
    
    // Bottom face (fan triangulation, facing down)
    for (size_t i = 1; i < vertexCount - 1; ++i) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i);
    }
    
    // Top face (fan triangulation, facing up)
    unsigned int topOffset = vertexCount;
    for (size_t i = 1; i < vertexCount - 1; ++i) {
        indices.push_back(topOffset);
        indices.push_back(topOffset + i);
        indices.push_back(topOffset + i + 1);
    }
    
    // Side faces (quads as two triangles each)
    for (size_t i = 0; i < vertexCount; ++i) {
        size_t next = (i + 1) % vertexCount;
        
        unsigned int bottomCurrent = i;
        unsigned int bottomNext = next;
        unsigned int topCurrent = vertexCount + i;
        unsigned int topNext = vertexCount + next;
        
        // First triangle of quad
        indices.push_back(bottomCurrent);
        indices.push_back(topCurrent);
        indices.push_back(bottomNext);
        
        // Second triangle of quad
        indices.push_back(bottomNext);
        indices.push_back(topCurrent);
        indices.push_back(topNext);
    }
    
    prismMesh.triangleCount = indices.size() / 3;
    
    prismMesh.mesh = CreateTriangulateMesh(
        vertices3D.data(),
        vertices3D.size() * sizeof(float),
        indices.data(),
        indices.size()
    );
    
    return prismMesh;
}

void DestroyPrismMesh(const PrismMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
