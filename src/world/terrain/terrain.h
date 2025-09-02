#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>

// Generate a key for chunk coordinates
long long keyFor(int cx, int cz);

// Initialize terrain system. Returns true on success.
bool InitTerrain();

// Update generated terrain based on camera position (world-space)
void UpdateTerrain(const glm::vec3& cameraPos);

// Render all generated terrain chunks using the provided shader programs and matrices
void RenderTerrain(GLuint terrainProgram, GLuint highwaysProgram, GLuint roadsProgram, GLuint streetsProgram, const glm::mat4& proj, const glm::mat4& view);

// Cleanup GPU resources used by terrain
void CleanupTerrain();

// Query terrain height at world (x, z)
float SampleTerrainHeight(float x, float z);

// Convert a world-space XZ position to chunk coordinates (cx, cz)
void WorldToChunk(float x, float z, int &out_cx, int &out_cz);

// Generate a single terrain chunk (terrain mesh only) at chunk coords. Returns true on success.
bool GenerateTerrainChunk(int cx, int cz);

// Generate road mesh for a single chunk. Returns true on success.
bool GenerateRoadsForChunk(int cx, int cz);

// Generate streets mesh for a single chunk (assumes roads/highways are already generated). Returns true on success.
void GenerateStreetsForChunk(int cx, int cz);

// Determine a good spawn point (nearest road) near world (x,z). Returns world x,z in out vector.
glm::vec2 DetermineSpawnPoint(float x, float z, int search_radius_chunks = 2);

// Determine spawn using only already-generated chunk road data (no on-the-fly generation).
// Returns true and writes out point if a candidate was found; returns false otherwise.
bool DetermineSpawnFromGenerated(float x, float z, glm::vec2 &out_point, int search_radius_chunks = 2);
