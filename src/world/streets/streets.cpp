#include "streets.h"
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <iostream>
#include <noise/noise.h>
#include <glm/glm.hpp>

// 64-bit splitmix hash to produce deterministic pseudo-random numbers from integers
static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Deterministic float in [0,1) from integer inputs
static double deterministic_unit(int64_t a, int64_t b, int64_t c) {
    uint64_t h = splitmix64((uint64_t)a);
    h = splitmix64(h + (uint64_t)b);
    h = splitmix64(h + (uint64_t)c);
    // take 52 bits
    uint64_t v = h & ((1ULL<<52)-1);
    return (double)v / (double)(1ULL<<52);
}

// Quantized angle using Perlin noise
static float quantized_angle(float x, float y, float scale, int grid_angles, float noise_strength, noise::module::Perlin& perlin) {
    float perlin_val = perlin.GetValue(x * scale, y * scale, 0.0f) * noise_strength;
    float base_angle = std::round(perlin_val * grid_angles) * (2.0f * M_PI / grid_angles);
    return base_angle;
}

// Get road direction at a point by checking neighboring road pixels
static float get_road_direction(const std::vector<std::vector<glm::vec2>>& roads, float x, float y, float search_radius = 4.0f) {
    std::vector<float> directions;

    // Check all road polylines for nearby points
    for (const auto& poly : roads) {
        for (size_t i = 0; i < poly.size(); ++i) {
            float dx = poly[i].x - x;
            float dy = poly[i].y - y;
            float dist_sq = dx*dx + dy*dy;

            if (dist_sq <= search_radius * search_radius) {
                // Find direction from this segment
                if (i + 1 < poly.size()) {
                    glm::vec2 dir = glm::normalize(poly[i+1] - poly[i]);
                    directions.push_back(std::atan2(dir.y, dir.x));
                } else if (i > 0) {
                    glm::vec2 dir = glm::normalize(poly[i] - poly[i-1]);
                    directions.push_back(std::atan2(dir.y, dir.x));
                }
            }
        }
    }

    if (directions.empty()) {
        return 0.0f; // Default direction
    }

    // Average the angles (handling circular nature)
    float avg_sin = 0.0f, avg_cos = 0.0f;
    for (float angle : directions) {
        avg_sin += std::sin(angle);
        avg_cos += std::cos(angle);
    }
    avg_sin /= directions.size();
    avg_cos /= directions.size();

    return std::atan2(avg_sin, avg_cos);
}

// Get perpendicular angles for street generation
static std::vector<float> get_perpendicular_angles(float road_angle, int grid_angles) {
    float angle_step = 2.0f * M_PI / grid_angles;

    // Calculate perpendicular directions (90 degrees from road direction)
    float perp1 = road_angle + M_PI / 2.0f;
    float perp2 = road_angle - M_PI / 2.0f;

    // Quantize to grid angles
    float perp1_quantized = std::round(perp1 / angle_step) * angle_step;
    float perp2_quantized = std::round(perp2 / angle_step) * angle_step;

    return {perp1_quantized, perp2_quantized};
}

// Generate a single street worm
static std::vector<glm::vec2> generate_street_worm(float start_x, float start_y, int worm_length, float step_size,
                                                   float initial_angle, float perlin_scale, int grid_angles,
                                                   float noise_strength, int seed, noise::module::Perlin& perlin) {
    std::vector<glm::vec2> poly;
    poly.reserve(worm_length);

    float x = start_x;
    float y = start_y;
    float current_angle = initial_angle;

    // Parameters for organic street generation
    int min_straight_distance = 2;
    int steps_since_turn = 0;

    for (int j = 0; j < worm_length; ++j) {
        poly.emplace_back(x, y);

        // Update angle with noise, but less frequently
        if (steps_since_turn >= min_straight_distance) {
            float noise_angle = quantized_angle(x, y, perlin_scale * 2.0f, grid_angles, noise_strength, perlin);

            // Allow turns with reduced noise influence
            float angle_diff = std::abs(noise_angle - current_angle);
            if (angle_diff > M_PI / (grid_angles * 3.0f)) {
                current_angle = noise_angle;
                steps_since_turn = 0;
            }
        }

        // Move to next position
        x += std::cos(current_angle) * step_size;
        y += std::sin(current_angle) * step_size;
        steps_since_turn++;
    }

    return poly;
}

