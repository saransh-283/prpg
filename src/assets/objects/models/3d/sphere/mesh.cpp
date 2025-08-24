#include "mesh.h"
#include <cmath>

SphereMesh CreateSphereMesh(float centerX, float centerY, float centerZ, float radius, int stacks, int slices) {
    SphereMesh mesh;
    mesh.triangleCount = stacks * slices * 2;
    mesh.triangles.resize(mesh.triangleCount);
    
    int triangleIndex = 0;
    
    // Generate sphere using latitude/longitude approach
    for (int i = 0; i < stacks; ++i) {
        float lat1 = M_PI * (-0.5f + (float)i / stacks);        // Current latitude
        float lat2 = M_PI * (-0.5f + (float)(i + 1) / stacks);  // Next latitude
        
        for (int j = 0; j < slices; ++j) {
            float lng1 = 2 * M_PI * (float)j / slices;           // Current longitude
            float lng2 = 2 * M_PI * (float)(j + 1) / slices;     // Next longitude
            
            // Calculate vertices
            float x1 = centerX + radius * cos(lat1) * cos(lng1);
            float y1 = centerY + radius * sin(lat1);
            float z1 = centerZ + radius * cos(lat1) * sin(lng1);
            
            float x2 = centerX + radius * cos(lat1) * cos(lng2);
            float y2 = centerY + radius * sin(lat1);
            float z2 = centerZ + radius * cos(lat1) * sin(lng2);
            
            float x3 = centerX + radius * cos(lat2) * cos(lng1);
            float y3 = centerY + radius * sin(lat2);
            float z3 = centerZ + radius * cos(lat2) * sin(lng1);
            
            float x4 = centerX + radius * cos(lat2) * cos(lng2);
            float y4 = centerY + radius * sin(lat2);
            float z4 = centerZ + radius * cos(lat2) * sin(lng2);
            
            // First triangle
            float triangle1[] = {
                x1, y1, z1,
                x2, y2, z2,
                x3, y3, z3
            };
            
            // Second triangle
            float triangle2[] = {
                x2, y2, z2,
                x4, y4, z4,
                x3, y3, z3
            };
            
            mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle1, sizeof(triangle1), 3);
            mesh.triangles[triangleIndex++] = CreateTriangulateMesh(triangle2, sizeof(triangle2), 3);
        }
    }
    
    return mesh;
}

void DestroySphereMesh(const SphereMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
