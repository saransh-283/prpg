#ifndef POLYGON_MESH_H
#define POLYGON_MESH_H

#include <glad/glad.h>
#include <utils/triangulate/mesh.h>
#include <vector>

struct PolygonMesh {
    TriangulateMesh mesh;
    unsigned int triangleCount;
};

// Creates a 2D polygon mesh from vertices (x, y pairs flattened)
// vertices: array of x,y coordinates [x0,y0, x1,y1, x2,y2, ...]
// vertexCount: number of vertices (not floats - i.e., pairs of x,y)
// centerX, centerY, centerZ: world position center of the polygon
PolygonMesh CreatePolygonMesh(const float* vertices, size_t vertexCount, 
                              float centerX, float centerY, float centerZ);

// Cleanup polygon mesh resources
void DestroyPolygonMesh(const PolygonMesh& mesh);

#endif // POLYGON_MESH_H
