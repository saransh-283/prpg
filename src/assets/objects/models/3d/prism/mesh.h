#ifndef PRISM_MESH_H
#define PRISM_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct PrismMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
};

// Creates a 3D prism mesh from a 2D base polygon
// vertices: array of x,y coordinates for the base polygon [x0,y0, x1,y1, x2,y2, ...]
// vertexCount: number of vertices (not floats - i.e., pairs of x,y)
// centerX, centerY, centerZ: world position center of the prism
// height: height of the prism (extrusion amount)
PrismMesh CreatePrismMeshFromPolygon(const float* vertices, size_t vertexCount,
                                      float centerX, float centerY, float centerZ, 
                                      float height);

// Cleanup prism mesh resources
void DestroyPrismMesh(const PrismMesh& mesh);

#endif // PRISM_MESH_H
