#include "buildings.h"
#include <core/params/params.h>
#include <cmath>
#include <cstdint>
#include <algorithm>

// 64-bit splitmix hash to produce deterministic pseudo-random numbers from integers
static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Deterministic float in [0,1) from integer inputs
static double deterministic_unit(int64_t a, int64_t b, int64_t c) {
    uint64_t h = static_cast<uint64_t>(a);
    h = splitmix64(h ^ static_cast<uint64_t>(b));
    h = splitmix64(h ^ static_cast<uint64_t>(c));
    return static_cast<double>(h & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53);
}

// Deterministic float in [0,1) from 4 integer inputs
static double deterministic_unit(int64_t a, int64_t b, int64_t c, int64_t d) {
    uint64_t h = static_cast<uint64_t>(a);
    h = splitmix64(h ^ static_cast<uint64_t>(b));
    h = splitmix64(h ^ static_cast<uint64_t>(c));
    h = splitmix64(h ^ static_cast<uint64_t>(d));
    return static_cast<double>(h & ((1ULL << 53) - 1)) / static_cast<double>(1ULL << 53);
}

// Generate a rectangular building shape
static std::vector<glm::vec2> generate_rectangle_shape(int width, int height) {
    return {
        glm::vec2(0, 0),
        glm::vec2(width, 0),
        glm::vec2(width, height),
        glm::vec2(0, height)
    };
}

// Generate an L-shaped building
static std::vector<glm::vec2> generate_l_shape(int width, int height, int seed) {
    double h1 = deterministic_unit(seed, 0, 0);
    double h2 = deterministic_unit(seed, 0, 1);
    
    int w1 = static_cast<int>(width * (0.4 + h1 * 0.3));
    int h1_val = static_cast<int>(height * (0.4 + h2 * 0.3));
    
    return {
        glm::vec2(0, 0),
        glm::vec2(w1, 0),
        glm::vec2(w1, h1_val),
        glm::vec2(width, h1_val),
        glm::vec2(width, height),
        glm::vec2(0, height)
    };
}

// Generate a T-shaped building
static std::vector<glm::vec2> generate_t_shape(int width, int height, int seed) {
    double h1 = deterministic_unit(seed, 0, 0);
    double h2 = deterministic_unit(seed, 0, 1);
    
    int w_center = static_cast<int>(width * (0.3 + h1 * 0.2));
    int h_top = static_cast<int>(height * (0.3 + h2 * 0.2));
    
    int x_offset = (width - w_center) / 2;
    
    return {
        glm::vec2(0, 0),
        glm::vec2(width, 0),
        glm::vec2(width, h_top),
        glm::vec2(x_offset + w_center, h_top),
        glm::vec2(x_offset + w_center, height),
        glm::vec2(x_offset, height),
        glm::vec2(x_offset, h_top),
        glm::vec2(0, h_top)
    };
}



// Point in polygon test using ray casting
static bool point_in_polygon(const glm::vec2& point, const std::vector<glm::vec2>& polygon) {
    int n = polygon.size();
    bool inside = false;
    
    float x = point.x, y = point.y;
    float p1x = polygon[0].x, p1y = polygon[0].y;
    
    for (int i = 1; i <= n; ++i) {
        float p2x = polygon[i % n].x;
        float p2y = polygon[i % n].y;
        
        if (y > std::min(p1y, p2y)) {
            if (y <= std::max(p1y, p2y)) {
                if (x <= std::max(p1x, p2x)) {
                    float xinters;
                    if (p1y != p2y) {
                        xinters = (y - p1y) * (p2x - p1x) / (p2y - p1y) + p1x;
                    } else {
                        xinters = p1x;
                    }
                    if (p1x == p2x || x <= xinters) {
                        inside = !inside;
                    }
                }
            }
        }
        p1x = p2x;
        p1y = p2y;
    }
    
    return inside;
}

// Terrain-side uses a point-in-polygon that expects points in the same coordinate system.
static bool pointInPolygon2D(const glm::vec2& point, const std::vector<glm::vec2>& polygon) {
    return point_in_polygon(point, polygon);
}

