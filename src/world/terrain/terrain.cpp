#include "terrain.h"
#include <core/config.h>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>
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
    int indexCount = 0;
    int highwayIndexCount = 0;
    int roadIndexCount = 0;
    int streetIndexCount = 0;
    int buildingIndexCount = 0;
    // store generated polyline data (vector of polylines)
    std::vector<std::vector<glm::vec2>> highwayPolylines;
    std::vector<std::vector<glm::vec2>> roadPolylines;
    std::vector<std::vector<glm::vec2>> streetPolylines;
    // Grid data for road types (0=terrain, 1=highway, 2=road, 3=street, 4=building)
    std::vector<std::vector<int>> roadGrid;
    // Building data
    std::vector<BuildingShape> buildings;
    std::vector<PolygonMesh> buildingPolygonMeshes;
    // Terrain vertices for filtering
    std::vector<float> terrainVertices;
    std::vector<unsigned int> terrainIndices;
};

static std::unordered_map<long long, Chunk> g_chunks;
static noise::module::Perlin g_perlin;
static int g_chunkSize = Config::World::CHUNK_SIZE; // vertices per side
static float g_scale = Config::World::VERTEX_SPACING; // spacing between vertices (larger to make terrain more planar)
static int g_viewRadius = Config::World::VIEW_RADIUS; // generate chunks within this radius
// terrain height scaling to reduce steepness
static float g_heightAmplitude = Config::Terrain::HEIGHT_AMPLITUDE; // lower amplitude
static float g_heightFrequency = Config::Terrain::HEIGHT_FREQUENCY; // lower frequency for gentler slopes

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
                            int& indexCount) {
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    indexCount = static_cast<int>(indices.size());
}

static void StartChunkWorker();
static void StopChunkWorker();
static void RequestChunkAsync(int cx, int cz);
static void ProcessReadyChunks(int currentCx, int currentCz);

long long keyFor(int cx, int cz) {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}

bool InitTerrain() {
    g_perlin.SetSeed(Config::World::PERLIN_SEED);
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
    int padding = Config::Highway::PADDING;
    int seed = Config::Highway::SEED;

    // Stage 1: Generate highways (dummy for now)
    c.roadGrid = generate_highways_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 
        Config::Highway::NUM_HIGHWAYS, Config::Highway::WORM_LENGTH, Config::Highway::STEP_SIZE, 
        Config::Highway::PERLIN_SCALE, seed, Config::Highway::GRID_ANGLES, Config::Highway::NOISE_STRENGTH);
    
    // Stage 2: Generate roads
    c.roadGrid = generate_roads_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 
        Config::Road::NUM_ROADS, Config::Road::WORM_LENGTH, Config::Road::STEP_SIZE, 
        Config::Road::PERLIN_SCALE, seed, Config::Road::GRID_ANGLES, Config::Road::NOISE_STRENGTH);
    
    // Stage 3: Generate streets
    c.roadGrid = generate_streets_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 
        Config::Street::NUM_STREETS, Config::Street::WORM_LENGTH, Config::Street::STEP_SIZE, 
        Config::Street::PERLIN_SCALE, seed, Config::Street::GRID_ANGLES, Config::Street::NOISE_STRENGTH);
    
    // Stage 4: Generate buildings
    c.buildings = generate_buildings_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding,
        Config::Building::DENSITY, Config::Building::SEED);
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

static bool pointInPolygon2D(const glm::vec2& point, const std::vector<glm::vec2>& polygon) {
    int n = static_cast<int>(polygon.size());
    if (n < 3) return false;
    bool inside = false;
    float x = point.x;
    float y = point.y;
    float p1x = polygon[0].x;
    float p1y = polygon[0].y;
    for (int i = 1; i <= n; ++i) {
        float p2x = polygon[i % n].x;
        float p2y = polygon[i % n].y;
        if (y > std::min(p1y, p2y)) {
            if (y <= std::max(p1y, p2y)) {
                if (x <= std::max(p1x, p2x)) {
                    float xinters;
                    if (p1y != p2y) {
                        xinters = (y - p1y) * (p2x - p1x) / (p2y - p1y) + p1x;
                    } else {
                        xinters = p1x;
                    }
                    if (p1x == p2x || x <= xinters) {
                        inside = !inside;
                    }
                }
            }
        }
        p1x = p2x;
        p1y = p2y;
    }
    return inside;
}

