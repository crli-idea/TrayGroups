// Group panel layout, drawing and input. Included after tray operations.
// 主窗口私有 T 面板：只列出已确认隐藏的分组图标，点击转发到原程序。
// items 持有图标副本，本面板只借用；销毁面板时不能重复释放这些 HICON。
#pragma once

static void ShowGroup(const std::wstring &group);
static void ShowGroups();
static int popupPage = 0, popupPageSize = 16, popupWidth = 420, popupItemTop = 40;
static int popupItemPage = 0, popupItemPageSize = 16, popupItemPages = 1;
static RECT TileRect(int n) {
    int col = n % 4, row = n / 4;
    return RECT{12 + col * 100, popupItemTop + row * 72, 108 + col * 100,
                popupItemTop + (row + 1) * 72 - 4};
}
static int TileAt(POINT pt) {
    for (size_t n = 0; n < popupItems.size(); n++) {
        RECT r = TileRect((int)n);
        if (PtInRect(&r, pt))
            return (int)n;
    }
    return -1;
}
static bool PopupEligible(const TrayItem &item) {
    return !item.group.empty() && !item.pending && (item.state & TBSTATE_HIDDEN);
}
static void PerformTile(int tile, int action) {
    if (tile < 0 || tile >= (int)popupItems.size())
        return;
    TrayItem item = items[popupItems[tile]];
    POINT pt;
    GetCursorPos(&pt);
    ClosePopup();
    if (Forward(item, action, pt) && settingsConfig.foldIcons && mainWindow) {
        reFoldAttempts = 6;
        SetTimer(mainWindow, ReFoldTimer, 250, nullptr);
    }
}
static HFONT LabelFont(int points, int dpi) {
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
}
static bool preservePopupPosition = false;
static const UINT CaptureStatusControl = 2200, PopupStatusTimer = 2201;
static const int CaptureFooterHeight = 62;
static POINT popupOrigin = {};
static RECT CursorWorkArea() {
    POINT cursor;
    if (preservePopupPosition)
        cursor = popupOrigin;
    else
        GetCursorPos(&cursor);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}
