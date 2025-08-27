#include "roads.h"
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <noise/noise.h>
#include <glm/glm.hpp>

// Draw a filled disk (circle) at (x, y) with given radius
static void draw_disk(std::vector<uint8_t>& canvas, int width, int height, int x, int y, int radius = 2) {
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            if (dx * dx + dy * dy <= radius * radius) {
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    canvas[ny * width + nx] = 255;
                }
            }
        }
    }
}

// Quantized angle using Perlin noise
static float quantized_angle(float x, float y, float scale, int grid_angles, float noise_strength, noise::module::Perlin& perlin) {
    float perlin_val = perlin.GetValue(x * scale, y * scale, 0.0f) * noise_strength;
    float base_angle = std::round(perlin_val * grid_angles) * (2.0f * M_PI / grid_angles);
    return base_angle;
}

std::vector<uint8_t> generate_perlin_roads(int canvas_size, int num_worms, int worm_length, float step_size, float perlin_scale, int seed, int grid_angles, float noise_strength, int road_width) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0, canvas_size);
    std::vector<uint8_t> canvas(canvas_size * canvas_size, 0);
    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    for (int i = 0; i < num_worms; ++i) {
        float x = dist(rng);
        float y = dist(rng);
        for (int j = 0; j < worm_length; ++j) {
            draw_disk(canvas, canvas_size, canvas_size, static_cast<int>(x), static_cast<int>(y), road_width);
            float angle = quantized_angle(x, y, perlin_scale, grid_angles, noise_strength, perlin);
            x += std::cos(angle) * step_size;
            y += std::sin(angle) * step_size;
        }
    }
    return canvas;
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

// Draw into flattened canvas
static void draw_disk_flat(std::vector<uint8_t>& canvas, int width, int height, double x, double y, int radius = 2) {
    int xi = static_cast<int>(std::round(x));
    int yi = static_cast<int>(std::round(y));
    int rr = radius * radius;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            if (dx*dx + dy*dy <= rr) {
                int nx = xi + dx;
                int ny = yi + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    canvas[ny * width + nx] = 255;
                }
            }
        }
    }
}

std::vector<uint8_t> generate_perlin_roads_chunk(int chunk_x, int chunk_y, int chunk_size, int padding, int num_worms, int worm_length, float step_size, float perlin_scale, int seed, int grid_angles, float noise_strength, int road_width) {
    // world-space bounds for padded region
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    int size = chunk_size + 2 * padding;
    std::vector<uint8_t> canvas(size * size, 0);

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    // Choose a global grid spacing so neighboring chunks will consider the same seed cells
    int cell_spacing = std::max(16, padding / 2);
    int gx_min = static_cast<int>(std::floor((double)wx / cell_spacing));
    int gx_max = static_cast<int>(std::floor((double)(wx + size) / cell_spacing));
    int gy_min = static_cast<int>(std::floor((double)wy / cell_spacing));
    int gy_max = static_cast<int>(std::floor((double)(wy + size) / cell_spacing));

    int count = 0;
    for (int gx = gx_min; gx <= gx_max; ++gx) {
        for (int gy = gy_min; gy <= gy_max; ++gy) {
            if (count >= num_worms) break;
            // deterministic offset within cell
            double u = deterministic_unit(gx, gy, seed + 0);
            double v = deterministic_unit(gx, gy, seed + 1);
            double x = gx * (double)cell_spacing + u * (double)cell_spacing;
            double y = gy * (double)cell_spacing + v * (double)cell_spacing;

            // run worm in world coordinates; draw when inside padded region
            for (int j = 0; j < worm_length; ++j) {
                double lx = x - wx; // local padded coords
                double ly = y - wy;
                if (lx >= 0 && lx < size && ly >= 0 && ly < size) {
                    draw_disk_flat(canvas, size, size, lx, ly, road_width);
                }
                float angle = quantized_angle((float)x, (float)y, perlin_scale, grid_angles, noise_strength, perlin);
                x += std::cos(angle) * step_size;
                y += std::sin(angle) * step_size;
            }
            ++count;
        }
        if (count >= num_worms) break;
    }

    // crop to chunk region
    std::vector<uint8_t> cropped(chunk_size * chunk_size, 0);
    for (int row = 0; row < chunk_size; ++row) {
        int src_y = padding + row;
        for (int col = 0; col < chunk_size; ++col) {
            int src_x = padding + col;
            cropped[row * chunk_size + col] = canvas[src_y * size + src_x];
        }
    }
    return cropped;
}

