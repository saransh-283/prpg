#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace Skybox {
    // Initialize skybox resources
    bool Initialize();

    // Cleanup skybox resources
    void Cleanup();

    // Render the skybox (should be rendered last with depth test set to GL_LEQUAL)
    void Render(const glm::mat4& view, const glm::mat4& projection);

    // Get skybox shader program
    GLuint GetShader();

    // Set time of day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
    void SetTimeOfDay(float time);
}
