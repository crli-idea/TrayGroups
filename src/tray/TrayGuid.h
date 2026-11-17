#pragma once
#include "TrayTypes.h"
#include <vector>

// Explorer 通知快照只在一次扫描中有效。真实 GUID 来自注册信息，不按程序名称推测。
namespace TrayGuid {
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
// 同线程初始化/释放 STA COM，只复制 RegisterCallback 的同步快照，不额外启动线程或泵消息。
Snapshot ReadSnapshot();
std::wstring NormalizeExecutable(const std::wstring &executable);
bool FindLiveAddress(const Snapshot &snapshot, HWND owner, UINT id, const std::wstring &executable,
                     GUID &guid, bool &usesGuid);
bool FindLiveGuid(const Snapshot &snapshot, HWND owner, UINT id, const std::wstring &executable,
                  GUID &guid);
void Resolve(std::vector<TrayItem> &items);
std::wstring Hex(HRESULT result);
std::wstring GuidText(const GUID &guid);
} // namespace TrayGuid