// Try to place a building on the grid
static bool try_place_building(std::vector<std::vector<int>>& grid, 
                               int center_x, int center_y,
                               const std::vector<glm::vec2>& shape_points) {
    int grid_height = grid.size();
    int grid_width = grid[0].size();
    
    // Find min/max to center the shape
    float min_x = shape_points[0].x, max_x = shape_points[0].x;
    float min_y = shape_points[0].y, max_y = shape_points[0].y;
    
    for (const auto& p : shape_points) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    
    float half_w = (max_x - min_x) / 2.0f;
    float half_h = (max_y - min_y) / 2.0f;
    
    // Translate shape points to world grid coordinates
    std::vector<glm::vec2> translated_points;
    for (const auto& p : shape_points) {
        translated_points.push_back(glm::vec2(
            center_x - half_w + p.x,
            center_y - half_h + p.y
        ));
    }
    
    // Find bounding box
    int bbox_min_x = std::max(0, static_cast<int>(center_x - half_w));
    int bbox_max_x = std::min(grid_width, static_cast<int>(center_x + half_w) + 1);
    int bbox_min_y = std::max(0, static_cast<int>(center_y - half_h));
    int bbox_max_y = std::min(grid_height, static_cast<int>(center_y + half_h) + 1);
    
    if (bbox_min_x >= bbox_max_x || bbox_min_y >= bbox_max_y) {
        return false;
    }
    
    // Collect all pixels that would be part of the building
    std::vector<std::pair<int, int>> building_pixels;
    for (int y = bbox_min_y; y < bbox_max_y; ++y) {
        for (int x = bbox_min_x; x < bbox_max_x; ++x) {
            if (point_in_polygon(glm::vec2(x, y), translated_points)) {
                building_pixels.push_back({x, y});
            }
        }
    }
    
    if (building_pixels.empty()) {
        return false;
    }
    
    // Check if any pixel intersects with roads or other buildings
    for (const auto& [x, y] : building_pixels) {
        if (grid[y][x] != 0) {  // Not terrain
            return false;
        }
    }
    
    // Check for road adjacency (accessibility)
    bool has_road_access = false;
    for (const auto& [x, y] : building_pixels) {
        // Check 4-connected neighbors
        const int dx[] = {-1, 1, 0, 0};
        const int dy[] = {0, 0, -1, 1};
        
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            
            if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height) {
                if (grid[ny][nx] == 1 || grid[ny][nx] == 2 || grid[ny][nx] == 3) {
                    has_road_access = true;
                    break;
                }
            }
        }
        if (has_road_access) break;
    }
    
    if (!has_road_access) {
        return false;
    }
    
    // Place the building
    for (const auto& [x, y] : building_pixels) {
        grid[y][x] = 4;  // BUILDING type
    }
    
    return true;
}

