// 截图实现：整十分钟调度、按天保存、多屏条带读取和 PNG 编码。
// 宿主通过 CaptureTimer.h 调用；只在实际截图时创建临时工作线程。
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <zlib.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include "../common/DebugLog.h"
#include "CaptureTimer.h"
#include "../common/NativeResources.h"

namespace CaptureTimer {

static const wchar_t *AppClass = L"Crli.ScreenCaptureTimer.Native";
static const UINT NotifyMessage = WM_APP + 1, WorkerDone = WM_APP + 2, StopMessage = WM_APP + 3;
static const UINT IntegratedTimer = WM_APP + 30, IntegratedDone = WM_APP + 31;
static UINT completionMessage = WorkerDone;
struct Settings {
    std::wstring directory, format = L"HH-mm";
};
static Settings settings;
static std::wstring root, configPath, lastError;
static HWND mainWindow = nullptr, settingsWindow = nullptr, resourcesWindow = nullptr,
            resourceLabel = nullptr, dirEdit = nullptr, formatEdit = nullptr;
static HFONT font = nullptr;
static std::atomic<bool> busy(false);
static std::atomic<SIZE_T> capturePeak(0);
static bool paused = false, locked = false, stopRequested = false;
static ULONGLONG nextTime = 0, previousTime = 0;
static UINT taskbarCreated;
static std::wstring completionStatus = L"尚未截图";

static std::wstring Trim(const std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    return a == std::wstring::npos ? L"" : s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}
static std::string Utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), s.size(), &r[0], n, nullptr, nullptr);
    return r;
}
static std::wstring Wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), nullptr, 0);
    std::wstring r(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), &r[0], n);
    return r;
}
static bool WriteAll(HANDLE h, const void *p, DWORD n) {
    DWORD wrote = 0;
    return WriteFile(h, p, n, &wrote, nullptr) && wrote == n;
}
static bool TextFile(const std::wstring &path, const std::wstring &text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    auto b = Utf8(text);
    bool ok = WriteAll(h, b.data(), b.size());
    CloseHandle(h);
    return ok;
}
static std::wstring ReadFileText(const std::wstring &path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L"";
    DWORD size = GetFileSize(h, nullptr);
    if (size > 1024 * 1024) {
        CloseHandle(h);
        return L"";
    }
    std::string b(size, 0);
    DWORD n = 0;
    ReadFile(h, b.empty() ? nullptr : &b[0], size, &n, nullptr);
    CloseHandle(h);
    b.resize(n);
    return Wide(b);
}
static std::wstring Json(const std::wstring &s) {
    std::wstring r = L"\"";
    for (auto c : s) {
        if (c == L'\\' || c == L'"') {
            r += L'\\';
            r += c;
        } else if (c == L'\r')
            r += L"\\r";
        else if (c == L'\n')
            r += L"\\n";
        else if (c == L'\t')
            r += L"\\t";
        else
            r += c;
    }
    return r + L"\"";
}
static std::wstring JsonValue(const std::wstring &s, const wchar_t *key) {
    size_t p = s.find(L"\"" + std::wstring(key) + L"\"");
    if (p == std::wstring::npos)
        return L"";
    p = s.find(L':', p);
    if (p == std::wstring::npos)
        return L"";
    p = s.find(L'"', p);
    if (p == std::wstring::npos)
        return L"";
    std::wstring r;
    for (++p; p < s.size(); p++) {
        wchar_t c = s[p];
        if (c == L'"')
            return r;
        if (c == L'\\' && p + 1 < s.size()) {
            c = s[++p];
            if (c == L'n')
                c = L'\n';
            else if (c == L'r')
                c = L'\r';
            else if (c == L't')
                c = L'\t';
            else if (c == L'u' && p + 4 < s.size()) {
                std::wstring hex = s.substr(p + 1, 4);
                c = (wchar_t)wcstoul(hex.c_str(), nullptr, 16);
                p += 4;
            }
        }
        r += c;
    }
    return L"";
}
static void Log(const std::wstring &message) {
    DebugLog::Write(L"CAPTURE " + message);
#ifndef SCREEN_CAPTURE_EMBEDDED
    // Preserve the legacy standalone log; the host uses one bounded debug log.
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond);
    HANDLE h = CreateFileW((root + L"\\activity.log").c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        auto s = Utf8(stamp + message + L"\r\n");
        WriteAll(h, s.data(), s.size());
        CloseHandle(h);
    }
