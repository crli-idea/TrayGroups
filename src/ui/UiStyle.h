#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "../common/NativeHandles.h"

// 共用轻量原生样式：使用系统控件、库存画刷/画笔，不创建常驻线程或整窗位图缓存。
namespace UiStyle {
inline bool highContrast = false;
inline COLORREF Paper() {
    return highContrast ? GetSysColor(COLOR_WINDOW) : RGB(255, 255, 255);
}
inline COLORREF Canvas() {
    return highContrast ? GetSysColor(COLOR_WINDOW) : RGB(246, 248, 251);
}
inline COLORREF Ink() {
    return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(32, 44, 62);
}
inline COLORREF Muted() {
    return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(103, 116, 135);
}
inline COLORREF Line() {
    return highContrast ? GetSysColor(COLOR_WINDOWTEXT) : RGB(221, 227, 235);
}
inline COLORREF Accent() {
    return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(40, 100, 211);
}
inline COLORREF Tint() {
    return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : RGB(234, 241, 254);
}
inline int Scale(HWND window, int value) {
    return MulDiv(value, GetDpiForWindow(window), 96);
}
inline void Fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
}
inline void Card(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius = 7) {
    NativeHandles::Selection brush(dc, GetStockObject(DC_BRUSH));
    NativeHandles::Selection pen(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
}
inline void RefreshContrast() {
    HIGHCONTRASTW contrast = {};
    contrast.cbSize = sizeof(contrast);
    highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                   (contrast.dwFlags & HCF_HIGHCONTRASTON);
}
inline constexpr UINT_PTR WindowSubclass = 0x54475549, ButtonSubclass = 0x54474254;
struct WindowState {
    int primary, title, subtitle;
    COLORREF accent = CLR_INVALID;
    HFONT normal = nullptr, heading = nullptr;
    bool ownsNormal = false;
    ~WindowState() {
        if (heading)
            DeleteObject(heading);
        if (ownsNormal)
            DeleteObject(normal);
    }
};
inline LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
inline COLORREF WindowAccent(HWND window) {
    if (highContrast)
        return Accent();
    for (HWND current = window; current; current = GetParent(current)) {
        DWORD_PTR value = 0;
        if (GetWindowSubclass(current, WindowProc, WindowSubclass, &value)) {
            auto *state = reinterpret_cast<WindowState *>(value);
            if (state->accent != CLR_INVALID)
                return state->accent;
        }
    }
    return Accent();
}
inline HFONT Font(HWND window, int points, int weight) {
    return CreateFontW(-MulDiv(points, GetDpiForWindow(window), 72), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
}
inline LRESULT CALLBACK ButtonProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR flags) {
    if (message == WM_MOUSEMOVE && !(flags & 2)) {
        SetWindowSubclass(window, ButtonProc, id, flags | 2);
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE) {
        SetWindowSubclass(window, ButtonProc, id, flags & ~DWORD_PTR(2));
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ButtonProc, id);
    return DefSubclassProc(window, message, w, l);
}
inline bool PaintButton(const DRAWITEMSTRUCT &item) {
    if (item.CtlType != ODT_BUTTON)
        return false;
    DWORD_PTR flags = 0;
    GetWindowSubclass(item.hwndItem, ButtonProc, ButtonSubclass, &flags);
    bool primary = flags & 1, hover = flags & 2, pressed = item.itemState & ODS_SELECTED;
    bool disabled = item.itemState & ODS_DISABLED;
    COLORREF accent = WindowAccent(item.hwndItem);
    COLORREF fill = primary ? accent : hover ? Canvas() : Paper();
    if (pressed)
        fill = primary ? accent : Tint();
    if (disabled)
        fill = Canvas();
    Fill(item.hDC, item.rcItem, Paper());
    RECT rect = item.rcItem;
    Card(item.hDC, rect, fill, primary && !disabled ? fill : Line(), Scale(item.hwndItem, 8));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC,
                 disabled  ? Muted()
                 : primary ? (highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(255, 255, 255))
                           : Ink());
    wchar_t text[256] = {};
    GetWindowTextW(item.hwndItem, text, 256);
    HFONT font = (HFONT)SendMessageW(item.hwndItem, WM_GETFONT, 0, 0);
    NativeHandles::Selection selected(item.hDC, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    if (pressed)
        OffsetRect(&rect, 0, 1);
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        InflateRect(&rect, -4, -4);
        DrawFocusRect(item.hDC, &rect);
    }
    return true;
}
inline LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                   DWORD_PTR value) {
    auto *state = reinterpret_cast<WindowState *>(value);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, WindowProc, id);
        auto result = DefSubclassProc(window, message, w, l);
        delete state;
        return result;
    }
    if (message == WM_SYSCOLORCHANGE)
        RefreshContrast();
    if (message == WM_DRAWITEM && PaintButton(*reinterpret_cast<DRAWITEMSTRUCT *>(l)))
        return TRUE;
    if (highContrast)
        return DefSubclassProc(window, message, w, l);
    if (message == WM_ERASEBKGND) {
        RECT rect;
        GetClientRect(window, &rect);
        Fill((HDC)w, rect, Paper());
        return 1;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX) {
        HDC dc = (HDC)w;
        bool muted = message == WM_CTLCOLORSTATIC && GetDlgCtrlID((HWND)l) == state->subtitle;
        SetTextColor(dc, muted ? Muted() : Ink());
        SetBkColor(dc, Paper());
        SetDCBrushColor(dc, Paper());
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    if (message == WM_NOTIFY) {
        auto *header = reinterpret_cast<NMHDR *>(l);
        wchar_t cls[64] = {};
        GetClassNameW(header->hwndFrom, cls, 64);
        if (header->code == NM_CUSTOMDRAW && !wcscmp(cls, WC_LISTVIEWW)) {
            auto *draw = reinterpret_cast<NMLVCUSTOMDRAW *>(l);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                if (!(draw->nmcd.uItemState & CDIS_SELECTED)) {
                    draw->clrText = Ink();
                    draw->clrTextBk = draw->nmcd.dwItemSpec % 2 ? Canvas() : Paper();
                }
                return CDRF_DODEFAULT;
            }
        }
    }
    return DefSubclassProc(window, message, w, l);
}
inline HFONT HeadingFont(HWND window) {
    DWORD_PTR value = 0;
    if (GetWindowSubclass(window, WindowProc, WindowSubclass, &value)) {
        auto *state = reinterpret_cast<WindowState *>(value);
        return state->heading ? state->heading : state->normal;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
inline void StyleChildren(HWND window) {
    DWORD_PTR value = 0;
    if (!GetWindowSubclass(window, WindowProc, WindowSubclass, &value))
        return;
    auto &state = *reinterpret_cast<WindowState *>(value);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        int control = GetDlgCtrlID(child);
        SendMessageW(
            child, WM_SETFONT,
            (WPARAM)(control == state.title && state.heading ? state.heading : state.normal), TRUE);
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, 64);
        if (!highContrast && !wcscmp(cls, L"Button")) {
            LONG_PTR type = GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK;
            if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW) {
                SendMessageW(child, BM_SETSTYLE, BS_OWNERDRAW, TRUE);
                SetWindowSubclass(child, ButtonProc, ButtonSubclass,
                                  control == state.primary ? 1 : 0);
            }
        } else if (!wcscmp(cls, WC_LISTVIEWW)) {
            SetWindowTheme(child, L"Explorer", nullptr);
            ListView_SetBkColor(child, Paper());
            ListView_SetTextBkColor(child, CLR_NONE);
        }
    }
    wchar_t cls[32] = {};
    GetClassNameW(window, cls, 32);
    if (state.primary && !wcscmp(cls, L"#32770"))
        SendMessageW(window, DM_SETDEFID, state.primary, 0);
}
inline void Attach(HWND window, int primary = 0, int title = -2, int subtitle = -2,
                   COLORREF accent = CLR_INVALID) {
    DWORD_PTR previous = 0;
    if (!window || GetWindowSubclass(window, WindowProc, WindowSubclass, &previous))
        return;
    RefreshContrast();
    auto *state = new WindowState{primary, title, subtitle};
    state->accent = accent;
    state->normal = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
    if (!state->normal) {
        state->normal = Font(window, 9, FW_NORMAL);
        state->ownsNormal = true;
    }
    if (title >= 0)
        state->heading = Font(window, title == 0 ? 11 : 14, FW_SEMIBOLD);
    if (!SetWindowSubclass(window, WindowProc, WindowSubclass, (DWORD_PTR)state)) {
        delete state;
        return;
    }
    if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) {
        HWND owner = GetWindow(window, GW_OWNER);
        if (owner) {
            for (int size : {ICON_BIG, ICON_SMALL}) {
                HICON icon = (HICON)SendMessageW(owner, WM_GETICON, size, 0);
                if (!icon)
                    icon = (HICON)GetClassLongPtrW(owner,
                                                   size == ICON_BIG ? GCLP_HICON : GCLP_HICONSM);
                if (!icon)
                    icon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
                if (icon)
                    SendMessageW(window, WM_SETICON, size, (LPARAM)icon);
            }
        }
    }
    StyleChildren(window);
}
inline void FitClient(HWND window, int width, int height) {
    RECT rect = {0, 0, Scale(window, width), Scale(window, height)};
    AdjustWindowRectExForDpi(&rect, (DWORD)GetWindowLongPtrW(window, GWL_STYLE), FALSE,
                             (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE),
                             GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}
} // namespace UiStyle
