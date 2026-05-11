#pragma once
#include <array>
#include <string>

struct SpriteCell {
    char ch = ' ';
    int type = 0; // 0=empty, 1=body, 2=thrust, 3=cockpit
};

struct ShipSprite {
    static constexpr int SIZE = 5;
    SpriteCell cells[SIZE][SIZE];
};

namespace Sprites {
    inline const std::string RESET      = "\033[0m";
    inline const std::string BODY       = "\033[1;32m";
    inline const std::string COCKPIT    = "\033[1;36m";
    inline const std::string THRUST     = "\033[1;33m";
    inline const std::string THRUST_HOT = "\033[1;31m";

    const std::string& colorFor(int type, bool thrusting);
    int getSpriteIndex(double angleRad);
    const ShipSprite& getSprite(int index);
    void init();
}
