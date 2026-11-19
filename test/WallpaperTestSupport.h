#pragma once
#include "../src/wallpaper/DesktopWallpaper.h"
#include <cstdio>

namespace WallpaperTest {
inline Wallpaper::Snapshot snapshot;
inline std::vector<std::pair<std::wstring, std::wstring>> writes;
inline std::vector<DESKTOP_WALLPAPER_POSITION> positions;
inline int rejectWrite = 0, calls = 0, reads = 0;
inline bool Read(Wallpaper::Snapshot &value, std::wstring &error) {
    ++reads;
    error.clear();
    value = snapshot;
    return true;
}
inline HRESULT SetImage(const std::wstring &device, const std::wstring &image) {
    writes.push_back({device, image});
    ++calls;
    if (calls == rejectWrite)
        return E_ACCESSDENIED;
    for (auto &monitor : snapshot.monitors)
        if (monitor.devicePath == device)
            monitor.image = image;
    return S_OK;
}
inline HRESULT SetPosition(DESKTOP_WALLPAPER_POSITION position) {
    positions.push_back(position);
    snapshot.position = position;
    return S_OK;
}
inline void Reset() {
    writes.clear();
    positions.clear();
    rejectWrite = calls = reads = 0;
}
struct Backend {
    Wallpaper::TestHooks previous = Wallpaper::testHooks;
    Backend() {
        Wallpaper::testHooks = {Read, SetImage, SetPosition};
        Reset();
    }
    ~Backend() {
        Wallpaper::testHooks = previous;
    }
};
inline bool MakeImage(const std::wstring &path) {
    BITMAPFILEHEADER file = {};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + 16;
    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info);
    info.biWidth = info.biHeight = 2;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biSizeImage = 16;
    unsigned char pixels[16] = {32, 160, 49, 32, 160, 49, 0, 0, 32, 160, 49, 32, 160, 49, 0, 0};
    FILE *out = _wfopen(path.c_str(), L"wb");
    if (!out)
        return false;
    bool ok = fwrite(&file, sizeof(file), 1, out) == 1 &&
              fwrite(&info, sizeof(info), 1, out) == 1 &&
              fwrite(pixels, sizeof(pixels), 1, out) == 1;
    fclose(out);
    return ok;
}
inline Wallpaper::Snapshot Fixture(const std::wstring &image) {
    Wallpaper::Snapshot value;
    for (int n = 0; n < 3; ++n) {
        Wallpaper::Monitor m;
        m.devicePath = L"test-device-" + std::to_wstring(n);
        m.source = L"test-source-" + std::to_wstring(n);
        m.manufacturer = L"TST";
        m.product = L"0001";
        m.serial = L"Serial-" + std::to_wstring(n);
        m.name = n == 0 ? L"测试屏幕 A" : n == 1 ? L"测试屏幕 B" : L"测试未连接屏幕";
        m.rect = {n * 640, 0, n * 640 + 640, 480};
        m.connected = n < 2;
        m.primary = n == 0;
        m.image = image;
        value.monitors.push_back(std::move(m));
    }
    Wallpaper::AssignIdentities(value.monitors);
    return value;
}
} // namespace WallpaperTest
