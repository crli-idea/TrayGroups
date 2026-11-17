#include "TrayNotifySnapshot.h"
#include "ToolbarState.h"
#include "../../src/common/TraySupport.h"
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <map>
#include <tuple>

namespace {
using namespace GuidResearch;
using TraySupport::Number;
std::wstring report;
unsigned checks = 0, failures = 0;
void SaveProgress() {
    auto path = TraySupport::ExePath();
    path.resize(path.find_last_of(L'\\'));
    TraySupport::WriteText(path + L"\\com-snapshot.txt", report);
}
LONG WINAPI FailureReport(EXCEPTION_POINTERS *exception) {
    HMODULE module = nullptr;
    auto address = exception->ExceptionRecord->ExceptionAddress;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(address), &module);
    wchar_t modulePath[32768] = {};
    GetModuleFileNameW(module, modulePath, 32768);
    wchar_t detail[256];
    swprintf_s(detail, L"exception=%08lx address=%p module=%p offset=%llx\n",
               exception->ExceptionRecord->ExceptionCode, address, module,
               (unsigned long long)((ULONG_PTR)address - (ULONG_PTR)module));
    report += detail;
    report += std::wstring(modulePath) + L"\n";
    SaveProgress();
    return EXCEPTION_EXECUTE_HANDLER;
}
void Check(bool ok, const wchar_t *description) {
    ++checks;
    failures += !ok;
    report += std::wstring(ok ? L"PASS " : L"FAIL ") + description + L"\n";
    SaveProgress();
}
void Pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 15, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (GetTickCount64() < end);
}
// 所有修改操作只允许使用此对象自己注册的图标；用户现有图标仅作只读对照。
class OwnIcons {
    std::vector<NOTIFYICONDATAW> icons;

