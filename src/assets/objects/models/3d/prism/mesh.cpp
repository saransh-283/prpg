#include "mesh.h"
#include <cmath>


PrismMesh CreatePrismMesh(float centerX, float centerY, float centerZ, float baseRadius, float height) {
    PrismMesh out;
    out.triangleCount = 3 * 2 + 2; // 8 triangles

    float r = baseRadius;
    float bx[3], bz[3];
    for (int i = 0; i < 3; ++i) {
        float ang = (float)i / 3.0f * 2.0f * M_PI;
        bx[i] = r * cos(ang) + centerX;
        bz[i] = r * sin(ang) + centerZ;
    }

    float yBottom = centerY - height * 0.5f;
    float yTop = centerY + height * 0.5f;

    // 6 unique vertices (bottom 0..2, top 3..5)
    float vertices[6 * 3];
    for (int i = 0; i < 3; ++i) {
        vertices[i*3 + 0] = bx[i];
        vertices[i*3 + 1] = yBottom;
        vertices[i*3 + 2] = bz[i];

        vertices[(i+3)*3 + 0] = bx[i];
        vertices[(i+3)*3 + 1] = yTop;
        vertices[(i+3)*3 + 2] = bz[i];
    }

    unsigned int indices[] = {
        // sides
        0,1,4, 4,3,0,
        1,2,5, 5,4,1,
        2,0,3, 3,5,2,
        // bottom cap
        0,1,2,
        // top cap (winding outward)
        3,5,4
    };

    out.mesh = CreateTriangulateMesh(vertices, sizeof(vertices), indices, sizeof(indices) / sizeof(indices[0]));
    return out;
}

void DestroyPrismMesh(const PrismMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