// New: generate polylines in world XZ coordinates for a chunk (including padding)
std::vector<std::vector<glm::vec2>> generate_perlin_roads_chunk_polylines(int chunk_x, int chunk_y, int chunk_size, int padding, int num_worms, int worm_length, float step_size, float perlin_scale, int seed, int grid_angles, float noise_strength) {
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding; // wy here is world Z
    int size = chunk_size + 2 * padding;

    noise::module::Perlin perlin;
    perlin.SetSeed(seed);

    int cell_spacing = std::max(16, padding / 2);
    int gx_min = static_cast<int>(std::floor((double)wx / cell_spacing));
    int gx_max = static_cast<int>(std::floor((double)(wx + size) / cell_spacing));
    int gy_min = static_cast<int>(std::floor((double)wy / cell_spacing));
    int gy_max = static_cast<int>(std::floor((double)(wy + size) / cell_spacing));

    std::vector<std::vector<glm::vec2>> polylines;
    int count = 0;
    for (int gx = gx_min; gx <= gx_max; ++gx) {
        for (int gy = gy_min; gy <= gy_max; ++gy) {
            if (count >= num_worms) break;
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
        if (count >= num_worms) break;
    }
    return polylines;
}

// Find nearest point on generated roads to (x,z). Search neighboring chunks within search_radius_chunks.
// Find nearest point on generated roads to (x,z). Search neighboring chunks within search_radius_chunks.
glm::vec2 find_nearest_road_point(float x, float z, int search_radius_chunks, int chunk_size) {
    // Map world x,z to chunk coordinates (using same chunk_size as generation)
    int cx = static_cast<int>(std::floor(x / (double)chunk_size));
    int cz = static_cast<int>(std::floor(z / (double)chunk_size));

    float bestDist2 = std::numeric_limits<float>::infinity();
    glm::vec2 bestPoint(x, z);
    int bestScore = 0; // intersection score (higher = better)

    int padding = 8; // match usage in terrain
    const float intersectionRadius = 4.0f; // units to consider nearby roads as intersection

    // Accumulate all nearby poly points to enable intersection scoring
    std::vector<glm::vec2> nearbyPoints;

    for (int dz = -search_radius_chunks; dz <= search_radius_chunks; ++dz) {
        for (int dx = -search_radius_chunks; dx <= search_radius_chunks; ++dx) {
            int nx = cx + dx;
            int nz = cz + dz;
            // generate polylines for this chunk (deterministic)
            auto polylines = generate_perlin_roads_chunk_polylines(nx, nz, chunk_size, padding, 64, 200, 1.0f, 0.02f, 1337, 4, 1.0f);
            for (auto &poly : polylines) {
                // store points for intersection checks
                for (auto &pt : poly) nearbyPoints.push_back(pt);

                // check segments for closer projection
                if (poly.size() < 2) continue;
                for (size_t i = 0; i + 1 < poly.size(); ++i) {
                    glm::vec2 a = poly[i];
                    glm::vec2 b = poly[i+1];
                    // project (x,z) onto segment ab
                    glm::vec2 ab = b - a;
                    float abLen2 = glm::dot(ab, ab);
                    float t = 0.0f;
                    if (abLen2 > 0.00001f) {
                        t = glm::dot(glm::vec2(x, z) - a, ab) / abLen2;
                        t = std::max(0.0f, std::min(1.0f, t));
                    }
                    glm::vec2 proj = a + ab * t;
                    float dxp = proj.x - x;
                    float dzp = proj.y - z;
                    float d2 = dxp*dxp + dzp*dzp;

                    // compute intersection score = number of other points within intersectionRadius
                    int score = 0;
                    for (auto &other : nearbyPoints) {
                        float ddx = other.x - proj.x;
                        float ddz = other.y - proj.y;
                        if (ddx*ddx + ddz*ddz <= intersectionRadius * intersectionRadius) ++score;
                    }

                    // prefer higher score (intersections), otherwise smaller distance
                    if (score > bestScore || (score == bestScore && d2 < bestDist2)) {
                        bestScore = score;
                        bestDist2 = d2;
                        bestPoint = proj;
                    }
                }
            }
        }
    }

    return bestPoint;
}