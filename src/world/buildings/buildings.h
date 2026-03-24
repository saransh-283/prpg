#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// Building shape representation
struct BuildingShape {
    std::vector<glm::vec2> points;  // 2D polygon points (local coordinates, relative to center)
    float height;                    // Building height
};

// Generate buildings on a grid for a chunk/region in world coordinates.
// Modifies the grid in place to mark building locations with value 4.
// Returns a vector of BuildingShape objects representing the buildings generated.
std::vector<BuildingShape> generate_buildings_grid(std::vector<std::vector<int>>& grid,
                                                    int chunk_x, int chunk_y,
                                                    int chunk_size = 256,
                                                    int padding = 64,
                                                    float density = 1.0f,
                                                    int seed = 42);

namespace BuildingsMesh {

// Door-edge mask: per cell (chunkSize-1 by chunkSize-1), 4-bit mask: N,S,W,E.
enum : uint8_t {
    DoorN = 1 << 0,
    DoorS = 1 << 1,
    DoorW = 1 << 2,
    DoorE = 1 << 3,
};

inline uint8_t DoorBitForDir(int dir) {
    switch (dir) {
        case 0: return DoorN;
        case 1: return DoorS;
        case 2: return DoorW;
        case 3: return DoorE;
        default: return 0;
    }
}

// Builds hollow building shell meshes with facade-cells (windows/walls) and door openings.
// Output is split into two meshes:
// - solid mesh: walls/roof/floor (vertex format [x,y,z])
// - windows mesh: window quads only (vertex format [x,y,z])
void BuildBuildingMeshesAndDoorsFromGrid(
    int chunkCx,
    int chunkCz,
    int chunkSize,
    const std::vector<std::vector<int>>& roadGrid,
    const std::vector<BuildingShape>& buildings,
    const std::vector<float>& terrainVertices,
    std::vector<float>& outSolidVertices,
    std::vector<unsigned int>& outSolidIndices,
    std::vector<float>& outWindowVertices,
    std::vector<unsigned int>& outWindowIndices,
    std::vector<uint8_t>& outDoorMask);

} // namespace BuildingsMesh
