#include "terminal.h"
#include "renderer.h"
#include "physics.h"
#include "camera.h"
#include "planet.h"
#include "chunk.h"

#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include <deque>
#include <unordered_set>
#include <random>

enum class GameState { ORBIT, LANDING, WORMHOLE_CUTSCENE, GAME_OVER };

// ─── Colors ──────────────────────────────────────────────────
static const std::string C_HUD_LBL = "\033[38;5;39m";   // electric blue labels
static const std::string C_HUD_VAL = "\033[38;5;159m";  // soft cyan values
static const std::string C_HUD_HDR = "\033[1;38;5;214m"; // gold header
static const std::string C_HUD_BG  = "\033[48;5;232m";  // near-black background
static const std::string C_HUD_DIM = "\033[38;5;238m";  // deep grey
static const std::string C_STAR    = "\033[38;5;240m";
static const std::string C_STAR_BR = "\033[38;5;250m";
static const std::string C_SHIP    = "\033[1;34m";       // bold blue
static const std::string C_THRUST  = "\033[1;33m";       // bold yellow
static const std::string C_TRAIL1  = "\033[38;5;48m";    // bright trail
static const std::string C_TRAIL2  = "\033[38;5;35m";    // medium trail
static const std::string C_TRAIL3  = "\033[38;5;22m";    // dim trail
static const std::string C_TRAIL4  = "\033[38;5;236m";   // faded trail

static constexpr int HUD_ROWS = 5;

static std::string fmt(const char* f, double v) {
    char buf[64]; std::snprintf(buf, sizeof(buf), f, v); return buf;
}
static std::string fmtI(int64_t v) {
    char buf[32]; std::snprintf(buf, sizeof(buf), "%lld", (long long)v); return buf;
}

// ─── Key-hold detection ─────────────────────────────────────
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct InputState {
    TimePoint thrust{}, retro{}, rotL{}, rotR{};
    static constexpr double HOLD_MS = 200.0;

    bool held(TimePoint last, TimePoint now) const {
        return std::chrono::duration<double, std::milli>(now - last).count() < HOLD_MS;
    }
    bool isThrusting(TimePoint now)  const { return held(thrust, now); }
    bool isRetrograde(TimePoint now) const { return held(retro, now); }
    bool isRotL(TimePoint now)       const { return held(rotL, now); }
    bool isRotR(TimePoint now)       const { return held(rotR, now); }
};

// ─── Unicode arrow from angle ───────────────────────────────
static const char* shipGlyph(double angle) {
    double deg = angle * 180.0 / M_PI;
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;

    // 8 Unicode arrows mapped to 8 directions (45° each)
    if (deg < 22.5 || deg >= 337.5) return "\xe2\x86\x92"; // →
    if (deg < 67.5)  return "\xe2\x86\x97"; // ↗
    if (deg < 112.5) return "\xe2\x86\x91"; // ↑
    if (deg < 157.5) return "\xe2\x86\x96"; // ↖
    if (deg < 202.5) return "\xe2\x86\x90"; // ←
    if (deg < 247.5) return "\xe2\x86\x99"; // ↙
    if (deg < 292.5) return "\xe2\x86\x93"; // ↓
    return "\xe2\x86\x98"; // ↘
}

// ─── Trajectory trail ───────────────────────────────────────
struct TrailPoint { double x, y; };
static constexpr int MAX_TRAIL = 500;
static constexpr int TRAIL_INTERVAL = 3; // record every N physics ticks

// ─── Terrain generation for landing ─────────────────────────
static uint64_t terrainHash(int64_t x, uint64_t seed) {
    uint64_t h = static_cast<uint64_t>(x) * 2654435761ULL ^ seed;
    h = (h ^ (h >> 13)) * 1274126177ULL;
    return h ^ (h >> 16);
}

static double terrainHeightAt(double worldX, uint64_t seed) {
    auto noise = [&](double x, double freq, double amp) {
        int64_t b = (int64_t)std::floor(x * freq);
        double t = (x * freq) - b;
        double h0 = (double)(terrainHash(b, seed) % 1000) / 1000.0;
        double h1 = (double)(terrainHash(b + 1, seed) % 1000) / 1000.0;
        t = t * t * (3.0 - 2.0 * t); // smoothstep
        return (h0 * (1.0 - t) + h1 * t) * amp;
    };
    
    double h = noise(worldX, 0.04, 15.0);  // Large scale geography (hills/valleys)
    h += noise(worldX, 0.2, 3.0);         // Mid scale (ridges)
    h += noise(worldX, 1.2, 0.6);         // Small scale (rocks/roughness)
    return h;
}

static double getSeaLevel(uint64_t seed) {
    uint64_t trait = terrainHash(0, seed ^ 0xDEADBEEF);
    if (trait % 11 == 0) return 6.5; // Water world: high sea level
    if (trait % 7 == 0) return -5.0; // Desert planet: no water
    return 2.5; // Standard sea level
}

