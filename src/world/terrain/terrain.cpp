#include "terrain.h"

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>

#include <utils/frustum/frustum.h>
#include <glm/glm.hpp>
#include <world/roads/roads.h>
#include <world/highways/highways.h>
#include <world/streets/streets.h>
#include <world/buildings/buildings.h>
#include <assets/objects/models/2d/polygon/mesh.h>
#include <limits>
#include <algorithm>
#include <iostream>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <atomic>

#include <core/params/params.h>

struct ChunkBlueprintInput {
    std::vector<float> polygonPoints; // [x0,z0,x1,z1,...] relative to center, world-space units
    size_t vertexCount = 0;           // number of 2D vertices (pairs)
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;
};

struct ChunkCpuData {
    int cx = 0;
    int cz = 0;

    std::vector<std::vector<int>> roadGrid;
    std::vector<BuildingShape> buildings;

    std::vector<float> terrainVertices;
    std::vector<unsigned int> terrainIndices;

    std::vector<float> terrainMeshVertices;
    std::vector<unsigned int> terrainMeshIndices;
    std::vector<float> highwayMeshVertices;
    std::vector<unsigned int> highwayMeshIndices;
    std::vector<float> roadMeshVertices;
    std::vector<unsigned int> roadMeshIndices;
    std::vector<float> streetMeshVertices;
    std::vector<unsigned int> streetMeshIndices;
    std::vector<float> buildingMeshVertices;
    std::vector<unsigned int> buildingMeshIndices;

    std::vector<float> buildingWindowMeshVertices;
    std::vector<unsigned int> buildingWindowMeshIndices;

    // Per building-cell door openings for collision (4-bit mask per cell: N,S,W,E).
    std::vector<uint8_t> buildingDoorMask;

    std::vector<ChunkBlueprintInput> blueprints;
};

struct Chunk {
    int cx, cz; // chunk coordinates
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    // road meshes
    GLuint highwayVAO = 0;
    GLuint highwayVBO = 0;
    GLuint highwayEBO = 0;
    GLuint roadVAO = 0;
    GLuint roadVBO = 0;
    GLuint roadEBO = 0;
    GLuint streetVAO = 0;
    GLuint streetVBO = 0;
    GLuint streetEBO = 0;
    GLuint buildingVAO = 0;
    GLuint buildingVBO = 0;
    GLuint buildingEBO = 0;
    GLuint buildingWindowsVAO = 0;
    GLuint buildingWindowsVBO = 0;
    GLuint buildingWindowsEBO = 0;
    int indexCount = 0;
    int highwayIndexCount = 0;
    int roadIndexCount = 0;
    int streetIndexCount = 0;
    int buildingIndexCount = 0;
    int buildingWindowsIndexCount = 0;
    // store generated polyline data (vector of polylines)
    std::vector<std::vector<glm::vec2>> highwayPolylines;
    std::vector<std::vector<glm::vec2>> roadPolylines;
    std::vector<std::vector<glm::vec2>> streetPolylines;
    // Grid data for road types (0=terrain, 1=highway, 2=road, 3=street, 4=building)
    std::vector<std::vector<int>> roadGrid;
    // Building data
    std::vector<BuildingShape> buildings;
    std::vector<PolygonMesh> buildingPolygonMeshes;

    // Per building-cell door openings for collision (4-bit mask per cell: N,S,W,E).
    std::vector<uint8_t> buildingDoorMask;
    // Terrain vertices for filtering
    std::vector<float> terrainVertices;
    std::vector<unsigned int> terrainIndices;

    // Axis-aligned bounding box enclosing all geometry in this chunk.
    AABB aabb;
};

static std::unordered_map<long long, Chunk> g_chunks;
static noise::module::Perlin g_perlin;
static int g_chunkSize = 128; // vertices per side

// Terrain generation parameters
static float g_scale = 0.5f; // spacing between vertices (larger to make terrain more planar)
static int g_viewRadius = 3; // generate chunks within this radius
// terrain height scaling to reduce steepness
static float g_heightAmplitude = 0.6f; // lower amplitude
static float g_heightFrequency = 0.04f; // lower frequency for gentler slopes

// Async generation state (CPU work off-thread; GPU upload on main thread)
static std::thread g_chunkWorker;
static std::mutex g_chunkWorkerMutex;
static std::condition_variable g_chunkWorkerCv;
static std::queue<std::pair<int, int>> g_chunkRequests;
static std::queue<ChunkCpuData> g_chunkReady;
static std::unordered_set<long long> g_chunkRequestedKeys;
static std::atomic<bool> g_chunkWorkerStop{false};
static std::atomic<bool> g_chunkWorkerRunning{false};

static void UploadMeshToGpu(const std::vector<float>& vertices,
                            const std::vector<unsigned int>& indices,
                            GLuint& VAO,
                            GLuint& VBO,
                            GLuint& EBO,
                            int& indexCount,
                            int floatsPerVertex = 3) {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
    VAO = VBO = EBO = 0;
    indexCount = 0;

    if (vertices.empty() || indices.empty()) return;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, floatsPerVertex * sizeof(float), (void*)0);
    glBindVertexArray(0);
    indexCount = static_cast<int>(indices.size());
}

// -----------------------------------------------------------------------------
// Deterministic hashing helpers (for doors/windows placement)
// -----------------------------------------------------------------------------
static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static double deterministic_unit(int64_t a, int64_t b, int64_t c, int64_t d) {
    uint64_t h = static_cast<uint64_t>(a);
    h = splitmix64(h ^ static_cast<uint64_t>(b));
    h = splitmix64(h ^ static_cast<uint64_t>(c));
    h = splitmix64(h ^ static_cast<uint64_t>(d));
    return static_cast<double>(h & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53);
}

static void StartChunkWorker();
static void StopChunkWorker();
static void RequestChunkAsync(int cx, int cz);
static void ProcessReadyChunks(int currentCx, int currentCz);

long long keyFor(int cx, int cz) {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}

bool InitTerrain() {
    // Load runtime params (with fallbacks to compile-time Config::* values).
    const auto& world = CoreParams::GetWorldParams();
    const auto& terrain = CoreParams::GetTerrainParams();
    g_chunkSize = world.value("chunk_size", 128);
    g_scale = world.value("vertex_spacing", 0.5f);
    g_viewRadius = world.value("view_radius", 3);
    g_heightAmplitude = terrain.value("height_amplitude", 0.6f);
    g_heightFrequency = terrain.value("height_frequency", 0.04f);

    g_perlin.SetSeed(world.value("perlin_seed", 1337));
    g_perlin.SetFrequency(g_heightFrequency);
    StartChunkWorker();
    return true;
}

static float sampleHeight(float x, float z) {
    double v = g_perlin.GetValue(x * g_heightFrequency, z * g_heightFrequency, 0.0);
    return static_cast<float>(v * g_heightAmplitude);
}

