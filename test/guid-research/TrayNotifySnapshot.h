#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 实验接口只读取元数据；不导出修改偏好或操作用户图标的能力。
namespace GuidResearch {
struct Record {
    ULONG event = 0;
    std::wstring executable, tip;
    HWND owner = nullptr;
    UINT id = 0;
    GUID guid = {};
    DWORD preference = 0, pid = 0;
    unsigned long long processStarted = 0;
};
struct Snapshot {
    HRESULT created = E_FAIL, registered = E_FAIL, unregistered = E_FAIL;
    ULONG cookie = 0;
    ULONGLONG elapsedMs = 0;
    std::vector<Record> records;
    bool Ok() const;
};

// 调用者先初始化 STA COM；注册、复制快照、注销均在同一线程完成。
Snapshot ReadSnapshot();
// COM 返回的路径可能以 Known Folder GUID 开头，通用展开，不按程序名特判。
std::wstring NormalizeExecutable(const std::wstring &executable);
// 仅接受路径、窗口、编号及进程生命周期均吻合且没有 GUID 歧义的活动记录。
bool FindLiveGuid(const Snapshot &snapshot, HWND owner, UINT id, const std::wstring &executable,
                  GUID &guid);
std::wstring Hex(HRESULT result);
std::wstring GuidText(const GUID &guid);
} // namespace GuidResearch
