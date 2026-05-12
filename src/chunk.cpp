#include "chunk.h"
#include <cmath>
#include <random>

static uint64_t chunkSeed(int64_t cx, int64_t cy) {
    uint64_t h = static_cast<uint64_t>(cx) * 374761393ULL
               + static_cast<uint64_t>(cy) * 668265263ULL;
    h = (h ^ (h >> 13)) * 1274126177ULL;
    h = h ^ (h >> 16);
    return h;
}

ChunkCoord ChunkManager::worldToChunk(double wx, double wy) {
    return {
        static_cast<int64_t>(std::floor(wx / CHUNK_SIZE)),
        static_cast<int64_t>(std::floor(wy / CHUNK_SIZE))
    };
}

// ─── Planet types with rich visual properties ────────────────
struct PlanetTemplate {
    double minRadius, maxRadius;
    double density;
    char symbol;
    const char* color;
    const char* color2;   // surface detail color
    bool canHaveRings;
    int maxMoons;
};

static const PlanetTemplate TEMPLATES[] = {
    // Tiny asteroids — extremely dense rock
    {1.0, 3.0, 3.5, '.', "\033[38;5;245m", "\033[38;5;240m", false, 0},
    // Small rocky
    {4.0, 10.0, 2.5, 'o', "\033[38;5;173m", "\033[38;5;130m", false, 0},
    // Medium rocky (Earth-like)
    {10.0, 20.0, 2.0, 'O', "\033[38;5;75m", "\033[38;5;34m", false, 1},
    // Ice world
    {12.0, 25.0, 1.2, 'O', "\033[1;36m", "\033[38;5;159m", false, 1},
    // Lava world — dense metallic core
    {15.0, 30.0, 2.8, 'O', "\033[1;31m", "\033[38;5;208m", false, 0},
    // Small gas giant (Neptune-like)
    {30.0, 50.0, 0.6, '@', "\033[38;5;27m", "\033[38;5;63m", true, 2},
    // Gas giant (Jupiter-like) — massive but fluffy
    {40.0, 70.0, 0.3, '@', "\033[38;5;208m", "\033[38;5;214m", true, 3},
    // Gas giant (Saturn-like)
    {40.0, 65.0, 0.25, '@', "\033[38;5;220m", "\033[38;5;178m", true, 3},
    // Massive Gas Giant
    {50.0, 85.0, 0.4, '#', "\033[1;35m", "\033[38;5;199m", true, 3},
    // Massive Emerald Giant
    {50.0, 85.0, 0.4, '#', "\033[38;5;46m", "\033[38;5;28m", true, 2},
};
static constexpr int NUM_TEMPLATES = sizeof(TEMPLATES) / sizeof(TEMPLATES[0]);