float SampleTerrainHeight(float x, float z) {
    return sampleHeight(x, z);
}

static void generateHeightmapAndGrid(Chunk& c) {
    // Generate heightmap and initial grid
    c.terrainVertices.clear();
    c.terrainIndices.clear();
    c.terrainVertices.reserve(g_chunkSize * g_chunkSize * 3);
    c.terrainIndices.reserve((g_chunkSize - 1) * (g_chunkSize - 1) * 6);

    // Generate terrain vertices
    for (int z = 0; z < g_chunkSize; ++z) {
        for (int x = 0; x < g_chunkSize; ++x) {
            float wx = (c.cx * (g_chunkSize - 1) + x) * g_scale;
            float wz = (c.cz * (g_chunkSize - 1) + z) * g_scale;
            float h = sampleHeight(wx, wz);
            c.terrainVertices.push_back(wx);
            c.terrainVertices.push_back(h);
            c.terrainVertices.push_back(wz);
        }
    }

    // Generate terrain indices
    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            int i0 = z * g_chunkSize + x;
            int i1 = i0 + 1;
            int i2 = i0 + g_chunkSize;
            int i3 = i2 + 1;
            // Two triangles
            c.terrainIndices.push_back(i0);
            c.terrainIndices.push_back(i2);
            c.terrainIndices.push_back(i1);
            c.terrainIndices.push_back(i1);
            c.terrainIndices.push_back(i2);
            c.terrainIndices.push_back(i3);
        }
    }

    // Initialize grid (same size as terrain) with all 0s (terrain)
    int grid_size = g_chunkSize;
    c.roadGrid.assign(grid_size, std::vector<int>(grid_size, 0));
}

static void generateGridBasedRoads(Chunk& c) {
    // Grid-based road generation pipeline
    int chunk_size = g_chunkSize;
    const auto& hwyParams = CoreParams::GetHighwayParams();
    const auto& roadParams = CoreParams::GetRoadParams();
    const auto& streetParams = CoreParams::GetStreetParams();

    int padding = hwyParams.value("padding", 8);
    int seed = hwyParams.value("seed", 42);

    // Stage 1: Generate highways (dummy for now)
    c.roadGrid = generate_highways_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding,
        hwyParams.value("num_highways", 2), hwyParams.value("worm_length", 1000), hwyParams.value("step_size", 1.0f),
        hwyParams.value("perlin_scale", 0.01f), seed, hwyParams.value("grid_angles", 36), hwyParams.value("noise_strength", 1.0f));

    // Stage 2: Generate roads
    c.roadGrid = generate_roads_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding,
        roadParams.value("num_roads", 20), roadParams.value("worm_length", 800), roadParams.value("step_size", 1.0f),
        roadParams.value("perlin_scale", 0.01f), seed, roadParams.value("grid_angles", 18), roadParams.value("noise_strength", 10.0f));

    // Stage 3: Generate streets
    c.roadGrid = generate_streets_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding,
        streetParams.value("num_streets", 20), streetParams.value("worm_length", 400), streetParams.value("step_size", 1.0f),
        streetParams.value("perlin_scale", 0.01f), seed, streetParams.value("grid_angles", 12), streetParams.value("noise_strength", 1.0f));

    // Stage 4: Generate buildings
    const auto& buildingParams = CoreParams::GetBuildingParams();
    c.buildings = generate_buildings_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding,
        buildingParams.value("density", 20.0f), buildingParams.value("seed", 42));
}

static void createMeshFromGrid(Chunk& c, int roadType, GLuint& VAO, GLuint& VBO, GLuint& EBO, int& indexCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    
    // Find triangles that belong to this road type
    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            // Only include this quad if the top-left corner matches the road type
            // This ensures each grid cell is rendered by exactly one mesh (no overlaps)
            if (c.roadGrid[z][x] == roadType) {
                // Add the two triangles for this quad
                int baseIdx = vertices.size() / 3;
                
                // Add 4 vertices for this quad
                for (int dz = 0; dz <= 1; ++dz) {
                    for (int dx = 0; dx <= 1; ++dx) {
                        int vertIdx = (z + dz) * g_chunkSize + (x + dx);
                        vertices.push_back(c.terrainVertices[vertIdx * 3]);     // x
                        vertices.push_back(c.terrainVertices[vertIdx * 3 + 1]); // y (same height - no offset)
                        vertices.push_back(c.terrainVertices[vertIdx * 3 + 2]); // z
                    }
                }
                
                // Add indices for two triangles
                indices.push_back(baseIdx + 0); // z, x
                indices.push_back(baseIdx + 2); // z+1, x
                indices.push_back(baseIdx + 1); // z, x+1
                indices.push_back(baseIdx + 1); // z, x+1
                indices.push_back(baseIdx + 2); // z+1, x
                indices.push_back(baseIdx + 3); // z+1, x+1
            }
        }
    }
    
    if (!vertices.empty()) {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
        indexCount = indices.size();
    } else {
        indexCount = 0;
    }
}

// -----------------------------------------------------------------------------
// Buildings: mesh generation lives in src/world/buildings/buildings.cpp
// (BuildBuildingMeshAndDoorsFromGrid)
// -----------------------------------------------------------------------------

static void createBuildingMeshFromGrid(Chunk& c, GLuint& VAO, GLuint& VBO, GLuint& EBO, int& indexCount) {
    std::vector<float> solidVertices;
    std::vector<unsigned int> solidIndices;
    std::vector<float> windowVertices;
    std::vector<unsigned int> windowIndices;
    std::vector<uint8_t> doorMask;

    BuildingsMesh::BuildBuildingMeshesAndDoorsFromGrid(
        c.cx,
        c.cz,
        g_chunkSize,
        c.roadGrid,
        c.buildings,
        c.terrainVertices,
        solidVertices,
        solidIndices,
        windowVertices,
        windowIndices,
        doorMask);

    c.buildingDoorMask = std::move(doorMask);

    if (!solidVertices.empty() && !solidIndices.empty()) {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, solidVertices.size() * sizeof(float), solidVertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, solidIndices.size() * sizeof(unsigned int), solidIndices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
        indexCount = static_cast<int>(solidIndices.size());
    } else {
        indexCount = 0;
    }

    // Upload windows mesh to its own buffers on the chunk.
    UploadMeshToGpu(windowVertices, windowIndices,
                    c.buildingWindowsVAO, c.buildingWindowsVBO, c.buildingWindowsEBO,
                    c.buildingWindowsIndexCount);
}

