#pragma once
#include <string>
#include <vector>

struct Moon {
    double orbitRadius;     // km from parent planet center
    double orbitSpeed;      // rad/s (angular velocity)
    double startAngle;      // initial angle at t=0
    std::string color;
};

struct Planet {
    double x = 0.0, y = 0.0;
    double mass = 1.0;
    double radius = 1.0;        // visual radius (km)
    char symbol = 'O';
    std::string color;
    std::string color2;         // secondary color for surface detail
    bool hasRings = false;
    std::string ringColor;
    double ringWidth = 0.0;     // extra radius beyond planet for ring
    std::vector<Moon> moons;
    bool isBlackHole = false;   // wormhole
    std::string id;             // unique identifier based on coordinates
};
