#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include "WallpaperPage.h"
#include "TraySettingsIds.h"
#include "UiStyle.h"
#include "../common/NativeHandles.h"
#include <commdlg.h>
#include <algorithm>

namespace WallpaperPage {
#ifdef NATIVE_TEST_BUILD
std::wstring (*testBrowse)(HWND, const std::wstring &) = nullptr;
#endif
namespace {
struct Row {
    Wallpaper::Monitor monitor;
    std::wstring draft;
};
struct State {
    std::wstring directory;
    std::vector<Row> rows;
    int selected = -1;
    bool filling = false, positionEdited = false;
    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    HBITMAP preview = nullptr;
    ~State() {
        if (preview)
            DeleteObject(preview);
    }
};
State *Get(HWND page) {
    return (State *)GetWindowLongPtrW(page, DWLP_USER);
}
void Status(HWND page, const std::wstring &text) {
    SetDlgItemTextW(page, IDC_WALL_STATUS, text.c_str());
}
void Dirty(HWND page) {
    PropSheet_Changed(GetParent(page), page);
}
std::wstring Text(HWND control) {
    int count = GetWindowTextLengthW(control);
    std::wstring value(count + 1, L'\0');
    GetWindowTextW(control, value.data(), count + 1);
    value.resize(count);
    return value;
}
std::wstring DisplayName(const Wallpaper::Monitor &m, int number) {
    return std::to_wstring(number) + L" · " + Wallpaper::Label(m) +
           (m.connected ? L"" : L"（未连接）");
}
void Preview(HWND page, State &state, const std::wstring &path) {
    auto previous =
        (HBITMAP)SendDlgItemMessageW(page, IDC_WALL_PREVIEW, STM_SETIMAGE, IMAGE_BITMAP, 0);
    // Static 可能为含 alpha 的 DIB 创建副本，返回句柄也必须释放。
    if (previous && previous != state.preview)
        DeleteObject(previous);
    if (state.preview)
        DeleteObject(state.preview);
    state.preview = nullptr;
    if (path.empty())
        return;
    RECT box;
    GetClientRect(GetDlgItem(page, IDC_WALL_PREVIEW), &box);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) {
        factory->GetImage({std::max(1L, box.right), std::max(1L, box.bottom)}, SIIGBF_THUMBNAILONLY,
                          &state.preview);
        factory->Release();
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (state.preview)
        SendDlgItemMessageW(page, IDC_WALL_PREVIEW, STM_SETIMAGE, IMAGE_BITMAP,
                            (LPARAM)state.preview);
}
void Select(HWND page, State &state, int index) {
    state.filling = true;
    state.selected = index;
    bool valid = index >= 0 && index < (int)state.rows.size();
    bool connected = valid && state.rows[index].monitor.connected;
    EnableWindow(GetDlgItem(page, IDC_WALL_IMAGE), connected);
    EnableWindow(GetDlgItem(page, IDC_WALL_BROWSE), connected);
    EnableWindow(GetDlgItem(page, IDC_WALL_DEFAULT), connected);
    if (valid) {
        const auto &row = state.rows[index];
        const auto &m = row.monitor;
        std::wstring details =
            Wallpaper::Label(m) + L"\r\n" + std::to_wstring(m.rect.right - m.rect.left) + L" × " +
            std::to_wstring(m.rect.bottom - m.rect.top) + L" · 坐标 " +
            std::to_wstring(m.rect.left) + L", " + std::to_wstring(m.rect.top) + L" · " + m.source +
            L"\r\n标识：" + m.identity + L"\r\n" +
            (m.portableIdentity ? L"硬件序列号可区分；更换接口后仍可核对同一台屏幕。"
                                : L"序列号缺失或重复，按设备连接路径区分；更换接口需重新确认。") +
            L"\r\n设备：" + m.devicePath;
        SetDlgItemTextW(page, IDC_WALL_DETAILS, details.c_str());
        SetDlgItemTextW(page, IDC_WALL_IMAGE, row.draft.c_str());
        Preview(page, state, row.draft);
        Status(page, connected ? L"选择图片后点击应用；只更改已编辑的显示器。"
                               : L"此显示器未连接，仅显示 Windows 历史记录，不能设置。");
    } else {
        SetDlgItemTextW(page, IDC_WALL_DETAILS, L"未检测到显示器。");
        SetDlgItemTextW(page, IDC_WALL_IMAGE, L"");
        Preview(page, state, L"");
    }
    state.filling = false;
}
std::vector<Wallpaper::Change> Changes(const State &state) {
    std::vector<Wallpaper::Change> changes;
    for (const auto &row : state.rows)
        if (row.draft != row.monitor.image)
            changes.push_back({row.monitor, row.draft});
    return changes;
}
std::wstring Browse(HWND page, const std::wstring &current) {
#ifdef NATIVE_TEST_BUILD
    if (testBrowse)
        return testBrowse(page, current);
#endif
    wchar_t file[32768] = {};
    wcsncpy_s(file, current.c_str(), _TRUNCATE);
    OPENFILENAMEW open = {};
    open.lStructSize = sizeof(open);
    open.hwndOwner = GetParent(page);
    open.lpstrFile = file;
    open.nMaxFile = 32768;
    open.lpstrFilter =
        L"壁纸图片 (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0所有文件\0*.*\0\0";
    open.lpstrTitle = L"选择此显示器的壁纸";
    open.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&open) ? file : L"";
}
struct IdentifyState {
    HFONT font = nullptr;
    ~IdentifyState() {
        if (font)
            DeleteObject(font);
    }
};
LRESULT CALLBACK IdentifyProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_CREATE) {
        auto data = new IdentifyState;
        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                                   GetDpiForWindow(window));
        metrics.lfMessageFont.lfHeight = -MulDiv(18, GetDpiForWindow(window), 96);
        data->font = CreateFontIndirectW(&metrics.lfMessageFont);
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)data);
        RECT client;
        GetClientRect(window, &client);
        auto text = (const wchar_t *)((CREATESTRUCTW *)l)->lpCreateParams;
        HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_CENTER, 12, 18,
                                     client.right - 24, client.bottom - 24, window, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        SendMessageW(label, WM_SETFONT, (WPARAM)data->font, TRUE);
        SetTimer(window, 1, 3000, nullptr);
        return 0;
    }
    if (message == WM_TIMER) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        KillTimer(window, 1);
        delete (IdentifyState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    }
    return DefWindowProcW(window, message, w, l);
}
void Identify(HWND page, const State &state) {
    // GetMonitorRECT 使用物理坐标；标记创建也统一为每屏 DPI，避免混合缩放时落到错误屏幕。
    struct DpiScope {
        DPI_AWARENESS_CONTEXT previous =
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~DpiScope() {
            if (previous)
                SetThreadDpiAwarenessContext(previous);
        }
    } dpi;
    const wchar_t *name = L"Crli.TrayGroups.MonitorIdentify";
    // 连续点击复用一次识别周期，先清理本设置窗口上次创建的标记。
    std::vector<HWND> oldMarkers;
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t cls[128] = {};
            GetClassNameW(window, cls, 128);
            if (!wcscmp(cls, L"Crli.TrayGroups.MonitorIdentify"))
                ((std::vector<HWND> *)data)->push_back(window);
            return TRUE;
        },
        (LPARAM)&oldMarkers);
    for (HWND window : oldMarkers)
        if (GetWindow(window, GW_OWNER) == GetParent(page))
            DestroyWindow(window);
    WNDCLASSW cls = {};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = IdentifyProc;
    cls.lpszClassName = name;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassW(&cls);
    // 重新读取避免在已断开的屏幕坐标显示标记。
    Wallpaper::Snapshot live;
    std::wstring error;
    if (!Wallpaper::Read(live, error)) {
        Status(page, error);
        return;
    }
    int number = 0;
    for (const auto &row : state.rows) {
        ++number;
        auto current = std::find_if(
            live.monitors.begin(), live.monitors.end(), [&](const Wallpaper::Monitor &m) {
                return m.connected && m.devicePath == row.monitor.devicePath &&
                       m.identity == row.monitor.identity;
            });
        if (current == live.monitors.end())
            continue;
        std::wstring text = DisplayName(*current, number) + L"\r\n" +
                            std::to_wstring(current->rect.right - current->rect.left) + L" × " +
                            std::to_wstring(current->rect.bottom - current->rect.top);
        int width = std::min(600L, current->rect.right - current->rect.left), height = 140;
        HWND overlay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, name,
                                       L"识别显示器", WS_POPUP | WS_BORDER,
                                       current->rect.left +
                                           (current->rect.right - current->rect.left - width) / 2,
                                       current->rect.top + 40, width, height, GetParent(page),
                                       nullptr, GetModuleHandleW(nullptr), (void *)text.c_str());
        if (overlay)
            ShowWindow(overlay, SW_SHOWNOACTIVATE);
    }
    Status(page, L"已在当前连接的屏幕上显示型号和序列号，3 秒后自动消失。");
}
} // namespace
void Refresh(HWND page) {
    auto state = Get(page);
    if (!state)
        return;
    Wallpaper::Snapshot snapshot;
    std::wstring error;
    if (!Wallpaper::Read(snapshot, error)) {
        Status(page, error);
        return;
    }
    std::wstring selected;
    if (state->selected >= 0 && state->selected < (int)state->rows.size())
        selected = state->rows[state->selected].monitor.devicePath;
    auto previous = std::move(state->rows);
    state->rows.clear();
    for (auto &monitor : snapshot.monitors) {
        std::wstring draft = monitor.image;
        auto old = std::find_if(previous.begin(), previous.end(), [&](const Row &r) {
            return r.monitor.devicePath == monitor.devicePath &&
                   r.monitor.identity == monitor.identity;
        });
        if (monitor.connected && old != previous.end() && old->draft != old->monitor.image)
            draft = old->draft;
        state->rows.push_back({std::move(monitor), std::move(draft)});
    }
    bool lost = std::any_of(previous.begin(), previous.end(), [&](const Row &r) {
        return r.draft != r.monitor.image &&
               std::none_of(state->rows.begin(), state->rows.end(), [&](const Row &now) {
                   return now.monitor.connected && now.monitor.devicePath == r.monitor.devicePath &&
                          now.monitor.identity == r.monitor.identity;
               });
    });
    state->filling = true;
    HWND combo = GetDlgItem(page, IDC_WALL_MONITOR);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int index = 0;
    for (size_t n = 0; n < state->rows.size(); ++n) {
        auto label = DisplayName(state->rows[n].monitor, (int)n + 1);
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (state->rows[n].monitor.devicePath == selected)
            index = (int)n;
    }
    if (!state->positionEdited) {
        state->position = snapshot.position;
        SendDlgItemMessageW(page, IDC_WALL_POSITION, CB_SETCURSEL, (WPARAM)snapshot.position, 0);
    }
    SendMessageW(combo, CB_SETCURSEL, state->rows.empty() ? -1 : index, 0);
    Select(page, *state, state->rows.empty() ? -1 : index);
    if (lost)
        Status(page, L"部分显示器已断开，对应草稿已移除；请重新选择并确认。");
    else if (snapshot.slideshow)
        Status(page, L"Windows 正在轮换壁纸；应用静态图片会更改当前轮换状态。");
}
void Initialize(HWND page, const std::wstring &directory) {
    if (Get(page))
        return;
    auto state = new State;
    state->directory = directory;
    SetWindowLongPtrW(page, DWLP_USER, (LONG_PTR)state);
    const wchar_t *positions[] = {L"居中", L"平铺", L"拉伸",
                                  L"适应", L"填充", L"跨屏（所有显示器）"};
    for (auto text : positions)
        SendDlgItemMessageW(page, IDC_WALL_POSITION, CB_ADDSTRING, 0, (LPARAM)text);
    SendDlgItemMessageW(page, IDC_WALL_IMAGE, EM_SETLIMITTEXT, 32767, 0);
    Refresh(page);
}
bool Validate(HWND page, std::wstring &error) {
    auto state = Get(page);
    if (!state)
        return true;
    auto changes = Changes(*state);
    if (!changes.empty() && state->position == DWPOS_SPAN) {
        error = L"跨屏模式会把图片铺到所有显示器，独立壁纸请选择填充、适应等模式。";
        return false;
    }
    if (changes.empty() && !state->positionEdited)
        return true;
    if (state->position < DWPOS_CENTER || state->position > DWPOS_SPAN) {
        error = L"请选择有效的壁纸显示方式。";
        return false;
    }
    return Wallpaper::Validate(changes, error);
}
bool Apply(HWND page, std::wstring &error) {
    auto state = Get(page);
    if (!state)
        return true;
    auto changes = Changes(*state);
    if (!Wallpaper::Apply(changes, state->position, state->positionEdited, error))
        return false;
    for (auto &row : state->rows)
        row.monitor.image = row.draft;
    state->positionEdited = false;
    return true;
}
INT_PTR Handle(HWND page, UINT message, WPARAM w, LPARAM) {
    auto state = Get(page);
    if (!state)
        return FALSE;
    if (message == WM_DESTROY) {
        Preview(page, *state, L"");
        SetWindowLongPtrW(page, DWLP_USER, 0);
        delete state;
        return TRUE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    int id = LOWORD(w), event = HIWORD(w);
    if (id == IDC_WALL_IMAGE && event == EN_KILLFOCUS && !state->filling && state->selected >= 0) {
        Preview(page, *state, state->rows[state->selected].draft);
        return TRUE;
    }
    if (id == IDC_WALL_MONITOR && event == CBN_SELCHANGE) {
        Select(page, *state, (int)SendDlgItemMessageW(page, id, CB_GETCURSEL, 0, 0));
        return TRUE;
    }
    if (id == IDC_WALL_REFRESH) {
        Refresh(page);
        return TRUE;
    }
    if (id == IDC_WALL_IDENTIFY) {
        Identify(page, *state);
        return TRUE;
    }
    if (id == IDC_WALL_BROWSE && state->selected >= 0) {
        auto path = Browse(page, state->rows[state->selected].draft);
        if (!path.empty()) {
            SetDlgItemTextW(page, IDC_WALL_IMAGE, path.c_str());
            Preview(page, *state, path);
        }
        return TRUE;
    }
    if (id == IDC_WALL_DEFAULT && state->selected >= 0 &&
        state->rows[state->selected].monitor.connected) {
        const auto &monitor = state->rows[state->selected].monitor;
        auto path =
            Wallpaper::DefaultImage(state->directory, monitor.rect.right - monitor.rect.left,
                                    monitor.rect.bottom - monitor.rect.top);
        if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Status(page, L"该分辨率的默认图片未配置或不存在，请检查程序旁 wallpapers.ini。");
        } else {
            SetDlgItemTextW(page, IDC_WALL_IMAGE, path.c_str());
            Preview(page, *state, path);
            Status(page, L"已按所选屏幕分辨率选中默认图片，点击应用后生效。");
        }
        return TRUE;
    }
    if (id == IDC_WALL_IMAGE && event == EN_CHANGE && !state->filling && state->selected >= 0) {
        state->rows[state->selected].draft = Text(GetDlgItem(page, id));
        Dirty(page);
        Status(page, L"图片修改尚未应用；切换显示器会保留草稿，取消或 X 放弃。");
        return TRUE;
    }
    if (id == IDC_WALL_POSITION && event == CBN_SELCHANGE && !state->filling) {
        state->position =
            (DESKTOP_WALLPAPER_POSITION)SendDlgItemMessageW(page, id, CB_GETCURSEL, 0, 0);
        state->positionEdited = true;
        Dirty(page);
        return TRUE;
    }
    return FALSE;
}
} // namespace WallpaperPage
