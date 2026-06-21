#pragma once

#include <cstdint>

// 日历事件块的固定调色板。
//
// 这是日历视图专属色，不属于 Theme::ColorSet（那套是 todo 便签的多主题皮肤）。事件块是
// 浅底深字的便签贴片，跨主题恒定，不随明暗主题反色。理由与 Google / Apple 日历的事件色
// 一致：块底若跟着主题反深、块字又用主题文字色，会糊成一团；固定成浅底深字在任何主题下都
// 清楚。窗口纸张、网格、文字、导航按钮、当前时间线仍走 Theme::ColorSet，跟随主题。
namespace CalendarTheme {

struct BlockColor {
    uint32_t fill; // 块填充
    uint32_t edge; // 块边框（prototype 的半透明描边已预混到填充上）
};

// 按当天块的时间顺序轮转的三色（源自 prototype：蓝 / 米 / 玫瑰）。
constexpr BlockColor kPalette[3] = {
    { 0xEDF1F7, 0xC1CAD5 }, // 蓝
    { 0xF3EAD9, 0xD5C4A8 }, // 米
    { 0xF0E6E3, 0xD6C0BA }, // 玫瑰
};

// 时间重叠的块。
constexpr BlockColor kConflict = { 0xF4DFDB, 0xCA918B };

// 块内文字（浅底上的深字，所有块色共用）。
constexpr uint32_t kBlockTitle = 0x24221F; // 标题
constexpr uint32_t kBlockTime  = 0x706D65; // 时间副文字

inline const BlockColor& BlockColorAt(int index) {
    int i = index % 3;
    if (i < 0) i += 3;
    return kPalette[i];
}

} // namespace CalendarTheme