static void createChunkMesh(Chunk& c) {
    // Generate heightmap and initial grid
    generateHeightmapAndGrid(c);
    
    // Generate grid-based roads
    generateGridBasedRoads(c);
    
    // Create meshes for each road type
    createMeshFromGrid(c, TERRAIN, c.VAO, c.VBO, c.EBO, c.indexCount);
    createMeshFromGrid(c, HIGHWAY, c.highwayVAO, c.highwayVBO, c.highwayEBO, c.highwayIndexCount);
    createMeshFromGrid(c, ROAD, c.roadVAO, c.roadVBO, c.roadEBO, c.roadIndexCount);
    createMeshFromGrid(c, STREET, c.streetVAO, c.streetVBO, c.streetEBO, c.streetIndexCount);
    createBuildingMeshFromGrid(c, c.buildingVAO, c.buildingVBO, c.buildingEBO, c.buildingIndexCount);

    c.buildingPolygonMeshes.clear();
    c.buildingPolygonMeshes.reserve(c.buildings.size());
    
    // World offset for this chunk
    float wx = c.cx * (g_chunkSize - 1) * g_scale;
    float wz = c.cz * (g_chunkSize - 1) * g_scale;
    
    for (const auto& building : c.buildings) {
        // Calculate center in grid coordinates
        float gridCenterX = 0.0f, gridCenterZ = 0.0f;
        for (const auto& pt : building.points) {
            gridCenterX += pt.x;
            gridCenterZ += pt.y;
        }
        gridCenterX /= building.points.size();
        gridCenterZ /= building.points.size();
        
        // Convert to world coordinates for the building center
        float worldCenterX = wx + gridCenterX * g_scale;
        float worldCenterZ = wz + gridCenterZ * g_scale;
        
        // Build polygon points relative to center, scaled to world space
        std::vector<float> polygonPoints;
        polygonPoints.reserve(building.points.size() * 2);
        for (const auto& pt : building.points) {
            polygonPoints.push_back((pt.x - gridCenterX) * g_scale);
            polygonPoints.push_back((pt.y - gridCenterZ) * g_scale);
        }
        
        // Sample terrain height at building center
        float centerY = sampleHeight(worldCenterX, worldCenterZ);
        
        // Create 2D polygon mesh for building blueprint
        PolygonMesh blueprintMesh = CreatePolygonMesh(
            polygonPoints.data(),
            building.points.size(),
            worldCenterX,
            centerY + 0.02f,  // Slightly above terrain to avoid z-fighting
            worldCenterZ
        );
        
        c.buildingPolygonMeshes.push_back(blueprintMesh);
    }
}

// Compute an AABB that encloses all geometry in the chunk (terrain + buildings).
static void computeChunkAABB(Chunk& c) {
    const float chunkWorld = (g_chunkSize - 1) * g_scale;
    c.aabb.min.x = c.cx * chunkWorld;
    c.aabb.min.z = c.cz * chunkWorld;
    c.aabb.max.x = c.aabb.min.x + chunkWorld;
    c.aabb.max.z = c.aabb.min.z + chunkWorld;

    // Y range from terrain vertices.
    float yMin =  1e30f;
    float yMax = -1e30f;
    for (size_t i = 1; i < c.terrainVertices.size(); i += 3) {
        float y = c.terrainVertices[i];
        if (y < yMin) yMin = y;
        if (y > yMax) yMax = y;
    }

    // Extend upward by the tallest building in the chunk.
    float maxBuildingH = 0.0f;
    for (const auto& b : c.buildings) {
        if (b.height > maxBuildingH) maxBuildingH = b.height;
    }
    yMax += maxBuildingH;

    // Small safety margin.
    c.aabb.min.y = yMin - 1.0f;
    c.aabb.max.y = yMax + 1.0f;
}

// Public API: generate terrain chunk with complete pipeline
bool GenerateTerrainChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) != g_chunks.end()) return true; // already present
    Chunk c;
    c.cx = cx; c.cz = cz;
    createChunkMesh(c);
    computeChunkAABB(c);
    g_chunks[k] = c;
    return true;
}

bool GenerateRoadsForChunk(int cx, int cz) {
    // Roads are now generated as part of the main terrain generation pipeline
    return GenerateTerrainChunk(cx, cz);
}

void GenerateStreetsForChunk(int cx, int cz) {
    // Streets are now generated as part of the main terrain generation pipeline
    GenerateTerrainChunk(cx, cz);
}

bool GenerateBuildingsForChunk(int cx, int cz) {
    // Buildings are generated as part of the main terrain generation pipeline.
    return GenerateTerrainChunk(cx, cz);
}
// Determine spawn point using already-generated chunk data (avoid regenerating roads)
// Returns the best spawn point found, or the input position if no roads exist
glm::vec2 DetermineSpawnPoint(float x, float z, int search_radius_chunks) {
    // compute chunk containing point
    int cx = static_cast<int>(std::floor(x / ((g_chunkSize - 1) * g_scale)));
    int cz = static_cast<int>(std::floor(z / ((g_chunkSize - 1) * g_scale)));

    float bestDist2 = std::numeric_limits<float>::infinity();
    glm::vec2 bestPoint(x, z);
    int bestScore = 0;
    const float intersectionRadius = CoreParams::GetRoadParams().value("intersection_radius", 4.0f);

    for (int dz = -search_radius_chunks; dz <= search_radius_chunks; ++dz) {
        for (int dx = -search_radius_chunks; dx <= search_radius_chunks; ++dx) {
            int nx = cx + dx;
            int nz = cz + dz;
            long long k = keyFor(nx, nz);
            auto it = g_chunks.find(k);
            if (it == g_chunks.end()) continue; // skip chunks not generated
            Chunk &c = it->second;
            // use stored polylines
            for (auto &poly : c.roadPolylines) {
                if (poly.size() < 2) continue;
                for (size_t i = 0; i + 1 < poly.size(); ++i) {
                    glm::vec2 a = poly[i];
                    glm::vec2 b = poly[i+1];
                    glm::vec2 ab = b - a;
                    float abLen2 = glm::dot(ab, ab);
                    float t = 0.0f;
                    if (abLen2 > 0.00001f) {
                        t = glm::dot(glm::vec2(x, z) - a, ab) / abLen2;
                        t = std::max(0.0f, std::min(1.0f, t));
                    }
                    glm::vec2 proj = a + ab * t;
                    float dxp = proj.x - x;
                    float dzp = proj.y - z;
                    float d2 = dxp*dxp + dzp*dzp;

                    int score = 0;
                    // basic intersection scoring: count other nearby points in this chunk's polylines
                    for (auto &otherPoly : c.roadPolylines) {
                        for (auto &other : otherPoly) {
                            float ddx = other.x - proj.x;
                            float ddz = other.y - proj.y;
                            if (ddx*ddx + ddz*ddz <= intersectionRadius * intersectionRadius) ++score;
                        }
                    }

                    if (score > bestScore || (score == bestScore && d2 < bestDist2)) {
                        bestScore = score;
                        bestDist2 = d2;
                        bestPoint = proj;
                    }
                }
            }
        }
    }

    return bestPoint;
}

