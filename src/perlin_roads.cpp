#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <noise/noise.h>

// Draw a filled disk (circle) at (x, y) with given radius
void draw_disk(std::vector<uint8_t>& canvas, int width, int height, int x, int y, int radius = 2) {
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
float quantized_angle(float x, float y, float scale, int grid_angles, float noise_strength, noise::module::Perlin& perlin) {
    float perlin_val = perlin.GetValue(x * scale, y * scale, 0.0f) * noise_strength;
    float base_angle = std::round(perlin_val * grid_angles) * (2.0f * M_PI / grid_angles);
    return base_angle;
}

// Generate Perlin roads
std::vector<uint8_t> generate_perlin_roads(int canvas_size = 1000, int num_worms = 100, int worm_length = 500, float step_size = 1.0f, float perlin_scale = 0.01f, int seed = 2, int grid_angles = 4, float noise_strength = 1.0f, int road_width = 3) {
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

int main() {
    int canvas_size = 1000;
    auto canvas = generate_perlin_roads(canvas_size);
    if (stbi_write_png("perlin_roads.png", canvas_size, canvas_size, 1, canvas.data(), canvas_size) == 0) {
        std::cerr << "Failed to write perlin_roads.png" << std::endl;
        return 1;
    }
    std::cout << "Saved perlin_roads.png" << std::endl;
    return 0;
}