void ChunkManager::generateChunk(ChunkCoord coord) {
    if (chunks_.count(coord)) return;

    Chunk chunk;
    chunk.coord = coord;

    uint64_t seed = chunkSeed(coord.cx, coord.cy);
    std::mt19937_64 rng(seed);

    double baseX = coord.cx * CHUNK_SIZE;
    double baseY = coord.cy * CHUNK_SIZE;

    // 0-2 planets per chunk (sparser, less crowded)
    std::uniform_int_distribution<int> planetCount(0, 2);
    double margin = 200.0;
    std::uniform_real_distribution<double> posDist(margin, CHUNK_SIZE - margin);

    int nPlanets = planetCount(rng);
    for (int i = 0; i < nPlanets; ++i) {
        Planet p;
        bool valid = false;
        
        // Pick a random template
        std::uniform_int_distribution<int> tmplDist(0, NUM_TEMPLATES - 1);
        const auto& tmpl = TEMPLATES[tmplDist(rng)];
        
        std::uniform_real_distribution<double> radDist(tmpl.minRadius, tmpl.maxRadius);
        p.radius = radDist(rng);
        // Mass = Density * Volume. (Scale by 0.02 so GAME_G=40.0 works perfectly)
        p.mass = tmpl.density * p.radius * p.radius * p.radius * 0.02;

        // Try to place without overlap
        for (int attempts = 0; attempts < 15; ++attempts) {
            p.x = baseX + posDist(rng);
            p.y = baseY + posDist(rng);
            
            bool overlap = false;
            for (const auto& existing : chunk.planets) {
                double dx = existing.x - p.x;
                double dy = existing.y - p.y;
                double dist = std::sqrt(dx*dx + dy*dy);
                if (dist < (existing.radius + p.radius + 250.0)) { // 250km minimum gap
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                valid = true;
                break;
            }
        }
        
        if (!valid) continue; // skip this planet if we couldn't fit it
        p.symbol = tmpl.symbol;
        p.color = tmpl.color;
        p.color2 = tmpl.color2;

        // Rings for eligible planets (50% chance)
        if (tmpl.canHaveRings) {
            std::uniform_int_distribution<int> ringChance(0, 1);
            p.hasRings = (ringChance(rng) == 0);
            if (p.hasRings) {
                p.ringWidth = p.radius * 0.6;
                // Ring color: slightly dimmer than planet
                std::uniform_int_distribution<int> rc(0, 2);
                const char* ringColors[] = {
                    "\033[38;5;223m", "\033[38;5;187m", "\033[38;5;181m"
                };
                p.ringColor = ringColors[rc(rng)];
            }
        }

        // Moons for eligible planets
        if (tmpl.maxMoons > 0) {
            std::uniform_int_distribution<int> moonCount(0, tmpl.maxMoons);
            int nMoons = moonCount(rng);
            std::uniform_real_distribution<double> angleDist(0.0, 2.0 * M_PI);
            for (int m = 0; m < nMoons; ++m) {
                Moon moon;
                // Orbit radius: 1.5x to 3x planet visual radius
                std::uniform_real_distribution<double> orbitDist(
                    p.radius * 1.5 + 3.0, p.radius * 3.0 + 8.0);
                moon.orbitRadius = orbitDist(rng);
                // Orbital speed: faster for closer orbits (Kepler-ish)
                moon.orbitSpeed = 0.3 / std::sqrt(moon.orbitRadius * 0.1);
                moon.startAngle = angleDist(rng);
                // Moon colors
                const char* moonColors[] = {
                    "\033[38;5;252m", "\033[38;5;223m", "\033[38;5;188m",
                    "\033[38;5;146m"
                };
                std::uniform_int_distribution<int> mc(0, 3);
                moon.color = moonColors[mc(rng)];
                p.moons.push_back(moon);
            }
        }

        p.id = std::to_string(static_cast<int64_t>(p.x)) + "_" + std::to_string(static_cast<int64_t>(p.y));

        // 5% chance to be a Black Hole
        std::uniform_int_distribution<int> bhChance(0, 100);
        if (bhChance(rng) < 5) {
            p.isBlackHole = true;
            p.radius = p.radius * 0.5; // Event horizon is smaller
            p.mass = p.mass * 100.0; // Extreme gravity
            p.symbol = ' '; // Void
            p.hasRings = true;
            p.ringWidth = p.radius * 2.0; // Accretion disk
            p.ringColor = "\033[1;35m"; // Bright magenta accretion disk
            p.moons.clear(); // Black holes consume moons!
        }

        chunk.planets.push_back(p);
    }

    // 15-40 background stars per chunk
    std::uniform_int_distribution<int> starCount(15, 40);
    std::uniform_real_distribution<double> starPos(0.0, CHUNK_SIZE);
    int nStars = starCount(rng);
    for (int i = 0; i < nStars; ++i) {
        Star s;
        s.x = baseX + starPos(rng);
        s.y = baseY + starPos(rng);
        chunk.stars.push_back(s);
    }

    // Nebulae: Rare (5% chance) and smaller localized gas clouds
    std::uniform_int_distribution<int> nebChance(0, 100);
    if (nebChance(rng) > 95) {
        Nebula n;
        n.x = baseX + posDist(rng);
        n.y = baseY + posDist(rng);
        n.radius = std::uniform_real_distribution<double>(300.0, 800.0)(rng);
        
        const char* colorsBright[] = {
            "\033[1;36m",     // bright cyan
            "\033[1;35m",     // bright magenta
            "\033[38;5;82m",  // bright neon green
            "\033[38;5;226m", // bright yellow
            "\033[38;5;208m"  // bright orange
        };
        const char* colorsMid[] = {
            "\033[38;5;33m",  // deep bright blue
            "\033[38;5;90m",  // deep magenta
            "\033[38;5;28m",  // deep green
            "\033[38;5;124m", // deep red
            "\033[38;5;55m"   // deep purple
        };
        const char* colorsDark[] = {
            "\033[38;5;17m",  // very dark blue
            "\033[38;5;53m",  // very dark purple
            "\033[38;5;22m",  // very dark green
            "\033[38;5;52m",  // very dark red
            "\033[38;5;235m"  // almost black/gray
        };
        
        std::uniform_int_distribution<int> cDist(0, 4);
        n.color1 = colorsBright[cDist(rng)];
        n.color2 = colorsMid[cDist(rng)];
        n.color3 = colorsDark[cDist(rng)];
        
        const char chars[] = {'.', ',', '`', '\'', ':'};
        std::uniform_int_distribution<int> charC(0, 4);
        n.ch = chars[charC(rng)];
        chunk.nebulae.push_back(n);
    }

    chunks_[coord] = std::move(chunk);
}

void ChunkManager::update(double playerX, double playerY, double zoom, int cols, int rows) {
    ChunkCoord cur = worldToChunk(playerX, playerY);

    double kmPerCol = 1.0 / zoom;
    double kmPerRow = kmPerCol * 2.0; // Terminal characters are ~2x as tall as wide
    
    double visibleWidthKm = cols * kmPerCol;
    double visibleHeightKm = rows * kmPerRow;
    
    // Distance from center to edge of screen
    double maxDistKm = std::max(visibleWidthKm, visibleHeightKm) / 2.0;
    
    // Calculate how many chunks are needed to cover that distance, plus 1 for padding
    int64_t radius = static_cast<int64_t>(std::ceil(maxDistKm / CHUNK_SIZE)) + 1;

    // Sanity clamps
    if (radius < 2) radius = 2;
    if (radius > 15) radius = 15; // prevent memory blowout at extreme zooms

    if (cur == lastPlayerChunk_ && radius == lastRadius_ && !dirty_) return;
    lastPlayerChunk_ = cur;
    lastRadius_ = radius;
    dirty_ = false;

    std::unordered_map<ChunkCoord, bool, ChunkCoordHash> needed;
    for (int64_t dy = -radius; dy <= radius; ++dy)
        for (int64_t dx = -radius; dx <= radius; ++dx)
            needed[{cur.cx + dx, cur.cy + dy}] = true;

    bool changed = false;
    for (auto it = chunks_.begin(); it != chunks_.end(); ) {
        if (!needed.count(it->first)) {
            it = chunks_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    for (auto& [coord, _] : needed) {
        if (!chunks_.count(coord)) {
            generateChunk(coord);
            changed = true;
        }
    }

    if (changed) rebuildCaches();
}

void ChunkManager::rebuildCaches() {
    activePlanets_.clear();
    activeStars_.clear();
    activeNebulae_.clear();
    for (auto& [_, chunk] : chunks_) {
        for (auto& p : chunk.planets) activePlanets_.push_back(&p);
        for (auto& s : chunk.stars) activeStars_.push_back(&s);
        for (auto& n : chunk.nebulae) activeNebulae_.push_back(&n);
    }
}

const std::vector<Planet*>& ChunkManager::getActivePlanets() {
    return activePlanets_;
}

const std::vector<Star*>& ChunkManager::getActiveStars() {
    return activeStars_;
}

const std::vector<Nebula*>& ChunkManager::getActiveNebulae() {
    return activeNebulae_;
}

int ChunkManager::totalPlanets() const {
    int n = 0;
    for (auto& [_, c] : chunks_) n += static_cast<int>(c.planets.size());
    return n;
}