static void destroyChunk(Chunk& c) {
    if (c.VAO) glDeleteVertexArrays(1, &c.VAO);
    if (c.VBO) glDeleteBuffers(1, &c.VBO);
    if (c.EBO) glDeleteBuffers(1, &c.EBO);
    c.VAO = c.VBO = c.EBO = 0;
    c.indexCount = 0;

    if (c.highwayVAO) glDeleteVertexArrays(1, &c.highwayVAO);
    if (c.highwayVBO) glDeleteBuffers(1, &c.highwayVBO);
    if (c.highwayEBO) glDeleteBuffers(1, &c.highwayEBO);
    c.highwayVAO = c.highwayVBO = c.highwayEBO = 0;
    c.highwayIndexCount = 0;

    if (c.roadVAO) glDeleteVertexArrays(1, &c.roadVAO);
    if (c.roadVBO) glDeleteBuffers(1, &c.roadVBO);
    if (c.roadEBO) glDeleteBuffers(1, &c.roadEBO);
    c.roadVAO = c.roadVBO = c.roadEBO = 0;
    c.roadIndexCount = 0;

    if (c.streetVAO) glDeleteVertexArrays(1, &c.streetVAO);
    if (c.streetVBO) glDeleteBuffers(1, &c.streetVBO);
    if (c.streetEBO) glDeleteBuffers(1, &c.streetEBO);
    c.streetVAO = c.streetVBO = c.streetEBO = 0;
    c.streetIndexCount = 0;
    
    if (c.buildingVAO) glDeleteVertexArrays(1, &c.buildingVAO);
    if (c.buildingVBO) glDeleteBuffers(1, &c.buildingVBO);
    if (c.buildingEBO) glDeleteBuffers(1, &c.buildingEBO);
    c.buildingVAO = c.buildingVBO = c.buildingEBO = 0;
    c.buildingIndexCount = 0;

    if (c.buildingWindowsVAO) glDeleteVertexArrays(1, &c.buildingWindowsVAO);
    if (c.buildingWindowsVBO) glDeleteBuffers(1, &c.buildingWindowsVBO);
    if (c.buildingWindowsEBO) glDeleteBuffers(1, &c.buildingWindowsEBO);
    c.buildingWindowsVAO = c.buildingWindowsVBO = c.buildingWindowsEBO = 0;
    c.buildingWindowsIndexCount = 0;
    
    // Cleanup building polygon meshes
    for (auto& polygonMesh : c.buildingPolygonMeshes) {
        DestroyPolygonMesh(polygonMesh);
    }
    c.buildingPolygonMeshes.clear();
}

static float sampleHeightWithPerlin(noise::module::Perlin& perlin, float x, float z) {
    double v = perlin.GetValue(x * g_heightFrequency, z * g_heightFrequency, 0.0);
    return static_cast<float>(v * g_heightAmplitude);
}

static void GenerateHeightmapAndInitGridCpu(ChunkCpuData& out, noise::module::Perlin& perlin) {
    out.terrainVertices.clear();
    out.terrainIndices.clear();
    out.terrainVertices.reserve(g_chunkSize * g_chunkSize * 3);
    out.terrainIndices.reserve((g_chunkSize - 1) * (g_chunkSize - 1) * 6);

    for (int z = 0; z < g_chunkSize; ++z) {
        for (int x = 0; x < g_chunkSize; ++x) {
            float wx = (out.cx * (g_chunkSize - 1) + x) * g_scale;
            float wz = (out.cz * (g_chunkSize - 1) + z) * g_scale;
            float h = sampleHeightWithPerlin(perlin, wx, wz);
            out.terrainVertices.push_back(wx);
            out.terrainVertices.push_back(h);
            out.terrainVertices.push_back(wz);
        }
    }

    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            int i0 = z * g_chunkSize + x;
            int i1 = i0 + 1;
            int i2 = i0 + g_chunkSize;
            int i3 = i2 + 1;
            out.terrainIndices.push_back(i0);
            out.terrainIndices.push_back(i2);
            out.terrainIndices.push_back(i1);
            out.terrainIndices.push_back(i1);
            out.terrainIndices.push_back(i2);
            out.terrainIndices.push_back(i3);
        }
    }

    int grid_size = g_chunkSize;
    out.roadGrid.assign(grid_size, std::vector<int>(grid_size, 0));
}

static void GenerateGridBasedRoadsCpu(ChunkCpuData& out) {
    int chunk_size = g_chunkSize;
    const auto& hwyParams = CoreParams::GetHighwayParams();
    const auto& roadParams = CoreParams::GetRoadParams();
    const auto& streetParams = CoreParams::GetStreetParams();

    int padding = hwyParams.value("padding", 8);
    int seed = hwyParams.value("seed", 42);

    out.roadGrid = generate_highways_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        hwyParams.value("num_highways", 2), hwyParams.value("worm_length", 1000), hwyParams.value("step_size", 1.0f),
        hwyParams.value("perlin_scale", 0.01f), seed, hwyParams.value("grid_angles", 36), hwyParams.value("noise_strength", 1.0f));

    out.roadGrid = generate_roads_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        roadParams.value("num_roads", 20), roadParams.value("worm_length", 800), roadParams.value("step_size", 1.0f),
        roadParams.value("perlin_scale", 0.01f), seed, roadParams.value("grid_angles", 18), roadParams.value("noise_strength", 10.0f));

    out.roadGrid = generate_streets_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        streetParams.value("num_streets", 20), streetParams.value("worm_length", 400), streetParams.value("step_size", 1.0f),
        streetParams.value("perlin_scale", 0.01f), seed, streetParams.value("grid_angles", 12), streetParams.value("noise_strength", 1.0f));

    const auto& buildingParams = CoreParams::GetBuildingParams();
    out.buildings = generate_buildings_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        buildingParams.value("density", 20.0f), buildingParams.value("seed", 42));
}

static void BuildMeshFromGridCpu(const ChunkCpuData& src, int roadType, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    vertices.clear();
    indices.clear();

    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            if (src.roadGrid[z][x] != roadType) continue;

            unsigned int baseIdx = static_cast<unsigned int>(vertices.size() / 3);
            for (int dz = 0; dz <= 1; ++dz) {
                for (int dx = 0; dx <= 1; ++dx) {
                    int vertIdx = (z + dz) * g_chunkSize + (x + dx);
                    vertices.push_back(src.terrainVertices[vertIdx * 3]);
                    vertices.push_back(src.terrainVertices[vertIdx * 3 + 1]);
                    vertices.push_back(src.terrainVertices[vertIdx * 3 + 2]);
                }
            }

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        }
    }
}

static void BuildBuildingMeshFromGridCpu(const ChunkCpuData& src,
                                        std::vector<float>& vertices,
                                        std::vector<unsigned int>& indices,
                                        std::vector<float>& windowVertices,
                                        std::vector<unsigned int>& windowIndices,
                                        std::vector<uint8_t>& doorMask) {
    BuildingsMesh::BuildBuildingMeshesAndDoorsFromGrid(
        src.cx,
        src.cz,
        g_chunkSize,
        src.roadGrid,
        src.buildings,
        src.terrainVertices,
        vertices,
        indices,
        windowVertices,
        windowIndices,
        doorMask);
}

