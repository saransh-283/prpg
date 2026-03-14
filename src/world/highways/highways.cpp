#include "highways.h"
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <noise/noise.h>
#include <glm/glm.hpp>
#include <core/params/params.h>

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

std::vector<std::vector<glm::vec2>> generate_highways_chunk_polylines(int chunk_x, int chunk_y, int chunk_size, int padding, int num_highways, int worm_length, float step_size, float perlin_scale, int seed, int grid_angles, float noise_strength) {
    // world-space bounds for padded region
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding; // wy here is world Z
    int size = chunk_size + 2 * padding;

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);
    const auto& highway = CoreParams::GetHighwayParams();

    // Choose a global grid spacing so neighboring chunks will consider the same seed cells
    int cell_spacing = std::max(16, padding / 2);
    int gx_min = static_cast<int>(std::floor((double)wx / cell_spacing));
    int gx_max = static_cast<int>(std::floor((double)(wx + size) / cell_spacing));
    int gy_min = static_cast<int>(std::floor((double)wy / cell_spacing));
    int gy_max = static_cast<int>(std::floor((double)(wy + size) / cell_spacing));

    std::vector<std::vector<glm::vec2>> polylines;
    int count = 0;

    for (int gx = gx_min; gx <= gx_max; ++gx) {
        for (int gy = gy_min; gy <= gy_max; ++gy) {
            if (count >= num_highways) break;

            // deterministic offset within cell
            double u = deterministic_unit(gx, gy, seed + 0);
            double v = deterministic_unit(gx, gy, seed + 1);
            double x = gx * (double)cell_spacing + u * (double)cell_spacing;
            double y = gy * (double)cell_spacing + v * (double)cell_spacing;

            std::vector<glm::vec2> poly;
            poly.reserve(worm_length);

            const float maxTurn = deg2rad(static_cast<float>(highway.value("max_turn_deg", 20.0)));
            const float maxTurnPerStep = deg2rad(static_cast<float>(highway.value("max_turn_deg_per_step", 0.75)));
            float heading = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);

            bool inBend = false;
            float bendTarget = heading;
            int phaseRemaining = 0;

            for (int j = 0; j < worm_length; ++j) {
                // world-space x,z
                poly.emplace_back((float)x, (float)y);

                if (phaseRemaining <= 0) {
                    int64_t xi = (int64_t)std::floor(x);
                    int64_t yi = (int64_t)std::floor(y);
                    int64_t salt = (int64_t)seed + (int64_t)j + (inBend ? 5000 : 7000);

                    if (inBend) {
                        inBend = false;
                        phaseRemaining = pick_steps_range(static_cast<int>(highway.value("straight_min_steps", 80)),
                                                         static_cast<int>(highway.value("straight_max_steps", 140)),
                                                         xi, yi, salt);
                    } else {
                        inBend = true;
                        phaseRemaining = pick_steps_range(static_cast<int>(highway.value("bend_min_steps", 20)),
                                                         static_cast<int>(highway.value("bend_max_steps", 40)),
                                                         xi, yi, salt);

                        float rawDesired = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);
                        float d = angle_diff(rawDesired, heading);
                        if (d > maxTurn) d = maxTurn;
                        else if (d < -maxTurn) d = -maxTurn;

                        // Ensure the bend phase actually bends.
                        if (std::abs(d) < deg2rad(2.0f)) {
                            double s = deterministic_unit(xi, yi, salt + 123);
                            float forced = std::min(maxTurn, deg2rad(10.0f));
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

            polylines.push_back(std::move(poly));
            ++count;
        }
        if (count >= num_highways) break;
    }

    return polylines;
}

// Dummy implementation for grid-based highway generation
std::vector<std::vector<int>> generate_highways_grid(const std::vector<std::vector<int>>& terrain_grid,
                                                    int chunk_x, int chunk_y,
                                                    int chunk_size, int padding,
                                                    int num_highways, int worm_length,
                                                    float step_size, float perlin_scale,
                                                    int seed, int grid_angles,
                                                    float noise_strength) {
    // Generate into a padded grid for seamless chunk borders, then crop.
    const int padded_size = chunk_size + 2 * padding;
    std::vector<std::vector<int>> padded_grid(padded_size, std::vector<int>(padded_size, 0));
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            padded_grid[z + padding][x + padding] = terrain_grid[z][x];
        }
    }

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    // Separate Perlin for thickness to keep it independent of direction.
    noise::module::Perlin thicknessPerlin;
    thicknessPerlin.SetSeed(seed + 10007);

    // World-space origin for padded grid (in grid units).
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;

    // Highway seeds: evaluate a sparse global grid in a local+neighbor margin.
    // Using a per-cell deterministic predicate keeps results consistent across chunks.
    // The spawn probability targets ~num_highways expected starts in this window.
    const auto& highway = CoreParams::GetStreetParams();
    const int cell_spacing = std::max(8, static_cast<int>(highway.value("global_cell_spacing", 64)));
    const double seed_margin = (double)chunk_size;

    const double wx0 = (double)wx - seed_margin;
    const double wy0 = (double)wy - seed_margin;
    const double wx1 = (double)wx + (double)padded_size + seed_margin;
    const double wy1 = (double)wy + (double)padded_size + seed_margin;

    const int gx_min = static_cast<int>(std::floor(wx0 / (double)cell_spacing));
    const int gx_max = static_cast<int>(std::floor(wx1 / (double)cell_spacing));
    const int gy_min = static_cast<int>(std::floor(wy0 / (double)cell_spacing));
    const int gy_max = static_cast<int>(std::floor(wy1 / (double)cell_spacing));

    const int64_t nx = (int64_t)gx_max - (int64_t)gx_min + 1;
    const int64_t ny = (int64_t)gy_max - (int64_t)gy_min + 1;
    const double cellCount = (nx > 0 && ny > 0) ? (double)(nx * ny) : 0.0;

    const double baseProb = std::min(1.0, std::max(0.0, (double)highway.value("global_seed_prob", 0.006)));
    const double targetProb = (cellCount > 0.0 && num_highways > 0) ? std::min(1.0, std::max(0.0, (double)num_highways / cellCount)) : 0.0;
    const double spawnProb = std::min(1.0, std::max(baseProb, targetProb));

    for (int gx = gx_min; gx <= gx_max; ++gx) {
        for (int gy = gy_min; gy <= gy_max; ++gy) {
            double h = deterministic_unit(gx, gy, seed + 90017);
            if (h >= spawnProb) continue;

            // Deterministic start position within cell
            double u = deterministic_unit(gx, gy, seed + 0);
            double v = deterministic_unit(gx, gy, seed + 1);
            double x = gx * (double)cell_spacing + u * (double)cell_spacing;
            double y = gy * (double)cell_spacing + v * (double)cell_spacing;
            
            const float maxTurn = deg2rad(static_cast<float>(highway.value("max_turn_deg", 20.0)));
            const float maxTurnPerStep = deg2rad(static_cast<float>(highway.value("max_turn_deg_per_step", 0.75)));
            float heading = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);

            bool inBend = false;
            float bendTarget = heading;
            int phaseRemaining = 0;

            float smoothRadius = (static_cast<float>(highway.value("thickness_min", 2.5)) + static_cast<float>(highway.value("thickness_max", 4.5))) * 0.5f;

            for (int j = 0; j < worm_length; ++j) {
                int grid_x = static_cast<int>(std::floor(x - (double)wx));
                int grid_y = static_cast<int>(std::floor(y - (double)wy));

                if (grid_x >= 0 && grid_x < padded_size && grid_y >= 0 && grid_y < padded_size) {
                    // Smooth thickness from Perlin (map [-1,1] -> [0,1]).
                    double n = thicknessPerlin.GetValue(x * highway.value("thickness_perlin_scale", 0.0025),
                                                       y * highway.value("thickness_perlin_scale", 0.0025),
                                                       0.0);
                    float t = clamp01(static_cast<float>((n + 1.0) * 0.5));
                    float targetRadius = lerp(static_cast<float>(highway.value("thickness_min", 2.5)), static_cast<float>(highway.value("thickness_max", 4.5)), t);
                    smoothRadius = lerp(smoothRadius, targetRadius, static_cast<float>(highway.value("thickness_smooth_alpha", 0.03)));

                    // Paint disc onto terrain only (0). Highways are type 1.
                    paint_disc_if(padded_grid, grid_x, grid_y, smoothRadius, 1, 0);
                }

                if (phaseRemaining <= 0) {
                    int64_t xi = (int64_t)std::floor(x);
                    int64_t yi = (int64_t)std::floor(y);
                    int64_t salt = (int64_t)seed + (int64_t)j + (inBend ? 5000 : 7000);

                    if (inBend) {
                        inBend = false;
                        phaseRemaining = pick_steps_range(static_cast<int>(highway.value("straight_min_steps", 80)),
                                                         static_cast<int>(highway.value("straight_max_steps", 140)),
                                                         xi, yi, salt);
                    } else {
                        inBend = true;
                        phaseRemaining = pick_steps_range(static_cast<int>(highway.value("bend_min_steps", 20)),
                                                         static_cast<int>(highway.value("bend_max_steps", 40)),
                                                         xi, yi, salt);

                        float rawDesired = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);
                        float d = angle_diff(rawDesired, heading);
                        if (d > maxTurn) d = maxTurn;
                        else if (d < -maxTurn) d = -maxTurn;

                        if (std::abs(d) < deg2rad(2.0f)) {
                            double s = deterministic_unit(xi, yi, salt + 123);
                            float forced = std::min(maxTurn, deg2rad(10.0f));
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
        }
    }

    // Crop padded result back to chunk grid.
    std::vector<std::vector<int>> result_grid = terrain_grid;
    for (int z = 0; z < chunk_size; ++z) {
        for (int x = 0; x < chunk_size; ++x) {
            result_grid[z][x] = padded_grid[z + padding][x + padding];
        }
    }
    return result_grid;
}
