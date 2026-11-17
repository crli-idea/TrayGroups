#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// 持久分组规则与运行时身份分开：PID、进程启动时间、窗口及编号只用于本次生命周期核验。
// TrayItem::image 是拥有的 HICON 副本；扫描结果必须交给 FreeItems 释放，不能重复释放浅拷贝。
struct RuntimeIdentity {
    DWORD pid = 0;
    uint64_t started = 0;
    HWND owner = nullptr;
    UINT id = 0;
    bool operator==(const RuntimeIdentity &other) const {
        return pid == other.pid && started == other.started && owner == other.owner &&
               id == other.id;
    }
    bool Valid() const {
        return pid && started && owner;
    }
};
struct TrayItem {
    HWND toolbar = nullptr, owner = nullptr;
    UINT id = 0, callback = 0, version = 0;
    int command = 0;
    BYTE state = 0;
    std::wstring process, path, tip, key, group, ownerClass;
    HICON image = nullptr;
    bool fallbackImage = false;
    RuntimeIdentity runtime;
    GUID guid = {};
    bool usesGuid = false;
    bool addressKnown = false; // 本次 COM 快照或已落盘的隐藏记录确认了寻址方式。
    bool pending = false;
    std::wstring matchStatus;
};
struct Hidden {
    HWND toolbar = nullptr, owner = nullptr;
    UINT id = 0;
    int command = 0;
    RuntimeIdentity runtime;
    bool originalHidden = false;
    std::wstring path, ownerClass;
    UINT callback = 0;
    GUID guid = {};
    bool usesGuid = false;
};
