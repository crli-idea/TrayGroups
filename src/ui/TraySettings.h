// 主窗口私有设置页实现；布局定义在 resources/TrayGroups.rc。
// 分组/外观编辑先留在界面状态，点“应用”才保存；关闭只合并窗口位置。
#pragma once
#include "TraySettingsIds.h"
#include <uxtheme.h>

static HWND settingsTabs = nullptr, settingsPages[3] = {}, appearanceGroup = nullptr;
static std::wstring appearanceName;
static int settingsWidth = 1000, settingsHeight = 680;

static std::wstring ControlText(HWND control) {
    wchar_t text[128] = {};
    GetWindowTextW(control, text, 128);
    return Trim(text);
}
static void FillGroupChoices() {
    auto groups = GroupNames();
    for (HWND combo : {groupEdit, appearanceGroup}) {
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
static void UpdateLabelSize() {
    wchar_t name[128];
    GetWindowTextW(appearanceGroup, name, 128);
    bool previous = fillingSettings;
    fillingSettings = true;
    std::wstring group = Trim(name);
    appearanceName = group;
    SetWindowTextW(labelSizeEdit, std::to_wstring(settingsConfig.GroupLabelSize(group)).c_str());
    SetWindowTextW(labelWidthEdit, std::to_wstring(settingsConfig.GroupLabelWidth(group)).c_str());
    SetWindowTextW(labelHeightEdit,
                   std::to_wstring(settingsConfig.GroupLabelHeight(group)).c_str());
    labelEditing = false;
    fillingSettings = previous;
}
static void FillSettings() {
    fillingSettings = true;
    SendMessageW(listWindow, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(listWindow);
    FreeItems(settingsItems);
    settingsItems = ReadTray();
    if (settingsImages) {
        ListView_SetImageList(listWindow, nullptr, LVSIL_SMALL);
        ImageList_Destroy(settingsImages);
    }
    int iconSize = MulDiv(16, GetDpiForWindow(settingsWindow), 96);
    settingsImages = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK,
                                      (int)settingsItems.size() + 1, 4);
    ListView_SetImageList(listWindow, settingsImages, LVSIL_SMALL);
    for (size_t n = 0; n < settingsItems.size(); n++) {
        auto &i = settingsItems[n];
        if (i.fallbackImage && !i.path.empty()) {
            HICON extracted = nullptr;
            if (ExtractIconExW(i.path.c_str(), 0, nullptr, &extracted, 1) && extracted) {
                DestroyIcon(i.image);
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
            status += !settingsConfig.foldIcons    ? L"；折叠已暂停"
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
                 settingsConfig.iconMatch == L"path-guid"        ? 3
                 : settingsConfig.iconMatch == L"path-id"        ? 1
                 : settingsConfig.iconMatch == L"path-class-tip" ? 2
                                                                 : 0,
                 0);
    SendMessageW(foldCheck, BM_SETCHECK, settingsConfig.foldIcons ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(groupEdit, L"");
    groupEditing = false;
    UpdateLabelSize();
    fillingSettings = false;
}
static bool ApplyGroupEdit() {
    int row = -1;
    wchar_t text[128];
    GetWindowTextW(groupEdit, text, 128);
    std::wstring group = Trim(text);
    int scope = (int)SendMessageW(scopeEdit, CB_GETCURSEL, 0, 0),
        match = (int)SendMessageW(matchEdit, CB_GETCURSEL, 0, 0);
    settingsConfig.iconMatch = match == 3   ? L"path-guid"
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
            auto key = IconKey(settingsItems[n], settingsConfig.iconMatch);
            unsigned count = 0;
            for (const auto &i : settingsItems)
                if (IconKey(i, settingsConfig.iconMatch) == key)
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
            auto key = IconKey(old, settingsConfig.iconMatch);
            unsigned matches = 0;
            bool same = false;
            for (const auto &item : live) {
                if (IconKey(item, settingsConfig.iconMatch) == key)
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
            for (const auto &key : matcher.IconKeys(selectedItem))
                matcher.ForgetRule(key);
            auto key = IconKey(selectedItem, settingsConfig.iconMatch);
            settingsConfig.assignments[key] = group;
            if (selectedItem.runtime.Valid())
                matchState.ruleBindings[key] = selectedItem.runtime;
        } else {
            for (auto it = settingsConfig.assignments.begin();
                 it != settingsConfig.assignments.end();) {
                std::wstring savedPath = RulePath(it->first);
                if (scope == 1 ? savedPath == path : FileName(savedPath) == program) {
                    matchState.pendingRules.erase(it->first);
                    matchState.ruleBindings.erase(it->first);
                    it = settingsConfig.assignments.erase(it);
                } else
                    ++it;
            }
            if (scope == 1)
                settingsConfig.pathAssignments[path] = group;
            else {
                for (auto it = settingsConfig.pathAssignments.begin();
                     it != settingsConfig.pathAssignments.end();)
                    if (FileName(it->first) == program)
                        it = settingsConfig.pathAssignments.erase(it);
                    else
                        ++it;
                settingsConfig.programAssignments[program] = group;
            }
        }
    }
    matcher.ResolveGroups(settingsItems);
    for (size_t n = 0; n < settingsItems.size(); n++)
        ListView_SetItemText(listWindow, n, 2, (LPWSTR)settingsItems[n].group.c_str());
    groupEditing = false;
    return true;
}
static bool SaveLabelSize() {
    wchar_t size[32];
    GetWindowTextW(labelSizeEdit, size, 32);
    int points = _wtoi(size);
    if (points < 8 || points > 32) {
        MessageBoxW(settingsWindow, L"标签字号请填写 8 到 32（磅）。", L"字号无效", MB_ICONWARNING);
        return false;
    }
    wchar_t value[32];
    GetWindowTextW(labelWidthEdit, value, 32);
    int width = _wtoi(value);
    GetWindowTextW(labelHeightEdit, value, 32);
    int height = _wtoi(value);
    if (width < 120 || width > 600 || (height != 0 && (height < 32 || height > 160))) {
        MessageBoxW(settingsWindow, L"标签宽度：120–600；高度：32–160，或填 0 自动。", L"大小无效",
                    MB_ICONWARNING);
        return false;
    }
    std::wstring group = appearanceName;
    settingsConfig.groupLabelSizes[group] = points;
    settingsConfig.groupLabelWidths[group] = width;
    settingsConfig.groupLabelHeights[group] = height;
    labelEditing = false;
    return true;
}

static void SelectSettingsPage(int page) {
    if (page < 0 || page >= 3)
        return;
    TabCtrl_SetCurSel(settingsTabs, page);
    for (int n = 0; n < 3; n++)
        ShowWindow(settingsPages[n], n == page ? SW_SHOW : SW_HIDE);
}
static bool ApplySettings(HWND h) {
    if (groupEditing && !ApplyGroupEdit())
        return false;
    if (labelEditing && !SaveLabelSize())
        return false;
    int mode = (int)SendMessageW(matchEdit, CB_GETCURSEL, 0, 0);
    settingsConfig.iconMatch = mode == 3   ? L"path-guid"
                               : mode == 1 ? L"path-id"
                               : mode == 2 ? L"path-class-tip"
                                           : L"path-class-id";
    settingsConfig.foldIcons = SendMessageW(foldCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!SaveSettings()) {
        MessageBoxW(h, L"配置保存失败，尚未应用。", L"TrayGroups", MB_ICONERROR);
        return false;
    }
    Refresh(true);
    FillSettings();
    SetWindowTextW(h, L"TrayGroups 设置 · 已应用");
    return true;
}
static INT_PTR CALLBACK SettingsPageProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY)
        return SendMessageW(GetParent(h), msg, w, l);
    if (msg == WM_INITDIALOG) {
        EnableThemeDialogTexture(h, ETDT_ENABLETAB);
        return TRUE;
    }
    return FALSE;
}
static INT_PTR CALLBACK SettingsProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(w), event = HIWORD(w);
        if (id == IDC_GROUP && (event == CBN_EDITCHANGE || event == CBN_SELCHANGE)) {
            if (!fillingSettings) {
                if (event == CBN_SELCHANGE)
                    SelectComboText(groupEdit);
                groupEditing = true;
            }
            return TRUE;
        }
        if (id == IDC_APPEARANCE_GROUP && (event == CBN_EDITCHANGE || event == CBN_SELCHANGE)) {
            if (!fillingSettings) {
                if (labelEditing && !SaveLabelSize()) {
                    SetWindowTextW(appearanceGroup, appearanceName.c_str());
                    return TRUE;
                }
                if (event == CBN_SELCHANGE)
                    SelectComboText(appearanceGroup);
                UpdateLabelSize();
            }
            return TRUE;
        }
        if ((id == IDC_LABEL_SIZE || id == IDC_LABEL_WIDTH || id == IDC_LABEL_HEIGHT) &&
            event == EN_CHANGE) {
            if (!fillingSettings)
                labelEditing = true;
            return TRUE;
        }
        if (id == IDC_APPLY || id == IDOK) {
            ApplySettings(h);
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
        if (id == IDC_CLOSE || id == IDCANCEL) {
            SendMessageW(h, WM_CLOSE, 0, 0);
            return TRUE;
        }
    }
    if (msg == WM_NOTIFY) {
        NMHDR *n = (NMHDR *)l;
        if (n->hwndFrom == settingsTabs && n->code == TCN_SELCHANGE) {
            SelectSettingsPage(TabCtrl_GetCurSel(settingsTabs));
            return TRUE;
        }
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
        return TRUE;
    }
    if (msg == WM_EXITSIZEMOVE) {
        RememberSettingsPosition(h);
        return TRUE;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(h);
        return TRUE;
    }
    if (msg == WM_DESTROY) {
        RememberSettingsPosition(h);
        settingsWindow = nullptr;
        listWindow = nullptr;
        if (settingsImages) {
            ImageList_Destroy(settingsImages);
            settingsImages = nullptr;
        }
        FreeItems(settingsItems);
        LoadSettings();
        return TRUE;
    }
    return FALSE;
}
static RECT SettingsRect() {
    RECT desired = {settingsConfig.settingsX, settingsConfig.settingsY,
                    settingsConfig.settingsX + settingsWidth,
                    settingsConfig.settingsY + settingsHeight};
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&desired, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT work = monitor.rcWork;
    int x = settingsConfig.haveSettingsPosition ? settingsConfig.settingsX : work.left + 40,
        y = settingsConfig.haveSettingsPosition ? settingsConfig.settingsY : work.top + 40;
    x = std::max((int)work.left, std::min(x, (int)work.right - settingsWidth));
    y = std::max((int)work.top, std::min(y, (int)work.bottom - settingsHeight));
    return RECT{x, y, x + settingsWidth, y + settingsHeight};
}
static void ShowSettings() {
    ClosePopup();
    if (settingsWindow && IsWindow(settingsWindow)) {
        ShowWindow(settingsWindow, SW_RESTORE);
        SetForegroundWindow(settingsWindow);
        return;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    settingsWindow =
        CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, SettingsProc, 0);
    if (!settingsWindow) {
        LogError(L"设置窗口创建失败：" + Number(GetLastError()));
        return;
    }
    settingsTabs = GetDlgItem(settingsWindow, IDC_SETTINGS_TABS);
    const wchar_t *names[] = {L"分组", L"外观", L"高级"};
    const int pages[] = {IDD_GROUPS, IDD_APPEARANCE, IDD_ADVANCED};
    for (int n = 0; n < 3; n++) {
        TCITEMW tab = {};
        tab.mask = TCIF_TEXT;
        tab.pszText = (LPWSTR)names[n];
        TabCtrl_InsertItem(settingsTabs, n, &tab);
    }
    RECT area;
    GetWindowRect(settingsTabs, &area);
    MapWindowPoints(nullptr, settingsWindow, (POINT *)&area, 2);
    TabCtrl_AdjustRect(settingsTabs, FALSE, &area);
    for (int n = 0; n < 3; n++) {
        settingsPages[n] = CreateDialogParamW(instance, MAKEINTRESOURCEW(pages[n]), settingsWindow,
                                              SettingsPageProc, 0);
        if (!settingsPages[n]) {
            LogError(L"设置分页创建失败");
            DestroyWindow(settingsWindow);
            return;
        }
        SetWindowPos(settingsPages[n], HWND_TOP, area.left, area.top, area.right - area.left,
                     area.bottom - area.top, 0);
    }
    auto group = settingsPages[0], appearance = settingsPages[1], advanced = settingsPages[2];
    listWindow = GetDlgItem(group, IDC_ICON_LIST);
    groupEdit = GetDlgItem(group, IDC_GROUP);
    scopeEdit = GetDlgItem(group, IDC_SCOPE);
    appearanceGroup = GetDlgItem(appearance, IDC_APPEARANCE_GROUP);
    labelSizeEdit = GetDlgItem(appearance, IDC_LABEL_SIZE);
    labelWidthEdit = GetDlgItem(appearance, IDC_LABEL_WIDTH);
    labelHeightEdit = GetDlgItem(appearance, IDC_LABEL_HEIGHT);
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
    for (HWND combo : {groupEdit, appearanceGroup})
        SendMessageW(combo, CB_LIMITTEXT, 100, 0);
    for (auto text : {L"仅选中的图标（默认）", L"此完整路径的全部图标", L"此程序名的全部图标"})
        SendMessageW(scopeEdit, CB_ADDSTRING, 0, (LPARAM)text);
    SendMessageW(scopeEdit, CB_SETCURSEL, 0, 0);
    for (auto text :
         {L"路径 + 窗口类别 + 图标编号", L"路径 + 图标编号", L"路径 + 窗口类别 + 提示文字",
          L"路径 + GUID 优先（推荐；无 GUID 时自动回退）"})
        SendMessageW(matchEdit, CB_ADDSTRING, 0, (LPARAM)text);
    SendMessageW(labelSizeEdit, EM_SETLIMITTEXT, 2, 0);
    for (HWND edit : {labelWidthEdit, labelHeightEdit})
        SendMessageW(edit, EM_SETLIMITTEXT, 3, 0);
    if (!appIcon)
        appIcon = MakeAppIcon();
    for (int size : {ICON_BIG, ICON_SMALL})
        SendMessageW(settingsWindow, WM_SETICON, size, (LPARAM)appIcon);
    RECT bounds;
    GetWindowRect(settingsWindow, &bounds);
    settingsWidth = bounds.right - bounds.left;
    settingsHeight = bounds.bottom - bounds.top;
    RECT placement = SettingsRect();
    SetWindowPos(settingsWindow, nullptr, placement.left, placement.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    FillSettings();
    SelectSettingsPage(0);
    ShowWindow(settingsWindow, SW_RESTORE);
    SetForegroundWindow(settingsWindow);
}
