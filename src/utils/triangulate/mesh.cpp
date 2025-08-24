#include "mesh.h"

// Global wireframe state
static bool globalWireframeMode = false;

void SetGlobalWireframeMode(bool wireframe) {
    globalWireframeMode = wireframe;
    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

bool IsGlobalWireframeMode() {
    return globalWireframeMode;
}

TriangulateMesh CreateTriangulateMesh(const float* vertices, size_t vertexSize, unsigned int vertexCount) {
    TriangulateMesh mesh;
    mesh.EBO = 0;
    mesh.indexCount = 0;
    mesh.vertexCount = vertexCount;
    
    // Generate buffers for filled triangle
    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);

    // Bind VAO first
    glBindVertexArray(mesh.VAO);
    
    // Bind and fill vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexSize, vertices, GL_STATIC_DRAW);

    // Configure vertex attributes (3D positions)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Unbind VAO
    glBindVertexArray(0);
    
    return mesh;
}

TriangulateMesh CreateTriangulateMesh(const float* vertices, size_t vertexSize, const unsigned int* indices, size_t indexCount) {
    TriangulateMesh mesh;
    mesh.vertexCount = (unsigned int)(vertexSize / (3 * sizeof(float))); // fallback if needed
    mesh.indexCount = (unsigned int)indexCount;
    mesh.EBO = 0;

    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexSize, vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount * sizeof(unsigned int), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    return mesh;
}

void DestroyTriangulateMesh(const TriangulateMesh& mesh) {
    if (mesh.EBO != 0) glDeleteBuffers(1, &mesh.EBO);
    glDeleteBuffers(1, &mesh.VBO);
    glDeleteVertexArrays(1, &mesh.VAO);
}
