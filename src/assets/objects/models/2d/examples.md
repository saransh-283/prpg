#### Square

```cpp
#include "objects/models/2d/square/mesh.h"

float squareVertices[] = {
    -0.5f, -0.5f, 0.0f,  // Bottom left
    0.5f, -0.5f, 0.0f,  // Bottom right
    0.5f,  0.5f, 0.0f,  // Top right
    -0.5f,  0.5f, 0.0f   // Top left
};
unsigned int squareIndices[] = {
    0, 1, 2,  // First triangle
    2, 3, 0   // Second triangle
};
SquareMesh squareMesh = CreateSquareMesh(squareVertices, 4, squareIndices, 6);

for (const auto& triangle : squareMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Triangle

```cpp
#include "objects/models/2d/triangle/mesh.h"

float triangleVertices[] = {
    0.0f,  0.5f, 0.0f,  // Top vertex
    -0.5f, -0.5f, 0.0f,  // Bottom left vertex
    0.5f, -0.5f, 0.0f   // Bottom right vertex
};
unsigned int triangleIndices[] = {
    0, 1, 2  // Single triangle
};
TriangleMesh triangleMesh = CreateTriangleMesh(triangleVertices, 3, triangleIndices, 3);

for (const auto& triangle : triangleMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```

#### Circle

```cpp
#include "objects/models/2d/circle/mesh.h"

CircleMesh circleMesh = CreateCircleMesh(0.0f, 0.0f, 0.0f, 0.5f, 16);

for (const auto& triangle : circleMesh.triangles) {
    glBindVertexArray(triangle.VAO);
    glDrawArrays(GL_TRIANGLES, 0, triangle.vertexCount);
}
```