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
    constexpr int CHUNK_SIZE = 128;           // Number of vertices per chunk side
    constexpr float VERTEX_SPACING = 0.5f;   // World units between terrain vertices (larger = more planar terrain)
    constexpr int VIEW_RADIUS = 3;           // Chunks to keep loaded around player
    constexpr int PERLIN_SEED = 1337;        // Seed for terrain height noise generator
}

// ============================================================================
// Terrain Generation Parameters
// ============================================================================
namespace Terrain {
    constexpr float HEIGHT_AMPLITUDE = 0.6f; // Vertical scaling of terrain height (lower = flatter)
    constexpr float HEIGHT_FREQUENCY = 0.04f;// Perlin noise frequency for terrain (lower = smoother/gentler slopes)
    
    // Rendering color (RGB values 0-255)
    constexpr int COLOR_R = 76;              // Terrain red component
    constexpr int COLOR_G = 204;             // Terrain green component (green grass)
    constexpr int COLOR_B = 76;              // Terrain blue component
}

// ============================================================================
// Highway Generation Parameters
// ============================================================================
namespace Highway {
    constexpr int NUM_HIGHWAYS = 2;          // Number of highways to generate per chunk
    constexpr int WORM_LENGTH = 1000;        // Number of segments in each highway polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between highway polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise (lower = straighter highways)
    constexpr int GRID_ANGLES = 36;          // Number of quantized angles (36 = 10° increments)
    constexpr float NOISE_STRENGTH = 1.0f;   // Amplitude of directional noise (higher = more variation)

    // Curvature constraints
    // - Max turn: limits sharp corners.
    // - Min steps between turns: enforces long straight/bendy runs.
    // - Max turn per step: makes bends gradual (radians/step derived from degrees).
    constexpr float MAX_TURN_DEG = 20.0f;     // Highways: restrict turns to <= 20°
    constexpr int MIN_STEPS_BETWEEN_TURNS = 30; // (legacy) prefer STRAIGHT_*/BEND_* below
    constexpr float MAX_TURN_DEG_PER_STEP = 0.75f; // Highways: highest gradual bending

    // Global highway seeding (for seamless long highways across chunks)
    constexpr int GLOBAL_CELL_SPACING = 64;     // Larger spacing => fewer global highway seeds
    constexpr float GLOBAL_SEED_PROB = 0.006f;  // Probability per global cell to start a highway

    // Balance straight parts vs bend parts (in steps)
    constexpr int STRAIGHT_MIN_STEPS = 80;
    constexpr int STRAIGHT_MAX_STEPS = 140;
    constexpr int BEND_MIN_STEPS = 20;
    constexpr int BEND_MAX_STEPS = 40;
    constexpr int PADDING = 8;               // Extra padding around chunk for seamless generation
    constexpr int SEED = 42;                 // Base seed for highway generation RNG

    // Thickness (in grid-cell radius around the centerline).
    // Varies smoothly using Perlin noise so thickness doesn't change over short lengths.
    constexpr float THICKNESS_MIN = 2.5f;
    constexpr float THICKNESS_MAX = 4.5f;
    constexpr float THICKNESS_PERLIN_SCALE = 0.0025f; // Lower = longer, smoother changes
    constexpr float THICKNESS_SMOOTH_ALPHA = 0.03f;   // Exponential smoothing per step
    
    // Rendering color (RGB values 0-255)
    constexpr int COLOR_R = 255;             // Highway red component
    constexpr int COLOR_G = 0;             // Highway green component (gray)
    constexpr int COLOR_B = 0;             // Highway blue component
}

// ============================================================================
// Road Generation Parameters
// ============================================================================
namespace Road {
    constexpr int NUM_ROADS = 20;           // Number of roads to generate per chunk
    constexpr int WORM_LENGTH = 800;         // Number of segments in each road polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between road polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise
    constexpr int GRID_ANGLES = 18;          // Number of quantized angles (18 = 20° increments)
    constexpr float NOISE_STRENGTH = 10.0f;   // Amplitude of directional noise

