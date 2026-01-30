#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Building shape representation
struct BuildingShape {
    std::vector<glm::vec2> points;  // 2D polygon points (local coordinates, relative to center)
    float height;                    // Building height
};

// Generate buildings on a grid for a chunk/region in world coordinates.
// Modifies the grid in place to mark building locations with value 4.
// Returns a vector of BuildingShape objects representing the buildings generated.
std::vector<BuildingShape> generate_buildings_grid(std::vector<std::vector<int>>& grid,
                                                    int chunk_x, int chunk_y,
                                                    int chunk_size = 256,
                                                    int padding = 64,
                                                    float density = 1.0f,
                                                    int seed = 42);