std::vector<BuildingShape> generate_buildings_grid(std::vector<std::vector<int>>& grid,
                                                    int chunk_x, int chunk_y,
                                                    int chunk_size,
                                                    int padding,
                                                    float density,
                                                    int seed) {
    const auto& building = CoreParams::GetBuildingParams();
    std::vector<BuildingShape> buildings;
    
    int grid_height = grid.size();
    int grid_width = grid[0].size();
    
    // World offset
    int wx = chunk_x * chunk_size - padding;
    int wy = chunk_y * chunk_size - padding;
    
    // Find all road pixels (highways, roads, streets)
    std::vector<std::pair<int, int>> road_pixels;
    for (int y = 0; y < grid_height; ++y) {
        for (int x = 0; x < grid_width; ++x) {
            if (grid[y][x] == 1 || grid[y][x] == 2 || grid[y][x] == 3) {
                road_pixels.push_back({x, y});
            }
        }
    }
    
    if (road_pixels.empty()) {
        return buildings;
    }
    
    // Determine number of building attempts
    int base_attempts = static_cast<int>(deterministic_unit(seed + 3000, chunk_x, chunk_y) * 50) + 20;
    int num_attempts = static_cast<int>(base_attempts * density);
    
    for (int i = 0; i < num_attempts; ++i) {
        // Select a random road pixel
        double hash_val = deterministic_unit(seed + 3000, chunk_x * 1000 + chunk_y, i);
        int road_idx = static_cast<int>(hash_val * road_pixels.size());
        
        auto [road_x, road_y] = road_pixels[road_idx];
        int road_type = grid[road_y][road_x];
        
        // Determine building size based on road type
        int min_size, max_size;
        float min_height, max_height;
        
        if (road_type == 1) {  // Highway
            min_size = static_cast<int>(building.value("highway_min_size", 15));
            max_size = static_cast<int>(building.value("highway_max_size", 30));
            min_height = static_cast<float>(building.value("highway_min_height", 20.0));
            max_height = static_cast<float>(building.value("highway_max_height", 50.0));
        } else if (road_type == 2) {  // Road
            min_size = static_cast<int>(building.value("road_min_size", 10));
            max_size = static_cast<int>(building.value("road_max_size", 20));
            min_height = static_cast<float>(building.value("road_min_height", 10.0));
            max_height = static_cast<float>(building.value("road_max_height", 30.0));
        } else {  // Street
            min_size = static_cast<int>(building.value("street_min_size", 6));
            max_size = static_cast<int>(building.value("street_max_size", 12));
            min_height = static_cast<float>(building.value("street_min_height", 5.0));
            max_height = static_cast<float>(building.value("street_max_height", 15.0));
        }
        
        // Generate building dimensions
        double size_hash = deterministic_unit(seed + 4000, chunk_x * 1000 + chunk_y, i);
        int building_width = min_size + static_cast<int>(size_hash * (max_size - min_size));
        
        double height_hash = deterministic_unit(seed + 4100, chunk_x * 1000 + chunk_y, i);
        int building_height = min_size + static_cast<int>(height_hash * (max_size - min_size));
        
        // Generate building height (3D)
        double height3d_hash = deterministic_unit(seed + 4200, chunk_x * 1000 + chunk_y, i);
        float height_3d = min_height + height3d_hash * (max_height - min_height);
        
        // Choose placement direction
        double side_hash = deterministic_unit(seed + 5000, chunk_x * 1000 + chunk_y, i);
        const int directions[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        int direction_idx = static_cast<int>(side_hash * 4);
        auto [dx, dy] = directions[direction_idx];
        
        // Place building adjacent to road
        int gap = 1;
        int center_x = road_x + dx * (gap + building_width / 2);
        int center_y = road_y + dy * (gap + building_height / 2);
        
        // Determine building shape
        double shape_hash = deterministic_unit(seed + 6000, chunk_x * 1000 + chunk_y, i);
        
        std::vector<glm::vec2> building_shape;
        if (shape_hash < static_cast<double>(building.value("rectangle_probability", 0.4))) {
            building_shape = generate_rectangle_shape(building_width, building_height);
        } else if (shape_hash < static_cast<double>(building.value("l_shape_probability", 0.7))) {
            building_shape = generate_l_shape(building_width, building_height, seed + i);
        } else {
            building_shape = generate_t_shape(building_width, building_height, seed + i);
        }
        
        // Try to place the building
        if (try_place_building(grid, center_x, center_y, building_shape)) {
            BuildingShape bs;
            // Store points in chunk-grid coordinates (translated), so downstream systems
            // can associate per-cell BUILDING pixels with a specific building + height.
            float min_x = building_shape[0].x, max_x = building_shape[0].x;
            float min_y = building_shape[0].y, max_y = building_shape[0].y;
            for (const auto& p : building_shape) {
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
            }

            float half_w = (max_x - min_x) / 2.0f;
            float half_h = (max_y - min_y) / 2.0f;

            bs.points.reserve(building_shape.size());
            for (const auto& p : building_shape) {
                bs.points.push_back(glm::vec2(
                    center_x - half_w + p.x,
                    center_y - half_h + p.y
                ));
            }
            bs.height = height_3d;
            buildings.push_back(bs);
        }
    }
    
    return buildings;
}

namespace BuildingsMesh {

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
    std::vector<uint8_t>& outDoorMask,
    std::vector<int16_t>& outOwnerGrid,
    std::vector<BuildingRampPlan>& outRampPlans) {

    outSolidVertices.clear();
    outSolidIndices.clear();
    outWindowVertices.clear();
    outWindowIndices.clear();
    outOwnerGrid.clear();
    outRampPlans.clear();

    const int cellsPerChunk = chunkSize - 1;
    if (cellsPerChunk <= 0) {
        outDoorMask.clear();
        return;
    }

    outDoorMask.assign(static_cast<size_t>(cellsPerChunk * cellsPerChunk), 0);

    // Rasterize building heights and ownership (which BuildingShape owns each BUILDING cell).
    std::vector<std::vector<float>> heightGrid(chunkSize, std::vector<float>(chunkSize, 0.0f));
    std::vector<std::vector<int>> ownerGrid(chunkSize, std::vector<int>(chunkSize, -1));

    auto cellType = [&](int x, int z) -> int {
        if (z < 0 || z >= static_cast<int>(roadGrid.size())) return 0;
        if (x < 0 || x >= static_cast<int>(roadGrid[z].size())) return 0;
        return roadGrid[z][x];
    };

    for (int bi = 0; bi < static_cast<int>(buildings.size()); ++bi) {
        const auto& building = buildings[bi];
        if (building.points.size() < 3) continue;

        float minX = building.points[0].x;
        float maxX = building.points[0].x;
        float minY = building.points[0].y;
        float maxY = building.points[0].y;
        for (const auto& p : building.points) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        int x1 = std::min(chunkSize - 1, static_cast<int>(std::ceil(maxX)));
        int z0 = std::max(0, static_cast<int>(std::floor(minY)));
        int z1 = std::min(chunkSize - 1, static_cast<int>(std::ceil(maxY)));

        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (cellType(x, z) != 4) continue; // BUILDING

                glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
                if (!pointInPolygon2D(p, building.points)) continue;

                if (building.height > heightGrid[z][x]) {
                    heightGrid[z][x] = building.height;
                    ownerGrid[z][x] = bi;
                }
            }
        }
    }

    // Export owner grid for gameplay sampling (cells only, not the outermost vertex row/col).
    outOwnerGrid.resize(static_cast<size_t>(cellsPerChunk * cellsPerChunk), static_cast<int16_t>(-1));
    for (int z = 0; z < cellsPerChunk; ++z) {
        for (int x = 0; x < cellsPerChunk; ++x) {
            const int bi = ownerGrid[z][x];
            outOwnerGrid[static_cast<size_t>(z * cellsPerChunk + x)] = static_cast<int16_t>(bi);
        }
    }

    const auto& buildingParams = CoreParams::GetBuildingParams();
    const auto& playerParams = CoreParams::GetPlayerParams();
    const int seed = buildingParams.value("seed", 42);

    // Facade cell sizing is driven by player height.
    const float playerHeight = static_cast<float>(playerParams.value("eye_height", 1.6f));
    const float facadeCellFactor = static_cast<float>(buildingParams.value("facade_cell_size_factor", 0.5f));
    const float facadeCellSize = std::max(0.05f, facadeCellFactor * playerHeight);

    // Door dimensions in facade cells.
    const int doorCellsHigh = std::max(1, buildingParams.value("door_cells_high", 3));
    const int doorCellsWide = std::max(1, buildingParams.value("door_cells_wide", 5));
    const float doorHeight = static_cast<float>(doorCellsHigh) * facadeCellSize;

    // Window probabilities (per facade cell).
    const float windowProbUpper = buildingParams.value("window_probability", 0.18f);
    const float windowProbGround = buildingParams.value("window_probability_ground", 0.03f);

    // Interior floors / stairs.
    const float floorHeight = std::max(0.5f, static_cast<float>(buildingParams.value("floor_height", 3.0f)));
    const int maxFloors = std::max(1, buildingParams.value("max_floors", 16));
    // Ramp slope/length: prefer a user-facing angle (degrees). If absent/invalid, fall back to explicit cell length.
    int rampLengthCells = std::clamp(buildingParams.value("stair_ramp_length_cells", 4), 2, 16);
    const int rampWidthCells = std::clamp(buildingParams.value("stair_ramp_width_cells", 2), 1, 8);
    const int rampLandingCells = std::clamp(buildingParams.value("stair_ramp_landing_cells", 1), 0, 8);
    const int rampTopInsetCells = std::clamp(buildingParams.value("stair_ramp_top_inset_cells", 0), 0, 8);

    // Pick one door edge per building, biased to edges adjacent to a road.
    struct DoorCandidate { int x = 0; int z = 0; int dir = 0; };
    std::vector<std::vector<DoorCandidate>> doorCandidates;
    doorCandidates.resize(buildings.size());

    for (int z = 0; z < cellsPerChunk; ++z) {
        for (int x = 0; x < cellsPerChunk; ++x) {
            if (cellType(x, z) != 4) continue; // BUILDING
            const int bi = ownerGrid[z][x];
            if (bi < 0) continue;

            // N,S,W,E with neighbor cell types.
            const int nx[4] = { x, x, x - 1, x + 1 };
            const int nz[4] = { z - 1, z + 1, z, z };
            for (int dir = 0; dir < 4; ++dir) {
                const int t = cellType(nx[dir], nz[dir]);
                if (t == 4) continue;
                if (t == 1 || t == 2 || t == 3) { // HIGHWAY/ROAD/STREET
                    doorCandidates[bi].push_back({x, z, dir});
                }
            }
        }
    }

    auto isRoadLike = [&](int t) -> bool {
        return t == 1 || t == 2 || t == 3;
    };

    auto isValidDoorCell = [&](int bi, int x, int z, int dir) -> bool {
        if (x < 0 || z < 0 || x >= cellsPerChunk || z >= cellsPerChunk) return false;
        if (cellType(x, z) != 4) return false;
        if (ownerGrid[z][x] != bi) return false;

        const int nx[4] = { x, x, x - 1, x + 1 };
        const int nz[4] = { z - 1, z + 1, z, z };
        const int t = cellType(nx[dir], nz[dir]);
        return isRoadLike(t);
    };

    auto candidateSupportsWidth = [&](int bi, const DoorCandidate& dc) -> bool {
        const int half = doorCellsWide / 2;
        for (int off = -half; off <= half; ++off) {
            int xx = dc.x;
            int zz = dc.z;
            if (dc.dir == 0 || dc.dir == 1) {
                xx += off;
            } else {
                zz += off;
            }
            if (!isValidDoorCell(bi, xx, zz, dc.dir)) return false;
        }
        return true;
    };

    // Decide chosen door for each building deterministically (prefer candidates that can fit door width).
    for (int bi = 0; bi < static_cast<int>(doorCandidates.size()); ++bi) {
        auto& cands = doorCandidates[bi];
        if (cands.empty()) continue;

        std::vector<DoorCandidate> wideCands;
        wideCands.reserve(cands.size());
        for (const auto& dc : cands) {
            if (candidateSupportsWidth(bi, dc)) wideCands.push_back(dc);
        }

        const auto& pickFrom = (!wideCands.empty()) ? wideCands : cands;
        const double r = deterministic_unit(seed + 9001, chunkCx, chunkCz, bi);
        const int idx = std::clamp(static_cast<int>(std::floor(r * pickFrom.size())), 0, static_cast<int>(pickFrom.size()) - 1);
        const DoorCandidate dc = pickFrom[idx];

        // Mark multiple adjacent boundary cells as door openings to achieve the requested width.
        const int half = doorCellsWide / 2;
        for (int off = -half; off <= half; ++off) {
            int xx = dc.x;
            int zz = dc.z;
            if (dc.dir == 0 || dc.dir == 1) {
                xx += off;
            } else {
                zz += off;
            }

            if (!isValidDoorCell(bi, xx, zz, dc.dir)) continue;

            const int flatIdx = zz * cellsPerChunk + xx;
            outDoorMask[static_cast<size_t>(flatIdx)] |= DoorBitForDir(dc.dir);
        }
    }

    auto heightAt = [&](int x, int z) -> float {
        if (x < 0 || z < 0 || x >= chunkSize || z >= chunkSize) return 0.0f;
        if (cellType(x, z) != 4) return 0.0f;
        float hh = heightGrid[z][x];
        if (hh <= 0.0f) hh = buildingParams.value("road_min_height", 10.0f);
        return hh;
    };

    enum class MeshKind { Solid, Windows };

    auto pushVertex = [&](MeshKind mk, float x, float y, float z) {
        auto& v = (mk == MeshKind::Solid) ? outSolidVertices : outWindowVertices;
        v.push_back(x);
        v.push_back(y);
        v.push_back(z);
    };

    auto addQuad = [&](MeshKind mk, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c_, const glm::vec3& d) {
        auto& v = (mk == MeshKind::Solid) ? outSolidVertices : outWindowVertices;
        auto& idx = (mk == MeshKind::Solid) ? outSolidIndices : outWindowIndices;
        unsigned int baseIdx = static_cast<unsigned int>(v.size() / 3);
        pushVertex(mk, a.x, a.y, a.z);
        pushVertex(mk, b.x, b.y, b.z);
        pushVertex(mk, c_.x, c_.y, c_.z);
        pushVertex(mk, d.x, d.y, d.z);
        idx.push_back(baseIdx + 0);
        idx.push_back(baseIdx + 2);
        idx.push_back(baseIdx + 1);
        idx.push_back(baseIdx + 1);
        idx.push_back(baseIdx + 2);
        idx.push_back(baseIdx + 3);
    };

    auto isWindowForFacadeCell = [&](int cellX, int cellZ, int dir, int verticalCellIdx) -> bool {
        const int globalCellX = chunkCx * cellsPerChunk + cellX;
        const int globalCellZ = chunkCz * cellsPerChunk + cellZ;
        const double r = deterministic_unit(seed + 1337 + dir * 19, globalCellX, globalCellZ, verticalCellIdx);
        const float p = (verticalCellIdx == 0) ? windowProbGround : windowProbUpper;
        return (r < p);
    };

    const float eps = 1e-4f;

    // Helper to read a terrain vertex (grid coords in [0,chunkSize-1]).
    auto terrainV = [&](int vx, int vz) -> glm::vec3 {
        vx = std::clamp(vx, 0, chunkSize - 1);
        vz = std::clamp(vz, 0, chunkSize - 1);
        const int idx = vz * chunkSize + vx;
        return glm::vec3(
            terrainVertices[idx * 3 + 0],
            terrainVertices[idx * 3 + 1],
            terrainVertices[idx * 3 + 2]
        );
    };

    // Estimate XZ cell size in world units (typically ~1.0).
    float cellWorldSizeXZ = 1.0f;
    if (chunkSize >= 2) {
        const glm::vec3 o = terrainV(0, 0);
        const glm::vec3 ox = terrainV(1, 0);
        const glm::vec3 oz = terrainV(0, 1);
        const float dx = std::abs(ox.x - o.x) + std::abs(ox.z - o.z);
        const float dz = std::abs(oz.x - o.x) + std::abs(oz.z - o.z);
        const float est = std::max(dx, dz);
        if (est > 1e-4f) cellWorldSizeXZ = est;
    }

    // If an angle is provided, derive ramp length in cells from trig: run = rise / tan(theta).
    // Smaller angle => longer / less steep ramp.
    {
        const double angleDeg = buildingParams.value("stair_ramp_angle_degrees", 0.0);
        if (angleDeg > 1.0 && angleDeg < 89.0) {
            const double theta = angleDeg * (M_PI / 180.0);
            const double runWorld = static_cast<double>(floorHeight) / std::tan(theta);
            const double denom = std::max(1e-4, static_cast<double>(cellWorldSizeXZ));
            const int derivedCells = static_cast<int>(std::ceil(runWorld / denom));
            rampLengthCells = std::clamp(derivedCells, 2, 16);
        }
    }

    auto buildingFloorCount = [&](int bi) -> int {
        if (bi < 0 || bi >= static_cast<int>(buildings.size())) return 1;
        const float h = buildings[bi].height;
        const int floors = std::max(1, static_cast<int>(std::floor(h / floorHeight)));
        return std::clamp(floors, 1, maxFloors);
    };

    struct RampPlan {
        bool valid = false;
        // wall direction the ramp faces: 0=N,1=S,2=W,3=E
        int wallDir = 0;
        // top (high) ramp cell in cell coordinates (may be inset from the outer wall)
        int topX = 0;
        int topZ = 0;
        // ramp runs inward for `len` cells, leaving `landing` cells flat beyond the base
        int len = 0;
        int width = 0;
        int landing = 0;
        // width offset direction: +1 means +perp, -1 means -perp
        int perpSign = +1;

        bool CoversCell(int x, int z) const {
            if (!valid) return false;

            // Compute inward direction (dx,dz) and perpendicular direction (px,pz).
            int dx = 0, dz = 0;
            int px = 0, pz = 0;
            switch (wallDir) {
                case 0: dx = 0; dz = +1; px = +1; pz = 0; break; // N wall, inward +z, width +x
                case 1: dx = 0; dz = -1; px = +1; pz = 0; break; // S wall, inward -z, width +x
                case 2: dx = +1; dz = 0; px = 0; pz = +1; break; // W wall, inward +x, width +z
                case 3: dx = -1; dz = 0; px = 0; pz = +1; break; // E wall, inward -x, width +z
                default: break;
            }
            px *= perpSign;
            pz *= perpSign;

            // Ramp footprint is an LxW rectangle of cells starting at top cell and extending inward.
            for (int t = 0; t < len; ++t) {
                for (int w = 0; w < width; ++w) {
                    const int cx = topX + dx * t + px * w;
                    const int cz = topZ + dz * t + pz * w;
                    if (cx == x && cz == z) return true;
                }
            }
            return false;
        }
    };

    std::vector<RampPlan> rampPlans;
    rampPlans.resize(buildings.size());

    auto ownedBy = [&](int bi, int x, int z) -> bool {
        if (x < 0 || z < 0 || x >= cellsPerChunk || z >= cellsPerChunk) return false;
        if (cellType(x, z) != 4) return false;
        return ownerGrid[z][x] == bi;
    };

    auto isBoundaryCell = [&](int x, int z, int dir) -> bool {
        const int nx[4] = { x, x, x - 1, x + 1 };
        const int nz[4] = { z - 1, z + 1, z, z };
        return cellType(nx[dir], nz[dir]) != 4;
    };

    auto canPlaceRamp = [&](int bi, int wallX, int wallZ, int wallDir, int perpSign) -> bool {
        // Inward direction and perpendicular direction.
        int dx = 0, dz = 0;
        int px = 0, pz = 0;
        switch (wallDir) {
            case 0: dx = 0; dz = +1; px = +1; pz = 0; break;
            case 1: dx = 0; dz = -1; px = +1; pz = 0; break;
            case 2: dx = +1; dz = 0; px = 0; pz = +1; break;
            case 3: dx = -1; dz = 0; px = 0; pz = +1; break;
            default: break;
        }
        px *= perpSign;
        pz *= perpSign;

        const int topX = wallX + dx * rampTopInsetCells;
        const int topZ = wallZ + dz * rampTopInsetCells;

        // Need a full run of (len + landing) cells inward from the ramp top cell, and width cells perpendicular.
        const int needT = rampLengthCells + rampLandingCells;
        for (int t = 0; t < needT; ++t) {
            for (int w = 0; w < rampWidthCells; ++w) {
                const int cx = topX + dx * t + px * w;
                const int cz = topZ + dz * t + pz * w;
                if (!ownedBy(bi, cx, cz)) return false;
            }
        }
        return true;
    };

    // Choose one ramp per building: top is near a wall (optionally inset), base stays inward.
    for (int bi = 0; bi < static_cast<int>(buildings.size()); ++bi) {
        const int floors = buildingFloorCount(bi);
        if (floors < 2) continue;

        struct Cand { int x, z, dir, perpSign; };
        std::vector<Cand> candidates;

        for (int z = 0; z < cellsPerChunk; ++z) {
            for (int x = 0; x < cellsPerChunk; ++x) {
                if (!ownedBy(bi, x, z)) continue;
                for (int dir = 0; dir < 4; ++dir) {
                    if (!isBoundaryCell(x, z, dir)) continue;
                    // Try width on either side of the top cell.
                    for (int ps : {+1, -1}) {
                        if (canPlaceRamp(bi, x, z, dir, ps)) {
                            candidates.push_back({x, z, dir, ps});
                        }
                    }
                }
            }
        }

        if (candidates.empty()) continue;

        // Deterministic pick.
        const double r = deterministic_unit(seed + 5555, chunkCx, chunkCz, bi);
        const int idx = std::clamp(static_cast<int>(std::floor(r * candidates.size())), 0, static_cast<int>(candidates.size()) - 1);
        const Cand c = candidates[idx];

        // Convert boundary (wall-adjacent) cell to actual ramp top cell (possibly inset).
        int dx = 0, dz = 0;
        switch (c.dir) {
            case 0: dx = 0; dz = +1; break;
            case 1: dx = 0; dz = -1; break;
            case 2: dx = +1; dz = 0; break;
            case 3: dx = -1; dz = 0; break;
            default: break;
        }
        const int rampTopX = c.x + dx * rampTopInsetCells;
        const int rampTopZ = c.z + dz * rampTopInsetCells;

        RampPlan rp;
        rp.valid = true;
        rp.wallDir = c.dir;
        rp.topX = rampTopX;
        rp.topZ = rampTopZ;
        rp.len = rampLengthCells;
        rp.width = rampWidthCells;
        rp.landing = rampLandingCells;
        rp.perpSign = c.perpSign;
        rampPlans[bi] = rp;
    }

    // Export ramp plans for gameplay sampling.
    outRampPlans.resize(buildings.size());
    for (int bi = 0; bi < static_cast<int>(buildings.size()); ++bi) {
        BuildingRampPlan out;
        if (bi >= 0 && bi < static_cast<int>(rampPlans.size())) {
            const RampPlan& rp = rampPlans[bi];
            out.valid = rp.valid;
            out.wallDir = rp.wallDir;
            out.topX = rp.topX;
            out.topZ = rp.topZ;
            out.len = rp.len;
            out.width = rp.width;
            out.landing = rp.landing;
            out.perpSign = rp.perpSign;
        }
        outRampPlans[bi] = out;
    }

    for (int z = 0; z < cellsPerChunk; ++z) {
        for (int x = 0; x < cellsPerChunk; ++x) {
            if (cellType(x, z) != 4) continue;

            float h = heightAt(x, z);
            if (h <= 0.0f) continue;

            const int i00 = (z + 0) * chunkSize + (x + 0);
            const int i10 = (z + 0) * chunkSize + (x + 1);
            const int i01 = (z + 1) * chunkSize + (x + 0);
            const int i11 = (z + 1) * chunkSize + (x + 1);

            glm::vec3 v00(terrainVertices[i00 * 3], terrainVertices[i00 * 3 + 1], terrainVertices[i00 * 3 + 2]);
            glm::vec3 v10(terrainVertices[i10 * 3], terrainVertices[i10 * 3 + 1], terrainVertices[i10 * 3 + 2]);
            glm::vec3 v01(terrainVertices[i01 * 3], terrainVertices[i01 * 3 + 1], terrainVertices[i01 * 3 + 2]);
            glm::vec3 v11(terrainVertices[i11 * 3], terrainVertices[i11 * 3 + 1], terrainVertices[i11 * 3 + 2]);

            // Floor: fill the gap where terrain mesh omits BUILDING cells.
            {
                constexpr float kFloorEpsY = 0.015f;
                glm::vec3 f00(v00.x, v00.y + kFloorEpsY, v00.z);
                glm::vec3 f10(v10.x, v10.y + kFloorEpsY, v10.z);
                glm::vec3 f01(v01.x, v01.y + kFloorEpsY, v01.z);
                glm::vec3 f11(v11.x, v11.y + kFloorEpsY, v11.z);
                addQuad(MeshKind::Solid, f00, f10, f01, f11);
            }

            // Interior floors (multiple storeys). These are render-only surfaces.
            {
                const int bi = ownerGrid[z][x];
                const int floors = buildingFloorCount(bi);
                if (floors > 1) {
                    constexpr float kInteriorFloorEpsY = 0.012f;
                    for (int fi = 1; fi < floors; ++fi) {
                        // If there's a ramp underneath/through this floor level, cut a hole.
                        if (bi >= 0 && bi < static_cast<int>(rampPlans.size()) && rampPlans[bi].CoversCell(x, z)) {
                            continue;
                        }
                        const float yOff = static_cast<float>(fi) * floorHeight;
                        if (yOff >= h - 0.05f) break;
                        glm::vec3 p00(v00.x, v00.y + yOff + kInteriorFloorEpsY, v00.z);
                        glm::vec3 p10(v10.x, v10.y + yOff + kInteriorFloorEpsY, v10.z);
                        glm::vec3 p01(v01.x, v01.y + yOff + kInteriorFloorEpsY, v01.z);
                        glm::vec3 p11(v11.x, v11.y + yOff + kInteriorFloorEpsY, v11.z);
                        addQuad(MeshKind::Solid, p00, p10, p01, p11);
                    }
                }
            }

            // Roof
            glm::vec3 t00(v00.x, v00.y + h, v00.z);
            glm::vec3 t10(v10.x, v10.y + h, v10.z);
            glm::vec3 t01(v01.x, v01.y + h, v01.z);
            glm::vec3 t11(v11.x, v11.y + h, v11.z);
            addQuad(MeshKind::Solid, t00, t10, t01, t11);

            auto emitFacade = [&](const glm::vec3& edgeA0, const glm::vec3& edgeB0, float neighborH, int dir, uint8_t doorBits) {
                if (!(h > neighborH + eps)) return;

                const float cellH = facadeCellSize;
                if (cellH <= 0.01f) {
                    glm::vec3 a(edgeA0.x, edgeA0.y + neighborH, edgeA0.z);
                    glm::vec3 b(edgeB0.x, edgeB0.y + neighborH, edgeB0.z);
                    glm::vec3 c(edgeA0.x, edgeA0.y + h, edgeA0.z);
                    glm::vec3 d(edgeB0.x, edgeB0.y + h, edgeB0.z);
                    addQuad(MeshKind::Solid, a, b, c, d);
                    return;
                }

                const bool isDoorEdge = (doorBits & DoorBitForDir(dir)) != 0;

                const float startY = std::max(0.0f, neighborH);
                const int vCells = std::max(1, static_cast<int>(std::ceil((h - startY) / cellH)));
                for (int vi = 0; vi < vCells; ++vi) {
                    float sliceY0 = startY + static_cast<float>(vi) * cellH;
                    float sliceY1 = std::min(startY + static_cast<float>(vi + 1) * cellH, h);
                    if (sliceY1 <= sliceY0 + 1e-5f) continue;

                    if (isDoorEdge && sliceY0 < doorHeight) {
                        continue;
                    }

                    const bool isWindow = isWindowForFacadeCell(x, z, dir, vi);
                    glm::vec3 a(edgeA0.x, edgeA0.y + sliceY0, edgeA0.z);
                    glm::vec3 b(edgeB0.x, edgeB0.y + sliceY0, edgeB0.z);
                    glm::vec3 c(edgeA0.x, edgeA0.y + sliceY1, edgeA0.z);
                    glm::vec3 d(edgeB0.x, edgeB0.y + sliceY1, edgeB0.z);
                    addQuad(isWindow ? MeshKind::Windows : MeshKind::Solid, a, b, c, d);
                }
            };

            const uint8_t doorBits = outDoorMask[static_cast<size_t>(z * cellsPerChunk + x)];

            // North
            emitFacade(v00, v10, heightAt(x, z - 1), /*dir=*/0, doorBits);
            // South
            emitFacade(v01, v11, heightAt(x, z + 1), /*dir=*/1, doorBits);
            // West
            emitFacade(v00, v01, heightAt(x - 1, z), /*dir=*/2, doorBits);
            // East
            emitFacade(v10, v11, heightAt(x + 1, z), /*dir=*/3, doorBits);
        }
    }

    // Simple sloped ramps inside buildings to connect consecutive floors.
    // Geometry-only (no collision/physics integration).
    {
        constexpr float kRampEpsY = 0.02f;

        for (int bi = 0; bi < static_cast<int>(buildings.size()); ++bi) {
            const int floors = buildingFloorCount(bi);
            if (floors < 2) continue;
            if (bi < 0 || bi >= static_cast<int>(rampPlans.size())) continue;
            const RampPlan& rp = rampPlans[bi];
            if (!rp.valid) continue;

            // Inward direction and perpendicular direction.
            int dx = 0, dz = 0;
            int px = 0, pz = 0;
            switch (rp.wallDir) {
                case 0: dx = 0; dz = +1; px = +1; pz = 0; break;
                case 1: dx = 0; dz = -1; px = +1; pz = 0; break;
                case 2: dx = +1; dz = 0; px = 0; pz = +1; break;
                case 3: dx = -1; dz = 0; px = 0; pz = +1; break;
                default: break;
            }
            px *= rp.perpSign;
            pz *= rp.perpSign;

            // Use wall-touching top edge in vertex coordinates.
            int topVX = rp.topX;
            int topVZ = rp.topZ;
            if (rp.wallDir == 1) topVZ = rp.topZ + 1; // south wall is at z+1
            if (rp.wallDir == 3) topVX = rp.topX + 1; // east wall is at x+1

            const int botVX = topVX + dx * rp.len;
            const int botVZ = topVZ + dz * rp.len;

            // Width in vertex coords spans `width` cells -> +width vertices along perpendicular.
            const int topVX2 = topVX + px * rp.width;
            const int topVZ2 = topVZ + pz * rp.width;
            const int botVX2 = botVX + px * rp.width;
            const int botVZ2 = botVZ + pz * rp.width;

            const glm::vec3 vTop0 = terrainV(topVX, topVZ);
            const glm::vec3 vTop1 = terrainV(topVX2, topVZ2);
            const glm::vec3 vBot0 = terrainV(botVX, botVZ);
            const glm::vec3 vBot1 = terrainV(botVX2, botVZ2);

            for (int fi = 0; fi < floors - 1; ++fi) {
                const float yLow = static_cast<float>(fi) * floorHeight + kRampEpsY;
                const float yHigh = static_cast<float>(fi + 1) * floorHeight + kRampEpsY;

                // Ramp base is inward; top is on the wall.
                glm::vec3 a(vBot0.x, vBot0.y + yLow, vBot0.z);
                glm::vec3 b(vTop0.x, vTop0.y + yHigh, vTop0.z);
                glm::vec3 c(vBot1.x, vBot1.y + yLow, vBot1.z);
                glm::vec3 d(vTop1.x, vTop1.y + yHigh, vTop1.z);
                addQuad(MeshKind::Solid, a, b, c, d);
            }
        }
    }
}

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
    std::vector<uint8_t>& outDoorMask) {

    std::vector<int16_t> ownerGrid;
    std::vector<BuildingRampPlan> rampPlans;
    BuildBuildingMeshesAndDoorsFromGrid(
        chunkCx,
        chunkCz,
        chunkSize,
        roadGrid,
        buildings,
        terrainVertices,
        outSolidVertices,
        outSolidIndices,
        outWindowVertices,
        outWindowIndices,
        outDoorMask,
        ownerGrid,
        rampPlans);
}

} // namespace BuildingsMesh