    // Curvature constraints
    constexpr float MAX_TURN_DEG = 40.0f;     // Roads: restrict turns to <= 40°
    constexpr int MIN_STEPS_BETWEEN_TURNS = 15; // (legacy) prefer STRAIGHT_*/BEND_* below
    constexpr float MAX_TURN_DEG_PER_STEP = 2.25f; // Roads: medium gradual bending

    // Balance straight parts vs bend parts (in steps)
    constexpr int STRAIGHT_MIN_STEPS = 35;
    constexpr int STRAIGHT_MAX_STEPS = 70;
    constexpr int BEND_MIN_STEPS = 15;
    constexpr int BEND_MAX_STEPS = 30;
    constexpr int PADDING = 8;               // Extra padding around chunk
    constexpr int SEED = 42;                 // Base seed for road generation RNG
    constexpr int SEARCH_RADIUS_CHUNKS = 1;  // Chunks to search when finding nearest road
    constexpr float INTERSECTION_RADIUS = 4.0f; // Distance to consider roads as intersecting (world units)

    // Thickness (in grid-cell radius around the centerline).
    constexpr float THICKNESS_MIN = 1.0f;
    constexpr float THICKNESS_MAX = 2.0f;
    constexpr float THICKNESS_PERLIN_SCALE = 0.0030f;
    constexpr float THICKNESS_SMOOTH_ALPHA = 0.04f;
    
    // Rendering color (RGB values 0-255)
    constexpr int COLOR_R = 255;             // Road red component
    constexpr int COLOR_G = 255;             // Road green component (light beige)
    constexpr int COLOR_B = 0;             // Road blue component
}

// ============================================================================
// Street Generation Parameters
// ============================================================================
namespace Street {
    constexpr int NUM_STREETS = 20;         // Number of streets to generate per chunk
    constexpr int WORM_LENGTH = 400;         // Number of segments in each street polyline
    constexpr float STEP_SIZE = 1.0f;        // Distance between street polyline points (world units)
    constexpr float PERLIN_SCALE = 0.01f;    // Scale factor for direction noise
    constexpr int GRID_ANGLES = 12;          // Number of quantized angles (12 = 30° increments)
    constexpr float NOISE_STRENGTH = 1.0f;   // Amplitude of directional noise

    // Curvature constraints
    constexpr float MAX_TURN_DEG = 60.0f;     // Streets: restrict turns to <= 60°
    constexpr int MIN_STEPS_BETWEEN_TURNS = 6; // (legacy) prefer STRAIGHT_*/BEND_* below
    constexpr float MAX_TURN_DEG_PER_STEP = 6.0f; // Streets: lowest gradual bending (most agile)

    // Balance straight parts vs bend parts (in steps)
    constexpr int STRAIGHT_MIN_STEPS = 6;
    constexpr int STRAIGHT_MAX_STEPS = 18;
    constexpr int BEND_MIN_STEPS = 10;
    constexpr int BEND_MAX_STEPS = 22;
    constexpr int PADDING = 8;               // Extra padding around chunk
    constexpr int SEED = 42;                 // Base seed for street generation RNG
    constexpr float ROAD_SEARCH_RADIUS = 4.0f; // Max distance to search for parent roads (world units)

    // Thickness (in grid-cell radius around the centerline).
    constexpr float THICKNESS_MIN = 0.5f;
    constexpr float THICKNESS_MAX = 1.0f;
    constexpr float THICKNESS_PERLIN_SCALE = 0.0035f;
    constexpr float THICKNESS_SMOOTH_ALPHA = 0.05f;
    
    // Rendering color (RGB values 0-255)
    constexpr int COLOR_R = 0;             // Street red component
    constexpr int COLOR_G = 0;             // Street green component (dark gray)
    constexpr int COLOR_B = 255;             // Street blue component
}

