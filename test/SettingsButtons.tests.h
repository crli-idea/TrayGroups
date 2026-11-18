#pragma once
#include <functional>

// 只操作本进程注册的图标与隔离配置。按钮使用 BM_CLICK，关闭走 SC_CLOSE 和消息预处理。
static std::atomic<unsigned> buttonCaptureCount{0};
static std::wstring buttonFolderRequest, buttonExpectedKey;
static int buttonModalMode = 0;
static bool buttonModalOk = true;
static constexpr const wchar_t *ButtonFixtureTip = L"TrayGroups settings buttons isolated fixture";
static bool ButtonCapture(const CaptureTimer::Settings &) {
    ++buttonCaptureCount;
    return true;
}
static void ButtonFolder(const std::wstring &path) {
    buttonFolderRequest = path;
}
static HWND ButtonDialog(const wchar_t *title) {
    struct Search {
        const wchar_t *title;
        HWND result = nullptr;
    } search{title};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM data) -> BOOL {
            auto &search = *(Search *)data;
            wchar_t text[256] = {}, cls[64] = {};
            GetWindowTextW(window, text, 256);
            GetClassNameW(window, cls, 64);
            if (!wcscmp(cls, L"#32770") && !wcscmp(text, search.title))
                search.result = window;
            return TRUE;
        },
        (LPARAM)&search);
    return search.result;
}
static void CALLBACK ButtonModalTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = ButtonDialog(buttonModalMode < 3 ? L"图标详情" : L"失效规则管理");
    if (!dialog)
        return;
    KillTimer(nullptr, timer);
    if (buttonModalMode < 3) {
        wchar_t text[16384] = {};
        GetDlgItemTextW(dialog, IDC_DETAIL_TEXT, text, 16384);
        buttonModalOk &= std::wstring(text).find(buttonExpectedKey) != std::wstring::npos;
        if (buttonModalMode == 1)
            SendMessageW(GetDlgItem(dialog, IDOK), BM_CLICK, 0, 0);
        else
            SendMessageW(dialog, WM_SYSCOMMAND, SC_CLOSE, 0);
    } else {
        HWND list = GetDlgItem(dialog, IDC_RULE_LIST);
        buttonModalOk &= ListView_GetItemCount(list) == 1;
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        SendMessageW(GetDlgItem(dialog, IDC_RULE_DELETE), BM_CLICK, 0, 0);
        buttonModalOk &= ListView_GetItemCount(list) == 1;
        SendMessageW(GetDlgItem(dialog, IDC_RULE_REFRESH), BM_CLICK, 0, 0);
        buttonModalOk &= ListView_GetItemCount(list) == 1;
        if (buttonModalMode == 5) {
            HWND targets = GetDlgItem(dialog, IDC_RULE_TARGET);
            int selected = -1;
            for (int n = 0; n < (int)SendMessageW(targets, CB_GETCOUNT, 0, 0); ++n) {
                int length = (int)SendMessageW(targets, CB_GETLBTEXTLEN, n, 0);
                std::wstring text(length + 1, L'\0');
                SendMessageW(targets, CB_GETLBTEXT, n, (LPARAM)text.data());
                if (text.find(ButtonFixtureTip) != std::wstring::npos)
                    selected = n;
            }
            buttonModalOk &= selected >= 0;
            SendMessageW(targets, CB_SETCURSEL, selected, 0);
            SendMessageW(GetDlgItem(dialog, IDC_RULE_REBIND), BM_CLICK, 0, 0);
        } else
            SendMessageW(GetDlgItem(dialog, IDC_RULE_DELETE), BM_CLICK, 0, 0);
        buttonModalOk &= ListView_GetItemCount(list) == 0;
        if (buttonModalMode == 4)
            SendMessageW(dialog, WM_SYSCOMMAND, SC_CLOSE, 0);
        else
            SendMessageW(GetDlgItem(dialog, buttonModalMode == 3 ? IDCANCEL : IDOK), BM_CLICK, 0,
                         0);
    }
}
static bool PumpButtonMessages(const std::function<bool()> &done) {
    DWORD start = GetTickCount();
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (!ProcessSettingsMessage(message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (done())
            return true;
        Sleep(1);
    } while (GetTickCount() - start < 2500);
    return false;
}
static bool WaitForButtonIcon(UINT id) {
    // Shell_NotifyIcon 的返回不代表 Explorer 已完成工具栏更新，等待可扫描的注册状态。
    return PumpButtonMessages([&] {
        auto tray = ReadTray(false);
        return std::any_of(tray.begin(), tray.end(), [&](const TrayItem &icon) {
            return icon.owner == mainWindow && icon.id == id;
        });
    });
}
static bool SettingsButtonsRegression(std::wstring &report) {
    bool ok = true;
    auto check = [&](bool value, const wchar_t *name) {
        report += (value ? L"PASS: " : L"FAIL: ") + std::wstring(name) + L"\n";
        ok &= value;
    };
    auto previous = settingsConfig;
    auto previousMatch = matchState;
    auto previousRecovery = recoveryPath;
    recoveryPath = baseDir + L"\\buttons-recovery.ini";
    settingsConfig = TrayConfig::Settings{};
    settingsConfig.iconMatch = L"path-class-id";
    settingsConfig.foldIcons = false;
    settingsConfig.Save(configPath);
    NOTIFYICONDATAW notification = {};
    notification.cbSize = sizeof(notification);
    notification.hWnd = mainWindow;
    notification.uID = 751;
    notification.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notification.hIcon = appIcon;
    notification.uCallbackMessage = ActCallback;
    wcscpy_s(notification.szTip, ButtonFixtureTip);
    testingOwnIcons = buttonHostMessages = true;
    bool added = Shell_NotifyIconW(NIM_ADD, &notification) != FALSE;
    check(added, L"button fixture registers only this test process's icon");
    check(WaitForButtonIcon(notification.uID),
          L"Explorer completes the button fixture registration before UI actions");
    ShowSettings();
    std::set<std::pair<int, int>> inventory, clicked;
    for (int page = -1; page < SettingsPageCount; ++page) {
        HWND parent = page < 0 ? settingsWindow : settingsPages[page];
        for (HWND child = GetWindow(parent, GW_CHILD); child;
             child = GetWindow(child, GW_HWNDNEXT)) {
            wchar_t cls[32] = {};
            GetClassNameW(child, cls, 32);
            auto type = GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK;
            if (!wcscmp(cls, L"Button") && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) &&
                (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_AUTOCHECKBOX))
                inventory.insert({page, GetDlgCtrlID(child)});
        }
    }
    auto click = [&](int page, int id) {
        if (page >= 0)
            SelectSettingsPage(page);
        HWND button = GetDlgItem(page < 0 ? settingsWindow : settingsPages[page], id);
        bool available = button && IsWindowEnabled(button);
        if (available) {
            clicked.insert({page, id});
            SendMessageW(button, BM_CLICK, 0, 0);
        }
        return available;
    };
    auto selectFixture = [&]() -> int {
        for (int n = 0; n < (int)settingsItems.size(); ++n)
            if (settingsItems[n].owner == mainWindow && settingsItems[n].id == notification.uID) {
                ListView_SetItemState(listWindow, -1, 0, LVIS_SELECTED);
                ListView_SetItemState(listWindow, n, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                return n;
            }
        return -1;
    };
    int row = selectFixture();
    check(row >= 0, L"settings list includes the isolated fixture");
    TrayItem fixture;
    if (row >= 0)
        fixture = settingsItems[row];
    buttonExpectedKey = IconKey(fixture, settingsDraft.iconMatch);
    auto stale = fixture;
    stale.id += 100000;
    auto staleKey = IconKey(stale, settingsDraft.iconMatch);
    auto modal = [&](int mode, int id) {
        buttonModalMode = mode;
        buttonModalOk = true;
        auto timer = SetTimer(nullptr, 0, 40, ButtonModalTimer);
        bool opened = timer && click(0, id);
        if (timer)
            KillTimer(nullptr, timer);
        return opened && buttonModalOk;
    };
    if (row >= 0) {
        check(modal(1, IDC_ICON_DETAILS),
              L"Icon Details button opens the selected icon and its Close button works");
        check(modal(2, IDC_ICON_DETAILS),
              L"Icon Details child window also closes through its title-bar X");
        settingsDraft.assignments[staleKey] = L"按钮测试组";
        check(modal(3, IDC_MANAGE_RULES) && settingsDraft.assignments.count(staleKey),
              L"Rule Manager refresh/delete work and Cancel rejects staged deletion");
        check(modal(4, IDC_MANAGE_RULES) && settingsDraft.assignments.count(staleKey),
              L"Rule Manager title-bar X rejects staged deletion");
        check(
            modal(5, IDC_MANAGE_RULES) && !settingsDraft.assignments.count(staleKey) &&
                settingsDraft.assignments[buttonExpectedKey] == L"按钮测试组",
            L"Rule Manager rebind and OK stage the selected live icon without saving prematurely");
        selectFixture();
        check(click(0, IDC_UNGROUP) && settingsDraft.assignments[buttonExpectedKey].empty() &&
                  VisibleAtRuntime(fixture.runtime),
              L"Ungroup button changes only the selected draft icon");
        SetWindowTextW(groupEdit, L"按钮测试组");
        groupEditing = true;
    }
    bool names = IsDlgButtonChecked(settingsPages[1], IDC_PANEL_NAMES) == BST_CHECKED;
    bool footer = IsDlgButtonChecked(settingsPages[1], IDC_PANEL_CAPTURE) == BST_CHECKED;
    click(1, IDC_PANEL_NAMES);
    click(1, IDC_PANEL_CAPTURE);
    click(3, IDC_FOLD);
    check(click(-1, IDC_APPLY) && settingsWindow && settingsConfig.panel.showNames != names &&
              settingsConfig.panel.showCapture != footer && settingsConfig.foldIcons,
          L"both appearance checkboxes, Fold checkbox and Apply persist their new values");
    check(row < 0 || !VisibleAtRuntime(fixture.runtime),
          L"Apply really hides only the grouped fixture");
    check(click(3, IDC_RESTORE_ALL) && PumpButtonMessages([&] { return hidden.empty(); }) &&
              (row < 0 || VisibleAtRuntime(fixture.runtime)) && !settingsConfig.foldIcons &&
              IsDlgButtonChecked(settingsPages[3], IDC_FOLD) == BST_UNCHECKED,
          L"Restore All button restores owned icons, pauses folding and unchecks the Fold control");
    check(click(3, IDC_RESOURCES) && resourceWindow && IsWindowVisible(resourceWindow),
          L"Resources button opens the process resource window");
    if (resourceWindow) {
        HWND window = resourceWindow;
        SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
        check(!resourceWindow && !IsWindow(window),
              L"Resources child window closes through its title-bar X");
    }
    auto encoder = CaptureTimer::testCapture;
    CaptureTimer::testCapture = ButtonCapture;
    buttonCaptureCount = 0;
    check(click(2, IDC_CAPTURE_NOW) && PumpButtonMessages([] { return !CaptureTimer::IsBusy(); }) &&
              buttonCaptureCount == 1 &&
              CaptureTimer::StatusText().find(L"已保存") != std::wstring::npos,
          L"Capture Now button completes one worker request and updates visible capture status");
    CaptureTimer::testCapture = encoder;
    CaptureTimer::testOpenFolder = ButtonFolder;
    buttonFolderRequest.clear();
    auto directory = CaptureTimer::settings.directory;
    auto firstDirectory = baseDir + L"\\buttons-first-folder-" + Number(GetTickCount64());
    CaptureTimer::settings.directory = firstDirectory;
    check(click(2, IDC_CAPTURE_FOLDER) && buttonFolderRequest == firstDirectory &&
              (GetFileAttributesW(firstDirectory.c_str()) != INVALID_FILE_ATTRIBUTES &&
               (GetFileAttributesW(firstDirectory.c_str()) & FILE_ATTRIBUTE_DIRECTORY)),
          L"Open Folder button creates the first-use output directory before requesting Explorer");
    CaptureTimer::settings.directory = directory;
    RemoveDirectoryW(firstDirectory.c_str());
    CaptureTimer::testOpenFolder = nullptr;
    SetDlgItemInt(settingsPages[1], IDC_PANEL_WIDTH, 480, FALSE);
    check(click(-1, IDCANCEL) && !settingsWindow && settingsConfig.panel.width != 480,
          L"Cancel button destroys the sheet and discards unapplied changes");
    ShowSettings();
    SetDlgItemInt(settingsPages[1], IDC_PANEL_WIDTH, 460, FALSE);
    check(click(-1, IDOK) && !settingsWindow && settingsConfig.panel.width == 460,
          L"OK button applies changes then destroys the sheet");
    check(clicked == inventory,
          L"every push button and checkbox in all five settings pages was clicked");
    report += L"SettingsButtonCoverage=" + Number(clicked.size()) + L"/" +
              Number(inventory.size()) + L"\n";
    for (int page = 0; page < SettingsPageCount; ++page) {
        ShowSettings(page);
        HWND window = settingsWindow;
        SetDlgItemInt(settingsPages[1], IDC_PANEL_WIDTH, 570, FALSE);
        if (page % 2) {
            MSG close = {};
            close.hwnd = window;
            close.message = WM_SYSCOMMAND;
            close.wParam = SC_CLOSE;
            if (!ProcessSettingsMessage(close))
                DispatchMessageW(&close);
        } else if (page == 2)
            SendMessageW(window, WM_SYSKEYDOWN, VK_F4, ((LPARAM)1 << 29) | 1);
        else
            SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
        bool closed = PumpButtonMessages([&] { return !IsWindow(window); });
        check(closed && !settingsWindow && !IsWindow(window) && settingsConfig.panel.width == 460 &&
                  !settingsImages && settingsItems.empty() && IsWindow(mainWindow),
              (L"X closes page " + Number(page) +
               L", discards edits and releases handles through system/message-loop paths")
                  .c_str());
        if (settingsWindow)
            DestroyWindow(settingsWindow);
    }
    settingsConfig.panel.showCapture = true;
    settingsConfig.Save(configPath);
    ShowGroups();
    SendMessageW(GetDlgItem(popupWindow, CaptureTimer::SettingsCommand), BM_CLICK, 0, 0);
    check(PumpButtonMessages([] { return settingsWindow != nullptr; }) && !popupWindow &&
              TabCtrl_GetCurSel(settingsTabs) == 2,
          L"T panel Screenshot Settings button opens the unified capture property page");
    if (settingsWindow)
        SendMessageW(settingsWindow, WM_SYSCOMMAND, SC_CLOSE, 0);
    CaptureTimer::testCapture = ButtonCapture;
    unsigned captures = buttonCaptureCount.load();
    ShowGroups();
    SendMessageW(GetDlgItem(popupWindow, CaptureTimer::CaptureNowCommand), BM_CLICK, 0, 0);
    check(PumpButtonMessages(
              [&] { return buttonCaptureCount > captures && !CaptureTimer::IsBusy(); }) &&
              !popupWindow,
          L"T panel Capture Now button closes the panel and completes a worker request");
    CaptureTimer::testCapture = encoder;
    if (row >= 0) {
        settingsConfig.foldIcons = true;
        settingsConfig.Save(configPath);
        Refresh(true);
        bool hiddenFixture = !VisibleAtRuntime(fixture.runtime);
        ShowGroups();
        SendMessageW(GetDlgItem(popupWindow, IDC_RESTORE_ALL), BM_CLICK, 0, 0);
        check(hiddenFixture &&
                  PumpButtonMessages([&] { return hidden.empty() && !settingsConfig.foldIcons; }) &&
                  VisibleAtRuntime(fixture.runtime) && !popupWindow,
              L"T panel Restore All button restores its hidden fixture and pauses folding");
    }
    ClosePopup();
    RestoreHidden();
    if (added)
        Shell_NotifyIconW(NIM_DELETE, &notification);
    buttonHostMessages = testingOwnIcons = false;
    recoveryPath = previousRecovery;
    settingsConfig = previous;
    matchState = previousMatch;
    settingsConfig.Save(configPath);
    FreeItems(items);
    return ok;
}
