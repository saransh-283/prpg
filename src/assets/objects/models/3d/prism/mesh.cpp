#include "mesh.h"
#include <cmath>
#include <vector>


PrismMesh CreatePrismMesh(float centerX, float centerY, float centerZ, float baseRadius, float height) {
    // Create triangular base points
    std::vector<float> basePoints;
    for (int i = 0; i < 3; ++i) {
        float ang = (float)i / 3.0f * 2.0f * M_PI;
        basePoints.push_back(baseRadius * cos(ang));
        basePoints.push_back(baseRadius * sin(ang));
    }
    
    return CreatePrismMeshFromPolygon(basePoints.data(), 3, centerX, centerY, centerZ, height);
}

PrismMesh CreatePrismMeshFromPolygon(const float* polygonPoints, int numPoints, float centerX, float centerY, float centerZ, float height) {
    PrismMesh out;
    
    if (numPoints < 3) {
        // Invalid polygon, return empty mesh
        out.triangleCount = 0;
        out.mesh.VAO = 0;
        out.mesh.VBO = 0;
        out.mesh.EBO = 0;
        out.mesh.vertexCount = 0;
        out.mesh.indexCount = 0;
        return out;
    }

    float yBottom = centerY - height * 0.5f;
    float yTop = centerY + height * 0.5f;

    // Create vertices: bottom polygon, then top polygon
    std::vector<float> vertices;
    vertices.reserve(numPoints * 2 * 3);
    
    // Bottom vertices
    for (int i = 0; i < numPoints; ++i) {
        vertices.push_back(polygonPoints[i * 2] + centerX);
        vertices.push_back(yBottom);
        vertices.push_back(polygonPoints[i * 2 + 1] + centerZ);
    }
    
    // Top vertices
    for (int i = 0; i < numPoints; ++i) {
        vertices.push_back(polygonPoints[i * 2] + centerX);
        vertices.push_back(yTop);
        vertices.push_back(polygonPoints[i * 2 + 1] + centerZ);
    }

    // Create indices
    std::vector<unsigned int> indices;
    
    // Side faces (2 triangles per edge)
    for (int i = 0; i < numPoints; ++i) {
        int next = (i + 1) % numPoints;
        int bottomCurr = i;
        int bottomNext = next;
        int topCurr = i + numPoints;
        int topNext = next + numPoints;
        
        // First triangle of quad
        indices.push_back(bottomCurr);
        indices.push_back(bottomNext);
        indices.push_back(topNext);
        
        // Second triangle of quad
        indices.push_back(topNext);
        indices.push_back(topCurr);
        indices.push_back(bottomCurr);
    }
    
    // Bottom cap (fan triangulation from first vertex)
    for (int i = 1; i < numPoints - 1; ++i) {
        indices.push_back(0);
        indices.push_back(i);
        indices.push_back(i + 1);
    }
    
    // Top cap (fan triangulation from first vertex, reverse winding)
    for (int i = 1; i < numPoints - 1; ++i) {
        indices.push_back(numPoints);
        indices.push_back(numPoints + i + 1);
        indices.push_back(numPoints + i);
    }
    
    out.triangleCount = indices.size() / 3;
    out.mesh = CreateTriangulateMesh(vertices.data(), vertices.size() * sizeof(float), 
                                     indices.data(), indices.size());
    return out;
}

void DestroyPrismMesh(const PrismMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
