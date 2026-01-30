#ifndef PRISM_MESH_H
#define PRISM_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct PrismMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
};

// Creates a triangular prism mesh using center, base radius and height.
// The prism will have an equilateral triangular base in the XZ plane centered at (centerX, centerY - height/2, centerZ)
PrismMesh CreatePrismMesh(float centerX, float centerY, float centerZ, float baseRadius, float height);

// Creates a prism mesh from an arbitrary 2D polygon.
// polygonPoints: Array of 2D points (x,z) defining the polygon base, length = numPoints * 2
// numPoints: Number of vertices in the polygon (minimum 3)
// centerX, centerY, centerZ: World position of the prism center
// height: Total height of the prism (extends from centerY - height/2 to centerY + height/2)
PrismMesh CreatePrismMeshFromPolygon(const float* polygonPoints, int numPoints, 
                                     float centerX, float centerY, float centerZ, float height);

// Cleanup prism mesh resources
void DestroyPrismMesh(const PrismMesh& mesh);

#endif // PRISM_MESH_H
