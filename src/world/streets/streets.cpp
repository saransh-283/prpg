#include "streets.h"
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
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
