#pragma once
#include "UiStyle.h"

// ListView 管理布局、焦点、滚动与提示；快照持有图标副本，避免后台刷新使其失效。
static HWND popupList = nullptr;
static HIMAGELIST popupImages = nullptr;
static std::vector<TrayItem> popupIcons;
static TrayConfig::Panel popupOptions;
static const UINT CaptureStatusControl = 2200, PopupStatusTimer = 2201;
static int PanelPx(int value) {
    return MulDiv(value, popupWindow ? GetDpiForWindow(popupWindow) : 96, 96);
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
    POINT point;
    GetCursorPos(&point);
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor);
    return monitor.rcWork;
}
static RECT TileRect(int index) {
    RECT rect = {};
    if (popupList && ListView_GetItemRect(popupList, index, &rect, LVIR_BOUNDS))
        MapWindowPoints(popupList, popupWindow, (POINT *)&rect, 2);
    return rect;
}
static void PerformTile(int tile, int action, bool keyboard = false) {
    if (tile < 0 || tile >= (int)popupIcons.size())
        return;
    TrayItem item = popupIcons[tile];
    POINT point;
    if (keyboard) {
        RECT rect = TileRect(tile);
        point = {(rect.left + rect.right) / 2, rect.bottom};
        ClientToScreen(popupWindow, &point);
    } else
        GetCursorPos(&point);
    ClosePopup();
    if (Forward(item, action, point) && settingsConfig.foldIcons && mainWindow) {
        reFoldAttempts = 6;
        SetTimer(mainWindow, ReFoldTimer, 250, nullptr);
    }
}
static LRESULT CALLBACK PanelControlProc(HWND control, UINT message, WPARAM w, LPARAM l,
                                         UINT_PTR id, DWORD_PTR) {
    if (message == WM_KEYDOWN) {
        if (w == VK_ESCAPE) {
            ClosePopup();
            return 0;
        }
        if (w == VK_TAB) {
            HWND next = GetNextDlgTabItem(popupWindow, control, GetKeyState(VK_SHIFT) < 0);
            if (next)
                SetFocus(next);
            return 0;
        }
        if (control == popupList &&
            (w == VK_RETURN || w == VK_APPS || (w == VK_F10 && GetKeyState(VK_SHIFT) < 0))) {
            PerformTile(ListView_GetNextItem(popupList, -1, LVNI_FOCUSED), w == VK_RETURN ? 0 : 2,
                        true);
            return 0;
        }
        // 键盘移动、滚动后不能再触发旧位置待定的单击。
        if (control == popupList) {
            KillTimer(popupWindow, SingleTimer);
            pendingClick = -1;
        }
    }
    if (control == popupList && (message == WM_MOUSEWHEEL || message == WM_VSCROLL)) {
        KillTimer(popupWindow, SingleTimer);
        pendingClick = -1;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(control, PanelControlProc, id);
    return DefSubclassProc(control, message, w, l);
}
static LRESULT CALLBACK PopupProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_ERASEBKGND) {
        RECT client;
        GetClientRect(window, &client);
        FillRect((HDC)w, &client, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    if (message == WM_NOTIFY && ((NMHDR *)l)->hwndFrom == popupList) {
        auto *header = (NMHDR *)l;
        if (header->code == LVN_GETEMPTYMARKUP) {
            auto *empty = (NMLVEMPTYMARKUP *)l;
            empty->dwFlags = EMF_CENTERED;
            wcscpy_s(empty->szMarkup, settingsConfig.foldIcons
                                          ? L"暂无已隐藏的分组图标。右键 T 打开设置。"
                                          : L"折叠已暂停，图标由 Windows 显示。");
            return TRUE;
        }
        if (header->code == LVN_GETINFOTIPW) {
            auto *tip = (NMLVGETINFOTIPW *)l;
            if (tip->iItem >= 0 && tip->iItem < (int)popupIcons.size())
                wcsncpy_s(tip->pszText, tip->cchTextMax,
                          (popupIcons[tip->iItem].tip.empty() ? popupIcons[tip->iItem].process
                                                              : popupIcons[tip->iItem].tip)
                              .c_str(),
                          _TRUNCATE);
            return 0;
        }
        if (header->code == NM_CLICK || header->code == NM_DBLCLK || header->code == NM_RCLICK) {
            auto *click = (NMITEMACTIVATE *)l;
            KillTimer(window, SingleTimer);
            pendingClick = -1;
            if (header->code == NM_CLICK && click->iItem >= 0) {
                pendingClick = click->iItem;
                SetTimer(window, SingleTimer, GetDoubleClickTime(), nullptr);
            } else if (header->code != NM_CLICK)
                PerformTile(click->iItem, header->code == NM_DBLCLK ? 1 : 2);
            return 0;
        }
    }
    if (message == WM_TIMER) {
        if (w == SingleTimer) {
            KillTimer(window, SingleTimer);
            int tile = pendingClick;
            pendingClick = -1;
            PerformTile(tile, 0);
        } else if (w == PopupStatusTimer)
            SetDlgItemTextW(window, CaptureStatusControl, CaptureTimer::StatusText().c_str());
        return 0;
    }
    if (message == WM_COMMAND) {
        UINT command = LOWORD(w);
        if (command == CaptureTimer::CaptureNowCommand ||
            command == CaptureTimer::SettingsCommand || command == IDC_RESTORE_ALL) {
            ClosePopup();
            PostMessageW(mainWindow, WM_COMMAND, command, 0);
        }
        return 0;
    }
    if (message == WM_KEYDOWN && w == VK_ESCAPE) {
        ClosePopup();
        return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(window, SingleTimer);
        KillTimer(window, PopupStatusTimer);
        if (popupImages) {
            ListView_SetImageList(popupList, nullptr, LVSIL_NORMAL);
            ImageList_Destroy(popupImages);
            popupImages = nullptr;
        }
        popupIcons.clear();
        popupItems.clear();
        popupList = nullptr;
        popupWindow = nullptr;
        pendingClick = -1;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
static void OpenTrayPanel() {
    ClosePopup();
    popupOptions = settingsConfig.panel;
    popupGroups.clear();
    auto names = GroupNames();
    for (const auto &name : popupOptions.groupOrder)
        if (!name.empty() && names.erase(name))
            popupGroups.push_back(name);
    for (const auto &name : names)
        if (!name.empty())
            popupGroups.push_back(name);
    for (const auto &group : popupGroups)
        for (size_t n = 0; n < items.size(); ++n)
            if (items[n].group == group && PopupEligible(items[n])) {
                popupItems.push_back((int)n);
                popupIcons.push_back(items[n]);
            }
    RECT work = CursorWorkArea();
    popupWindow =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, L"托盘分组",
                        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, work.left + 8, work.top + 8, 420,
                        100, mainWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!popupWindow) {
        popupIcons.clear();
        popupItems.clear();
        LogError(L"无法打开分组面板");
        return;
    }
    UiStyle::Attach(popupWindow);
    int width = std::min(PanelPx(popupOptions.width), (int)(work.right - work.left - PanelPx(12)));
    int gap = PanelPx(8), footer = popupOptions.showCapture ? PanelPx(81) : PanelPx(8);
    int rows = 0;
    for (const auto &group : popupGroups) {
        int count = (int)std::count_if(popupIcons.begin(), popupIcons.end(),
                                       [&](const TrayItem &i) { return i.group == group; });
        rows += std::max(1, (count + popupOptions.columns - 1) / popupOptions.columns);
    }
    int desired = rows * PanelPx(popupOptions.iconSize + (popupOptions.showNames ? 42 : 25)) +
                  (int)popupGroups.size() * PanelPx(42);
    int height = std::min(std::max(PanelPx(130), desired) + footer + gap,
                          (int)(work.bottom - work.top - PanelPx(12)));
    POINT position;
    GetCursorPos(&position);
    RECT bounds = {0, 0, width, height};
    AdjustWindowRectExForDpi(&bounds, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                             GetDpiForWindow(popupWindow));
    int outerWidth = bounds.right - bounds.left, outerHeight = bounds.bottom - bounds.top;
    position.x =
        std::clamp((int)position.x - outerWidth / 2, (int)work.left, (int)work.right - outerWidth);
    position.y =
        std::clamp((int)position.y - outerHeight, (int)work.top, (int)work.bottom - outerHeight);
    SetWindowPos(popupWindow, HWND_TOPMOST, position.x, position.y, outerWidth, outerHeight, 0);
    popupList = CreateWindowExW(0, WC_LISTVIEWW, L"分组图标",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL |
                                    LVS_AUTOARRANGE | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
                                gap, gap, width - gap * 2, height - footer - gap, popupWindow,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!popupList) {
        ClosePopup();
        return;
    }
    ListView_SetExtendedListViewStyle(popupList, LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    SetWindowTheme(popupList, L"Explorer", nullptr);
    int size = PanelPx(popupOptions.iconSize);
    popupImages = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, (int)popupIcons.size(), 4);
    ListView_SetImageList(popupList, popupImages, LVSIL_NORMAL);
    // 预留系统滚动条宽度，避免出现滚动条后每行突然少一个图标。
    int cellWidth =
        (width - gap * 2 - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(popupWindow)) -
         PanelPx(4)) /
        popupOptions.columns;
    ListView_SetIconSpacing(popupList, cellWidth,
                            PanelPx(popupOptions.iconSize + (popupOptions.showNames ? 42 : 25)));
    ListView_EnableGroupView(popupList, TRUE);
    for (size_t n = 0; n < popupGroups.size(); ++n) {
        LVGROUP group = {};
        group.cbSize = sizeof(group);
        group.mask = LVGF_HEADER | LVGF_GROUPID;
        group.iGroupId = (int)n;
        group.pszHeader = (LPWSTR)popupGroups[n].c_str();
        ListView_InsertGroup(popupList, -1, &group);
    }
    for (size_t n = 0; n < popupIcons.size(); ++n) {
        const auto &icon = popupIcons[n];
        auto label = popupOptions.showNames ? FriendlyIconName(icon) : L"";
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_GROUPID;
        item.iItem = (int)n;
        item.pszText = (LPWSTR)label.c_str();
        item.iImage = ImageList_AddIcon(popupImages, icon.image);
        item.iGroupId = (int)(std::find(popupGroups.begin(), popupGroups.end(), icon.group) -
                              popupGroups.begin());
        ListView_InsertItem(popupList, &item);
    }
    UiStyle::StyleChildren(popupWindow);
    // 使用系统实际排版尺寸收紧面板，不用手工估算字体和组标题高度。
    int content = PanelPx(40);
    for (int n = 0; n < (int)popupIcons.size(); ++n) {
        RECT tile = {};
        if (ListView_GetItemRect(popupList, n, &tile, LVIR_BOUNDS))
            content = std::max(content, (int)tile.bottom);
    }
    for (int n = 0; n < (int)popupGroups.size(); ++n) {
        RECT header = {};
        if (ListView_GetGroupRect(popupList, n, LVGGR_HEADER, &header))
            content = std::max(content, (int)header.bottom);
    }
    height = std::min(height, std::max(PanelPx(76), content + gap) + footer + gap);
    bounds = {0, 0, width, height};
    AdjustWindowRectExForDpi(&bounds, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                             GetDpiForWindow(popupWindow));
    outerHeight = bounds.bottom - bounds.top;
    GetCursorPos(&position);
    position.x =
        std::clamp((int)position.x - outerWidth / 2, (int)work.left, (int)work.right - outerWidth);
    position.y =
        std::clamp((int)position.y - outerHeight, (int)work.top, (int)work.bottom - outerHeight);
    SetWindowPos(popupWindow, HWND_TOPMOST, position.x, position.y, outerWidth, outerHeight, 0);
    SetWindowPos(popupList, nullptr, gap, gap, width - gap * 2, height - footer - gap,
                 SWP_NOZORDER);
    if (popupOptions.showCapture) {
        int top = height - footer + gap;
        CreateWindowExW(0, L"STATIC", CaptureTimer::StatusText().c_str(),
                        WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, gap, top, width - gap * 2,
                        PanelPx(19), popupWindow, (HMENU)(INT_PTR)CaptureStatusControl,
                        GetModuleHandleW(nullptr), nullptr);
        int buttonWidth = (width - gap * 4) / 3;
        const wchar_t *labels[] = {L"立即截图", L"截图设置", L"恢复全部"};
        const UINT commands[] = {CaptureTimer::CaptureNowCommand, CaptureTimer::SettingsCommand,
                                 IDC_RESTORE_ALL};
        for (int n = 0; n < 3; ++n) {
            HWND button = CreateWindowExW(
                0, L"BUTTON", labels[n], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                gap + n * (buttonWidth + gap), top + PanelPx(27), buttonWidth, PanelPx(27),
                popupWindow, (HMENU)(INT_PTR)commands[n], GetModuleHandleW(nullptr), nullptr);
            SetWindowSubclass(button, PanelControlProc, 1, 0);
        }
        SetTimer(popupWindow, PopupStatusTimer, 1000, nullptr);
    }
    UiStyle::StyleChildren(popupWindow);
    SetWindowSubclass(popupList, PanelControlProc, 1, 0);
    ShowWindow(popupWindow, SW_SHOWNORMAL);
    SetForegroundWindow(popupWindow);
    SetFocus(popupList);
    DebugLog::Write(L"POPUP native list groups=" + Number(popupGroups.size()) + L" icons=" +
                    Number(popupIcons.size()));
}
static void ShowGroups() {
    ClosePopup();
    Refresh(false, true);
    OpenTrayPanel();
}
