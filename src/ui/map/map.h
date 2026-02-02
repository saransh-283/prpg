#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

// Initialize map resources
bool InitMap();

// Render map given camera position, window size, marker shader and per-layer shaders
// Supports both small corner map and full-screen map mode
void RenderMap(const glm::vec3& cameraPos, const glm::vec3& cameraFront, int windowW, int windowH,
               GLuint solidColorProgram,
               GLuint markerSdfProgram,
               GLuint terrainProgram,
               GLuint highwaysProgram,
               GLuint roadsProgram,
               GLuint streetsProgram,
               GLuint buildingsProgram,
               bool showFullMap = false,
               const glm::vec2& mapOffset = glm::vec2(0.0f));

// Toggle map visibility
void ToggleMap();

// Get map visibility state
bool IsMapVisible();

// Update map scroll offset
void UpdateMapOffset(const glm::vec2& delta);

// Adjust map zoom level
void ZoomMap(float delta);

// Get current zoom level
float GetMapZoom();

// Cleanup map resources
void CleanupMap();
