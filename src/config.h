#pragma once

// ============================================================================
// PRPG (Procedural RPG) Configuration
// ============================================================================
// This file contains all configurable constants and parameters for the
// procedural generation system, rendering, and gameplay mechanics.
// ============================================================================

namespace Config {

// ============================================================================
// Window & Rendering Settings
// ============================================================================
namespace Window {
    constexpr int WIDTH = 1200;              // Window width in pixels
    constexpr int HEIGHT = 800;              // Window height in pixels
    constexpr int DEPTH_BUFFER_SIZE = 24;    // OpenGL depth buffer bit depth
}

// ============================================================================
// World Generation Settings
// ============================================================================
namespace World {
    constexpr int INITIAL_VIEW_RADIUS = 3;   // Number of chunks to generate initially around spawn
    constexpr int CHUNK_SIZE = 256;           // Number of vertices per chunk side
    constexpr float VERTEX_SPACING = 1.5f;   // World units between terrain vertices (larger = more planar terrain)
    constexpr int VIEW_RADIUS = 3;           // Chunks to keep loaded around player
    constexpr int PERLIN_SEED = 1337;        // Seed for terrain height noise generator
}

// ============================================================================
// Terrain Generation Parameters
// ============================================================================
namespace Terrain {
    constexpr float HEIGHT_AMPLITUDE = 0.6f; // Vertical scaling of terrain height (lower = flatter)
    constexpr float HEIGHT_FREQUENCY = 0.04f;// Perlin noise frequency for terrain (lower = smoother/gentler slopes)
}

// ============================================================================
// Highway Generation Parameters
// ============================================================================
namespace Highway {
    constexpr int NUM_HIGHWAYS = 2;          // Number of highways to generate per chunk
    constexpr int WORM_LENGTH = 1000;        // Number of segments in each highway polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between highway polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise (lower = straighter highways)
    constexpr int GRID_ANGLES = 4;           // Number of quantized angles (4 = 90° increments, 8 = 45°, etc.)
    constexpr float NOISE_STRENGTH = 1.0f;   // Amplitude of directional noise (higher = more variation)
    constexpr int PADDING = 8;               // Extra padding around chunk for seamless generation
    constexpr int SEED = 42;                 // Base seed for highway generation RNG
}

// ============================================================================
// Road Generation Parameters
// ============================================================================
namespace Road {
    constexpr int NUM_ROADS = 200;           // Number of roads to generate per chunk
    constexpr int WORM_LENGTH = 800;         // Number of segments in each road polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between road polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise
    constexpr int GRID_ANGLES = 4;           // Number of quantized angles
    constexpr float NOISE_STRENGTH = 1.0f;   // Amplitude of directional noise
    constexpr int PADDING = 8;               // Extra padding around chunk
    constexpr int SEED = 42;                 // Base seed for road generation RNG
    constexpr int SEARCH_RADIUS_CHUNKS = 1;  // Chunks to search when finding nearest road
    constexpr float INTERSECTION_RADIUS = 4.0f; // Distance to consider roads as intersecting (world units)
}

// ============================================================================
// Street Generation Parameters
// ============================================================================
namespace Street {
    constexpr int NUM_STREETS = 100;         // Number of streets to generate per chunk
    constexpr int WORM_LENGTH = 400;         // Number of segments in each street polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between street polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise
    constexpr int GRID_ANGLES = 4;           // Number of quantized angles
    constexpr float NOISE_STRENGTH = 1.0f;   // Amplitude of directional noise
    constexpr int PADDING = 8;               // Extra padding around chunk
    constexpr int SEED = 42;                 // Base seed for street generation RNG
    constexpr float ROAD_SEARCH_RADIUS = 4.0f; // Max distance to search for parent roads (world units)
}

// ============================================================================
// LLM (Language Model) Parameters
// ============================================================================
namespace LLM {
    constexpr int DEFAULT_GPU_LAYERS = -1;   // Number of model layers to offload to GPU (-1 = auto/all)
    constexpr int DEFAULT_CONTEXT_SIZE = 2048; // Context window size in tokens
    constexpr float MIN_P = 0.05f;           // Minimum probability threshold for token sampling
    constexpr float TEMPERATURE = 0.8f;      // Sampling temperature (higher = more random, lower = more deterministic)
    constexpr unsigned int DEFAULT_SEED = 0; // Default RNG seed for sampling (0 = random)
}

// ============================================================================
// Player Movement & Camera Parameters
// ============================================================================
namespace Player {
    constexpr float MOUSE_SENSITIVITY = 0.12f;  // Mouse look sensitivity multiplier
    constexpr float MOVE_SPEED = 2.5f;          // Player movement speed in units per second
    constexpr float FLY_SPEED = 3.0f;           // Vertical movement speed when flying (units/sec)
    constexpr float GRAVITY = -9.81f;           // Gravity acceleration (m/s²)
}

// ============================================================================
// Rendering Colors (RGB values 0.0-1.0)
// ============================================================================
namespace Colors {
    // Terrain color
    constexpr float TERRAIN_R = 0.3f;        // Terrain red component
    constexpr float TERRAIN_G = 0.8f;        // Terrain green component (green grass)
    constexpr float TERRAIN_B = 0.3f;        // Terrain blue component
    
