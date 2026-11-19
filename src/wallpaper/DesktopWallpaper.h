#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <string>
#include <vector>

// 按需访问 Windows 壁纸服务；不创建常驻窗口、线程或图片缓存。
namespace Wallpaper {
struct Monitor {
    std::wstring devicePath, source, name, manufacturer, product, serial, identity, image;
    RECT rect = {};
    bool primary = false, connected = false, portableIdentity = false;
};
struct Snapshot {
    std::vector<Monitor> monitors;
    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    bool slideshow = false;
};
struct Change {
    Monitor monitor;
    std::wstring image;
};
bool Read(Snapshot &snapshot, std::wstring &error);
// 在调用系统前重新验证设备路径、硬件身份和图片；不以显示编号重新绑定。
bool Validate(const std::vector<Change> &changes, std::wstring &error);
bool Apply(const std::vector<Change> &changes, DESKTOP_WALLPAPER_POSITION position,
           bool changePosition, std::wstring &error, const std::wstring &saveDirectory = L"");
bool Reapply(const std::wstring &directory, std::wstring &error);
std::wstring Label(const Monitor &monitor);
// defaults/wallpapers.ini 打包为程序旁 wallpapers.ini；相对路径以程序目录为基准。
std::wstring DefaultImage(const std::wstring &directory, int width, int height);
// EDID 与重复序列号处理独立于设备 API，方便对多屏/无序列号情况回归。
bool DecodeEdid(const std::vector<unsigned char> &bytes, Monitor &monitor);
void AssignIdentities(std::vector<Monitor> &monitors);
#ifdef NATIVE_TEST_BUILD
struct TestHooks {
    bool (*read)(Snapshot &, std::wstring &) = nullptr;
    HRESULT (*setWallpaper)(const std::wstring &, const std::wstring &) = nullptr;
    HRESULT (*setPosition)(DESKTOP_WALLPAPER_POSITION) = nullptr;
};
extern TestHooks testHooks;
#endif
} // namespace Wallpaper