static void BuildBlueprintInputsCpu(ChunkCpuData& out, noise::module::Perlin& perlin) {
    out.blueprints.clear();
    out.blueprints.reserve(out.buildings.size());

    float wx = out.cx * (g_chunkSize - 1) * g_scale;
    float wz = out.cz * (g_chunkSize - 1) * g_scale;

    for (const auto& building : out.buildings) {
        if (building.points.size() < 3) continue;

        float gridCenterX = 0.0f, gridCenterZ = 0.0f;
        for (const auto& pt : building.points) {
            gridCenterX += pt.x;
            gridCenterZ += pt.y;
        }
        gridCenterX /= building.points.size();
        gridCenterZ /= building.points.size();

        float worldCenterX = wx + gridCenterX * g_scale;
        float worldCenterZ = wz + gridCenterZ * g_scale;
        float centerY = sampleHeightWithPerlin(perlin, worldCenterX, worldCenterZ);

        ChunkBlueprintInput bi;
        bi.centerX = worldCenterX;
        bi.centerZ = worldCenterZ;
        bi.centerY = centerY + 0.02f;
        bi.vertexCount = building.points.size();
        bi.polygonPoints.reserve(building.points.size() * 2);
        for (const auto& pt : building.points) {
            bi.polygonPoints.push_back((pt.x - gridCenterX) * g_scale);
            bi.polygonPoints.push_back((pt.y - gridCenterZ) * g_scale);
        }
        out.blueprints.push_back(std::move(bi));
    }
}

static ChunkCpuData GenerateChunkCpuData(int cx, int cz, noise::module::Perlin& perlin) {
    ChunkCpuData out;
    out.cx = cx;
    out.cz = cz;
    GenerateHeightmapAndInitGridCpu(out, perlin);
    GenerateGridBasedRoadsCpu(out);

    BuildMeshFromGridCpu(out, TERRAIN, out.terrainMeshVertices, out.terrainMeshIndices);
    BuildMeshFromGridCpu(out, HIGHWAY, out.highwayMeshVertices, out.highwayMeshIndices);
    BuildMeshFromGridCpu(out, ROAD, out.roadMeshVertices, out.roadMeshIndices);
    BuildMeshFromGridCpu(out, STREET, out.streetMeshVertices, out.streetMeshIndices);
    BuildBuildingMeshFromGridCpu(out,
                                out.buildingMeshVertices,
                                out.buildingMeshIndices,
                                out.buildingWindowMeshVertices,
                                out.buildingWindowMeshIndices,
                                out.buildingDoorMask);
    BuildBlueprintInputsCpu(out, perlin);
    return out;
}

static void ChunkWorkerMain() {
    noise::module::Perlin perlinLocal;
    perlinLocal.SetSeed(CoreParams::GetWorldParams().value("perlin_seed", 1337));
    perlinLocal.SetFrequency(g_heightFrequency);

    while (!g_chunkWorkerStop.load()) {
        std::pair<int, int> req;
        {
            std::unique_lock<std::mutex> lock(g_chunkWorkerMutex);
            g_chunkWorkerCv.wait(lock, [](){
                return g_chunkWorkerStop.load() || !g_chunkRequests.empty();
            });
            if (g_chunkWorkerStop.load()) break;
            req = g_chunkRequests.front();
            g_chunkRequests.pop();
        }

        ChunkCpuData data = GenerateChunkCpuData(req.first, req.second, perlinLocal);
        {
            std::lock_guard<std::mutex> lock(g_chunkWorkerMutex);
            g_chunkReady.push(std::move(data));
        }
    }
}

static void StartChunkWorker() {
    if (g_chunkWorkerRunning.load()) return;
    g_chunkWorkerStop = false;
    g_chunkWorkerRunning = true;
    g_chunkWorker = std::thread(ChunkWorkerMain);
}

static void StopChunkWorker() {
    g_chunkWorkerStop = true;
    g_chunkWorkerCv.notify_all();
    if (g_chunkWorkerRunning.exchange(false)) {
        if (g_chunkWorker.joinable()) g_chunkWorker.join();
    }
    std::lock_guard<std::mutex> lock(g_chunkWorkerMutex);
    while (!g_chunkRequests.empty()) g_chunkRequests.pop();
    while (!g_chunkReady.empty()) g_chunkReady.pop();
    g_chunkRequestedKeys.clear();
}

static void RequestChunkAsync(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) != g_chunks.end()) return;

    {
        std::lock_guard<std::mutex> lock(g_chunkWorkerMutex);
        if (g_chunkRequestedKeys.find(k) != g_chunkRequestedKeys.end()) return;
        g_chunkRequestedKeys.insert(k);
        g_chunkRequests.push({cx, cz});
    }
    g_chunkWorkerCv.notify_one();
}

static void ProcessReadyChunks(int currentCx, int currentCz) {
    const int maxUploadsPerFrame = 1;
    int uploaded = 0;

    while (uploaded < maxUploadsPerFrame) {
        ChunkCpuData data;
        {
            std::lock_guard<std::mutex> lock(g_chunkWorkerMutex);
            if (g_chunkReady.empty()) break;
            data = std::move(g_chunkReady.front());
            g_chunkReady.pop();
            g_chunkRequestedKeys.erase(keyFor(data.cx, data.cz));
        }

        if (std::abs(data.cx - currentCx) > g_viewRadius || std::abs(data.cz - currentCz) > g_viewRadius) {
            continue;
        }

        long long k = keyFor(data.cx, data.cz);
        if (g_chunks.find(k) != g_chunks.end()) continue;

        Chunk c;
        c.cx = data.cx;
        c.cz = data.cz;
        c.roadGrid = std::move(data.roadGrid);
        c.buildings = std::move(data.buildings);
        c.terrainVertices = std::move(data.terrainVertices);
        c.terrainIndices = std::move(data.terrainIndices);

        UploadMeshToGpu(data.terrainMeshVertices, data.terrainMeshIndices, c.VAO, c.VBO, c.EBO, c.indexCount);
        UploadMeshToGpu(data.highwayMeshVertices, data.highwayMeshIndices, c.highwayVAO, c.highwayVBO, c.highwayEBO, c.highwayIndexCount);
        UploadMeshToGpu(data.roadMeshVertices, data.roadMeshIndices, c.roadVAO, c.roadVBO, c.roadEBO, c.roadIndexCount);
        UploadMeshToGpu(data.streetMeshVertices, data.streetMeshIndices, c.streetVAO, c.streetVBO, c.streetEBO, c.streetIndexCount);
        UploadMeshToGpu(data.buildingMeshVertices, data.buildingMeshIndices, c.buildingVAO, c.buildingVBO, c.buildingEBO, c.buildingIndexCount);
        UploadMeshToGpu(data.buildingWindowMeshVertices, data.buildingWindowMeshIndices, c.buildingWindowsVAO, c.buildingWindowsVBO, c.buildingWindowsEBO, c.buildingWindowsIndexCount);

        c.buildingDoorMask = std::move(data.buildingDoorMask);

        c.buildingPolygonMeshes.clear();
        c.buildingPolygonMeshes.reserve(data.blueprints.size());
        for (const auto& bp : data.blueprints) {
            if (bp.vertexCount < 3 || bp.polygonPoints.empty()) continue;
            PolygonMesh blueprintMesh = CreatePolygonMesh(
                bp.polygonPoints.data(),
                bp.vertexCount,
                bp.centerX,
                bp.centerY,
                bp.centerZ
            );
            c.buildingPolygonMeshes.push_back(blueprintMesh);
        }

        computeChunkAABB(c);
        g_chunks[k] = std::move(c);
        ++uploaded;
    }
}