static bool isWaterAt(double worldX, uint64_t seed) {
    // Water regions defined by low-freq humidity noise
    int64_t b = (int64_t)std::floor(worldX / 40.0); 
    double t = (worldX / 40.0) - b;
    double w0 = (double)(terrainHash(b, seed ^ 0x123) % 100);
    double w1 = (double)(terrainHash(b + 1, seed ^ 0x123) % 100);
    t = t * t * (3.0 - 2.0 * t);
    double humidity = (w0 * (1.0 - t) + w1 * t);
    
    uint64_t trait = terrainHash(0, seed ^ 0xDEADBEEF);
    double threshold = (trait % 7 == 0) ? 85.0 : (trait % 11 == 0) ? 10.0 : 45.0;
    
    double sl = getSeaLevel(seed);
    double h = terrainHeightAt(worldX, seed);
    // Water exists only below sea level in humid regions
    return (h < sl) && (humidity > threshold);
}

// ─── HUD ─────────────────────────────────────────────────────
static void drawHUD(Renderer& ren, const Entity& ship, const Camera& cam,
                    const ChunkManager& chunks, double nearestGrav, double fps,
                    bool trailOn, GameState state, bool canLand, bool simulationStarted) {
    int w = ren.getCols();
    for (int r = 0; r < HUD_ROWS && r < ren.getRows(); ++r)
        for (int c = 0; c < w; ++c)
            ren.putChar(c, r, ' ', C_HUD_BG);

    // Top stylized border
    for (int c = 0; c < w; ++c) ren.putString(c, 0, "\xE2\x96\x80", "\033[38;5;235m" + C_HUD_BG);

    // Row 0: Title (centered)
    std::string title = " P L A N E T S ";
    ren.putString(std::max(0, (w - (int)title.size()) / 2), 0, title, C_HUD_HDR + C_HUD_BG);

    // Row 1: Position (Left) + Velocity (Right)
    ren.putString(0, 1, " X:", C_HUD_LBL + C_HUD_BG);
    ren.putString(3, 1, fmt("%.1f", ship.x), C_HUD_VAL + C_HUD_BG);
    ren.putString(12, 1, " Y:", C_HUD_LBL + C_HUD_BG);
    ren.putString(15, 1, fmt("%.1f", ship.y), C_HUD_VAL + C_HUD_BG);
    
    std::string vxStr = fmt("%.2f", ship.vx) + " km/s";
    std::string vyStr = fmt("%.2f", ship.vy) + " km/s";
    ren.putString(w - 28, 1, "Vx:", C_HUD_LBL + C_HUD_BG);
    ren.putString(w - 24, 1, vxStr, C_HUD_VAL + C_HUD_BG);
    ren.putString(w - 14, 1, "Vy:", C_HUD_LBL + C_HUD_BG);
    ren.putString(w - 10, 1, vyStr, C_HUD_VAL + C_HUD_BG);

    // Row 2: Orientation + Fuel (Left) + FPS (Right)
    double deg = ship.angle * 180.0 / M_PI;
    ren.putString(0, 2, " ANG:", C_HUD_LBL + C_HUD_BG);
    ren.putString(5, 2, fmt("%.0f", deg) + "\xC2\xB0", C_HUD_VAL + C_HUD_BG);
    
    int fuelBars = static_cast<int>((ship.fuel / ship.maxFuel) * 10.0);
    ren.putString(12, 2, " FUEL:", C_HUD_LBL + C_HUD_BG);
    std::string fuelColor = (fuelBars > 6) ? "\033[38;5;46m" : (fuelBars > 3) ? "\033[38;5;226m" : "\033[1;31m";
    std::string emptyColor = "\033[38;5;240m";
    ren.putString(18, 2, "[", C_HUD_LBL + C_HUD_BG);
    for (int i = 0; i < 10; ++i) {
        if (i < fuelBars) ren.putChar(19 + i, 2, '|', fuelColor + C_HUD_BG);
        else              ren.putChar(19 + i, 2, '.', emptyColor + C_HUD_BG);
    }
    ren.putString(29, 2, "]", C_HUD_LBL + C_HUD_BG);

    std::string fpVal = fmt("%.0f", fps);
    ren.putString(w - 10, 2, "FPS:", C_HUD_LBL + C_HUD_BG);
    ren.putString(w - 5, 2, fpVal, C_HUD_VAL + C_HUD_BG);

    // Add state indicator to row 0 if not ORBIT
    if (state == GameState::LANDING) {
        ren.putString(0, 0, " [LANDING MODE] ", "\033[1;31m" + C_HUD_BG);
    } else if (state == GameState::GAME_OVER) {
        ren.putString(0, 0, " [GAME OVER - PRESS R TO RETRY] ", "\033[1;31m" + C_HUD_BG);
    } else if (state == GameState::WORMHOLE_CUTSCENE) {
        ren.putString(0, 0, " [WORMHOLE TRAVERSAL] ", "\033[1;35m" + C_HUD_BG);
    } else if (canLand) {
        ren.putString(0, 0, " >>> PRESS L TO LAND <<< ", "\033[1;32m" + C_HUD_BG);
    } else if (!simulationStarted && state == GameState::ORBIT) {
        ren.putString(0, 0, " [SYSTEMS OFFLINE - PRESS ANY THRUST KEY TO ENGAGE] ", "\033[1;36m" + C_HUD_BG);
    }

    // Row 3: Speed + Gravity (Left) + Zoom (Right)
    double spd = Physics::speed(ship);
    double nearestG = nearestGrav / 1.0; 
    
    ren.putString(0, 3, " SPD:", C_HUD_LBL + C_HUD_BG);
    ren.putString(5, 3, fmt("%.1f", spd) + " km/s", C_HUD_VAL + C_HUD_BG);
    
    ren.putString(18, 3, " GRAV:", C_HUD_LBL + C_HUD_BG);
    ren.putString(24, 3, fmt("%.2f", nearestGrav) + " km/s\xC2\xB2", C_HUD_VAL + C_HUD_BG);
    ren.putString(35, 3, " (" + fmt("%.1f", nearestG) + "G)", C_HUD_DIM + C_HUD_BG);

    std::string zmVal = fmt("%.2f", cam.zoom) + "x";
    ren.putString(w - 11, 3, "ZOOM:", C_HUD_LBL + C_HUD_BG);
    ren.putString(w - 5, 3, zmVal, C_HUD_VAL + C_HUD_BG);

    // Row 4: Controls bar (context-sensitive)
    if (state == GameState::LANDING) {
        // Landing telemetry
        std::string alt = " ALT:" + fmt("%.1f", ship.y) + "km";
        std::string vspd = "  V/SPD:" + fmt("%.2f", ship.vy) + "km/s";
        std::string hspd = "  H/SPD:" + fmt("%.2f", ship.vx) + "km/s";
        double adeg = ship.angle * 180.0 / M_PI;
        adeg = std::fmod(adeg, 360.0);
        if (adeg < 0) adeg += 360.0;
        bool ok = (adeg > 60.0 && adeg < 120.0);
        std::string tilt = ok ? "  TILT:OK" : "  TILT:BAD";
        double totalSpd = Physics::speed(ship);
        std::string spdWarn = (totalSpd > 5.0) ? "  [TOO FAST!]" : (totalSpd > 3.0) ? "  [CAUTION]" : "  [SAFE]";
        std::string landHelp = alt + vspd + hspd + tilt + spdWarn;
        std::string spdWarnColor = (totalSpd > 5.0) ? "\033[1;31m" : (totalSpd > 3.0) ? "\033[1;33m" : "\033[1;32m";
        ren.putString(0, 4, landHelp, spdWarnColor + C_HUD_BG);
    } else {
        std::string help = " Q/W/E:Thrust+Turn  A/D:Turn  Z/S/C:Retro+Turn  SPACE:Stop  T:Trail  L:Land  ESC:Quit";
        ren.putString(0, 4, help, C_HUD_DIM + C_HUD_BG);
    }
}

