#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>

// Initialize terrain system. Returns true on success.
bool InitTerrain();

// Update generated terrain based on camera position (world-space)
void UpdateTerrain(const glm::vec3& cameraPos);

// Render all generated terrain chunks using the provided shader program and matrices
void RenderTerrain(GLuint program, const glm::mat4& proj, const glm::mat4& view);

// Cleanup GPU resources used by terrain
void CleanupTerrain();

// Query terrain height at world (x, z)
float SampleTerrainHeight(float x, float z);

// Convert a world-space XZ position to chunk coordinates (cx, cz)
void WorldToChunk(float x, float z, int &out_cx, int &out_cz);
