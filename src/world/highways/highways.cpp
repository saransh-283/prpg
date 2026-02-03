#include "highways.h"
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <noise/noise.h>
#include <glm/glm.hpp>
#include "../../core/config.h"

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

            for (int j = 0; j < worm_length; ++j) {
                // world-space x,z
                poly.emplace_back((float)x, (float)y);
                float angle = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);
                x += std::cos(angle) * step_size;
                y += std::sin(angle) * step_size;
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
    // Create a copy of the input grid
    std::vector<std::vector<int>> result_grid = terrain_grid;
    int grid_size = static_cast<int>(result_grid.size());

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    // Separate Perlin for thickness to keep it independent of direction.
    noise::module::Perlin thicknessPerlin;
    thicknessPerlin.SetSeed(seed + 10007);

    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    int size = chunk_size + 2 * padding;

    // Deterministic placement across chunks
    int cell_spacing = std::max(16, padding / 2);
    int gx_min = static_cast<int>(std::floor((double)wx / cell_spacing));
    int gx_max = static_cast<int>(std::floor((double)(wx + size) / cell_spacing));
    int gy_min = static_cast<int>(std::floor((double)wy / cell_spacing));
    int gy_max = static_cast<int>(std::floor((double)(wy + size) / cell_spacing));

    int count = 0;
    for (int gx = gx_min; gx <= gx_max && count < num_highways; ++gx) {
        for (int gy = gy_min; gy <= gy_max && count < num_highways; ++gy) {
            // Deterministic start position within cell
            double u = deterministic_unit(gx, gy, seed + 0);
            double v = deterministic_unit(gx, gy, seed + 1);
            double x = gx * (double)cell_spacing + u * (double)cell_spacing;
            double y = gy * (double)cell_spacing + v * (double)cell_spacing;

            float smoothRadius = (Config::Highway::THICKNESS_MIN + Config::Highway::THICKNESS_MAX) * 0.5f;

            for (int j = 0; j < worm_length; ++j) {
                int grid_x = static_cast<int>((x - wx) * grid_size / size);
                int grid_y = static_cast<int>((y - wy) * grid_size / size);

                if (grid_x >= 0 && grid_x < grid_size && grid_y >= 0 && grid_y < grid_size) {
                    // Smooth thickness from Perlin (map [-1,1] -> [0,1]).
                    double n = thicknessPerlin.GetValue(x * Config::Highway::THICKNESS_PERLIN_SCALE,
                                                       y * Config::Highway::THICKNESS_PERLIN_SCALE,
                                                       0.0);
                    float t = clamp01(static_cast<float>((n + 1.0) * 0.5));
                    float targetRadius = lerp(Config::Highway::THICKNESS_MIN, Config::Highway::THICKNESS_MAX, t);
                    smoothRadius = lerp(smoothRadius, targetRadius, Config::Highway::THICKNESS_SMOOTH_ALPHA);

                    // Paint disc onto terrain only (0). Highways are type 1.
                    paint_disc_if(result_grid, grid_x, grid_y, smoothRadius, 1, 0);
                }

                float angle = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);
                x += std::cos(angle) * step_size;
                y += std::sin(angle) * step_size;
            }

            ++count;
        }
    }

    return result_grid;
}
