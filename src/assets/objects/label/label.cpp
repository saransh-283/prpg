#include "label.h"

#include <assets/objects/text/text_renderer.h>

#include <glm/gtc/matrix_transform.hpp>

namespace HudLabel {

bool WorldToScreen(const glm::vec3& worldPos,
                   const glm::mat4& proj,
                   const glm::mat4& view,
                   int windowW,
                   int windowH,
                   glm::vec2& outScreenPx) {
    const glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0001f) return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;

    // If it's wildly outside, skip.
    if (ndc.z < -1.0f || ndc.z > 1.0f) return false;

    const float x = (ndc.x * 0.5f + 0.5f) * (float)windowW;
    const float y = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)windowH;
    outScreenPx = glm::vec2(x, y);
    return true;
}

static float ApproxTextWidthPx(const std::string& text) {
    // The built-in bitmap font is roughly ~8px per glyph in most of our UI usage.
    return (float)text.size() * 8.0f;
}

void RenderWorldLabel(const std::string& text,
                      const glm::vec3& worldPos,
                      const glm::mat4& proj,
                      const glm::mat4& view,
                      int windowW,
                      int windowH) {
    glm::vec2 screen;
    if (!WorldToScreen(worldPos, proj, view, windowW, windowH, screen)) return;

    // Center horizontally and lift slightly above the head.
    const float x = screen.x - ApproxTextWidthPx(text) * 0.5f;
    const float y = screen.y - 18.0f;

    RenderTextOverlay(text, x, y, windowW, windowH);
}

} // namespace HudLabel
