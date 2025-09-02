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

static void createChunkMesh(Chunk& c) {
    // generate grid of g_chunkSize x g_chunkSize vertices
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    vertices.reserve(g_chunkSize * g_chunkSize * 3);
    indices.reserve((g_chunkSize - 1) * (g_chunkSize - 1) * 6);

    for (int z = 0; z < g_chunkSize; ++z) {
        for (int x = 0; x < g_chunkSize; ++x) {
            float wx = (c.cx * (g_chunkSize - 1) + x) * g_scale;
            float wz = (c.cz * (g_chunkSize - 1) + z) * g_scale;
            float h = sampleHeight(wx, wz);
            vertices.push_back(wx);
            vertices.push_back(h);
            vertices.push_back(wz);
        }
    }

    for (int z = 0; z < g_chunkSize - 1; ++z) {
        for (int x = 0; x < g_chunkSize - 1; ++x) {
            int i0 = z * g_chunkSize + x;
            int i1 = i0 + 1;
            int i2 = i0 + g_chunkSize;
            int i3 = i2 + 1;
            // two triangles
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    // create GL buffers
    glGenVertexArrays(1, &c.VAO);
    glGenBuffers(1, &c.VBO);
    glGenBuffers(1, &c.EBO);
    glBindVertexArray(c.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, c.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    // position attrib
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    c.indexCount = (int)indices.size();

    // generate roads for this chunk using roads module (polylines in world XZ)
    int padding = 8; // keep small padding for roads
    auto polylines = generate_roads_chunk_polylines(c.cx, c.cz, g_chunkSize *  (int)g_scale, padding, 64, 200, 1.0f, 0.02f, 1337, 4, 1.0f);
    // build simple ribbon mesh for roads: two vertices per poly point offset along tangent
    std::vector<float> roadVerts;
    std::vector<unsigned int> roadIdx;
    int baseVert = 0;
    float halfWidth = 0.5f; // world units
    for (auto &poly : polylines) {
        if (poly.size() < 2) continue;
        // build vertices
        for (size_t i = 0; i < poly.size(); ++i) {
            glm::vec2 p = poly[i];
            // compute tangent
            glm::vec2 t;
            if (i + 1 < poly.size()) t = glm::normalize(poly[i+1] - p);
            else t = glm::normalize(p - poly[i-1]);
            glm::vec2 n = glm::vec2(-t.y, t.x);
            glm::vec2 left = p + n * halfWidth;
            glm::vec2 right = p - n * halfWidth;
            float hy = sampleHeight(left.x, left.y);
            float ry = sampleHeight(right.x, right.y);
            // left vertex
            roadVerts.push_back(left.x);
            roadVerts.push_back(hy + 0.01f); // slight offset to avoid z-fighting
            roadVerts.push_back(left.y);
            // right vertex
            roadVerts.push_back(right.x);
            roadVerts.push_back(ry + 0.01f);
            roadVerts.push_back(right.y);
        }
        // indices
        int pts = (int)poly.size();
        for (int i = 0; i < pts - 1; ++i) {
            int a = baseVert + i * 2;
            int b = baseVert + i * 2 + 1;
            int c2 = baseVert + (i+1) * 2;
            int d = baseVert + (i+1) * 2 + 1;
            // two tris (a,c,b) and (b,c,d) but ensure winding
            roadIdx.push_back(a);
            roadIdx.push_back(c2);
            roadIdx.push_back(b);
            roadIdx.push_back(b);
            roadIdx.push_back(c2);
            roadIdx.push_back(d);
        }
        baseVert += pts * 2;
    }

    if (!roadVerts.empty()) {
        glGenVertexArrays(1, &c.roadVAO);
        glGenBuffers(1, &c.roadVBO);
        glGenBuffers(1, &c.roadEBO);
        glBindVertexArray(c.roadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, c.roadVBO);
        glBufferData(GL_ARRAY_BUFFER, roadVerts.size() * sizeof(float), roadVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.roadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, roadIdx.size() * sizeof(unsigned int), roadIdx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
        c.roadIndexCount = (int)roadIdx.size();
    }
}

// Public API: generate terrain chunk (terrain mesh only)
bool GenerateTerrainChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) != g_chunks.end()) return true; // already present
    Chunk c;
    c.cx = cx; c.cz = cz;
    createChunkMesh(c);
    // store without road generation (roads are created separately via GenerateRoadsForChunk)
    g_chunks[k] = c;
    return true;
}

bool GenerateRoadsForChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) == g_chunks.end()) {
        Chunk c;
        c.cx = cx; c.cz = cz;
        createChunkMesh(c);
        g_chunks[k] = c;
    }
    Chunk &c = g_chunks[k];

    // Generate hierarchical road system: highways and roads first
    int padding = 8;
    int chunk_size = g_chunkSize * (int)g_scale;

    // Generate highways first
    auto highways = generate_highways_chunk_polylines(cx, cz, chunk_size, padding,
                                                       2, 1000, 1.0f, 0.01f, 42, 4, 1.0f);

    // Generate roads
    auto roads = generate_roads_chunk_polylines(cx, cz, chunk_size, padding,
                                                 200, 800, 1.0f, 0.01f, 42, 4, 1.0f);

    // Combine highways and roads for storage and initial rendering
    std::vector<std::vector<glm::vec2>> highways_and_roads;
    highways_and_roads.reserve(highways.size() + roads.size());
    highways_and_roads.insert(highways_and_roads.end(), highways.begin(), highways.end());
    highways_and_roads.insert(highways_and_roads.end(), roads.begin(), roads.end());

    // Store polylines separately for different road types
    c.highwayPolylines = std::move(highways);
    c.roadPolylines = std::move(roads);

    // Build highway geometry for rendering
    std::vector<float> highwayVerts;
    std::vector<unsigned int> highwayIdx;
    int highwayBaseVert = 0;
    float halfWidth = 0.5f;

    for (auto &poly : c.highwayPolylines) {
        if (poly.size() < 2) continue;
        for (size_t i = 0; i < poly.size(); ++i) {
            glm::vec2 p = poly[i];
            glm::vec2 t;
            if (i + 1 < poly.size()) t = glm::normalize(poly[i+1] - p);
            else t = glm::normalize(p - poly[i-1]);
            glm::vec2 n = glm::vec2(-t.y, t.x);
            glm::vec2 left = p + n * halfWidth;
            glm::vec2 right = p - n * halfWidth;
            float hy = sampleHeight(left.x, left.y);
            float ry = sampleHeight(right.x, right.y);
            highwayVerts.push_back(left.x);
            highwayVerts.push_back(hy + 0.01f);
            highwayVerts.push_back(left.y);
            highwayVerts.push_back(right.x);
            highwayVerts.push_back(ry + 0.01f);
            highwayVerts.push_back(right.y);
        }
        int pts = (int)poly.size();
        for (int i = 0; i < pts - 1; ++i) {
            int a = highwayBaseVert + i * 2;
            int b = highwayBaseVert + i * 2 + 1;
            int c2 = highwayBaseVert + (i+1) * 2;
            int d = highwayBaseVert + (i+1) * 2 + 1;
            highwayIdx.push_back(a);
            highwayIdx.push_back(c2);
            highwayIdx.push_back(b);
            highwayIdx.push_back(b);
            highwayIdx.push_back(c2);
            highwayIdx.push_back(d);
        }
        highwayBaseVert += pts * 2;
    }

    if (!highwayVerts.empty()) {
        if (c.highwayVAO) {
            glDeleteVertexArrays(1, &c.highwayVAO);
            glDeleteBuffers(1, &c.highwayVBO);
            glDeleteBuffers(1, &c.highwayEBO);
        }
        glGenVertexArrays(1, &c.highwayVAO);
        glGenBuffers(1, &c.highwayVBO);
        glGenBuffers(1, &c.highwayEBO);
        glBindVertexArray(c.highwayVAO);
        glBindBuffer(GL_ARRAY_BUFFER, c.highwayVBO);
        glBufferData(GL_ARRAY_BUFFER, highwayVerts.size() * sizeof(float), highwayVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.highwayEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, highwayIdx.size() * sizeof(unsigned int), highwayIdx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        c.highwayIndexCount = highwayIdx.size();
    } else {
        c.highwayIndexCount = 0;
    }

    // Build road geometry for rendering
    std::vector<float> roadVerts;
    std::vector<unsigned int> roadIdx;
    int roadBaseVert = 0;

    for (auto &poly : c.roadPolylines) {
        if (poly.size() < 2) continue;
        for (size_t i = 0; i < poly.size(); ++i) {
            glm::vec2 p = poly[i];
            glm::vec2 t;
            if (i + 1 < poly.size()) t = glm::normalize(poly[i+1] - p);
            else t = glm::normalize(p - poly[i-1]);
            glm::vec2 n = glm::vec2(-t.y, t.x);
            glm::vec2 left = p + n * halfWidth;
            glm::vec2 right = p - n * halfWidth;
            float hy = sampleHeight(left.x, left.y);
            float ry = sampleHeight(right.x, right.y);
            roadVerts.push_back(left.x);
            roadVerts.push_back(hy + 0.01f);
            roadVerts.push_back(left.y);
            roadVerts.push_back(right.x);
            roadVerts.push_back(ry + 0.01f);
            roadVerts.push_back(right.y);
        }
        int pts = (int)poly.size();
        for (int i = 0; i < pts - 1; ++i) {
            int a = roadBaseVert + i * 2;
            int b = roadBaseVert + i * 2 + 1;
            int c2 = roadBaseVert + (i+1) * 2;
            int d = roadBaseVert + (i+1) * 2 + 1;
            roadIdx.push_back(a);
            roadIdx.push_back(c2);
            roadIdx.push_back(b);
            roadIdx.push_back(b);
            roadIdx.push_back(c2);
            roadIdx.push_back(d);
        }
        roadBaseVert += pts * 2;
    }

    if (!roadVerts.empty()) {
        if (c.roadVAO) {
            glDeleteVertexArrays(1, &c.roadVAO);
            glDeleteBuffers(1, &c.roadVBO);
            glDeleteBuffers(1, &c.roadEBO);
        }
        glGenVertexArrays(1, &c.roadVAO);
        glGenBuffers(1, &c.roadVBO);
        glGenBuffers(1, &c.roadEBO);
        glBindVertexArray(c.roadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, c.roadVBO);
        glBufferData(GL_ARRAY_BUFFER, roadVerts.size() * sizeof(float), roadVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.roadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, roadIdx.size() * sizeof(unsigned int), roadIdx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        c.roadIndexCount = roadIdx.size();
    } else {
        c.roadIndexCount = 0;
    }

    return true;
}

void GenerateStreetsForChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) == g_chunks.end()) {
        // If chunk doesn't exist, generate roads first
        GenerateRoadsForChunk(cx, cz);
    }
    Chunk &c = g_chunks[k];

    int padding = 8;
    int chunk_size = g_chunkSize * (int)g_scale;

    // Generate streets that branch from highways and roads
    auto streets = generate_streets_chunk_polylines(cx, cz, chunk_size, padding,
                                                     300, 400, 1.0f, 0.01f, 42, 4, 1.0f,
                                                     c.highwayPolylines, c.roadPolylines);

    // Store streets polylines
    c.streetPolylines = std::move(streets);

    // Build street geometry for rendering
    std::vector<float> streetVerts;
    std::vector<unsigned int> streetIdx;
    int streetBaseVert = 0;
    float halfWidth = 0.3f; // Streets are narrower than highways/roads

    for (auto &poly : c.streetPolylines) {
        if (poly.size() < 2) continue;
        for (size_t i = 0; i < poly.size(); ++i) {
            glm::vec2 p = poly[i];
            glm::vec2 t;
            if (i + 1 < poly.size()) t = glm::normalize(poly[i+1] - p);
            else t = glm::normalize(p - poly[i-1]);
            glm::vec2 n = glm::vec2(-t.y, t.x);
            glm::vec2 left = p + n * halfWidth;
            glm::vec2 right = p - n * halfWidth;
            float hy = sampleHeight(left.x, left.y);
            float ry = sampleHeight(right.x, right.y);
            streetVerts.push_back(left.x);
            streetVerts.push_back(hy + 0.01f);
            streetVerts.push_back(left.y);
            streetVerts.push_back(right.x);
            streetVerts.push_back(ry + 0.01f);
            streetVerts.push_back(right.y);
        }
        int pts = (int)poly.size();
        for (int i = 0; i < pts - 1; ++i) {
            int a = streetBaseVert + i * 2;
            int b = streetBaseVert + i * 2 + 1;
            int c2 = streetBaseVert + (i+1) * 2;
            int d = streetBaseVert + (i+1) * 2 + 1;
            streetIdx.push_back(a);
            streetIdx.push_back(c2);
            streetIdx.push_back(b);
            streetIdx.push_back(b);
            streetIdx.push_back(c2);
            streetIdx.push_back(d);
        }
        streetBaseVert += pts * 2;
    }

    if (!streetVerts.empty()) {
        if (c.streetVAO) {
            glDeleteVertexArrays(1, &c.streetVAO);
            glDeleteBuffers(1, &c.streetVBO);
            glDeleteBuffers(1, &c.streetEBO);
        }
        glGenVertexArrays(1, &c.streetVAO);
        glGenBuffers(1, &c.streetVBO);
        glGenBuffers(1, &c.streetEBO);
        glBindVertexArray(c.streetVAO);
        glBindBuffer(GL_ARRAY_BUFFER, c.streetVBO);
        glBufferData(GL_ARRAY_BUFFER, streetVerts.size() * sizeof(float), streetVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.streetEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, streetIdx.size() * sizeof(unsigned int), streetIdx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        c.streetIndexCount = streetIdx.size();
    } else {
        c.streetIndexCount = 0;
    }
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
            glUniform3f(colorLoc, 0.0f, 0.0f, 0.0f); // Black highways
            glBindVertexArray(c.highwayVAO);
            glDrawElements(GL_TRIANGLES, c.highwayIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render roads
        if (roadsProgram != 0 && c.roadVAO != 0 && c.roadIndexCount > 0) {
            glUseProgram(roadsProgram);
            GLint loc = glGetUniformLocation(roadsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(roadsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, 0.2f, 0.2f, 0.2f); // Dark gray roads
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
        }

        // Render streets
        if (streetsProgram != 0 && c.streetVAO != 0 && c.streetIndexCount > 0) {
            glUseProgram(streetsProgram);
            GLint loc = glGetUniformLocation(streetsProgram, "uMVP");
            GLint colorLoc = glGetUniformLocation(streetsProgram, "uColor");
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(colorLoc, 0.4f, 0.4f, 0.4f); // Light gray streets
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
