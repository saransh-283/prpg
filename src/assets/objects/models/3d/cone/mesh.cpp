#include "mesh.h"
#include <cmath>

ConeMesh CreateConeMesh(float centerX, float centerY, float centerZ, float radius, float height, int segments) {
    ConeMesh mesh;
    mesh.triangleCount = segments * 2; // sides + base
    mesh.triangles.resize(mesh.triangleCount);
    
    int triangleIndex = 0;
    
    // Create side triangles (from apex to base)
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float triangle[] = {
            centerX, centerY + height/2, centerZ,  // Apex
            centerX + radius * cos(angle1), centerY - height/2, centerZ + radius * sin(angle1), // Base point 1
            centerX + radius * cos(angle2), centerY - height/2, centerZ + radius * sin(angle2)  // Base point 2
        };
        
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    // Create base triangles
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float triangle[] = {
            centerX, centerY - height/2, centerZ,  // Base center
            centerX + radius * cos(angle2), centerY - height/2, centerZ + radius * sin(angle2), // Base point 2
            centerX + radius * cos(angle1), centerY - height/2, centerZ + radius * sin(angle1)  // Base point 1
        };
        
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    return mesh;
}

void DestroyConeMesh(const ConeMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
