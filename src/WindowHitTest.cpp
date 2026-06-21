#include "WindowHitTest.h"

namespace GuiHit {

NonClientHit HitTestNonClient(const Input& input) {
    if (input.forceClient) return NonClientHit::Client;

    if (input.menu.Contains(input.x, input.y) ||
        input.theme.Contains(input.x, input.y) ||
        input.pin.Contains(input.x, input.y) ||
        input.close.Contains(input.x, input.y)) {
        return NonClientHit::Client;
    }

    const float edge = input.resizeEdge * input.dpiScale;
    const bool left = input.x < edge;
    const bool right = input.x >= input.width - edge;
    const bool top = input.y < edge;
    const bool bottom = input.y >= input.height - edge;

    if (top && left) return NonClientHit::TopLeft;
    if (top && right) return NonClientHit::TopRight;
    if (bottom && left) return NonClientHit::BottomLeft;
    if (bottom && right) return NonClientHit::BottomRight;
    if (left) return NonClientHit::Left;
    if (right) return NonClientHit::Right;
    if (top) return NonClientHit::Top;
    if (bottom) return NonClientHit::Bottom;

    if (input.y < input.titleHeight * input.dpiScale) {
        return input.capsuleMode ? NonClientHit::Client : NonClientHit::Caption;
    }
    return NonClientHit::Client;
}

} // namespace GuiHit
