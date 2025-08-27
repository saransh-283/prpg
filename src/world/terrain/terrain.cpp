#include "terrain.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/glm.hpp>
#include <world/roads/roads.h>

struct Chunk {
    int cx, cz; // chunk coordinates
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    // road mesh
    GLuint roadVAO = 0;
    GLuint roadVBO = 0;
    GLuint roadEBO = 0;
    int indexCount = 0;
    int roadIndexCount = 0;
    // store generated polyline data (vector of polylines)
    std::vector<std::vector<glm::vec2>> roadPolylines;
};

static std::unordered_map<long long, Chunk> g_chunks;
static noise::module::Perlin g_perlin;
static int g_chunkSize = 32; // vertices per side
static float g_scale = 1.5f; // spacing between vertices (larger to make terrain more planar)
static int g_viewRadius = 3; // generate chunks within this radius
// terrain height scaling to reduce steepness
static float g_heightAmplitude = 0.6f; // lower amplitude
static float g_heightFrequency = 0.04f; // lower frequency for gentler slopes

static long long keyFor(int cx, int cz) {
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
    auto polylines = generate_perlin_roads_chunk_polylines(c.cx, c.cz, g_chunkSize *  (int)g_scale, padding, 64, 200, 1.0f, 0.02f, 1337, 4, 1.0f);
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

// Build road geometry for a single chunk and attach to chunk structure. If the chunk
// isn't present it will be created first.
bool GenerateRoadsForChunk(int cx, int cz) {
    long long k = keyFor(cx, cz);
    if (g_chunks.find(k) == g_chunks.end()) {
        Chunk c;
        c.cx = cx; c.cz = cz;
        createChunkMesh(c);
        g_chunks[k] = c;
    }
    Chunk &c = g_chunks[k];
    // reuse earlier road building logic from createChunkMesh but only roads
    int padding = 8;
    auto polylines = generate_perlin_roads_chunk_polylines(c.cx, c.cz, g_chunkSize *  (int)g_scale, padding, 64, 200, 1.0f, 0.02f, 1337, 4, 1.0f);
    // store polylines so later searches can use generated data without regenerating
    c.roadPolylines = polylines;
    std::vector<float> roadVerts;
    std::vector<unsigned int> roadIdx;
    int baseVert = 0;
    float halfWidth = 0.5f;
    for (auto &poly : polylines) {
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
            int a = baseVert + i * 2;
            int b = baseVert + i * 2 + 1;
            int c2 = baseVert + (i+1) * 2;
            int d = baseVert + (i+1) * 2 + 1;
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
        glBindVertexArray(0);
        c.roadIndexCount = (int)roadIdx.size();
    }

    return true;
}

// Search only among already-generated chunks' stored polylines
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
    if (c.roadVAO) glDeleteVertexArrays(1, &c.roadVAO);
    if (c.roadVBO) glDeleteBuffers(1, &c.roadVBO);
    if (c.roadEBO) glDeleteBuffers(1, &c.roadEBO);
    c.roadVAO = c.roadVBO = c.roadEBO = 0;
    c.roadIndexCount = 0;
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

void RenderTerrain(GLuint program, const glm::mat4& proj, const glm::mat4& view) {
    if (program == 0) return;
    glUseProgram(program);
    GLint loc = glGetUniformLocation(program, "uMVP");
    GLint colorLoc = glGetUniformLocation(program, "uColor");

    for (auto& kv : g_chunks) {
        Chunk& c = kv.second;
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = proj * view * model;
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3f(colorLoc, 0.3f, 0.8f, 0.3f);
        glBindVertexArray(c.VAO);
        glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_INT, 0);
        // draw roads if available (use same program and color differently)
        if (c.roadVAO && c.roadIndexCount > 0) {
            glUniform3f(colorLoc, 0.1f, 0.1f, 0.1f);
            glBindVertexArray(c.roadVAO);
            glDrawElements(GL_TRIANGLES, c.roadIndexCount, GL_UNSIGNED_INT, 0);
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
