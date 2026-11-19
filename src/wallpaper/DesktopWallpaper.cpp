#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include "DesktopWallpaper.h"
#include "../common/TraySupport.h"
#include <setupapi.h>
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>

namespace Wallpaper {
#ifdef NATIVE_TEST_BUILD
TestHooks testHooks;
#endif
namespace {
// COM 指针在本次操作结束时释放；兼容宿主已经初始化的 STA/MTA。
struct Service {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IDesktopWallpaper *value = nullptr;
    HRESULT SetImage(const std::wstring &path, const std::wstring &image) {
#ifdef NATIVE_TEST_BUILD
        if (testHooks.setWallpaper)
            return testHooks.setWallpaper(path, image);
#endif
        return value->SetWallpaper(path.c_str(), image.c_str());
    }
    HRESULT SetPosition(DESKTOP_WALLPAPER_POSITION position) {
#ifdef NATIVE_TEST_BUILD
        if (testHooks.setPosition)
            return testHooks.setPosition(position);
#endif
        return value->SetPosition(position);
    }
    ~Service() {
        if (value)
            value->Release();
        if (SUCCEEDED(initialized))
            CoUninitialize();
    }
    bool Open(std::wstring &error) {
#ifdef NATIVE_TEST_BUILD
        if (testHooks.read && testHooks.setWallpaper && testHooks.setPosition)
            return true;
#endif
        HRESULT result = FAILED(initialized) && initialized != RPC_E_CHANGED_MODE
                             ? initialized
                             : CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                                                IID_PPV_ARGS(&value));
        if (FAILED(result)) {
            wchar_t code[32];
            swprintf_s(code, L"0x%08lx", (unsigned long)result);
            error = L"无法连接 Windows 壁纸服务：" + std::wstring(code);
        }
        return SUCCEEDED(result);
    }
};
std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    return value;
}
std::wstring Trim(std::wstring value) {
    auto first = value.find_first_not_of(L" \r\n\t");
    return first == std::wstring::npos
               ? L""
               : value.substr(first, value.find_last_not_of(L" \r\n\t") - first + 1);
}
std::wstring Escape(const std::wstring &value) {
    std::wstring result;
    for (wchar_t c : value) {
        if (c == L'%' || c == L'|' || c == L'=' || c < 32) {
            wchar_t hex[8];
            swprintf_s(hex, L"%%%04x", (unsigned)c);
            result += hex;
        } else
            result += c;
    }
    return result;
}

