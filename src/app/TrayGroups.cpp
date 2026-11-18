#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include "../common/NativeResources.h"
#include "../common/TraySupport.h"
#include "../config/TrayConfig.h"
#include "../tray/TrayIdentity.h"
#include "../tray/TrayGuid.h"
#include "AppLifecycle.h"
using namespace TraySupport;
using TrayConfig::LabelSize;
using TrayIdentity::GuidKey;
using TrayIdentity::IconKey;
using TrayIdentity::RulePath;
using TrayIdentity::SameRuntime;
using TrayIdentity::StableClass;
#include "../common/DebugLog.h"
#include "AppVersion.h"
#include "../ui/TraySettingsIds.h"
#include "../capture/CaptureTimer.h"
#ifdef NATIVE_TEST_BUILD
// Isolated tests can inject a fake encoder; production links a separate module.
#define SCREEN_CAPTURE_EMBEDDED
#include "../capture/ScreenCaptureTimer.cpp"
#endif

// 持久设置、运行时匹配及退出状态分别归属自己的对象。
static TrayConfig::Settings settingsConfig;
static TrayIdentity::MatchState matchState;
static TrayIdentity::Matcher matcher(settingsConfig, matchState);
static AppLifecycle lifecycle;

static const wchar_t *AppClass = L"Crli.TrayGroups.Native";
static const wchar_t *PopupClass = L"Crli.TrayGroups.Popup";
static const UINT TrayCallback = WM_APP + 1;
static const UINT StopApp = WM_APP + 3;
static const UINT RefreshApp = WM_APP + 4;
static const UINT RestoreAllApp = WM_APP + 5;
static const int ResourceTimer = 2, RefreshTimer = 3, SingleTimer = 4, ReFoldTimer = 5,
                 RestoreTimer = 6;
static HWND mainWindow = nullptr, popupWindow = nullptr, settingsWindow = nullptr,
            resourceWindow = nullptr;
static HWND listWindow = nullptr, groupEdit = nullptr, foldCheck = nullptr, resourceText = nullptr;
static HWND scopeEdit = nullptr, matchEdit = nullptr;
static NativeHandles::Icon appIcon;
static std::vector<std::wstring> popupGroups;
static HIMAGELIST settingsImages = nullptr;
static bool groupEditing = false, fillingSettings = false;
static HFONT appFont = nullptr;
static std::wstring baseDir, configPath, recoveryPath;
static bool refreshing = false;
static UINT taskbarCreated;
static int pendingClick = -1, reFoldAttempts = 0;
static unsigned notificationCount = 0;
static unsigned forwardingFailures = 0;
static std::map<UINT, std::wstring> notificationGroups;
static std::map<UINT, NativeHandles::Icon> notificationIcons;
static std::wstring latestError;
#ifdef NATIVE_TEST_BUILD
static bool testingOwnIcons = false;
#endif

static std::vector<TrayItem> items;
static std::vector<Hidden> hidden;
static std::vector<int> popupItems;
static std::vector<TrayItem> settingsItems;

static void LogError(const std::wstring &s) {
    latestError = s;
    DebugLog::Write(L"ERROR " + s);
    OutputDebugStringW((L"TrayGroups: " + s + L"\n").c_str());
}

// 主程序仅负责将配置变化同步到日志及窗口，不承担 INI 解析。
static void LoadSettings() {
    settingsConfig.Load(configPath);
    DebugLog::enabled = settingsConfig.debugLogging;
}
static bool SaveSettings() {
    settingsConfig.debugLogging = DebugLog::enabled.load();
    return settingsConfig.Save(configPath);
}
static void RememberSettingsPosition(HWND h) {
    if (IsIconic(h))
        return;
    RECT r;
    if (GetWindowRect(h, &r) && !settingsConfig.SaveWindowPosition(configPath, r.left, r.top))
        LogError(L"无法保存设置窗口位置");
}

static std::wstring ClassOf(HWND w) {
    wchar_t c[128] = {};
    GetClassNameW(w, c, 128);
    return c;
}

