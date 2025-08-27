#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

// Initialize minimap resources
bool InitMinimap();

// Render minimap given camera position, projection and window size
void RenderMinimap(const glm::vec3& cameraPos, int windowW, int windowH, GLuint shaderProgram);

// Cleanup minimap resources
void CleanupMinimap();
