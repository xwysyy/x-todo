#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "CalendarLayout.h"
#include "ReminderService.h"
#include "TodoModel.h"
#include "Store.h"
#include "I18n.h"
#include "Theme.h"

constexpr UINT WM_TRAY     = WM_APP + 1; // 托盘回调消息
constexpr UINT WM_APP_SHOW = WM_APP + 2; // 第二实例请求显示
constexpr UINT WM_APP_REMINDER_OPEN = WM_APP + 3; // 提醒弹窗请求打开日历块
constexpr UINT WM_APP_REMINDER_CHECK = WM_APP + 4; // 提醒 fallback 请求执行一次检查
inline constexpr wchar_t kWindowClass[] = L"XTodoWindowClass";

// 挂载形态：普通窗口 / 挂到桌面层 / 侧边吸附胶囊
enum class MountMode { Normal, Desktop, Capsule };

// 胶囊外观样式（仅 Capsule 形态）。存档名：Slim=slim(魔方) / Dot=dot(精灵球) /
// Bar=bar(细边) / Pip=pip(圆点)。魔方与精灵球为固定色装饰，细边/圆点受主题约束。
enum class CapsuleStyle { Slim, Dot, Bar, Pip };

// 胶囊吸附的屏幕竖边
enum class DockEdge { Left, Right };

enum class MainView { Lists, Calendar };

// 桌面便签主窗口：无边框、Direct2D 自绘、托盘常驻。
class MainWindow {
public:
    ~MainWindow();
    bool Create();
    void Show(bool expandCapsule = true);
    void InitialShow(); // 冷启动首显
    HWND Hwnd() const { return hwnd_; }
    void OpenReminderTarget(int blockId);
    void RunReminderCheckOnce(bool backgroundOnly = false);

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
    bool RenderCapsuleEntryLayered(); // 折叠态侧边入口：per-pixel alpha 固定色视觉
    void Resize(UINT w, UINT h);

    // —— 布局与命中 ——
    struct RowLayout {
        int itemIndex;
        bool completed;
        D2D1_RECT_F row;    // 文档坐标（y 从内容区顶部 0 起算）
        D2D1_RECT_F disclosure;
        D2D1_RECT_F check;
        D2D1_RECT_F text;
        D2D1_RECT_F del;
        D2D1_RECT_F handle;
        bool hasChildren = false;
        bool collapsed = false;
        mutable IDWriteTextLayout* strikeLayout = nullptr; // 完成项删除线布局，首帧创建后缓存
    };
    struct ListTabLayout {
        int listIndex = -1;
        D2D1_RECT_F rect{};
    };
    enum class HitKind {
        None,
        TreeToggle,
        Check,
        Text,
        Delete,
        Handle,
        Section,
        Clear,
        EmptyActive,
        Calendar,
        ListTab,
        AddList,
        AddTask,
        CalendarPrevDay,
        CalendarNextDay,
        CalendarToday,
        CalendarEmptyTimeline,
        CalendarBlock,
        CalendarResizeStart,
        CalendarResizeEnd,
        CalendarModeDay,
        CalendarModeWeek,
        CalendarModeMonth,
        CalendarWeekDayHeader,
        CalendarWeekBlock,
        CalendarMonthCell,
        Pin,
        Close,
        Menu,
        Theme
    };
    struct Hit { HitKind kind = HitKind::None; int rowIndex = -1; int itemIndex = -1; };

    void  RebuildLayout();
    float MeasureRowHeight(const std::wstring& text, float textWidth);
    float ContentHeight() const;
    void  ClampScroll();
    void  ScrollItemIntoView(int itemIndex);
    float ContentTop() const;     // 内容区顶部 y（物理像素）
    float ViewportHeight() const; // 可滚动视口高度（物理像素）
    Hit   HitTest(float x, float y);

