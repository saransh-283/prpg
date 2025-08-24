#### Cube

```cpp
#include "objects/models/3d/cube/mesh.h"

float vertices[] = {
    // Front face
    -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
    // Back face
    -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};

unsigned int indices[] = {
    0, 1, 2, 2, 3, 0, // Front
    1, 5, 6, 6, 2, 1, // Right
    5, 4, 7, 7, 6, 5, // Back
    4, 0, 3, 3, 7, 4, // Left
    3, 2, 6, 6, 7, 3, // Top
    4, 5, 1, 1, 0, 4  // Bottom
};

// Create all shapes
CubeMesh cubeMesh = CreateCubeMesh(vertices, sizeof(vertices), indices, 36);

for (const auto& triangle : cubeMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Cylinder

```cpp
#include "objects/models/3d/cylinder/mesh.h"

CylinderMesh cylinderMesh = CreateCylinderMesh(0.0f, 0.0f, 0.0f, 0.3f, 0.6f, 12);

for (const auto& triangle : cylinderMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Cone

```cpp
#include "objects/models/3d/cone/mesh.h"

ConeMesh coneMesh = CreateConeMesh(0.0f, 0.0f, 0.0f, 0.4f, 0.6f, 12);

for (const auto& triangle : coneMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Prism

```cpp
#include "objects/models/3d/prism/mesh.h"

float prismVertices[] = {
    // Bottom triangle
    0.0f, -0.3f, 0.4f,          // 0
    0.4f * cos(2*M_PI/3), -0.3f, 0.4f * sin(2*M_PI/3),  // 1
    0.4f * cos(4*M_PI/3), -0.3f, 0.4f * sin(4*M_PI/3),  // 2
    // Top triangle
    0.0f, 0.3f, 0.4f,           // 3
    0.4f * cos(2*M_PI/3), 0.3f, 0.4f * sin(2*M_PI/3),   // 4
    0.4f * cos(4*M_PI/3), 0.3f, 0.4f * sin(4*M_PI/3)    // 5
};
unsigned int prismIndices[] = {
    // Bottom face
    2, 1, 0,
    // Top face
    3, 4, 5,
    // Side faces
    0, 1, 3,  1, 4, 3,  // Face 1
    1, 2, 4,  2, 5, 4,  // Face 2
    2, 0, 5,  0, 3, 5   // Face 3
};
PrismMesh prismMesh = CreatePrismMesh(prismVertices, 6, prismIndices, 24);

for (const auto& triangle : prismMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Sphere

```cpp
#include "objects/models/3d/sphere/mesh.h"

SphereMesh sphereMesh = CreateSphereMesh(0.0f, 0.0f, 0.0f, 0.4f, 8, 12);

for (const auto& triangle : sphereMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Custom GLTF

**mesh.h**

```cpp
#ifndef CUSTOM_MESH_H
#define CUSTOM_MESH_H

#include <glad/glad.h>
#include "utils/triangulate/mesh.h"
#include "utils/gltf/gltf_loader.h"
#include <vector>

struct CustomMesh {
    std::vector<TriangulateMesh> triangles;
    unsigned int triangleCount;
};

// Creates a custom mesh using GLTF file
CustomMesh CreateCustomMesh();

// Cleanup custom mesh resources
void DestroyCustomMesh(const CustomMesh& mesh);

#endif // CUSTOM_MESH_H
```

**mesh.cpp**
```cpp
#include "mesh.h"
#include "utils/gltf/gltf_loader.h"
#include <iostream>

CustomMesh CreateCustomMesh()
{
    CustomMesh mesh;
    // Fallback: Load the custom model from GLTF file if no vertices provided
    std::string gltfPath = "Custom.gltf";
    GLTFMesh gltfMesh = LoadGLTFMesh(gltfPath);

    // Copy triangles from GLTF mesh to custom mesh
    mesh.triangles = gltfMesh.triangles;
    mesh.triangleCount = gltfMesh.triangleCount;

    std::cout << "Created custom mesh with " << mesh.triangleCount << " triangles from GLTF" << std::endl;

    return mesh;
}

void DestroyCustomMesh(const CustomMesh &mesh)
{
    for (const auto &triangle : mesh.triangles)
    {
        DestroyTriangulateMesh(triangle);
    }
}
```

**Usage Example**

```cpp
#include "objects/models/3d/custom/mesh.h"

CustomMesh customMesh = CreateCustomMesh();

for (const auto& triangle : customMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```