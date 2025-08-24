#pragma once

#include <vector>
#include <cstdint>

// Generate Perlin roads as a grayscale single-channel canvas (values 0..255)
// Parameters are the same as the previous implementation in perlin_roads.cpp
std::vector<uint8_t> generate_perlin_roads(int canvas_size = 1000,
                                            int num_worms = 100,
                                            int worm_length = 500,
                                            float step_size = 1.0f,
                                            float perlin_scale = 0.01f,
                                            int seed = 2,
                                            int grid_angles = 4,
                                            float noise_strength = 1.0f,
                                            int road_width = 3);
