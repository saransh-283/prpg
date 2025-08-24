#include "mesh.h"
#include <cmath>

CylinderMesh CreateCylinderMesh(float centerX, float centerY, float centerZ, float radius, float height, int segments) {
    CylinderMesh mesh;
    mesh.triangleCount = segments * 4; // sides + top + bottom caps
    mesh.triangles.resize(mesh.triangleCount);
    
    int triangleIndex = 0;
    
    // Create side triangles
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float x1 = centerX + radius * cos(angle1);
        float z1 = centerZ + radius * sin(angle1);
        float x2 = centerX + radius * cos(angle2);
        float z2 = centerZ + radius * sin(angle2);
        
        // Bottom triangle
        float triangle1[] = {
            x1, centerY - height/2, z1,
            x2, centerY - height/2, z2,
            x1, centerY + height/2, z1
        };
        
        // Top triangle
        float triangle2[] = {
            x2, centerY - height/2, z2,
            x2, centerY + height/2, z2,
            x1, centerY + height/2, z1
        };
        
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle1, sizeof(triangle1), 3);
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle2, sizeof(triangle2), 3);
    }
    
    // Top cap
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float triangle[] = {
            centerX, centerY + height/2, centerZ,
            centerX + radius * cos(angle2), centerY + height/2, centerZ + radius * sin(angle2),
            centerX + radius * cos(angle1), centerY + height/2, centerZ + radius * sin(angle1)
        };
        
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    // Bottom cap
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float triangle[] = {
            centerX, centerY - height/2, centerZ,
            centerX + radius * cos(angle1), centerY - height/2, centerZ + radius * sin(angle1),
            centerX + radius * cos(angle2), centerY - height/2, centerZ + radius * sin(angle2)
        };
        
        mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    return mesh;
}

void DestroyCylinderMesh(const CylinderMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
