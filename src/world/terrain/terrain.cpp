#include "terrain.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <noise/noise.h>
#include <glm/gtc/type_ptr.hpp>

struct Chunk {
    int cx, cz; // chunk coordinates
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    int indexCount = 0;
};

static std::unordered_map<long long, Chunk> g_chunks;
static noise::module::Perlin g_perlin;
static int g_chunkSize = 32; // vertices per side
static float g_scale = 1.0f; // spacing between vertices
static int g_viewRadius = 3; // generate chunks within this radius

static long long keyFor(int cx, int cz) {
    return (static_cast<long long>(cx) << 32) ^ static_cast<unsigned int>(cz);
}

bool InitTerrain() {
    g_perlin.SetSeed(1337);
    g_perlin.SetFrequency(1.0);
    return true;
}

static float sampleHeight(float x, float z) {
    double v = g_perlin.GetValue(x * 0.08, z * 0.08, 0.0);
    return static_cast<float>(v * 2.0); // scale height
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
}

static void destroyChunk(Chunk& c) {
    if (c.VAO) glDeleteVertexArrays(1, &c.VAO);
    if (c.VBO) glDeleteBuffers(1, &c.VBO);
    if (c.EBO) glDeleteBuffers(1, &c.EBO);
    c.VAO = c.VBO = c.EBO = 0;
    c.indexCount = 0;
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
