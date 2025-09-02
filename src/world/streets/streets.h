#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Generate street polylines for a chunk/region in world coordinates.
// Streets branch from existing roads and highways.
// Returns a vector of polylines; each polyline is a vector of 2D points (x,z) in world space.
std::vector<std::vector<glm::vec2>> generate_streets_chunk_polylines(int chunk_x,
                                                                     int chunk_y,
                                                                     int chunk_size = 256,
                                                                     int padding = 64,
                                                                     int num_streets = 100,
                                                                     int worm_length = 400,
                                                                     float step_size = 1.0f,
                                                                     float perlin_scale = 0.01f,
                                                                     int seed = 42,
                                                                     int grid_angles = 4,
                                                                     float noise_strength = 1.0f,
                                                                     const std::vector<std::vector<glm::vec2>>& highways = {},
                                                                     const std::vector<std::vector<glm::vec2>>& roads = {});