static void createBuildingMeshFromGrid(Chunk& c, GLuint& VAO, GLuint& VBO, GLuint& EBO, int& indexCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Build a per-cell height grid by rasterizing generated building polygons.
    // roadGrid marks BUILDING cells, but does not store heights; BuildingShape does.
    std::vector<std::vector<float>> heightGrid(g_chunkSize, std::vector<float>(g_chunkSize, 0.0f));
    for (const auto& building : c.buildings) {
        if (building.points.size() < 3) continue;

        float minX = building.points[0].x;
        float maxX = building.points[0].x;
        float minY = building.points[0].y;
        float maxY = building.points[0].y;
        for (const auto& p : building.points) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        int x1 = std::min(g_chunkSize - 1, static_cast<int>(std::ceil(maxX)));
        int z0 = std::max(0, static_cast<int>(std::floor(minY)));
        int z1 = std::min(g_chunkSize - 1, static_cast<int>(std::ceil(maxY)));

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (c.roadGrid[z][x] != BUILDING) continue;
                // Test cell center in grid coordinates.
                glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
                if (pointInPolygon2D(p, building.points)) {
                    heightGrid[z][x] = std::max(heightGrid[z][x], building.height);
                }
            }
        }
    }

    auto pushVertex = [&](float x, float y, float z) {
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
    };

    auto addQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c_, const glm::vec3& d) {
        unsigned int baseIdx = static_cast<unsigned int>(vertices.size() / 3);
        pushVertex(a.x, a.y, a.z);
        pushVertex(b.x, b.y, b.z);
        pushVertex(c_.x, c_.y, c_.z);
        pushVertex(d.x, d.y, d.z);
        // Two triangles: (a,c,b) and (b,c,d) to match the existing terrain winding.
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
    };

    auto heightAt = [&](int gx, int gz) -> float {
        if (gx < 0 || gz < 0 || gx >= g_chunkSize || gz >= g_chunkSize) return 0.0f;
        if (c.roadGrid[gz][gx] != BUILDING) return 0.0f;
        float hh = heightGrid[gz][gx];
        if (hh <= 0.0f) {
            // Keep consistent with the main loop fallback.
            hh = Config::Building::ROAD_MIN_HEIGHT;
        }
        return hh;
    };

    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            if (c.roadGrid[z][x] != BUILDING) continue;

            float h = heightAt(x, z);

            int i00 = (z + 0) * g_chunkSize + (x + 0);
            int i10 = (z + 0) * g_chunkSize + (x + 1);
            int i01 = (z + 1) * g_chunkSize + (x + 0);
            int i11 = (z + 1) * g_chunkSize + (x + 1);

            glm::vec3 v00(c.terrainVertices[i00 * 3], c.terrainVertices[i00 * 3 + 1], c.terrainVertices[i00 * 3 + 2]);
            glm::vec3 v10(c.terrainVertices[i10 * 3], c.terrainVertices[i10 * 3 + 1], c.terrainVertices[i10 * 3 + 2]);
            glm::vec3 v01(c.terrainVertices[i01 * 3], c.terrainVertices[i01 * 3 + 1], c.terrainVertices[i01 * 3 + 2]);
            glm::vec3 v11(c.terrainVertices[i11 * 3], c.terrainVertices[i11 * 3 + 1], c.terrainVertices[i11 * 3 + 2]);

            // Base (ground) face.
            addQuad(v00, v10, v01, v11);

            // Top face.
            glm::vec3 t00(v00.x, v00.y + h, v00.z);
            glm::vec3 t10(v10.x, v10.y + h, v10.z);
            glm::vec3 t01(v01.x, v01.y + h, v01.z);
            glm::vec3 t11(v11.x, v11.y + h, v11.z);
            addQuad(t00, t10, t01, t11);

            // Side faces:
            // - Always emit along the outer boundary (neighbor not BUILDING).
            // - Also emit vertical walls where adjacent BUILDING cells have a lower height.
            //   This prevents holes when two buildings touch but have different heights.
            const float eps = 1e-4f;

            // North edge (neighbor z-1)
            {
                float hn = heightAt(x, z - 1);
                if (h > hn + eps) {
                    glm::vec3 b00(v00.x, v00.y + hn, v00.z);
                    glm::vec3 b10(v10.x, v10.y + hn, v10.z);
                    addQuad(b00, b10, t00, t10);
                }
            }
            // South edge (neighbor z+1)
            {
                float hs = heightAt(x, z + 1);
                if (h > hs + eps) {
                    glm::vec3 b01(v01.x, v01.y + hs, v01.z);
                    glm::vec3 b11(v11.x, v11.y + hs, v11.z);
                    addQuad(b01, b11, t01, t11);
                }
            }
            // West edge (neighbor x-1)
            {
                float hw = heightAt(x - 1, z);
                if (h > hw + eps) {
                    glm::vec3 b00(v00.x, v00.y + hw, v00.z);
                    glm::vec3 b01(v01.x, v01.y + hw, v01.z);
                    addQuad(b00, b01, t00, t01);
                }
            }
            // East edge (neighbor x+1)
            {
                float he = heightAt(x + 1, z);
                if (h > he + eps) {
                    glm::vec3 b10(v10.x, v10.y + he, v10.z);
                    glm::vec3 b11(v11.x, v11.y + he, v11.z);
                    addQuad(b10, b11, t10, t11);
                }
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
        indexCount = static_cast<int>(indices.size());
    } else {
        indexCount = 0;
    }
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
    
    // Create 2D polygon meshes for building blueprints
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

