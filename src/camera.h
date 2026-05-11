#pragma once
#include <optional>

struct ScreenPos { int col, row; };

class Camera {
public:
    double x = 0.0, y = 0.0;
    double zoom = 1.0;

    // Returns nullopt if center is off-screen (for point objects)
    std::optional<ScreenPos> worldToScreen(double wx, double wy,
                                           int screenCols, int screenRows) const;

    // Always returns screen position (for large objects that may be partially visible)
    ScreenPos worldToScreenRaw(double wx, double wy,
                               int screenCols, int screenRows) const;
};