#endif
}
static bool MakeDirectories(const std::wstring &p) {
    if (p.empty())
        return false;
    DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES)
        return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 2 && !MakeDirectories(p.substr(0, slash)))
        return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static ULONGLONG Ticks(const SYSTEMTIME &t) {
    FILETIME f;
    if (!SystemTimeToFileTime(&t, &f))
        return 0;
    ULARGE_INTEGER u;
    u.LowPart = f.dwLowDateTime;
    u.HighPart = f.dwHighDateTime;
    return u.QuadPart;
}
static SYSTEMTIME FromTicks(ULONGLONG n) {
    ULARGE_INTEGER u;
    u.QuadPart = n;
    FILETIME f = {u.LowPart, u.HighPart};
    SYSTEMTIME t = {};
    FileTimeToSystemTime(&f, &t);
    return t;
}
static ULONGLONG Now() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return Ticks(t);
}
static ULONGLONG Next(ULONGLONG ticks) {
    SYSTEMTIME t = FromTicks(ticks);
    t.wMilliseconds = 0;
    t.wSecond = 0;
    t.wMinute = t.wMinute / 10 * 10;
    return Ticks(t) + 600ULL * 10000000ULL;
}
static std::wstring FormatName(const std::wstring &format, const SYSTEMTIME &t) {
    const std::wstring f = format.empty() ? L"HH-mm" : format;
    std::wstring r;
    for (size_t i = 0; i < f.size();) {
        wchar_t c = f[i];
        size_t end = i + 1;
        while (end < f.size() && f[end] == c)
            end++;
        int n = (int)(end - i), value = -1;
        if (c == L'y')
            value = t.wYear;
        else if (c == L'M')
            value = t.wMonth;
        else if (c == L'd')
            value = t.wDay;
        else if (c == L'H')
            value = t.wHour;
        else if (c == L'h')
            value = t.wHour % 12 ? t.wHour % 12 : 12;
        else if (c == L'm')
            value = t.wMinute;
        else if (c == L's')
            value = t.wSecond;
        else if (c == L'f')
            value = t.wMilliseconds;
        if (value >= 0) {
            wchar_t b[32];
            int width = std::min(6, n);
            if (c == L'y' && n == 2)
                value %= 100;
            if (c == L'f') {
                width = std::min(3, n);
                if (n == 1)
                    value /= 100;
                else if (n == 2)
                    value /= 10;
            }
            swprintf_s(b, L"%0*d", width, value);
            r += b;
        } else if (c == L'\'' || c == L'"') {
            size_t close = f.find(c, i + 1);
            if (close == std::wstring::npos)
                return L"";
            r += f.substr(i + 1, close - i - 1);
            end = close + 1;
        } else
            r += f.substr(i, end - i);
        i = end;
    }
    if (r.empty() || r.size() > 150 || r.back() == L'.' || r.back() == L' ')
        return L"";
    if (r.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos)
        return L"";
    for (wchar_t c : r)
        if (c < 32)
            return L"";
    std::wstring stem = r.substr(0, r.find(L'.'));
    if (!stem.empty())
        CharUpperBuffW(&stem[0], stem.size());
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        ((stem.size() == 4) && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") &&
         stem[3] >= L'1' && stem[3] <= L'9'))
        return L"";
    return r;
}
static void Load() {
    std::wstring text = ReadFileText(configPath);
    settings.directory = JsonValue(text, L"OutputDirectory");
    settings.format = JsonValue(text, L"FileNameFormat");
    if (settings.directory.empty())
        settings.directory = root + L"\\Captures";
    if (settings.format.empty())
        settings.format = L"HH-mm";
}
static bool Save(const Settings &s) {
    std::wstring tmp = configPath + L".tmp";
    return TextFile(tmp, L"{\"OutputDirectory\":" + Json(s.directory) + L",\"FileNameFormat\":" +
                             Json(s.format) + L"}\n") &&
           MoveFileExW(tmp.c_str(), configPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static bool DesktopAvailable() {
    HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!d)
        return false;
    wchar_t name[128];
    DWORD n;
    bool ok = GetUserObjectInformationW(d, UOI_NAME, name, sizeof(name), &n) &&
              _wcsicmp(name, L"Default") == 0;
    CloseDesktop(d);
    return ok;
}
struct Monitor {
    RECT rect;
    bool primary;
    std::wstring name;
};
static BOOL CALLBACK MonitorProc(HMONITOR h, HDC, LPRECT, LPARAM p) {
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(h, &info))
        ((std::vector<Monitor> *)p)
            ->push_back(
                {info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0, info.szDevice});
    return TRUE;
}
static void SampleCapture() {
    SIZE_T n = NativeResources::Read().privateWorking, old = capturePeak.load();
    while (n > old && !capturePeak.compare_exchange_weak(old, n)) {
    };
}

