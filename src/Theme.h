#pragma once
#include <d2d1.h>
#include <cstdint>

// 固定浅色纸张配色 + 布局尺寸。尺寸为逻辑像素（96dpi 基准），渲染时乘 dpiScale。
namespace Theme {

// 0xRRGGBB -> D2D1_COLOR_F
inline D2D1_COLOR_F Color(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        a);
}

// —— 配色 ——
constexpr uint32_t kPaper       = 0xFBF7EC; // 便签背景（暖白纸）
constexpr uint32_t kPaperEdge   = 0xEAE1CC; // 纸张描边
constexpr uint32_t kText        = 0x33312C; // 主文本
constexpr uint32_t kTextDone    = 0xAAA395; // 已完成文本（淡）
constexpr uint32_t kTextWeak    = 0x9A9384; // 次要文字（计数 / 提示）
constexpr uint32_t kCheckBorder = 0xB8AE97; // 复选框边框
constexpr uint32_t kCheckFill   = 0x7C9A6B; // 勾选填充（柔绿）
constexpr uint32_t kCheckMark   = 0xFFFFFF; // 对勾
constexpr uint32_t kHover       = 0x000000; // 悬停叠加（低 alpha 使用）
constexpr uint32_t kDivider     = 0xE7DECB; // 分隔线
constexpr uint32_t kDanger      = 0xC8755E; // 删除 / 危险
constexpr uint32_t kHandle      = 0xC4BBA6; // 拖动手柄
constexpr uint32_t kDragGhost   = 0xEFE7D4; // 拖动占位高亮
constexpr uint32_t kCheckFillHover = 0x6E8A5D; // 圆点胶囊悬停：加深柔绿
constexpr uint32_t kHandleHover    = 0xB0A78F; // 圆点胶囊悬停：加深灰

// —— 尺寸（逻辑像素）——
constexpr float kCorner     = 12.0f; // 窗口圆角半径
constexpr float kPadX       = 14.0f; // 左右内边距
constexpr float kTitleH     = 34.0f; // 顶部标题 / 拖动条高度
constexpr float kRowH       = 34.0f; // 条目行高
constexpr float kCheckSize  = 18.0f; // 复选框边长
constexpr float kFontSize   = 14.0f; // 正文字号
constexpr float kSmallFont  = 12.0f; // 小字号
constexpr float kResizeEdge = 6.0f;  // 缩放命中边缘宽度
constexpr float kFooterH    = 0.0f;  // 固定底栏高度；新增入口已移入内容区加号行
constexpr float kSectionH   = 26.0f; // 已完成折叠条高度

// 胶囊折叠态尺寸与半透明（仅 Capsule 形态使用）
constexpr float kCapsuleSlimW     = 18.0f; // 细边胶囊折叠宽
constexpr float kCapsuleSlimH     = 96.0f; // 细边胶囊折叠高
constexpr float kCapsuleDot       = 20.0f; // 圆点胶囊直径（折叠为正方）
constexpr float kCapsuleSlimAlpha = 0.60f; // 细边折叠静止不透明度（hover/展开=1.0）

constexpr wchar_t kFontFamily[] = L"Microsoft YaHei UI"; // 含中英文字形

} // namespace Theme
