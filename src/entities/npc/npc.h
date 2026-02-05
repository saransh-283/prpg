#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

#include <assets/objects/models/3d/custom/mesh.h>

class NpcSystem {
public:
    NpcSystem();
    ~NpcSystem();

    // Loads the pill model and generates deterministic NPC instances near spawnCenter.
    bool Initialize(const glm::vec3& spawnCenter);

    void RenderToGBuffer(GLuint geometryShader, const glm::mat4& proj, const glm::mat4& view) const;
    void RenderToShadowMap(GLuint shadowShader, const glm::mat4& lightSpaceMatrix) const;

    // Simple collision query in XZ-plane: true if a circle at (x,z) with radius
    // overlaps any NPC's collision radius.
    bool CollidesXZ(float x, float z, float radius) const;

    void Cleanup();

private:
    struct NpcInstance {
        glm::vec3 position;
        glm::vec3 color01; // 0..1
    };

    CustomMesh mesh;
    bool meshLoaded;

    std::vector<glm::vec3> palette01;
    std::vector<NpcInstance> npcs;

    static constexpr float kNpcCollisionRadius = 0.28f;

    bool LoadPalette();
    void GenerateDeterministicSpawns(const glm::vec3& spawnCenter);

    void RenderInstancesCommon(GLuint shaderProgram, int modelLoc, const glm::mat4& baseModel) const;
};