class PngWriter {
    HANDLE file;
    z_stream stream = {};
    bool ready = false, ended = false;
    std::vector<unsigned char> output, filtered;
    static void Big(unsigned char *b, unsigned n) {
        b[0] = n >> 24;
        b[1] = n >> 16;
        b[2] = n >> 8;
        b[3] = n;
    }
    bool Chunk(const char *type, const unsigned char *data, unsigned size) {
        unsigned char header[8], tail[4];
        Big(header, size);
        memcpy(header + 4, type, 4);
        uLong crc = crc32(0, (const Bytef *)type, 4);
        if (size)
            crc = crc32(crc, data, size);
        Big(tail, crc);
        return WriteAll(file, header, 8) && (!size || WriteAll(file, data, size)) &&
               WriteAll(file, tail, 4);
    }
    bool Compress(int flush) {
        int result;
        do {
            stream.next_out = output.data();
            stream.avail_out = output.size();
            result = deflate(&stream, flush);
            if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
                return false;
            unsigned n = output.size() - stream.avail_out;
            if (n && !Chunk("IDAT", output.data(), n))
                return false;
        } while (stream.avail_in || stream.avail_out == 0 ||
                 (flush == Z_FINISH && result != Z_STREAM_END));
        return true;
    }

  public:
    PngWriter(HANDLE f) : file(f), output(32768) {}
    ~PngWriter() {
        if (ready)
            deflateEnd(&stream);
    }
    bool Begin(int width, int height) {
        const unsigned char magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
        unsigned char header[13] = {};
        Big(header, width);
        Big(header + 4, height);
        header[8] = 8;
        header[9] = 2;
        if (!WriteAll(file, magic, 8) || !Chunk("IHDR", header, 13))
            return false;
        filtered.resize((size_t)width * 3 + 1);
        if (deflateInit2(&stream, 3, Z_DEFLATED, 15, 4, Z_DEFAULT_STRATEGY) != Z_OK)
            return false;
        ready = true;
        return true;
    }
    bool Row(const unsigned char *bgr, int width) {
        filtered[0] = 1;
        unsigned char previous[3] = {};
        for (int x = 0; x < width; x++)
            for (int c = 0; c < 3; c++) {
                unsigned char v = bgr[x * 3 + 2 - c];
                filtered[1 + x * 3 + c] = (unsigned char)(v - previous[c]);
                previous[c] = v;
            }
        stream.next_in = filtered.data();
        stream.avail_in = filtered.size();
        return Compress(Z_NO_FLUSH);
    }
    bool Finish() {
        stream.next_in = nullptr;
        stream.avail_in = 0;
        ended = Compress(Z_FINISH) && Chunk("IEND", nullptr, 0);
        return ended;
    }
};
static bool EncodeDesktop(HANDLE file, const std::vector<Monitor> &monitors, RECT bounds) {
    // 每次仅保留 32 行像素，不分配整张双屏位图；无屏幕覆盖的区域填黑。
    // 各条带采样时间不同，快速运动画面可能存在时间差。
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top, rows = 32,
        stride = (width * 3 + 3) & ~3;
    if (width <= 0 || height <= 0 || width > 30000 || height > 30000)
        return false;
    HDC desktop = GetDC(nullptr), memory = CreateCompatibleDC(desktop);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -rows;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
        DeleteDC(memory);
        ReleaseDC(nullptr, desktop);
        return false;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    bool ok = true;
    {
        PngWriter writer(file);
        ok = writer.Begin(width, height);
        SampleCapture();
        for (int y = 0; ok && y < height; y += rows) {
            int count = std::min(rows, height - y);
            memset(pixels, 0, stride * rows);
            RECT strip = {bounds.left, bounds.top + y, bounds.right, bounds.top + y + count};
            for (auto &monitor : monitors) {
                RECT intersect;
                if (IntersectRect(&intersect, &strip, &monitor.rect))
                    if (!BitBlt(memory, intersect.left - bounds.left, intersect.top - strip.top,
                                intersect.right - intersect.left, intersect.bottom - intersect.top,
                                desktop, intersect.left, intersect.top, SRCCOPY | CAPTUREBLT)) {
                        ok = false;
                        break;
                    }
            }
            GdiFlush();
            for (int row = 0; ok && row < count; row++)
                ok = writer.Row((unsigned char *)pixels + row * stride, width);
            if (y == 0 || y + rows >= height)
                SampleCapture();
        }
        if (ok)
            ok = writer.Finish();
        SampleCapture();
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, desktop);
    return ok;
}
static bool Capture(const Settings &s) {
    ULONGLONG started = GetTickCount64();
    capturePeak = 0;
    if (!DesktopAvailable()) {
        Log(L"SKIP desktop locked or unavailable");
        return false;
    }
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, MonitorProc, (LPARAM)&monitors);
    if (monitors.empty())
        return false;
    RECT bounds = monitors[0].rect;
    for (auto &m : monitors)
        UnionRect(&bounds, &bounds, &m.rect);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t day[32];
    swprintf_s(day, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
    std::wstring dir = s.directory + L"\\" + day, name = FormatName(s.format, t);
    if (name.empty() || !MakeDirectories(dir)) {
        lastError = L"目录或文件名格式无效";
        Log(L"CAPTURE ERROR " + lastError);
        return false;
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring path;
    for (unsigned n = 0; n < 10000; n++) {
        path = dir + L"\\" + name + (n ? L"_" + std::to_wstring(n + 1) : L"") + L".png";
        file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        Log(L"ERROR cannot create PNG path=" + path + L" win32=" + std::to_wstring(GetLastError()));
        return false;
    }
    bool ok = EncodeDesktop(file, monitors, bounds);
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
        Log(L"CAPTURE ERROR encoding failed");
        return false;
    }
    std::wstring json = L"{\"file\":" + Json(path) + L",\"width\":" +
                        std::to_wstring(bounds.right - bounds.left) + L",\"height\":" +
                        std::to_wstring(bounds.bottom - bounds.top) +
                        L",\"capturePeakPrivateWorkingSetBytes\":" +
                        std::to_wstring(capturePeak.load()) + L",\"monitors\":[";
    for (size_t i = 0; i < monitors.size(); i++) {
        auto &m = monitors[i];
        if (i)
            json += L",";
        json += L"{\"name\":" + Json(m.name) + L",\"primary\":" + (m.primary ? L"true" : L"false") +
                L",\"left\":" + std::to_wstring(m.rect.left) + L",\"top\":" +
                std::to_wstring(m.rect.top) + L",\"width\":" +
                std::to_wstring(m.rect.right - m.rect.left) + L",\"height\":" +
                std::to_wstring(m.rect.bottom - m.rect.top) + L"}";
    }
    TextFile(root + L"\\last-capture.json",
             json + L"],\"durationMs\":" + std::to_wstring(GetTickCount64() - started) + L"}");
    Log(L"SAVED " + path + L" native-streaming monitors=" + std::to_wstring(monitors.size()) +
        L" size=" + std::to_wstring(bounds.right - bounds.left) + L"x" +
        std::to_wstring(bounds.bottom - bounds.top) + L" durationMs=" +
        std::to_wstring(GetTickCount64() - started));
    return true;
}
#ifdef NATIVE_TEST_BUILD
static bool (*testCapture)(const Settings &) = nullptr;
#endif
static DWORD WINAPI Worker(void *p) {
    // Keep PNG encoding behind foreground work without lowering tray UI priority.
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL))
        Log(L"ERROR SetThreadPriority win32=" + std::to_wstring(GetLastError()));
    Settings *s = (Settings *)p;
