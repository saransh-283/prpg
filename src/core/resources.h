#pragma once

// ============================================================================
// PRPG Resource Paths
// ============================================================================
// This file contains all resource file paths used throughout the project:
// - Shader files (.vert, .frag)
// - Asset images (.png, .jpg)
// - Model files (.gltf, .obj, .fbx)
// - Font files (.ttf)
// ============================================================================

namespace Resources {

// ============================================================================
// Shader Paths
// ============================================================================
namespace Shaders {
    // Simple 3D rendering
    namespace Simple3D {
        constexpr const char* VERTEX = "src/assets/shaders/simple3d/simple3d.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/simple3d/simple3d.frag";
    }
    
    // Terrain rendering
    namespace Terrain {
        constexpr const char* VERTEX = "src/assets/shaders/terrain/terrain.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/terrain/terrain.frag";
    }
    
    // Roads rendering
    namespace Roads {
        constexpr const char* VERTEX = "src/assets/shaders/roads/roads.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/roads/roads.frag";
    }
    
    // Highways rendering
    namespace Highways {
        constexpr const char* VERTEX = "src/assets/shaders/highways/highways.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/highways/highways.frag";
    }
    
    // Streets rendering
    namespace Streets {
        constexpr const char* VERTEX = "src/assets/shaders/streets/streets.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/streets/streets.frag";
    }
    
    // Buildings rendering (if used)
    namespace Buildings {
        constexpr const char* VERTEX = "src/assets/shaders/buildings/buildings.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/buildings/buildings.frag";
    }
    
    // UI - Marker SDF
    namespace UI {
        namespace MarkerSDF {
            constexpr const char* VERTEX = "src/assets/shaders/ui/marker_sdf.vert";
            constexpr const char* FRAGMENT = "src/assets/shaders/ui/marker_sdf.frag";
        }
    }
    
    // Deferred rendering
    namespace Deferred {
        namespace Geometry {
            constexpr const char* VERTEX = "src/assets/shaders/deferred/geometry.vert";
            constexpr const char* FRAGMENT = "src/assets/shaders/deferred/geometry.frag";
        }
        
        namespace Lighting {
            constexpr const char* VERTEX = "src/assets/shaders/deferred/lighting.vert";
            constexpr const char* FRAGMENT = "src/assets/shaders/deferred/lighting.frag";
        }
        
        namespace Shadow {
            constexpr const char* VERTEX = "src/assets/shaders/shadow/shadow.vert";
            constexpr const char* FRAGMENT = "src/assets/shaders/shadow/shadow.frag";
        }
    }
    
    // Skybox rendering
    namespace Skybox {
        constexpr const char* VERTEX = "src/assets/shaders/skybox/skybox.vert";
        constexpr const char* FRAGMENT = "src/assets/shaders/skybox/skybox.frag";
    }
}

// ============================================================================
// Image/Asset Paths
// ============================================================================
namespace Images {
    namespace UI {
        constexpr const char* MARKER = "src/assets/images/marker.png";
    }
}

// ============================================================================
// Model Paths
// ============================================================================
namespace Models {
    // Add model paths as needed
    // Example:
    // constexpr const char* PLAYER_MODEL = "src/assets/models/player.gltf";

    // NPC base model
    constexpr const char* NPC_BASE_MODEL = "src/assets/objects/models/glb/pill.glb";
}

// ============================================================================
// Font Paths
// ============================================================================
namespace Fonts {
    // Add font paths as needed
    // Example:
    // constexpr const char* DEFAULT_FONT = "src/assets/fonts/arial.ttf";
}

} // namespace Resources
