#include "terrain.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/glm.hpp>
#include <world/roads/roads.h>
#include <world/highways/highways.h>
#include <world/streets/streets.h>
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
    int indexCount = 0;
    int highwayIndexCount = 0;
    int roadIndexCount = 0;
    int streetIndexCount = 0;
    // store generated polyline data (vector of polylines)
    std::vector<std::vector<glm::vec2>> highwayPolylines;
    std::vector<std::vector<glm::vec2>> roadPolylines;
    std::vector<std::vector<glm::vec2>> streetPolylines;
    // Grid data for road types (0=terrain, 1=highway, 2=road, 3=street)
    std::vector<std::vector<int>> roadGrid;
    // Terrain vertices for filtering
    std::vector<float> terrainVertices;
    std::vector<unsigned int> terrainIndices;
};

static std::unordered_map<long long, Chunk> g_chunks;
static noise::module::Perlin g_perlin;
static int g_chunkSize = 32; // vertices per side
static float g_scale = 1.5f; // spacing between vertices (larger to make terrain more planar)
static int g_viewRadius = 3; // generate chunks within this radius
// terrain height scaling to reduce steepness
static float g_heightAmplitude = 0.6f; // lower amplitude
static float g_heightFrequency = 0.04f; // lower frequency for gentler slopes

long long keyFor(int cx, int cz) {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}

bool InitTerrain() {
    g_perlin.SetSeed(1337);
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
    int padding = 8;
    int seed = 42;

    // Stage 1: Generate highways (dummy for now)
    c.roadGrid = generate_highways_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 2, 1000, 1.0f, 0.01f, seed, 4, 1.0f);
    
    // Stage 2: Generate roads
    c.roadGrid = generate_roads_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 200, 800, 1.0f, 0.01f, seed, 4, 1.0f);
    
    // Stage 3: Generate streets
    c.roadGrid = generate_streets_grid(c.roadGrid, c.cx, c.cz, chunk_size, padding, 100, 400, 1.0f, 0.01f, seed, 4, 1.0f);
}

static void createMeshFromGrid(Chunk& c, int roadType, GLuint& VAO, GLuint& VBO, GLuint& EBO, int& indexCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    
    // Find triangles that belong to this road type
    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            // Check if any corner of this quad has the matching road type
            bool hasRoadType = false;
            if (c.roadGrid[z][x] == roadType || 
                c.roadGrid[z][x+1] == roadType ||
                c.roadGrid[z+1][x] == roadType ||
                c.roadGrid[z+1][x+1] == roadType) {
                hasRoadType = true;
            }
            
            if (hasRoadType) {
                // Add the two triangles for this quad
                int baseIdx = vertices.size() / 3;
                
                // Add 4 vertices for this quad
                for (int dz = 0; dz <= 1; ++dz) {
                    for (int dx = 0; dx <= 1; ++dx) {
                        int vertIdx = (z + dz) * g_chunkSize + (x + dx);
                        vertices.push_back(c.terrainVertices[vertIdx * 3]);     // x
                        vertices.push_back(c.terrainVertices[vertIdx * 3 + 1] + 0.01f * roadType); // y (slight offset)
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
bool DetermineSpawnFromGenerated(float x, float z, glm::vec2 &out_point, int search_radius_chunks) {
    // compute chunk containing point
    int cx = static_cast<int>(std::floor(x / ((g_chunkSize - 1) * g_scale)));
    int cz = static_cast<int>(std::floor(z / ((g_chunkSize - 1) * g_scale)));

    float bestDist2 = std::numeric_limits<float>::infinity();
    glm::vec2 bestPoint(x, z);
    int bestScore = 0;
    const float intersectionRadius = 4.0f;

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

    if (bestScore > 0 || bestDist2 < std::numeric_limits<float>::infinity()) {
        out_point = bestPoint;
        return true;
    }
    return false;
}

// Determine spawn point using existing road search helper
glm::vec2 DetermineSpawnPoint(float x, float z, int search_radius_chunks) {
    return find_nearest_road_point(x, z, search_radius_chunks, g_chunkSize * (int)g_scale);
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

void RenderTerrain(GLuint terrainProgram, GLuint highwaysProgram, GLuint roadsProgram, GLuint streetsProgram, const glm::mat4& proj, const glm::mat4& view) {
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
            glUniform3f(colorLoc, 0.3f, 0.8f, 0.3f); // Green terrain
            glBindVertexArray(c.VAO);
            glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Render highways
        if (highwaysProgram != 0 && c.highwayVAO != 0 && c.highwayIndexCount > 0) {
            glUseProgram(highwaysProgram);
            GLint loc = glGetUniformLocation(highwaysProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(highwaysProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, 0.6f, 0.6f, 0.6f); // Gray for highways (matching notebook)
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render roads
        if (roadsProgram != 0 && c.roadVAO != 0 && c.roadIndexCount > 0) {
            glUseProgram(roadsProgram);
            GLint loc = glGetUniformLocation(roadsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(roadsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, 0.8f, 0.8f, 0.6f); // Light beige for roads (matching notebook)
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render streets
        if (streetsProgram != 0 && c.streetVAO != 0 && c.streetIndexCount > 0) {
            glUseProgram(streetsProgram);
            GLint loc = glGetUniformLocation(streetsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(streetsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, 0.4f, 0.4f, 0.4f); // Dark gray for streets (matching notebook)
            glBindVertexArray(c.streetVAO);
            glDrawElements(GL_TRIANGLES, c.streetIndexCount, GL_UNSIGNED_INT, 0);
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
