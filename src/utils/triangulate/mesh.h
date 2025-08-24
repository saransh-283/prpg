#ifndef TRIANGULATE_MESH_H
#define TRIANGULATE_MESH_H

#include <glad/glad.h>

struct TriangulateMesh {
    GLuint VAO;
    GLuint VBO;
    unsigned int vertexCount;
};

// Global wireframe management
void SetGlobalWireframeMode(bool wireframe);
bool IsGlobalWireframeMode();

// Creates a triangulate mesh from vertex data
TriangulateMesh CreateTriangulateMesh(const float* vertices, size_t vertexSize, unsigned int vertexCount);

// Cleanup triangulate mesh resources
void DestroyTriangulateMesh(const TriangulateMesh& mesh);

#endif // TRIANGULATE_MESH_H
