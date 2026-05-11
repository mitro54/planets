#pragma once
#include "planet.h"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>

struct Star { double x, y; };

struct ChunkCoord {
    int64_t cx, cy;
    bool operator==(const ChunkCoord& o) const { return cx == o.cx && cy == o.cy; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h1 = std::hash<int64_t>{}(c.cx);
        size_t h2 = std::hash<int64_t>{}(c.cy);
        return h1 ^ (h2 * 2654435761ULL);
    }
};

struct Chunk {
    ChunkCoord coord;
    std::vector<Planet> planets;
    std::vector<Star> stars;
};

class ChunkManager {
public:
    static constexpr double CHUNK_SIZE = 2000.0; // km per chunk side

    ChunkManager() = default;
    void update(double playerX, double playerY, double zoom, int cols, int rows);
    const std::vector<Planet*>& getActivePlanets();
    const std::vector<Star*>& getActiveStars();
    static ChunkCoord worldToChunk(double wx, double wy);
    size_t loadedCount() const { return chunks_.size(); }
    int totalPlanets() const;

private:
    void rebuildCaches();
    void generateChunk(ChunkCoord coord);
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> chunks_;
    std::vector<Planet*> activePlanets_;
    std::vector<Star*> activeStars_;
    ChunkCoord lastPlayerChunk_{INT64_MAX, INT64_MAX};
    int64_t lastRadius_ = 0;
    bool dirty_ = true;
};
