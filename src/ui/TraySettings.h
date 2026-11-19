// 主窗口私有设置页实现；布局定义在 resources/TrayGroups.rc。
// PropertySheet 管理分页和标准按钮；编辑副本只在应用时保存。
#pragma once
#include "TraySettingsIds.h"
#include "TrayInspector.h"
#include "UiStyle.h"
#include "WallpaperPage.h"
#include <uxtheme.h>

static TrayConfig::Settings settingsDraft;
static TrayIdentity::MatchState settingsMatchState;
static TrayIdentity::Matcher settingsMatcher(settingsDraft, settingsMatchState);
static constexpr int SettingsPageCount = 6;
static HWND settingsTabs = nullptr, settingsPages[SettingsPageCount] = {};
static CaptureTimer::Preferences captureDraft;
static int settingsWidth = 1000, settingsHeight = 680;

static std::wstring ControlText(HWND control) {
    wchar_t text[128] = {};
    GetWindowTextW(control, text, 128);
    return Trim(text);
}
static void FillGroupChoices() {
    std::set<std::wstring> groups;
    for (const auto *rules : {&settingsDraft.assignments, &settingsDraft.pathAssignments,
                              &settingsDraft.programAssignments})
        for (const auto &rule : *rules)
            if (!rule.second.empty())
                groups.insert(rule.second);
    for (HWND combo : {groupEdit}) {
        auto text = ControlText(combo);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto &group : groups)
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)group.c_str());
        SetWindowTextW(combo, text.c_str());
    }
}
static void SelectComboText(HWND combo) {
    int row = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (row >= 0) {
        wchar_t text[128];
        SendMessageW(combo, CB_GETLBTEXT, row, (LPARAM)text);
        SetWindowTextW(combo, text);
    }
}
static void FillSettings() {
    fillingSettings = true;
    SendMessageW(listWindow, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(listWindow);
    FreeItems(settingsItems);
    settingsItems = ReadTray();
    settingsMatcher.ResolveGroups(settingsItems);
    if (settingsImages) {
        ListView_SetImageList(listWindow, nullptr, LVSIL_SMALL);
        ImageList_Destroy(settingsImages);
    }
    int iconSize = MulDiv(20, GetDpiForWindow(settingsWindow), 96);
    settingsImages = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK,
                                      (int)settingsItems.size() + 1, 4);
    ListView_SetImageList(listWindow, settingsImages, LVSIL_SMALL);
    for (size_t n = 0; n < settingsItems.size(); n++) {
        auto &i = settingsItems[n];
        if (i.fallbackImage && !i.path.empty()) {
            HICON extracted = nullptr;
            if (ExtractIconExW(i.path.c_str(), 0, nullptr, &extracted, 1) && extracted) {
                i.image = extracted;
                i.fallbackImage = false;
            }
        }
        int image = ImageList_AddIcon(settingsImages, i.image);
        LVITEMW row = {};
        row.mask = LVIF_TEXT | LVIF_IMAGE;
        row.iItem = (int)n;
        row.iImage = image;
        std::wstring displayName = i.process + (i.pending ? L"（待确认）" : L"");
        row.pszText = (LPWSTR)displayName.c_str();
        ListView_InsertItem(listWindow, &row);
        ListView_SetItemText(listWindow, n, 1, (LPWSTR)i.tip.c_str());
        ListView_SetItemText(listWindow, n, 2, (LPWSTR)i.group.c_str());
        ListView_SetItemText(listWindow, n, 3, (LPWSTR)i.path.c_str());
        std::wstring status = i.matchStatus;
        if (!i.group.empty() && !i.pending)
            status += !settingsDraft.foldIcons     ? L"；折叠已暂停"
                      : !i.addressKnown            ? L"；未取得注册信息，保持原状"
                      : (i.state & TBSTATE_HIDDEN) ? L"；已隐藏"
                                                   : L"；仍显示在 Windows";
        ListView_SetItemText(listWindow, n, 5, (LPWSTR)status.c_str());
        std::wstring identity = FriendlyIconName(i);
        ListView_SetItemText(listWindow, n, 4, (LPWSTR)identity.c_str());
    }
    SendMessageW(listWindow, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(listWindow, nullptr, TRUE);
    FillGroupChoices();
    SendMessageW(matchEdit, CB_SETCURSEL,
                 settingsDraft.iconMatch == L"path-guid"        ? 3
                 : settingsDraft.iconMatch == L"path-id"        ? 1
                 : settingsDraft.iconMatch == L"path-class-tip" ? 2
                                                                : 0,
                 0);
    SendMessageW(foldCheck, BM_SETCHECK, settingsDraft.foldIcons ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(groupEdit, L"");
    groupEditing = false;
    fillingSettings = false;
}
static bool ApplyGroupEdit() {
    int row = -1;
    wchar_t text[128];
    GetWindowTextW(groupEdit, text, 128);
    std::wstring group = Trim(text);
    int scope = (int)SendMessageW(scopeEdit, CB_GETCURSEL, 0, 0),
        match = (int)SendMessageW(matchEdit, CB_GETCURSEL, 0, 0);
    settingsDraft.iconMatch = match == 3   ? L"path-guid"
                              : match == 1 ? L"path-id"
                              : match == 2 ? L"path-class-tip"
                                           : L"path-class-id";
    std::vector<int> selected;
    while ((row = ListView_GetNextItem(listWindow, row, LVNI_SELECTED)) != -1)
        selected.push_back(row);
    if (scope == 1)
        for (int n : selected)
            if (settingsItems[n].path.empty()) {
                MessageBoxW(settingsWindow, L"无法读取该图标的程序路径。", L"无法按路径匹配",
                            MB_ICONWARNING);
                return false;
            }
    if (scope <= 0)
        for (int n : selected) {
            auto key = IconKey(settingsItems[n], settingsDraft.iconMatch);
            unsigned count = 0;
            for (const auto &i : settingsItems)
                if (IconKey(i, settingsDraft.iconMatch) == key)
                    count++;
            if (key.empty() || count > 1) {
                MessageBoxW(settingsWindow,
                            L"当前规则不能唯一区分所选图标。请更换识别方式，或明确选择按路径/"
                            L"程序整体分组。",
                            L"无法单独匹配", MB_ICONWARNING);
                return false;
            }
        }
    if (scope <= 0 && std::any_of(selected.begin(), selected.end(),
                                  [&](int n) { return settingsItems[n].owner != nullptr; })) {
        auto live = ReadTray(false);
        bool valid = true;
        for (int n : selected) {
            const auto &old = settingsItems[n];
            auto key = IconKey(old, settingsDraft.iconMatch);
            unsigned matches = 0;
            bool same = false;
            for (const auto &item : live) {
                if (IconKey(item, settingsDraft.iconMatch) == key)
                    matches++;
                if (SameRuntime(old, item))
                    same = true;
            }
            if (matches != 1 || !same)
                valid = false;
        }
        FreeItems(live);
        if (!valid) {
            MessageBoxW(settingsWindow, L"图标列表已变化，请关闭并重新打开设置后再选择。",
                        L"请重新选择", MB_ICONWARNING);
            return false;
        }
    }
    for (int n : selected) {
        auto &selectedItem = settingsItems[n];
        std::wstring path = Lower(selectedItem.path), program = Lower(selectedItem.process);
        if (scope <= 0) {
            for (const auto &key : settingsMatcher.IconKeys(selectedItem))
                settingsMatcher.ForgetRule(key);
            auto key = IconKey(selectedItem, settingsDraft.iconMatch);
            settingsDraft.assignments[key] = group;
            if (selectedItem.runtime.Valid())
                settingsMatchState.ruleBindings[key] = selectedItem.runtime;
        } else {
            for (auto it = settingsDraft.assignments.begin();
                 it != settingsDraft.assignments.end();) {
                std::wstring savedPath = RulePath(it->first);
                if (scope == 1 ? savedPath == path : FileName(savedPath) == program) {
                    settingsMatchState.pendingRules.erase(it->first);
                    settingsMatchState.ruleBindings.erase(it->first);
                    it = settingsDraft.assignments.erase(it);
                } else
                    ++it;
            }
            if (scope == 1)
                settingsDraft.pathAssignments[path] = group;
            else {
                for (auto it = settingsDraft.pathAssignments.begin();
                     it != settingsDraft.pathAssignments.end();)
                    if (FileName(it->first) == program)
                        it = settingsDraft.pathAssignments.erase(it);
                    else
                        ++it;
                settingsDraft.programAssignments[program] = group;
            }
        }
    }
    settingsMatcher.ResolveGroups(settingsItems);
    for (size_t n = 0; n < settingsItems.size(); n++)
        ListView_SetItemText(listWindow, n, 2, (LPWSTR)settingsItems[n].group.c_str());
    groupEditing = false;
    return true;
}
static void SelectSettingsPage(int page) {
    if (settingsWindow && page >= 0 && page < SettingsPageCount)
        PropSheet_SetCurSel(settingsWindow, nullptr, page);
}
static void SettingsChanged() {
    if (!fillingSettings && settingsWindow && settingsPages[0])
        PropSheet_Changed(settingsWindow, settingsPages[0]);
}
static bool ReadNumber(HWND page, int id, unsigned minimum, unsigned maximum, int &value) {
    BOOL valid = FALSE;
    UINT number = GetDlgItemInt(page, id, &valid, FALSE);
    if (!valid || number < minimum || number > maximum) {
        SelectSettingsPage(page == settingsPages[1] ? 1 : 2);
        SetFocus(GetDlgItem(page, id));
        MessageBoxW(settingsWindow, L"请输入说明范围内的整数。", L"数值无效", MB_ICONWARNING);
        return false;
    }
    value = (int)number;
    return true;
}
static bool ReadCaptureDraft(CaptureTimer::Preferences &value) {
    wchar_t directory[32768] = {}, format[256] = {};
    GetDlgItemTextW(settingsPages[2], IDC_CAPTURE_DIRECTORY, directory, 32768);
    GetDlgItemTextW(settingsPages[2], IDC_CAPTURE_FORMAT, format, 256);
    value.directory = Trim(directory);
    value.format = Trim(format);
    int interval = 10;
    if (!ReadNumber(settingsPages[2], IDC_CAPTURE_INTERVAL, 1, 1440, interval))
        return false;
    value.intervalMinutes = (unsigned)interval;
    return true;
}
static bool ApplySettings(HWND h, bool closing) {
    if (groupEditing && !ApplyGroupEdit())
        return false;
    int mode = (int)SendMessageW(matchEdit, CB_GETCURSEL, 0, 0);
    settingsDraft.iconMatch = mode == 3   ? L"path-guid"
                              : mode == 1 ? L"path-id"
                              : mode == 2 ? L"path-class-tip"
                                          : L"path-class-id";
    settingsDraft.foldIcons = SendMessageW(foldCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!ReadNumber(settingsPages[1], IDC_PANEL_WIDTH, 340, 600, settingsDraft.panel.width) ||
        !ReadNumber(settingsPages[1], IDC_PANEL_ICON_SIZE, 20, 44, settingsDraft.panel.iconSize) ||
        !ReadNumber(settingsPages[1], IDC_PANEL_COLUMNS, 3, 6, settingsDraft.panel.columns))
        return false;
    settingsDraft.panel.showNames =
        IsDlgButtonChecked(settingsPages[1], IDC_PANEL_NAMES) == BST_CHECKED;
    settingsDraft.panel.showCapture =
        IsDlgButtonChecked(settingsPages[1], IDC_PANEL_CAPTURE) == BST_CHECKED;
    CaptureTimer::Preferences capture;
    if (!ReadCaptureDraft(capture))
        return false;
    bool captureChanged = capture.directory != captureDraft.directory ||
                          capture.format != captureDraft.format ||
                          capture.intervalMinutes != captureDraft.intervalMinutes;
    std::wstring error;
    if (captureChanged && !CaptureTimer::ValidatePreferences(capture, error)) {
        SelectSettingsPage(2);
        MessageBoxW(h, error.c_str(), L"截图设置无效", MB_ICONWARNING);
        return false;
    }
    bool groupingChanged = settingsDraft.iconMatch != settingsConfig.iconMatch ||
                           settingsDraft.foldIcons != settingsConfig.foldIcons ||
                           settingsDraft.assignments != settingsConfig.assignments ||
                           settingsDraft.pathAssignments != settingsConfig.pathAssignments ||
                           settingsDraft.programAssignments != settingsConfig.programAssignments;
    bool bindingsChanged = settingsMatchState.ruleBindings != matchState.ruleBindings ||
                           settingsMatchState.pendingRules != matchState.pendingRules;
    const auto &panel = settingsDraft.panel;
    const auto &oldPanel = settingsConfig.panel;
    bool configChanged = groupingChanged || panel.width != oldPanel.width ||
                         panel.iconSize != oldPanel.iconSize || panel.columns != oldPanel.columns ||
                         panel.showNames != oldPanel.showNames ||
                         panel.showCapture != oldPanel.showCapture;
    settingsDraft.settingsX = settingsConfig.settingsX;
    settingsDraft.settingsY = settingsConfig.settingsY;
    settingsDraft.haveSettingsPosition = settingsConfig.haveSettingsPosition;
    settingsDraft.debugLogging = DebugLog::enabled.load();
    if (configChanged && !settingsDraft.Save(configPath)) {
        MessageBoxW(h, L"配置保存失败，尚未应用。", L"TrayGroups", MB_ICONERROR);
        return false;
    }
    // 两份配置分别原子写入；截图配置失败时回滚分组配置，避免部分应用。
    if (captureChanged && !CaptureTimer::SavePreferences(capture, error)) {
        if (configChanged && !settingsConfig.Save(configPath))
            error += L"\n分组配置回滚失败，请检查目录权限。";
        MessageBoxW(h, error.c_str(), L"未能应用", MB_ICONERROR);
        return false;
    }
    if (!WallpaperPage::Apply(settingsPages[4], error)) {
        if (configChanged && !settingsConfig.Save(configPath))
            error += L"\n分组配置回滚失败。";
        if (captureChanged) {
            std::wstring captureError;
            if (!CaptureTimer::SavePreferences(captureDraft, captureError))
                error += L"\n截图配置回滚失败：" + captureError;
        }
        SelectSettingsPage(4);
        MessageBoxW(h, error.c_str(), L"壁纸未能应用", MB_ICONERROR);
        return false;
    }
    captureDraft = capture;
    if (configChanged)
        settingsConfig = settingsDraft;
    if (groupingChanged || bindingsChanged) {
        matchState = settingsMatchState;
        Refresh(true);
        settingsMatchState = matchState;
        if (!closing)
            FillSettings();
    }
    for (HWND page : settingsPages)
        PropSheet_UnChanged(h, page);
    return true;
}
static INT_PTR SettingsCommands(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(w), event = HIWORD(w);
        if (id == IDC_ICON_DETAILS) {
            int row = ListView_GetNextItem(listWindow, -1, LVNI_SELECTED);
            if (row >= 0 && row < (int)settingsItems.size()) {
                auto live = ReadTray(false);
                auto current = std::find_if(live.begin(), live.end(), [&](const TrayItem &i) {
                    return SameRuntime(i, settingsItems[row]);
                });
                if (current != live.end())
                    TrayInspector::ShowIcon(h, *current, settingsConfig.iconMatch,
                                            settingsConfig.foldIcons);
                else
                    MessageBoxW(h, L"图标已退出或重建，请重新打开设置刷新列表。", L"图标详情",
                                MB_ICONINFORMATION);
            } else
                MessageBoxW(h, L"请先选择一个图标。", L"图标详情", MB_ICONINFORMATION);
            return TRUE;
        }
        if (id == IDC_MANAGE_RULES) {
            if (groupEditing && !ApplyGroupEdit())
                return TRUE;
            if (TrayInspector::ManageRules(h, settingsDraft, settingsMatchState, ReadTray)) {
                FillSettings();
                SettingsChanged();
            }
            return TRUE;
        }
        if (id == IDC_GROUP && (event == CBN_EDITCHANGE || event == CBN_SELCHANGE)) {
            if (!fillingSettings) {
                if (event == CBN_SELCHANGE)
                    SelectComboText(groupEdit);
                groupEditing = true;
            }
            return TRUE;
        }
        if (id == IDC_UNGROUP) {
            SetWindowTextW(groupEdit, L"");
            ApplyGroupEdit();
            return TRUE;
        }
        if (id == IDC_RESOURCES) {
            ShowResources();
            return TRUE;
        }
        if (id == IDC_RESTORE_ALL) {
            PostMessageW(mainWindow, RestoreAllApp, 0, 0);
            return TRUE;
        }
        if (id == IDC_CAPTURE_NOW || id == IDC_CAPTURE_FOLDER) {
            CaptureTimer::HandleMenu(id == IDC_CAPTURE_NOW ? CaptureTimer::CaptureNowCommand
                                                           : CaptureTimer::FolderCommand);
            return TRUE;
        }
    }
    if (msg == WM_NOTIFY) {
        NMHDR *n = (NMHDR *)l;
        if (n->hwndFrom == listWindow && n->code == LVN_ITEMCHANGED && !fillingSettings &&
            !groupEditing) {
            auto change = (NMLISTVIEW *)l;
            if ((change->uChanged & LVIF_STATE) &&
                ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) &&
                (change->uNewState & LVIS_SELECTED)) {
                int row = change->iItem;
                if (row >= 0 && row < (int)settingsItems.size())
                    SetWindowTextW(groupEdit, settingsItems[row].group.c_str());
            }
        }
        return n->hwndFrom == listWindow;
    }
    return FALSE;
}
// 系统属性页先创建全部页面，以便应用时集中验证并提交编辑副本。
static INT_PTR CALLBACK SettingsPageProc(HWND page, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        auto *descriptor = (PROPSHEETPAGEW *)l;
        settingsPages[descriptor->lParam] = page;
        EnableThemeDialogTexture(page, ETDT_ENABLETAB);
        return TRUE;
    }
    // 壁纸页自行区分用户编辑与内部刷新，不能把刷新文本误记为全局草稿修改。
    if (page == settingsPages[4] && (message == WM_COMMAND || message == WM_DESTROY))
        return WallpaperPage::Handle(page, message, w, l);
    if (message == WM_NOTIFY) {
        auto *header = (NMHDR *)l;
        if (page == settingsPages[4] && header->code == PSN_SETACTIVE)
            WallpaperPage::Initialize(page, baseDir);
        if (header->code == PSN_APPLY) {
            auto *notification = reinterpret_cast<const PSHNOTIFY *>(l);
            bool success = page != settingsPages[0] ||
                           ApplySettings(settingsWindow, notification->lParam != 0);
            SetWindowLongPtrW(page, DWLP_MSGRESULT,
                              success ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }
        if (header->code == PSN_KILLACTIVE) {
            SetWindowLongPtrW(page, DWLP_MSGRESULT, FALSE);
            return TRUE;
        }
    }
    if (message == WM_COMMAND) {
        int event = HIWORD(w), id = LOWORD(w);
        if (event == EN_CHANGE || event == CBN_EDITCHANGE || event == CBN_SELCHANGE ||
            (event == BN_CLICKED && (id == IDC_FOLD || id == IDC_PANEL_NAMES ||
                                     id == IDC_PANEL_CAPTURE || id == IDC_UNGROUP)))
            SettingsChanged();
    }
    if (message == WM_COMMAND || message == WM_NOTIFY)
        return SettingsCommands(settingsWindow, message, w, l);
    return FALSE;
}
static LRESULT CALLBACK SettingsSheetProc(HWND window, UINT message, WPARAM w, LPARAM l,
                                          UINT_PTR id, DWORD_PTR) {
    // 这些页面没有取消确认；关闭与取消均丢弃草稿并立即释放无模式窗口。
    if (message == WM_CLOSE || (message == WM_SYSCOMMAND && (w & 0xfff0) == SC_CLOSE) ||
        (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_TIMER && w == 1) {
        SetDlgItemTextW(settingsPages[2], IDC_CAPTURE_STATUS, CaptureTimer::StatusText().c_str());
        return 0;
    }
    if (message == WM_EXITSIZEMOVE)
        RememberSettingsPosition(window);
    if (message == WM_DISPLAYCHANGE) {
        WallpaperPage::Refresh(settingsPages[4]);
    }
    if (message == WM_NCDESTROY) {
        KillTimer(window, 1);
        RememberSettingsPosition(window);
        RemoveWindowSubclass(window, SettingsSheetProc, id);
        auto result = DefSubclassProc(window, message, w, l);
        settingsWindow = nullptr;
        settingsTabs = nullptr;
        for (auto &page : settingsPages)
            page = nullptr;
        listWindow = nullptr;
        if (settingsImages)
            ImageList_Destroy(settingsImages);
        settingsImages = nullptr;
        FreeItems(settingsItems);
        LoadSettings();
        return result;
    }
    if ((message == WM_COMMAND && LOWORD(w) != IDC_APPLY && LOWORD(w) != IDOK &&
         LOWORD(w) != IDCANCEL) ||
        (message == WM_NOTIFY && ((NMHDR *)l)->hwndFrom == listWindow)) {
        if (SettingsCommands(window, message, w, l))
            return TRUE;
    }
    auto result = DefSubclassProc(window, message, w, l);
    // 无模式 PropertySheet 在确定/取消后需要宿主销毁，不遗留空白窗口。
    if (!fillingSettings && (message == WM_COMMAND || message == WM_CLOSE) && IsWindow(window) &&
        !PropSheet_GetCurrentPageHwnd(window))
        DestroyWindow(window);
    return result;
}
// 无模式属性页也可能在预处理键盘/系统消息时结束；所有消息入口共用销毁检查。
static bool ProcessSettingsMessage(MSG &message) {
    HWND window = settingsWindow;
    if (!window)
        return false;
    bool handled = PropSheet_IsDialogMessage(window, &message) != FALSE;
    if (IsWindow(window) && !PropSheet_GetCurrentPageHwnd(window))
        DestroyWindow(window);
    return handled;
}
static int CALLBACK SettingsSheetCallback(HWND window, UINT message, LPARAM) {
    if (message == PSCB_INITIALIZED) {
        settingsWindow = window;
        SetWindowSubclass(window, SettingsSheetProc, 1, 0);
    }
    return 0;
}
static RECT SettingsRect() {
    RECT desired = {settingsDraft.settingsX, settingsDraft.settingsY,
                    settingsDraft.settingsX + settingsWidth,
                    settingsDraft.settingsY + settingsHeight};
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT work = monitor.rcWork;
    int x = settingsDraft.haveSettingsPosition ? settingsDraft.settingsX : work.left + 40,
        y = settingsDraft.haveSettingsPosition ? settingsDraft.settingsY : work.top + 40;
    x = std::max((int)work.left, std::min(x, (int)work.right - settingsWidth));
    y = std::max((int)work.top, std::min(y, (int)work.bottom - settingsHeight));
    return RECT{x, y, x + settingsWidth, y + settingsHeight};
}
static void ShowSettings(int page) {
    ClosePopup();
    if (settingsWindow && IsWindow(settingsWindow)) {
        ShowWindow(settingsWindow, SW_RESTORE);
        SelectSettingsPage(page);
        SetForegroundWindow(settingsWindow);
        return;
    }
    settingsDraft = settingsConfig;
    settingsMatchState = matchState;
    captureDraft = CaptureTimer::GetPreferences();
    fillingSettings = true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const int templates[] = {IDD_GROUPS,   IDD_APPEARANCE, IDD_CAPTURE,
                             IDD_ADVANCED, IDD_WALLPAPER,  IDD_ABOUT};
    PROPSHEETPAGEW pages[SettingsPageCount] = {};
    for (int n = 0; n < SettingsPageCount; ++n) {
        pages[n].dwSize = sizeof(pages[n]);
        pages[n].dwFlags = PSP_PREMATURE;
        pages[n].hInstance = instance;
        pages[n].pszTemplate = MAKEINTRESOURCEW(templates[n]);
        pages[n].pfnDlgProc = SettingsPageProc;
        pages[n].lParam = n;
    }
    if (!appIcon)
        appIcon = MakeAppIcon();
    PROPSHEETHEADERW sheet = {};
    sheet.dwSize = sizeof(sheet);
    sheet.dwFlags =
        PSH_MODELESS | PSH_PROPSHEETPAGE | PSH_USECALLBACK | PSH_USEHICON | PSH_NOCONTEXTHELP;
    sheet.hInstance = instance;
    sheet.hIcon = appIcon;
    sheet.pszCaption = L"TrayGroups 设置";
    sheet.nPages = SettingsPageCount;
    sheet.nStartPage = std::clamp(page, 0, SettingsPageCount - 1);
    sheet.ppsp = pages;
    sheet.pfnCallback = SettingsSheetCallback;
    auto created = PropertySheetW(&sheet);
    if (created <= 0) {
        settingsWindow = nullptr;
        fillingSettings = false;
        LogError(L"设置属性页创建失败");
        return;
    }
    settingsWindow = (HWND)created;
    settingsTabs = PropSheet_GetTabControl(settingsWindow);
    auto group = settingsPages[0], advanced = settingsPages[3];
    listWindow = GetDlgItem(group, IDC_ICON_LIST);
    groupEdit = GetDlgItem(group, IDC_GROUP);
    scopeEdit = GetDlgItem(group, IDC_SCOPE);
    matchEdit = GetDlgItem(advanced, IDC_MATCH);
    foldCheck = GetDlgItem(advanced, IDC_FOLD);
    ListView_SetExtendedListViewStyle(listWindow,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    const wchar_t *titles[] = {L"程序", L"提示", L"组名", L"完整路径", L"图标名称", L"匹配状态"};
    int widths[] = {125, 165, 80, 280, 180, 180}, dpi = GetDpiForWindow(settingsWindow);
    for (int i = 0; i < 6; i++) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)titles[i];
        col.cx = MulDiv(widths[i], dpi, 96);
        ListView_InsertColumn(listWindow, i, &col);
    }
    int columnOrder[] = {0, 1, 2, 5, 3, 4};
    ListView_SetColumnOrderArray(listWindow, 6, columnOrder);
    SetWindowTheme(listWindow, L"Explorer", nullptr);
    for (HWND combo : {groupEdit})
        SendMessageW(combo, CB_LIMITTEXT, 100, 0);
    for (auto text : {L"仅选中的图标（默认）", L"此完整路径的全部图标", L"此程序名的全部图标"})
        SendMessageW(scopeEdit, CB_ADDSTRING, 0, (LPARAM)text);
    SendMessageW(scopeEdit, CB_SETCURSEL, 0, 0);
    for (auto text :
         {L"路径 + 窗口类别 + 图标编号", L"路径 + 图标编号", L"路径 + 窗口类别 + 提示文字",
          L"路径 + GUID 优先（推荐；无 GUID 时自动回退）"})
        SendMessageW(matchEdit, CB_ADDSTRING, 0, (LPARAM)text);
    if (!appIcon)
        appIcon = MakeAppIcon();
    for (int size : {ICON_BIG, ICON_SMALL})
        SendMessageW(settingsWindow, WM_SETICON, size, (LPARAM)(HICON)appIcon);
    RECT bounds;
    GetWindowRect(settingsWindow, &bounds);
    settingsWidth = bounds.right - bounds.left;
    settingsHeight = bounds.bottom - bounds.top;
    RECT placement = SettingsRect();
    SetWindowPos(settingsWindow, nullptr, placement.left, placement.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    FillSettings();
    fillingSettings = true;
    SetDlgItemInt(settingsPages[1], IDC_PANEL_WIDTH, settingsDraft.panel.width, FALSE);
    SetDlgItemInt(settingsPages[1], IDC_PANEL_ICON_SIZE, settingsDraft.panel.iconSize, FALSE);
    SetDlgItemInt(settingsPages[1], IDC_PANEL_COLUMNS, settingsDraft.panel.columns, FALSE);
    CheckDlgButton(settingsPages[1], IDC_PANEL_NAMES,
                   settingsDraft.panel.showNames ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settingsPages[1], IDC_PANEL_CAPTURE,
                   settingsDraft.panel.showCapture ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(settingsPages[2], IDC_CAPTURE_DIRECTORY, captureDraft.directory.c_str());
    SetDlgItemTextW(settingsPages[2], IDC_CAPTURE_FORMAT, captureDraft.format.c_str());
    SetDlgItemInt(settingsPages[2], IDC_CAPTURE_INTERVAL, captureDraft.intervalMinutes, FALSE);
    SendDlgItemMessageW(settingsPages[2], IDC_CAPTURE_DIRECTORY, EM_SETLIMITTEXT, 32767, 0);
    SendDlgItemMessageW(settingsPages[2], IDC_CAPTURE_FORMAT, EM_SETLIMITTEXT, 255, 0);
    SendDlgItemMessageW(settingsPages[2], IDC_CAPTURE_INTERVAL, EM_SETLIMITTEXT, 4, 0);
    SetDlgItemTextW(settingsPages[2], IDC_CAPTURE_STATUS, CaptureTimer::StatusText().c_str());
    SetDlgItemTextW(settingsPages[5], IDC_ABOUT_VERSION,
                    L"TrayGroups " TRAYGROUPS_WIDEN(TRAYGROUPS_VERSION_TEXT));
    fillingSettings = false;
    for (HWND current : settingsPages)
        PropSheet_UnChanged(settingsWindow, current);
    SetTimer(settingsWindow, 1, 1000, nullptr);
    SelectSettingsPage(page);
    ShowWindow(settingsWindow, SW_RESTORE);
    SetForegroundWindow(settingsWindow);
}