// ============================================================================
// Building Generation Parameters
// ============================================================================
namespace Building {
    constexpr float DENSITY = 20.0f;         // Building density multiplier (higher = more buildings)
    constexpr int SEED = 42;                 // Base seed for building generation RNG
    constexpr int PADDING = 8;               // Extra padding around chunk
    
    // Size ranges by road type
    constexpr int HIGHWAY_MIN_SIZE = 15;     // Min building size near highways
    constexpr int HIGHWAY_MAX_SIZE = 30;     // Max building size near highways
    constexpr int ROAD_MIN_SIZE = 10;        // Min building size near roads
    constexpr int ROAD_MAX_SIZE = 20;        // Max building size near roads
    constexpr int STREET_MIN_SIZE = 6;       // Min building size near streets
    constexpr int STREET_MAX_SIZE = 12;      // Max building size near streets
    
    // Height ranges by road type (deterministic random)
    constexpr float HIGHWAY_MIN_HEIGHT = 20.0f; // Min building height near highways
    constexpr float HIGHWAY_MAX_HEIGHT = 50.0f; // Max building height near highways
    constexpr float ROAD_MIN_HEIGHT = 10.0f;    // Min building height near roads
    constexpr float ROAD_MAX_HEIGHT = 30.0f;    // Max building height near roads
    constexpr float STREET_MIN_HEIGHT = 5.0f;   // Min building height near streets
    constexpr float STREET_MAX_HEIGHT = 15.0f;  // Max building height near streets
    
    // Shape probabilities
    constexpr float RECTANGLE_PROBABILITY = 0.4f;  // 40% rectangles
    constexpr float L_SHAPE_PROBABILITY = 0.7f;    // 30% L-shapes (cumulative)
    constexpr float T_SHAPE_PROBABILITY = 1.0f;    // 30% T-shapes (cumulative)
    
    // Rendering color (RGB values 0-255)
    constexpr int COLOR_R = 180;             // Building red component
    constexpr int COLOR_G = 180;             // Building green component (light gray)
    constexpr int COLOR_B = 180;             // Building blue component
}

// ============================================================================
// LLM (Language Model) Parameters
// ============================================================================
namespace LLM {
    constexpr int DEFAULT_GPU_LAYERS = 16;      // Number of model layers to offload to GPU (-1 = auto/all)
    constexpr int DEFAULT_CONTEXT_SIZE = 8192;  // Context window size in tokens
    constexpr int BATCH_SIZE = 8192;            // Prompt batch size (tokens processed per batch)
    constexpr int MICRO_BATCH_SIZE = 512;       // Micro-batch size for prompt processing
    constexpr int CPU_THREADS = 16;             // Number of CPU threads for generation
    constexpr int BATCH_THREADS = 16;           // Number of CPU threads for batch processing
    constexpr float MIN_P = 0.05f;              // Minimum probability threshold for token sampling
    constexpr float TEMPERATURE = 0.8f;         // Sampling temperature (higher = more random, lower = more deterministic)
    constexpr unsigned int DEFAULT_SEED = 0;    // Default RNG seed for sampling (0 = random)

    // NPC name generation via LLM
    constexpr float NPC_NAME_RADIUS = 20.0f;        // Radius in world units to detect nearby NPCs (~5 sec walk)
    constexpr float NPC_NAME_BATCH_WINDOW = 2.0f;   // Seconds to collect NPCs before generating names
    constexpr int   NPC_NAME_HISTORY_SIZE = 15;      // Number of recent names kept to avoid duplicates
}

// ============================================================================
// Player Movement & Camera Parameters
// ============================================================================
namespace Player {
    constexpr float MOUSE_SENSITIVITY = 0.12f;  // Mouse look sensitivity multiplier
    constexpr float MOVE_SPEED = 15.0f;          // Player movement speed in units per second
    constexpr float GRAVITY = -9.81f;           // Gravity acceleration (m/s²)
    constexpr float COLLISION_RADIUS = 0.45f;   // Player horizontal collision radius (world units)

