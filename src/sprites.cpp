#include "sprites.h"
#include "physics.h"
#include <cmath>

namespace Sprites {

static std::array<ShipSprite, 16> sprites_;
static bool initialized_ = false;

// Build sprite from 5 strings (each exactly 5 chars) + type grid
static ShipSprite make(const char* r0, const char* r1, const char* r2,
                       const char* r3, const char* r4,
                       const int t[5][5]) {
    ShipSprite sp{};
    const char* rows[5] = {r0, r1, r2, r3, r4};
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c) {
            sp.cells[r][c].ch = rows[r][c];
            sp.cells[r][c].type = t[r][c];
        }
    return sp;
}

void init() {
    if (initialized_) return;
    initialized_ = true;

    // 0: East -> (0 deg)
    { const int t[5][5]={
        {0,0,0,0,0},
        {0,0,1,1,0},
        {2,1,3,1,1},
        {0,0,1,1,0},
        {0,0,0,0,0}};
      sprites_[0]=make(
        "     ",
        "  /- ",
        "=-o->",
        "  \\- ",
        "     ",t); }

    // 1: ENE (22.5 deg)
    { const int t[5][5]={
        {0,0,0,1,0},
        {0,0,1,1,0},
        {2,1,3,1,0},
        {0,0,1,0,0},
        {0,0,0,0,0}};
      sprites_[1]=make(
        "   / ",
        "  // ",
        "=-o/ ",
        "  \\  ",
        "     ",t); }

    // 2: NE (45 deg)
    { const int t[5][5]={
        {0,0,0,0,1},
        {0,0,0,1,0},
        {0,0,3,0,0},
        {0,1,0,0,0},
        {2,0,0,0,0}};
      sprites_[2]=make(
        "    /",
        "   / ",
        "  o  ",
        " /   ",
        "=    ",t); }

    // 3: NNE (67.5 deg)
    { const int t[5][5]={
        {0,0,1,0,0},
        {0,1,1,0,0},
        {0,0,3,0,0},
        {0,0,1,0,0},
        {0,0,2,0,0}};
      sprites_[3]=make(
        "  /  ",
        " /|  ",
        "  o  ",
        "  |  ",
        "  =  ",t); }

    // 4: North (90 deg)
    { const int t[5][5]={
        {0,0,1,0,0},
        {0,1,0,1,0},
        {0,0,3,0,0},
        {0,1,0,1,0},
        {0,0,2,0,0}};
      sprites_[4]=make(
        "  ^  ",
        " / \\ ",
        "  o  ",
        " | | ",
        "  =  ",t); }

    // 5: NNW (112.5 deg)
    { const int t[5][5]={
        {0,0,1,0,0},
        {0,0,1,1,0},
        {0,0,3,0,0},
        {0,0,1,0,0},
        {0,0,2,0,0}};
      sprites_[5]=make(
        "  \\  ",
        "  |\\ ",
        "  o  ",
        "  |  ",
        "  =  ",t); }

    // 6: NW (135 deg)
    { const int t[5][5]={
        {1,0,0,0,0},
        {0,1,0,0,0},
        {0,0,3,0,0},
        {0,0,0,1,0},
        {0,0,0,0,2}};
      sprites_[6]=make(
        "\\    ",
        " \\   ",
        "  o  ",
        "   \\ ",
        "    =",t); }

    // 7: WNW (157.5 deg)
    { const int t[5][5]={
        {0,0,0,0,0},
        {0,0,1,0,0},
        {1,1,3,1,2},
        {0,0,1,0,0},
        {0,0,0,0,0}};
      sprites_[7]=make(
        "     ",
        "  \\  ",
        "<-o-=",
        "  /  ",
        "     ",t); }

    // 8: West (180 deg)
    { const int t[5][5]={
        {0,0,0,0,0},
        {0,1,1,0,0},
        {1,1,3,1,2},
        {0,1,1,0,0},
        {0,0,0,0,0}};
      sprites_[8]=make(
        "     ",
        " -/  ",
        "<-o-=",
        " -\\  ",
        "     ",t); }

    // 9: WSW (202.5 deg)
    { const int t[5][5]={
        {0,0,0,0,0},
        {0,0,1,0,0},
        {1,1,3,1,2},
        {0,0,1,1,0},
        {0,0,0,1,0}};
      sprites_[9]=make(
        "     ",
        "  /  ",
        "<-o-=",
        "  \\\\ ",
        "   \\ ",t); }

    // 10: SW (225 deg)
    { const int t[5][5]={
        {0,0,0,0,2},
        {0,0,0,1,0},
        {0,0,3,0,0},
        {0,1,0,0,0},
        {1,0,0,0,0}};
      sprites_[10]=make(
        "    =",
        "   / ",
        "  o  ",
        " /   ",
        "/    ",t); }

    // 11: SSW (247.5 deg)
    { const int t[5][5]={
        {0,0,2,0,0},
        {0,0,1,0,0},
        {0,0,3,0,0},
        {0,0,1,1,0},
        {0,0,1,0,0}};
      sprites_[11]=make(
        "  =  ",
        "  |  ",
        "  o  ",
        "  |/ ",
        "  /  ",t); }

    // 12: South (270 deg)
    { const int t[5][5]={
        {0,0,2,0,0},
        {0,1,0,1,0},
        {0,0,3,0,0},
        {0,1,0,1,0},
        {0,0,1,0,0}};
      sprites_[12]=make(
        "  =  ",
        " | | ",
        "  o  ",
        " \\ / ",
        "  v  ",t); }

    // 13: SSE (292.5 deg)
    { const int t[5][5]={
        {0,0,2,0,0},
        {0,0,1,0,0},
        {0,0,3,0,0},
        {0,1,1,0,0},
        {0,0,1,0,0}};
      sprites_[13]=make(
        "  =  ",
        "  |  ",
        "  o  ",
        " \\|  ",
        "  \\  ",t); }

    // 14: SE (315 deg)
    { const int t[5][5]={
        {2,0,0,0,0},
        {0,1,0,0,0},
        {0,0,3,0,0},
        {0,0,0,1,0},
        {0,0,0,0,1}};
      sprites_[14]=make(
        "=    ",
        " \\   ",
        "  o  ",
        "   \\ ",
        "    \\",t); }

    // 15: ESE (337.5 deg)
    { const int t[5][5]={
        {0,0,0,0,0},
        {0,0,1,0,0},
        {2,1,3,1,1},
        {0,0,1,1,0},
        {0,0,0,1,0}};
      sprites_[15]=make(
        "     ",
        "  /  ",
        "=-o->",
        "  \\\\ ",
        "   \\ ",t); }
}

static const std::string EMPTY;

const std::string& colorFor(int type, bool thrusting) {
    switch (type) {
        case 1: return BODY;
        case 2: return thrusting ? THRUST : EMPTY;
        case 3: return COCKPIT;
        default: return EMPTY;
    }
}

int getSpriteIndex(double angleRad) {
    double a = Physics::normalizeAngle(angleRad);
    return static_cast<int>(std::round(a / (2.0 * M_PI / 16.0))) % 16;
}

const ShipSprite& getSprite(int index) {
    if (!initialized_) init();
    return sprites_[index & 15];
}

} // namespace Sprites
