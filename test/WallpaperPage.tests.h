#pragma once
#include "WallpaperTestSupport.h"

static std::wstring wallpaperBrowseResult;
static std::wstring WallpaperBrowse(HWND, const std::wstring &) {
    return wallpaperBrowseResult;
}
static std::vector<HWND> WallpaperMarkers() {
    std::vector<HWND> windows;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t cls[128] = {};
            GetClassNameW(window, cls, 128);
            if (!wcscmp(cls, L"Crli.TrayGroups.MonitorIdentify"))
                ((std::vector<HWND> *)data)->push_back(window);
            return TRUE;
        },
        (LPARAM)&windows);
    return windows;
}
static bool WallpaperPageButtons(std::wstring &report) {
    bool ok = true;
    auto check = [&](bool condition, const wchar_t *name) {
        report += (condition ? L"PASS: " : L"FAIL: ") + std::wstring(name) + L"\n";
        ok &= condition;
    };
    for (HWND current : settingsPages)
        PropSheet_UnChanged(settingsWindow, current);
    HWND page = settingsPages[4];
    SelectSettingsPage(4);
    SendMessageW(GetDlgItem(page, IDC_WALL_REFRESH), BM_CLICK, 0, 0);
    check(!IsWindowEnabled(GetDlgItem(settingsWindow, IDC_APPLY)),
          L"opening and refreshing wallpaper settings does not invent an unapplied edit");
    Wallpaper::Snapshot live;
    std::wstring error;
    bool read = Wallpaper::Read(live, error);
    unsigned active =
        (unsigned)std::count_if(live.monitors.begin(), live.monitors.end(),
                                [](const Wallpaper::Monitor &m) { return m.connected; });
    check(read && active > 0,
          L"wallpaper page reads current Windows device paths, identities and physical rectangles");
    SendMessageW(GetDlgItem(page, IDC_WALL_IDENTIFY), BM_CLICK, 0, 0);
    SendMessageW(GetDlgItem(page, IDC_WALL_IDENTIFY), BM_CLICK, 0, 0);
    auto markers = WallpaperMarkers();
    bool correct = markers.size() == active;
    auto coordinates = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::set<std::wstring> identified;
    for (HWND window : markers) {
        RECT rect;
        GetWindowRect(window, &rect);
        auto text = ControlText(GetWindow(window, GW_CHILD));
        correct &= std::any_of(
            live.monitors.begin(), live.monitors.end(), [&](const Wallpaper::Monitor &m) {
                bool match = m.connected && rect.left >= m.rect.left &&
                             rect.right <= m.rect.right && rect.top >= m.rect.top &&
                             rect.bottom <= m.rect.bottom &&
                             text.find(m.name) != std::wstring::npos &&
                             (m.serial.empty() || text.find(m.serial) != std::wstring::npos);
                if (match)
                    identified.insert(m.identity);
                return match;
            });
        SendMessageW(window, WM_TIMER, 1, 0);
    }
    SetThreadDpiAwarenessContext(coordinates);
    correct &= identified.size() == active;
    check(correct && WallpaperMarkers().empty(),
          L"Identify button marks each connected screen, excludes offline records and releases "
          L"timed overlays");
    std::wstring first = baseDir + L"\\wall-ui-a.bmp", second = baseDir + L"\\wall-ui-b.bmp";
    check(WallpaperTest::MakeImage(first) && WallpaperTest::MakeImage(second),
          L"wallpaper UI images are isolated test fixtures");
    {
        WallpaperTest::Backend backend;
        WallpaperTest::snapshot = WallpaperTest::Fixture(first);
        SendMessageW(GetDlgItem(page, IDC_WALL_REFRESH), BM_CLICK, 0, 0);
        check(SendDlgItemMessageW(page, IDC_WALL_MONITOR, CB_GETCOUNT, 0, 0) == 3,
              L"Refresh button lists active and explicitly disconnected monitor records");
        auto select = [&](int n) {
            SendDlgItemMessageW(page, IDC_WALL_MONITOR, CB_SETCURSEL, n, 0);
            SendMessageW(page, WM_COMMAND, MAKEWPARAM(IDC_WALL_MONITOR, CBN_SELCHANGE), 0);
        };
        select(2);
        check(!IsWindowEnabled(GetDlgItem(page, IDC_WALL_BROWSE)) &&
                  !IsWindowEnabled(GetDlgItem(page, IDC_WALL_IMAGE)),
              L"offline monitor cannot accept a wallpaper edit or open the file picker");
        select(0);
        auto defaults = baseDir + L"\\wallpapers.ini";
        WriteText(defaults, L"[ByResolution]\n640x480=wall-ui-b.bmp\n");
        SendMessageW(GetDlgItem(page, IDC_WALL_DEFAULT), BM_CLICK, 0, 0);
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == second &&
                  WallpaperTest::writes.empty(),
              L"Default Image button selects the resolution-matched portable picture without "
              L"applying it");
        SetDlgItemTextW(page, IDC_WALL_IMAGE, first.c_str());
        DeleteFileW(defaults.c_str());
        auto oldBrowse = WallpaperPage::testBrowse;
        WallpaperPage::testBrowse = WallpaperBrowse;
        wallpaperBrowseResult.clear();
        SendMessageW(GetDlgItem(page, IDC_WALL_BROWSE), BM_CLICK, 0, 0);
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == first &&
                  WallpaperTest::writes.empty(),
              L"cancelling Browse preserves the current file and performs no writes");
        wallpaperBrowseResult = second;
        SendMessageW(GetDlgItem(page, IDC_WALL_BROWSE), BM_CLICK, 0, 0);
        WallpaperPage::testBrowse = oldBrowse;
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == second &&
                  SendDlgItemMessageW(page, IDC_WALL_PREVIEW, STM_GETIMAGE, IMAGE_BITMAP, 0) != 0,
              L"Browse button updates only the selected monitor draft and displays a bounded "
              L"thumbnail");
        select(1);
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == first,
              L"selecting the other display keeps its original wallpaper");
        select(0);
        SendMessageW(GetDlgItem(page, IDC_WALL_REFRESH), BM_CLICK, 0, 0);
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == second,
              L"refresh and monitor switching preserve an unapplied edit for the same device "
              L"identity");
        SendDlgItemMessageW(page, IDC_WALL_POSITION, CB_SETCURSEL, DWPOS_SPAN, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(IDC_WALL_POSITION, CBN_SELCHANGE), 0);
        check(!WallpaperPage::Apply(page, error) && WallpaperTest::writes.empty(),
              L"independent wallpaper edits cannot silently use the global span mode");
        SendDlgItemMessageW(page, IDC_WALL_POSITION, CB_SETCURSEL, DWPOS_FIT, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(IDC_WALL_POSITION, CBN_SELCHANGE), 0);
        check(WallpaperPage::Apply(page, error) && WallpaperTest::writes.size() == 1 &&
                  WallpaperTest::writes[0].first ==
                      WallpaperTest::snapshot.monitors[0].devicePath &&
                  WallpaperTest::snapshot.monitors[1].image == first &&
                  WallpaperTest::snapshot.position == DWPOS_FIT,
              L"wallpaper Apply targets exactly the edited device and applies explicitly selected "
              L"global fit mode");
        select(1);
        SetDlgItemTextW(page, IDC_WALL_IMAGE, second.c_str());
        auto reads = WallpaperTest::reads;
        auto images = settingsImages;
        SendMessageW(GetDlgItem(settingsWindow, IDC_APPLY), BM_CLICK, 0, 0);
        check(WallpaperTest::writes.size() == 2 &&
                  WallpaperTest::writes.back().first ==
                      WallpaperTest::snapshot.monitors[1].devicePath &&
                  WallpaperTest::snapshot.monitors[0].image == second,
              L"standard property-sheet Apply commits the second monitor without rewriting the "
              L"first");
        check(WallpaperTest::reads == reads + 1 && settingsImages == images,
              L"wallpaper-only Apply reads monitor topology once and keeps the tray image list");
        select(0);
        DWORD baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int repeat = 0; repeat < 8; ++repeat) {
            select(1);
            select(0);
        }
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == baseline,
              L"repeated wallpaper selection releases thumbnail and Static bitmap copies");
        SetDlgItemTextW(page, IDC_WALL_IMAGE, L"C:\\missing-wallpaper.bmp");
        check(!WallpaperPage::Apply(page, error),
              L"missing image is rejected without dropping the draft");
        SetDlgItemTextW(page, IDC_WALL_IMAGE, second.c_str());
        WallpaperTest::snapshot.monitors[0].identity += L"replacement";
        SetDlgItemTextW(page, IDC_WALL_IMAGE, first.c_str());
        check(!WallpaperPage::Apply(page, error),
              L"hotplug replacement on an old device path is rejected before apply");
        // 丢弃上一个设备的草稿后恢复真实只读列表，后续按钮测试不触及系统壁纸。
        WallpaperPage::Refresh(page);
        select(0);
        SetDlgItemTextW(page, IDC_WALL_IMAGE, first.c_str());
        size_t written = WallpaperTest::writes.size();
        SendMessageW(settingsWindow, WM_SYSCOMMAND, SC_CLOSE, 0);
        check(!settingsWindow && WallpaperTest::writes.size() == written,
              L"closing wallpaper settings discards a pending picture without calling Windows");
        ShowSettings(4);
        page = settingsPages[4];
        check(ControlText(GetDlgItem(page, IDC_WALL_IMAGE)) == second &&
                  WallpaperTest::writes.size() == written,
              L"reopening wallpaper settings reads the applied state instead of the cancelled "
              L"picture");
        WallpaperTest::snapshot.monitors[0].identity =
            WallpaperTest::Fixture(first).monitors[0].identity;
        WallpaperTest::snapshot.monitors[0].image = first;
        WallpaperTest::snapshot.monitors[1].image = first;
        WallpaperTest::snapshot.position = DWPOS_TILE;
        MainProc(mainWindow, WM_COMMAND, ReapplyWallpaperCommand, 0);
        check(WallpaperTest::snapshot.monitors[0].image == second &&
                  WallpaperTest::snapshot.monitors[1].image == second &&
                  WallpaperTest::snapshot.position == DWPOS_FIT,
              L"tray command restores both saved wallpapers after Windows replaces them");
    }
    WallpaperPage::Refresh(page);
    SaveTestWindow(settingsWindow, baseDir + L"\\wallpaper-settings-preview.bmp");
    auto memory = NativeResources::Read();
    report += L"WallpaperPagePrivateWorkingSetBytes=" + Number(memory.privateWorking) + L"\n";
    check(memory.privateWorking < 7 * 1048576,
          L"wallpaper page with device metadata and small preview stays below 7 MiB");
    wchar_t realCapture[2] = {};
    if (GetEnvironmentVariableW(L"TRAYGROUPS_TEST_REAL_CAPTURE", realCapture, 2)) {
        auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        bool captured = CaptureTimer::Capture(CaptureTimer::settings);
        SetThreadDpiAwarenessContext(previousDpi);
        report += L"WallpaperOpenCapturePeakPrivateWorkingSetBytes=" +
                  Number(CaptureTimer::capturePeak.load()) + L"\n";
        check(captured && CaptureTimer::capturePeak.load() < 7 * 1048576,
              L"real multi-monitor capture while wallpaper preview is open stays below 7 MiB");
    }
    DeleteFileW(first.c_str());
    DeleteFileW(second.c_str());
    DeleteFileW((baseDir + L"\\wallpaper-selection.ini").c_str());
    return ok;
}
