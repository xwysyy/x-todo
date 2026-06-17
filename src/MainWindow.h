#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "TodoModel.h"
#include "Store.h"
#include "I18n.h"

constexpr UINT WM_TRAY     = WM_APP + 1; // 托盘回调消息
constexpr UINT WM_APP_SHOW = WM_APP + 2; // 第二实例请求显示
inline constexpr wchar_t kWindowClass[] = L"XTodoWindowClass";

// 挂载形态：普通窗口 / 挂到桌面层 / 侧边吸附胶囊
enum class MountMode { Normal, Desktop, Capsule };

// 胶囊外观样式（仅 Capsule 形态）：细边长条 / 圆点
enum class CapsuleStyle { Slim, Dot };

// 胶囊吸附的屏幕竖边
enum class DockEdge { Left, Right };

// 桌面便签主窗口：无边框、Direct2D 自绘、托盘常驻。
class MainWindow {
public:
    bool Create();
    void Show(bool expandCapsule = true);
    HWND Hwnd() const { return hwnd_; }

private:
    // —— 窗口生命周期 ——
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
    bool RegisterWindowClass();

    // —— Direct2D 资源 ——
    bool CreateDeviceIndependentResources();
    bool CreateDeviceResources();
    void DiscardDeviceResources();
    bool Render(); // 返回 false 表示设备丢失等，本帧未成功绘制
    void Resize(UINT w, UINT h);

    // —— 布局与命中 ——
    struct RowLayout {
        int itemIndex;
        bool completed;
        D2D1_RECT_F row;    // 文档坐标（y 从内容区顶部 0 起算）
        D2D1_RECT_F check;
        D2D1_RECT_F text;
        D2D1_RECT_F del;
        D2D1_RECT_F handle;
        mutable IDWriteTextLayout* strikeLayout = nullptr; // 完成项删除线布局，首帧创建后缓存
    };
    enum class HitKind { None, Check, Text, Delete, Handle, Section, Clear, Footer, Pin, Close, Menu };
    struct Hit { HitKind kind = HitKind::None; int rowIndex = -1; int itemIndex = -1; };

    void  RebuildLayout();
    float ContentHeight() const;
    void  ClampScroll();
    float ContentTop() const;     // 内容区顶部 y（物理像素）
    float ViewportHeight() const; // 可滚动视口高度（物理像素）
    Hit   HitTest(float x, float y);

