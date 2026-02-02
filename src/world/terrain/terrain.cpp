#include "terrain.h"
#include "../../config.h"
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

long long keyFor(int cx, int cz) {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}

bool InitTerrain() {
    g_perlin.SetSeed(Config::World::PERLIN_SEED);
    g_perlin.SetFrequency(g_heightFrequency);
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
    createMeshFromGrid(c, BUILDING, c.buildingVAO, c.buildingVBO, c.buildingEBO, c.buildingIndexCount);
    
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

void UpdateTerrain(const glm::vec3& cameraPos) {
    int cx = static_cast<int>(std::floor(cameraPos.x / ((g_chunkSize - 1) * g_scale)));
    int cz = static_cast<int>(std::floor(cameraPos.z / ((g_chunkSize - 1) * g_scale)));

    // ensure chunks in view radius exist
    for (int dz = -g_viewRadius; dz <= g_viewRadius; ++dz) {
        for (int dx = -g_viewRadius; dx <= g_viewRadius; ++dx) {
            int nx = cx + dx;
            int nz = cz + dz;
            long long k = keyFor(nx, nz);
            if (g_chunks.find(k) == g_chunks.end()) {
                Chunk c;
                c.cx = nx; c.cz = nz;
                createChunkMesh(c);
                g_chunks[k] = c;
            }
        }
    }

    // remove distant chunks
    std::vector<long long> toRemove;
    for (auto& kv : g_chunks) {
        int dx = (int)(kv.second.cx - cx);
        int dz = (int)(kv.second.cz - cz);
        if (std::abs(dx) > g_viewRadius + 1 || std::abs(dz) > g_viewRadius + 1) {
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
    for (auto& kv : g_chunks) {
        destroyChunk(kv.second);
    }
    g_chunks.clear();
}

void WorldToChunk(float x, float z, int &out_cx, int &out_cz) {
    out_cx = static_cast<int>(std::floor(x / ((g_chunkSize - 1) * g_scale)));
    out_cz = static_cast<int>(std::floor(z / ((g_chunkSize - 1) * g_scale)));
}
