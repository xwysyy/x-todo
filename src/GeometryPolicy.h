#pragma once

namespace GuiGeometry {

inline constexpr int kDefaultWindowW = 260;
inline constexpr int kDefaultWindowH = 340;
inline constexpr int kMinWindowW = 220;
inline constexpr int kMinWindowH = 160;
inline constexpr int kMaxLoadedWindowW = 4000;
inline constexpr int kMaxLoadedWindowH = 4000;

enum class MountMode {
    Normal,
    Desktop,
    Capsule,
};

struct Size {
    int w = 0;
    int h = 0;
};

struct CaptureInput {
    int w = 0;
    int h = 0;
    float dpiScale = 1.0f;
    MountMode mountMode = MountMode::Normal;
    bool capsuleExpanded = false;
    bool animActive = false;
};

struct CaptureDecision {
    bool accept = false;
    bool capturePosition = false;
    bool captureDock = false;
};

int ScaleToPx(int logical, float dpiScale);
Size MinimumTrackSize(float dpiScale);
bool AcceptsGeometrySize(int w, int h, float dpiScale);
bool AcceptsLoadedGeometrySize(int w, int h, float dpiScale);
CaptureDecision DecideCapture(const CaptureInput& input);
bool ShouldCaptureBeforeModeSwitch(bool capsuleShrunk);
Size ExpandedSize(bool geomValid, int geomW, int geomH, int workW, int workH);

} // namespace GuiGeometry
