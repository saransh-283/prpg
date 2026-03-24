#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <vector>

// Axis-aligned bounding box used for chunk bounds (previously in frustum.h)
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

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
void RenderTerrain(GLuint terrainProgram,
                   GLuint highwaysProgram,
                   GLuint roadsProgram,
                   GLuint streetsProgram,
                   GLuint buildingsProgram,
                   GLuint buildingWindowsProgram,
                   const glm::mat4& proj,
                   const glm::mat4& view);

// Render terrain to G-buffer (deferred rendering).
// Preprocessing: buildings are skipped for chunks outside the camera view frustum.
void RenderTerrainToGBuffer(GLuint geometryShader,
                            GLuint windowsGeometryShader,
                            const glm::mat4& proj,
                            const glm::mat4& view,
                            const glm::vec3& cameraPos,
                            const glm::vec3& cameraFront);

// Render terrain to shadow map (same camera-frustum building filter).
void RenderTerrainToShadowMap(GLuint shadowShader,
                              const glm::mat4& lightSpaceMatrix,
                              const glm::mat4& proj,
                              const glm::mat4& view);

// Cleanup GPU resources used by terrain
void CleanupTerrain();

// Query terrain height at world (x, z)
float SampleTerrainHeight(float x, float z);

// Query walkable floor/ceiling at world (x, z) given the player's current feet height.
// - feetY is the player's feet/world-contact height (cameraY - eye_height).
// - outFloorY is the best-matching walkable surface at/below the player (terrain, floor, or ramp).
// - outCeilingY is the nearest ceiling surface above the player (next interior floor or roof), or +inf if none.
// Returns true if the query position is inside a generated building cell; false otherwise.
bool SampleWalkableFloorAndCeiling(float x, float z, float feetY, float& outFloorY, float& outCeilingY);

// True if a circle at (x,z) overlaps any generated building footprint.
// Note: buildings only exist inside generated chunks; outside that area this returns false.
bool CollidesWithBuilding(float x, float z, float radius);

// Same as above, but only considers building wall triangles whose vertical span overlaps [yMin, yMax].
// This allows doorway openings (walls above the door height) to not block the player.
bool CollidesWithBuilding(float x, float z, float radius, float yMin, float yMax);

// Convert a world-space XZ position to chunk coordinates (cx, cz)
void WorldToChunk(float x, float z, int &out_cx, int &out_cz);

// Read-only access to a generated chunk's road-type grid.
// Returns true if the chunk exists and has a populated grid.
// The returned pointer remains valid until the chunk is unloaded.
bool GetChunkRoadGrid(int cx, int cz, const std::vector<std::vector<int>>*& outGrid);

// Generate a single terrain chunk with complete pipeline (terrain + highways + roads + streets)
bool GenerateTerrainChunk(int cx, int cz);

// Generate road mesh for a single chunk. Returns true on success.
bool GenerateRoadsForChunk(int cx, int cz);

// Generate streets mesh for a single chunk (assumes roads/highways are already generated). Returns true on success.
void GenerateStreetsForChunk(int cx, int cz);

// Ensure building data/meshes exist for a single chunk. Returns true on success.
// Note: buildings are currently generated as part of the chunk pipeline; this wrapper
// exists so the loader can track buildings as an explicit step.
bool GenerateBuildingsForChunk(int cx, int cz);

// Determine spawn point near (x,z) by finding nearest road intersection
// Uses already-generated chunk data. Returns best spawn point or input position if no roads found.
glm::vec2 DetermineSpawnPoint(float x, float z, int search_radius_chunks = 2);
