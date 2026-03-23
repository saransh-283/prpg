#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_set>

#include <assets/objects/models/3d/custom/mesh.h>

class NpcSystem {
public:
    NpcSystem();
    ~NpcSystem();

    // Loads the pill model and parameters. Spawning is done separately per chunk.
    bool Initialize(const glm::vec3& spawnCenter);

    // Deterministically spawn NPCs for a specific chunk.
    // Safe to call multiple times; subsequent calls for the same chunk are ignored.
    void SpawnForChunk(int cx, int cz);

    void RenderToGBuffer(GLuint geometryShader,
                         const glm::mat4& proj,
                         const glm::mat4& view,
                         const glm::vec3& cameraPos,
                         const glm::vec3& cameraFront) const;
    void RenderToShadowMap(GLuint shadowShader,
                           const glm::mat4& lightSpaceMatrix,
                           const glm::vec3& cameraPos,
                           const glm::vec3& cameraFront) const;

    // HUD: render NPC name labels in screen-space (not affected by lighting).
    // Labels are shown only when the player is close enough.
    void RenderNameLabels(const glm::vec3& playerPos, const glm::mat4& proj, const glm::mat4& view, int windowW, int windowH) const;

    // Called every frame from the main loop.  Detects nearby NPCs that need
    // names, batches them for 2 seconds, then dispatches an async LLM call.
    void UpdateNameGeneration(const glm::vec3& playerPos, float deltaTime);

    // Simple collision query in XZ-plane: true if a circle at (x,z) with radius
    // overlaps any NPC's collision radius.
    bool CollidesXZ(float x, float z, float radius) const;

    void Cleanup();

private:
    struct NpcInstance {
        glm::vec3 position;
        glm::vec3 color01; // 0..1

        std::string name;

        // Unique per-NPC id (index in the npcs vector at creation time).
        int id = -1;

        // True once a name has been generated (or is being generated).
        bool nameRequested = false;

        // Precomputed vertical offset from NPC origin to the top of the head (plus a small padding), in world units.
        float headLabelOffsetY = 1.6f;

        // Non-uniform scale applied in model space.
        // widthScale affects X/Z; heightScale affects Y.
        float widthScale = 1.0f;
        float heightScale = 1.0f;
    };

    CustomMesh mesh;
    bool meshLoaded;

    std::vector<glm::vec3> palette01;
    std::vector<NpcInstance> npcs;

    std::unordered_set<long long> spawnedChunkKeys;

    // Scale ranges loaded from npc_params.json
    float heightScaleMin = 1.0f;
    float heightScaleMax = 1.0f;
    float widthScaleMin = 1.0f;
    float widthScaleMax = 1.0f;

    static constexpr float kNpcCollisionRadius = 0.28f;

    bool LoadParams();
    void GenerateDeterministicSpawns(const glm::vec3& spawnCenter);
    void GenerateDeterministicSpawnsForChunk(int cx, int cz);

    void RenderInstancesCommon(GLuint shaderProgram,
                               int modelLoc,
                               const glm::mat4& baseModel,
                               const glm::vec3& cameraPos,
                               const glm::vec3& cameraFront) const;

    // ── Name-generation bookkeeping ──────────────────────────────────────

    // IDs of NPCs that are waiting in the current batch window.
    std::vector<int> pendingNameIds;

    // Timer that counts up while we are collecting NPCs for a batch.
    float batchTimer = 0.0f;

    // True while the batch window is open (we've seen at least one unnamed NPC).
    bool batchWindowOpen = false;

    // True while an async generation thread is running.
    std::atomic<bool> generationRunning{false};

    // Background thread for name generation.
    std::thread nameGenThread;

    // Mutex protecting pendingNameIds and the NPC name writes.
    std::mutex nameMutex;

    // Ring-buffer of the last N generated names (for duplicate avoidance).
    std::deque<std::string> recentNames;

    // The next batch to process once the current generation finishes.
    std::vector<int> queuedNameIds;

    // Launches a background thread that calls the LLM and assigns names.
    void StartNameGeneration(std::vector<int> ids);
};
