#include "map.h"
#include <core/params/params.h>
#include <core/resources.h>

#include <array>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <world/terrain/terrain.h>
#include <core/params/params.h>

#include <stb_image.h>

static GLuint g_miniVAO = 0;
static GLuint g_miniVBO = 0;
static GLuint g_borderVAO = 0;
static GLuint g_borderVBO = 0;
static bool g_mapVisible = false;
static glm::vec2 g_mapOffset(0.0f, 0.0f);
static float g_mapZoom = 1.0f;
static GLuint g_markerSdfTex = 0;
static int g_markerSdfW = 0;
static int g_markerSdfH = 0;

struct ChunkMapDrawCache {
    GLuint vao = 0;
    GLuint vbo = 0;

    int highwayFirst = 0;
    int highwayCount = 0;
    int roadFirst = 0;
    int roadCount = 0;
    int streetFirst = 0;
    int streetCount = 0;
    int buildingFirst = 0;
    int buildingCount = 0;

    std::uint64_t lastUsedFrame = 0;
};

static std::unordered_map<long long, ChunkMapDrawCache> g_chunkDrawCache;
static std::uint64_t g_mapFrameCounter = 0;
static constexpr size_t MAX_CHUNK_DRAW_CACHE = 256;

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

    // Map border (solid quads in clip space; updated per viewport size)
    glGenVertexArrays(1, &g_borderVAO);
    glGenBuffers(1, &g_borderVBO);
    glBindVertexArray(g_borderVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_borderVBO);
    // 4 edges * 2 triangles * 3 verts = 24 vertices, each 3 floats
    glBufferData(GL_ARRAY_BUFFER, 24 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Load SDF marker texture.
    g_markerSdfTex = LoadSdfTextureR8(Resources::Images::UI::MARKER, g_markerSdfW, g_markerSdfH);
    if (!g_markerSdfTex) {
        // Non-fatal: marker will simply not render.
        // (Avoid logging spam here; InitMap is called once.)
    }
    // Initialize map zoom from params (fall back to compile-time config)
    {
        const json& uj = CoreParams::GetUiMapParams();
        g_mapZoom = uj.value("zoom_default", static_cast<float>(1.0f));
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
    const json& uj = CoreParams::GetUiMapParams();
    const float zmin = uj.value("zoom_min", static_cast<float>(0.5f));
    const float zmax = uj.value("zoom_max", static_cast<float>(5.0f));
    if (g_mapZoom < zmin) g_mapZoom = zmin;
    if (g_mapZoom > zmax) g_mapZoom = zmax;
}

float GetMapZoom() {
    return g_mapZoom;
}

void RenderMap(const glm::vec3& cameraPos, const glm::vec3& cameraFront, const glm::mat4& proj, int windowW, int windowH,
                   GLuint solidColorProgram,
                   GLuint markerSdfProgram,
                   GLuint terrainProgram,
                   GLuint highwaysProgram,
                   GLuint roadsProgram,
                   GLuint streetsProgram,
                   GLuint buildingsProgram,
                   bool debugDrawCullingSemicircle,
                   bool showFullMap,
                   const glm::vec2& mapOffset) {
    // Use internal offset instead of parameter (parameter kept for API compatibility)
    (void)mapOffset; // Suppress unused parameter warning
    
    // Save previous viewport
    GLint oldVp[4];
    glGetIntegerv(GL_VIEWPORT, oldVp);

    int miniX, miniY, miniW, miniH;
    float worldRadius;
    
    const json& uj = CoreParams::GetUiMapParams();

    // Player facing yaw (used to rotate the minimap background while keeping the marker locked).
    // Project facing direction onto XZ plane.
    const glm::vec2 f2 = SafeNormalize2(glm::vec2(cameraFront.x, cameraFront.z), glm::vec2(0.0f, 1.0f));
    // yaw = angle from +Z toward +X
    const float yaw = std::atan2(f2.x, f2.y);

    // Match 3D world colors (see world/terrain/terrain.cpp RenderTerrain).
    const auto& hwyParams = CoreParams::GetHighwayParams();
    const glm::vec3 highwayColor(
        hwyParams.value("color_r", 255) / 255.0f,
        hwyParams.value("color_g", 0) / 255.0f,
        hwyParams.value("color_b", 0) / 255.0f
    );
    const auto& roadParams = CoreParams::GetRoadParams();
    const glm::vec3 roadColor(
        roadParams.value("color_r", 255) / 255.0f,
        roadParams.value("color_g", 255) / 255.0f,
        roadParams.value("color_b", 0) / 255.0f
    );
    const auto& streetParams = CoreParams::GetStreetParams();
    const glm::vec3 streetColor(
        streetParams.value("color_r", 0) / 255.0f,
        streetParams.value("color_g", 0) / 255.0f,
        streetParams.value("color_b", 255) / 255.0f
    );
    const auto& buildingParams = CoreParams::GetBuildingParams();
    const glm::vec3 buildingColor(
        buildingParams.value("color_r", 180) / 255.0f,
        buildingParams.value("color_g", 180) / 255.0f,
        buildingParams.value("color_b", 180) / 255.0f
    );

    if (showFullMap) {
        // Full screen map mode
        miniX = 0;
        miniY = 0;
        miniW = windowW;
        miniH = windowH;
        worldRadius = (uj.value("world_radius", static_cast<float>(40.0f)) *
                   uj.value("map_world_radius_multiplier", static_cast<float>(3.0f))) / g_mapZoom;
    } else {
        // Small map in corner
        const int defaultSize = uj.value("size", static_cast<int>(220));
        const int margin = uj.value("margin", static_cast<int>(10));
        miniX = windowW - defaultSize - margin;
        miniY = windowH - defaultSize - margin;
        miniW = defaultSize;
        miniH = defaultSize;
        const float minimapRadiusMul = uj.value("minimap_world_radius_multiplier", static_cast<float>(1.5f));
        worldRadius = (uj.value("world_radius", static_cast<float>(40.0f)) * minimapRadiusMul) / g_mapZoom; // Apply zoom to minimap too
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
    glm::vec3 eye(cx, uj.value("camera_height", static_cast<float>(200.0f)), cz);
    glm::vec3 center(cx, 0.0f, cz);
    glm::vec3 up(0.0f, 0.0f, -1.0f);
    glm::mat4 viewMini = glm::lookAt(eye, center, up);

    glViewport(miniX, miniY, miniW, miniH);

    // Render map directly from chunk road-grid data (no top-down world geometry rendering)
    (void)terrainProgram;
    (void)highwaysProgram;
    (void)roadsProgram;
    (void)streetsProgram;
    (void)buildingsProgram;

    // Base MVP used for HUD elements that should not rotate (e.g., player marker in minimap).
    const glm::mat4 mvpMiniBase = projMini * viewMini;

    // Map MVP used by layer shaders (world-space -> clip-space in the minimap camera).
    // For minimap: rotate the map background so player forward is "up".
    glm::mat4 mvpMini = mvpMiniBase;
    if (!showFullMap) {
        const glm::mat4 t0 = glm::translate(glm::mat4(1.0f), glm::vec3(cx, 0.0f, cz));
        // The minimap camera basis (top-down viewMini + ortho) results in a 180° flip
        // relative to our yaw convention. Apply a +pi offset so the minimap shows
        // the world in front of the player above the marker.
        const float minimapYaw = -yaw + glm::pi<float>();
        const glm::mat4 r = glm::rotate(glm::mat4(1.0f), minimapYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 t1 = glm::translate(glm::mat4(1.0f), glm::vec3(-cx, 0.0f, -cz));
        const glm::mat4 modelRotateAboutCenter = t0 * r * t1;
        mvpMini = mvpMiniBase * modelRotateAboutCenter;
    }

    auto emitQuadXZ = [](std::vector<float>& out, float x0, float z0, float x1, float z1) {
        const float y = 0.0f;
        // Two triangles: (x0,z0)-(x0,z1)-(x1,z0) and (x1,z0)-(x0,z1)-(x1,z1)
        out.push_back(x0); out.push_back(y); out.push_back(z0);
        out.push_back(x0); out.push_back(y); out.push_back(z1);
        out.push_back(x1); out.push_back(y); out.push_back(z0);

        out.push_back(x1); out.push_back(y); out.push_back(z0);
        out.push_back(x0); out.push_back(y); out.push_back(z1);
        out.push_back(x1); out.push_back(y); out.push_back(z1);
    };

    const json& wj = CoreParams::GetWorldParams();
    const float spacing = wj.value("vertex_spacing", static_cast<float>(0.5f));
    const int chunkCfgSize = wj.value("chunk_size", static_cast<int>(128));

    auto buildChunkCache = [&](int chunkCx, int chunkCz) -> bool {
        const std::vector<std::vector<int>>* gridPtr = nullptr;
        if (!GetChunkRoadGrid(chunkCx, chunkCz, gridPtr) || !gridPtr) return false;
        const auto& grid = *gridPtr;
        if (grid.empty() || grid[0].empty()) return false;

        const int gridSizeZ = static_cast<int>(grid.size());
        const int gridSizeX = static_cast<int>(grid[0].size());
        if (gridSizeX < 2 || gridSizeZ < 2) return false;

        std::vector<float> vHighway;
        std::vector<float> vRoad;
        std::vector<float> vStreet;
        std::vector<float> vBuilding;

        const float chunkOriginX = (static_cast<float>(chunkCx) * static_cast<float>(chunkCfgSize - 1)) * spacing;
        const float chunkOriginZ = (static_cast<float>(chunkCz) * static_cast<float>(chunkCfgSize - 1)) * spacing;

        for (int z = 0; z < gridSizeZ - 1; ++z) {
            for (int x = 0; x < gridSizeX - 1; ++x) {
                const int t = grid[z][x];
                if (t == TERRAIN) continue;

                const float x0 = chunkOriginX + static_cast<float>(x) * spacing;
                const float z0 = chunkOriginZ + static_cast<float>(z) * spacing;
                const float x1 = x0 + spacing;
                const float z1 = z0 + spacing;

                switch (t) {
                    case HIGHWAY:  emitQuadXZ(vHighway, x0, z0, x1, z1); break;
                    case ROAD:     emitQuadXZ(vRoad, x0, z0, x1, z1); break;
                    case STREET:   emitQuadXZ(vStreet, x0, z0, x1, z1); break;
                    case BUILDING: emitQuadXZ(vBuilding, x0, z0, x1, z1); break;
                    default: break;
                }
            }
        }

        std::vector<float> vertices;
        vertices.reserve(vHighway.size() + vRoad.size() + vStreet.size() + vBuilding.size());

        ChunkMapDrawCache cache;
        cache.highwayFirst = 0;
        cache.highwayCount = static_cast<int>(vHighway.size() / 3);
        vertices.insert(vertices.end(), vHighway.begin(), vHighway.end());

        cache.roadFirst = cache.highwayFirst + cache.highwayCount;
        cache.roadCount = static_cast<int>(vRoad.size() / 3);
        vertices.insert(vertices.end(), vRoad.begin(), vRoad.end());

        cache.streetFirst = cache.roadFirst + cache.roadCount;
        cache.streetCount = static_cast<int>(vStreet.size() / 3);
        vertices.insert(vertices.end(), vStreet.begin(), vStreet.end());

        cache.buildingFirst = cache.streetFirst + cache.streetCount;
        cache.buildingCount = static_cast<int>(vBuilding.size() / 3);
        vertices.insert(vertices.end(), vBuilding.begin(), vBuilding.end());

        const long long k = keyFor(chunkCx, chunkCz);
        auto& dst = g_chunkDrawCache[k];

        if (dst.vao) glDeleteVertexArrays(1, &dst.vao);
        if (dst.vbo) glDeleteBuffers(1, &dst.vbo);
        dst = cache;

        if (vertices.empty()) {
            dst.lastUsedFrame = g_mapFrameCounter;
            return true;
        }

        glGenVertexArrays(1, &dst.vao);
        glGenBuffers(1, &dst.vbo);
        glBindVertexArray(dst.vao);
        glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        dst.lastUsedFrame = g_mapFrameCounter;
        return true;
    };

    ++g_mapFrameCounter;

    // Determine visible chunk range (use worldRadius in Z; X uses aspect-corrected radius)
    const float chunkWorldSize = (chunkCfgSize - 1) * spacing;
    const int minChunkX = static_cast<int>(std::floor((cx - worldRadiusX) / chunkWorldSize));
    const int maxChunkX = static_cast<int>(std::ceil((cx + worldRadiusX) / chunkWorldSize));
    const int minChunkZ = static_cast<int>(std::floor((cz - worldRadiusY) / chunkWorldSize));
    const int maxChunkZ = static_cast<int>(std::ceil((cz + worldRadiusY) / chunkWorldSize));

    // Track the rectangular bounds of chunks that actually exist / can be fetched.
    bool haveChunkBounds = false;
    int presentMinCx = 0;
    int presentMaxCx = 0;
    int presentMinCz = 0;
    int presentMaxCz = 0;

    // Soft-prune cache if it grows too large (simple LRU by lastUsedFrame)
    if (g_chunkDrawCache.size() > MAX_CHUNK_DRAW_CACHE) {
        long long oldestKey = 0;
        std::uint64_t oldestFrame = UINT64_MAX;
        for (const auto& kv : g_chunkDrawCache) {
            if (kv.second.lastUsedFrame < oldestFrame) {
                oldestFrame = kv.second.lastUsedFrame;
                oldestKey = kv.first;
            }
        }
        auto it = g_chunkDrawCache.find(oldestKey);
        if (it != g_chunkDrawCache.end()) {
            if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
            if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
            g_chunkDrawCache.erase(it);
        }
    }

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);

    for (int chunkCz = minChunkZ; chunkCz <= maxChunkZ; ++chunkCz) {
        for (int chunkCx = minChunkX; chunkCx <= maxChunkX; ++chunkCx) {
            const long long k = keyFor(chunkCx, chunkCz);
            auto it = g_chunkDrawCache.find(k);
            if (it == g_chunkDrawCache.end()) {
                if (!buildChunkCache(chunkCx, chunkCz)) {
                    continue;
                }
                it = g_chunkDrawCache.find(k);
                if (it == g_chunkDrawCache.end()) continue;
            }

            // This chunk exists (GetChunkRoadGrid succeeded at least once) and is part of the map area.
            if (!haveChunkBounds) {
                haveChunkBounds = true;
                presentMinCx = presentMaxCx = chunkCx;
                presentMinCz = presentMaxCz = chunkCz;
            } else {
                presentMinCx = std::min(presentMinCx, chunkCx);
                presentMaxCx = std::max(presentMaxCx, chunkCx);
                presentMinCz = std::min(presentMinCz, chunkCz);
                presentMaxCz = std::max(presentMaxCz, chunkCz);
            }

            it->second.lastUsedFrame = g_mapFrameCounter;
            if (!it->second.vao) continue;
            glBindVertexArray(it->second.vao);

            if (it->second.highwayCount > 0 && highwaysProgram) {
                glUseProgram(highwaysProgram);
                const GLint loc = glGetUniformLocation(highwaysProgram, "uMVP");
                const GLint colorLoc = glGetUniformLocation(highwaysProgram, "uColor");
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpMini));
                if (colorLoc >= 0) glUniform3f(colorLoc, highwayColor.r, highwayColor.g, highwayColor.b);
                glDrawArrays(GL_TRIANGLES, it->second.highwayFirst, it->second.highwayCount);
            }
            if (it->second.roadCount > 0 && roadsProgram) {
                glUseProgram(roadsProgram);
                const GLint loc = glGetUniformLocation(roadsProgram, "uMVP");
                const GLint colorLoc = glGetUniformLocation(roadsProgram, "uColor");
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpMini));
                if (colorLoc >= 0) glUniform3f(colorLoc, roadColor.r, roadColor.g, roadColor.b);
                glDrawArrays(GL_TRIANGLES, it->second.roadFirst, it->second.roadCount);
            }
            if (it->second.streetCount > 0 && streetsProgram) {
                glUseProgram(streetsProgram);
                const GLint loc = glGetUniformLocation(streetsProgram, "uMVP");
                const GLint colorLoc = glGetUniformLocation(streetsProgram, "uColor");
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpMini));
                if (colorLoc >= 0) glUniform3f(colorLoc, streetColor.r, streetColor.g, streetColor.b);
                glDrawArrays(GL_TRIANGLES, it->second.streetFirst, it->second.streetCount);
            }
            if (it->second.buildingCount > 0 && buildingsProgram) {
                glUseProgram(buildingsProgram);
                const GLint loc = glGetUniformLocation(buildingsProgram, "uMVP");
                const GLint colorLoc = glGetUniformLocation(buildingsProgram, "uColor");
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpMini));
                if (colorLoc >= 0) glUniform3f(colorLoc, buildingColor.r, buildingColor.g, buildingColor.b);
                glDrawArrays(GL_TRIANGLES, it->second.buildingFirst, it->second.buildingCount);
            }

            glBindVertexArray(0);
        }
    }

    glUseProgram(0);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);

    // ── Debug: draw camera frustum indicator ─────────────────────────
    // Show the camera view frustum in the XZ plane as a wedge (two edge rays)
    // plus a forward direction ray. This matches the frustum-based preprocessing
    // used to decide whether to draw expensive meshes (e.g., buildings).
    if (debugDrawCullingSemicircle && solidColorProgram && g_borderVAO && g_borderVBO) {
        GLboolean depthEnabledBefore = glIsEnabled(GL_DEPTH_TEST);
        if (depthEnabledBefore) glDisable(GL_DEPTH_TEST);

        const glm::vec2 f2dbg = SafeNormalize2(glm::vec2(cameraFront.x, cameraFront.z), glm::vec2(0.0f, 1.0f));
        const glm::vec2 right2 = SafeNormalize2(glm::vec2(f2dbg.y, -f2dbg.x), glm::vec2(1.0f, 0.0f));

        // For a standard perspective projection, tan(fovX/2) == 1 / proj[0][0].
        const float p00 = proj[0][0];
        const float tanHalfFovX = (std::abs(p00) > 1e-8f) ? (1.0f / p00) : 1.0f;

        const glm::vec2 leftDir = SafeNormalize2(f2dbg - right2 * tanHalfFovX, f2dbg);
        const glm::vec2 rightDir = SafeNormalize2(f2dbg + right2 * tanHalfFovX, f2dbg);
        const float r = std::max(worldRadiusX, worldRadiusY) * 1.1f;

        const float camX = cameraPos.x;
        const float camZ = cameraPos.z;

        // 3 lines (6 vertices): left edge + right edge + forward direction.
        float lineVerts[6 * 3] = {
            camX, 0.0f, camZ,
            camX + leftDir.x * r, 0.0f, camZ + leftDir.y * r,

            camX, 0.0f, camZ,
            camX + rightDir.x * r, 0.0f, camZ + rightDir.y * r,

            camX, 0.0f, camZ,
            camX + f2dbg.x * r, 0.0f, camZ + f2dbg.y * r,
        };

        glBindVertexArray(g_borderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_borderVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lineVerts), lineVerts);

        glUseProgram(solidColorProgram);
        const GLint loc = glGetUniformLocation(solidColorProgram, "uMVP");
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpMini));
        glDrawArrays(GL_LINES, 0, 6);
        glBindVertexArray(0);
        glUseProgram(0);

        if (depthEnabledBefore) glEnable(GL_DEPTH_TEST);
    }

    // Shared MVP for HUD map rendering (marker + borders).

    // Player marker: rounded-bottom triangle pointing in facing direction
    // Use minimap marker size if small map, full map marker size if full screen
    float markerSizeRatio = showFullMap ? uj.value("marker_size_map", static_cast<float>(0.02f)) :
                                           uj.value("marker_size_minimap", static_cast<float>(0.03f));
    float markSize = worldRadius * markerSizeRatio;
    // Use original camera position for marker (not offset)
    float markerX = cameraPos.x;
    float markerZ = cameraPos.z;
    float hy = SampleTerrainHeight(markerX, markerZ) + uj.value("marker_height_offset", static_cast<float>(50.0f));

    // For full map we keep the marker pointing in the facing direction.
    // For minimap we lock marker orientation and rotate the map background instead.
    // The SDF marker art is authored pointing "down" at zero rotation, so flip it 180°
    // to make the locked minimap marker point "up".
    const float markerYaw = showFullMap ? yaw : glm::pi<float>();
    const float c = std::cos(markerYaw);
    const float sn = std::sin(markerYaw);

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
        const glm::mat4& mvpMarker = showFullMap ? mvpMini : mvpMiniBase;
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, glm::value_ptr(mvpMarker));
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

    // Map border: full-map only, and sized to the chunk area (not the whole window)
    if (showFullMap && haveChunkBounds && solidColorProgram && g_borderVAO) {
        GLboolean depthEnabledBefore = glIsEnabled(GL_DEPTH_TEST);
        if (depthEnabledBefore) glDisable(GL_DEPTH_TEST);

        // Build a border with a fixed pixel thickness.
        // Using quads avoids line rasterization edge cases.
        const float thicknessPx = 4.0f;
        const float tx = (miniW > 0) ? (2.0f * thicknessPx / static_cast<float>(miniW)) : 0.01f;
        const float ty = (miniH > 0) ? (2.0f * thicknessPx / static_cast<float>(miniH)) : 0.01f;

        // Compute the chunk rect in clip space by transforming the chunk-world bounds through the map MVP.
        const float xMinW = static_cast<float>(presentMinCx) * chunkWorldSize;
        const float xMaxW = static_cast<float>(presentMaxCx + 1) * chunkWorldSize;
        const float zMinW = static_cast<float>(presentMinCz) * chunkWorldSize;
        const float zMaxW = static_cast<float>(presentMaxCz + 1) * chunkWorldSize;

        const glm::vec4 c00 = mvpMini * glm::vec4(xMinW, 0.0f, zMinW, 1.0f);
        const glm::vec4 c10 = mvpMini * glm::vec4(xMaxW, 0.0f, zMinW, 1.0f);
        const glm::vec4 c11 = mvpMini * glm::vec4(xMaxW, 0.0f, zMaxW, 1.0f);
        const glm::vec4 c01 = mvpMini * glm::vec4(xMinW, 0.0f, zMaxW, 1.0f);

        const float ndcMinX = std::min(std::min(c00.x, c10.x), std::min(c11.x, c01.x));
        const float ndcMaxX = std::max(std::max(c00.x, c10.x), std::max(c11.x, c01.x));
        const float ndcMinY = std::min(std::min(c00.y, c10.y), std::min(c11.y, c01.y));
        const float ndcMaxY = std::max(std::max(c00.y, c10.y), std::max(c11.y, c01.y));

        // Outer rect expands outward by half thickness; inner rect shrinks inward by half thickness.
        const float ox0 = ndcMinX - tx * 0.5f;
        const float ox1 = ndcMaxX + tx * 0.5f;
        const float oy0 = ndcMinY - ty * 0.5f;
        const float oy1 = ndcMaxY + ty * 0.5f;

        const float ix0 = ndcMinX + tx * 0.5f;
        const float ix1 = ndcMaxX - tx * 0.5f;
        const float iy0 = ndcMinY + ty * 0.5f;
        const float iy1 = ndcMaxY - ty * 0.5f;

        if (!(ix0 < ix1 && iy0 < iy1)) {
            if (depthEnabledBefore) glEnable(GL_DEPTH_TEST);
            // Skip border if the chunk rect is too small / degenerate.
            glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
            glDisable(GL_SCISSOR_TEST);
            glViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
            return;
        }

        auto emitTri = [](float* dst, int& idx, float x, float y) {
            dst[idx++] = x;
            dst[idx++] = y;
            dst[idx++] = 0.0f;
        };

        float borderVerts[24 * 3];
        int out = 0;
        // Top: (ox0, iy1)-(ox1, iy1)-(ox1, oy1)-(ox0, oy1)
        emitTri(borderVerts, out, ox0, iy1);
        emitTri(borderVerts, out, ox1, iy1);
        emitTri(borderVerts, out, ox1, oy1);
        emitTri(borderVerts, out, ox0, iy1);
        emitTri(borderVerts, out, ox1, oy1);
        emitTri(borderVerts, out, ox0, oy1);
        // Bottom: (ox0, oy0)-(ox1, oy0)-(ox1, iy0)-(ox0, iy0)
        emitTri(borderVerts, out, ox0, oy0);
        emitTri(borderVerts, out, ox1, oy0);
        emitTri(borderVerts, out, ox1, iy0);
        emitTri(borderVerts, out, ox0, oy0);
        emitTri(borderVerts, out, ox1, iy0);
        emitTri(borderVerts, out, ox0, iy0);
        // Left: (ox0, iy0)-(ix0, iy0)-(ix0, iy1)-(ox0, iy1)
        emitTri(borderVerts, out, ox0, iy0);
        emitTri(borderVerts, out, ix0, iy0);
        emitTri(borderVerts, out, ix0, iy1);
        emitTri(borderVerts, out, ox0, iy0);
        emitTri(borderVerts, out, ix0, iy1);
        emitTri(borderVerts, out, ox0, iy1);
        // Right: (ix1, iy0)-(ox1, iy0)-(ox1, iy1)-(ix1, iy1)
        emitTri(borderVerts, out, ix1, iy0);
        emitTri(borderVerts, out, ox1, iy0);
        emitTri(borderVerts, out, ox1, iy1);
        emitTri(borderVerts, out, ix1, iy0);
        emitTri(borderVerts, out, ox1, iy1);
        emitTri(borderVerts, out, ix1, iy1);

        glBindVertexArray(g_borderVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_borderVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(borderVerts), borderVerts);

        glUseProgram(solidColorProgram);
        const GLint loc = glGetUniformLocation(solidColorProgram, "uMVP");
        const glm::mat4 identity(1.0f);
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(identity));

        glDrawArrays(GL_TRIANGLES, 0, 24);
        glBindVertexArray(0);
        glUseProgram(0);

        if (depthEnabledBefore) glEnable(GL_DEPTH_TEST);
    }

    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glDisable(GL_SCISSOR_TEST);
    glViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
}

void CleanupMap() {
    for (auto& kv : g_chunkDrawCache) {
        if (kv.second.vbo) glDeleteBuffers(1, &kv.second.vbo);
        if (kv.second.vao) glDeleteVertexArrays(1, &kv.second.vao);
    }
    g_chunkDrawCache.clear();

    if (g_miniVBO) glDeleteBuffers(1, &g_miniVBO);
    if (g_miniVAO) glDeleteVertexArrays(1, &g_miniVAO);
    if (g_borderVBO) glDeleteBuffers(1, &g_borderVBO);
    if (g_borderVAO) glDeleteVertexArrays(1, &g_borderVAO);
    if (g_markerSdfTex) glDeleteTextures(1, &g_markerSdfTex);
    g_miniVBO = 0;
    g_miniVAO = 0;
    g_borderVBO = 0;
    g_borderVAO = 0;
    g_markerSdfTex = 0;
    g_markerSdfW = 0;
    g_markerSdfH = 0;
}
