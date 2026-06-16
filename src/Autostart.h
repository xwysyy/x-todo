#pragma once

// 开机自启：写 / 删 HKCU 的 Run 注册表项。
namespace Autostart {
    bool IsEnabled();
    bool SetEnabled(bool on);
}