// Public API: generate terrain chunk with complete pipeline
bool GenerateTerrainChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) != g_chunks.end()) return true; // already present
    Chunk c;
    c.cx = cx; c.cz = cz;
    createChunkMesh(c);
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
    const float intersectionRadius = Config::Road::INTERSECTION_RADIUS;

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
    int padding = Config::Highway::PADDING;
    int seed = Config::Highway::SEED;

    out.roadGrid = generate_highways_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        Config::Highway::NUM_HIGHWAYS, Config::Highway::WORM_LENGTH, Config::Highway::STEP_SIZE,
        Config::Highway::PERLIN_SCALE, seed, Config::Highway::GRID_ANGLES, Config::Highway::NOISE_STRENGTH);

    out.roadGrid = generate_roads_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        Config::Road::NUM_ROADS, Config::Road::WORM_LENGTH, Config::Road::STEP_SIZE,
        Config::Road::PERLIN_SCALE, seed, Config::Road::GRID_ANGLES, Config::Road::NOISE_STRENGTH);

    out.roadGrid = generate_streets_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        Config::Street::NUM_STREETS, Config::Street::WORM_LENGTH, Config::Street::STEP_SIZE,
        Config::Street::PERLIN_SCALE, seed, Config::Street::GRID_ANGLES, Config::Street::NOISE_STRENGTH);

    out.buildings = generate_buildings_grid(out.roadGrid, out.cx, out.cz, chunk_size, padding,
        Config::Building::DENSITY, Config::Building::SEED);
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