static std::wstring OwnerClass(HWND owner, DWORD pid) {
    std::wstring cls = ClassOf(owner);
    if (Lower(cls).find(L"atl:") == 0) {
        ULONG_PTR address = wcstoull(cls.c_str() + 4, nullptr, 16);
        NativeHandles::Handle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
        if (process) {
            wchar_t file[32768];
            DWORD n = GetMappedFileNameW(process, (void *)address, file, 32768);
            if (n) {
                std::wstring path(file, n);
                size_t slash = path.find_last_of(L"\\/");
                return L"atl@" + Lower(path.substr(slash == std::wstring::npos ? 0 : slash + 1));
            }
        }
        return L"atl";
    }
    return cls;
}
static bool Send(HWND h, UINT msg, WPARAM w, LPARAM l, DWORD_PTR &result) {
    return SendMessageTimeoutW(h, msg, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &result) != 0;
}
template <class T> static bool ReadMemory(HANDLE p, ULONG_PTR a, T &v) {
    SIZE_T n = 0;
    return ReadProcessMemory(p, (void *)a, &v, sizeof(v), &n) && n == sizeof(v);
}
struct RemoteTrayData {
    uint64_t owner;
    uint32_t id, callback, reserved, version;
    uint64_t icon;
};
static_assert(sizeof(RemoteTrayData) == 32, "Unexpected Explorer tray layout");
static uint64_t ProcessStart(HANDLE process) {
    FILETIME created, exit, kernel, user;
    if (!GetProcessTimes(process, &created, &exit, &kernel, &user))
        return 0;
    return ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
}

static void FreeItems(std::vector<TrayItem> &list) {
    list.clear();
}
static bool RecoveryMatches(const Hidden &record, const TrayItem &item);
static void ResolveTrayGuids(std::vector<TrayItem> &list) {
    TrayGuid::Resolve(list);
    // NIS_HIDDEN 图标不在新的 COM 快照中；只从本程序落盘的同一运行时记录补回地址。
    for (auto &item : list)
        if (!item.addressKnown && (item.state & TBSTATE_HIDDEN))
            for (const auto &record : hidden)
                if (RecoveryMatches(record, item)) {
                    item.usesGuid = record.usesGuid;
                    item.guid = record.guid;
                    item.addressKnown = true;
                    break;
                }
}
#include "../tray/TrayEnumeration.h"

static bool CommandMatches(const TrayItem &item);
static bool NativeVisibility(const TrayItem &item, bool hide) {
    auto current = ReadTrayLocations();
    bool valid = false, usesGuid = false;
    GUID guid = {};
    for (auto &live : current) {
        // 调用者刚取得的地址也可用于恢复，适用于未经过分组的独立显隐操作。
        if (!live.addressKnown && item.addressKnown && (live.state & TBSTATE_HIDDEN)) {
            live.guid = item.guid;
            live.usesGuid = item.usesGuid;
            live.addressKnown = true;
        }
        if (SameRuntime(item, live)) {
            valid = live.addressKnown;
            usesGuid = live.usesGuid;
            guid = live.guid;
            break;
        }
    }
    FreeItems(current);
    if (!valid)
        return false;
    NOTIFYICONDATAW icon = {};
    icon.cbSize = sizeof(icon);
    icon.hWnd = item.owner;
    icon.uID = item.id;
    icon.uFlags = NIF_STATE;
    if (usesGuid) {
        icon.uFlags |= NIF_GUID;
        icon.guidItem = guid;
    }
    icon.dwStateMask = NIS_HIDDEN;
    icon.dwState = hide ? NIS_HIDDEN : 0;
    return Shell_NotifyIconW(NIM_MODIFY, &icon) != FALSE;
}
static bool VisibleAtRuntime(const RuntimeIdentity &runtime) {
    auto current = ReadTrayLocations();
    bool visible = false;
    for (const auto &item : current)
        if (item.runtime == runtime && !(item.state & TBSTATE_HIDDEN))
            visible = true;
    FreeItems(current);
    return visible;
}
#include "../tray/TrayRecovery.h"

static bool OwnsHidden(const TrayItem &i) {
    return std::any_of(hidden.begin(), hidden.end(),
                       [&](const Hidden &h) { return RecoveryMatches(h, i); });
}
static bool CommandMatches(const TrayItem &item) {
    DWORD pid = 0;
    GetWindowThreadProcessId(item.toolbar, &pid);
    NativeHandles::Handle process(OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid));
    if (!process)
        return false;
    NativeHandles::RemoteBuffer buffer(process, 4096);
    void *memory = buffer.Get();
    DWORD_PTR index = 0, read = 0;
    TBBUTTON button = {};
    RemoteTrayData data = {};
    bool ok = memory && Send(item.toolbar, TB_COMMANDTOINDEX, item.command, 0, index) &&
              index != (DWORD_PTR)-1 &&
              Send(item.toolbar, TB_GETBUTTON, index, (LPARAM)memory, read) && read &&
              ReadMemory(process, (ULONG_PTR)memory, button) && button.idCommand == item.command &&
              button.dwData && ReadMemory(process, button.dwData, data) &&
              (HWND)data.owner == item.owner && data.id == item.id &&
              data.callback == item.callback;
    if (ok) {
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(item.owner, &ownerPid);
        NativeHandles::Handle ownerProcess(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ownerPid));
        uint64_t started = ownerProcess ? ProcessStart(ownerProcess) : 0;
        RuntimeIdentity live = {ownerPid, started, item.owner, item.id};
        ok = item.runtime.Valid() && live == item.runtime;
    }
    return ok;
}

