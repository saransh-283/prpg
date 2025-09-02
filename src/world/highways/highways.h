#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Generate highway polylines for a chunk/region in world coordinates.
// Returns a vector of polylines; each polyline is a vector of 2D points (x,z) in world space.
std::vector<std::vector<glm::vec2>> generate_highways_chunk_polylines(int chunk_x,
                                                                       int chunk_y,
                                                                       int chunk_size = 256,
                                                                       int padding = 64,
                                                                       int num_highways = 2,
                                                                       int worm_length = 1000,
                                                                       float step_size = 1.0f,
                                                                       float perlin_scale = 0.01f,
                                                                       int seed = 42,
                                                                       int grid_angles = 4,
                                                                       float noise_strength = 1.0f);
