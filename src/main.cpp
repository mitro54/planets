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

// ─── Colors ──────────────────────────────────────────────────
static const std::string C_HUD_LBL = "\033[1;37m";
static const std::string C_HUD_VAL = "\033[1;33m";
static const std::string C_HUD_HDR = "\033[1;36m";
static const std::string C_HUD_BG  = "\033[48;5;234m";
static const std::string C_HUD_DIM = "\033[38;5;245m";
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

// ─── HUD ─────────────────────────────────────────────────────
static void drawHUD(Renderer& ren, const Entity& ship, const Camera& cam,
                    const ChunkManager& chunks, double nearestGrav, double fps,
                    bool trailOn) {
    int w = ren.getCols();
    for (int r = 0; r < HUD_ROWS && r < ren.getRows(); ++r)
        for (int c = 0; c < w; ++c)
            ren.putChar(c, r, ' ', C_HUD_BG);

    // Row 0: Title (centered)
    std::string title = " SPACE FLIGHT ENGINE ";
    ren.putString(std::max<long long>(0, (w - (int)title.size()) / 2), 0, title, C_HUD_HDR + C_HUD_BG);

    // Row 1: Position + Velocity
    std::string pos = " X:" + fmt("%.1f", ship.x) + " Y:" + fmt("%.1f", ship.y);
    ren.putString(0, 1, pos, C_HUD_LBL + C_HUD_BG);
    std::string vel = "Vx:" + fmt("%.2f", ship.vx) + " Vy:" + fmt("%.2f", ship.vy) + " km/s ";
    ren.putString(std::max(0, w - (int)vel.size()), 1, vel, C_HUD_LBL + C_HUD_BG);

    // Row 2: Speed + Angle + Zoom
    double spd = Physics::speed(ship);
    double deg = ship.angle * 180.0 / M_PI;
    std::string r2 = " SPD:" + fmt("%.1f", spd) + " km/s  ANG:" + fmt("%.0f", deg) + "\xC2\xB0";
    ren.putString(0, 2, r2, C_HUD_VAL + C_HUD_BG);
    std::string zm = "ZOOM:" + fmt("%.2f", cam.zoom) + "x ";
    ren.putString(std::max(0, w - (int)zm.size()), 2, zm, C_HUD_VAL + C_HUD_BG);

    // Row 3: Chunk + Gravity + Trail
    auto cc = ChunkManager::worldToChunk(ship.x, ship.y);
    std::string r3 = " CHUNK:[" + fmtI(cc.cx) + "," + fmtI(cc.cy) + "]"
                   + " GRAV:" + fmt("%.2f", nearestGrav)
                   + " PLANETS:" + std::to_string(chunks.totalPlanets())
                   + (trailOn ? " [TRAIL]" : "");
    ren.putString(0, 3, r3, C_HUD_LBL + C_HUD_BG);
    std::string fp = "FPS:" + fmt("%.0f", fps) + " ";
    ren.putString(std::max(0, w - (int)fp.size()), 3, fp, C_HUD_VAL + C_HUD_BG);

    // Row 4: Controls bar
    std::string help = " Q/W/E:Thrust+Turn  A/D:Turn  Z/S/C:Retro+Turn  SPACE:Stop  T:Trail  ESC:Quit";
    ren.putString(0, 4, help, C_HUD_DIM + C_HUD_BG);
}

// ─── MAIN ────────────────────────────────────────────────────
int main() {
    Terminal term;
    term.enableRawMode();

    Renderer renderer;
    Camera cam;
    cam.zoom = 0.5;

    Entity ship;
    ship.x = 100.0;
    ship.y = 100.0;
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
    std::deque<TrailPoint> trail;
    int trailTick = 0;
    double simTime = 0.0;

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
                    cam.zoom = std::min(cam.zoom * 1.15, 10.0); break;
                case '-': case '_': case Terminal::KEY_SCROLL_DN:
                    cam.zoom = std::max(cam.zoom / 1.15, 0.01); break;
            }
        }

        if (stopPressed) Physics::fullStop(ship);

        ship.thrusting  = input.isThrusting(inputNow);
        ship.retrograde = input.isRetrograde(inputNow);
        ship.angularVel = 0.0;
        if (input.isRotL(inputNow)) ship.angularVel += Physics::ROTATION_SPEED;
        if (input.isRotR(inputNow)) ship.angularVel -= Physics::ROTATION_SPEED;

        auto [cols, rows] = term.getSize();

        // ─── Physics ────────────────────────────────
        while (accumulator >= FIXED_DT) {
            chunks.update(ship.x, ship.y, cam.zoom, cols, rows);

            const auto& planets = chunks.getActivePlanets();
            for (const Planet* p : planets)
                Physics::applyGravity(ship, p->x, p->y, p->mass, p->radius, FIXED_DT);

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
            int maxR = std::min<long long>(viewH - 1, r + vr);
            int minC = std::max<long long>(0, c - vr * 2);
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
        double nearestGrav = 0.0;
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
            int vr = std::max(0, static_cast<int>(p->radius * cam.zoom / 2.0));

            // Back half of rings
            int rOut = 0;
            if (p->hasRings) {
                rOut = std::max<long long>(vr + 1, static_cast<int>((p->radius + p->ringWidth) * cam.zoom / 2.0));
                for (int dr = -rOut; dr < 0; ++dr) {
                    for (int dc = -rOut * 2; dc <= rOut * 2; ++dc) {
                        double nd = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (rOut * 2.0);
                        double ni = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (std::max<long long>(1, vr) * 2.0);
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
                        double ni = std::sqrt(dr*dr*16.0 + dc*dc*1.0) / (std::max<long long>(1, vr) * 2.0);
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

        // ─── Ship: Unicode arrow ────────────────────
        int shipCol = cols / 2;
        int shipRow = HUD_ROWS + viewH / 2;
        const char* sg = shipGlyph(ship.angle);
        renderer.putGlyph(shipCol, shipRow, sg, ship.thrusting ? C_THRUST : C_SHIP);

        drawHUD(renderer, ship, cam, chunks, nearestGrav, currentFps, trailOn);
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
