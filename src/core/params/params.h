#pragma once

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace CoreParams {
    // Returns the parsed NPC params JSON (may be null on failure).
    const json& GetNpcParams();
    // Returns the parsed window params JSON (may be null on failure).
    const json& GetWindowParams();
    // Other top-level namespace param accessors
    const json& GetWorldParams();
    const json& GetTerrainParams();
    const json& GetHighwayParams();
    const json& GetStreetParams();
    const json& GetRoadParams();
    const json& GetBuildingParams();
    const json& GetPlayerParams();
    const json& GetLLMParams();
    const json& GetCameraParams();

    // Rendering sub-namespaces
    const json& GetRenderingAmbientParams();
    const json& GetRenderingShadowsParams();
    const json& GetRenderingSunParams();
    const json& GetRenderingSkyboxParams();
    const json& GetRenderingCullingParams();

    // UI sub-namespaces
    const json& GetUiDebugParams();
    const json& GetUiMapParams();
}
