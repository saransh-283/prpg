#include "npc.h"


#include <core/resources.h>
#include <core/prompts.h>
#include <world/terrain/terrain.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <utils/frustum/frustum.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <core/params/params.h>
#include <random>
#include <sstream>

#include <assets/objects/label/label.h>

#include <utils/llm/init/init.h>

#include <algorithm>
using nlohmann::json;

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

float ComputeTransformedMaxY(const glm::mat4& localTransform, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
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

    float maxY = -std::numeric_limits<float>::infinity();
    for (const auto& c : corners) {
        const glm::vec4 p = localTransform * glm::vec4(c, 1.0f);
        maxY = std::max(maxY, p.y);
    }
    return std::isfinite(maxY) ? maxY : 0.0f;
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
    (void)spawnCenter; // spawning is now done per-chunk
    return true;
}

void NpcSystem::Cleanup() {
    // Join any running name-generation thread before tearing down.
    if (nameGenThread.joinable()) {
        nameGenThread.join();
    }

    if (meshLoaded) {
        DestroyCustomMesh(mesh);
        meshLoaded = false;
    }
    npcs.clear();
    palette01.clear();
    spawnedChunkKeys.clear();
    pendingNameIds.clear();
    queuedNameIds.clear();
    recentNames.clear();
    batchTimer = 0.0f;
    batchWindowOpen = false;
    generationRunning = false;
}

void NpcSystem::SpawnForChunk(int cx, int cz) {
    if (!meshLoaded) return;

    const long long k = keyFor(cx, cz);
    if (spawnedChunkKeys.find(k) != spawnedChunkKeys.end()) return;
    spawnedChunkKeys.insert(k);

    GenerateDeterministicSpawnsForChunk(cx, cz);
}

bool NpcSystem::LoadParams() {
    palette01.clear();

    // Defaults if JSON does not provide scale ranges
    heightScaleMin = heightScaleMax = 1.0f;
    widthScaleMin = widthScaleMax = 1.0f;

    // Load NPC params via core params accessor.
    json j = CoreParams::GetNpcParams();
    if (j.is_null()) {
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

    const auto& world = CoreParams::GetWorldParams();
    const uint32_t seedA = (uint32_t)world.value("perlin_seed", 1337);
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
        inst.name = "Villager";
        inst.id = (int)npcs.size();
        inst.nameRequested = false;

        if (mesh.hasAabb) {
            const glm::mat4 local = baseModel * glm::scale(glm::mat4(1.0f), glm::vec3(wScale, hScale, wScale));
            const float maxY = ComputeTransformedMaxY(local, mesh.aabbMin, mesh.aabbMax);
            inst.headLabelOffsetY = maxY + 0.25f;
        } else {
            inst.headLabelOffsetY = 1.6f;
        }
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
        inst.name = "Villager";
        inst.id = (int)npcs.size();
        inst.nameRequested = false;

        if (mesh.hasAabb) {
            const glm::mat4 local = baseModel * glm::scale(glm::mat4(1.0f), glm::vec3(wScale, hScale, wScale));
            const float maxY = ComputeTransformedMaxY(local, mesh.aabbMin, mesh.aabbMax);
            inst.headLabelOffsetY = maxY + 0.25f;
        } else {
            inst.headLabelOffsetY = 1.6f;
        }
        npcs.push_back(inst);
        spawned++;
    }

    std::cout << "NPCs spawned: " << npcs.size() << std::endl;
}

