#pragma once

#include <stdbool.h>
#include <glm/glm.hpp>

// Initialize and cleanup the debug overlay (text is rendered via the project's
// existing text renderer). Hidden by default.
void InitDebugOverlay();
void CleanupDebugOverlay();

// Toggle visibility (tied to the tilde/backquote key)
void ToggleDebugOverlay();
bool IsDebugOverlayVisible();

// Render the debug overlay. Pass window size, wireframe state and camera/player
// world position so the overlay can display chunk coordinates and player pos.
void RenderDebugOverlay(int windowW, int windowH, bool wireframeMode, const glm::vec3& cameraPos);
