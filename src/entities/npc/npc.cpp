#include "npc.h"

#include <core/config.h>
#include <core/resources.h>
#include <world/terrain/terrain.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <random>

using nlohmann::json;

namespace {
std::string ReadWholeFile(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) return {};
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return contents;
}

glm::vec3 Rgb255To01(const glm::vec3& rgb255) {
    return glm::vec3(rgb255.x / 255.0f, rgb255.y / 255.0f, rgb255.z / 255.0f);
}

uint32_t QuantizeSeedComponent(float v) {
    // Quantize to decimeters so small float diffs don't change seed.
    // Negative values handled by shifting into unsigned domain.
    int32_t q = (int32_t)std::lround(v * 10.0f);
    return (uint32_t)(q ^ (q >> 16));
}
}

NpcSystem::NpcSystem() : meshLoaded(false) {}

NpcSystem::~NpcSystem() {
    Cleanup();
}

bool NpcSystem::Initialize(const glm::vec3& spawnCenter) {
    Cleanup();

    if (!LoadPalette()) {
        std::cerr << "Failed to load NPC palette; using fallback colors" << std::endl;
        palette01.clear();
        palette01.push_back(glm::vec3(1.0f, 0.0f, 1.0f));
        palette01.push_back(glm::vec3(0.0f, 1.0f, 1.0f));
        palette01.push_back(glm::vec3(1.0f, 1.0f, 0.0f));
    }

    mesh = CreateCustomMesh(Resources::Models::NPC_BASE_MODEL);
    meshLoaded = (mesh.triangleCount > 0);
    if (!meshLoaded) {
        std::cerr << "Failed to load NPC model: " << Resources::Models::NPC_BASE_MODEL << std::endl;
        return false;
    }

    GenerateDeterministicSpawns(spawnCenter);
    return true;
}

void NpcSystem::Cleanup() {
    if (meshLoaded) {
        DestroyCustomMesh(mesh);
        meshLoaded = false;
    }
    npcs.clear();
    palette01.clear();
}

bool NpcSystem::LoadPalette() {
    palette01.clear();

    const std::string text = ReadWholeFile(Resources::Entities::NPC_PARAMS);
    if (text.empty()) return false;

    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        std::cerr << "NPC params JSON parse failed: " << e.what() << std::endl;
        return false;
    }

    if (!j.contains("colors") || !j["colors"].is_array()) return false;

    for (const auto& c : j["colors"]) {
        if (!c.is_array() || c.size() != 3) continue;
        glm::vec3 rgb255((float)c[0].get<int>(), (float)c[1].get<int>(), (float)c[2].get<int>());
        palette01.push_back(Rgb255To01(rgb255));
    }

    return palette01.size() >= 1;
}

void NpcSystem::GenerateDeterministicSpawns(const glm::vec3& spawnCenter) {
    npcs.clear();

    constexpr int kNpcCount = 28;
    constexpr float kMinRadius = 10.0f;
    constexpr float kMaxRadius = 55.0f;
    constexpr float kMinSeparation = 2.0f;
    // Base hover height above terrain; a small per-NPC offset is added deterministically.
    constexpr float kHeightOffset = 0.28f;

    const uint32_t seedA = (uint32_t)Config::World::PERLIN_SEED;
    const uint32_t seedB = QuantizeSeedComponent(spawnCenter.x);
    const uint32_t seedC = QuantizeSeedComponent(spawnCenter.z);
    const uint32_t seedD = 0x4E504300u; // 'NPC\0'

    std::seed_seq seq{seedA, seedB, seedC, seedD};
    std::mt19937 rng(seq);

    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDist(0, (int)palette01.size() - 1);

    auto tooClose = [&](const glm::vec3& p) {
        for (const auto& n : npcs) {
            if (glm::distance(glm::vec2(p.x, p.z), glm::vec2(n.position.x, n.position.z)) < kMinSeparation) {
                return true;
            }
        }
        return false;
    };

    int spawned = 0;
    int attempts = 0;
    const int maxAttempts = kNpcCount * 60;

    while (spawned < kNpcCount && attempts < maxAttempts) {
        attempts++;

        const float a = angleDist(rng);
        // Uniform in area
        const float r = glm::mix(kMinRadius, kMaxRadius, std::sqrt(unitDist(rng)));
        const float x = spawnCenter.x + std::cos(a) * r;
        const float z = spawnCenter.z + std::sin(a) * r;

        if (CollidesWithBuilding(x, z, 0.45f)) continue;

        const float hoverJitter = 0.12f * unitDist(rng);
        glm::vec3 p(x, SampleTerrainHeight(x, z) + kHeightOffset + hoverJitter, z);
        if (tooClose(p)) continue;

        const int ci = colorDist(rng);
        NpcInstance inst;
        inst.position = p;
        inst.color01 = palette01[ci];
        npcs.push_back(inst);
        spawned++;
    }

    // If we couldn't place enough, fill remaining without collision checks but keep determinism.
    while (spawned < kNpcCount) {
        const float a = angleDist(rng);
        const float r = glm::mix(kMinRadius, kMaxRadius, std::sqrt(unitDist(rng)));
        const float x = spawnCenter.x + std::cos(a) * r;
        const float z = spawnCenter.z + std::sin(a) * r;
        const float hoverJitter = 0.12f * unitDist(rng);
        glm::vec3 p(x, SampleTerrainHeight(x, z) + kHeightOffset + hoverJitter, z);

        const int ci = colorDist(rng);
        NpcInstance inst;
        inst.position = p;
        inst.color01 = palette01[ci];
        npcs.push_back(inst);
        spawned++;
    }

    std::cout << "NPCs spawned: " << npcs.size() << std::endl;
}

