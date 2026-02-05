#include "map.h"
#include <core/config.h>
#include <core/resources.h>

#include <array>
#include <cmath>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <world/terrain/terrain.h>

#include <stb_image.h>

static GLuint g_miniVAO = 0;
static GLuint g_miniVBO = 0;
static GLuint g_chunkBorderVAO = 0;
static GLuint g_chunkBorderVBO = 0;
static bool g_mapVisible = false;
static glm::vec2 g_mapOffset(0.0f, 0.0f);
static float g_mapZoom = Config::UI::Map::ZOOM_DEFAULT;
static GLuint g_markerSdfTex = 0;
static int g_markerSdfW = 0;
static int g_markerSdfH = 0;

namespace {
    inline glm::vec2 SafeNormalize2(const glm::vec2& v, const glm::vec2& fallback) {
        float len2 = glm::dot(v, v);
        if (len2 < 1e-8f) return fallback;
        return v / std::sqrt(len2);
    }

    GLuint LoadSdfTextureR8(const char* path, int& outW, int& outH) {
        outW = 0;
        outH = 0;

        // stb_image loads with (0,0) at top-left; flip so OpenGL's UVs feel natural.
        stbi_set_flip_vertically_on_load(1);
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(path, &w, &h, &channels, 1);
        stbi_set_flip_vertically_on_load(0);

        if (!data || w <= 0 || h <= 0) {
            if (data) stbi_image_free(data);
            return 0;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(data);
        outW = w;
        outH = h;
        return tex;
    }
}

bool InitMap() {
    glGenVertexArrays(1, &g_miniVAO);
    glGenBuffers(1, &g_miniVBO);
    glBindVertexArray(g_miniVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
    // Player marker quad (two triangles), interleaved position + UV.
    // 6 verts * (3 pos + 2 uv) floats
    glBufferData(GL_ARRAY_BUFFER, (6 * 5) * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    
    // Initialize chunk border rendering (dynamic buffer for lines)
    glGenVertexArrays(1, &g_chunkBorderVAO);
    glGenBuffers(1, &g_chunkBorderVBO);
    glBindVertexArray(g_chunkBorderVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_chunkBorderVBO);
    glBufferData(GL_ARRAY_BUFFER, 10000 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Load SDF marker texture.
    g_markerSdfTex = LoadSdfTextureR8(Resources::Images::UI::MARKER, g_markerSdfW, g_markerSdfH);
    if (!g_markerSdfTex) {
        // Non-fatal: marker will simply not render.
        // (Avoid logging spam here; InitMap is called once.)
    }
    
    return true;
}

void ToggleMap() {
    g_mapVisible = !g_mapVisible;
    // Reset offset when toggling map
    if (!g_mapVisible) {
        g_mapOffset = glm::vec2(0.0f, 0.0f);
    }
}

bool IsMapVisible() {
    return g_mapVisible;
}

void UpdateMapOffset(const glm::vec2& delta) {
    g_mapOffset += delta;
}

void ZoomMap(float delta) {
    g_mapZoom += delta;
    if (g_mapZoom < Config::UI::Map::ZOOM_MIN) g_mapZoom = Config::UI::Map::ZOOM_MIN;
    if (g_mapZoom > Config::UI::Map::ZOOM_MAX) g_mapZoom = Config::UI::Map::ZOOM_MAX;
}

float GetMapZoom() {
    return g_mapZoom;
}

void RenderMap(const glm::vec3& cameraPos, const glm::vec3& cameraFront, int windowW, int windowH,
                   GLuint solidColorProgram,
                   GLuint markerSdfProgram,
                   GLuint terrainProgram,
                   GLuint highwaysProgram,
                   GLuint roadsProgram,
                   GLuint streetsProgram,
                   GLuint buildingsProgram,
                   bool showFullMap,
                   const glm::vec2& mapOffset) {
    // Use internal offset instead of parameter (parameter kept for API compatibility)
    (void)mapOffset; // Suppress unused parameter warning
    
    // Save previous viewport
    GLint oldVp[4];
    glGetIntegerv(GL_VIEWPORT, oldVp);

    int miniX, miniY, miniW, miniH;
    float worldRadius;
    
    if (showFullMap) {
        // Full screen map mode
        miniX = 0;
        miniY = 0;
        miniW = windowW;
        miniH = windowH;
        worldRadius = (Config::UI::Map::WORLD_RADIUS * Config::UI::Map::MAP_WORLD_RADIUS_MULTIPLIER) / g_mapZoom;
    } else {
        // Small map in corner
        const int defaultSize = Config::UI::Map::SIZE;
        const int margin = Config::UI::Map::MARGIN;
        miniX = windowW - defaultSize - margin;
        miniY = windowH - defaultSize - margin;
        miniW = defaultSize;
        miniH = defaultSize;
        worldRadius = Config::UI::Map::WORLD_RADIUS / g_mapZoom; // Apply zoom to minimap too
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(miniX, miniY, miniW, miniH);
    GLfloat prevClear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glClearColor(0.06f, 0.06f, 0.07f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Apply map offset (use internal state for scrolling)
    float cx = cameraPos.x + g_mapOffset.x;
    float cz = cameraPos.z + g_mapOffset.y;

    // Calculate aspect ratio to prevent stretching
    float aspect = static_cast<float>(miniW) / static_cast<float>(miniH);
    float worldRadiusX = worldRadius * aspect;
    float worldRadiusY = worldRadius;
    
    glm::mat4 projMini = glm::ortho(-worldRadiusX, worldRadiusX, -worldRadiusY, worldRadiusY, -1000.0f, 1000.0f);
    glm::vec3 eye(cx, Config::UI::Map::CAMERA_HEIGHT, cz);
    glm::vec3 center(cx, 0.0f, cz);
    glm::vec3 up(0.0f, 0.0f, -1.0f);
    glm::mat4 viewMini = glm::lookAt(eye, center, up);

    glViewport(miniX, miniY, miniW, miniH);

    // Render terrain top-down using the specialized per-layer shaders
    RenderTerrain(terrainProgram, highwaysProgram, roadsProgram, streetsProgram, buildingsProgram, projMini, viewMini);

    // Shared MVP for HUD map rendering (marker + borders).
    glm::mat4 mvpMini = projMini * viewMini * glm::mat4(1.0f);

    // Player marker: rounded-bottom triangle pointing in facing direction
    // Use minimap marker size if small map, full map marker size if full screen
    float markerSizeRatio = showFullMap ? Config::UI::Map::MARKER_SIZE_MAP : Config::UI::Map::MARKER_SIZE_MINIMAP;
    float markSize = worldRadius * markerSizeRatio;
    // Use original camera position for marker (not offset)
    float markerX = cameraPos.x;
    float markerZ = cameraPos.z;
    float hy = SampleTerrainHeight(markerX, markerZ) + Config::UI::Map::MARKER_HEIGHT_OFFSET;

    // Project facing direction onto XZ plane.
    glm::vec2 f2 = SafeNormalize2(glm::vec2(cameraFront.x, cameraFront.z), glm::vec2(0.0f, 1.0f));
    // yaw = angle from +Z toward +X
    float yaw = std::atan2(f2.x, f2.y);
    float c = std::cos(yaw);
    float sn = std::sin(yaw);

    auto rotXZ = [&](float lx, float lz) -> glm::vec2 {
        // Rotate local (x,z) around Y by yaw
        return glm::vec2(lx * c + lz * sn, -lx * sn + lz * c);
    };

    if (g_markerSdfTex && markerSdfProgram) {
        // Size the quad using the texture aspect ratio so it doesn't get stretched.
        float texAspect = 1.0f;
        if (g_markerSdfW > 0 && g_markerSdfH > 0) {
            texAspect = static_cast<float>(g_markerSdfW) / static_cast<float>(g_markerSdfH);
        }

        // Interpret markSize as "half-height" in world units; width follows aspect.
        float halfH = markSize * 2.2f;
        float halfW = halfH * texAspect;

        glm::vec2 bl = rotXZ(-halfW, -halfH);
        glm::vec2 br = rotXZ( halfW, -halfH);
        glm::vec2 tr = rotXZ( halfW,  halfH);
        glm::vec2 tl = rotXZ(-halfW,  halfH);

        // Two triangles: (bl, br, tr) and (bl, tr, tl)
        std::array<float, 6 * 5> markerVerts{};
        int out = 0;
        auto emit = [&](const glm::vec2& p, float u, float v) {
            markerVerts[out++] = markerX + p.x;
            markerVerts[out++] = hy;
            markerVerts[out++] = markerZ + p.y;
            markerVerts[out++] = u;
            markerVerts[out++] = v;
        };

        emit(bl, 0.0f, 0.0f);
        emit(br, 1.0f, 0.0f);
        emit(tr, 1.0f, 1.0f);

        emit(bl, 0.0f, 0.0f);
        emit(tr, 1.0f, 1.0f);
        emit(tl, 0.0f, 1.0f);

        glBindVertexArray(g_miniVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(markerVerts), markerVerts.data());

        glUseProgram(markerSdfProgram);
        GLint locMvp = glGetUniformLocation(markerSdfProgram, "uMVP");
        GLint locColor = glGetUniformLocation(markerSdfProgram, "uColor");
        GLint locSdf = glGetUniformLocation(markerSdfProgram, "uSDF");
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, glm::value_ptr(mvpMini));
        glUniform3f(locColor, 1.0f, 0.1f, 0.1f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_markerSdfTex);
        glUniform1i(locSdf, 0);

        GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
        if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

        GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
        if (!blendWasEnabled) glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (!blendWasEnabled) glDisable(GL_BLEND);
        if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // Render chunk borders when in full map mode
    if (showFullMap) {
        // Calculate chunk size in world units
        float chunkWorldSize = (Config::World::CHUNK_SIZE - 1) * Config::World::VERTEX_SPACING;
        
        // Determine visible chunk range
        int minChunkX = static_cast<int>(std::floor((cx - worldRadius) / chunkWorldSize));
        int maxChunkX = static_cast<int>(std::ceil((cx + worldRadius) / chunkWorldSize));
        int minChunkZ = static_cast<int>(std::floor((cz - worldRadius) / chunkWorldSize));
        int maxChunkZ = static_cast<int>(std::ceil((cz + worldRadius) / chunkWorldSize));
        
        std::vector<float> borderVerts;
        float borderY = Config::UI::Map::CHUNK_BORDER_HEIGHT;
        
        // Generate vertical lines (along X axis)
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            float worldX = chunkX * chunkWorldSize;
            borderVerts.push_back(worldX);
            borderVerts.push_back(borderY);
            borderVerts.push_back(cz - worldRadius - chunkWorldSize);
            
            borderVerts.push_back(worldX);
            borderVerts.push_back(borderY);
            borderVerts.push_back(cz + worldRadius + chunkWorldSize);
        }
        
        // Generate horizontal lines (along Z axis)
        for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ) {
            float worldZ = chunkZ * chunkWorldSize;
            borderVerts.push_back(cx - worldRadius - chunkWorldSize);
            borderVerts.push_back(borderY);
            borderVerts.push_back(worldZ);
            
            borderVerts.push_back(cx + worldRadius + chunkWorldSize);
            borderVerts.push_back(borderY);
            borderVerts.push_back(worldZ);
        }
        
        // Render chunk borders
        if (!borderVerts.empty()) {
            glBindVertexArray(g_chunkBorderVAO);
            glBindBuffer(GL_ARRAY_BUFFER, g_chunkBorderVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, borderVerts.size() * sizeof(float), borderVerts.data());
            
            glUseProgram(solidColorProgram);
            GLint locBorder = glGetUniformLocation(solidColorProgram, "uMVP");
            GLint colorLocBorder = glGetUniformLocation(solidColorProgram, "uColor");
            glUniformMatrix4fv(locBorder, 1, GL_FALSE, glm::value_ptr(mvpMini));
            glUniform3f(colorLocBorder, Config::UI::Map::CHUNK_BORDER_R, Config::UI::Map::CHUNK_BORDER_G, Config::UI::Map::CHUNK_BORDER_B);
            
            glDrawArrays(GL_LINES, 0, borderVerts.size() / 3);
            glBindVertexArray(0);
            glUseProgram(0);
        }
    }

    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glDisable(GL_SCISSOR_TEST);
    glViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
}

void CleanupMap() {
    if (g_miniVBO) glDeleteBuffers(1, &g_miniVBO);
    if (g_miniVAO) glDeleteVertexArrays(1, &g_miniVAO);
    if (g_chunkBorderVBO) glDeleteBuffers(1, &g_chunkBorderVBO);
    if (g_chunkBorderVAO) glDeleteVertexArrays(1, &g_chunkBorderVAO);
    if (g_markerSdfTex) glDeleteTextures(1, &g_markerSdfTex);
    g_miniVBO = 0;
    g_miniVAO = 0;
    g_chunkBorderVBO = 0;
    g_chunkBorderVAO = 0;
    g_markerSdfTex = 0;
    g_markerSdfW = 0;
    g_markerSdfH = 0;
}
