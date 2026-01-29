#include <ui/debug/debug_overlay.h>
#include "../../config.h"
#include <stdio.h>
#include <objects/text/text_renderer.h>
#include <world/terrain/terrain.h>
#include <glm/gtc/type_ptr.hpp>

static bool g_visible = false;

void InitDebugOverlay() {
    // start hidden by default
    g_visible = false;
}

void CleanupDebugOverlay() {
    // nothing to free here; text system manages its own resources
}

void ToggleDebugOverlay() {
    g_visible = !g_visible;
}

bool IsDebugOverlayVisible() {
    return g_visible;
}

void RenderDebugOverlay(int windowW, int windowH, bool wireframeMode, const glm::vec3& cameraPos) {
    if (!g_visible) return;

    // Draw a simple debug panel in the top-left like the screenshot
    const float padX = Config::UI::Debug::PADDING_X;
    const float padY = Config::UI::Debug::PADDING_Y;
    const float lineH = Config::UI::Debug::LINE_HEIGHT;

    RenderTextOverlay("PROCGEN - Minimal Text Demo", padX, padY + 0.0f, windowW, windowH);
    RenderTextOverlay("Press ESC or close window to exit", padX, padY + lineH, windowW, windowH);
    char wfbuf[64];
    snprintf(wfbuf, sizeof(wfbuf), "Wireframe: %s (press 'F' to toggle)", (wireframeMode ? "ON" : "OFF"));
    RenderTextOverlay(wfbuf, padX, padY + lineH * 2.0f, windowW, windowH);

    // Player position
    char posbuf[128];
    snprintf(posbuf, sizeof(posbuf), "PlayerPos: x=%.2f y=%.2f z=%.2f", cameraPos.x, cameraPos.y, cameraPos.z);
    RenderTextOverlay(posbuf, padX, padY + lineH * 3.0f, windowW, windowH);

    // Chunk coordinates
    int cx = 0, cz = 0;
    WorldToChunk(cameraPos.x, cameraPos.z, cx, cz);
    char chbuf[64];
    snprintf(chbuf, sizeof(chbuf), "Chunk: (%d, %d)", cx, cz);
    RenderTextOverlay(chbuf, padX, padY + lineH * 4.0f, windowW, windowH);
}