void UpdateTerrain(const glm::vec3& cameraPos) {
    int cx = static_cast<int>(std::floor(cameraPos.x / ((g_chunkSize - 1) * g_scale)));
    int cz = static_cast<int>(std::floor(cameraPos.z / ((g_chunkSize - 1) * g_scale)));

    // Integrate completed background chunks (GPU upload happens here on main thread)
    ProcessReadyChunks(cx, cz);

    // ensure chunks in view radius exist
    for (int dz = -g_viewRadius; dz <= g_viewRadius; ++dz) {
        for (int dx = -g_viewRadius; dx <= g_viewRadius; ++dx) {
            int nx = cx + dx;
            int nz = cz + dz;
            long long k = keyFor(nx, nz);
            if (g_chunks.find(k) == g_chunks.end()) {
                RequestChunkAsync(nx, nz);
            }
        }
    }

    // remove distant chunks
    std::vector<long long> toRemove;
    for (auto& kv : g_chunks) {
        int dx = (int)(kv.second.cx - cx);
        int dz = (int)(kv.second.cz - cz);
        if (std::abs(dx) > g_viewRadius || std::abs(dz) > g_viewRadius) {
            toRemove.push_back(kv.first);
        }
    }
    for (auto k : toRemove) {
        destroyChunk(g_chunks[k]);
        g_chunks.erase(k);
    }
}