void NpcSystem::GenerateDeterministicSpawnsForChunk(int cx, int cz) {
    if (palette01.empty()) return;

    const auto& world = CoreParams::GetWorldParams();

    // Chunk world-space bounds match terrain generation: (chunkSize-1) * vertexSpacing.
    const int chunkSize = world.value("chunk_size", 128);
    const float vertexSpacing = world.value("vertex_spacing", 0.5f);
    const float chunkWorldSize = (float)(chunkSize - 1) * vertexSpacing;
    const float originX = (float)cx * chunkWorldSize;
    const float originZ = (float)cz * chunkWorldSize;

    // Keep some padding from the edges.
    const float pad = 2.0f * vertexSpacing;
    const float minX = originX + pad;
    const float maxX = originX + chunkWorldSize - pad;
    const float minZ = originZ + pad;
    const float maxZ = originZ + chunkWorldSize - pad;
    if (!(maxX > minX && maxZ > minZ)) return;

    // Deterministic seed per chunk.
    const uint32_t seedA = (uint32_t)world.value("perlin_seed", 1337);
    const uint32_t seedB = (uint32_t)(cx ^ (cx >> 16));
    const uint32_t seedC = (uint32_t)(cz ^ (cz >> 16));
    const uint32_t seedD = 0x4E504343u; // 'NPCC'
    std::seed_seq seq{seedA, seedB, seedC, seedD};
    std::mt19937 rng(seq);

    // Variable NPC count per chunk (deterministic).
    constexpr int kMaxNpcPerChunk = 4;
    std::uniform_int_distribution<int> countDist(0, kMaxNpcPerChunk);
    const int targetCount = countDist(rng);
    if (targetCount <= 0) return;

    // Independent deterministic stream for scale selection.
    const uint32_t seedScale = seedD ^ 0x5343414Cu; // 'SCAL'
    std::seed_seq scaleSeq{seedA, seedB, seedC, seedScale};
    std::mt19937 scaleRng(scaleSeq);

    std::uniform_real_distribution<float> xDist(minX, maxX);
    std::uniform_real_distribution<float> zDist(minZ, maxZ);
    std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> colorDist(0, (int)palette01.size() - 1);

    std::uniform_real_distribution<float> heightDist(heightScaleMin, heightScaleMax);
    std::uniform_real_distribution<float> widthDist(widthScaleMin, widthScaleMax);

    // Same base transform as in rendering, so grounding matches visuals.
    glm::mat4 baseModel(1.0f);
    baseModel = glm::rotate(baseModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    baseModel = glm::scale(baseModel, glm::vec3(0.22f));

    constexpr float kMinSeparation = 2.0f;
    constexpr float kHeightOffset = 0.28f;

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
    const int maxAttempts = targetCount * 60;
    while (spawned < targetCount && attempts < maxAttempts) {
        attempts++;

        const float x = xDist(rng);
        const float z = zDist(rng);
        if (CollidesWithBuilding(x, z, 0.45f)) continue;

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
        if (tooClose(p)) continue;

        const int ci = colorDist(rng);
        NpcInstance inst;
        inst.position = p;
        inst.color01 = palette01[ci];
        inst.heightScale = hScale;
        inst.widthScale = wScale;
        inst.name = "Villager";
        inst.id = (int)npcs.size();
        inst.nameRequested = false;

        if (mesh.hasAabb) {
            const glm::mat4 local = baseModel * glm::scale(glm::mat4(1.0f), glm::vec3(wScale, hScale, wScale));
            const float maxY = ComputeTransformedMaxY(local, mesh.aabbMin, mesh.aabbMax);
            inst.headLabelOffsetY = maxY + 0.25f;
        } else {
            inst.headLabelOffsetY = 1.6f;
        }

        npcs.push_back(inst);
        spawned++;
    }
}

void NpcSystem::RenderNameLabels(const glm::vec3& playerPos, const glm::mat4& proj, const glm::mat4& view, int windowW, int windowH) const {
    if (!meshLoaded) return;

    // Show labels only when close enough to the player.
    constexpr float kShowRadius = 7.0f;
    constexpr float kShowRadiusSq = kShowRadius * kShowRadius;

    for (const auto& npc : npcs) {
        const glm::vec3 d = npc.position - playerPos;
        const float distSq = d.x * d.x + d.z * d.z;
        if (distSq > kShowRadiusSq) continue;

        const glm::vec3 labelPos = npc.position + glm::vec3(0.0f, npc.headLabelOffsetY, 0.0f);
        HudLabel::RenderWorldLabel(npc.name, labelPos, proj, view, windowW, windowH);
    }
}

void NpcSystem::RenderInstancesCommon(GLuint shaderProgram,
                                      int modelLoc,
                                      const glm::mat4& baseModel,
                                      const glm::mat4& viewProj) const {
    if (!meshLoaded) return;

    const FrustumUtil::Frustum frustum = FrustumUtil::ExtractFrustum(viewProj);

    float localRadius = 0.55f;
    if (mesh.hasAabb) {
        const glm::vec3 ext = (mesh.aabbMax - mesh.aabbMin) * 0.5f;
        localRadius = glm::length(ext);
    }

    // Matches the base model used in the render passes.
    constexpr float kBaseUniformScale = 0.22f;

    for (const auto& npc : npcs) {
        const float r = localRadius * kBaseUniformScale * std::max(npc.widthScale, npc.heightScale);
        if (!FrustumUtil::IntersectsSphere(frustum, npc.position, r)) {
            continue;
        }
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

void NpcSystem::RenderToGBuffer(GLuint geometryShader,
                                const glm::mat4& proj,
                                const glm::mat4& view,
                                const glm::vec3& cameraPos,
                                const glm::vec3& cameraFront) const {
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

    (void)cameraPos;
    (void)cameraFront;
    RenderInstancesCommon(geometryShader, modelLoc, baseModel, proj * view);
}

void NpcSystem::RenderToShadowMap(GLuint shadowShader,
                                  const glm::mat4& lightSpaceMatrix,
                                  const glm::mat4& proj,
                                  const glm::mat4& view) const {
    if (!meshLoaded || shadowShader == 0) return;

    glUseProgram(shadowShader);

    const GLint lightLoc = glGetUniformLocation(shadowShader, "lightSpaceMatrix");
    const GLint modelLoc = glGetUniformLocation(shadowShader, "model");

    if (lightLoc >= 0) glUniformMatrix4fv(lightLoc, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    glm::mat4 baseModel(1.0f);
    baseModel = glm::rotate(baseModel, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    baseModel = glm::scale(baseModel, glm::vec3(0.22f));

    RenderInstancesCommon(shadowShader, modelLoc, baseModel, proj * view);
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

// ─────────────────────────────────────────────────────────────────────────────
// Async NPC Name Generation
// ─────────────────────────────────────────────────────────────────────────────

void NpcSystem::UpdateNameGeneration(const glm::vec3& playerPos, float deltaTime) {
    if (!meshLoaded) return;

    const auto& llmParams = CoreParams::GetLLMParams();
    const float radiusSq = llmParams.value("npc_name_radius", 20.0f) * llmParams.value("npc_name_radius", 20.0f);

    // ── 1. Detect unnamed NPCs within radius ────────────────────────────
    {
        std::lock_guard<std::mutex> lock(nameMutex);
        for (auto& npc : npcs) {
            if (npc.nameRequested) continue;

            const float dx = npc.position.x - playerPos.x;
            const float dz = npc.position.z - playerPos.z;
            if ((dx * dx + dz * dz) > radiusSq) continue;

            // First detection in this batch?
            if (!batchWindowOpen) {
                batchWindowOpen = true;
                batchTimer = 0.0f;
                std::cout << "[NPC-Names] First NPC detected nearby (id=" << npc.id
                          << "), opening 2-second batch window." << std::endl;
            }

            npc.nameRequested = true;

            // If a generation is already running, queue for the next batch.
            if (generationRunning.load()) {
                queuedNameIds.push_back(npc.id);
            } else {
                pendingNameIds.push_back(npc.id);
            }
        }
    }

    // ── 2. Advance batch timer ──────────────────────────────────────────
    if (batchWindowOpen) {
        batchTimer += deltaTime;
    }

    // ── 3. Fire when the window closes ──────────────────────────────────
    if (batchWindowOpen && batchTimer >= CoreParams::GetLLMParams().value("npc_name_batch_window", 2.0f)) {
        std::lock_guard<std::mutex> lock(nameMutex);

        batchWindowOpen = false;
        batchTimer = 0.0f;

        if (!pendingNameIds.empty() && !generationRunning.load()) {
            std::cout << "[NPC-Names] Batch window closed. " << pendingNameIds.size()
                      << " NPC(s) detected in 2 seconds; starting name generation."
                      << std::endl;

            std::vector<int> batch = std::move(pendingNameIds);
            pendingNameIds.clear();
            StartNameGeneration(std::move(batch));
        }
    }

    // ── 4. If a previous generation finished and there is a queued batch,
    //       start the next one. ──────────────────────────────────────────
    if (!generationRunning.load()) {
        // Join the finished thread, if any.
        if (nameGenThread.joinable()) {
            nameGenThread.join();
        }

        std::lock_guard<std::mutex> lock(nameMutex);
        if (!queuedNameIds.empty()) {
            std::cout << "[NPC-Names] Previous generation done. Starting queued batch of "
                      << queuedNameIds.size() << " NPC(s)." << std::endl;

            std::vector<int> batch = std::move(queuedNameIds);
            queuedNameIds.clear();
            StartNameGeneration(std::move(batch));
        }
    }
}

void NpcSystem::StartNameGeneration(std::vector<int> ids) {
    // Must be called with nameMutex held.
    if (ids.empty()) return;

    generationRunning = true;

    // Snapshot recent names for the prompt (read under lock).
    std::vector<std::string> recentSnapshot(recentNames.begin(), recentNames.end());

    // Detach-safe: capture this pointer (the NpcSystem outlives the thread
    // because Cleanup() joins it).
    nameGenThread = std::thread([this, ids = std::move(ids), recentSnapshot = std::move(recentSnapshot)]() {
        // ── Wait for LLM to be ready ────────────────────────────────────
        if (!is_llm_ready()) {
            if (is_llm_loading()) {
                std::cout << "[NPC-Names] Waiting for LLM model to finish loading..." << std::endl;
                wait_for_llm_load();
            }
            if (!is_llm_ready()) {
                std::cerr << "[NPC-Names] LLM not available; skipping name generation." << std::endl;
                generationRunning = false;
                return;
            }
        }

        const int count = (int)ids.size();

        // Log the exclusion list being sent to the LLM.
        if (!recentSnapshot.empty()) {
            std::string excludeLog = "[NPC-Names] Recent names (excluded): ";
            for (size_t i = 0; i < recentSnapshot.size(); ++i) {
                if (i > 0) excludeLog += ", ";
                excludeLog += recentSnapshot[i];
            }
            std::cout << excludeLog << std::endl;
        } else {
            std::cout << "[NPC-Names] No recent names to exclude (first batch)." << std::endl;
        }

        std::string prompt = Prompts::NpcNameGeneration(count, recentSnapshot);

        std::cout << "[NPC-Names] Generating " << count << " name(s) via LLM..." << std::endl;

        // Clear LLM KV-cache so each generation starts fresh and doesn't
        // repeat names from a previous conversation turn.
        clear_llm_context();

        std::string raw = generate_from_prompt(prompt);

        // Build a lowercase set of recent names for fast duplicate checking.
        auto toLower = [](std::string s) {
            for (auto& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        };
        std::unordered_set<std::string> recentSet;
        for (const auto& n : recentSnapshot) recentSet.insert(toLower(n));

        // ── Parse response: one name per line ───────────────────────────
        std::vector<std::string> names;
        {
            std::istringstream iss(raw);
            std::string line;
            while (std::getline(iss, line)) {
                // Trim whitespace
                size_t start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                size_t end = line.find_last_not_of(" \t\r\n");
                std::string trimmed = line.substr(start, end - start + 1);

                // Skip empty or too-long lines
                if (trimmed.empty() || trimmed.size() > 30) continue;

                // Remove leading numbering (e.g., "1. ", "1) ")
                if (!trimmed.empty() && std::isdigit(trimmed[0])) {
                    size_t pos = trimmed.find_first_of(".) ");
                    if (pos != std::string::npos && pos < 4) {
                        size_t nameStart = trimmed.find_first_not_of(" \t", pos + 1);
                        if (nameStart != std::string::npos) {
                            trimmed = trimmed.substr(nameStart);
                        }
                    }
                }

                // Take only the first word as the name (in case the LLM added descriptions)
                {
                    size_t spacePos = trimmed.find(' ');
                    if (spacePos != std::string::npos) {
                        trimmed = trimmed.substr(0, spacePos);
                    }
                }

                if (trimmed.empty()) continue;

                // ── Post-generation duplicate check (case-insensitive) ──
                std::string lowerName = toLower(trimmed);
                if (recentSet.count(lowerName)) {
                    std::cout << "[NPC-Names] Skipping duplicate name from LLM: " << trimmed << std::endl;
                    continue;
                }

                // Also guard against duplicates within this same batch.
                recentSet.insert(lowerName);
                names.push_back(trimmed);
            }
        }

        // ── Assign names to NPCs ────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(nameMutex);

            // Re-read recentNames under lock to also exclude names that may
            // have been added by another thread between snapshot and now.
            for (const auto& n : recentNames) recentSet.insert(toLower(n));

            std::string logMsg = "[NPC-Names] Generated names: ";
            size_t nameIdx = 0;
            for (size_t i = 0; i < ids.size(); ++i) {
                const int id = ids[i];

                // Pick the next non-duplicate name from the parsed list.
                std::string assignedName;
                while (nameIdx < names.size()) {
                    std::string candidate = names[nameIdx++];
                    std::string lower = toLower(candidate);
                    // Final duplicate guard against recentNames updated in the meantime.
                    if (!recentSet.count(lower) || nameIdx <= ids.size()) {
                        assignedName = candidate;
                        break;
                    }
                }
                if (assignedName.empty()) {
                    assignedName = (nameIdx <= names.size() && !names.empty())
                                       ? names[std::min(nameIdx, names.size()) - 1]
                                       : "Villager";
                }

                // Find the NPC by id and assign.
                for (auto& npc : npcs) {
                    if (npc.id == id) {
                        npc.name = assignedName;
                        break;
                    }
                }

                // Update recent names ring-buffer.
                recentNames.push_back(assignedName);
                if ((int)recentNames.size() > CoreParams::GetLLMParams().value("npc_name_history_size", 15)) {
                    recentNames.pop_front();
                }
                recentSet.insert(toLower(assignedName));

                if (i > 0) logMsg += ", ";
                logMsg += assignedName;
            }

            std::cout << logMsg << std::endl;
        }

        generationRunning = false;
    });
}
