#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace DeferredRenderer {
    // G-Buffer textures
    struct GBuffer {
        GLuint fbo;              // Framebuffer object
        GLuint positionTex;      // World-space positions
        GLuint normalTex;        // World-space normals
        GLuint albedoTex;        // Color/albedo
        GLuint depthTex;         // Depth buffer
        int width;
        int height;
    };

    // Shadow mapping
    struct ShadowMap {
        GLuint fbo;
        GLuint depthTex;
        int resolution;
    };

    // Directional light (sun)
    struct DirectionalLight {
        glm::vec3 direction;
        glm::vec3 color;
        float intensity;
    };

    // Initialize the deferred renderer
    bool Initialize(int width, int height);

    // Cleanup resources
    void Cleanup();

    // Resize G-buffer when window resizes
    void Resize(int width, int height);

    // Begin geometry pass (renders to G-buffer)
    void BeginGeometryPass();

    // End geometry pass
    void EndGeometryPass();

    // Begin shadow pass
    void BeginShadowPass();

    // End shadow pass
    void EndShadowPass();

    // Perform lighting pass (reads G-buffer, outputs final lit scene)
    void LightingPass(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);

    // Copy depth from G-buffer to default framebuffer (call after lighting pass, before forward rendering)
    void CopyDepthToDefaultFramebuffer();

    // Get shader programs
    GLuint GetGeometryShader();
    GLuint GetGeometryWindowsShader();
    GLuint GetLightingShader();
    GLuint GetShadowShader();

    // Get G-Buffer
    const GBuffer& GetGBuffer();

    // Get shadow map
    const ShadowMap& GetShadowMap();

    // Set sun/directional light properties
    void SetSunDirection(const glm::vec3& direction);
    void SetSunColor(const glm::vec3& color);
    void SetSunIntensity(float intensity);

    // Get light-space matrix for shadow mapping
    glm::mat4 GetLightSpaceMatrix();

    // Bind G-buffer textures for reading
    void BindGBufferTextures();
}
