#include "camera.h"
#include <cmath>

static ScreenPos computeScreenPos(double dx, double dy, double zoom,
                                   int screenCols, int screenRows) {
    return {
        static_cast<int>(std::round(dx * zoom)) + screenCols / 2,
        static_cast<int>(std::round(-dy * zoom / 2.0)) + screenRows / 2
    };
}

std::optional<ScreenPos> Camera::worldToScreen(double wx, double wy,
                                                int screenCols, int screenRows) const {
    ScreenPos sp = computeScreenPos(wx - x, wy - y, zoom, screenCols, screenRows);
    if (sp.col < 0 || sp.col >= screenCols || sp.row < 0 || sp.row >= screenRows)
        return std::nullopt;
    return sp;
}

ScreenPos Camera::worldToScreenRaw(double wx, double wy,
                                    int screenCols, int screenRows) const {
    return computeScreenPos(wx - x, wy - y, zoom, screenCols, screenRows);
}