#ifdef NATIVE_TEST_BUILD
    bool ok = testCapture ? testCapture(*s) : Capture(*s);
#else
    bool ok = Capture(*s);
#endif
    delete s;
    Log(L"WORKER done success=" + std::to_wstring(ok) + L" message=" +
        std::to_wstring(completionMessage));
    if (!PostMessageW(mainWindow, completionMessage, ok, 0)) {
        Log(L"ERROR completion PostMessage win32=" + std::to_wstring(GetLastError()));
        busy = false;
    }
    return 0;
}
static void QueueCapture() {
    // busy.exchange 防止手动和定时任务重叠；Settings 副本由 Worker 负责释放。
    if (stopRequested || locked || busy.exchange(true)) {
        Log(L"SKIP stopping=" + std::to_wstring(stopRequested) + L" locked=" +
            std::to_wstring(locked) + L" busy=" + std::to_wstring(busy.load()));
        return;
    }
    Log(L"QUEUED");
    Settings *s = new Settings(settings);
    HANDLE thread =
        CreateThread(nullptr, 256 * 1024, Worker, s, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (thread)
        CloseHandle(thread);
    else {
        DWORD error = GetLastError();
        delete s;
        busy = false;
        completionStatus = L"截图线程启动失败";
        Log(L"ERROR CreateThread win32=" + std::to_wstring(error));
    }
}
static std::wstring ResourceText(bool json = false) {
    auto m = NativeResources::Read();
    wchar_t b[1800];
    static ULONGLONG lastWall = 0;
    static double lastCpu = 0;
    ULONGLONG now = GetTickCount64();
    SYSTEM_INFO sys;
    GetSystemInfo(&sys);
    double percent = lastWall && now > lastWall
                         ? (m.cpuSeconds - lastCpu) * 100000.0 / (now - lastWall) /
                               std::max(1ul, sys.dwNumberOfProcessors)
                         : 0;
    lastWall = now;
    lastCpu = m.cpuSeconds;
    if (json)
        swprintf_s(b,
                   L"{\"pid\":%lu,\"taskManagerPrivateWorkingSetBytes\":%llu,\"privateBytes\":%llu,"
                   L"\"totalWorkingSetBytes\":%llu,\"capturePeakPrivateWorkingSetBytes\":%llu,"
                   L"\"cpuSeconds\":%.3f,\"cpuPercent\":%.3f}",
                   GetCurrentProcessId(), (unsigned long long)m.privateWorking,
                   (unsigned long long)m.privateCommit, (unsigned long long)m.working,
                   (unsigned long long)capturePeak.load(), m.cpuSeconds, percent);
    else
        swprintf_s(b,
                   L"TrayGroups 定时截图\r\n\r\n任务管理器口径（专用工作集）：%.2f "
                   L"MiB\r\n专用提交内存：%.2f MiB\r\n总工作集（含共享内存）：%.2f "
                   L"MiB\r\n最近截图采样峰值（进程专用工作集）：%.2f MiB\r\nCPU 当前：%.2f %%    "
                   L"累计：%.2f 秒\r\n\r\n不强制清空工作集。",
                   m.privateWorking / 1048576.0, m.privateCommit / 1048576.0, m.working / 1048576.0,
                   capturePeak.load() / 1048576.0, percent, m.cpuSeconds);
    return b;
}
static HWND CaptureControl(HWND h, const wchar_t *cls, const wchar_t *text, DWORD style, int x,
                           int y, int width, int height, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, x, y, width,
                             height, h, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}
static void ShowResources();
static LRESULT CALLBACK ResourceProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_TIMER) {
        SetWindowTextW(resourceLabel, ResourceText().c_str());
        return 0;
    }
    if (msg == WM_DESTROY) {
        KillTimer(h, 1);
        resourcesWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
static void ShowResources() {
    if (resourcesWindow) {
        ShowWindow(resourcesWindow, SW_RESTORE);
        SetForegroundWindow(resourcesWindow);
        return;
    }
    resourcesWindow =
        CreateWindowExW(WS_EX_APPWINDOW, L"Crli.Capture.Resources", L"截图程序 · 资源占用",
                        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 550, 350, nullptr,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    resourceLabel =
        CaptureControl(resourcesWindow, L"STATIC", ResourceText().c_str(), 0, 15, 15, 510, 280, 0);
    SetTimer(resourcesWindow, 1, 1000, nullptr);
    ShowWindow(resourcesWindow, SW_RESTORE);
    SetWindowPos(resourcesWindow, HWND_TOP, 50, 50, 550, 350, SWP_SHOWWINDOW);
    SetForegroundWindow(resourcesWindow);
}
static LRESULT CALLBACK SettingsProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_COMMAND && LOWORD(w) == 1) {
        wchar_t dir[32768], format[256];
        GetWindowTextW(dirEdit, dir, 32768);
        GetWindowTextW(formatEdit, format, 256);
        Settings changed;
        changed.directory = Trim(dir);
        changed.format = Trim(format);
        SYSTEMTIME t;
        GetLocalTime(&t);
        if (changed.directory.size() < 3 ||
            !(changed.directory[1] == L':' ||
              (changed.directory[0] == L'\\' && changed.directory[1] == L'\\')) ||
            FormatName(changed.format, t).empty()) {
            MessageBoxW(h, L"请输入完整保存路径，以及 HH-mm、HH-mm-ss 等文件名格式。", L"设置",
                        MB_ICONWARNING);
            return 0;
        }
        if (!MakeDirectories(changed.directory) || !Save(changed)) {
            MessageBoxW(h, L"目录或设置无法保存。", L"设置", MB_ICONERROR);
            return 0;
        }
        settings = changed;
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(w) == 2) {
        ShowResources();
        return 0;
    }
    if (msg == WM_DESTROY) {
        settingsWindow = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
static void ShowSettings() {
    if (settingsWindow) {
        ShowWindow(settingsWindow, SW_RESTORE);
        SetForegroundWindow(settingsWindow);
        return;
    }
    settingsWindow =
        CreateWindowExW(WS_EX_APPWINDOW, L"Crli.Capture.Settings", L"定时双屏截图 · 设置",
                        WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 660, 265, nullptr,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    CaptureControl(settingsWindow, L"STATIC", L"保存根目录（粘贴完整路径；自动在下面按天建文件夹）",
                   0, 15, 15, 620, 22, 0);
    dirEdit = CaptureControl(settingsWindow, L"EDIT", settings.directory.c_str(),
                             WS_BORDER | ES_AUTOHSCROLL, 15, 43, 615, 28, 10);
    CaptureControl(settingsWindow, L"STATIC", L"文件名格式：HH-mm 为 24 小时制，例如 16-10.png", 0,
                   15, 88, 615, 22, 0);
    formatEdit = CaptureControl(settingsWindow, L"EDIT", settings.format.c_str(),
                                WS_BORDER | ES_AUTOHSCROLL, 15, 116, 615, 28, 11);
    CaptureControl(settingsWindow, L"BUTTON", L"资源占用", BS_PUSHBUTTON, 15, 169, 110, 30, 2);
    CaptureControl(settingsWindow, L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 510, 169, 120, 30, 1);
    ShowWindow(settingsWindow, SW_RESTORE);
    SetWindowPos(settingsWindow, HWND_TOP, 40, 40, 660, 265, SWP_SHOWWINDOW);
    SetForegroundWindow(settingsWindow);
}
static void Notification(bool add) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof(n);
    n.hWnd = mainWindow;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = NotifyMessage;
    n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    std::wstring text = L"定时双屏截图";
    if (paused)
        text += L"：已暂停";
    else {
        SYSTEMTIME t = FromTicks(nextTime);
        wchar_t b[40];
        swprintf_s(b, L"：%02u:%02u", t.wHour, t.wMinute);
        text += b;
    }
    wcsncpy_s(n.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &n);
    if (add) {
        n.uVersion = 4;
        Shell_NotifyIconW(NIM_SETVERSION, &n);
    }
}
static bool StartupEnabled() {
    HKEY key;
    bool ok = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        ok = RegQueryValueExW(key, L"ScreenCaptureTimer", nullptr, nullptr, nullptr, nullptr) ==
             ERROR_SUCCESS;
        RegCloseKey(key);
    }
    return ok;
}
static void ToggleStartup() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        if (StartupEnabled())
            RegDeleteValueW(key, L"ScreenCaptureTimer");
        else {
            wchar_t exe[32768];
            GetModuleFileNameW(nullptr, exe, 32768);
            std::wstring command = L"\"" + std::wstring(exe) + L"\"";
            RegSetValueExW(key, L"ScreenCaptureTimer", 0, REG_SZ, (BYTE *)command.c_str(),
                           (command.size() + 1) * sizeof(wchar_t));
        }
        RegCloseKey(key);
    }
}
static void OpenFolder() {
    std::wstring command = L"explorer.exe \"" + settings.directory + L"\"";
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                       &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}
static void Menu() {
    HMENU m = CreatePopupMenu();
    SYSTEMTIME t = FromTicks(nextTime);
    wchar_t status[80];
    swprintf_s(status, L"下次截图：%02u:%02u（每 10 分钟整点）", t.wHour, t.wMinute);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, status);
    AppendMenuW(m, MF_STRING, 1, L"立即截图");
    AppendMenuW(m, MF_STRING | (paused ? MF_CHECKED : 0), 2, L"暂停截图");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"设置保存目录 / 文件名…");
    AppendMenuW(m, MF_STRING, 4, L"资源占用");
    AppendMenuW(m, MF_STRING, 5, L"打开保存目录");
    AppendMenuW(m, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), 6, L"登录后自动启动");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 7, L"退出");
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(mainWindow);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, mainWindow, nullptr);
    DestroyMenu(m);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    if (cmd == 1)
        QueueCapture();
    else if (cmd == 2) {
        paused = !paused;
        nextTime = Next(Now());
        Notification(false);
    } else if (cmd == 3)
        ShowSettings();
    else if (cmd == 4)
        ShowResources();
    else if (cmd == 5)
        OpenFolder();
    else if (cmd == 6)
        ToggleStartup();
    else if (cmd == 7)
        PostMessageW(mainWindow, StopMessage, 0, 0);
}
static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == NotifyMessage) {
        if (LOWORD(l) == WM_CONTEXTMENU)
            PostMessageW(h, WM_APP + 8, 2, 0);
        else if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT)
            PostMessageW(h, WM_APP + 8, 1, 0);
        return 0;
    }
    if (msg == WM_APP + 8) {
        if (w == 2)
            Menu();
        else
            ShowSettings();
        return 0;
    }
    if (msg == WM_TIMER) {
        if (stopRequested) {
            if (!busy)
                DestroyWindow(h);
            return 0;
        }
        ULONGLONG now = Now();
        if (now + 20000000 < previousTime || now > previousTime + 100000000)
            nextTime = Next(now);
        previousTime = now;
        if (now >= nextTime) {
            nextTime = Next(now);
            if (!paused && !locked)
                QueueCapture();
            Notification(false);
        }
        return 0;
    }
    if (msg == WM_WTSSESSION_CHANGE) {
        if (w == WTS_SESSION_LOCK)
            locked = true;
        else if (w == WTS_SESSION_UNLOCK) {
            locked = false;
            nextTime = Next(Now());
        }
        return 0;
    }
    if (msg == WorkerDone) {
        busy = false;
        Log(L"CAPTURE completed success=" + std::to_wstring(w));
        return 0;
    }
    if (msg == StopMessage || msg == WM_CLOSE) {
        if (busy) {
            stopRequested = true;
            return 0;
        }
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(w) == 1)
            ShowSettings();
        else if (LOWORD(w) == 2)
            QueueCapture();
        return 0;
    }
    if (msg == taskbarCreated) {
        Notification(true);
        return 0;
    }
    if (msg == WM_DESTROY) {
        KillTimer(h, 1);
        WTSUnRegisterSessionNotification(h);
        NOTIFYICONDATAW n = {};
        n.cbSize = sizeof(n);
        n.hWnd = h;
        n.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &n);
        if (settingsWindow)
            DestroyWindow(settingsWindow);
        if (resourcesWindow)
            DestroyWindow(resourcesWindow);
        Log(L"STOP native");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static HINSTANCE hostInstance = nullptr;