static void RestoreUnassigned() {
    if (!ImportRecovery())
        return;
    std::vector<Hidden> keep;
    for (const auto &record : hidden) {
        if (record.originalHidden)
            continue;
        auto current = std::find_if(items.begin(), items.end(), [&](const TrayItem &item) {
            return RecoveryMatches(record, item);
        });
        if (current == items.end()) {
            if (RuntimeAlive(record.runtime))
                keep.push_back(record);
            continue;
        }
        if (!current->group.empty() && !current->pending) {
            keep.push_back(record);
            continue;
        }
        if (VisibleAtRuntime(record.runtime)) {
            current->state &= ~TBSTATE_HIDDEN;
            continue;
        }
        if (!NativeVisibility(*current, false) || !VisibleAtRuntime(record.runtime))
            keep.push_back(record);
        else
            current->state &= ~TBSTATE_HIDDEN;
    }
    hidden = keep;
    WriteRecovery();
}
static void FoldAssigned() {
    // 退出/销毁阶段绝不重新折叠；写入恢复日志和看护握手必须发生在实际隐藏之前。
    if (!settingsConfig.foldIcons || !lifecycle.CanFold() || !ImportRecovery())
        return;
    // Journal exact identities before asking Explorer to hide them.
    std::vector<size_t> todo;
    const auto previous = hidden;
    for (size_t n = 0; n < items.size(); n++) {
        auto &item = items[n];
        if (item.group.empty() || item.pending || !item.addressKnown ||
            (item.state & TBSTATE_HIDDEN) || !CommandMatches(item))
            continue;
        // Applications can reveal their icon again after a click or state update.
        // Reapply the fold even when this runtime already has a recovery record.
        if (!OwnsHidden(item)) {
            // 同一窗口/编号可以重新注册另一个 GUID；当前已确认可见时，不能沿用旧地址。
            hidden.erase(std::remove_if(
                             hidden.begin(), hidden.end(),
                             [&](const Hidden &record) { return record.runtime == item.runtime; }),
                         hidden.end());
            Hidden record = {item.toolbar, item.owner, item.id, item.command, item.runtime};
            record.path = item.path;
            record.ownerClass = item.ownerClass;
            record.callback = item.callback;
            record.guid = item.guid;
            record.usesGuid = item.usesGuid;
            hidden.push_back(std::move(record));
        }
        todo.push_back(n);
    }
    if (!todo.empty() && !WriteRecovery()) {
        hidden = previous;
        return;
    }
    if (!todo.empty() && !EnsureRecoveryGuard())
        return;
    std::vector<RuntimeIdentity> rejected;
    for (size_t n : todo) {
        auto &item = items[n];
        if (!NativeVisibility(item, true)) {
            rejected.push_back(item.runtime);
            LogError(L"此图标不支持折叠，已保留原图标");
        }
    }
    // Display only icons Explorer confirms hidden; a successful request alone
    // is not proof that the original has disappeared.
    if (!todo.empty()) {
        auto live = ReadTrayLocations();
        for (auto &item : items) {
            bool found = false, visible = false;
            for (const auto &current : live)
                if (current.runtime == item.runtime) {
                    found = true;
                    if (!(current.state & TBSTATE_HIDDEN))
                        visible = true;
                }
            if (found) {
                if (visible)
                    item.state &= ~TBSTATE_HIDDEN;
                else
                    item.state |= TBSTATE_HIDDEN;
            }
        }
        hidden.erase(std::remove_if(hidden.begin(), hidden.end(),
                                    [&](const Hidden &record) {
                                        return std::find(rejected.begin(), rejected.end(),
                                                         record.runtime) != rejected.end() &&
                                               !RecoveryStillHidden(record, live);
                                    }),
                     hidden.end());
        FreeItems(live);
    }
    // Keep all successful requests journaled, including delayed Explorer updates.
    WriteRecovery();
}

