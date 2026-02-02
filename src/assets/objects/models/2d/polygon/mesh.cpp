#include "mesh.h"
#include <vector>

PolygonMesh CreatePolygonMesh(const float* vertices, size_t vertexCount, 
                              float centerX, float centerY, float centerZ) {
    PolygonMesh polygonMesh;
    
    if (vertexCount < 3) {
        // Invalid polygon, return empty mesh
        polygonMesh.mesh.VAO = 0;
        polygonMesh.mesh.VBO = 0;
        polygonMesh.mesh.EBO = 0;
        polygonMesh.mesh.vertexCount = 0;
        polygonMesh.mesh.indexCount = 0;
        polygonMesh.triangleCount = 0;
        return polygonMesh;
    }
    
    // Convert 2D vertices to 3D (all at centerY height)
    std::vector<float> vertices3D;
    vertices3D.reserve(vertexCount * 3);
    
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices3D.push_back(centerX + vertices[i * 2]);     // x
        vertices3D.push_back(centerY);                        // y (constant height)
        vertices3D.push_back(centerZ + vertices[i * 2 + 1]);  // z
    }
    
    // Triangulate using fan triangulation (assumes convex polygon or simple polygon)
    std::vector<unsigned int> indices;
    polygonMesh.triangleCount = vertexCount - 2;
    indices.reserve(polygonMesh.triangleCount * 3);
    
    for (size_t i = 1; i < vertexCount - 1; ++i) {
        indices.push_back(0);
        indices.push_back(i);
        indices.push_back(i + 1);
    }
    
    polygonMesh.mesh = CreateTriangulateMesh(
        vertices3D.data(), 
        vertices3D.size() * sizeof(float),
        indices.data(),
        indices.size()
    );
    
    return polygonMesh;
}

void DestroyPolygonMesh(const PolygonMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