std::vector<std::vector<glm::vec2>> generate_streets_chunk_polylines(int chunk_x, int chunk_y, int chunk_size, int padding, int num_streets, int worm_length, float step_size, float perlin_scale, int seed, int grid_angles, float noise_strength, const std::vector<std::vector<glm::vec2>>& highways, const std::vector<std::vector<glm::vec2>>& roads) {
    // world-space bounds for padded region
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    int size = chunk_size + 2 * padding;

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    std::vector<std::vector<glm::vec2>> street_polylines;

    // Combine highways and roads for finding branch points
    std::vector<std::vector<glm::vec2>> all_roads = highways;
    all_roads.insert(all_roads.end(), roads.begin(), roads.end());

    if (all_roads.empty()) {
        return street_polylines; // No roads to branch from
    }

    // Collect all road points for potential street starts
    std::vector<glm::vec2> road_points;
    for (const auto& poly : all_roads) {
        road_points.insert(road_points.end(), poly.begin(), poly.end());
    }

    // Generate streets by branching from road points
    int streets_generated = 0;
    int max_attempts = num_streets * 3; // Allow more attempts to find valid start points
    int attempts = 0;

    while (streets_generated < num_streets && attempts < max_attempts && !road_points.empty()) {
        attempts++;

        // Choose a random road point deterministically
        double hash_val = deterministic_unit(chunk_x, chunk_y, seed + attempts);
        size_t point_idx = static_cast<size_t>(hash_val * road_points.size());
        glm::vec2 start_point = road_points[point_idx];

        // Get road direction at this point
        float road_direction = get_road_direction(all_roads, start_point.x, start_point.y);

        // Get perpendicular directions
        auto perpendicular_angles = get_perpendicular_angles(road_direction, grid_angles);

        // Choose one of the perpendicular directions
        double dir_hash = deterministic_unit(chunk_x, chunk_y, seed + attempts + 1000);
        float chosen_angle = perpendicular_angles[static_cast<size_t>(dir_hash * perpendicular_angles.size())];

        // Generate street worm
        auto street_poly = generate_street_worm(start_point.x, start_point.y, worm_length, step_size,
                                               chosen_angle, perlin_scale, grid_angles, noise_strength,
                                               seed + attempts, perlin);

        if (!street_poly.empty()) {
            street_polylines.push_back(std::move(street_poly));
            streets_generated++;
        }
    }

    return street_polylines;
}

// Grid-based street generation implementing the algorithm from the notebook
std::vector<std::vector<int>> generate_streets_grid(const std::vector<std::vector<int>>& input_grid,
                                                   int chunk_x, int chunk_y,
                                                   int chunk_size, int padding,
                                                   int num_streets, int worm_length,
                                                   float step_size, float perlin_scale,
                                                   int seed, int grid_angles,
                                                   float noise_strength) {
    // Create a copy of the input grid
    std::vector<std::vector<int>> result_grid = input_grid;
    int grid_size = result_grid.size();
    
    // Set up world coordinates and noise
    noise::module::Perlin perlin;
    perlin.SetSeed(seed); // Different seed for streets
    
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    int size = chunk_size + 2 * padding;
    
    // Find all road pixels (value 2) to start streets from their edges
    std::vector<std::pair<int, int>> road_pixels;
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            if (result_grid[y][x] == 2) { // Road pixel
                road_pixels.push_back({x, y});
            }
        }
    }
    
    if (road_pixels.empty()) {
        return result_grid; // No roads to branch from
    }
    
    // Generate more streets - reduce spacing between street starts
    int streets_per_road = std::max(1, static_cast<int>(road_pixels.size() / (num_streets * 2))); // Changed from *5 to *2
    int streets_generated = 0;
    
    for (size_t i = 0; i < road_pixels.size() && streets_generated < num_streets; i += streets_per_road) {
        int grid_x = road_pixels[i].first;
        int grid_y = road_pixels[i].second;
        
        // Convert grid coordinates to world coordinates
        double start_x = wx + (grid_x * size) / (double)grid_size;
        double start_y = wy + (grid_y * size) / (double)grid_size;
        
        // Estimate road direction by checking neighboring road pixels
        float road_direction = 0.0f;
        int direction_count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = grid_x + dx;
                int ny = grid_y + dy;
                if (nx >= 0 && nx < grid_size && ny >= 0 && ny < grid_size) {
                    if (result_grid[ny][nx] == 2) { // Another road pixel
                        road_direction += std::atan2(dy, dx);
                        direction_count++;
                    }
                }
            }
        }
        
        if (direction_count > 0) {
            road_direction /= direction_count;
        }
        
        // Generate perpendicular direction
        float street_direction = road_direction + M_PI / 2.0f; // 90 degrees
        if (deterministic_unit(chunk_x, chunk_y, seed + i) > 0.5) {
            street_direction -= M_PI; // -90 degrees instead
        }
        
        // Start street NEXT TO the road pixel, not ON it
        // Move one step in the perpendicular direction from the road
        double offset_distance = size / (double)grid_size; // One grid cell worth
        double x = start_x + std::cos(street_direction) * offset_distance;
        double y = start_y + std::sin(street_direction) * offset_distance;
        int street_length = worm_length / 3; // Streets are shorter
        
        for (int j = 0; j < street_length; ++j) {
            // Convert world coordinates to grid coordinates
            int street_grid_x = static_cast<int>((x - wx) * grid_size / size);
            int street_grid_y = static_cast<int>((y - wy) * grid_size / size);
            
            // Check bounds and avoid roads
            if (street_grid_x >= 0 && street_grid_x < grid_size && 
                street_grid_y >= 0 && street_grid_y < grid_size) {
                if (result_grid[street_grid_y][street_grid_x] == 0) { // Only mark terrain as street
                    result_grid[street_grid_y][street_grid_x] = 3; // Street type
                } else if (result_grid[street_grid_y][street_grid_x] == 2) {
                    break; // Stop if we hit another road
                }
            } else {
                break; // Out of bounds
            }
            
            // Add slight noise to direction for more organic streets
            float noise_influence = 0.3f;
            float perlin_val = perlin.GetValue(x * perlin_scale * 2, y * perlin_scale * 2, 0.0f) * noise_influence;
            float angle = street_direction + perlin_val;
            
            // Move to next position
            x += std::cos(angle) * step_size;
            y += std::sin(angle) * step_size;
        }
        
        streets_generated++;
    }
    
    return result_grid;
}
