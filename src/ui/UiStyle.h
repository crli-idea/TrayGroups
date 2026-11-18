#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

// 保留系统控件绘制，只统一 DPI 字体、图标和资源释放。
namespace UiStyle {
inline int Scale(HWND window, int value) {
    return MulDiv(value, GetDpiForWindow(window), 96);
}
inline void FitClient(HWND window, int width, int height) {
    RECT rect = {0, 0, Scale(window, width), Scale(window, height)};
    AdjustWindowRectExForDpi(&rect, GetWindowLongW(window, GWL_STYLE), FALSE,
                             GetWindowLongW(window, GWL_EXSTYLE), GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}
inline constexpr UINT_PTR WindowSubclass = 0x54475549;
struct WindowState {
    HFONT font = nullptr;
};
inline LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR data) {
    if (message == WM_NCDESTROY) {
        auto *state = reinterpret_cast<WindowState *>(data);
        RemoveWindowSubclass(window, WindowProc, id);
        auto result = DefSubclassProc(window, message, w, l);
        if (state->font)
            DeleteObject(state->font);
        delete state;
        return result;
    }
    return DefSubclassProc(window, message, w, l);
}
inline void StyleChildren(HWND window) {
    auto font = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (font)
            SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, 64);
        if (!wcscmp(cls, WC_LISTVIEWW))
            SetWindowTheme(child, L"Explorer", nullptr);
    }
}
inline void Attach(HWND window) {
    DWORD_PTR previous = 0;
    if (!window || GetWindowSubclass(window, WindowProc, WindowSubclass, &previous))
        return;
    auto *state = new WindowState;
    auto font = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
    if (!font) {
        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                                   GetDpiForWindow(window));
        state->font = CreateFontIndirectW(&metrics.lfMessageFont);
        font = state->font;
        SendMessageW(window, WM_SETFONT, (WPARAM)font, FALSE);
    }
    if (!SetWindowSubclass(window, WindowProc, WindowSubclass, (DWORD_PTR)state)) {
        if (state->font)
            DeleteObject(state->font);
        delete state;
        return;
    }
    HWND owner = GetWindow(window, GW_OWNER);
    if (owner)
        for (int size : {ICON_BIG, ICON_SMALL}) {
            auto icon = SendMessageW(owner, WM_GETICON, size, 0);
            if (icon)
                SendMessageW(window, WM_SETICON, size, icon);
        }
    StyleChildren(window);
}
} // namespace UiStyle
