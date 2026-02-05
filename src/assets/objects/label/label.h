#pragma once

#include <glm/glm.hpp>
#include <string>

namespace HudLabel {

// Projects a world position to screen pixel coordinates.
// Returns false if the point is behind the camera.
bool WorldToScreen(const glm::vec3& worldPos,
                   const glm::mat4& proj,
                   const glm::mat4& view,
                   int windowW,
                   int windowH,
                   glm::vec2& outScreenPx);

// Renders a small HUD label at a world position (screen-space, unlit).
// The label is drawn slightly above the projected point.
void RenderWorldLabel(const std::string& text,
                      const glm::vec3& worldPos,
                      const glm::mat4& proj,
                      const glm::mat4& view,
                      int windowW,
                      int windowH);

} // namespace HudLabel
