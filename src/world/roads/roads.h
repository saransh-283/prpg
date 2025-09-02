#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Generate road polylines for a chunk/region in world coordinates.
// Returns a vector of polylines; each polyline is a vector of 2D points (x,z) in world space.
std::vector<std::vector<glm::vec2>> generate_roads_chunk_polylines(int chunk_x,
                                                                   int chunk_y,
                                                                   int chunk_size = 256,
                                                                   int padding = 64,
                                                                   int num_roads = 200,
                                                                   int worm_length = 800,
                                                                   float step_size = 1.0f,
                                                                   float perlin_scale = 0.01f,
                                                                   int seed = 42,
                                                                   int grid_angles = 4,
                                                                   float noise_strength = 1.0f);

// Find the nearest point on any generated road to the given world (x,z) position.
// This will search chunks in a square radius (in chunk units) around the point.
// Returns the world-space (x,z) of the nearest point. If no roads are found,
// returns the input position.
glm::vec2 find_nearest_road_point(float x, float z, int search_radius_chunks = 1, int chunk_size = 256);
