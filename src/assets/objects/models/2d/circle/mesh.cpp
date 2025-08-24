#include "mesh.h"
#include <cmath>

CircleMesh CreateCircleMesh(float centerX, float centerY, float centerZ, float radius, int segments) {
    CircleMesh mesh;
    mesh.triangleCount = segments;
    mesh.triangles.resize(segments);
    
    // Create triangles from center to circumference
    for (int i = 0; i < segments; ++i) {
        float angle1 = 2.0f * M_PI * i / segments;
        float angle2 = 2.0f * M_PI * (i + 1) / segments;
        
        float triangle[] = {
            centerX, centerY, centerZ,  // Center
            centerX + radius * cos(angle1), centerY + radius * sin(angle1), centerZ, // Current point
            centerX + radius * cos(angle2), centerY + radius * sin(angle2), centerZ  // Next point
        };
        
        mesh.triangles[i] = CreateTriangulateMesh(triangle, sizeof(triangle), 3);
    }
    
    return mesh;
}

void DestroyCircleMesh(const CircleMesh& mesh) {
    for (const auto& triangle : mesh.triangles) {
        DestroyTriangulateMesh(triangle);
    }
}
