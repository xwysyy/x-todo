#pragma once

namespace Gui {

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool Contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }

    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
};

} // namespace Gui
