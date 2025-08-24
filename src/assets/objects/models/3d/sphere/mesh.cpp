#include "mesh.h"
#include <cmath>

SphereMesh CreateSphereMesh(float centerX, float centerY, float centerZ, float radius, int stacks, int slices) {
    SphereMesh out;
    out.triangleCount = stacks * slices * 2;

    // Build a single vertex array (non-indexed) containing each triangle's 3 vertices
    std::vector<float> verts;
    verts.reserve(out.triangleCount * 9);

    for (int i = 0; i < stacks; ++i) {
        float lat1 = M_PI * (-0.5f + (float)i / stacks);
        float lat2 = M_PI * (-0.5f + (float)(i + 1) / stacks);
        for (int j = 0; j < slices; ++j) {
            float lng1 = 2 * M_PI * (float)j / slices;
            float lng2 = 2 * M_PI * (float)(j + 1) / slices;

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

            // triangle 1
            verts.push_back(x1); verts.push_back(y1); verts.push_back(z1);
            verts.push_back(x2); verts.push_back(y2); verts.push_back(z2);
            verts.push_back(x3); verts.push_back(y3); verts.push_back(z3);

            // triangle 2
            verts.push_back(x2); verts.push_back(y2); verts.push_back(z2);
            verts.push_back(x4); verts.push_back(y4); verts.push_back(z4);
            verts.push_back(x3); verts.push_back(y3); verts.push_back(z3);
        }
    }

    out.mesh = CreateTriangulateMesh(verts.data(), verts.size() * sizeof(float), (unsigned int)(verts.size() / 3));
    out.vertexCount = (unsigned int)(verts.size() / 3);
    return out;
}

void DestroySphereMesh(const SphereMesh& mesh) {
    DestroyTriangulateMesh(mesh.mesh);
}
