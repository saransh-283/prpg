#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

// Initialize minimap resources
bool InitMinimap();

// Render minimap given camera position, window size, marker shader and per-layer shaders
void RenderMinimap(const glm::vec3& cameraPos, int windowW, int windowH,
				   GLuint markerProgram,
				   GLuint terrainProgram,
				   GLuint highwaysProgram,
				   GLuint roadsProgram,
				   GLuint streetsProgram);

// Cleanup minimap resources
void CleanupMinimap();
