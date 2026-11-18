// 主窗口私有紧凑面板。独立快照拥有图标副本；刷新后台列表不会使绘制或提示悬空。
#pragma once
#include "UiStyle.h"
#include "CompactPanelLayout.h"

static const UINT CaptureStatusControl = 2200, PopupStatusTimer = 2201;
static HWND popupTooltip = nullptr;
static std::vector<TrayItem> popupIcons;
static CompactPanel::Layout popupLayout;
static TrayConfig::Panel popupOptions;
static std::vector<HFONT> popupGroupFonts;
static HFONT popupTextFont = nullptr, popupNameFont = nullptr;
static RECT popupViewport = {};
static int popupWidth = 420, popupHeight = 0, popupScroll = 0, popupFocus = -1, hoverTile = -1;
static int PanelPx(int value) {
    return MulDiv(value, popupWindow ? GetDpiForWindow(popupWindow) : 96, 96);
}
static COLORREF PanelAccent() {
    if (UiStyle::highContrast)
        return GetSysColor(COLOR_HIGHLIGHT);
    auto value = wcstoul(popupOptions.accent.c_str() + 1, nullptr, 16);
    return RGB((value >> 16) & 255, (value >> 8) & 255, value & 255);
}
static COLORREF PanelTint() {
    auto color = PanelAccent();
    return UiStyle::highContrast
               ? GetSysColor(COLOR_HIGHLIGHT)
               : RGB((GetRValue(color) + 255 * 10) / 11, (GetGValue(color) + 255 * 10) / 11,
                     (GetBValue(color) + 255 * 10) / 11);
}
static std::wstring ItemLabel(const TrayItem &item) {
    return item.tip.empty() ? item.process : item.tip.substr(0, item.tip.find_first_of(L"\r\n"));
}
static std::wstring FriendlyIconName(const TrayItem &item) {
    auto label = ItemLabel(item);
    return label.empty() ? L"程序图标" : label;
}
static bool PopupEligible(const TrayItem &item) {
    return !item.group.empty() && !item.pending && item.addressKnown &&
           (item.state & TBSTATE_HIDDEN);
}
static RECT CursorWorkArea() {
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    return monitor.rcWork;
}
static RECT TileRect(int index) {
    if (index < 0 || index >= (int)popupLayout.tiles.size())
        return {};
    RECT rect = popupLayout.tiles[index];
    OffsetRect(&rect, 0, popupViewport.top - popupScroll);
    return rect;
}
static int TileAt(POINT point) {
    if (!PtInRect(&popupViewport, point))
        return -1;
    for (size_t n = 0; n < popupLayout.tiles.size(); ++n) {
        auto rect = TileRect((int)n);
        if (PtInRect(&rect, point))
            return (int)n;
    }
    return -1;
}
static void UpdatePopupTools() {
    if (!popupTooltip)
        return;
    for (size_t n = 0; n < popupIcons.size(); ++n) {
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.hwnd = popupWindow;
        tool.uId = n + 1;
        RECT tile = TileRect((int)n);
        IntersectRect(&tool.rect, &tile, &popupViewport);
        SendMessageW(popupTooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&tool);
    }
    SendMessageW(popupTooltip, TTM_POP, 0, 0);
}
static void ScrollPopup(int offset) {
    int limit =
        CompactPanel::ScrollLimit(popupLayout.height, popupViewport.bottom - popupViewport.top);
    int next = std::clamp(offset, 0, limit);
    if (next == popupScroll)
        return;
    popupScroll = next;
    hoverTile = -1;
    pendingClick = -1;
    KillTimer(popupWindow, SingleTimer);
    SetScrollPos(popupWindow, SB_VERT, popupScroll, TRUE);
    UpdatePopupTools();
    InvalidateRect(popupWindow, &popupViewport, FALSE);
}
static void FocusPopupIcon(int next) {
    if (popupIcons.empty())
        return;
    popupFocus = std::clamp(next, 0, (int)popupIcons.size() - 1);
    auto rect = TileRect(popupFocus);
    if (rect.top < popupViewport.top)
        ScrollPopup(popupScroll + rect.top - popupViewport.top);
    else if (rect.bottom > popupViewport.bottom)
        ScrollPopup(popupScroll + rect.bottom - popupViewport.bottom);
    InvalidateRect(popupWindow, &popupViewport, FALSE);
}
static void PerformTile(int tile, int action) {
    if (tile < 0 || tile >= (int)popupIcons.size())
        return;
    TrayItem item = popupIcons[tile];
    POINT point;
    GetCursorPos(&point);
    ClosePopup();
    if (Forward(item, action, point) && settingsConfig.foldIcons && mainWindow) {
        reFoldAttempts = 6;
        SetTimer(mainWindow, ReFoldTimer, 250, nullptr);
    }
}
static void ShowGroups();
static LRESULT CALLBACK PopupProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int next = TileAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (next != hoverTile) {
            hoverTile = next;
            InvalidateRect(window, &popupViewport, FALSE);
        }
        TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        return 0;
    }
    case WM_MOUSELEAVE:
        hoverTile = -1;
        InvalidateRect(window, &popupViewport, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && hoverTile >= 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT client;
        GetClientRect(window, &client);
        UiStyle::Card(dc, client, UiStyle::Paper(), UiStyle::Line(),
                      PanelPx(popupOptions.radius * 2));
        SetBkMode(dc, TRANSPARENT);
        {
            NativeHandles::Selection font(dc, popupTextFont ? popupTextFont : appFont);
            const int margin = PanelPx(18);
            RECT logo = {margin, PanelPx(18), margin + PanelPx(30), PanelPx(48)};
            UiStyle::Card(dc, logo, PanelTint(), PanelTint(), PanelPx(16));
            SetTextColor(dc, PanelAccent());
            {
                NativeHandles::Selection heading(dc, UiStyle::HeadingFont(window));
                DrawTextW(dc, L"T", -1, &logo, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            RECT title = {margin + PanelPx(40), PanelPx(15), client.right - margin, PanelPx(35)};
            SetTextColor(dc, UiStyle::Ink());
            {
                NativeHandles::Selection heading(dc, UiStyle::HeadingFont(window));
                DrawTextW(dc, L"托盘分组", -1, &title, DT_SINGLELINE | DT_VCENTER);
            }
            RECT subtitle = {title.left, PanelPx(37), title.right, PanelPx(56)};
            SetTextColor(dc, UiStyle::Muted());
            std::wstring hint = settingsConfig.foldIcons ? L"常用图标，各归其位  ·  " +
                                                               Number(popupIcons.size()) + L" 个"
                                                         : L"折叠已暂停 · 原图标由 Windows 显示";
            DrawTextW(dc, hint.c_str(), -1, &subtitle, DT_SINGLELINE | DT_END_ELLIPSIS);
            int saved = SaveDC(dc);
            IntersectClipRect(dc, popupViewport.left, popupViewport.top, popupViewport.right,
                              popupViewport.bottom);
            for (size_t n = 0; n < popupLayout.sections.size(); ++n) {
                const auto &section = popupLayout.sections[n];
                RECT heading = section.heading;
                OffsetRect(&heading, 0, popupViewport.top - popupScroll);
                if (heading.bottom < popupViewport.top || heading.top > popupViewport.bottom)
                    continue;
                SetTextColor(dc, UiStyle::Muted());
                NativeHandles::Selection groupFont(
                    dc, n < popupGroupFonts.size() ? popupGroupFonts[n] : popupTextFont);
                std::wstring label = popupGroups[n] + L"  " + Number(section.count);
                DrawTextW(dc, label.c_str(), -1, &heading,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (!section.count) {
                    RECT empty = {heading.left, heading.bottom + PanelPx(3), heading.right,
                                  heading.bottom + PanelPx(23)};
                    NativeHandles::Selection emptyFont(dc, popupNameFont);
                    DrawTextW(dc, L"暂无已隐藏图标", -1, &empty, DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
            for (size_t n = 0; n < popupIcons.size(); ++n) {
                RECT tile = TileRect((int)n);
                if (tile.bottom <= popupViewport.top || tile.top >= popupViewport.bottom)
                    continue;
                if ((int)n == hoverTile || (int)n == popupFocus)
                    UiStyle::Card(dc, tile, PanelTint(), PanelTint(), PanelPx(16));
                const auto &icon = popupIcons[n];
                int size = std::min(PanelPx(popupOptions.iconSize),
                                    (int)(tile.right - tile.left - PanelPx(8)));
                int x = (tile.left + tile.right - size) / 2, y = tile.top + PanelPx(7);
                DrawIconEx(dc, x, y, icon.image, size, size, 0, nullptr, DI_NORMAL);
                if (popupOptions.showNames) {
                    RECT name = {tile.left + PanelPx(3), y + size + PanelPx(5),
                                 tile.right - PanelPx(3), tile.bottom - PanelPx(2)};
                    NativeHandles::Selection labelFont(dc, popupNameFont);
                    SetTextColor(dc, UiStyle::Ink());
                    auto label = ItemLabel(icon);
                    DrawTextW(dc, label.c_str(), -1, &name,
                              DT_SINGLELINE | DT_CENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                if ((int)n == popupFocus && GetFocus() == window) {
                    RECT focus = tile;
                    InflateRect(&focus, -2, -2);
                    DrawFocusRect(dc, &focus);
                }
            }
            if (popupGroups.empty()) {
                RECT empty = popupViewport;
                InflateRect(&empty, -margin, -PanelPx(15));
                SetTextColor(dc, UiStyle::Muted());
                const wchar_t *text = !settingsConfig.foldIcons
                                          ? L"折叠已暂停\n可在设置的高级页开启"
                                      : popupGroups.empty() ? L"暂无分组\n右键 T 打开设置"
                                                            : L"暂无已隐藏的分组图标";
                DrawTextW(dc, text, -1, &empty, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            }
            RestoreDC(dc, saved);
            if (popupOptions.showCapture) {
                RECT line = {margin, popupViewport.bottom + PanelPx(5), client.right - margin,
                             popupViewport.bottom + PanelPx(6)};
                UiStyle::Fill(dc, line, UiStyle::Line());
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEWHEEL:
        ScrollPopup(popupScroll - GET_WHEEL_DELTA_WPARAM(w) * PanelPx(48) / WHEEL_DELTA);
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_TRACKPOS;
        GetScrollInfo(window, SB_VERT, &info);
        int next = popupScroll, page = popupViewport.bottom - popupViewport.top;
        switch (LOWORD(w)) {
        case SB_LINEUP:
            next -= PanelPx(32);
            break;
        case SB_LINEDOWN:
            next += PanelPx(32);
            break;
        case SB_PAGEUP:
            next -= page;
            break;
        case SB_PAGEDOWN:
            next += page;
            break;
        case SB_THUMBTRACK:
            next = info.nTrackPos;
            break;
        case SB_TOP:
            next = 0;
            break;
        case SB_BOTTOM:
            next = popupLayout.height;
            break;
        }
        ScrollPopup(next);
        return 0;
    }
    case WM_LBUTTONUP:
        pendingClick = TileAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (pendingClick >= 0) {
            popupFocus = pendingClick;
            SetTimer(window, SingleTimer, GetDoubleClickTime(), nullptr);
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        KillTimer(window, SingleTimer);
        pendingClick = -1;
        PerformTile(TileAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)}), 1);
        return 0;
    case WM_RBUTTONUP:
        KillTimer(window, SingleTimer);
        pendingClick = -1;
        PerformTile(TileAt({GET_X_LPARAM(l), GET_Y_LPARAM(l)}), 2);
        return 0;
    case WM_TIMER:
        if (w == SingleTimer) {
            KillTimer(window, SingleTimer);
            int tile = pendingClick;
            pendingClick = -1;
            PerformTile(tile, 0);
        } else if (w == PopupStatusTimer)
            SetDlgItemTextW(window, CaptureStatusControl, CaptureTimer::StatusText().c_str());
        return 0;
    case WM_COMMAND: {
        UINT command = LOWORD(w);
        if (command == CaptureTimer::CaptureNowCommand ||
            command == CaptureTimer::SettingsCommand || command == IDC_RESTORE_ALL) {
            ClosePopup();
            PostMessageW(mainWindow, WM_COMMAND, command, 0);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE)
            ClosePopup();
        else if (w == VK_DOWN || w == VK_RIGHT)
            FocusPopupIcon(popupFocus + 1);
        else if (w == VK_UP || w == VK_LEFT)
            FocusPopupIcon(popupFocus < 0 ? (int)popupIcons.size() - 1 : popupFocus - 1);
        else if (w == VK_NEXT)
            ScrollPopup(popupScroll + popupViewport.bottom - popupViewport.top);
        else if (w == VK_PRIOR)
            ScrollPopup(popupScroll - popupViewport.bottom + popupViewport.top);
        else if (w == VK_HOME)
            FocusPopupIcon(0);
        else if (w == VK_END)
            FocusPopupIcon((int)popupIcons.size() - 1);
        else if (w == VK_RETURN)
            PerformTile(popupFocus, 0);
        else if (w == VK_APPS || (w == VK_F10 && GetKeyState(VK_SHIFT) < 0))
            PerformTile(popupFocus, 2);
        else if (w == VK_TAB && popupOptions.showCapture)
            SetFocus(GetDlgItem(window, CaptureTimer::CaptureNowCommand));
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE)
            PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        KillTimer(window, SingleTimer);
        KillTimer(window, PopupStatusTimer);
        if (popupTooltip) {
            DestroyWindow(popupTooltip);
            popupTooltip = nullptr;
        }
        for (auto font : popupGroupFonts)
            if (font)
                DeleteObject(font);
        popupGroupFonts.clear();
        if (popupTextFont)
            DeleteObject(popupTextFont);
        popupTextFont = nullptr;
        if (popupNameFont)
            DeleteObject(popupNameFont);
        popupNameFont = nullptr;
        popupIcons.clear();
        popupLayout = {};
        popupItems.clear();
        popupWindow = nullptr;
        pendingClick = -1;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
static HWND NewPopup() {
    RECT work = CursorWorkArea();
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, L"托盘分组",
                                  WS_POPUP, work.left + 8, work.top + 8, 420, 100, mainWindow,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
    UiStyle::Attach(window, CaptureTimer::CaptureNowCommand, 0, CaptureStatusControl,
                    PanelAccent());
    return window;
}
static LRESULT CALLBACK PopupButtonProc(HWND button, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                        DWORD_PTR) {
    if (message == WM_KEYDOWN && w == VK_ESCAPE) {
        ClosePopup();
        return 0;
    }
    if (message == WM_KEYDOWN && w == VK_TAB) {
        HWND next = GetNextDlgTabItem(popupWindow, button, GetKeyState(VK_SHIFT) < 0);
        if (next && next != button)
            SetFocus(next);
        else
            SetFocus(popupWindow);
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(button, PopupButtonProc, id);
    return DefSubclassProc(button, message, w, l);
}
static void AttachCaptureControls(int top) {
    const int margin = PanelPx(18), gap = PanelPx(7),
              width = (popupWidth - margin * 2 - gap * 2) / 3;
    HWND status = CreateWindowExW(
        0, L"STATIC", CaptureTimer::StatusText().c_str(), WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
        margin, top, popupWidth - margin * 2, PanelPx(20), popupWindow,
        (HMENU)(INT_PTR)CaptureStatusControl, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(status, WM_SETFONT, (WPARAM)(popupTextFont ? popupTextFont : appFont), TRUE);
    const wchar_t *labels[] = {L"立即截图", L"截图设置", L"恢复全部"};
    const UINT commands[] = {CaptureTimer::CaptureNowCommand, CaptureTimer::SettingsCommand,
                             IDC_RESTORE_ALL};
    for (int n = 0; n < 3; ++n) {
        HWND button = CreateWindowExW(
            0, L"BUTTON", labels[n], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            margin + n * (width + gap), top + PanelPx(27), width, PanelPx(30), popupWindow,
            (HMENU)(INT_PTR)commands[n], GetModuleHandleW(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, (WPARAM)(popupTextFont ? popupTextFont : appFont), TRUE);
        SetWindowSubclass(button, PopupButtonProc, 1, 0);
    }
    UiStyle::StyleChildren(popupWindow);
    SetTimer(popupWindow, PopupStatusTimer, 1000, nullptr);
}
static void AttachPopupTooltips() {
    popupTooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, popupWindow,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(popupTooltip, TTM_SETMAXTIPWIDTH, 0, PanelPx(400));
    for (size_t n = 0; n < popupIcons.size(); ++n) {
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = popupWindow;
        tool.uId = n + 1;
        tool.lpszText = (LPWSTR)(popupIcons[n].tip.empty() ? popupIcons[n].process.c_str()
                                                           : popupIcons[n].tip.c_str());
        SendMessageW(popupTooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
    }
    UpdatePopupTools();
}
static HFONT PanelFont(int pixels, int weight = FW_NORMAL) {
    return CreateFontW(-PanelPx(pixels), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                       0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
}
static void BuildCompactContent() {
    popupIcons.clear();
    popupItems.clear();
    popupGroups = CompactPanel::Order(GroupNames(), popupOptions.groupOrder);
    std::vector<CompactPanel::Group> groups;
    for (const auto &name : popupGroups) {
        int count = 0;
        for (size_t n = 0; n < items.size(); ++n)
            if (items[n].group == name && PopupEligible(items[n])) {
                popupItems.push_back((int)n);
                popupIcons.push_back(items[n]);
                ++count;
            }
        auto fontSize = settingsConfig.groupLabelSizes.find(name);
        int pixels = fontSize == settingsConfig.groupLabelSizes.end()
                         ? popupOptions.labelSize
                         : MulDiv(fontSize->second, 96, 72);
        popupGroupFonts.push_back(PanelFont(pixels));
        auto minimum = settingsConfig.groupLabelHeights.find(name);
        int height =
            minimum == settingsConfig.groupLabelHeights.end() ? 0 : PanelPx(minimum->second);
        groups.push_back({name, count, std::max(PanelPx(pixels + 12), height)});
    }
    popupLayout =
        CompactPanel::Build(groups, popupOptions, popupWidth, GetDpiForWindow(popupWindow));
    for (size_t n = 0; n < popupGroups.size(); ++n) {
        auto width = settingsConfig.groupLabelWidths.find(popupGroups[n]);
        if (width != settingsConfig.groupLabelWidths.end())
            popupLayout.sections[n].heading.right =
                std::min(popupLayout.sections[n].heading.right,
                         popupLayout.sections[n].heading.left + PanelPx(width->second));
    }
    popupTextFont = PanelFont(12);
    popupNameFont = PanelFont(10);
}
static void OpenCompactPanel() {
    ClosePopup();
    popupOptions = settingsConfig.panel;
    popupScroll = 0;
    popupFocus = hoverTile = -1;
    popupWindow = NewPopup();
    if (!popupWindow) {
        LogError(L"无法打开紧凑分组面板");
        return;
    }
    const int dpi = GetDpiForWindow(popupWindow);
    RECT work = CursorWorkArea();
    popupWidth = std::min(PanelPx(popupOptions.width), (int)(work.right - work.left - PanelPx(12)));
    BuildCompactContent();
    const int header = PanelPx(70), footer = popupOptions.showCapture ? PanelPx(89) : PanelPx(12);
    popupHeight = std::min(header + std::max(popupLayout.height, PanelPx(76)) + footer,
                           (int)(work.bottom - work.top - PanelPx(12)));
    popupViewport = {1, header, popupWidth - 1, popupHeight - footer};
    bool scroll = popupLayout.height > popupViewport.bottom - popupViewport.top;
    if (scroll) {
        SetWindowLongPtrW(popupWindow, GWL_STYLE,
                          GetWindowLongPtrW(popupWindow, GWL_STYLE) | WS_VSCROLL);
        int scrollbar = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
        popupWidth -= scrollbar;
        for (auto font : popupGroupFonts)
            if (font)
                DeleteObject(font);
        popupGroupFonts.clear();
        if (popupTextFont)
            DeleteObject(popupTextFont);
        if (popupNameFont)
            DeleteObject(popupNameFont);
        BuildCompactContent();
        popupViewport.right = popupWidth - 1;
        SCROLLINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, popupLayout.height - 1);
        info.nPage = popupViewport.bottom - popupViewport.top;
        SetScrollInfo(popupWindow, SB_VERT, &info, TRUE);
        popupWidth += scrollbar;
    }
    POINT position;
    GetCursorPos(&position);
    position.x -= popupWidth / 2;
    position.y -= popupHeight;
    position.x = std::clamp((int)position.x, (int)work.left, (int)work.right - popupWidth);
    position.y = std::clamp((int)position.y, (int)work.top, (int)work.bottom - popupHeight);
    SetWindowPos(popupWindow, HWND_TOPMOST, position.x, position.y, popupWidth, popupHeight,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    RECT client;
    GetClientRect(popupWindow, &client);
    popupWidth = client.right;
    if (popupOptions.radius) {
        HRGN region = CreateRoundRectRgn(
            0, 0, client.right + (scroll ? GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) : 0) + 1,
            popupHeight + 1, PanelPx(popupOptions.radius * 2), PanelPx(popupOptions.radius * 2));
        if (region && !SetWindowRgn(popupWindow, region, TRUE))
            DeleteObject(region);
    }
    if (popupOptions.showCapture)
        AttachCaptureControls(popupViewport.bottom + PanelPx(16));
    AttachPopupTooltips();
    SetForegroundWindow(popupWindow);
    SetFocus(popupWindow);
    DebugLog::Write(L"POPUP compact groups=" + Number(popupGroups.size()) + L" icons=" +
                    Number(popupIcons.size()));
}
static void ShowGroups() {
    ClosePopup();
    Refresh(false, true);
    OpenCompactPanel();
}
