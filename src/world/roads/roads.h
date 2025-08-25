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

// Chunk-aware variant: generate the roads for a single chunk specified by chunk coordinates.
// Returns a flattened canvas of size chunk_size x chunk_size (values 0..255).
std::vector<uint8_t> generate_perlin_roads_chunk(int chunk_x,
                                                  int chunk_y,
                                                  int chunk_size = 256,
                                                  int padding = 64,
                                                  int num_worms = 200,
                                                  int worm_length = 800,
                                                  float step_size = 1.0f,
                                                  float perlin_scale = 0.01f,
                                                  int seed = 2,
                                                  int grid_angles = 4,
                                                  float noise_strength = 1.0f,
                                                  int road_width = 3);