static HICON MakeAppIcon() {
    HICON embedded =
        (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    if (embedded)
        return embedded;
    HDC dc = GetDC(nullptr), mem = CreateCompatibleDC(dc);
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(header);
    header.bV5Width = 32;
    header.bV5Height = -32;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    void *pixels = nullptr;
    HBITMAP color =
        CreateDIBSection(dc, (BITMAPINFO *)&header, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HBITMAP old = (HBITMAP)SelectObject(mem, color);
    DWORD *p = (DWORD *)pixels;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            p[y * 32 + x] = ((x - 16) * (x - 16) + (y - 16) * (y - 16) < 225) ? 0xff2378bb : 0;
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    HFONT f = CreateFontW(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(mem, f);
    RECT r = {0, 1, 32, 32};
    std::wstring text = L"T";
    DrawTextW(mem, text.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, of);
    DeleteObject(f);
    for (int i = 0; i < 1024; i++)
        if (p[i] & 0x00ffffff)
            p[i] |= 0xff000000;
    SelectObject(mem, old);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(mem);
    ReleaseDC(nullptr, dc);
    return icon;
}
static void DeleteNotifications() {
    for (auto &p : notificationIcons) {
        NOTIFYICONDATAW n = {};
        n.cbSize = sizeof(n);
        n.hWnd = mainWindow;
        n.uID = p.first;
        Shell_NotifyIconW(NIM_DELETE, &n);
    }
    notificationIcons.clear();
    notificationGroups.clear();
    notificationCount = 0;
}
static void AddNotification(UINT id, const std::wstring &group, NativeHandles::Icon icon) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof(n);
    n.hWnd = mainWindow;
    n.uID = id;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = TrayCallback;
    n.hIcon = icon;
    std::wstring tip =
        id == 1 ? L"TrayGroups " TRAYGROUPS_VERSION_W L" · 左键分组和截图，右键菜单" : group;
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    BOOL registered = Shell_NotifyIconW(NIM_ADD, &n);
    if (!registered) {
        DWORD error = GetLastError();
        registered = Shell_NotifyIconW(NIM_MODIFY, &n);
        DebugLog::Write(L"TRAY add failed win32=" + Number(error) + L" modify-existing=" +
                        Number(registered));
    }
    if (registered) {
        n.uVersion = NOTIFYICON_VERSION_4;
        BOOL versioned = Shell_NotifyIconW(NIM_SETVERSION, &n);
        DebugLog::Write(L"TRAY registered id=" + Number(id) + L" version4=" + Number(versioned));
        notificationIcons[id] = std::move(icon);
        notificationGroups[id] = group;
        notificationCount++;
    } else {
        LogError(L"添加 T 图标失败，Win32=" + Number(GetLastError()));
    }
}

static std::set<std::wstring> GroupNames() {
    std::set<std::wstring> groups;
    for (const auto &i : settingsConfig.assignments)
        if (!i.second.empty())
            groups.insert(i.second);
    for (const auto &i : settingsConfig.pathAssignments)
        if (!i.second.empty())
            groups.insert(i.second);
    for (const auto &i : settingsConfig.programAssignments)
        if (!i.second.empty())
            groups.insert(i.second);
    return groups;
}
static void SyncNotifications() {
    if (notificationIcons.size() == 1 && notificationIcons.count(1))
        return;
    DeleteNotifications();
    AddNotification(1, L"", appIcon ? CopyIcon(appIcon) : MakeAppIcon());
}

static void Refresh(bool restore = false, bool includeImages = false) {
    // 常驻刷新只扫描元数据。用户打开界面时再取图标副本，减少句柄和内存开销。
    if (refreshing)
        return;
    refreshing = true;
    if (restore)
        RestoreHidden();
    FreeItems(items);
    items = ReadTray(includeImages);
    SyncNotifications();
    RestoreUnassigned();
    FoldAssigned();
    refreshing = false;
}
static std::vector<std::pair<WPARAM, LPARAM>> CallbackSequence(UINT id, UINT version, int action,
                                                               POINT pos) {
    std::vector<std::pair<WPARAM, LPARAM>> result;
    auto add = [&](UINT event) {
        if (version >= 4)
            result.push_back({MAKEWPARAM((short)pos.x, (short)pos.y), MAKELPARAM(event, (WORD)id)});
        else
            result.push_back({id, event});
    };
    add(WM_MOUSEMOVE);
    if (action == 2) {
        add(WM_RBUTTONDOWN);
        add(version >= 4 ? WM_CONTEXTMENU : WM_RBUTTONUP);
    } else if (action == 1) {
        add(WM_LBUTTONDOWN);
        add(version >= 4 ? NIN_SELECT : WM_LBUTTONUP);
        add(WM_LBUTTONDBLCLK);
        add(version >= 4 ? NIN_SELECT : WM_LBUTTONUP);
    } else {
        add(WM_LBUTTONDOWN);
        add(version >= 4 ? NIN_SELECT : WM_LBUTTONUP);
    }
    return result;
}
static bool Dispatch(const TrayItem &item, int action, POINT pos) {
    if (!IsWindow(item.owner)) {
        LogError(L"程序图标已重建，请刷新后重试");
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(item.owner, &pid);
    if (item.runtime.Valid()) {
        NativeHandles::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        RuntimeIdentity live = {pid, process ? ProcessStart(process) : 0, item.owner, item.id};
        if (!(live == item.runtime)) {
            LogError(L"图标已重建，请刷新后重试");
            return false;
        }
    }
    AllowSetForegroundWindow(pid);
    bool ok = true;
    for (auto m : CallbackSequence(item.id, item.version, action, pos))
        if (!PostMessageW(item.owner, item.callback, m.first, m.second))
            ok = false;
    if (!ok) {
        forwardingFailures++;
        LogError(L"操作转发被拒绝（可能是程序权限不同），可改用原托盘图标");
    }
    return ok;
}
static bool Forward(TrayItem identity, int action, POINT pos) {
    // 点击前重找同一生命周期的原图标，沿用它最新的回调和协议版本。
    // Resolve latest callback/version after an application changes its icon.
    auto current = ReadTray(false);
    auto match = std::find_if(current.begin(), current.end(),
                              [&](const TrayItem &t) { return SameRuntime(t, identity); });
    bool ok = false;
    if (match != current.end())
        ok = Dispatch(*match, action, pos);
    else
        LogError(L"原图标已退出，请刷新");
    FreeItems(current);
    return ok;
}
static void ClosePopup() {
    if (popupWindow)
        DestroyWindow(popupWindow);
}
static void ShowSettings(int page = 0);
static void ShowResources();
static std::wstring ResourceReport(bool json = false) {
    auto actual = NativeResources::Read();
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    unsigned threads = NativeResources::ThreadCount();
    wchar_t report[2304] = {};
    double cpu = actual.cpuSeconds;
    static double previousCpu = 0;
    static ULONGLONG previousWall = 0;
    ULONGLONG wall = GetTickCount64();
    SYSTEM_INFO sys;
    GetSystemInfo(&sys);
    double percent = previousWall && wall > previousWall
                         ? (cpu - previousCpu) * 100000.0 / (wall - previousWall) /
                               std::max(1ul, sys.dwNumberOfProcessors)
                         : 0;
    previousCpu = cpu;
    previousWall = wall;
    if (json)
        swprintf_s(report,
                   L"{\"pid\":%lu,\"taskManagerPrivateWorkingSetBytes\":%llu,\"workingSetBytes\":%"
                   L"llu,\"privateBytes\":%llu,\"cpuSeconds\":%.3f,\"cpuPercent\":%.3f,\"threads\":"
                   L"%u,\"handles\":%lu,\"detectedIcons\":%llu,\"groupIcons\":%u,"
                   L"\"hiddenOriginals\":%llu,\"forwardingFailures\":%u}",
                   GetCurrentProcessId(), (unsigned long long)actual.privateWorking,
                   (unsigned long long)actual.working, (unsigned long long)actual.privateCommit,
                   cpu, percent, threads, handles, (unsigned long long)items.size(),
                   (unsigned)GroupNames().size(), (unsigned long long)hidden.size(),
                   forwardingFailures);
    else
        swprintf_s(
            report,
            L"TrayGroups.exe · 原生 Win32\r\n\r\n任务管理器口径（专用工作集）：%.2f "
            L"MiB\r\n专用提交内存：%.2f MiB\r\n总工作集（含共享内存）：%.2f MiB\r\nCPU 当前：%.2f "
            L"%%    累计：%.2f 秒\r\n线程：%u（每 5 秒采样）    句柄：%lu\r\n分组数量：%u    "
            L"托盘项目：%llu\r\n已折叠原图标：%llu\r\n\r\n每个程序 < 3 "
            L"MiB：%ls\r\n不强制清空工作集。",
            actual.privateWorking / 1048576.0, actual.privateCommit / 1048576.0,
            actual.working / 1048576.0, percent, cpu, threads, handles,
            (unsigned)GroupNames().size(), (unsigned long long)items.size(),
            (unsigned long long)hidden.size(),
            actual.privateWorking < 3 * 1048576 ? L"当前达到" : L"当前未达到");
    std::wstring result = report;
    if (!json && !latestError.empty())
        result += L"\r\n最近状态：" + latestError;
    return result;
}

#include "../ui/NativeTrayPanel.h"

static HWND Control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y,
                    int width, int height, int id) {
    HWND c =
        CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, width,
                        height, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)appFont, TRUE);
    return c;
}

