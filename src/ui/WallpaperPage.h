#pragma once
#include "../wallpaper/DesktopWallpaper.h"

namespace WallpaperPage {
void Initialize(HWND page, const std::wstring &directory);
bool Apply(HWND page, std::wstring &error);
// 测试与热插拔使用同一条刷新路径；刷新保留仍连接且身份相同的未应用图片。
void Refresh(HWND page);
INT_PTR Handle(HWND page, UINT message, WPARAM w, LPARAM l);
#ifdef NATIVE_TEST_BUILD
extern std::wstring (*testBrowse)(HWND, const std::wstring &);
#endif
} // namespace WallpaperPage