void NpcSystem::RenderInstancesCommon(GLuint shaderProgram, int modelLoc, const glm::mat4& baseModel) const {
    if (!meshLoaded) return;

    for (const auto& npc : npcs) {
        // Important: translate first, then apply baseModel so scaling/rotation don't affect the translation.
        glm::mat4 model(1.0f);
        model = glm::translate(model, npc.position);
        model *= baseModel;
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        const GLint colorLoc = glGetUniformLocation(shaderProgram, "uColor");
        if (colorLoc >= 0) glUniform3fv(colorLoc, 1, glm::value_ptr(npc.color01));

        for (const auto& tri : mesh.triangles) {
            glBindVertexArray(tri.VAO);
            glDrawArrays(GL_TRIANGLES, 0, tri.vertexCount);
        }
    }

    glBindVertexArray(0);
}

void NpcSystem::RenderToGBuffer(GLuint geometryShader, const glm::mat4& proj, const glm::mat4& view) const {
    if (!meshLoaded || geometryShader == 0) return;

    glUseProgram(geometryShader);

    const GLint projLoc = glGetUniformLocation(geometryShader, "projection");
    const GLint viewLoc = glGetUniformLocation(geometryShader, "view");
    const GLint modelLoc = glGetUniformLocation(geometryShader, "model");

    if (projLoc >= 0) glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(proj));
    if (viewLoc >= 0) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));

    // Pill model: make it smaller and rotate so it's vertical.
    glm::mat4 baseModel(1.0f);
    baseModel = glm::rotate(baseModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    baseModel = glm::scale(baseModel, glm::vec3(0.22f));

    RenderInstancesCommon(geometryShader, modelLoc, baseModel);
}

void NpcSystem::RenderToShadowMap(GLuint shadowShader, const glm::mat4& lightSpaceMatrix) const {
    if (!meshLoaded || shadowShader == 0) return;

    glUseProgram(shadowShader);

    const GLint lightLoc = glGetUniformLocation(shadowShader, "lightSpaceMatrix");
    const GLint modelLoc = glGetUniformLocation(shadowShader, "model");

    if (lightLoc >= 0) glUniformMatrix4fv(lightLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    glm::mat4 baseModel(1.0f);
    baseModel = glm::rotate(baseModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    baseModel = glm::scale(baseModel, glm::vec3(0.22f));

    for (const auto& npc : npcs) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, npc.position);
        model *= baseModel;
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        for (const auto& tri : mesh.triangles) {
            glBindVertexArray(tri.VAO);
            glDrawArrays(GL_TRIANGLES, 0, tri.vertexCount);
        }
    }

    glBindVertexArray(0);
}

bool NpcSystem::CollidesXZ(float x, float z, float radius) const {
    const float rr = radius + kNpcCollisionRadius;
    const float rr2 = rr * rr;
    for (const auto& npc : npcs) {
        const float dx = npc.position.x - x;
        const float dz = npc.position.z - z;
        if ((dx * dx + dz * dz) < rr2) return true;
    }
    return false;
}
