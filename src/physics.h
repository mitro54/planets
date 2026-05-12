#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Entity {
    double x = 0.0, y = 0.0;       // position (km)
    double vx = 0.0, vy = 0.0;     // velocity (km/s)
    double angle = M_PI / 2.0;     // heading (rad), 0=East, CCW+
    double mass = 1000.0;           // kg
    double angularVel = 0.0;        // rad/s
    bool thrusting = false;
    bool retrograde = false;        // thrusting against velocity (realistic braking)
    double maxFuel = 1200.0;
    double fuel = 1200.0;
};

namespace Physics {
    // Newtonian constants (game-scaled for playability at km distances)
    constexpr double GAME_G         = 40.0;
    constexpr double THRUST_ACCEL   = 6.0;      // low thrust, slingshots matter
    constexpr double ROTATION_SPEED = 3.5;      // rad/s
    constexpr double BURN_RATE      = 15.0;     // fuel per second while thrusting

    // No BRAKE_FACTOR — space has no friction!

    void applyThrust(Entity& e, double dt);         // thrust along ship heading
    void applyRetrograde(Entity& e, double dt);     // thrust against velocity (realistic brake)
    void fullStop(Entity& e);                       // emergency: zero velocity (cheat)
    void applyGravity(Entity& ship, double px, double py, double pmass, double pradius, double dt);
    void integrate(Entity& e, double dt);
    double normalizeAngle(double a);
    double speed(const Entity& e);
}
