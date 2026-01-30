#include "map.h"
#include "../../config.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <world/terrain/terrain.h>

static GLuint g_miniVAO = 0;
static GLuint g_miniVBO = 0;
static GLuint g_chunkBorderVAO = 0;
static GLuint g_chunkBorderVBO = 0;
static bool g_mapVisible = false;
static glm::vec2 g_mapOffset(0.0f, 0.0f);
static float g_mapZoom = Config::UI::Map::ZOOM_DEFAULT;

bool InitMap() {
    glGenVertexArrays(1, &g_miniVAO);
    glGenBuffers(1, &g_miniVBO);
    glBindVertexArray(g_miniVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
    glBufferData(GL_ARRAY_BUFFER, 18 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
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

void RenderMap(const glm::vec3& cameraPos, int windowW, int windowH,
                   GLuint markerProgram,
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

    // Player marker as small filled square
    float markSize = worldRadius * Config::UI::Map::MARKER_SIZE_RATIO;
    // Use original camera position for marker (not offset)
    float markerX = cameraPos.x;
    float markerZ = cameraPos.z;
    float hy = SampleTerrainHeight(markerX, markerZ) + Config::UI::Map::MARKER_HEIGHT_OFFSET;
    float s = markSize;
    float squareVerts[18] = {
        markerX - s, hy, markerZ - s,
        markerX + s, hy, markerZ - s,
        markerX + s, hy, markerZ + s,
        markerX - s, hy, markerZ - s,
        markerX + s, hy, markerZ + s,
        markerX - s, hy, markerZ + s
    };

    glBindVertexArray(g_miniVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(squareVerts), squareVerts);

    // bind marker shader and set uniform then draw triangles
    glUseProgram(markerProgram);
    GLint loc2 = glGetUniformLocation(markerProgram, "uMVP");
    GLint colorLoc2 = glGetUniformLocation(markerProgram, "uColor");
    glm::mat4 mvpMini = projMini * viewMini * glm::mat4(1.0f);
    glUniformMatrix4fv(loc2, 1, GL_FALSE, glm::value_ptr(mvpMini));
    glUniform3f(colorLoc2, 1.0f, 0.1f, 0.1f);

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);

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
            
            glUseProgram(markerProgram);
            GLint locBorder = glGetUniformLocation(markerProgram, "uMVP");
            GLint colorLocBorder = glGetUniformLocation(markerProgram, "uColor");
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
    g_miniVBO = 0;
    g_miniVAO = 0;
    g_chunkBorderVBO = 0;
    g_chunkBorderVAO = 0;
}