    // —— 渲染细节 ——
    void DrawRow(const RowLayout& r, bool hovered);
    void DrawCheckbox(const D2D1_RECT_F& box, bool checked);
    void DrawTitleBar();
    void DrawSection(); // 已完成折叠条（内容层，文档坐标）
    void DrawFooter();  // 底部新增区（固定）
    void FillRect(const D2D1_RECT_F& r, uint32_t rgb, float a = 1.0f);
    void StrokeRect(const D2D1_RECT_F& r, uint32_t rgb, float w, float a = 1.0f);
    void Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t rgb,
              IDWriteTextFormat* fmt);

    // —— 输入 ——
    void    OnLButtonDown(float x, float y);
    void    OnLButtonUp(float x, float y);
    void    OnMouseMove(float x, float y, bool lButton);
    void    OnMouseLeave();
    void    OnMouseWheel(int delta);
    LRESULT OnNcHitTest(int sx, int sy);

    // —— 行内编辑（浮动 EDIT 子控件，原生 IME）——
    void BeginEdit(int itemIndex);
    void CommitEdit(bool addNext);
    void CancelEdit();
    void LayoutEditBox();
    bool editing() const { return editIndex_ >= 0; }
    static LRESULT CALLBACK EditProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    // —— 托盘 ——
    bool  AddTrayIcon();
    void  RemoveTrayIcon();
    HICON CreateTrayIconHandle();
    void  ShowTrayMenu();
    void  ShowTitleMenu();                 // 标题栏菜单按钮弹出
    void  AppendMenuItems(HMENU menu);     // 模式 + 语言 + 自启（托盘与标题栏共用）
    void  HandleMenuCommand(UINT cmd);
    void  SetLanguage(Lang lang);

    // —— 挂载形态（普通 / 挂桌面 / 侧边胶囊）——
    void        SetMountMode(MountMode m);
    void        ApplyMountMode();
    void        SetCapsuleStyle(CapsuleStyle s);
    void        StartCapsuleAnim(bool expand);
    void        OnAnimTick();
    void        MaybeCollapseCapsule(); // 编辑结束后若鼠标已在窗口外则收回胶囊
    RECT        CapsuleTargetRect() const;  // 折叠贴边矩形（屏幕坐标）
    RECT        ExpandedTargetRect() const; // 胶囊展开后的矩形（屏幕坐标）
    void        CaptureVisibleGeometry();
    bool        capsuleShrunk() const {
        return mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_;
    }

    // —— 胶囊拖动 / 吸附（折叠态可拖到任意显示器的左 / 右竖边）——
    void        BeginCapsulePress(int x, int y);
    void        UpdateCapsulePress(bool lButton);
    void        FinishCapsulePress();
    void        CancelCapsulePress();
    void        SnapCapsuleToNearestEdge();
    DockEdge    CapsuleDockEdge() const;
    double      CapsuleDockT() const;
    bool        DockMonitorInfo(MONITORINFOEXW& mi) const;
    HMONITOR    FindMonitorByDevice(const std::string& device) const;
    void        UpdateLayeredState();  // 按样式 / 折叠 / hover 维护 WS_EX_LAYERED 整窗 alpha
    void        UpdateCapsuleRegion(); // 圆点折叠态用椭圆窗口区域确保圆形（不依赖 DWM 圆角 hint）

    // —— 行为 ——
    bool Confirm(Str message, UINT icon);
    void ApplyTopmost();
    void TogglePin();
    void ToggleCompletedExpanded();
    void ToggleAutostart();
    void DeleteItem(int itemIndex);
    void ClearCompletedConfirm();
    void HideToTray();
    void ExitApp();

    // —— 保存 ——
    void ScheduleSave();
    void SaveNow();
    void CaptureGeometry();

    // —— DPI ——
    float dpiScale() const { return dpi_ / 96.0f; }
    float S(float v) const { return v * dpiScale(); }

    // —— 状态 ——
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    UINT dpi_  = 96;

    ID2D1Factory*          d2dFactory_  = nullptr;
    ID2D1HwndRenderTarget* rt_          = nullptr;
    IDWriteFactory*        dwrite_      = nullptr;
    IDWriteTextFormat*     textFormat_  = nullptr;
    IDWriteTextFormat*     smallFormat_ = nullptr;
    ID2D1SolidColorBrush*  brush_       = nullptr;

    TodoModel      model_;
    WindowGeometry geom_;
    UiState        ui_;

    std::vector<RowLayout> rows_;
    float       scroll_       = 0.0f;
    float       contentH_     = 0.0f;
    D2D1_RECT_F sectionRect_{}; // 已完成折叠条（文档坐标）
    D2D1_RECT_F clearRect_{};   // 清空已完成（文档坐标）
    D2D1_RECT_F pinRect_{};     // 置顶按钮（固定）
    D2D1_RECT_F closeRect_{};   // 关闭按钮（固定）
    D2D1_RECT_F menuRect_{};    // 标题栏菜单按钮（固定）

    int   hoverRow_   = -1;
    int   editIndex_  = -1;
    bool  dragging_   = false;
    int   dragFrom_   = -1;
    float dragY_      = 0.0f;
    int   dragInsert_ = -1;
    Hit   pressHit_;

    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    UINT taskbarCreatedMsg_ = 0; // Explorer 重启后重建托盘图标的消息 ID

    Lang         lang_            = Lang::Zh;
    MountMode    mountMode_       = MountMode::Normal;
    CapsuleStyle capsuleStyle_    = CapsuleStyle::Slim; // 胶囊外观
    bool      capsuleExpanded_ = false;  // 胶囊形态下是否已滑出
    bool      animActive_     = false;
    bool      capsulePressing_ = false;  // 折叠胶囊：鼠标按下中（待区分点击 / 拖动）
    bool      capsuleDragging_ = false;  // 折叠胶囊：已越过阈值进入拖动
    bool      capsuleHover_    = false;  // 折叠胶囊：鼠标悬停视觉提示
    POINT     capsulePressClient_{};     // 按下点（客户坐标，拖动时窗口跟随）
    POINT     capsulePressScreen_{};     // 按下点（屏幕坐标，阈值判定）
    RECT      animFrom_{};
    RECT      animTo_{};
    int       animStep_       = 0;
    static constexpr UINT_PTR kAnimTimerId = 2;
    static constexpr int      kAnimSteps   = 16; // 滑动动画帧数（配合 15ms/帧，放慢更柔和）

    static constexpr UINT_PTR kSaveTimerId = 1;
    bool savePending_ = false;

    HFONT  editFont_ = nullptr; // 行内编辑框字体
    HBRUSH editBg_   = nullptr; // 行内编辑框背景刷（贴合纸张色）
};