    // —— 渲染细节 ——
    void DrawRow(const RowLayout& r, bool hovered);
    void DrawCheckbox(const D2D1_RECT_F& box, bool checked);
    void DrawTitleBar();
    void DrawListTabs();
    void DrawCalendarView(float windowWidth, float windowHeight);
    void DrawCalendarDay(float windowWidth, float windowHeight);
    void DrawCalendarWeek(float windowWidth, float windowHeight);
    void DrawCalendarMonth(float windowWidth, float windowHeight);
    void DrawCalendarHeader(const GuiCalendar::HeaderLayout& header, const std::wstring& title);
    std::wstring CalendarHeaderTitle(bool compact) const;
    float MeasureCalendarText(const std::wstring& text, const D2D1_RECT_F& rect);
    float DrawCalendarWrappedText(const std::wstring& text, const D2D1_RECT_F& rect, uint32_t color);
    void DrawTimelineBlockText(const CalendarBlock& block, const D2D1_RECT_F& blockRect,
                               bool includeTime);
    void DrawSection(); // 已完成折叠条（内容层，文档坐标）
    void DrawEmptyActivePrompt(bool hovered); // 空列表的居中提示（图标 + 文案 + 新建按钮）
    void DrawAddTaskRow(bool hovered);        // 列表底部常驻"新建待办"入口
    void FillRect(const D2D1_RECT_F& r, uint32_t rgb, float a = 1.0f);
    void StrokeRect(const D2D1_RECT_F& r, uint32_t rgb, float w, float a = 1.0f);
    void FillRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float a = 1.0f);
    void StrokeRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float w, float a = 1.0f);
    void DrawSurfaceFrame(const D2D1_RECT_F& r, float radius, uint32_t fill,
                          uint32_t edge, float stroke = 1.0f);
    void Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t rgb,
              IDWriteTextFormat* fmt);

    // —— 输入 ——
    void    OnLButtonDown(float x, float y);
    void    OnLButtonUp(float x, float y);
    void    OnLButtonDoubleClick(float x, float y);
    void    OnRButtonUp(float x, float y);
    void    OnMouseMove(float x, float y, bool lButton);
    void    OnMouseLeave();
    void    OnMouseWheel(int delta);
    LRESULT OnNcHitTest(int sx, int sy);

    // —— 行内编辑（浮动 EDIT 子控件，原生 IME）——
    void BeginEdit(int itemIndex);
    void CommitEdit(bool addNext);
    void CancelEdit();
    void LayoutEditBox();
    int  PreviousVisibleActiveItem(int itemIndex) const;
    bool editing() const { return editIndex_ >= 0; }
    static LRESULT CALLBACK EditProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    // —— 日历编辑（独立顶层标签页，不影响 TodoModel 当前列表）——
    enum class CalendarEditFocus { Title, StartTime, EndTime };
    void EnsureCalendarDay();
    void SetActiveView(MainView view);
    void SwitchCalendarDay(int deltaDays);
    void GoToCalendarToday();
    CalendarViewMode calendarMode() const { return ui_.calendarView; }
    void SetCalendarMode(CalendarViewMode mode);
    void SwitchCalendarPeriod(int dir);
    void DrillToCalendarDay(const std::string& dayKey);
    std::string CalendarWeekDayKey(int dayIndex) const;
    std::string CalendarMonthCellDayKey(int cellIndex) const;
    void BuildCalendarWeekBlockRects();
    bool calendarActive() const { return activeView_ == MainView::Calendar; }
    bool calendarEditing() const { return calendarEditId_ >= 0; }
    void ClampCalendarScroll();
    void AlignCalendarScrollToNow(bool force);
    void BuildCalendarBlockRects();
    void BeginCalendarEdit(int blockId, CalendarEditFocus focus);
    void EndCalendarEdit(bool removeEmpty);
    void HideCalendarEditors();
    void EnsureCalendarEditors();
    void SyncCalendarEditors();
    void LayoutCalendarEditControls();
    void OnCalendarEditChanged(HWND edit);
    bool CommitCalendarTimeEdits(bool syncText);
    bool CalendarEditSurfaceContainsPoint(int blockId, float x, float y) const;
    CalendarEditFocus CalendarEditFocusFromPoint(int blockId, float x, float y) const;
    CalendarEditFocus CalendarEditFocusFromHwnd(HWND edit) const;
    CalendarEditFocus NextCalendarEditFocus(HWND edit, bool reverse) const;
    void FocusCalendarEditor(CalendarEditFocus focus, bool selectAll);
    bool IsCalendarEditorHwnd(HWND edit) const;
    bool IsCalendarEditInternalFocusTarget(HWND target) const;
    bool CalendarBlockTitleEmpty(int blockId) const;
    void ResetCalendarDrag();
    void CancelCalendarCapture();
    std::string OffsetCalendarDayKey(int deltaDays) const;
    static LRESULT CALLBACK CalendarEditProcStatic(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    // —— 托盘 ——
    bool  AddTrayIcon();
    void  RemoveTrayIcon();
    HICON CreateTrayIconHandle();
    void  RefreshTrayIcon();
    void  ShowTrayMenu();
    void  ShowTitleMenu();                 // 标题栏菜单按钮弹出
    void  ShowThemeMenu();                 // 标题栏皮肤按钮弹出
    void  ShowSettings();
    void  HandleMenuCommand(UINT cmd);
    void  SetLanguage(Lang lang);
    void  SwitchList(int index);
    void  CreateList();
    void  RenameList(int index);
    void  DeleteList(int index);
    void  ShowListTabMenu(int index, float x, float y);
    void  ShowCalendarBlockMenu(int blockId, float x, float y);
    void  DeleteCalendarBlock(int blockId);

    // —— 主题 ——
    void ReloadThemes();                   // 重扫 %APPDATA%\x-todo\themes\ 自定义主题目录
    void ApplyResolvedTheme(bool persist); // 读系统明暗 / 高对比 + ResolveTheme + 刷新全部 surface
    void SetThemeMode(const std::string& mode);  // builtin | custom | follow_system
    void SetThemeId(const std::string& id);      // 切具体主题（退出 follow_system）
    void SetFollowSystemThemes(const std::string& lightId, const std::string& darkId);
    void ShowThemeManager();                     // 打开主题管理窗口
    const Theme::ThemeVisual& theme() const { return theme_; }

    // —— 挂载形态（普通 / 挂桌面 / 侧边胶囊）——
    void        SetMountMode(MountMode m);
    void        ApplyMountMode();
    void        SetCapsuleStyle(CapsuleStyle s);
    void        StartCapsuleAnim(bool expand);
    void        OnAnimTick();
    void        OnHoverTick();           // 折叠入口 hover 缓动推进
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
    void        CaptureCapsuleDockFromRect(const RECT& wr);
    void        UpdateLayeredState();  // 折叠入口维护 WS_EX_LAYERED per-pixel alpha
    void        UpdateCapsuleRegion(); // 折叠入口压制 DWM 边框 / 阴影，展开态恢复普通窗口

    // —— 行为 ——
    bool Confirm(Str message, UINT icon);
    bool ConfirmText(const std::wstring& message, bool danger = true);
    bool PromptText(const std::wstring& prompt, std::wstring& value);
    void ApplyTopmost();
    void TogglePin();
    void ToggleCompletedExpanded();
    void SetAutostart(bool enabled);
    void CreateEmptyActiveItem();
    void BeginNewTask(); // 新建一条待办并进入编辑（不受当前是否已有未完成项限制）
    void DeleteItem(int itemIndex);
    void ClearCompletedConfirm();
    void HideToTray();
    void ExitApp();

    // —— 保存 ——
    void ScheduleSave();
    bool SaveNow();
    void CaptureGeometry();
    void SyncActiveEditorsForSave();
    bool ChooseBackupDirectory(HWND owner, std::wstring& out);
    void SetBackupDirectoryFromUser(HWND owner);
    void DisableAutoBackup();
    void StartBackupTimer();
    void StopBackupTimer();
    void MaybeRunAutoBackup(bool force);
    bool RunAutoBackupNow();
    std::wstring BackupStatusText() const;
    void RefreshReminderSchedule();
    void RefreshReminderSchedulerFallback();
    void StartReminderTimer();
    void StopReminderTimer();
    void OnReminderTimer(bool catchUp);
    bool DispatchReminders(const std::vector<ReminderCandidate>& due);
    bool ShowSystemReminder(const std::vector<ReminderCandidate>& due);
    bool ShowBackgroundSystemReminder(const std::vector<ReminderCandidate>& due);
    bool StartCapsuleReminderPulse(const std::vector<ReminderCandidate>& due);

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
    IDWriteTextFormat*     calendarTextFormat_ = nullptr;
    ID2D1SolidColorBrush*  brush_       = nullptr;

    TodoModel      model_;
    CalendarModel  calendar_;
    ReminderService reminders_;
    WindowGeometry geom_;
    UiState        ui_;

    // —— 主题 ——
    Theme::ThemeVisual              theme_;        // 渲染层唯一读取的已解析主题快照
    std::vector<Theme::ThemeVisual> customThemes_; // 已加载的自定义主题
    std::vector<Theme::ThemeIssue>  themeIssues_;  // 自定义主题加载 issue（文件级）
    std::vector<Theme::ThemeNotice> themeNotices_; // 运行时 notice（fallback / 系统读取 / 托盘失败）

    std::vector<RowLayout> rows_;
    std::vector<ListTabLayout> listTabs_;
    MainView    activeView_ = MainView::Lists;
    std::string calendarDay_;
    GuiCalendar::Frame calendarFrame_{};
    std::vector<GuiCalendar::BlockRect> calendarBlockRects_;
    GuiCalendar::WeekFrame calendarWeekFrame_{};
    GuiCalendar::MonthFrame calendarMonthFrame_{};
    std::vector<GuiCalendar::WeekBlockRect> calendarWeekBlockRects_;
    float       calendarScroll_ = 0.0f;
    bool        calendarScrollInitialized_ = false;
    float       scroll_       = 0.0f;
    float       contentH_     = 0.0f;
    float       activeEndY_   = 0.0f; // 未完成段末尾 y，用于拖拽插入线
    D2D1_RECT_F emptyActiveRect_{}; // 空列表居中提示区域（文档坐标）
    D2D1_RECT_F addTaskRect_{};     // 列表底部"新建待办"入口（文档坐标）
    D2D1_RECT_F sectionRect_{}; // 已完成折叠条（文档坐标）
    D2D1_RECT_F clearRect_{};   // 清空已完成（文档坐标）
    D2D1_RECT_F pinRect_{};     // 置顶按钮（固定）
    D2D1_RECT_F closeRect_{};   // 关闭按钮（固定）
    D2D1_RECT_F menuRect_{};    // 标题栏菜单按钮（固定）
    D2D1_RECT_F themeRect_{};   // 标题栏皮肤按钮（固定）
    D2D1_RECT_F addListRect_{}; // 标签栏新增按钮（固定）
    D2D1_RECT_F calendarBtnRect_{}; // 标题栏日历视图开关

    int   hoverRow_   = -1;
    int   editIndex_  = -1;
    bool  dragging_   = false;
    int   dragFrom_   = -1;
    float dragY_      = 0.0f;
    int   dragInsert_ = -1;
    Hit   pressHit_;

    enum class CalendarDragMode { None, PendingCreate, PendingBlock, Creating, Moving, ResizingStart, ResizingEnd };
    struct CalendarDragState {
        CalendarDragMode mode = CalendarDragMode::None;
        int blockId = -1;
        int anchorMinute = 0;
        int originalStart = 0;
        int originalEnd = 0;
        float startX = 0.0f;
        float startY = 0.0f;
    } calendarDrag_;

    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    UINT explorerRestartMsg_ = 0; // Explorer 重启后重建托盘图标的消息 ID

    Lang         lang_            = Lang::Zh;

    MountMode    mountMode_       = MountMode::Normal;
    CapsuleStyle capsuleStyle_    = CapsuleStyle::Slim; // 胶囊外观
    bool      capsuleExpanded_ = false;  // 胶囊形态下是否已滑出
    bool      animActive_     = false;
    bool      capsulePressing_ = false;  // 折叠胶囊：鼠标按下中（待区分点击 / 拖动）
    bool      capsuleDragging_ = false;  // 折叠胶囊：已越过阈值进入拖动
    bool      capsuleHover_    = false;  // 折叠胶囊：鼠标悬停目标方向（true=醒/探出）
    double    capsuleHoverT_   = 0.0;    // 折叠入口 hover 缓动进度 0..1（仅过渡期间重绘）
    bool      menuOpen_        = false;  // 弹出菜单存活期间：抑制 WM_MOUSELEAVE 误触收缩
    bool      calendarSyncing_ = false;  // 同步日历编辑框文本时抑制 EN_CHANGE 写回
    struct ReminderVisualState {
        bool active = false;
        int blockId = -1;
        long long untilEpoch = 0;
        double pulseT = 0.0;
        int pendingCount = 0;
    } reminderVisual_;
    int       lastSystemReminderBlockId_ = -1;
    std::wstring reminderNotificationStatus_;
    std::wstring reminderSchedulerStatus_;
    bool      reminderSchedulerSynced_ = false;
    bool      reminderSchedulerRegistered_ = false;
    int       layeredMode_     = 0;      // 0=none, 2=per-pixel alpha folded entry
    POINT     capsulePressClient_{};     // 按下点（客户坐标，拖动时窗口跟随）
    POINT     capsulePressScreen_{};     // 按下点（屏幕坐标，阈值判定）
    RECT      animFrom_{};
    RECT      animTo_{};
    int       animStep_       = 0;
    static constexpr UINT_PTR kAnimTimerId = 2;
    static constexpr int      kAnimSteps   = 16; // 滑动动画帧数（配合 15ms/帧，放慢更柔和）
    static constexpr UINT_PTR kHoverTimerId = 3;       // 折叠入口 hover 缓动（探出/醒 ←→ 缩回/睡）
    static constexpr double   kHoverStep   = 1.0 / 12.0; // 每帧推进（15ms/帧，约 180ms 到位）

    static constexpr UINT_PTR kSaveTimerId = 1;
    static constexpr UINT_PTR kCollapseTimerId = 4;     // 展开胶囊：鼠标离开后的折叠宽限定时器
    static constexpr UINT_PTR kBackupTimerId = 5;       // 自动备份到用户指定目录
    static constexpr UINT_PTR kReminderTimerId = 6;      // 日历块提醒检查
    static constexpr UINT      kCollapseDelayMs = 500;  // 折叠宽限毫秒：移回即取消，避免太敏感
    static constexpr UINT      kBackupCheckMs = 60 * 1000;
    static constexpr UINT      kReminderCheckMaxMs = 60 * 1000;
    static constexpr long long kBackupIntervalSeconds = 60 * 60;
    enum class BackupStatus { None, Ready, Failed, ChooseFailed, SameFolder };
    bool savePending_ = false;
    BackupStatus backupStatus_ = BackupStatus::None;

    HFONT  editFont_ = nullptr; // 行内编辑框字体
    HBRUSH editBg_   = nullptr; // 行内编辑框背景刷（贴合纸张色）
    HBRUSH   calendarEditBg_ = nullptr; // 日历块编辑框背景刷（与编辑块同色，融入不突兀）
    uint32_t calendarEditFill_ = 0;     // 当前编辑块的填充色
    HWND   calendarTitleEdit_ = nullptr;
    HWND   calendarStartEdit_ = nullptr;
    HWND   calendarEndEdit_ = nullptr;
    int    calendarEditId_ = -1;
    CalendarEditFocus calendarEditFocus_ = CalendarEditFocus::Title;
};