using Entries = std::map<std::wstring, std::wstring>;
Entries ReadSection(const std::wstring &text, const std::wstring &section) {
    Entries entries;
    bool active = false;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        auto line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (!line.empty() && line.front() == 0xfeff)
            line.erase(0, 1);
        if (line.empty() || line.front() == L';' || line.front() == L'#')
            continue;
        if (line.front() == L'[' && line.back() == L']') {
            active = Lower(line) == section;
            continue;
        }
        size_t equals = line.find(L'=');
        if (active && equals != std::wstring::npos)
            entries[Lower(Trim(line.substr(0, equals)))] = Trim(line.substr(equals + 1));
    }
    return entries;
}
std::wstring ProfilePath(const std::wstring &directory) {
    return directory + L"\\wallpaper-selection.ini";
}
std::wstring PortablePath(const std::wstring &directory, const std::wstring &path) {
    std::wstring prefix = directory + L"\\";
    if (path.size() > prefix.size() && _wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) == 0)
        return path.substr(prefix.size());
    return path;
}
std::wstring ResolveImage(const std::wstring &directory, std::wstring value) {
    if (value.empty())
        return L"";
    bool absolute =
        value.size() > 2 && ((value[1] == L':' && (value[2] == L'\\' || value[2] == L'/')) ||
                             (value[0] == L'\\' && value[1] == L'\\'));
    if (!absolute)
        value = directory + L"\\" + value;
    DWORD length = GetFullPathNameW(value.c_str(), 0, nullptr, nullptr);
    if (!length)
        return L"";
    std::wstring resolved(length, L'\0');
    DWORD written = GetFullPathNameW(value.c_str(), length, resolved.data(), nullptr);
    if (!written || written >= length)
        return L"";
    resolved.resize(written);
    return resolved;
}
void ReadEdid(Monitor &monitor) {
    const GUID monitorClass = {
        0xe6f07b5f, 0xee97, 0x4a90, {0xb0, 0x76, 0x33, 0xf5, 0x7b, 0xf4, 0xea, 0xa7}};
    HDEVINFO devices = SetupDiGetClassDevsW(&monitorClass, nullptr, nullptr, DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE)
        return;
    SP_DEVICE_INTERFACE_DATA deviceInterface = {};
    deviceInterface.cbSize = sizeof(deviceInterface);
    if (SetupDiOpenDeviceInterfaceW(devices, monitor.devicePath.c_str(), 0, &deviceInterface)) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &deviceInterface, nullptr, 0, &size, nullptr);
        std::vector<unsigned char> data(size);
        if (size >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)data.data();
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA info = {};
            info.cbSize = sizeof(info);
            if (SetupDiGetDeviceInterfaceDetailW(devices, &deviceInterface, detail, size, nullptr,
                                                 &info)) {
                HKEY key =
                    SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                if (key != INVALID_HANDLE_VALUE) {
                    DWORD type = 0, bytes = 0;
                    if (RegQueryValueExW(key, L"EDID", nullptr, &type, nullptr, &bytes) ==
                            ERROR_SUCCESS &&
                        type == REG_BINARY && bytes >= 128 && bytes <= 4096) {
                        std::vector<unsigned char> edid(bytes);
                        if (RegQueryValueExW(key, L"EDID", nullptr, &type, edid.data(), &bytes) ==
                            ERROR_SUCCESS)
                            DecodeEdid(edid, monitor);
                    }
                    RegCloseKey(key);
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
}
struct DisplayTarget {
    std::wstring path, source, name;
};
std::vector<DisplayTarget> Targets() {
    std::vector<DisplayTarget> result;
    for (int retry = 0; retry < 3; ++retry) {
        UINT32 paths = 0, modes = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
        std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
        LONG status =
            QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, p.data(), &modes, m.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (status != ERROR_SUCCESS)
            break;
        for (UINT32 n = 0; n < paths; ++n) {
            DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
            target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size = sizeof(target);
            target.header.adapterId = p[n].targetInfo.adapterId;
            target.header.id = p[n].targetInfo.id;
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = p[n].sourceInfo.adapterId;
            source.header.id = p[n].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
                DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS)
                result.push_back({target.monitorDevicePath, source.viewGdiDeviceName,
                                  target.monitorFriendlyDeviceName});
        }
        break;
    }
    return result;
}
bool ImageValid(const std::wstring &path, std::wstring &error) {
    size_t dot = path.find_last_of(L'.');
    auto extension = dot == std::wstring::npos ? L"" : Lower(path.substr(dot));
    if (extension != L".jpg" && extension != L".jpeg" && extension != L".png" &&
        extension != L".bmp") {
        error = L"请选择 JPG、PNG 或 BMP 图片。";
        return false;
    }
    DWORD attributes = GetFileAttributesW(path.c_str());
    bool absolute =
        path.size() > 2 && ((path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
                            (path[0] == L'\\' && path[1] == L'\\'));
    if (!absolute || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"图片不存在或不是完整文件路径：" + path;
        return false;
    }
    // 由 Shell 解码验证图片，使用固定小尺寸，避免加载原分辨率图片到常驻进程。
    IShellItemImageFactory *factory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory));
    HBITMAP bitmap = nullptr;
    if (SUCCEEDED(hr)) {
        hr = factory->GetImage({32, 32}, SIIGBF_THUMBNAILONLY, &bitmap);
        factory->Release();
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (FAILED(hr)) {
        error = L"无法读取图片，请选择有效的 JPG、PNG 或 BMP：" + path;
        return false;
    }
    return true;
}
bool MatchChanges(const Snapshot &current, const std::vector<Change> &changes,
                  std::wstring &error) {
    std::set<std::wstring> targets, images;
    for (const auto &change : changes) {
        const auto &old = change.monitor;
        auto path = Lower(old.devicePath);
        auto match = std::find_if(current.monitors.begin(), current.monitors.end(),
                                  [&](const Monitor &m) { return Lower(m.devicePath) == path; });
        if (match == current.monitors.end() || !match->connected ||
            match->identity != old.identity) {
            error = L"显示器已断开或身份发生变化，请刷新后重新选择：" + Label(old);
            return false;
        }
        if (std::count_if(current.monitors.begin(), current.monitors.end(), [&](const Monitor &m) {
                return m.connected && EqualRect(&m.rect, &match->rect);
            }) > 1) {
            error = L"当前屏幕使用复制显示模式，请先在 Windows 中切换为扩展显示，再设置独立壁纸。";
            return false;
        }
        if (!targets.insert(path).second) {
            error = L"同一显示器不能重复设置。";
            return false;
        }
        if (images.insert(change.image).second && !ImageValid(change.image, error))
            return false;
    }
    return true;
}
} // namespace
bool DecodeEdid(const std::vector<unsigned char> &bytes, Monitor &monitor) {
    const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
    if (bytes.size() < 128 || !std::equal(std::begin(header), std::end(header), bytes.begin()))
        return false;
    unsigned sum = 0;
    for (int n = 0; n < 128; ++n)
        sum += bytes[n];
    if (sum % 256)
        return false;
    unsigned manufacturer = (bytes[8] << 8) | bytes[9];
    std::wstring name;
    for (int shift : {10, 5, 0}) {
        unsigned c = (manufacturer >> shift) & 31;
        if (c < 1 || c > 26)
            return false;
        name += wchar_t(L'A' + c - 1);
    }
    monitor.manufacturer = name;
    wchar_t product[8];
    swprintf_s(product, L"%04x", bytes[10] | (bytes[11] << 8));
    monitor.product = product;
    for (int offset = 54; offset + 18 <= 126; offset += 18)
        if (!bytes[offset] && !bytes[offset + 1] && !bytes[offset + 2]) {
            std::wstring value;
            for (int n = offset + 5; n < offset + 18; ++n) {
                unsigned c = bytes[n];
                if (c == 10 || c == 0)
                    break;
                if (c >= 32 && c <= 126)
                    value += wchar_t(c);
            }
            value = Trim(value);
            if (bytes[offset + 3] == 0xff)
                monitor.serial = value;
            if (bytes[offset + 3] == 0xfc && !value.empty())
                monitor.name = value;
        }
    unsigned serial = bytes[12] | (unsigned(bytes[13]) << 8) | (unsigned(bytes[14]) << 16) |
                      (unsigned(bytes[15]) << 24);
    if (monitor.serial.empty() && serial && serial != 0xffffffff)
        monitor.serial = std::to_wstring(serial);
    return true;
}
void AssignIdentities(std::vector<Monitor> &monitors) {
    for (auto &m : monitors) {
        auto serial = Lower(Trim(m.serial));
        bool useful = !serial.empty() && serial != L"0" && serial != L"00000000" &&
                      serial != L"unknown" && serial != L"none";
        m.portableIdentity = useful && !m.manufacturer.empty() && !m.product.empty();
        m.identity = m.portableIdentity ? L"edid|" + Lower(m.manufacturer) + L"|" + m.product +
                                              L"|" + Escape(serial)
                                        : L"device|" + Lower(m.devicePath);
    }
    // 重复硬件序列号不能当唯一身份；两台都明确绑定当前连接路径。
    std::vector<std::wstring> ids;
    for (const auto &m : monitors)
        ids.push_back(m.identity);
    for (size_t n = 0; n < monitors.size(); ++n)
        if (std::count(ids.begin(), ids.end(), ids[n]) > 1) {
            monitors[n].portableIdentity = false;
            monitors[n].identity = L"device|" + Lower(monitors[n].devicePath);
        }
}
std::wstring Label(const Monitor &m) {
    return (m.name.empty() ? L"显示器" : m.name) + (m.serial.empty() ? L"" : L" · " + m.serial) +
           (m.primary ? L"（主屏）" : L"");
}
std::wstring DefaultImage(const std::wstring &directory, int width, int height) {
    auto defaults =
        ReadSection(TraySupport::ReadText(directory + L"\\wallpapers.ini"), L"[byresolution]");
    return ResolveImage(directory,
                        defaults[std::to_wstring(width) + L"x" + std::to_wstring(height)]);
}
static bool ReadSnapshot(Service &service, Snapshot &snapshot, std::wstring &error) {
#ifdef NATIVE_TEST_BUILD
    if (testHooks.read)
        return testHooks.read(snapshot, error);
#endif
    error.clear();
    snapshot = {};
    if (!service.Open(error))
        return false;
    UINT count = 0;
    HRESULT hr = service.value->GetMonitorDevicePathCount(&count);
    if (FAILED(hr)) {
        error = L"无法枚举壁纸显示器。";
        return false;
    }
    auto targets = Targets();
    for (UINT n = 0; n < count; ++n) {
        LPWSTR path = nullptr;
        if (FAILED(service.value->GetMonitorDevicePathAt(n, &path)) || !path)
            continue;
        Monitor m;
        m.devicePath = path;
        CoTaskMemFree(path);
        m.connected = service.value->GetMonitorRECT(m.devicePath.c_str(), &m.rect) == S_OK &&
                      m.rect.right > m.rect.left && m.rect.bottom > m.rect.top;
        for (const auto &target : targets)
            if (Lower(target.path) == Lower(m.devicePath)) {
                m.name = target.name;
                m.source = target.source;
                break;
            }
        ReadEdid(m);
        if (m.connected) {
            MONITORINFO info = {};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(MonitorFromRect(&m.rect, MONITOR_DEFAULTTONULL), &info))
                m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        }
        LPWSTR image = nullptr;
        if (SUCCEEDED(service.value->GetWallpaper(m.devicePath.c_str(), &image)) && image) {
            m.image = image;
            CoTaskMemFree(image);
        }
        snapshot.monitors.push_back(std::move(m));
    }
    AssignIdentities(snapshot.monitors);
    std::stable_sort(
        snapshot.monitors.begin(), snapshot.monitors.end(), [](const Monitor &a, const Monitor &b) {
            if (a.connected != b.connected)
                return a.connected > b.connected;
            return a.rect.left != b.rect.left ? a.rect.left < b.rect.left : a.rect.top < b.rect.top;
        });
    service.value->GetPosition(&snapshot.position);
    DESKTOP_SLIDESHOW_STATE status = {};
    if (SUCCEEDED(service.value->GetStatus(&status)))
        snapshot.slideshow = (status & DSS_SLIDESHOW) != 0;
    return true;
}
bool Read(Snapshot &snapshot, std::wstring &error) {
    Service service;
    return ReadSnapshot(service, snapshot, error);
}