  public:
    HWND owner = nullptr;
    OwnIcons(HINSTANCE instance) {
        WNDCLASSW cls = {};
        cls.hInstance = instance;
        cls.lpfnWndProc = DefWindowProcW;
        cls.lpszClassName = L"Crli.TrayGuidResearch.Owner";
        if (RegisterClassW(&cls))
            owner = CreateWindowW(cls.lpszClassName, L"GUID probe", WS_OVERLAPPED, 0, 0, 1, 1,
                                  nullptr, nullptr, instance, nullptr);
    }
    ~OwnIcons() {
        Clear();
    }
    bool Add(UINT id, const GUID *guid, bool hidden) {
        if (!owner)
            return false;
        NOTIFYICONDATAW icon = {};
        icon.cbSize = sizeof(icon);
        icon.hWnd = owner;
        icon.uID = id;
        icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_STATE;
        icon.uCallbackMessage = WM_APP + 1;
        icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        icon.dwStateMask = NIS_HIDDEN;
        icon.dwState = hidden ? NIS_HIDDEN : 0;
        wcscpy_s(icon.szTip, L"TrayGroups isolated GUID probe");
        if (guid) {
            icon.uFlags |= NIF_GUID;
            icon.guidItem = *guid;
        }
        if (!Shell_NotifyIconW(NIM_ADD, &icon))
            return false;
        icons.push_back(icon);
        return true;
    }
    bool SetHidden(const GUID &learned, bool hidden) {
        // 即使测试解析器出错，也不允许修改本测试之外的 GUID。
        bool own = false;
        for (const auto &icon : icons)
            if ((icon.uFlags & NIF_GUID) && IsEqualGUID(icon.guidItem, learned))
                own = true;
        if (!own)
            return false;
        NOTIFYICONDATAW icon = {};
        icon.cbSize = sizeof(icon);
        icon.guidItem = learned;
        icon.uFlags = NIF_GUID | NIF_STATE;
        icon.dwStateMask = NIS_HIDDEN;
        icon.dwState = hidden ? NIS_HIDDEN : 0;
        return Shell_NotifyIconW(NIM_MODIFY, &icon) != FALSE;
    }
    void Clear() {
        for (auto &icon : icons)
            Shell_NotifyIconW(NIM_DELETE, &icon);
        icons.clear();
        if (owner) {
            DestroyWindow(owner);
            owner = nullptr;
        }
    }
};
bool HasRect(const GUID &guid) {
    NOTIFYICONIDENTIFIER identifier = {};
    identifier.cbSize = sizeof(identifier);
    identifier.guidItem = guid;
    RECT rect = {};
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect));
}
Snapshot SnapshotReport(const wchar_t *name) {
    report += std::wstring(L"BEGIN ") + name + L"\n";
    SaveProgress();
    Snapshot snapshot = ReadSnapshot();
    report += std::wstring(name) + L": create=" + Hex(snapshot.created) + L" register=" +
              Hex(snapshot.registered) + L" unregister=" + Hex(snapshot.unregistered) +
              L" records=" + Number(snapshot.records.size()) + L" ms=" +
              Number(snapshot.elapsedMs) + L"\n";
    return snapshot;
}
using RuntimeKey = std::tuple<ULONG_PTR, UINT, DWORD, unsigned long long, std::wstring>;
std::map<RuntimeKey, DWORD> ActivePreferences(const Snapshot &snapshot) {
    std::map<RuntimeKey, DWORD> result;
    for (const auto &record : snapshot.records)
        if (record.owner && IsWindow(record.owner))
            result[{(ULONG_PTR)record.owner, record.id, record.pid, record.processStarted,
                    GuidText(record.guid)}] = record.preference;
    return result;
}
void SelfTest(HINSTANCE instance, const Snapshot &before) {
    const auto toolbarBefore = ReadToolbarStates();
    OwnIcons own(instance);
    GUID first = {}, second = {}, initiallyHidden = {};
    Check(SUCCEEDED(CoCreateGuid(&first)) && SUCCEEDED(CoCreateGuid(&second)) &&
              SUCCEEDED(CoCreateGuid(&initiallyHidden)),
          L"generate fresh random GUIDs");
    Check(own.owner && own.Add(4201, nullptr, false) && own.Add(4202, &first, false) &&
              own.Add(4203, &second, false) && own.Add(4204, &initiallyHidden, true),
          L"register four isolated icons");
    if (failures)
        return;
    Pump(200);
    std::wstring path = TraySupport::ExePath();
    auto visible = SnapshotReport(L"own-visible");
    Check(visible.Ok(), L"snapshot lifecycle succeeds");
    GUID learned = {}, learnedSecond = {}, none = {};
    bool found = FindLiveGuid(visible, own.owner, 4202, path, learned);
    Check(found && IsEqualGUID(learned, first), L"learn first random GUID without lookup table");
    Check(FindLiveGuid(visible, own.owner, 4203, path, learnedSecond) &&
              IsEqualGUID(learnedSecond, second) && !IsEqualGUID(learned, learnedSecond),
          L"distinguish two GUID icons from same process and window");
    bool ordinaryPresent = false;
    for (const auto &record : visible.records)
        if (record.owner == own.owner && record.id == 4201 && IsEqualGUID(record.guid, none))
            ordinaryPresent = true;
    Check(ordinaryPresent && !FindLiveGuid(visible, own.owner, 4201, path, none),
          L"ordinary registration has no GUID; do not invent one");
    Check(!FindLiveGuid(visible, nullptr, 4202, path, none) &&
              !FindLiveGuid(visible, own.owner, 4202, path + L".wrong", none),
          L"reject historical owner and wrong executable");
    Snapshot conflicting = visible;
    for (const auto &record : visible.records)
        if (record.owner == own.owner && record.id == 4202) {
            auto duplicate = record;
            duplicate.guid = second;
            conflicting.records.push_back(duplicate);
            break;
        }
    Check(!FindLiveGuid(conflicting, own.owner, 4202, path, none),
          L"reject conflicting GUID records");
    Snapshot expired = visible;
    for (auto &record : expired.records)
        ++record.processStarted;
    Check(!FindLiveGuid(expired, own.owner, 4202, path, none), L"reject expired process identity");
    PWSTR windows = nullptr;
    bool normalized = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Windows, 0, nullptr, &windows));
    if (normalized) {
        normalized = NormalizeExecutable(GuidText(FOLDERID_Windows) + L"\\probe.exe") ==
                     TraySupport::Lower(std::wstring(windows) + L"\\probe.exe");
        CoTaskMemFree(windows);
    }
    Check(normalized, L"expand Known Folder path without program-specific mapping");
    Check(HasToolbarState(own.owner, 4202, false) && HasRect(first),
          L"visible GUID icon has visible toolbar state and rectangle");
    bool hidden = found && own.SetHidden(learned, true);
    Check(hidden, L"hide only own icon using discovered GUID");
    Pump(200);
    auto whileHidden = SnapshotReport(L"own-hidden");
    Check(whileHidden.Ok() && HasToolbarState(own.owner, 4202, true),
          L"GUID hide sets actual toolbar hidden state");
    bool hiddenFound = FindLiveGuid(whileHidden, own.owner, 4202, path, none);
    bool initiallyFound = FindLiveGuid(whileHidden, own.owner, 4204, path, none);
    report += L"OBSERVATION fresh-snapshot finds hidden=" + Number(hiddenFound) +
              L" initially-hidden=" + Number(initiallyFound) + L" hidden-GetRect-still-succeeds=" +
              Number(HasRect(first)) + L"\n";
    Check(found && own.SetHidden(learned, false), L"restore own icon using pre-hide GUID");
    Pump(200);
    auto restored = SnapshotReport(L"own-restored");
    Check(HasToolbarState(own.owner, 4202, false) && HasRect(first) &&
              FindLiveGuid(restored, own.owner, 4202, path, none) && IsEqualGUID(none, first),
          L"restored icon regains rectangle and original GUID");
    HWND oldOwner = own.owner;
    own.Clear();
    Pump(150);
    auto after = SnapshotReport(L"own-cleaned");
    Check(after.Ok() && !HasRect(first) && !HasRect(second) && !HasRect(initiallyHidden) &&
              !FindLiveGuid(visible, oldOwner, 4202, path, none),
          L"cleanup own icons; reject old owner and cached snapshot");
    auto previous = ActivePreferences(before), current = ActivePreferences(after);
    size_t compared = 0, changed = 0, missing = 0;
    for (const auto &pair : previous) {
        auto match = current.find(pair.first);
        if (match == current.end()) {
            ++missing;
            continue;
        }
        ++compared;
        changed += match->second != pair.second;
    }
    report += L"OBSERVATION preexisting compared=" + Number(compared) + L" missing=" +
              Number(missing) + L" preference-changed=" + Number(changed) + L"\n";
    Check(after.Ok() && compared > 0 && changed == 0 && missing == 0,
          L"preexisting active icon identities and preferences remain unchanged");
    Check(!toolbarBefore.empty() && ReadToolbarStates() == toolbarBefore,
          L"all preexisting toolbar icon hidden states remain unchanged");
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR commandLine, int) {
    SetUnhandledExceptionFilter(FailureReport);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    report = L"Explorer GUID discovery; isolated tests of production resolver\nCOM=" +
             Hex(initialized) + L"\n";
    report += L"arguments=[" + std::wstring(commandLine) + L"]\n";
    if (SUCCEEDED(initialized)) {
        {
            auto baseline = SnapshotReport(L"baseline");
            if (TraySupport::Trim(commandLine) == L"--self-test") {
                Check(baseline.Ok(), L"baseline read-only snapshot succeeds");
                if (baseline.Ok())
                    SelfTest(instance, baseline);
            } else {
                Check(baseline.Ok(), L"read-only snapshot succeeds");
                size_t historical = 0;
                for (const auto &record : baseline.records) {
                    if (!record.owner || !IsWindow(record.owner)) {
                        ++historical;
                        continue;
                    }
                    report += L"owner=" + Number((ULONG_PTR)record.owner) + L" id=" +
                              Number(record.id) + L" guid=" + GuidText(record.guid) + L" exe=" +
                              TraySupport::Escape(NormalizeExecutable(record.executable)) + L"\n";
                }
                report += L"historical/inactive=" + Number(historical) + L"\n";
            }
        }
        CoUninitialize();
    } else
        Check(false, L"COM initialization succeeds");
    report += L"checks=" + Number(checks) + L" failures=" + Number(failures) + L"\n";
    auto path = TraySupport::ExePath();
    path.resize(path.find_last_of(L'\\'));
    bool saved = TraySupport::WriteText(path + L"\\com-snapshot.txt", report);
    return saved && failures == 0 ? 0 : 1;
}
