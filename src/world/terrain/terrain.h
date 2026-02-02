#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <vector>

// Road type enumeration matching the notebook
enum RoadType {
    TERRAIN = 0,
    HIGHWAY = 1,
    ROAD = 2,
    STREET = 3,
    BUILDING = 4
};

// Generate a key for chunk coordinates
long long keyFor(int cx, int cz);

// Initialize terrain system. Returns true on success.
bool InitTerrain();

// Update generated terrain based on camera position (world-space)
void UpdateTerrain(const glm::vec3& cameraPos);

// Render all generated terrain chunks using the provided shader programs and matrices
void RenderTerrain(GLuint terrainProgram, GLuint highwaysProgram, GLuint roadsProgram, GLuint streetsProgram, GLuint buildingsProgram, const glm::mat4& proj, const glm::mat4& view);

// Cleanup GPU resources used by terrain
void CleanupTerrain();

// Query terrain height at world (x, z)
float SampleTerrainHeight(float x, float z);

// True if a circle at (x,z) overlaps any generated building footprint.
// Note: buildings only exist inside generated chunks; outside that area this returns false.
bool CollidesWithBuilding(float x, float z, float radius);

// Convert a world-space XZ position to chunk coordinates (cx, cz)
void WorldToChunk(float x, float z, int &out_cx, int &out_cz);

// Generate a single terrain chunk with complete pipeline (terrain + highways + roads + streets)
bool GenerateTerrainChunk(int cx, int cz);

// Generate road mesh for a single chunk. Returns true on success.
bool GenerateRoadsForChunk(int cx, int cz);

// Generate streets mesh for a single chunk (assumes roads/highways are already generated). Returns true on success.
void GenerateStreetsForChunk(int cx, int cz);

// Determine spawn point near (x,z) by finding nearest road intersection
// Uses already-generated chunk data. Returns best spawn point or input position if no roads found.
glm::vec2 DetermineSpawnPoint(float x, float z, int search_radius_chunks = 2);