void RenderTerrain(GLuint terrainProgram,
                   GLuint highwaysProgram,
                   GLuint roadsProgram,
                   GLuint streetsProgram,
                   GLuint buildingsProgram,
                   GLuint buildingWindowsProgram,
                   const glm::mat4& proj,
                   const glm::mat4& view) {
    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = proj * view * model;

        // Render terrain
        if (terrainProgram != 0 && c.VAO != 0 && c.indexCount > 0) {
            glUseProgram(terrainProgram);
            GLint loc = glGetUniformLocation(terrainProgram, "uMVP");
                GLint colorLoc = glGetUniformLocation(terrainProgram, "uColor");
                const auto& terrainParams = CoreParams::GetTerrainParams();
                const float terrainR = terrainParams.value("color_r", 76) / 255.0f;
                const float terrainG = terrainParams.value("color_g", 204) / 255.0f;
                const float terrainB = terrainParams.value("color_b", 76) / 255.0f;
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, terrainR, terrainG, terrainB);
            glBindVertexArray(c.VAO);
            glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Render highways
        if (highwaysProgram != 0 && c.highwayVAO != 0 && c.highwayIndexCount > 0) {
            glUseProgram(highwaysProgram);
            GLint loc = glGetUniformLocation(highwaysProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(highwaysProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            const auto& hwyParams = CoreParams::GetHighwayParams();
            glUniform3f(colorLoc, hwyParams.value("color_r", 255) / 255.0f, hwyParams.value("color_g", 0) / 255.0f, hwyParams.value("color_b", 0) / 255.0f);
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render roads
        if (roadsProgram != 0 && c.roadVAO != 0 && c.roadIndexCount > 0) {
            glUseProgram(roadsProgram);
            GLint loc = glGetUniformLocation(roadsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(roadsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            const auto& roadParams = CoreParams::GetRoadParams();
            glUniform3f(colorLoc, roadParams.value("color_r", 255) / 255.0f, roadParams.value("color_g", 255) / 255.0f, roadParams.value("color_b", 0) / 255.0f);
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render streets
        if (streetsProgram != 0 && c.streetVAO != 0 && c.streetIndexCount > 0) {
            glUseProgram(streetsProgram);
            GLint loc = glGetUniformLocation(streetsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(streetsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            const auto& streetParams = CoreParams::GetStreetParams();
            glUniform3f(colorLoc, streetParams.value("color_r", 0) / 255.0f, streetParams.value("color_g", 0) / 255.0f, streetParams.value("color_b", 255) / 255.0f);
            glBindVertexArray(c.streetVAO);
            glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
        }
        
        // Render buildings (solid mesh)
        if (buildingsProgram != 0 && c.buildingVAO != 0 && c.buildingIndexCount > 0) {
            glUseProgram(buildingsProgram);
            GLint loc = glGetUniformLocation(buildingsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(buildingsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            const auto& buildingParams = CoreParams::GetBuildingParams();
            const float wr = buildingParams.value("color_r", 180) / 255.0f;
            const float wg = buildingParams.value("color_g", 180) / 255.0f;
            const float wb = buildingParams.value("color_b", 180) / 255.0f;
            glUniform3f(colorLoc, wr, wg, wb);
            glBindVertexArray(c.buildingVAO);
            glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render building windows (separate mesh + shader)
        if (buildingWindowsProgram != 0 && c.buildingWindowsVAO != 0 && c.buildingWindowsIndexCount > 0) {
            glUseProgram(buildingWindowsProgram);
            GLint loc = glGetUniformLocation(buildingWindowsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(buildingWindowsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            const auto& buildingParams = CoreParams::GetBuildingParams();
            const float wr = buildingParams.value("color_r", 180) / 255.0f;
            const float wg = buildingParams.value("color_g", 180) / 255.0f;
            const float wb = buildingParams.value("color_b", 180) / 255.0f;
            const float winR = std::clamp(wr * 0.45f + 0.12f, 0.0f, 1.0f);
            const float winG = std::clamp(wg * 0.55f + 0.20f, 0.0f, 1.0f);
            const float winB = std::clamp(wb * 0.60f + 0.35f, 0.0f, 1.0f);
            glUniform3f(colorLoc, winR, winG, winB);
            glBindVertexArray(c.buildingWindowsVAO);
            glDrawElements(GL_TRIANGLES, c.buildingWindowsIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void CleanupTerrain() {
    StopChunkWorker();
    for (auto& kv : g_chunks) {
        destroyChunk(kv.second);
    }
    g_chunks.clear();
}

// Helper: compute squared distance from camera to chunk AABB center.
static float ChunkDistSq(const Chunk& c, const glm::vec3& cam) {
    const glm::vec3 center = (c.aabb.min + c.aabb.max) * 0.5f;
    const glm::vec3 d = center - cam;
    return glm::dot(d, d);
}

// Render a single chunk's geometry. Terrain/roads are always drawn; buildings can be skipped.
static void DrawChunkGeometry(const Chunk& c,
                              GLint modelLoc,
                              GLint colorLoc,
                              bool drawBuildings) {
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

    if (c.VAO != 0 && c.indexCount > 0) {
        const auto& terrainParams = CoreParams::GetTerrainParams();
        glUniform3f(colorLoc, terrainParams.value("color_r", 76) / 255.0f, terrainParams.value("color_g", 204) / 255.0f, terrainParams.value("color_b", 76) / 255.0f);
        glBindVertexArray(c.VAO);
        glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
    }
    if (c.highwayVAO != 0 && c.highwayIndexCount > 0) {
        const auto& hwyParams = CoreParams::GetHighwayParams();
        glUniform3f(colorLoc, hwyParams.value("color_r", 255) / 255.0f, hwyParams.value("color_g", 0) / 255.0f, hwyParams.value("color_b", 0) / 255.0f);
        glBindVertexArray(c.highwayVAO);
        glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
    }
    if (c.roadVAO != 0 && c.roadIndexCount > 0) {
        const auto& roadParams = CoreParams::GetRoadParams();
        glUniform3f(colorLoc, roadParams.value("color_r", 255) / 255.0f, roadParams.value("color_g", 255) / 255.0f, roadParams.value("color_b", 0) / 255.0f);
        glBindVertexArray(c.roadVAO);
        glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
    }
    if (c.streetVAO != 0 && c.streetIndexCount > 0) {
        const auto& streetParams = CoreParams::GetStreetParams();
        glUniform3f(colorLoc, streetParams.value("color_r", 0) / 255.0f, streetParams.value("color_g", 0) / 255.0f, streetParams.value("color_b", 255) / 255.0f);
        glBindVertexArray(c.streetVAO);
        glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
    }
    if (drawBuildings && c.buildingVAO != 0 && c.buildingIndexCount > 0) {
        const auto& buildingParams = CoreParams::GetBuildingParams();
        const float wr = buildingParams.value("color_r", 180) / 255.0f;
        const float wg = buildingParams.value("color_g", 180) / 255.0f;
        const float wb = buildingParams.value("color_b", 180) / 255.0f;
        glUniform3f(colorLoc, wr, wg, wb);
        glBindVertexArray(c.buildingVAO);
        glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
    }
}

// Render terrain to G-buffer for deferred rendering.
// Preprocessing: skip buildings for chunks outside the camera view frustum.
void RenderTerrainToGBuffer(GLuint geometryShader,
                            GLuint windowsGeometryShader,
                            const glm::mat4& proj,
                            const glm::mat4& view,
                            const glm::vec3& cameraPos,
                            const glm::vec3& cameraFront) {
    if (geometryShader == 0) return;

    glUseProgram(geometryShader);

    GLint modelLoc = glGetUniformLocation(geometryShader, "model");
    GLint viewLoc  = glGetUniformLocation(geometryShader, "view");
    GLint projLoc  = glGetUniformLocation(geometryShader, "projection");
    GLint colorLoc = glGetUniformLocation(geometryShader, "uColor");

    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));

    const FrustumUtil::Frustum frustum = FrustumUtil::ExtractFrustum(proj * view);

    for (auto& kv : g_chunks) {
        const Chunk& c = kv.second;
        (void)cameraPos;
        (void)cameraFront;
        const bool visible = FrustumUtil::IntersectsAabb(frustum, c.aabb.min, c.aabb.max);
        DrawChunkGeometry(c, modelLoc, colorLoc, /*drawBuildings=*/visible);
    }

    // Draw windows as a second pass with their own geometry shader (still writing to the same G-buffer).
    if (windowsGeometryShader != 0) {
        glUseProgram(windowsGeometryShader);

        GLint wModelLoc = glGetUniformLocation(windowsGeometryShader, "model");
        GLint wViewLoc  = glGetUniformLocation(windowsGeometryShader, "view");
        GLint wProjLoc  = glGetUniformLocation(windowsGeometryShader, "projection");
        GLint wColorLoc = glGetUniformLocation(windowsGeometryShader, "uColor");

        glUniformMatrix4fv(wViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(wProjLoc, 1, GL_FALSE, glm::value_ptr(proj));

        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(wModelLoc, 1, GL_FALSE, glm::value_ptr(model));

        const auto& buildingParams = CoreParams::GetBuildingParams();
        const float wr = buildingParams.value("color_r", 180) / 255.0f;
        const float wg = buildingParams.value("color_g", 180) / 255.0f;
        const float wb = buildingParams.value("color_b", 180) / 255.0f;
        const float winR = std::clamp(wr * 0.45f + 0.12f, 0.0f, 1.0f);
        const float winG = std::clamp(wg * 0.55f + 0.20f, 0.0f, 1.0f);
        const float winB = std::clamp(wb * 0.60f + 0.35f, 0.0f, 1.0f);
        glUniform3f(wColorLoc, winR, winG, winB);

        for (auto& kv : g_chunks) {
            const Chunk& c = kv.second;
            const bool visible = FrustumUtil::IntersectsAabb(frustum, c.aabb.min, c.aabb.max);
            if (!visible) continue;
            if (c.buildingWindowsVAO == 0 || c.buildingWindowsIndexCount <= 0) continue;
            glBindVertexArray(c.buildingWindowsVAO);
            glDrawElements(GL_TRIANGLES, c.buildingWindowsIndexCount, GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
    }

    glBindVertexArray(0);
}

// Render terrain to shadow map.
// Same preprocessing rule as the G-buffer pass (camera frustum-based building visibility).
void RenderTerrainToShadowMap(GLuint shadowShader,
                              const glm::mat4& lightSpaceMatrix,
                              const glm::mat4& proj,
                              const glm::mat4& view) {
    if (shadowShader == 0) return;

    glUseProgram(shadowShader);
    
    GLint lightSpaceLoc = glGetUniformLocation(shadowShader, "lightSpaceMatrix");
    GLint modelLoc = glGetUniformLocation(shadowShader, "model");
    
    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    const FrustumUtil::Frustum frustum = FrustumUtil::ExtractFrustum(proj * view);

    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
        const bool visible = FrustumUtil::IntersectsAabb(frustum, c.aabb.min, c.aabb.max);
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        // Render all geometry to shadow map
        if (c.VAO != 0 && c.indexCount > 0) {
            glBindVertexArray(c.VAO);
            glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        }

        if (c.highwayVAO != 0 && c.highwayIndexCount > 0) {
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        if (c.roadVAO != 0 && c.roadIndexCount > 0) {
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        if (c.streetVAO != 0 && c.streetIndexCount > 0) {
            glBindVertexArray(c.streetVAO);
            glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
        }
        
        if (visible) {
            if (c.buildingVAO != 0 && c.buildingIndexCount > 0) {
                glBindVertexArray(c.buildingVAO);
                glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
            }
            if (c.buildingWindowsVAO != 0 && c.buildingWindowsIndexCount > 0) {
                glBindVertexArray(c.buildingWindowsVAO);
                glDrawElements(GL_TRIANGLES, c.buildingWindowsIndexCount, GL_UNSIGNED_INT, 0);
            }
        }
    }

    glBindVertexArray(0);
}

void WorldToChunk(float x, float z, int &out_cx, int &out_cz) {
    out_cx = static_cast<int>(std::floor(x / ((g_chunkSize - 1) * g_scale)));
    out_cz = static_cast<int>(std::floor(z / ((g_chunkSize - 1) * g_scale)));
}

bool GetChunkRoadGrid(int cx, int cz, const std::vector<std::vector<int>>*& outGrid) {
    outGrid = nullptr;
    const auto it = g_chunks.find(keyFor(cx, cz));
    if (it == g_chunks.end()) return false;
    if (it->second.roadGrid.empty()) return false;
    outGrid = &it->second.roadGrid;
    return true;
}

static bool isBuildingCellGlobal(int cellX, int cellZ) {
    const int cellsPerChunk = g_chunkSize - 1;
    if (cellsPerChunk <= 0) return false;

    // Use floor division that behaves for negative coordinates.
    const int cx = static_cast<int>(std::floor(static_cast<float>(cellX) / static_cast<float>(cellsPerChunk)));
    const int cz = static_cast<int>(std::floor(static_cast<float>(cellZ) / static_cast<float>(cellsPerChunk)));
    const int localX = cellX - cx * cellsPerChunk;
    const int localZ = cellZ - cz * cellsPerChunk;

    if (localX < 0 || localX >= cellsPerChunk || localZ < 0 || localZ >= cellsPerChunk) return false;
    auto it = g_chunks.find(keyFor(cx, cz));
    if (it == g_chunks.end()) return false;
    Chunk& c = it->second;
    if (c.roadGrid.empty()) return false;
    if (localZ >= static_cast<int>(c.roadGrid.size())) return false;
    if (localX >= static_cast<int>(c.roadGrid[localZ].size())) return false;
    return c.roadGrid[localZ][localX] == BUILDING;
}

static uint8_t doorMaskCellGlobal(int cellX, int cellZ) {
    const int cellsPerChunk = g_chunkSize - 1;
    if (cellsPerChunk <= 0) return 0;

    const int cx = static_cast<int>(std::floor(static_cast<float>(cellX) / static_cast<float>(cellsPerChunk)));
    const int cz = static_cast<int>(std::floor(static_cast<float>(cellZ) / static_cast<float>(cellsPerChunk)));
    const int localX = cellX - cx * cellsPerChunk;
    const int localZ = cellZ - cz * cellsPerChunk;

    if (localX < 0 || localX >= cellsPerChunk || localZ < 0 || localZ >= cellsPerChunk) return 0;
    auto it = g_chunks.find(keyFor(cx, cz));
    if (it == g_chunks.end()) return 0;
    Chunk& c = it->second;
    if (c.buildingDoorMask.empty()) return 0;
    const size_t idx = static_cast<size_t>(localZ * cellsPerChunk + localX);
    if (idx >= c.buildingDoorMask.size()) return 0;
    return c.buildingDoorMask[idx];
}

bool CollidesWithBuilding(float x, float z, float radius) {
    if (g_scale <= 0.0f) return false;

    // Treat point-test as a tiny circle so walls are respected.
    if (radius <= 0.0f) radius = std::max(0.001f, g_scale * 0.02f);

    const float minX = x - radius;
    const float maxX = x + radius;
    const float minZ = z - radius;
    const float maxZ = z + radius;

    const int minCellX = static_cast<int>(std::floor(minX / g_scale));
    const int maxCellX = static_cast<int>(std::floor(maxX / g_scale));
    const int minCellZ = static_cast<int>(std::floor(minZ / g_scale));
    const int maxCellZ = static_cast<int>(std::floor(maxZ / g_scale));

    const float r2 = radius * radius;
    const float wallThickness = std::clamp(g_scale * 0.18f, 0.05f, g_scale);

    auto circleIntersectsAabbXZ = [&](float ax0, float az0, float ax1, float az1) -> bool {
        const float closestX = std::clamp(x, ax0, ax1);
        const float closestZ = std::clamp(z, az0, az1);
        const float dx = x - closestX;
        const float dz = z - closestZ;
        return (dx * dx + dz * dz) <= r2;
    };

    // Scan nearby building cells and test only their boundary wall edges (doors are skipped).
    // Expand by one cell to catch edges near the circle boundary.
    for (int cellZ = minCellZ - 1; cellZ <= maxCellZ + 1; ++cellZ) {
        for (int cellX = minCellX - 1; cellX <= maxCellX + 1; ++cellX) {
            if (!isBuildingCellGlobal(cellX, cellZ)) continue;

            const uint8_t doorBits = doorMaskCellGlobal(cellX, cellZ);

            const float x0 = static_cast<float>(cellX) * g_scale;
            const float z0 = static_cast<float>(cellZ) * g_scale;
            const float x1 = x0 + g_scale;
            const float z1 = z0 + g_scale;

            // North wall (edge at z0)
            if (!isBuildingCellGlobal(cellX, cellZ - 1)) {
                if ((doorBits & BuildingsMesh::DoorN) == 0) {
                    if (circleIntersectsAabbXZ(x0, z0 - wallThickness, x1, z0 + wallThickness)) return true;
                }
            }
            // South wall (edge at z1)
            if (!isBuildingCellGlobal(cellX, cellZ + 1)) {
                if ((doorBits & BuildingsMesh::DoorS) == 0) {
                    if (circleIntersectsAabbXZ(x0, z1 - wallThickness, x1, z1 + wallThickness)) return true;
                }
            }
            // West wall (edge at x0)
            if (!isBuildingCellGlobal(cellX - 1, cellZ)) {
                if ((doorBits & BuildingsMesh::DoorW) == 0) {
                    if (circleIntersectsAabbXZ(x0 - wallThickness, z0, x0 + wallThickness, z1)) return true;
                }
            }
            // East wall (edge at x1)
            if (!isBuildingCellGlobal(cellX + 1, cellZ)) {
                if ((doorBits & BuildingsMesh::DoorE) == 0) {
                    if (circleIntersectsAabbXZ(x1 - wallThickness, z0, x1 + wallThickness, z1)) return true;
                }
            }
        }
    }

    return false;
}