#include "../ui/TraySettings.h"

static LRESULT CALLBACK ResourceProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_TIMER) {
        SetWindowTextW(resourceText, ResourceReport().c_str());
        return 0;
    }
    if (msg == WM_DESTROY) {
        KillTimer(h, ResourceTimer);
        resourceWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
static void ShowResources() {
    if (resourceWindow) {
        SetForegroundWindow(resourceWindow);
        return;
    }
    resourceWindow =
        CreateWindowExW(WS_EX_APPWINDOW, L"Crli.TrayGroups.Resources", L"TrayGroups · 资源占用",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540,
                        420, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    resourceText =
        Control(resourceWindow, L"STATIC", ResourceReport().c_str(), 0, 18, 16, 495, 350, 0);
    SetTimer(resourceWindow, ResourceTimer, 1000, nullptr);
    ShowWindow(resourceWindow, SW_SHOW);
    SetForegroundWindow(resourceWindow);
}
static void ContinueRestoring() {
    // 恢复可重试，失败记录不能清空；退出还要同时等截图工作线程完成。
    RestoreHidden();
    bool pending = !recoveryHealthy || !hidden.empty();
    std::wstring status = pending ? L"仍有 " + Number(hidden.size()) + L" 个图标待恢复，记录已保留"
                                  : L"本程序隐藏的图标已全部恢复";
    if (!recoveryHealthy)
        status = L"恢复记录无法处理，文件已保留，请查看日志";
    DebugLog::Write(L"RESTORE status=" + status);
    if (settingsWindow)
        SetWindowTextW(settingsWindow, (L"TrayGroups 设置 · " + status).c_str());
    if (pending && recoveryHealthy && ++lifecycle.restoreAttempts < 10 && mainWindow)
        SetTimer(mainWindow, RestoreTimer, 200, nullptr);
    else {
        KillTimer(mainWindow, RestoreTimer);
        if (pending)
            LogError(status);
        if (lifecycle.IsStopping() && !pending && !CaptureTimer::IsBusy())
            DestroyWindow(mainWindow);
    }
}
static void RestoreAllOwnedIcons() {
    ClosePopup();
    KillTimer(mainWindow, ReFoldTimer);
    reFoldAttempts = 0;
    lifecycle.restoreAttempts = 0;
    settingsConfig.foldIcons = false;
    if (!TrayConfig::PauseFolding(configPath))
        LogError(L"折叠已暂停，但暂停设置未能保存");
    ContinueRestoring();
    if (settingsWindow) {
        SendMessageW(foldCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        FillSettings();
    }
}
static void RootMenu() {
    DebugLog::Write(L"MENU open");
    ClosePopup();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"设置分组…");
    AppendMenuW(menu, MF_STRING, 2, L"资源占用");
    CaptureTimer::AddMenu(menu);
    AppendMenuW(menu, MF_STRING, 4, L"重新加载配置并刷新");
    AppendMenuW(menu, MF_STRING, 7, L"编辑 groups.ini 配置文件");
    AppendMenuW(menu, MF_STRING, 8, L"打开本地调试日志");
    AppendMenuW(menu, MF_STRING | (DebugLog::enabled.load() ? MF_CHECKED : 0), 9,
                L"记录本地调试日志");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 5, L"恢复本程序隐藏的全部图标");
    AppendMenuW(menu, MF_STRING, 6, L"退出（恢复原图标）");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(mainWindow);
    int id =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, mainWindow, nullptr);
    DestroyMenu(menu);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    DebugLog::Write(L"MENU command=" + Number(id));
    if (id == 8) {
        ShellExecuteW(mainWindow, L"open", L"notepad.exe", (L"\"" + DebugLog::path + L"\"").c_str(),
                      baseDir.c_str(), SW_SHOWNORMAL);
        return;
    }
    if (id == 9) {
        bool previous = DebugLog::enabled.load();
        DebugLog::Write(L"DEBUG logging disabled");
        DebugLog::enabled = !previous;
        if (!SaveSettings()) {
            DebugLog::enabled = previous;
            MessageBoxW(mainWindow, L"无法保存日志开关", L"TrayGroups", MB_ICONERROR);
        } else
            DebugLog::Write(L"DEBUG logging enabled");
        return;
    }
    if (id == CaptureTimer::SettingsCommand) {
        ShowSettings(2);
        return;
    }
    if (CaptureTimer::HandleMenu(id))
        return;
    if (id == 1)
        ShowSettings();
    else if (id == 2)
        ShowResources();
    else if (id == 4) {
        RestoreHidden();
        LoadSettings();
        Refresh();
        if (settingsWindow)
            FillSettings();
    } else if (id == 5)
        RestoreAllOwnedIcons();
    else if (id == 6)
        PostMessageW(mainWindow, StopApp, 0, 0);
    else if (id == 7) {
        std::wstring cmd = L"notepad.exe \"" + configPath + L"\"";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                           &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
}
static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == TrayCallback) {
        bool legacy = HIWORD(l) == 0;
        UINT id = legacy ? (UINT)w : HIWORD(l), event = LOWORD(l);
        if (event != WM_MOUSEMOVE)
            DebugLog::Write(L"TRAY callback id=" + Number(id) + L" event=" + Number(event) +
                            L" known=" + Number(notificationGroups.count(id)));
        // The single T entry always uses ID 1. Explorer can retain it while our
        // registration cache is being rebuilt; cache loss must not disable input.
        if (id != 1)
            return 0;
        if (event == WM_CONTEXTMENU || (legacy && event == WM_RBUTTONUP))
            PostMessageW(h, WM_APP + 8, 2, 0);
        else if (event == NIN_SELECT || event == NIN_KEYSELECT || (legacy && event == WM_LBUTTONUP))
            PostMessageW(h, WM_APP + 8, 1, id);
        return 0;
    }
    if (msg == WM_APP + 8) {
        if (w == 2)
            RootMenu();
        else if (l == 1) {
            if (popupWindow)
                ClosePopup();
            else {
                ShowGroups();
            }
        }
        return 0;
    }
    if (msg == WM_TIMER && w == RefreshTimer) {
        if (!popupWindow && !settingsWindow)
            Refresh();
        return 0;
    }
    if (msg == WM_TIMER && w == CaptureTimer::TimerId()) {
        CaptureTimer::OnTimer();
        return 0;
    }
    if (msg == WM_TIMER && w == RestoreTimer) {
        ContinueRestoring();
        return 0;
    }
    if (msg == WM_TIMER && w == ReFoldTimer) {
        if (!popupWindow && !settingsWindow)
            Refresh();
        if (--reFoldAttempts <= 0)
            KillTimer(h, ReFoldTimer);
        return 0;
    }
    if (msg == StopApp) {
        // 先切换状态并停刷新，再恢复图标，避免等待截图时又被计时器隐藏。
        DebugLog::Write(L"STOP requested busy=" + Number(CaptureTimer::IsBusy()));
        lifecycle.BeginExit();
        CaptureTimer::PrepareStop();
        KillTimer(h, RefreshTimer);
        KillTimer(h, ReFoldTimer);
        reFoldAttempts = 0;
        ClosePopup();
        ContinueRestoring();
        return 0;
    }
    if (msg == WM_CLOSE) {
        PostMessageW(h, StopApp, 0, 0);
        return 0;
    }
    if (msg == RestoreAllApp) {
        RestoreAllOwnedIcons();
        return 0;
    }
    if (msg == RefreshApp) {
        RestoreHidden();
        LoadSettings();
        Refresh();
        if (settingsWindow)
            FillSettings();
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(w) == 1) {
        ShowSettings();
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(w) == IDC_RESTORE_ALL) {
        RestoreAllOwnedIcons();
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(w) == CaptureTimer::SettingsCommand) {
        ShowSettings(2);
        return 0;
    }
    if (msg == WM_COMMAND && CaptureTimer::HandleMenu(LOWORD(w)))
        return 0;
    if (msg == WM_WTSSESSION_CHANGE) {
        CaptureTimer::OnSessionChange(w);
        return 0;
    }
    if (msg == WM_QUERYENDSESSION) {
        if (CaptureTimer::IsBusy())
            return FALSE;
        lifecycle.BeginExit();
        RestoreHidden();
        return recoveryHealthy && hidden.empty();
    }
    if (msg == WM_ENDSESSION) {
        if (!w) {
            lifecycle.CancelExit();
            SetTimer(h, RefreshTimer, 30000, nullptr);
        }
        return 0;
    }
    if (msg == taskbarCreated) {
        notificationGroups.clear();
        notificationCount = 0;
        notificationIcons.clear();
        SetTimer(h, RefreshTimer, 30000, nullptr);
        RecoverIcons();
        Refresh();
        return 0;
    }
    if (msg == WM_DESTROY) {
        lifecycle.BeginShutdown();
        KillTimer(h, RefreshTimer);
        KillTimer(h, ReFoldTimer);
        ClosePopup();
        if (settingsWindow)
            DestroyWindow(settingsWindow);
        if (resourceWindow)
            DestroyWindow(resourceWindow);
        CaptureTimer::Shutdown();
        WTSUnRegisterSessionNotification(h);
        RestoreHidden();
        DeleteNotifications();
        FreeItems(items);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == CaptureTimer::CompletionMessage()) {
        CaptureTimer::OnCompletion(w != 0);
        if (popupWindow)
            SetDlgItemTextW(popupWindow, CaptureStatusControl, CaptureTimer::StatusText().c_str());
        if (lifecycle.IsStopping())
            PostMessageW(h, StopApp, 0, 0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

#ifndef NATIVE_TEST_BUILD
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmd, int) {
    baseDir = ExePath();
    baseDir.resize(baseDir.find_last_of(L'\\'));
    configPath = baseDir + L"\\groups.ini";
    recoveryPath = baseDir + L"\\restore-icons.ini";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring args = Trim(cmd ? cmd : L"");
    if (args.find(L"--recovery-watch ") == 0)
        return RunRecoveryWatch(args.substr(17));
    HWND existing = FindWindowW(AppClass, nullptr);
    if (args == L"--stop") {
        if (existing)
            PostMessageW(existing, StopApp, 0, 0);
        return 0;
    }
    if (args == L"--reload") {
        if (existing)
            PostMessageW(existing, RefreshApp, 0, 0);
        return 0;
    }
    if (args == L"--restore") {
        if (existing) {
            PostMessageW(existing, RestoreAllApp, 0, 0);
            return 0;
        }
        HANDLE gate = CreateMutexW(nullptr, FALSE, L"Local\\Crli.TrayGroups.Native.Instance");
        DWORD acquired = gate ? WaitForSingleObject(gate, 10000) : WAIT_FAILED;
        if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
            if (gate)
                CloseHandle(gate);
            return 1;
        }
        DebugLog::Initialize(baseDir);
        LoadSettings();
        settingsConfig.foldIcons = false;
        TrayConfig::PauseFolding(configPath);
        MigrateLegacyRecovery();
        for (unsigned attempt = 0; attempt < 15; attempt++) {
            RecoverIcons();
            if (!recoveryHealthy || hidden.empty())
                break;
            Sleep(150);
        }
        bool ok = recoveryHealthy && hidden.empty();
        ReleaseMutex(gate);
        CloseHandle(gate);
        return ok ? 0 : 1;
    }
    if (existing) {
        if (args != L"--background")
            PostMessageW(existing, WM_COMMAND, 1, 0);
        return 0;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Crli.TrayGroups.Native.Instance");
    DWORD acquired = mutex ? WaitForSingleObject(mutex, 3000) : WAIT_FAILED;
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&common);
    LOGFONTW font = {};
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        font = metrics.lfMessageFont;
    else {
        font.lfHeight = -14;
        wcscpy_s(font.lfFaceName, L"Microsoft YaHei UI");
    }
    appFont = CreateFontIndirectW(&font);
    appIcon = MakeAppIcon();
    struct Registration {
        const wchar_t *name;
        WNDPROC proc;
        UINT style;
    };
    Registration classes[] = {{AppClass, MainProc, 0},
                              {PopupClass, PopupProc, CS_DBLCLKS | CS_DROPSHADOW},
                              {L"Crli.TrayGroups.Resources", ResourceProc, 0}};
    for (auto c : classes) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = c.proc;
        wc.hInstance = instance;
        wc.lpszClassName = c.name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.style = c.style;
        wc.hIcon = appIcon;
        RegisterClassW(&wc);
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    DebugLog::Initialize(baseDir);
    LoadSettings();
    DebugLog::Write(L"START version=" TRAYGROUPS_VERSION_W L" exe=" + ExePath() + L" config=" +
                    configPath);
    if (GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        SaveSettings();
    MigrateLegacyRecovery();
    RecoverIcons();
    mainWindow = CreateWindowExW(WS_EX_TOOLWINDOW, AppClass, L"TrayGroups 托盘分组器", WS_POPUP, 0,
                                 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!mainWindow) {
        CloseHandle(mutex);
        return 1;
    }
    ShowWindow(mainWindow, SW_HIDE);
    CaptureTimer::Initialize(mainWindow, instance, baseDir);
    WTSRegisterSessionNotification(mainWindow, NOTIFY_FOR_THIS_SESSION);
    if (!hidden.empty()) {
        settingsConfig.foldIcons = false;
        EnsureRecoveryGuard();
        SetTimer(mainWindow, RestoreTimer, 200, nullptr);
    }
    Refresh();
    SetTimer(mainWindow, RefreshTimer, 30000, nullptr);
    if (args != L"--background")
        ShowSettings();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWindow && PropSheet_IsDialogMessage(settingsWindow, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (appFont)
        DeleteObject(appFont);
    appIcon = nullptr;
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

#endif
