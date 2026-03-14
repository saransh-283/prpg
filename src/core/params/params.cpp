#include "params.h"

#include <core/resources.h>
#include <utils/loadjson/loadjson.h>
#include <iostream>
#include <map>
#include <string>

namespace CoreParams {
    
// Helper to reduce duplication
static const json& LoadStaticJson(const char* path, const char* errmsgPrefix="CoreParams") {
    static std::map<std::string, json> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    json tmp = LoadJsonFile(path);
    if (tmp.is_null()) {
        std::cerr << errmsgPrefix << ": failed to load " << path << std::endl;
    }
    cache.emplace(path, std::move(tmp));
    return cache[path];
}

const json& GetWindowParams() { return LoadStaticJson("src/core/params/window.json"); }
const json& GetWorldParams() { return LoadStaticJson("src/core/params/world.json"); }
const json& GetTerrainParams() { return LoadStaticJson("src/core/params/terrain.json"); }
const json& GetHighwayParams() { return LoadStaticJson("src/core/params/highway.json"); }
const json& GetRoadParams() { return LoadStaticJson("src/core/params/road.json"); }
const json& GetStreetParams() { return LoadStaticJson("src/core/params/street.json"); }
const json& GetBuildingParams() { return LoadStaticJson("src/core/params/building.json"); }
const json& GetPlayerParams() { return LoadStaticJson("src/core/params/player.json"); }
const json& GetLLMParams() { return LoadStaticJson("src/core/params/llm.json"); }
const json& GetCameraParams() { return LoadStaticJson("src/core/params/camera.json"); }
const json& GetNpcParams() { return LoadStaticJson("src/core/params/npc.json"); }
const json& GetRenderingAmbientParams() { return LoadStaticJson("src/core/params/rendering/ambient.json"); }
const json& GetRenderingShadowsParams() { return LoadStaticJson("src/core/params/rendering/shadows.json"); }
const json& GetRenderingSunParams() { return LoadStaticJson("src/core/params/rendering/sun.json"); }
const json& GetRenderingSkyboxParams() { return LoadStaticJson("src/core/params/rendering/skybox.json"); }
const json& GetRenderingCullingParams() { return LoadStaticJson("src/core/params/rendering/culling.json"); }

const json& GetUiDebugParams() { return LoadStaticJson("src/core/params/ui/debug.json"); }
const json& GetUiMapParams() { return LoadStaticJson("src/core/params/ui/map.json"); }

// (Functions above use LoadStaticJson; duplicate loader implementations removed.)

} // namespace CoreParams
