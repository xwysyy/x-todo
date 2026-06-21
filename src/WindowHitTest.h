#pragma once

#include "GuiTypes.h"

namespace GuiHit {

enum class NonClientHit {
    Client,
    Caption,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct Input {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float dpiScale = 1.0f;
    float titleHeight = 0.0f;
    float resizeEdge = 0.0f;
    bool forceClient = false;
    bool capsuleMode = false;
    Gui::Rect menu;
    Gui::Rect theme;
    Gui::Rect pin;
    Gui::Rect close;
};

NonClientHit HitTestNonClient(const Input& input);

} // namespace GuiHit
