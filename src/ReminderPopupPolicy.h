#pragma once

struct ReminderPopupWorkArea {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct ReminderPopupPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline int ReminderPopupScale(int value, unsigned int dpi) {
    return static_cast<int>((static_cast<long long>(value) * dpi + 48) / 96);
}

inline ReminderPopupPlacement ComputeReminderPopupPlacement(ReminderPopupWorkArea work,
                                                            unsigned int dpi) {
    if (dpi == 0) dpi = 96;
    const int margin = ReminderPopupScale(20, dpi);
    const int preferredW = ReminderPopupScale(330, dpi);
    const int preferredH = ReminderPopupScale(152, dpi);
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;

    ReminderPopupPlacement out;
    out.width = preferredW;
    out.height = preferredH;
    if (workW > 0 && out.width > workW) out.width = workW;
    if (workH > 0 && out.height > workH) out.height = workH;
    out.x = work.right - out.width - margin;
    out.y = work.bottom - out.height - margin;
    if (out.x < work.left) out.x = work.left;
    if (out.y < work.top) out.y = work.top;
    return out;
}