    // Highway color
    constexpr float HIGHWAY_R = 0.6f;        // Highway red component
    constexpr float HIGHWAY_G = 0.6f;        // Highway green component (gray)
    constexpr float HIGHWAY_B = 0.6f;        // Highway blue component
    
    // Road color
    constexpr float ROAD_R = 0.8f;           // Road red component
    constexpr float ROAD_G = 0.8f;           // Road green component (light beige)
    constexpr float ROAD_B = 0.6f;           // Road blue component
    
    // Street color
    constexpr float STREET_R = 0.4f;         // Street red component
    constexpr float STREET_G = 0.4f;         // Street green component (dark gray)
    constexpr float STREET_B = 0.4f;         // Street blue component
}

// ============================================================================
// UI Parameters
// ============================================================================
namespace UI {
    // Debug Overlay
    namespace Debug {
        constexpr float PADDING_X = 10.0f;   // Horizontal padding from screen edge
        constexpr float PADDING_Y = 10.0f;   // Vertical padding from screen edge
        constexpr float LINE_HEIGHT = 30.0f; // Height of each debug text line
    }
    
    // Map (both corner minimap and full-screen map)
    namespace Map {
        constexpr int SIZE = 220;            // Small map view width/height in pixels
        constexpr int MARGIN = 10;           // Distance from screen edge in pixels
        constexpr float WORLD_RADIUS = 40.0f;// World units visible in small map view
        
        // Full-screen map settings
        constexpr float MAP_WORLD_RADIUS_MULTIPLIER = 3.0f; // How much larger the full map view is vs small corner view
        constexpr float MAP_SCROLL_SPEED = 50.0f;           // World units per second when scrolling map with WASD
        
        // Zoom settings
        constexpr float ZOOM_DEFAULT = 1.0f;     // Default zoom level
        constexpr float ZOOM_MIN = 0.5f;         // Minimum zoom (most zoomed out)
        constexpr float ZOOM_MAX = 5.0f;         // Maximum zoom (most zoomed in)
        constexpr float ZOOM_STEP = 0.2f;        // Zoom increment/decrement per keypress
        
        // Camera and rendering settings
        constexpr float CAMERA_HEIGHT = 200.0f;  // Height of top-down camera above terrain
        constexpr float MARKER_SIZE_RATIO = 0.02f; // Player marker size as ratio of world radius
        constexpr float MARKER_HEIGHT_OFFSET = 50.0f; // Height offset for player marker above terrain
        constexpr float CHUNK_BORDER_HEIGHT = 100.0f; // Height for chunk border lines
        
        // Colors (RGB, 0.0-1.0)
        constexpr float CHUNK_BORDER_R = 0.5f;   // Chunk border red component
        constexpr float CHUNK_BORDER_G = 0.5f;   // Chunk border green component
        constexpr float CHUNK_BORDER_B = 0.5f;   // Chunk border blue component
    }
}

} // namespace Config
