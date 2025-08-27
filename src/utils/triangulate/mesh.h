#ifndef TRIANGULATE_MESH_H
#define TRIANGULATE_MESH_H

#include <cstddef>
#include <glad/glad.h>

struct TriangulateMesh {
    GLuint VAO;
    GLuint VBO;
    GLuint EBO; // 0 if no element buffer
    unsigned int vertexCount;
    unsigned int indexCount; // number of indices when using EBO
};

// Global wireframe management
void SetGlobalWireframeMode(bool wireframe);
bool IsGlobalWireframeMode();

// Creates a triangulate mesh from vertex data
// Create a non-indexed mesh (vertices contain repeated triangle vertices)
TriangulateMesh CreateTriangulateMesh(const float* vertices, size_t vertexSize, unsigned int vertexCount);

// Create an indexed mesh: vertices is list of unique vertices, indices specifies triangles (3 per triangle)
TriangulateMesh CreateTriangulateMesh(const float* vertices, size_t vertexSize, const unsigned int* indices, size_t indexCount);

// Cleanup triangulate mesh resources
void DestroyTriangulateMesh(const TriangulateMesh& mesh);

#endif // TRIANGULATE_MESH_H
