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

#include <algorithm>
#include <limits>

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

float ComputeTransformedMinY(const glm::mat4& localTransform, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    // Evaluate all 8 corners of the AABB.
    const glm::vec3 corners[8] = {
        {aabbMin.x, aabbMin.y, aabbMin.z},
        {aabbMax.x, aabbMin.y, aabbMin.z},
        {aabbMin.x, aabbMax.y, aabbMin.z},
        {aabbMax.x, aabbMax.y, aabbMin.z},
        {aabbMin.x, aabbMin.y, aabbMax.z},
        {aabbMax.x, aabbMin.y, aabbMax.z},
        {aabbMin.x, aabbMax.y, aabbMax.z},
        {aabbMax.x, aabbMax.y, aabbMax.z},
    };

    float minY = std::numeric_limits<float>::infinity();
    for (const auto& c : corners) {
        const glm::vec4 p = localTransform * glm::vec4(c, 1.0f);
        minY = std::min(minY, p.y);
    }
    return std::isfinite(minY) ? minY : 0.0f;
}
}

NpcSystem::NpcSystem() : meshLoaded(false) {}

NpcSystem::~NpcSystem() {
    Cleanup();
}

bool NpcSystem::Initialize(const glm::vec3& spawnCenter) {
    Cleanup();

    if (!LoadParams()) {
        std::cerr << "Failed to load NPC params; using fallback colors/scales" << std::endl;
        palette01.clear();
        palette01.push_back(glm::vec3(1.0f, 0.0f, 1.0f));
        palette01.push_back(glm::vec3(0.0f, 1.0f, 1.0f));
        palette01.push_back(glm::vec3(1.0f, 1.0f, 0.0f));

        heightScaleMin = heightScaleMax = 1.0f;
        widthScaleMin = widthScaleMax = 1.0f;
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

bool NpcSystem::LoadParams() {
    palette01.clear();

    // Defaults if JSON does not provide scale ranges
    heightScaleMin = heightScaleMax = 1.0f;
    widthScaleMin = widthScaleMax = 1.0f;

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

    auto readScaleRange = [&](const char* key, float& outMin, float& outMax) {
        if (!j.contains(key) || !j[key].is_object()) return;
        const auto& obj = j[key];
        if (obj.contains("min") && obj["min"].is_number()) outMin = obj["min"].get<float>();
        if (obj.contains("max") && obj["max"].is_number()) outMax = obj["max"].get<float>();

        if (!std::isfinite(outMin) || outMin <= 0.0f) outMin = 1.0f;
        if (!std::isfinite(outMax) || outMax <= 0.0f) outMax = 1.0f;
        if (outMax < outMin) std::swap(outMin, outMax);
    };

    readScaleRange("height_scales", heightScaleMin, heightScaleMax);
    readScaleRange("width_scales", widthScaleMin, widthScaleMax);

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

    // Independent deterministic stream for scale selection so NPC positions/colors remain stable.
    const uint32_t seedScale = seedD ^ 0x5343414Cu; // 'SCAL'
    std::seed_seq scaleSeq{seedA, seedB, seedC, seedScale};
    std::mt19937 scaleRng(scaleSeq);

    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDist(0, (int)palette01.size() - 1);

    std::uniform_real_distribution<float> heightDist(heightScaleMin, heightScaleMax);
    std::uniform_real_distribution<float> widthDist(widthScaleMin, widthScaleMax);

    // Same base transform as in rendering, so grounding matches visuals.
    glm::mat4 baseModel(1.0f);
    baseModel = glm::rotate(baseModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    baseModel = glm::scale(baseModel, glm::vec3(0.22f));

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

        const float hScale = heightDist(scaleRng);
        const float wScale = widthDist(scaleRng);

        float y = SampleTerrainHeight(x, z) + kHeightOffset + hoverJitter;
        if (mesh.hasAabb) {
            const glm::mat4 local = baseModel * glm::scale(glm::mat4(1.0f), glm::vec3(wScale, hScale, wScale));
            const float minY = ComputeTransformedMinY(local, mesh.aabbMin, mesh.aabbMax);
            // Ensure bottom (minY) is above ground + hover.
            y += std::max(0.0f, -minY);
        }

        glm::vec3 p(x, y, z);
        if (tooClose(p)) continue;

        const int ci = colorDist(rng);
        NpcInstance inst;
        inst.position = p;
        inst.color01 = palette01[ci];
        inst.heightScale = hScale;
        inst.widthScale = wScale;
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

        const float hScale = heightDist(scaleRng);
        const float wScale = widthDist(scaleRng);

        float y = SampleTerrainHeight(x, z) + kHeightOffset + hoverJitter;
        if (mesh.hasAabb) {
            const glm::mat4 local = baseModel * glm::scale(glm::mat4(1.0f), glm::vec3(wScale, hScale, wScale));
            const float minY = ComputeTransformedMinY(local, mesh.aabbMin, mesh.aabbMax);
            y += std::max(0.0f, -minY);
        }

        glm::vec3 p(x, y, z);

        const int ci = colorDist(rng);
        NpcInstance inst;
        inst.position = p;
        inst.color01 = palette01[ci];
        inst.heightScale = hScale;
        inst.widthScale = wScale;
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
        model = glm::scale(model, glm::vec3(npc.widthScale, npc.heightScale, npc.widthScale));
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