    // Player camera/eye height above the ground when standing.
    // This is the value used by grounding/collision, not just spawn.
    constexpr float EYE_HEIGHT = 2.0f;
}

namespace Camera {
    constexpr float NEAR_PLANE = 0.1f;          // Near clipping plane distance
    constexpr float FAR_PLANE = 1000.0f;        // Far clipping plane distance
    constexpr float FOV = 60.0f;                // Field of view in degrees
}

// ============================================================================
// Rendering Parameters
// ============================================================================
namespace Rendering {
    constexpr int SHADOW_MAP_RESOLUTION = 4096; // Shadow map texture resolution (higher = sharper shadows)
    constexpr float SHADOW_DISTANCE = 100.0f;   // Distance from camera to render shadows
    constexpr float SHADOW_BIAS = 0.0005f;      // Bias to reduce shadow acne

    // Ambient/indirect light (keeps unlit faces from going black)
    namespace Ambient {
        // Overall ambient intensity multiplier.
        constexpr float INTENSITY = 0.55f;

        // Extra baseline ambient added on top of hemisphere ambient.
        // Helps ensure walls never go near-black even if the hemisphere mix is dim.
        constexpr float MIN = 0.08f;
        // Simple hemisphere ambient colors.
        // Sky is slightly blue; ground is neutral/darker.
        constexpr float SKY_R = 0.62f;
        constexpr float SKY_G = 0.74f;
        constexpr float SKY_B = 0.92f;
        constexpr float GROUND_R = 0.26f;
        constexpr float GROUND_G = 0.26f;
        constexpr float GROUND_B = 0.28f;
    }

    // Shadow tuning
    namespace Shadows {
        // 1.0 = full shadowing, 0.0 = shadows disabled.
        constexpr float STRENGTH = 0.45f;
    }
    
    // Sun/Directional Light
    namespace Sun {
        // Direction is the light ray direction (from sun toward the world).
        // 10–11am-ish: not directly overhead (shallower elevation).
        constexpr float DIRECTION_X = -0.3f;    // Sun direction X component
        constexpr float DIRECTION_Y = -0.7f;    // Sun direction Y component (negative = from above)
        constexpr float DIRECTION_Z = -0.5f;    // Sun direction Z component
        constexpr float COLOR_R = 1.0f;         // Sun color red component
        constexpr float COLOR_G = 0.95f;        // Sun color green component (warm white)
        constexpr float COLOR_B = 0.8f;         // Sun color blue component
        constexpr float INTENSITY = 1.2f;       // Sun light intensity multiplier
    }
    
    // Skybox
    namespace Skybox {
        constexpr float TIME_OF_DAY = 0.5f;     // Time of day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
    }
}

// ============================================================================
// UI Parameters
// ============================================================================
namespace UI {
    // Debug Overlay
    namespace Debug {
        constexpr bool VISIBLE_BY_DEFAULT = true;
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
        
        // Marker settings (separate sizes for minimap and full map)
        constexpr float MARKER_SIZE_MINIMAP = 0.03f; // Minimap marker size as ratio of world radius (larger)
        constexpr float MARKER_SIZE_MAP = 0.02f;     // Full map marker size as ratio of world radius (smaller)
        constexpr float MARKER_HEIGHT_OFFSET = 50.0f; // Height offset for player marker above terrain
        constexpr float CHUNK_BORDER_HEIGHT = 100.0f; // Height for chunk border lines
        
        // Colors (RGB, 0.0-1.0)
        constexpr float CHUNK_BORDER_R = 0.5f;   // Chunk border red component
        constexpr float CHUNK_BORDER_G = 0.5f;   // Chunk border green component
        constexpr float CHUNK_BORDER_B = 0.5f;   // Chunk border blue component
    }
}

} // namespace Config
