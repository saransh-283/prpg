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
#include "../../core/config.h"

// Forward declaration (definition appears later in this file)
static double deterministic_unit(int64_t a, int64_t b, int64_t c);

static inline float wrap_pi(float a) {
    while (a > (float)M_PI) a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

static inline float angle_diff(float target, float current) {
    return wrap_pi(target - current);
}

static inline float rotate_towards(float current, float target, float max_delta) {
    float d = angle_diff(target, current);
    if (d > max_delta) d = max_delta;
    else if (d < -max_delta) d = -max_delta;
    return wrap_pi(current + d);
}

static inline float deg2rad(float deg) {
    return deg * ((float)M_PI / 180.0f);
}

static inline int pick_steps_range(int minSteps, int maxSteps, int64_t a, int64_t b, int64_t c) {
    if (minSteps < 1) minSteps = 1;
    if (maxSteps < minSteps) maxSteps = minSteps;
    double u = deterministic_unit(a, b, c);
    int span = maxSteps - minSteps + 1;
    int v = minSteps + (int)std::floor(u * (double)span);
    if (v < minSteps) v = minSteps;
    if (v > maxSteps) v = maxSteps;
    return v;
}

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void paint_disc_if(std::vector<std::vector<int>>& grid, int cx, int cy, float radius, int value, int only_if_value) {
    if (radius <= 0.0f) return;
    int h = static_cast<int>(grid.size());
    if (h <= 0) return;
    int w = static_cast<int>(grid[0].size());

    int r = static_cast<int>(std::ceil(radius));
    float r2 = radius * radius;
    for (int dy = -r; dy <= r; ++dy) {
        int y = cy + dy;
        if (y < 0 || y >= h) continue;
        for (int dx = -r; dx <= r; ++dx) {
            int x = cx + dx;
            if (x < 0 || x >= w) continue;
            if (static_cast<float>(dx * dx + dy * dy) > r2) continue;
            if (grid[y][x] == only_if_value) {
                grid[y][x] = value;
            }
        }
    }
}

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
    const float maxTurn = deg2rad(Config::Street::MAX_TURN_DEG);
    const float maxTurnPerStep = deg2rad(Config::Street::MAX_TURN_DEG_PER_STEP);
    float heading = wrap_pi(initial_angle);
    bool inBend = false;
    float bendTarget = heading;
    int phaseRemaining = 0;

    for (int j = 0; j < worm_length; ++j) {
        poly.emplace_back(x, y);

        if (phaseRemaining <= 0) {
            int64_t xi = (int64_t)std::floor(x);
            int64_t yi = (int64_t)std::floor(y);
            int64_t salt = (int64_t)seed + (int64_t)j + (inBend ? 5200 : 7200);

            if (inBend) {
                inBend = false;
                phaseRemaining = pick_steps_range(Config::Street::STRAIGHT_MIN_STEPS,
                                                 Config::Street::STRAIGHT_MAX_STEPS,
                                                 xi, yi, salt);
            } else {
                inBend = true;
                phaseRemaining = pick_steps_range(Config::Street::BEND_MIN_STEPS,
                                                 Config::Street::BEND_MAX_STEPS,
                                                 xi, yi, salt);

                float rawDesired = quantized_angle(x, y, perlin_scale * 2.0f, grid_angles, noise_strength, perlin);
                float d = angle_diff(rawDesired, heading);
                if (d > maxTurn) d = maxTurn;
                else if (d < -maxTurn) d = -maxTurn;

                if (std::abs(d) < deg2rad(2.0f)) {
                    double s = deterministic_unit(xi, yi, salt + 123);
                    float forced = std::min(maxTurn, deg2rad(20.0f));
                    d = (s < 0.5) ? -forced : forced;
                }

                bendTarget = wrap_pi(heading + d);
            }
        }

        if (inBend) {
            heading = rotate_towards(heading, bendTarget, maxTurnPerStep);
        }
        x += std::cos(heading) * step_size;
        y += std::sin(heading) * step_size;
        phaseRemaining--;
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
    // Generate into a padded grid for seamless chunk borders, then crop.
    const int padded_size = chunk_size + 2 * padding;
    std::vector<std::vector<int>> padded_grid(padded_size, std::vector<int>(padded_size, 0));
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            padded_grid[z + padding][x + padding] = input_grid[z][x];
        }
    }
    
    // Set up world coordinates and noise
    noise::module::Perlin perlin;
    perlin.SetSeed(seed); // Different seed for streets

    noise::module::Perlin thicknessPerlin;
    thicknessPerlin.SetSeed(seed + 10007);
    
    // World-space origin for padded grid (in grid units).
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    
    // Find all road pixels (value 2) to start streets from their edges
    std::vector<std::pair<int, int>> road_pixels;
    for (int y = 0; y < padded_size; ++y) {
        for (int x = 0; x < padded_size; ++x) {
            if (padded_grid[y][x] == 2) { // Road pixel
                road_pixels.push_back({x, y});
            }
        }
    }
    
    if (road_pixels.empty()) {
        return input_grid; // No roads to branch from
    }
    
    // Generate more streets - reduce spacing between street starts
    int denom = std::max(1, num_streets * 2);
    int streets_per_road = std::max(1, static_cast<int>(road_pixels.size() / denom)); // Changed from *5 to *2
    int streets_generated = 0;
    
    for (size_t i = 0; i < road_pixels.size() && streets_generated < (size_t)std::max(0, num_streets); i += streets_per_road) {
        int grid_x = road_pixels[i].first;
        int grid_y = road_pixels[i].second;
        
        // Convert grid coordinates to world coordinates
        double start_x = wx + (double)grid_x;
        double start_y = wy + (double)grid_y;
        
        // Estimate road direction by checking neighboring road pixels
        float road_direction = 0.0f;
        int direction_count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = grid_x + dx;
                int ny = grid_y + dy;
                if (nx >= 0 && nx < padded_size && ny >= 0 && ny < padded_size) {
                    if (padded_grid[ny][nx] == 2) { // Another road pixel
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
        double offset_distance = 1.0; // One grid cell
        double x = start_x + std::cos(street_direction) * offset_distance;
        double y = start_y + std::sin(street_direction) * offset_distance;
        int street_length = worm_length / 3; // Streets are shorter

        const float maxTurn = deg2rad(Config::Street::MAX_TURN_DEG);
        const float maxTurnPerStep = deg2rad(Config::Street::MAX_TURN_DEG_PER_STEP);
        float heading = wrap_pi(street_direction);
        bool inBend = false;
        float bendTarget = heading;
        int phaseRemaining = 0;

        float smoothRadius = (Config::Street::THICKNESS_MIN + Config::Street::THICKNESS_MAX) * 0.5f;
        
        for (int j = 0; j < street_length; ++j) {
            // Convert world coordinates to grid coordinates
            int street_grid_x = static_cast<int>(std::floor(x - (double)wx));
            int street_grid_y = static_cast<int>(std::floor(y - (double)wy));
            
            // Check bounds and avoid roads
            if (street_grid_x >= 0 && street_grid_x < padded_size && 
                street_grid_y >= 0 && street_grid_y < padded_size) {
                if (padded_grid[street_grid_y][street_grid_x] == 2) {
                    break; // Stop if we hit a road
                }

                // Smooth thickness from Perlin and paint onto terrain only.
                double n = thicknessPerlin.GetValue(x * Config::Street::THICKNESS_PERLIN_SCALE,
                                                   y * Config::Street::THICKNESS_PERLIN_SCALE,
                                                   0.0);
                float t = clamp01(static_cast<float>((n + 1.0) * 0.5));
                float targetRadius = lerp(Config::Street::THICKNESS_MIN, Config::Street::THICKNESS_MAX, t);
                smoothRadius = lerp(smoothRadius, targetRadius, Config::Street::THICKNESS_SMOOTH_ALPHA);

                paint_disc_if(padded_grid, street_grid_x, street_grid_y, smoothRadius, 3, 0);
            } else {
                break; // Out of bounds
            }

            if (phaseRemaining <= 0) {
                int64_t xi = (int64_t)std::floor(x);
                int64_t yi = (int64_t)std::floor(y);
                int64_t salt = (int64_t)seed + (int64_t)i + (int64_t)j + (inBend ? 5300 : 7300);

                if (inBend) {
                    inBend = false;
                    phaseRemaining = pick_steps_range(Config::Street::STRAIGHT_MIN_STEPS,
                                                     Config::Street::STRAIGHT_MAX_STEPS,
                                                     xi, yi, salt);
                } else {
                    inBend = true;
                    phaseRemaining = pick_steps_range(Config::Street::BEND_MIN_STEPS,
                                                     Config::Street::BEND_MAX_STEPS,
                                                     xi, yi, salt);

                    float rawDesired = quantized_angle((float)x, (float)y, perlin_scale * 2.0f, grid_angles, noise_strength, perlin);
                    float d = angle_diff(rawDesired, heading);
                    if (d > maxTurn) d = maxTurn;
                    else if (d < -maxTurn) d = -maxTurn;

                    if (std::abs(d) < deg2rad(2.0f)) {
                        double s = deterministic_unit(xi, yi, salt + 123);
                        float forced = std::min(maxTurn, deg2rad(20.0f));
                        d = (s < 0.5) ? -forced : forced;
                    }

                    bendTarget = wrap_pi(heading + d);
                }
            }

            if (inBend) {
                heading = rotate_towards(heading, bendTarget, maxTurnPerStep);
            }
            x += std::cos(heading) * step_size;
            y += std::sin(heading) * step_size;
            phaseRemaining--;
        }
        
        streets_generated++;
    }

    // Crop padded result back to chunk grid.
    std::vector<std::vector<int>> result_grid = input_grid;
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            result_grid[z][x] = padded_grid[z + padding][x + padding];
        }
    }
    return result_grid;
}
