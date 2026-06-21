#include "GeometryPolicy.h"

namespace GuiGeometry {

int ScaleToPx(int logical, float dpiScale) {
    return static_cast<int>(static_cast<float>(logical) * dpiScale);
}

Size MinimumTrackSize(float dpiScale) {
    return Size{ ScaleToPx(kMinWindowW, dpiScale), ScaleToPx(kMinWindowH, dpiScale) };
}

bool AcceptsGeometrySize(int w, int h, float dpiScale) {
    const Size min = MinimumTrackSize(dpiScale);
    return w >= min.w && h >= min.h;
}

bool AcceptsLoadedGeometrySize(int w, int h, float dpiScale) {
    return AcceptsGeometrySize(w, h, dpiScale) &&
           w <= kMaxLoadedWindowW &&
           h <= kMaxLoadedWindowH;
}

CaptureDecision DecideCapture(const CaptureInput& input) {
    if (!AcceptsGeometrySize(input.w, input.h, input.dpiScale)) return {};

    if (input.mountMode == MountMode::Capsule) {
        if (!input.capsuleExpanded || input.animActive) return {};
        return CaptureDecision{ true, false, true };
    }
    return CaptureDecision{ true, true, false };
}

bool ShouldCaptureBeforeModeSwitch(bool capsuleShrunk) {
    return !capsuleShrunk;
}

Size ExpandedSize(bool geomValid, int geomW, int geomH, int workW, int workH) {
    Size out{ geomValid ? geomW : kDefaultWindowW + 40,
              geomValid ? geomH : kDefaultWindowH + 40 };
    if (workW > 0 && out.w > workW) out.w = workW;
    if (workH > 0 && out.h > workH) out.h = workH;
    return out;
}

} // namespace GuiGeometry
