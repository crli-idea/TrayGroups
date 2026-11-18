#include "TrayGuid.h"
#include "../common/TraySupport.h"
#include <objbase.h>
#include <shlobj.h>

namespace TrayGuid {
// COM 对象由进程外实现，接口类型不能放匿名命名空间，否则 GCC 可能按本地类型去虚化。
namespace Detail {
// 这些是 Explorer 服务/接口的标识，不是任何托盘图标的 GUID。
constexpr GUID TrayNotifyClass = {
    0x25dead04, 0x1eac, 0x4911, {0x9e, 0x3a, 0xad, 0x0a, 0x4a, 0xb5, 0x60, 0xfd}};
constexpr GUID NotifyCallbackId = {
    0xd782ccba, 0xafb0, 0x43f1, {0x94, 0xdb, 0xfd, 0xa3, 0x77, 0x9e, 0xac, 0xcb}};
constexpr GUID TrayNotify8Id = {
    0xd133ce13, 0x3537, 0x48ba, {0x93, 0xa7, 0xaf, 0xcd, 0x5d, 0x20, 0x53, 0xb4}};
struct NotifyItem {
    PWSTR executable;
    PWSTR tip;
    HICON image;
    HWND owner;
    DWORD preference;
    UINT id;
    GUID guid;
};
static_assert(sizeof(NotifyItem) == 56, "Research probe expects x64 notification layout");
struct NotifyCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Notify(ULONG event, const NotifyItem *item) = 0;
};
// 仅声明使用到的 vtable 前缀；不调用 SetPreference 等修改状态的方法。
struct TrayNotify : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RegisterCallback(NotifyCallback *callback, ULONG *cookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterCallback(ULONG cookie) = 0;
};
unsigned long long ProcessStarted(DWORD pid) {
    NativeHandles::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
        return 0;
    FILETIME created, exited, kernel, user;
    bool ok = GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
    return ok ? ((unsigned long long)created.dwHighDateTime << 32) | created.dwLowDateTime : 0;
}
class Callback final : public NotifyCallback {
    LONG references = 1;

  public:
    std::vector<Record> records;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, NotifyCallbackId)) {
            *out = static_cast<NotifyCallback *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&references);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = InterlockedDecrement(&references);
        if (!left)
            delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Notify(ULONG event, const NotifyItem *item) override {
        if (!item)
            return E_POINTER;
        // 指针仅在回调期间有效，复制数据；HICON 借用且不释放。
        Record record;
        record.event = event;
        record.executable = item->executable ? item->executable : L"";
        record.tip = item->tip ? item->tip : L"";
        record.owner = item->owner;
        record.id = item->id;
        record.guid = item->guid;
        record.preference = item->preference;
        if (record.owner && IsWindow(record.owner)) {
            GetWindowThreadProcessId(record.owner, &record.pid);
            record.processStarted = ProcessStarted(record.pid);
        }
        records.push_back(std::move(record));
        return S_OK;
    }
};
} // namespace Detail
using namespace Detail;
bool Snapshot::Ok() const {
    return SUCCEEDED(created) && SUCCEEDED(registered) && SUCCEEDED(unregistered);
}
Snapshot ReadSnapshot() {
    Snapshot snapshot;
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        snapshot.created = initialized;
        return snapshot;
    }
    ULONGLONG started = GetTickCount64();
    TrayNotify *service = nullptr;
    snapshot.created = CoCreateInstance(TrayNotifyClass, nullptr, CLSCTX_LOCAL_SERVER,
                                        TrayNotify8Id, (void **)&service);
    if (SUCCEEDED(snapshot.created)) {
        Callback *callback = new Callback;
        snapshot.registered = service->RegisterCallback(callback, &snapshot.cookie);
        if (SUCCEEDED(snapshot.registered)) {
            snapshot.unregistered = service->UnregisterCallback(snapshot.cookie);
        }
        snapshot.records = std::move(callback->records);
        callback->Release();
        service->Release();
    }
    snapshot.elapsedMs = GetTickCount64() - started;
    CoUninitialize();
    return snapshot;
}
std::wstring NormalizeExecutable(const std::wstring &executable) {
    std::wstring path = executable;
    if (!path.empty() && path.front() == L'{') {
        size_t end = path.find(L'}');
        if (end == std::wstring::npos)
            return L"";
        GUID folder = {};
        PWSTR resolved = nullptr;
        if (FAILED(CLSIDFromString(path.substr(0, end + 1).c_str(), &folder)) ||
            FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &resolved)))
            return L"";
        path = std::wstring(resolved) + path.substr(end + 1);
        CoTaskMemFree(resolved);
    }
    return TraySupport::Lower(path);
}
bool FindLiveAddress(const Snapshot &snapshot, HWND owner, UINT id, const std::wstring &executable,
                     GUID &guid, bool &usesGuid) {
    guid = {};
    usesGuid = false;
    if (!snapshot.Ok() || !owner || !IsWindow(owner) || executable.empty())
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    auto started = ProcessStarted(pid);
    if (!started)
        return false;
    NativeHandles::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
        return false;
    wchar_t realPath[32768];
    DWORD length = 32768;
    bool queried = QueryFullProcessImageNameW(process, 0, realPath, &length) != FALSE;
    auto expected = TraySupport::Lower(executable);
    if (!queried || TraySupport::Lower(std::wstring(realPath, length)) != expected)
        return false;
    GUID empty = {}, candidate = {};
    bool found = false;
    for (const auto &record : snapshot.records) {
        if (record.owner != owner || record.id != id || record.pid != pid ||
            record.processStarted != started || NormalizeExecutable(record.executable) != expected)
            continue;
        if (found && !IsEqualGUID(candidate, record.guid))
            return false;
        candidate = record.guid;
        found = true;
    }
    DWORD stillPid = 0;
    GetWindowThreadProcessId(owner, &stillPid);
    if (!found || stillPid != pid || ProcessStarted(pid) != started)
        return false;
    guid = candidate;
    usesGuid = !IsEqualGUID(candidate, empty);
    return true;
}
bool FindLiveGuid(const Snapshot &snapshot, HWND owner, UINT id, const std::wstring &executable,
                  GUID &guid) {
    bool usesGuid = false;
    return FindLiveAddress(snapshot, owner, id, executable, guid, usesGuid) && usesGuid;
}
void Resolve(std::vector<TrayItem> &items) {
    if (items.empty())
        return;
    auto snapshot = ReadSnapshot();
    for (auto &item : items)
        item.addressKnown =
            FindLiveAddress(snapshot, item.owner, item.id, item.path, item.guid, item.usesGuid);
}
std::wstring Hex(HRESULT result) {
    wchar_t text[16];
    swprintf_s(text, L"0x%08lx", (unsigned long)result);
    return text;
}
std::wstring GuidText(const GUID &guid) {
    wchar_t text[40];
    StringFromGUID2(guid, text, 40);
    return text;
}
} // namespace TrayGuid
