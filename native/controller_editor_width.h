#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <windows.h>

namespace ymcc::editorwidth {
// Physical window pixels; editor widths never overwrite normal window settings.
struct Session {
    std::string owner;
    RECT normalBounds{};
    bool addedThickFrame = false;
    double scale = 1.0;
    int baseWidth = 0;
    int maxWidth = 0;
    bool active() const { return !owner.empty(); }
};

inline int maximumWidth(int normalWidth, int workWidth) {
    return std::min(workWidth, normalWidth > workWidth / 2 ? workWidth : normalWidth * 2);
}

inline int scaledWidth(int normalWidth, int workWidth, double scale) {
    if (normalWidth <= 0 || workWidth <= 0) return 0;
    const int base = std::min(normalWidth, workWidth);
    if (!std::isfinite(scale)) scale = 1.0;
    return std::clamp(static_cast<int>(std::lround(base * std::clamp(scale, 1.0, 2.0))),
                      base, maximumWidth(base, workWidth));
}

}
