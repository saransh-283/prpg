#include "terrain.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/glm.hpp>
#include "../roads/roads.h"

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