// ─── MAIN ────────────────────────────────────────────────────
int main() {
    Terminal term;
    term.enableRawMode();

    Renderer renderer;
    Camera cam;
    cam.zoom = 0.01;

    Entity ship;
    std::mt19937_64 startupRng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<double> startDist(-50000.0, 50000.0);
    ship.x = startDist(startupRng);
    ship.y = startDist(startupRng);
    ship.angle = M_PI / 2.0;

    ChunkManager chunks;
    auto initialSize = term.getSize();
    chunks.update(ship.x, ship.y, cam.zoom, initialSize.first, initialSize.second);

    constexpr double FIXED_DT = 1.0 / 60.0;
    double accumulator = 0.0;
    auto prevTime = Clock::now();
    double fpsTimer = 0.0;
    int frameCount = 0;
    double currentFps = 60.0;

    InputState input;
    bool running = true;
    bool trailOn = true;  // trajectory trail enabled by default
    bool simulationStarted = false; 
    std::deque<TrailPoint> trail;
    int trailTick = 0;
    double simTime = 0.0;
    
    GameState state = GameState::ORBIT;
    std::unordered_set<std::string> visitedPlanets;
    const Planet* landingPlanet = nullptr;
    double landingY = 0.0;
    double cutsceneTimer = 0.0;
    uint64_t landingSeed = 0;

    while (running) {
        auto now = Clock::now();
        double frameTime = std::chrono::duration<double>(now - prevTime).count();
        prevTime = now;
        if (frameTime > 0.1) frameTime = 0.1;
        accumulator += frameTime;

        fpsTimer += frameTime;
        frameCount++;
        if (fpsTimer >= 1.0) {
            currentFps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0;
        }

        // ─── Input ──────────────────────────────────
        auto inputNow = Clock::now();
        bool stopPressed = false;
        int key;
        while ((key = term.readKey()) != Terminal::KEY_NONE) {
            switch (key) {
                case '\033': case '\x03': // ESC or Ctrl+C
                    running = false; break;
                
                // Thrust + Combos
                case 'w': case 'W': case Terminal::KEY_UP:
                    input.thrust = inputNow; break;
                case 'q': case 'Q':
                    input.thrust = inputNow; input.rotL = inputNow; break;
                case 'e': case 'E':
                    input.thrust = inputNow; input.rotR = inputNow; break;
                
                // Retrograde + Combos
                case 's': case 'S': case Terminal::KEY_DOWN:
                    input.retro = inputNow; break;
                case 'z': case 'Z':
                    input.retro = inputNow; input.rotL = inputNow; break;
                case 'c': case 'C':
                    input.retro = inputNow; input.rotR = inputNow; break;

                // Pure Turning
                case 'a': case 'A': case Terminal::KEY_LEFT:
                    input.rotL = inputNow; break;
                case 'd': case 'D': case Terminal::KEY_RIGHT:
                    input.rotR = inputNow; break;
                
                // Utils
                case ' ':
                    stopPressed = true; break;
                case 't': case 'T':
                    trailOn = !trailOn;
                    if (!trailOn) trail.clear();
                    break;
                case 'x': case 'X':
                    trail.clear(); break;  // clear trail without toggling
                case '+': case '=': case Terminal::KEY_SCROLL_UP:
                    if (state == GameState::ORBIT) { cam.zoom = std::min(cam.zoom * 1.15, 10.0); } break;
                case '-': case '_': case Terminal::KEY_SCROLL_DN:
                    if (state == GameState::ORBIT) { cam.zoom = std::max(cam.zoom / 1.15, 0.01); } break;
                
                case 'l': case 'L':
                    if (state == GameState::ORBIT) {
                        // Find closest planet
                        const auto& planets = chunks.getActivePlanets();
                        for (const Planet* p : planets) {
                            if (p->isBlackHole) continue;
                            double dist = std::sqrt(std::pow(p->x - ship.x, 2) + std::pow(p->y - ship.y, 2));
                            if (dist < p->radius * 1.5 + 50.0) { // Close enough to land
                                state = GameState::LANDING;
                                landingPlanet = p;
                                landingY = 50.0; // Start 50km above surface (always visible)
                                landingSeed = static_cast<uint64_t>(p->x * 1000) ^ static_cast<uint64_t>(p->y * 7919);
                                
                                // Map velocities relative to planet (simplified)
                                // Tangential becomes vx, radial becomes vy (falling)
                                double dx = ship.x - p->x;
                                double dy = ship.y - p->y;
                                double pAng = std::atan2(dy, dx);
                                
                                double speed = Physics::speed(ship);
                                double velAng = std::atan2(ship.vy, ship.vx);
                                
                                // Clamp entry velocity to something survivable-ish
                                if (speed > 8.0) speed = 8.0;
                                
                                // Approximate mapping for 2D minigame
                                double diffAng = velAng - pAng;
                                ship.vx = speed * std::sin(diffAng) * 0.3; // Horizontal along surface (damped)
                                ship.vy = -std::abs(speed * std::cos(diffAng)) * 0.3; // Vertical towards surface (damped)
                                ship.x = 0; // Center ship horizontally
                                ship.y = landingY;
                                
                                // Preserve approach angle: derive from mapped velocity
                                // If entering mostly sideways, ship will be tilted
                                if (std::abs(ship.vx) > 0.01 || std::abs(ship.vy) > 0.01) {
                                    ship.angle = std::atan2(ship.vy, ship.vx) + M_PI; // Point opposite to velocity
                                    // Bias towards upright — blend 60% velocity angle, 40% upright
                                    double uprightAngle = M_PI / 2.0;
                                    ship.angle = ship.angle * 0.6 + uprightAngle * 0.4;
                                } else {
                                    ship.angle = M_PI / 2.0;
                                }
                                trail.clear();
                                break;
                            }
                        }
                    }
                    break;
                case 'r': case 'R':
                    if (state == GameState::GAME_OVER) {
                        // Respawn
                        if (ship.fuel <= 0.0) ship.fuel = ship.maxFuel; // Only refill if out of fuel
                        ship.vx = 0; ship.vy = 0;
                        ship.angle = M_PI / 2.0;
                        state = GameState::ORBIT;
                        if (landingPlanet) {
                            ship.x = landingPlanet->x + landingPlanet->radius * 1.5;
                            ship.y = landingPlanet->y;
                            landingPlanet = nullptr;
                        }
                    }
                    break;
            }
        }

        if (stopPressed) Physics::fullStop(ship);

        ship.thrusting  = input.isThrusting(inputNow);
        ship.retrograde = input.isRetrograde(inputNow);
        ship.angularVel = 0.0;
        if (input.isRotL(inputNow)) ship.angularVel += Physics::ROTATION_SPEED;
        if (input.isRotR(inputNow)) ship.angularVel -= Physics::ROTATION_SPEED;

        if (!simulationStarted && (ship.thrusting || ship.retrograde || ship.angularVel != 0.0)) {
            simulationStarted = true;
        }

        auto [cols, rows] = term.getSize();

        // ─── Physics ────────────────────────────────
        while (accumulator >= FIXED_DT) {
            if (state == GameState::ORBIT) {
                chunks.update(ship.x, ship.y, cam.zoom, cols, rows);

                const auto& planets = chunks.getActivePlanets();
                for (const Planet* p : planets) {
                    Physics::applyGravity(ship, p->x, p->y, p->mass, p->radius, FIXED_DT);
                    
                    if (p->isBlackHole) {
                        double dist = std::sqrt(std::pow(p->x - ship.x, 2) + std::pow(p->y - ship.y, 2));
                        if (dist < p->radius * 0.8) { // Event horizon
                            state = GameState::WORMHOLE_CUTSCENE;
                            cutsceneTimer = 2.0; 
                            break;
                        }
                    }
                }
                
                if (ship.thrusting)  Physics::applyThrust(ship, FIXED_DT);
                if (ship.retrograde) Physics::applyRetrograde(ship, FIXED_DT);
                Physics::integrate(ship, FIXED_DT);

                // Record trail
                if (trailOn) {
                    if (++trailTick >= TRAIL_INTERVAL) {
                        trailTick = 0;
                        trail.push_back({ship.x, ship.y});
                        if ((int)trail.size() > MAX_TRAIL)
                            trail.pop_front();
                    }
                }
            } else if (state == GameState::LANDING) {
                // Landing Physics
                double tHeight = terrainHeightAt(ship.x, landingSeed);
                double gRaw = Physics::GAME_G * landingPlanet->mass / std::max(0.001, std::pow(landingPlanet->radius, 2));
                double g = std::min(gRaw, Physics::THRUST_ACCEL * 0.6);
                
                // Gravity applies if above terrain
                if (ship.y > tHeight) {
                    ship.vy -= g * FIXED_DT;
                }
                
                if (ship.thrusting)  Physics::applyThrust(ship, FIXED_DT);
                if (ship.retrograde) Physics::applyRetrograde(ship, FIXED_DT);
                Physics::integrate(ship, FIXED_DT);
                
                // Check if we exited atmosphere (Takeoff)
                if (ship.y > landingY * 1.2) {
                    state = GameState::ORBIT;
                    std::mt19937_64 rng(landingSeed);
                    double escapeAngle = std::uniform_real_distribution<double>(0, 2*M_PI)(rng);
                    ship.x = landingPlanet->x + std::cos(escapeAngle) * landingPlanet->radius * 1.5;
                    ship.y = landingPlanet->y + std::sin(escapeAngle) * landingPlanet->radius * 1.5;
                    ship.vx = std::cos(escapeAngle) * 5.0; 
                    ship.vy = std::sin(escapeAngle) * 5.0;
                    cam.zoom = 0.5;
                    // Skip remaining landing-specific checks this frame
                }

                if (state == GameState::LANDING) {
                    tHeight = terrainHeightAt(ship.x, landingSeed);
                    bool water = isWaterAt(ship.x, landingSeed);
                    double sl = getSeaLevel(landingSeed);
                    double effectiveTHeight = water ? sl : tHeight;
                    
                    if (ship.y <= effectiveTHeight) {
                        if (ship.vy < 0) { 
                            double crashSpeed = Physics::speed(ship);
                            double deg = ship.angle * 180.0 / M_PI;
                            deg = std::fmod(deg, 360.0);
                            if (deg < 0) deg += 360.0;
                            
                            bool upright = (deg > 60.0 && deg < 120.0);
                            
                            if (crashSpeed > 5.0 || !upright || water) {
                                state = GameState::GAME_OVER;
                            } else {
                                ship.y = effectiveTHeight;
                                ship.vx = 0; ship.vy = 0;
                                ship.angle = M_PI / 2.0;
                                if (!visitedPlanets.count(landingPlanet->id)) {
                                    visitedPlanets.insert(landingPlanet->id);
                                    ship.fuel = ship.maxFuel;
                                }
                            }
                        } else {
                            if (ship.y < effectiveTHeight) ship.y = effectiveTHeight;
                        }
                    }
                }

                // Global Fuel Check
                if (ship.fuel <= 0) {
                    state = GameState::GAME_OVER;
                }
            } else if (state == GameState::WORMHOLE_CUTSCENE) {
                cutsceneTimer -= FIXED_DT;
                if (cutsceneTimer <= 0) {
                    // Teleport
                    state = GameState::ORBIT;
                    
                    // Dampen extreme speeds gained from black hole gravity to a manageable 150 km/s
                    double spd = Physics::speed(ship);
                    if (spd > 150.0) {
                        double scale = 150.0 / spd;
                        ship.vx *= scale;
                        ship.vy *= scale;
                    }

                    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
                    std::uniform_real_distribution<double> dist(-100000.0, 100000.0);
                    ship.x += dist(rng);
                    ship.y += dist(rng);
                    trail.clear();
                }
            }

            if (!simulationStarted && state == GameState::ORBIT) {
                // Keep ship stationary until pilot starts engines
                ship.vx = 0; ship.vy = 0;
            }

            accumulator -= FIXED_DT;
            simTime += FIXED_DT;
        }

        // ─── Camera ─────────────────────────────────
        cam.x = ship.x;
        cam.y = ship.y;

        // ─── Render ─────────────────────────────────
        renderer.resize(cols, rows);
        renderer.clear();

        int viewH = rows - HUD_ROWS;

        // ─── Rendering based on State ───────────────
        double nearestGrav = 0.0;

        if (state == GameState::ORBIT || state == GameState::GAME_OVER) {
            // Nebulae (Background)
        const auto& nebulae = chunks.getActiveNebulae();
        for (const Nebula* n : nebulae) {
            auto sp = cam.worldToScreenRaw(n->x, n->y, cols, viewH);
            int vr = std::max(0, static_cast<int>(n->radius * cam.zoom / 2.0));
            if (vr == 0) continue;

            int c = sp.col;
            int r = sp.row;
            
            // Clip perfectly to screen bounds to prevent massive loop overhead
            int minR = std::max(0, r - vr);
            int maxR = std::min(viewH - 1, r + vr);
            int minC = std::max(0, c - vr * 2);
            int maxC = std::min(cols - 1, c + vr * 2);

            for (int rr = minR; rr <= maxR; ++rr) {
                for (int cc = minC; cc <= maxC; ++cc) {
                    int dr = rr - r;
                    int dc = cc - c;
                    double nd = std::sqrt(dr*dr*4.0 + dc*dc*1.0) / (vr*2.0);
                    if (nd <= 1.0) {
                        // Smooth radial falloff combined with simple static noise
                        double falloff = 1.0 - nd;
                        falloff = falloff * falloff * falloff; // sharp exponential dropoff

                        // Screen-space noise creates a shimmering effect as camera moves
                        uint64_t h = static_cast<uint64_t>(cc * 31) ^ static_cast<uint64_t>(rr * 73);
                        
                        // Map screen coords to world coords to create slow, static color veins
                        double wx = n->x + dc * (1.0 / cam.zoom);
                        double wy = n->y + dr * (2.0 / cam.zoom);
                        int colorBlockX = static_cast<int>(std::floor(wx / 100.0));
                        int colorBlockY = static_cast<int>(std::floor(wy / 100.0));
                        uint64_t chash = static_cast<uint64_t>(colorBlockX * 73856) ^ static_cast<uint64_t>(colorBlockY * 19349);
                        
                        // Pick a stunning color based on distance and spatial hash (core -> veins -> fringes)
                        const char* drawColor = n->color3; // dark outer fringes
                        if (nd < 0.25) {
                            drawColor = n->color1; // bright hot core
                        } else if ((chash % 100) > 50) {
                            drawColor = n->color2; // vibrant mid-tone veins
                        }

                        // Only draw if noise passes the falloff threshold
                        if ((h % 100) / 100.0 < falloff * 0.7) {
                            renderer.putChar(cc, rr + HUD_ROWS, n->ch, drawColor);
                        }
                    }
                }
            }
        }

        // Stars
        const auto& stars = chunks.getActiveStars();
        for (const Star* s : stars) {
            auto sp = cam.worldToScreen(s->x, s->y, cols, viewH);
            if (sp) {
                uint64_t h = static_cast<uint64_t>(s->x * 1000) ^ static_cast<uint64_t>(s->y * 7919);
                renderer.putChar(sp->col, sp->row + HUD_ROWS,
                    (h % 5 == 0) ? '+' : '.', (h % 3 == 0) ? C_STAR_BR : C_STAR);
            }
        }

        // Trajectory trail
        if (trailOn && !trail.empty()) {
            int total = (int)trail.size();
            for (int i = 0; i < total; ++i) {
                auto sp = cam.worldToScreen(trail[i].x, trail[i].y, cols, viewH);
                if (sp) {
                    // Fade: newest = bright, oldest = dim
                    double age = (double)(total - 1 - i) / std::max(1, total - 1);
                    const std::string* clr;
                    if (age < 0.25)      clr = &C_TRAIL1;
                    else if (age < 0.50) clr = &C_TRAIL2;
                    else if (age < 0.75) clr = &C_TRAIL3;
                    else                 clr = &C_TRAIL4;
                    renderer.putChar(sp->col, sp->row + HUD_ROWS, '.', *clr);
                }
            }
        }

        // Planets, rings, and moons
        const auto& planets = chunks.getActivePlanets();
        for (const Planet* p : planets) {
            double dx = p->x - ship.x, dy = p->y - ship.y;
            double dist = std::sqrt(dx*dx + dy*dy);
            
            double grav = 0.0;
            if (dist < p->radius) {
                grav = Physics::GAME_G * p->mass * dist / std::max(0.001, p->radius * p->radius * p->radius);
            } else {
                grav = Physics::GAME_G * p->mass / std::max(0.001, dist * dist);
            }
            nearestGrav = std::max(nearestGrav, grav);

            ScreenPos sp = cam.worldToScreenRaw(p->x, p->y, cols, viewH);
            int r = sp.row + HUD_ROWS, c = sp.col;

            // ─── Special: Black Hole / Wormhole Rendering ───
            if (p->isBlackHole) {
                int vr = std::max(0, static_cast<int>(p->radius * cam.zoom / 2.0));
                int rOut = std::max(vr + 1, static_cast<int>((p->radius + p->ringWidth) * cam.zoom / 2.0));
                
                if (rOut < 1) {
                    // Far away: just a tiny swirling point
                    char s = (static_cast<int>(simTime * 10) % 2 == 0) ? '*' : '+';
                    renderer.putChar(c, r, s, "\033[1;35m");
                } else {
                    for (int dr = -rOut; dr <= rOut; ++dr) {
                        for (int dc = -rOut * 2; dc <= rOut * 2; ++dc) {
                            double d = std::sqrt(dr*dr*4.0 + dc*dc*1.0) / (rOut * 2.0);
                            double inside = std::sqrt(dr*dr*4.0 + dc*dc*1.0) / (std::max(0.8, (double)vr) * 2.0);
                            
                            if (d <= 1.0) {
                                if (inside <= 1.0) {
                                    // Event Horizon (Pitch Black)
                                    renderer.putChar(c + dc, r + dr, ' ', "\033[48;5;232m");
                                } else {
                                    // Accretion Disk (Swirling energy)
                                    double ang = std::atan2(dr, dc/2.0) + simTime * 8.0;
                                    char swirls[] = {'/', '-', '\\', '|'};
                                    int sIdx = static_cast<int>(std::abs(ang * 4 / M_PI)) % 4;
                                    const char* diskColors[] = {"\033[1;35m", "\033[1;36m", "\033[1;37m", "\033[38;5;93m"};
                                    int cIdx = (static_cast<int>(d * 4 + simTime * 2)) % 4;
                                    renderer.putChar(c + dc, r + dr, swirls[sIdx], diskColors[cIdx]);
                                }
                            }
                        }
                    }
                }
                continue; // Done with this object
            }

            int vr = std::max(0, static_cast<int>(p->radius * cam.zoom / 2.0));

            // Back half of rings
            int rOut = 0;
            if (p->hasRings) {
                rOut = std::max(vr + 1, static_cast<int>((p->radius + p->ringWidth) * cam.zoom / 2.0));
                for (int dr = -rOut; dr < 0; ++dr) {
                    for (int dc = -rOut * 2; dc <= rOut * 2; ++dc) {
                        double nd = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (rOut * 2.0);
                        double ni = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (std::max(1, vr) * 2.0);
                        if (nd <= 1.0 && ni > 1.0) {
                            if (rOut < 4 || (dr + dc) % 2 == 0) { // Solid if small, dashed if large
                                renderer.putChar(c + dc, r + dr, '=', p->ringColor);
                            }
                        }
                    }
                }
            }

            // Planet body
            if (vr == 0) {
                renderer.putChar(c, r, p->symbol, p->color);
            } else {
                for (int dr = -vr; dr <= vr; ++dr) {
                    for (int dc = -vr*2; dc <= vr*2; ++dc) {
                        double nd = std::sqrt(dr*dr*4.0 + dc*dc*1.0) / (vr*2.0);
                        if (nd <= 1.0) {
                            bool detail = (vr >= 3) && ((dr * 7 + dc * 13) % 5 == 0); // No noise when small
                            renderer.putChar(c + dc, r + dr,
                                nd > 0.8 ? '.' : (detail ? '~' : p->symbol),
                                detail ? p->color2 : p->color);
                        }
                    }
                }
            }

            // Front half of rings
            if (p->hasRings) {
                for (int dr = 0; dr <= rOut; ++dr) {
                    for (int dc = -rOut * 2; dc <= rOut * 2; ++dc) {
                        double nd = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (rOut * 2.0);
                        double ni = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (std::max(1, vr) * 2.0);
                        if (nd <= 1.0 && ni > 1.0) {
                            if (rOut < 4 || (dr + dc) % 2 == 0) {
                                renderer.putChar(c + dc, r + dr, '=', p->ringColor);
                            }
                        }
                    }
                }
            }

            // Moons
            for (const auto& moon : p->moons) {
                double angle = moon.startAngle + moon.orbitSpeed * simTime;
                double mx = p->x + moon.orbitRadius * std::cos(angle);
                double my = p->y + moon.orbitRadius * std::sin(angle);
                auto msp = cam.worldToScreen(mx, my, cols, viewH);
                if (msp) {
                    char mchar = (cam.zoom < 0.3) ? '.' : 'o';
                    renderer.putChar(msp->col, msp->row + HUD_ROWS, mchar, moon.color);
                }
            }
        }
        } else if (state == GameState::LANDING) {
            // Surface gravity for HUD display
            double gRaw = Physics::GAME_G * landingPlanet->mass / std::max(0.001, std::pow(landingPlanet->radius, 2));
            nearestGrav = std::min(gRaw, Physics::THRUST_ACCEL * 0.6);

            // Stars in background
            std::mt19937_64 rng(1337);
            for (int i=0; i<50; ++i) {
                int sc = rng() % cols;
                int sr = rng() % viewH;
                renderer.putChar(sc, sr + HUD_ROWS, '.', "\033[38;5;238m");
            }

            // Dynamic zoom
            double altFraction = std::max(0.0, ship.y / landingY);
            double zoomFactor = 1.0 + (1.0 - altFraction) * 2.0;

            int groundScreenRow = viewH - (viewH / 7);
            double viewRangeKm = landingY / zoomFactor;
            double kmPerRow = viewRangeKm / (double)(groundScreenRow - viewH / 2);
            if (kmPerRow < 0.1) kmPerRow = 0.1;
            double kmPerCol = kmPerRow * 0.5; // Aspect ratio: chars are tall
            
            int shipScreenRow = groundScreenRow - (int)(ship.y / kmPerRow) - 1;
            int shipScreenCol = cols / 2; 
            
            // Draw altitude markers
            int markerStep = (landingY > 30) ? 10 : 5;
            for (int alt = markerStep; alt <= (int)landingY; alt += markerStep) {
                int markerRow = groundScreenRow - (int)((double)alt / kmPerRow);
                if (markerRow >= 0 && markerRow < viewH) {
                    std::string label = std::to_string(alt) + "km";
                    int labelCol = cols - (int)label.size() - 1;
                    if (labelCol > 0) {
                        for (int ci = 0; ci < cols - (int)label.size() - 2; ci += 4)
                            renderer.putChar(ci, markerRow + HUD_ROWS, '-', "\033[38;5;236m");
                        for (int ci = 0; ci < (int)label.size(); ++ci)
                            renderer.putChar(labelCol + ci, markerRow + HUD_ROWS, label[ci], "\033[38;5;240m");
                    }
                }
            }

            // Draw terrain with variable height, cliffs, and level water
            double sl = getSeaLevel(landingSeed);
            for (int c = 0; c < cols; ++c) {
                double worldX = (c - cols / 2) * kmPerCol + ship.x;
                
                double tH = terrainHeightAt(worldX, landingSeed);
                bool water = isWaterAt(worldX, landingSeed);
                double effectiveH = water ? sl : tH;
                int surfRow = groundScreenRow - (int)(effectiveH / kmPerRow);
                
                if (surfRow >= 0 && surfRow < viewH) {
                    if (water) {
                        char wChar = ((c + (int)(simTime * 3)) % 3 == 0) ? '~' : '-';
                        renderer.putChar(c, surfRow + HUD_ROWS, wChar, "\033[1;34m"); 
                        for (int r = surfRow + 1; r < viewH; ++r)
                            renderer.putChar(c, r + HUD_ROWS, '~', "\033[38;5;17m"); 
                    } else {
                        uint64_t th = terrainHash((int64_t)(worldX * 10), landingSeed ^ 0xFACE);
                        char surfChar = ((th % 7 == 0) ? '^' : (th % 3 == 0) ? '~' : '=');
                        renderer.putChar(c, surfRow + HUD_ROWS, surfChar, landingPlanet->color);
                        for (int r = surfRow + 1; r < viewH; ++r) {
                            uint64_t dh = static_cast<uint64_t>(c * 31 + r * 97) ^ 0xBEEF;
                            char dirtChar = ((dh % 5 == 0) ? '.' : (dh % 7 == 0) ? ',' : '#');
                            renderer.putChar(c, r + HUD_ROWS, dirtChar, landingPlanet->color2);
                        }
                    }
                }
            }

            // Draw ship at screen center
            if (shipScreenRow >= 0 && shipScreenRow < viewH) {
                const char* sg = shipGlyph(ship.angle);
                renderer.putGlyph(shipScreenCol, shipScreenRow + HUD_ROWS, sg, ship.thrusting ? C_THRUST : C_SHIP);
            }
        } else if (state == GameState::WORMHOLE_CUTSCENE) {
            // Radiating stars effect
            double progress = 1.0 - (cutsceneTimer / 2.0); // 0.0 to 1.0
            int cx = cols / 2;
            int cy = viewH / 2;
            std::mt19937_64 rng(static_cast<uint64_t>(simTime * 100)); // Fast changing seed
            for (int i=0; i<100; ++i) {
                double ang = std::uniform_real_distribution<double>(0, 2*M_PI)(rng);
                double rad = std::uniform_real_distribution<double>(0.1, 1.0)(rng) * std::max(cols, viewH) * progress;
                int sc = cx + (int)(std::cos(ang) * rad);
                int sr = cy + (int)(std::sin(ang) * rad);
                if (sc >= 0 && sc < cols && sr >= 0 && sr < viewH) {
                    renderer.putChar(sc, sr + HUD_ROWS, '*', "\033[1;35m"); // Magenta streaks
                }
            }
            // Event horizon void
            for (int dr = -2; dr <= 2; ++dr) {
                for (int dc = -4; dc <= 4; ++dc) {
                    if (dr*dr*4 + dc*dc <= 16) {
                        renderer.putChar(cx + dc, cy + dr + HUD_ROWS, ' ', C_HUD_BG);
                    }
                }
            }
        }


        // ─── Ship: Unicode arrow ────────────────────
        if (state != GameState::WORMHOLE_CUTSCENE && state != GameState::LANDING) {
            int shipCol = cols / 2;
            int shipRow = HUD_ROWS + viewH / 2;
            const char* sg = shipGlyph(ship.angle);
            renderer.putGlyph(shipCol, shipRow, sg, ship.thrusting ? C_THRUST : C_SHIP);
        }

        // Check if we can land on any nearby planet (for HUD indicator)
        bool canLand = false;
        if (state == GameState::ORBIT) {
            const auto& allPlanets = chunks.getActivePlanets();
            for (const Planet* p : allPlanets) {
                if (p->isBlackHole) continue;
                double dist = std::sqrt(std::pow(p->x - ship.x, 2) + std::pow(p->y - ship.y, 2));
                if (dist < p->radius * 1.5 + 50.0) {
                    canLand = true;
                    break;
                }
            }
        }
        drawHUD(renderer, ship, cam, chunks, nearestGrav, currentFps, trailOn, state, canLand, simulationStarted);
        renderer.flush();

        // Frame limit
        auto el = std::chrono::duration<double>(Clock::now() - now).count();
        if (double sl = (1.0/60.0) - el; sl > 0.0) {
            struct timespec ts{0, static_cast<long>(sl * 1e9)};
            nanosleep(&ts, nullptr);
        }
    }
    return 0;
}
