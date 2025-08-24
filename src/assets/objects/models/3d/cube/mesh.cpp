#include "mesh.h"

CubeMesh CreateCubeMesh(float centerX, float centerY, float centerZ, float length, float breadth, float height) {
    // Default breadth/height to length if zero
    if (breadth == 0.0f) breadth = length;
    if (height == 0.0f) height = length;

    CubeMesh out;
    out.triangleCount = 12; // 6 faces * 2

    float hx = length * 0.5f;
    float hy = height * 0.5f;
    float hz = breadth * 0.5f;

    // 8 unique vertices
    float vertices[8 * 3] = {
        centerX - hx, centerY - hy, centerZ + hz,
        centerX + hx, centerY - hy, centerZ + hz,
        centerX + hx, centerY + hy, centerZ + hz,
        centerX - hx, centerY + hy, centerZ + hz,
        centerX - hx, centerY - hy, centerZ - hz,
        centerX + hx, centerY - hy, centerZ - hz,
        centerX + hx, centerY + hy, centerZ - hz,
        centerX - hx, centerY + hy, centerZ - hz
    };

    unsigned int indices[] = {
        0,1,2, 2,3,0, // front
        1,5,6, 6,2,1, // right
        5,4,7, 7,6,5, // back
        4,0,3, 3,7,4, // left
        3,2,6, 6,7,3, // top
        4,5,1, 1,0,4  // bottom
    };

    out.mesh = CreateTriangulateMesh(vertices, sizeof(vertices), indices, sizeof(indices) / sizeof(indices[0]));

    return out;
}

void DestroyCubeMesh(const CubeMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