static bool ApplySnapshot(Service &service, const Snapshot &current,
                          const std::vector<Change> &changes, DESKTOP_WALLPAPER_POSITION position,
                          bool changePosition, std::wstring &error,
                          const std::wstring &saveDirectory) {
    if (!MatchChanges(current, changes, error))
        return false;
    if (changePosition && (position < DWPOS_CENTER || position > DWPOS_SPAN)) {
        error = L"壁纸显示方式无效。";
        return false;
    }
    auto appliedPosition = changePosition ? position : current.position;
    if (!changes.empty() && appliedPosition == DWPOS_SPAN) {
        error = L"请先选择填充、适应等显示方式，再应用独立壁纸。";
        return false;
    }
    std::wstring profile, temporary;
    if (!saveDirectory.empty()) {
        profile = ProfilePath(saveDirectory);
        auto images = ReadSection(TraySupport::ReadText(profile), L"[monitors]");
        for (const auto &change : changes)
            images[Escape(change.monitor.identity)] = PortablePath(saveDirectory, change.image);
        std::wstring text =
            L"[Settings]\nPosition=" + std::to_wstring(appliedPosition) + L"\n\n[Monitors]\n";
        for (const auto &[identity, image] : images)
            text += identity + L"=" + image + L"\n";
        temporary = profile + L".tmp";
        if (!TraySupport::WriteText(temporary, text)) {
            DeleteFileW(temporary.c_str());
            error = L"无法在程序目录保存壁纸配置。";
            return false;
        }
    }
    // Windows 的显示方式是全局设置；先记住原值，失败时尝试撤销本次已完成的更改。
    size_t applied = 0;
    HRESULT hr = S_OK;
    if (changePosition)
        hr = service.SetPosition(position);
    for (; SUCCEEDED(hr) && applied < changes.size(); ++applied)
        hr = service.SetImage(changes[applied].monitor.devicePath, changes[applied].image);
    bool saveFailed = SUCCEEDED(hr) && !temporary.empty() &&
                      !MoveFileExW(temporary.c_str(), profile.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (FAILED(hr) || saveFailed) {
        if (!temporary.empty())
            DeleteFileW(temporary.c_str());
        bool restored = true;
        for (size_t n = 0; n < applied; ++n) {
            auto old = std::find_if(
                current.monitors.begin(), current.monitors.end(),
                [&](const Monitor &m) { return m.devicePath == changes[n].monitor.devicePath; });
            if (old != current.monitors.end())
                restored &= SUCCEEDED(service.SetImage(old->devicePath, old->image));
        }
        if (changePosition)
            restored &= SUCCEEDED(service.SetPosition(current.position));
        wchar_t code[32];
        swprintf_s(code, L"0x%08lx", (unsigned long)hr);
        error = saveFailed ? L"无法保存壁纸配置。" : L"Windows 拒绝设置壁纸：" + std::wstring(code);
        error += restored ? L" 已撤销本次更改。" : L" 部分更改无法撤销，请刷新检查。";
        return false;
    }
    return true;
}

bool Apply(const std::vector<Change> &changes, DESKTOP_WALLPAPER_POSITION position,
           bool changePosition, std::wstring &error, const std::wstring &saveDirectory) {
    error.clear();
    if (changes.empty() && !changePosition)
        return true;
    Service service;
    Snapshot current;
    return ReadSnapshot(service, current, error) &&
           ApplySnapshot(service, current, changes, position, changePosition, error, saveDirectory);
}

bool Reapply(const std::wstring &directory, std::wstring &error) {
    auto profile = TraySupport::ReadText(ProfilePath(directory));
    auto images = ReadSection(profile, L"[monitors]");
    auto defaults =
        ReadSection(TraySupport::ReadText(directory + L"\\wallpapers.ini"), L"[byresolution]");
    Service service;
    Snapshot current;
    if (!ReadSnapshot(service, current, error))
        return false;
    std::vector<Change> changes;
    for (const auto &monitor : current.monitors) {
        if (!monitor.connected)
            continue;
        auto saved = images.find(Escape(monitor.identity));
        auto resolution = std::to_wstring(monitor.rect.right - monitor.rect.left) + L"x" +
                          std::to_wstring(monitor.rect.bottom - monitor.rect.top);
        auto image =
            ResolveImage(directory, saved == images.end() ? defaults[resolution] : saved->second);
        if (!image.empty())
            changes.push_back({monitor, std::move(image)});
    }
    if (changes.empty()) {
        error = L"当前连接的显示器没有壁纸配置。请在「显示器壁纸」里选择图片并应用。";
        return false;
    }
    auto position = current.position;
    auto savedPosition = ReadSection(profile, L"[settings]")[L"position"];
    if (savedPosition.size() == 1 && savedPosition[0] >= L'0' && savedPosition[0] <= L'5')
        position = static_cast<DESKTOP_WALLPAPER_POSITION>(savedPosition[0] - L'0');
    if (position == DWPOS_SPAN)
        position = DWPOS_FILL;
    return ApplySnapshot(service, current, changes, position, position != current.position, error,
                         L"");
}
} // namespace Wallpaper