static void PlacePopup(int width, int height) {
    RECT work = CursorWorkArea();
    POINT pos;
    GetCursorPos(&pos);
    if (preservePopupPosition) {
        int x = std::max((int)work.left, std::min((int)popupOrigin.x, (int)work.right - width));
        int y = std::max((int)work.top, std::min((int)popupOrigin.y, (int)work.bottom - height));
        SetWindowPos(popupWindow, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
        return;
    }
    int x = std::max((int)work.left, std::min((int)pos.x - width / 2, (int)work.right - width));
    int y = std::max((int)work.top, std::min((int)pos.y - height, (int)work.bottom - height));
    SetWindowPos(popupWindow, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
}
static std::wstring ItemLabel(const TrayItem &i) {
    if (!i.tip.empty())
        return i.tip.substr(0, i.tip.find_first_of(L"\r\n"));
    return i.process;
}
static std::wstring FriendlyIconName(const TrayItem &i) {
    std::wstring cls = StableClass(i.ownerClass);
    if (cls == L"atl@sndvolsso.dll")
        return L"系统音量";
    if (cls == L"bluetoothnotificationareaiconwindowclass")
        return L"蓝牙设备";
    if (cls == L"atl@pnidui.dll")
        return L"网络状态";
    if (cls == L"atl@securityhealthsso.dll")
        return L"Windows 安全中心";
    std::wstring label = ItemLabel(i);
    return label.empty() ? L"程序图标" : label;
}
static int PopupFooter() {
    if (showingGroups)
        return groupRects.empty() ? 42 : groupRects.back().bottom + 6;
    return 44 + ((int)popupItems.size() + 3) / 4 * 72;
}
static LRESULT CALLBACK PopupProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        SetBkMode(dc, TRANSPARENT);
        HFONT old = (HFONT)SelectObject(dc, appFont);
        RECT title = {12, 8, popupWidth - 12, 34};
        std::wstring text = showingGroups ? L"分组列表" : L"‹ 分组    " + activeGroup;
        DrawTextW(dc, text.c_str(), -1, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (showingGroups) {
            int dpi = GetDpiForWindow(h);
            for (size_t n = 0; n < groupRects.size(); n++) {
                const auto &group = popupGroups[popupPage * popupPageSize + n];
                RECT r = groupRects[n];
                DrawEdge(dc, &r, EDGE_ETCHED, BF_RECT);
                HFONT font = LabelFont(settingsConfig.GroupLabelSize(group), dpi),
                      prior = (HFONT)SelectObject(dc, font);
                InflateRect(&r, -12, -4);
                std::wstring label = group;
                if (group == activeGroup)
                    FillRect(dc, &r, (HBRUSH)(COLOR_HIGHLIGHT + 1));
                SetTextColor(dc, group == activeGroup ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                                      : GetSysColor(COLOR_WINDOWTEXT));
                DrawTextW(dc, label.c_str(), -1, &r,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                SelectObject(dc, prior);
                DeleteObject(font);
            }
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            RECT selected = {12, popupItemTop - 28, popupWidth - 12, popupItemTop - 4};
            std::wstring groupName = activeGroup;
            DrawTextW(dc, groupName.c_str(), -1, &selected,
                      DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        {
            for (size_t n = 0; n < popupItems.size(); n++) {
                auto &i = items[popupItems[n]];
                RECT r = TileRect((int)n);
                DrawEdge(dc, &r, EDGE_ETCHED, BF_RECT);
                DrawIconEx(dc, r.left + 35, r.top + 6, i.image, 24, 24, 0, nullptr, DI_NORMAL);
                RECT label = {r.left + 3, r.top + 36, r.right - 3, r.bottom - 2};
                std::wstring name = ItemLabel(i);
                DrawTextW(dc, name.c_str(), -1, &label,
                          DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            if (popupItems.empty()) {
                RECT empty = {12, popupItemTop + 12, popupWidth - 12, popupItemTop + 66};
                const wchar_t *message = !settingsConfig.foldIcons
                                             ? L"折叠已暂停；可在设置的高级页开启"
                                         : popupGroups.empty() ? L"暂无分组；右键 T 打开设置"
                                                               : L"此组暂无隐藏图标";
                DrawTextW(dc, message, -1, &empty, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        if (showingGroups && popupGroups.size() > (size_t)popupPageSize) {
            RECT footer = {12, PopupFooter(), popupWidth - 12, PopupFooter() + 26};
            DrawTextW(dc, L"‹ 上一页                        下一页 ›", -1, &footer,
                      DT_SINGLELINE | DT_CENTER);
        }
        if (showingGroups && popupItemPages > 1) {
            int bottom = popupItemTop + std::max(1, ((int)popupItems.size() + 3) / 4) * 72;
            RECT footer = {12, bottom, popupWidth - 12, bottom + 26};
            std::wstring page = L"‹ 上一页           " + Number(popupItemPage + 1) + L" / " +
                                Number(popupItemPages) + L"           下一页 ›";
            DrawTextW(dc, page.c_str(), -1, &footer, DT_SINGLELINE | DT_CENTER);
        }
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (pt.y < 34) {
            if (!showingGroups) {
                popupPage = 0;
                PostMessageW(h, WM_APP + 20, 0, 0);
            }
            return 0;
        }
        int itemFooter = popupItemTop + std::max(1, ((int)popupItems.size() + 3) / 4) * 72;
        if (showingGroups && popupItemPages > 1 && pt.y >= itemFooter) {
            int next = popupItemPage + (pt.x < popupWidth / 2 ? -1 : 1);
            if (next >= 0 && next < popupItemPages) {
                popupItemPage = next;
                PostMessageW(h, WM_APP + 20, 0, 0);
            }
            return 0;
        }
        if (pt.y >= PopupFooter() && (!showingGroups || pt.y < popupItemTop)) {
            if (showingGroups && popupGroups.size() <= (size_t)popupPageSize)
                return 0;
            int direction = pt.x < popupWidth / 2 ? -1 : 1;
            if (popupPage + direction < 0)
                return 0;
            popupPage += direction;
            PostMessageW(h, WM_APP + 20, showingGroups ? 0 : 1, 0);
            return 0;
        }
        if (showingGroups) {
            for (size_t n = 0; n < groupRects.size(); n++)
                if (PtInRect(&groupRects[n], pt)) {
                    activeGroup = popupGroups[popupPage * popupPageSize + n];
                    popupItemPage = 0;
                    PostMessageW(h, WM_APP + 20, 0, 0);
                    return 0;
                }
        }
        pendingClick = TileAt(pt);
        if (pendingClick >= 0)
            SetTimer(h, SingleTimer, GetDoubleClickTime(), nullptr);
        return 0;
    }
    case WM_APP + 20: {
        RECT placement;
        if (GetWindowRect(h, &placement)) {
            popupOrigin = {placement.left, placement.top};
            preservePopupPosition = true;
        }
        std::wstring group = activeGroup;
        if (w)
            ShowGroup(group);
        else
            ShowGroups();
        preservePopupPosition = false;
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        KillTimer(h, SingleTimer);
        pendingClick = -1;
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        PerformTile(TileAt(pt), 1);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        KillTimer(h, SingleTimer);
        PerformTile(TileAt(pt), 2);
        return 0;
    }
    case WM_TIMER:
        if (w == SingleTimer) {
            KillTimer(h, SingleTimer);
            PerformTile(pendingClick, 0);
            pendingClick = -1;
        } else if (w == PopupStatusTimer)
            SetDlgItemTextW(h, CaptureStatusControl, CaptureTimer::StatusText().c_str());
        return 0;
    case WM_COMMAND: {
        UINT command = LOWORD(w);
        if (command == CaptureTimer::CaptureNowCommand ||
            command == CaptureTimer::SettingsCommand || command == IDC_RESTORE_ALL) {
            DebugLog::Write(L"POPUP capture button=" + Number(command));
            ClosePopup();
            PostMessageW(mainWindow, WM_COMMAND, command, 0);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE)
            DestroyWindow(h);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE)
            PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        DebugLog::Write(L"POPUP destroyed");
        popupWindow = nullptr;
        pendingClick = -1;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
static HWND NewPopup() {
    RECT work = CursorWorkArea();
    return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, L"托盘分组",
                           WS_POPUP | WS_BORDER, work.left + 8, work.top + 8, 420, 100, mainWindow,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
}
static void AttachCaptureControls(int top) {
    struct Entry {
        const wchar_t *cls;
        const wchar_t *text;
        int y, width, id;
    };
    std::wstring status = CaptureTimer::StatusText();
    Entry controls[] = {
        {L"STATIC", status.c_str(), top, popupWidth - 24, (int)CaptureStatusControl},
        {L"BUTTON", L"立即截图", top + 25, 100, (int)CaptureTimer::CaptureNowCommand},
        {L"BUTTON", L"截图设置…", top + 25, 100, (int)CaptureTimer::SettingsCommand},
        {L"BUTTON", L"恢复全部图标", top + 25, 170, IDC_RESTORE_ALL}};
    for (int n = 0; n < 4; n++) {
        const auto &c = controls[n];
        int x = n >= 2 ? 124 + (n - 2) * 112 : 12;
        HWND control = CreateWindowExW(
            0, c.cls, c.text,
            WS_CHILD | WS_VISIBLE | (n ? WS_TABSTOP | BS_PUSHBUTTON : SS_ENDELLIPSIS), x, c.y,
            c.width, 24, popupWindow, (HMENU)(INT_PTR)c.id, GetModuleHandleW(nullptr), nullptr);
        if (control)
            SendMessageW(control, WM_SETFONT, (WPARAM)appFont, TRUE);
        else
            LogError(L"创建截图控件失败，Win32=" + Number(GetLastError()));
    }
    SetTimer(popupWindow, PopupStatusTimer, 1000, nullptr);
    DebugLog::Write(L"POPUP capture controls attached; " + status);
}
static void FillPopupItems(const std::wstring &group) {
    std::vector<int> matching;
    for (size_t n = 0; n < items.size(); n++)
        if (items[n].group == group && PopupEligible(items[n]))
            matching.push_back((int)n);
    popupItemPages =
        std::max(1, ((int)matching.size() + popupItemPageSize - 1) / popupItemPageSize);
    popupItemPage = std::max(0, std::min(popupItemPage, popupItemPages - 1));
    auto first =
        matching.begin() + std::min((int)matching.size(), popupItemPage * popupItemPageSize);
    popupItems.assign(first, first + std::min(popupItemPageSize, (int)(matching.end() - first)));
    for (int n : popupItems) {
        auto &item = items[n];
        if (item.fallbackImage && !item.path.empty()) {
            HICON extracted = nullptr;
            if (ExtractIconExW(item.path.c_str(), 0, nullptr, &extracted, 1) && extracted) {
                DestroyIcon(item.image);
                item.image = extracted;
                item.fallbackImage = false;
            }
        }
    }
}
static void AttachPopupTooltips() {
    HWND tooltip =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, popupWindow,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
    for (size_t n = 0; n < popupItems.size(); n++) {
        TOOLINFOW t = {};
        t.cbSize = sizeof(t);
        t.uFlags = TTF_SUBCLASS;
        t.hwnd = popupWindow;
        t.uId = n + 1;
        t.rect = TileRect((int)n);
        auto &item = items[popupItems[n]];
        t.lpszText = (LPWSTR)(item.tip.empty() ? item.process.c_str() : item.tip.c_str());
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&t);
    }
}
static void ShowGroups() {
    DebugLog::Write(L"POPUP ShowGroups begin");
    ClosePopup();
    Refresh(false, true);
    showingGroups = true;
    auto groups = GroupNames();
    popupGroups.assign(groups.begin(), groups.end());
    if (std::find(popupGroups.begin(), popupGroups.end(), activeGroup) == popupGroups.end()) {
        activeGroup = popupGroups.empty() ? L"" : popupGroups.front();
        for (const auto &item : items)
            if (PopupEligible(item)) {
                activeGroup = item.group;
                break;
            }
    }
    popupWindow = NewPopup();
    if (!popupWindow) {
        LogError(L"无法打开 T 面板，Win32=" + Number(GetLastError()));
        return;
    }
    RECT work = CursorWorkArea();
    int dpi = GetDpiForWindow(popupWindow);
    int maximumHeight = 0;
    for (const auto &group : popupGroups)
        maximumHeight = std::max(
            maximumHeight, std::max(MulDiv(settingsConfig.GroupLabelSize(group), dpi, 72) + 24,
                                    MulDiv(settingsConfig.GroupLabelHeight(group), dpi, 96)) +
                               6);
    popupPageSize =
        std::max(1, std::min(8, ((int)(work.bottom - work.top) - 250 - CaptureFooterHeight) /
                                    std::max(1, maximumHeight)));
    popupPage = std::max(0, std::min(popupPage, ((int)popupGroups.size() - 1) / popupPageSize));
    groupRects.clear();
    int top = 40;
    popupWidth = 420;
    for (size_t n = popupPage * popupPageSize;
         n < popupGroups.size() && groupRects.size() < (size_t)popupPageSize; n++) {
        int height = std::max(MulDiv(settingsConfig.GroupLabelSize(popupGroups[n]), dpi, 72) + 24,
                              MulDiv(settingsConfig.GroupLabelHeight(popupGroups[n]), dpi, 96));
        int width = std::min((int)(work.right - work.left) - 28,
                             MulDiv(settingsConfig.GroupLabelWidth(popupGroups[n]), dpi, 96));
        groupRects.push_back(RECT{12, top, 12 + width, top + height});
        top += height + 6;
        popupWidth = std::max(popupWidth, width + 24);
    }
    popupItemTop = PopupFooter() + (popupGroups.size() > (size_t)popupPageSize ? 58 : 34);
    popupItemPageSize = 4 * std::max(1, std::min(4, ((int)(work.bottom - work.top) - popupItemTop -
                                                     140 - CaptureFooterHeight) /
                                                        72));
    FillPopupItems(activeGroup);
    int height = popupItemTop + std::max(1, ((int)popupItems.size() + 3) / 4) * 72 +
                 (popupItemPages > 1 ? 34 : 12);
    PlacePopup(popupWidth, height + CaptureFooterHeight);
    AttachPopupTooltips();
    AttachCaptureControls(height);
    SetForegroundWindow(popupWindow);
    DebugLog::Write(L"POPUP visible=" + Number(IsWindowVisible(popupWindow)) + L" groups=" +
                    Number(popupGroups.size()) + L" icons=" + Number(popupItems.size()));
}
static void ShowGroup(const std::wstring &group) {
    if (group.empty()) {
        ShowGroups();
        return;
    }
    ClosePopup();
    Refresh(false, true);
    showingGroups = false;
    popupWidth = 420;
    popupItemTop = 40;
    activeGroup = group;
    std::vector<int> all;
    for (size_t n = 0; n < items.size(); n++)
        if (items[n].group == group && PopupEligible(items[n]))
            all.push_back((int)n);
    RECT work = CursorWorkArea();
    int pageSize =
        4 *
        std::max(1, std::min(4, ((int)(work.bottom - work.top) - 110 - CaptureFooterHeight) / 72));
    popupPage =
        std::max(0, std::min(popupPage, all.empty() ? 0 : ((int)all.size() - 1) / pageSize));
    auto first = all.begin() + std::min((int)all.size(), popupPage * pageSize);
    popupItems.assign(first, first + std::min(pageSize, (int)(all.end() - first)));
    for (int n : popupItems) {
        auto &i = items[n];
        if (i.fallbackImage && !i.path.empty()) {
            HICON extracted = nullptr;
            if (ExtractIconExW(i.path.c_str(), 0, nullptr, &extracted, 1) && extracted) {
                DestroyIcon(i.image);
                i.image = extracted;
                i.fallbackImage = false;
            }
        }
    }
    popupWindow = NewPopup();
    if (!popupWindow)
        return;
    int height = std::max(110, PopupFooter() + 34);
    PlacePopup(420, height + CaptureFooterHeight);
    AttachCaptureControls(height);
    AttachPopupTooltips();
    SetForegroundWindow(popupWindow);
}
