#include "physics.h"

namespace Physics {

void applyThrust(Entity& e, double dt) {
    // F = m*a along heading direction
    e.vx += THRUST_ACCEL * std::cos(e.angle) * dt;
    e.vy += THRUST_ACCEL * std::sin(e.angle) * dt;
}

void applyRetrograde(Entity& e, double dt) {
    // Realistic braking: thrust opposite to current velocity direction
    double spd = speed(e);
    if (spd < 0.001) return; // already stopped

    // Direction of velocity
    double vAngle = std::atan2(e.vy, e.vx);

    // Decelerate, but don't overshoot past zero
    double decel = std::min(THRUST_ACCEL * dt, spd);
    e.vx -= decel * std::cos(vAngle);
    e.vy -= decel * std::sin(vAngle);
}

void fullStop(Entity& e) {
    e.vx = 0.0;
    e.vy = 0.0;
}

void applyGravity(Entity& ship, double px, double py, double pmass, double pradius, double dt) {
    // Newton's law
    double dx = px - ship.x;
    double dy = py - ship.y;
    double distSq = dx * dx + dy * dy;
    double dist = std::sqrt(distSq);
    if (dist < 0.001) return; // exactly at center, forces cancel out

    double accel = 0.0;
    if (dist < pradius) {
        // Inside planet: gravity decreases linearly to zero at the core
        accel = GAME_G * pmass * dist / (pradius * pradius * pradius);
    } else {
        // Outside planet: inverse square law
        accel = GAME_G * pmass / distSq;
    }

    ship.vx += accel * (dx / dist) * dt;
    ship.vy += accel * (dy / dist) * dt;
}

void integrate(Entity& e, double dt) {
    // Semi-implicit Euler: update velocity first (done by thrust/gravity),
    // then update position. This conserves energy better than explicit Euler.
    e.angle += e.angularVel * dt;
    e.angle = normalizeAngle(e.angle);
    e.x += e.vx * dt;
    e.y += e.vy * dt;
    // No drag, no friction — velocity persists forever (Newton's 1st law)
}

double normalizeAngle(double a) {
    const double TWO_PI = 2.0 * M_PI;
    a = std::fmod(a, TWO_PI);
    if (a < 0.0) a += TWO_PI;
    return a;
}

double speed(const Entity& e) {
    return std::sqrt(e.vx * e.vx + e.vy * e.vy);
}

} // namespace Physics