static bool initialized = false;

void Initialize(HWND owner, HINSTANCE instance, const std::wstring &directory) {
    if (initialized)
        return;
    initialized = true;
    mainWindow = owner;
    hostInstance = instance;
    completionMessage = IntegratedDone;
    stopRequested = false;
    root = directory;
    configPath = root + L"\\config.json";
    bool migrate = GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (migrate) {
        std::wstring legacy = root + L"\\ScreenCaptureTimer\\config.json";
        if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES)
            configPath = legacy;
    }
    Load();
    if (GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES && !Save(settings))
        Log(L"CONFIG ERROR cannot create portable config");
    if (configPath != root + L"\\config.json") {
        configPath = root + L"\\config.json";
        if (!Save(settings))
            Log(L"CONFIG ERROR cannot migrate portable config");
    }
    LOGFONTW lf = {};
    lf.lfHeight = -14;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font = CreateFontIndirectW(&lf);
    struct Registration {
        const wchar_t *cls;
        WNDPROC proc;
    };
    for (auto reg : {Registration{L"Crli.Capture.Settings", SettingsProc},
                     Registration{L"Crli.Capture.Resources", ResourceProc}}) {
        WNDCLASSW wc = {};
        wc.hInstance = hostInstance;
        wc.lpszClassName = reg.cls;
        wc.lpfnWndProc = reg.proc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
    }
    previousTime = Now();
    nextTime = Next(previousTime);
    if (!SetTimer(mainWindow, IntegratedTimer, 500, nullptr))
        Log(L"ERROR SetTimer win32=" + std::to_wstring(GetLastError()));
    Log(L"START integrated capture; fileFormat=" + settings.format + L" output=" +
        settings.directory);
}
UINT CompletionMessage() {
    return IntegratedDone;
}
UINT TimerId() {
    return IntegratedTimer;
}
bool IsBusy() {
    return busy.load();
}
void OnTimer() {
    ULONGLONG now = Now();
    if (now + 20000000 < previousTime || now > previousTime + 100000000)
        nextTime = Next(now);
    previousTime = now;
    if (now >= nextTime) {
        nextTime = Next(now);
        if (!paused && !locked)
            QueueCapture();
    }
}
void OnSessionChange(WPARAM event) {
    if (event == WTS_SESSION_LOCK)
        locked = true;
    else if (event == WTS_SESSION_UNLOCK) {
        locked = false;
        nextTime = Next(Now());
    }
}
void OnCompletion(bool success) {
    busy = false;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[30];
    swprintf_s(stamp, L"%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    completionStatus = success ? L"已保存 " + std::wstring(stamp) : L"截图失败，请查看日志";
    Log(L"COMPLETE received success=" + std::to_wstring(success) +
        L" sampledPrivateWorkingSetPeak=" + std::to_wstring(capturePeak.load()));
}
void PrepareStop() {
    stopRequested = true;
}
std::wstring StatusText() {
    if (!initialized)
        return L"定时截图尚未初始化";
    SYSTEMTIME t = FromTicks(nextTime);
    wchar_t stamp[30];
    swprintf_s(stamp, L"%02u:%02u", t.wHour, t.wMinute);
    std::wstring state = busy     ? L"正在截图…"
                         : paused ? L"定时截图已暂停"
                         : locked ? L"锁屏暂停截图"
                                  : L"下次截图 " + std::wstring(stamp);
    return state + L" · " + completionStatus;
}
void ShowCaptureSettings() {
    Log(L"SETTINGS open requested");
    ShowSettings();
    Log(L"SETTINGS hwnd=" + std::to_wstring((ULONG_PTR)settingsWindow) + L" visible=" +
        std::to_wstring(settingsWindow && IsWindowVisible(settingsWindow)) + L" win32=" +
        std::to_wstring(GetLastError()));
}
void AddMenu(HMENU parent) {
    HMENU submenu = CreatePopupMenu();
    SYSTEMTIME t = FromTicks(nextTime);
    wchar_t status[80];
    swprintf_s(status, L"下次截图：%02u:%02u（每 10 分钟）", t.wHour, t.wMinute);
    AppendMenuW(submenu, MF_STRING | MF_DISABLED, 0, status);
    AppendMenuW(submenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(submenu, MF_STRING, CaptureNowCommand, L"立即截图");
    AppendMenuW(submenu, MF_STRING | (paused ? MF_CHECKED : 0), PauseCommand, L"暂停定时截图");
    AppendMenuW(submenu, MF_STRING, SettingsCommand, L"设置保存目录和文件名…");
    AppendMenuW(submenu, MF_STRING, ResourcesCommand, L"截图资源占用");
    AppendMenuW(submenu, MF_STRING, FolderCommand, L"打开截图目录");
    AppendMenuW(parent, MF_POPUP, (UINT_PTR)submenu, L"定时截图");
}
bool HandleMenu(UINT command) {
    if (command >= CaptureNowCommand && command <= FolderCommand)
        Log(L"COMMAND id=" + std::to_wstring(command));
    if (command == CaptureNowCommand)
        QueueCapture();
    else if (command == PauseCommand) {
        paused = !paused;
        nextTime = Next(Now());
    } else if (command == SettingsCommand)
        ShowCaptureSettings();
    else if (command == ResourcesCommand)
        ShowResources();
    else if (command == FolderCommand)
        OpenFolder();
    else
        return false;
    return true;
}
void Shutdown() {
    KillTimer(mainWindow, IntegratedTimer);
    if (settingsWindow)
        DestroyWindow(settingsWindow);
    if (resourcesWindow)
        DestroyWindow(resourcesWindow);
    if (font) {
        DeleteObject(font);
        font = nullptr;
    }
    initialized = false;
}

#if !defined(NATIVE_TEST_BUILD) && !defined(SCREEN_CAPTURE_EMBEDDED)
int RunStandalone(HINSTANCE instance, LPWSTR command) {
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, 32768);
    root = exe;
    root.resize(root.find_last_of(L'\\'));
    configPath = root + L"\\config.json";
    std::wstring args = Trim(command ? command : L"");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Load();
    if (GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES && !Save(settings))
        Log(L"CONFIG ERROR cannot create portable config");
    HWND existing = FindWindowW(AppClass, nullptr);
    if (args == L"--stop") {
        if (existing)
            PostMessageW(existing, StopMessage, 0, 0);
        return 0;
    }
    if (args == L"--capture-once")
        return Capture(settings) ? 0 : 2;
    if (existing) {
        if (args == L"--settings")
            PostMessageW(existing, WM_COMMAND, 1, 0);
        else if (args == L"--capture-now")
            PostMessageW(existing, WM_COMMAND, 2, 0);
        return 0;
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Crli.ScreenCaptureTimer.Native");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }
    LOGFONTW lf = {};
    lf.lfHeight = -14;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font = CreateFontIndirectW(&lf);
    struct Registration {
        const wchar_t *cls;
        WNDPROC proc;
    };
    for (auto reg :
         {Registration{AppClass, MainProc}, Registration{L"Crli.Capture.Settings", SettingsProc},
          Registration{L"Crli.Capture.Resources", ResourceProc}}) {
        WNDCLASSW wc = {};
        wc.hInstance = instance;
        wc.lpszClassName = reg.cls;
        wc.lpfnWndProc = reg.proc;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
    }
    mainWindow = CreateWindowExW(WS_EX_TOOLWINDOW, AppClass, L"定时双屏截图", WS_POPUP, 0, 0, 0, 0,
                                 nullptr, nullptr, instance, nullptr);
    if (!mainWindow) {
        CloseHandle(mutex);
        return 1;
    }
    ShowWindow(mainWindow, SW_HIDE);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WTSRegisterSessionNotification(mainWindow, NOTIFY_FOR_THIS_SESSION);
    previousTime = Now();
    nextTime = Next(previousTime);
    Notification(true);
    SetTimer(mainWindow, 1, 500, nullptr);
    Log(L"START native streaming; fileFormat=" + settings.format + L" output=" +
        settings.directory);
    if (args == L"--settings")
        ShowSettings();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (settingsWindow && IsDialogMessageW(settingsWindow, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(font);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

#endif

} // namespace CaptureTimer

#if !defined(NATIVE_TEST_BUILD) && !defined(SCREEN_CAPTURE_EMBEDDED)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int) {
    return CaptureTimer::RunStandalone(instance, command);
}
#endif