static void BuildBuildingMeshFromGridCpu(const ChunkCpuData& src, std::vector<float>& vertices, std::vector<unsigned int>& indices) {
    vertices.clear();
    indices.clear();

    std::vector<std::vector<float>> heightGrid(g_chunkSize, std::vector<float>(g_chunkSize, 0.0f));
    for (const auto& building : src.buildings) {
        if (building.points.size() < 3) continue;

        float minX = building.points[0].x;
        float maxX = building.points[0].x;
        float minY = building.points[0].y;
        float maxY = building.points[0].y;
        for (const auto& p : building.points) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        int x1 = std::min(g_chunkSize - 1, static_cast<int>(std::ceil(maxX)));
        int z0 = std::max(0, static_cast<int>(std::floor(minY)));
        int z1 = std::min(g_chunkSize - 1, static_cast<int>(std::ceil(maxY)));

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (src.roadGrid[z][x] != BUILDING) continue;
                glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
                if (pointInPolygon2D(p, building.points)) {
                    heightGrid[z][x] = std::max(heightGrid[z][x], building.height);
                }
            }
        }
    }

    auto pushVertex = [&](float x, float y, float z) {
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
    };

    auto addQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c_, const glm::vec3& d) {
        unsigned int baseIdx = static_cast<unsigned int>(vertices.size() / 3);
        pushVertex(a.x, a.y, a.z);
        pushVertex(b.x, b.y, b.z);
        pushVertex(c_.x, c_.y, c_.z);
        pushVertex(d.x, d.y, d.z);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
    };

    auto heightAt = [&](int gx, int gz) -> float {
        if (gx < 0 || gz < 0 || gx >= g_chunkSize || gz >= g_chunkSize) return 0.0f;
        if (src.roadGrid[gz][gx] != BUILDING) return 0.0f;
        float hh = heightGrid[gz][gx];
        if (hh <= 0.0f) hh = Config::Building::ROAD_MIN_HEIGHT;
        return hh;
    };

    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            if (src.roadGrid[z][x] != BUILDING) continue;

            float h = heightAt(x, z);

            int i00 = (z + 0) * g_chunkSize + (x + 0);
            int i10 = (z + 0) * g_chunkSize + (x + 1);
            int i01 = (z + 1) * g_chunkSize + (x + 0);
            int i11 = (z + 1) * g_chunkSize + (x + 1);

            glm::vec3 v00(src.terrainVertices[i00 * 3], src.terrainVertices[i00 * 3 + 1], src.terrainVertices[i00 * 3 + 2]);
            glm::vec3 v10(src.terrainVertices[i10 * 3], src.terrainVertices[i10 * 3 + 1], src.terrainVertices[i10 * 3 + 2]);
            glm::vec3 v01(src.terrainVertices[i01 * 3], src.terrainVertices[i01 * 3 + 1], src.terrainVertices[i01 * 3 + 2]);
            glm::vec3 v11(src.terrainVertices[i11 * 3], src.terrainVertices[i11 * 3 + 1], src.terrainVertices[i11 * 3 + 2]);

            addQuad(v00, v10, v01, v11);

            glm::vec3 t00(v00.x, v00.y + h, v00.z);
            glm::vec3 t10(v10.x, v10.y + h, v10.z);
            glm::vec3 t01(v01.x, v01.y + h, v01.z);
            glm::vec3 t11(v11.x, v11.y + h, v11.z);
            addQuad(t00, t10, t01, t11);

            const float eps = 1e-4f;

            { // North
                float hn = heightAt(x, z - 1);
                if (h > hn + eps) {
                    glm::vec3 b00(v00.x, v00.y + hn, v00.z);
                    glm::vec3 b10(v10.x, v10.y + hn, v10.z);
                    addQuad(b00, b10, t00, t10);
                }
            }
            { // South
                float hs = heightAt(x, z + 1);
                if (h > hs + eps) {
                    glm::vec3 b01(v01.x, v01.y + hs, v01.z);
                    glm::vec3 b11(v11.x, v11.y + hs, v11.z);
                    addQuad(b01, b11, t01, t11);
                }
            }
            { // West
                float hw = heightAt(x - 1, z);
                if (h > hw + eps) {
                    glm::vec3 b00(v00.x, v00.y + hw, v00.z);
                    glm::vec3 b01(v01.x, v01.y + hw, v01.z);
                    addQuad(b00, b01, t00, t01);
                }
            }
            { // East
                float he = heightAt(x + 1, z);
                if (h > he + eps) {
                    glm::vec3 b10(v10.x, v10.y + he, v10.z);
                    glm::vec3 b11(v11.x, v11.y + he, v11.z);
                    addQuad(b10, b11, t10, t11);
                }
            }
        }
    }
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
    BuildBuildingMeshFromGridCpu(out, out.buildingMeshVertices, out.buildingMeshIndices);
    BuildBlueprintInputsCpu(out, perlin);
    return out;
}

