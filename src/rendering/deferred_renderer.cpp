#include "deferred_renderer.h"
#include <utils/shaders/shader_utils.h>
#include <core/resources.h>
#include <core/params/params.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace DeferredRenderer {
    // Internal state
    static GBuffer gBuffer;
    static ShadowMap shadowMap;
    static DirectionalLight sun;
    
    static GLuint geometryShader = 0;
    static GLuint lightingShader = 0;
    static GLuint shadowShader = 0;
    
    static GLuint quadVAO = 0;
    static GLuint quadVBO = 0;

    // Initialize fullscreen quad for lighting pass
    static void InitQuad() {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,

            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    // Create G-Buffer
    static bool CreateGBuffer(int width, int height) {
        // Generate framebuffer
        glGenFramebuffers(1, &gBuffer.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, gBuffer.fbo);
        
        gBuffer.width = width;
        gBuffer.height = height;

        // Position texture (RGB = world position)
        glGenTextures(1, &gBuffer.positionTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gBuffer.positionTex, 0);

        // Normal texture (RGB = world normal)
        glGenTextures(1, &gBuffer.normalTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gBuffer.normalTex, 0);

        // Albedo texture (RGB = color)
        glGenTextures(1, &gBuffer.albedoTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.albedoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gBuffer.albedoTex, 0);

        // Depth texture
        glGenTextures(1, &gBuffer.depthTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gBuffer.depthTex, 0);

        // Tell OpenGL which color attachments we'll use
        unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        glDrawBuffers(3, attachments);

        // Check framebuffer completeness
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "G-Buffer framebuffer is not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    // Create shadow map
    static bool CreateShadowMap(int resolution) {
        shadowMap.resolution = resolution;

        glGenFramebuffers(1, &shadowMap.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);

        glGenTextures(1, &shadowMap.depthTex);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap.depthTex, 0);
        
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Shadow map framebuffer is not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    bool Initialize(int width, int height) {
        // Initialize sun with default values
        sun.direction = glm::normalize(glm::vec3(-0.3f, -0.7f, -0.5f));
        sun.color = glm::vec3(1.0f, 0.95f, 0.8f);
        sun.intensity = 1.2f;

        // Load shaders
        if (!LoadShaderProgram(Resources::Shaders::Deferred::Geometry::VERTEX, 
                              Resources::Shaders::Deferred::Geometry::FRAGMENT, 
                              geometryShader)) {
            std::cerr << "Failed to load geometry shader" << std::endl;
            return false;
        }

        if (!LoadShaderProgram(Resources::Shaders::Deferred::Lighting::VERTEX, 
                              Resources::Shaders::Deferred::Lighting::FRAGMENT, 
                              lightingShader)) {
            std::cerr << "Failed to load lighting shader" << std::endl;
            return false;
        }

        if (!LoadShaderProgram(Resources::Shaders::Deferred::Shadow::VERTEX, 
                              Resources::Shaders::Deferred::Shadow::FRAGMENT, 
                              shadowShader)) {
            std::cerr << "Failed to load shadow shader" << std::endl;
            return false;
        }

        // Create G-buffer
        if (!CreateGBuffer(width, height)) {
            return false;
        }

        // Create shadow map (4096x4096 for high quality shadows)
        if (!CreateShadowMap(4096)) {
            return false;
        }

        // Initialize fullscreen quad
        InitQuad();

        std::cout << "Deferred renderer initialized successfully" << std::endl;
        return true;
    }

    void Cleanup() {
        if (gBuffer.fbo) {
            glDeleteFramebuffers(1, &gBuffer.fbo);
            glDeleteTextures(1, &gBuffer.positionTex);
            glDeleteTextures(1, &gBuffer.normalTex);
            glDeleteTextures(1, &gBuffer.albedoTex);
            glDeleteTextures(1, &gBuffer.depthTex);
        }

        if (shadowMap.fbo) {
            glDeleteFramebuffers(1, &shadowMap.fbo);
            glDeleteTextures(1, &shadowMap.depthTex);
        }

        if (quadVAO) {
            glDeleteVertexArrays(1, &quadVAO);
            glDeleteBuffers(1, &quadVBO);
        }

        if (geometryShader) glDeleteProgram(geometryShader);
        if (lightingShader) glDeleteProgram(lightingShader);
        if (shadowShader) glDeleteProgram(shadowShader);
    }

    void Resize(int width, int height) {
        // Delete old G-buffer textures
        glDeleteTextures(1, &gBuffer.positionTex);
        glDeleteTextures(1, &gBuffer.normalTex);
        glDeleteTextures(1, &gBuffer.albedoTex);
        glDeleteTextures(1, &gBuffer.depthTex);
        
        // Recreate G-buffer with new size
        glBindFramebuffer(GL_FRAMEBUFFER, gBuffer.fbo);
        
        gBuffer.width = width;
        gBuffer.height = height;

        // Recreate all textures with new dimensions
        glGenTextures(1, &gBuffer.positionTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gBuffer.positionTex, 0);

        glGenTextures(1, &gBuffer.normalTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gBuffer.normalTex, 0);

        glGenTextures(1, &gBuffer.albedoTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.albedoTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gBuffer.albedoTex, 0);

        glGenTextures(1, &gBuffer.depthTex);
        glBindTexture(GL_TEXTURE_2D, gBuffer.depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gBuffer.depthTex, 0);

        unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
        glDrawBuffers(3, attachments);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void BeginGeometryPass() {
        glBindFramebuffer(GL_FRAMEBUFFER, gBuffer.fbo);
        glViewport(0, 0, gBuffer.width, gBuffer.height);
        // Blending corrupts G-buffer values (positions/normals/albedo must not blend).
        glDisable(GL_BLEND);

        // Clear each attachment explicitly (glClearColor would clear to non-sensical position/normal values).
        const GLfloat clear0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, clear0); // gPosition
        glClearBufferfv(GL_COLOR, 1, clear0); // gNormal
        glClearBufferfv(GL_COLOR, 2, clear0); // gAlbedo
        const GLfloat clearDepth = 1.0f;
        glClearBufferfv(GL_DEPTH, 0, &clearDepth);
        glEnable(GL_DEPTH_TEST);

        // ── Backface culling ──
        // Not using GL_CULL_FACE here because building/road meshes have
        // inconsistent winding order.  Instead the fragment shader discards
        // back-facing fragments using screen-space derivative normals,
        // which is winding-order independent.  The depth test also naturally
        // prevents back faces from showing on solid convex geometry.
    }

    void EndGeometryPass() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void BeginShadowPass() {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
        glViewport(0, 0, shadowMap.resolution, shadowMap.resolution);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        // Front-face culling reduces shadow acne for shadow maps.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
    }

    void EndShadowPass() {
        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void LightingPass(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
        glDisable(GL_BLEND);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(lightingShader);

        // Bind G-buffer textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gBuffer.albedoTex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depthTex);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, gBuffer.depthTex);

        // Set uniforms
        glUniform1i(glGetUniformLocation(lightingShader, "gPosition"), 0);
        glUniform1i(glGetUniformLocation(lightingShader, "gNormal"), 1);
        glUniform1i(glGetUniformLocation(lightingShader, "gAlbedo"), 2);
        glUniform1i(glGetUniformLocation(lightingShader, "shadowMap"), 3);
        glUniform1i(glGetUniformLocation(lightingShader, "gDepth"), 4);

        glUniform3fv(glGetUniformLocation(lightingShader, "viewPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(glGetUniformLocation(lightingShader, "sunDirection"), 1, glm::value_ptr(sun.direction));
        glUniform3fv(glGetUniformLocation(lightingShader, "sunColor"), 1, glm::value_ptr(sun.color));
        glUniform1f(glGetUniformLocation(lightingShader, "sunIntensity"), sun.intensity);

        // Ambient (hemisphere) lighting + shadow tuning
        const auto& amb = CoreParams::GetRenderingAmbientParams();
        glUniform1f(glGetUniformLocation(lightingShader, "ambientIntensity"), amb.value("intensity", 0.55f));
        glUniform1f(glGetUniformLocation(lightingShader, "ambientMin"), amb.value("min", 0.08f));
        const glm::vec3 ambientSky(amb.value("sky_r", 0.62f), amb.value("sky_g", 0.74f), amb.value("sky_b", 0.92f));
        const glm::vec3 ambientGround(amb.value("ground_r", 0.26f), amb.value("ground_g", 0.26f), amb.value("ground_b", 0.28f));
        glUniform3fv(glGetUniformLocation(lightingShader, "ambientSkyColor"), 1, glm::value_ptr(ambientSky));
        glUniform3fv(glGetUniformLocation(lightingShader, "ambientGroundColor"), 1, glm::value_ptr(ambientGround));
        glUniform1f(glGetUniformLocation(lightingShader, "shadowStrength"), CoreParams::GetRenderingShadowsParams().value("strength", 0.45f));

        glm::mat4 lightSpaceMatrix = GetLightSpaceMatrix();
        glUniformMatrix4fv(glGetUniformLocation(lightingShader, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        // Render fullscreen quad
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glEnable(GL_DEPTH_TEST);
    }

    void CopyDepthToDefaultFramebuffer() {
        // Blit depth buffer from G-buffer to default framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0, 0, gBuffer.width, gBuffer.height,
            0, 0, gBuffer.width, gBuffer.height,
            GL_DEPTH_BUFFER_BIT, GL_NEAREST
        );
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint GetGeometryShader() { return geometryShader; }
    GLuint GetLightingShader() { return lightingShader; }
    GLuint GetShadowShader() { return shadowShader; }

    const GBuffer& GetGBuffer() { return gBuffer; }
    const ShadowMap& GetShadowMap() { return shadowMap; }

    void SetSunDirection(const glm::vec3& direction) {
        sun.direction = glm::normalize(direction);
    }

    void SetSunColor(const glm::vec3& color) {
        sun.color = color;
    }

    void SetSunIntensity(float intensity) {
        sun.intensity = intensity;
    }

    glm::mat4 GetLightSpaceMatrix() {
        // Create orthographic projection for directional light (sun)
        float orthoSize = 100.0f;
        glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 200.0f);
        
        // Position light to look at origin from the sun direction
        glm::vec3 lightPos = -sun.direction * 50.0f;
        glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        
        return lightProjection * lightView;
    }

    void BindGBufferTextures() {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gBuffer.positionTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBuffer.normalTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gBuffer.albedoTex);
    }
}
