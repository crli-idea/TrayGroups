#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <map>

namespace GuidResearch {
// 只读核对真实工具栏状态。GetRect 在本机隐藏后仍可能返回缓存矩形，不能作为隐藏依据。
using ToolbarStates = std::map<std::pair<ULONG_PTR, UINT>, bool>;
namespace ToolbarDetail {
struct RemoteData {
    uint64_t owner;
    uint32_t id, callback, reserved, version;
    uint64_t icon;
};
static_assert(sizeof(RemoteData) == 32, "Expected x64 Explorer layout");
inline bool Send(HWND window, UINT message, WPARAM w, LPARAM l, DWORD_PTR &result) {
    return SendMessageTimeoutW(window, message, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200,
                               &result) != 0;
}
template <class T> bool Read(HANDLE process, const void *address, T &value) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, address, &value, sizeof(value), &read) &&
           read == sizeof(value);
}
inline BOOL CALLBACK ReadChild(HWND window, LPARAM context) {
    wchar_t cls[128] = {};
    GetClassNameW(window, cls, 128);
    if (wcscmp(cls, L"ToolbarWindow32"))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
        return TRUE;
    void *buffer = VirtualAllocEx(process, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD_PTR count = 0;
    if (buffer && Send(window, TB_BUTTONCOUNT, 0, 0, count) && count < 512) {
        auto &states = *reinterpret_cast<ToolbarStates *>(context);
        for (DWORD_PTR index = 0; index < count; ++index) {
            DWORD_PTR ok = 0;
            TBBUTTON button = {};
            RemoteData data = {};
            if (!Send(window, TB_GETBUTTON, index, (LPARAM)buffer, ok) || !ok ||
                !Read(process, buffer, button) || !button.dwData ||
                !Read(process, (void *)button.dwData, data) || !IsWindow((HWND)data.owner))
                continue;
            auto key = std::make_pair((ULONG_PTR)data.owner, data.id);
            bool hidden = (button.fsState & TBSTATE_HIDDEN) != 0;
            auto previous = states.find(key);
            if (previous == states.end())
                states[key] = hidden;
            else
                previous->second = previous->second && hidden;
        }
    }
    if (buffer)
        VirtualFreeEx(process, buffer, 0, MEM_RELEASE);
    CloseHandle(process);
    return TRUE;
}
inline BOOL CALLBACK ReadRoot(HWND window, LPARAM context) {
    wchar_t cls[128] = {};
    GetClassNameW(window, cls, 128);
    if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"NotifyIconOverflowWindow") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        EnumChildWindows(window, ReadChild, context);
    return TRUE;
}
} // namespace ToolbarDetail
inline ToolbarStates ReadToolbarStates() {
    ToolbarStates states;
    EnumWindows(ToolbarDetail::ReadRoot, (LPARAM)&states);
    return states;
}
inline bool HasToolbarState(HWND owner, UINT id, bool hidden) {
    auto states = ReadToolbarStates();
    auto item = states.find({(ULONG_PTR)owner, id});
    return item != states.end() && item->second == hidden;
}
} // namespace GuidResearch