static void ChunkWorkerMain() {
    noise::module::Perlin perlinLocal;
    perlinLocal.SetSeed(Config::World::PERLIN_SEED);
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

void RenderTerrain(GLuint terrainProgram, GLuint highwaysProgram, GLuint roadsProgram, GLuint streetsProgram, GLuint buildingsProgram, const glm::mat4& proj, const glm::mat4& view) {
    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = proj * view * model;

        // Render terrain
        if (terrainProgram != 0 && c.VAO != 0 && c.indexCount > 0) {
            glUseProgram(terrainProgram);
            GLint loc = glGetUniformLocation(terrainProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(terrainProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, Config::Terrain::COLOR_R / 255.0f, Config::Terrain::COLOR_G / 255.0f, Config::Terrain::COLOR_B / 255.0f);
            glBindVertexArray(c.VAO);
            glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Render highways
        if (highwaysProgram != 0 && c.highwayVAO != 0 && c.highwayIndexCount > 0) {
            glUseProgram(highwaysProgram);
            GLint loc = glGetUniformLocation(highwaysProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(highwaysProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, Config::Highway::COLOR_R / 255.0f, Config::Highway::COLOR_G / 255.0f, Config::Highway::COLOR_B / 255.0f);
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render roads
        if (roadsProgram != 0 && c.roadVAO != 0 && c.roadIndexCount > 0) {
            glUseProgram(roadsProgram);
            GLint loc = glGetUniformLocation(roadsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(roadsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, Config::Road::COLOR_R / 255.0f, Config::Road::COLOR_G / 255.0f, Config::Road::COLOR_B / 255.0f);
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render streets
        if (streetsProgram != 0 && c.streetVAO != 0 && c.streetIndexCount > 0) {
            glUseProgram(streetsProgram);
            GLint loc = glGetUniformLocation(streetsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(streetsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, Config::Street::COLOR_R / 255.0f, Config::Street::COLOR_G / 255.0f, Config::Street::COLOR_B / 255.0f);
            glBindVertexArray(c.streetVAO);
            glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
        }
        
        // Render building base (2D footprint)
        if (buildingsProgram != 0 && c.buildingVAO != 0 && c.buildingIndexCount > 0) {
            glUseProgram(buildingsProgram);
            GLint loc = glGetUniformLocation(buildingsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(buildingsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, Config::Building::COLOR_R / 255.0f, Config::Building::COLOR_G / 255.0f, Config::Building::COLOR_B / 255.0f);
            glBindVertexArray(c.buildingVAO);
            glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
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

// Render terrain to G-buffer for deferred rendering
void RenderTerrainToGBuffer(GLuint geometryShader, const glm::mat4& proj, const glm::mat4& view) {
    if (geometryShader == 0) return;

    glUseProgram(geometryShader);
    
    GLint modelLoc = glGetUniformLocation(geometryShader, "model");
    GLint viewLoc = glGetUniformLocation(geometryShader, "view");
    GLint projLoc = glGetUniformLocation(geometryShader, "projection");
    GLint colorLoc = glGetUniformLocation(geometryShader, "uColor");
    
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));

    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        // Render terrain
        if (c.VAO != 0 && c.indexCount > 0) {
            glUniform3f(colorLoc, Config::Terrain::COLOR_R / 255.0f, Config::Terrain::COLOR_G / 255.0f, Config::Terrain::COLOR_B / 255.0f);
            glBindVertexArray(c.VAO);
            glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Render highways
        if (c.highwayVAO != 0 && c.highwayIndexCount > 0) {
            glUniform3f(colorLoc, Config::Highway::COLOR_R / 255.0f, Config::Highway::COLOR_G / 255.0f, Config::Highway::COLOR_B / 255.0f);
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render roads
        if (c.roadVAO != 0 && c.roadIndexCount > 0) {
            glUniform3f(colorLoc, Config::Road::COLOR_R / 255.0f, Config::Road::COLOR_G / 255.0f, Config::Road::COLOR_B / 255.0f);
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render streets
        if (c.streetVAO != 0 && c.streetIndexCount > 0) {
            glUniform3f(colorLoc, Config::Street::COLOR_R / 255.0f, Config::Street::COLOR_G / 255.0f, Config::Street::COLOR_B / 255.0f);
            glBindVertexArray(c.streetVAO);
            glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
        }
        
        // Render buildings
        if (c.buildingVAO != 0 && c.buildingIndexCount > 0) {
            glUniform3f(colorLoc, Config::Building::COLOR_R / 255.0f, Config::Building::COLOR_G / 255.0f, Config::Building::COLOR_B / 255.0f);
            glBindVertexArray(c.buildingVAO);
            glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
}

// Render terrain to shadow map
void RenderTerrainToShadowMap(GLuint shadowShader, const glm::mat4& lightSpaceMatrix) {
    if (shadowShader == 0) return;

    glUseProgram(shadowShader);
    
    GLint lightSpaceLoc = glGetUniformLocation(shadowShader, "lightSpaceMatrix");
    GLint modelLoc = glGetUniformLocation(shadowShader, "model");
    
    glUniformMatrix4fv(lightSpaceLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
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
        
        if (c.buildingVAO != 0 && c.buildingIndexCount > 0) {
            glBindVertexArray(c.buildingVAO);
            glDrawElements(GL_TRIANGLES, c.buildingIndexCount, GL_UNSIGNED_INT, 0);
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

bool CollidesWithBuilding(float x, float z, float radius) {
    if (g_scale <= 0.0f) return false;

    // Fast path: point test.
    if (radius <= 0.0f) {
        const int cellX = static_cast<int>(std::floor(x / g_scale));
        const int cellZ = static_cast<int>(std::floor(z / g_scale));
        return isBuildingCellGlobal(cellX, cellZ);
    }

    const float minX = x - radius;
    const float maxX = x + radius;
    const float minZ = z - radius;
    const float maxZ = z + radius;

    const int minCellX = static_cast<int>(std::floor(minX / g_scale));
    const int maxCellX = static_cast<int>(std::floor(maxX / g_scale));
    const int minCellZ = static_cast<int>(std::floor(minZ / g_scale));
    const int maxCellZ = static_cast<int>(std::floor(maxZ / g_scale));

    const float r2 = radius * radius;

    for (int cz = minCellZ; cz <= maxCellZ; ++cz) {
        for (int cx = minCellX; cx <= maxCellX; ++cx) {
            if (!isBuildingCellGlobal(cx, cz)) continue;

            const float cellX0 = static_cast<float>(cx) * g_scale;
            const float cellZ0 = static_cast<float>(cz) * g_scale;
            const float cellX1 = cellX0 + g_scale;
            const float cellZ1 = cellZ0 + g_scale;

            // Closest point on the cell AABB (in XZ) to the circle center.
            const float closestX = std::clamp(x, cellX0, cellX1);
            const float closestZ = std::clamp(z, cellZ0, cellZ1);
            const float dx = x - closestX;
            const float dz = z - closestZ;
            if (dx * dx + dz * dz <= r2) {
                return true;
            }
        }
    }

    return false;
}
