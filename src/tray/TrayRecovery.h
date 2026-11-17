// 主程序私有恢复实现。依赖托盘扫描/可见状态操作，由 app/TrayGroups.cpp 内部包含。
// 只恢复本程序认领的隐藏操作，不批量显示 Windows 原本隐藏的图标。
#pragma once
#include <cerrno>

static bool recoveryHealthy = true;
static unsigned lastRestoredCount = 0;
static HANDLE recoveryGuard = nullptr;
static std::wstring legacyRecoveryPath;

static bool RecoveryNumber(const std::wstring &text, uint64_t &value) {
    if (text.empty() || text[0] == L'-')
        return false;
    wchar_t *end = nullptr;
    errno = 0;
    value = wcstoull(text.c_str(), &end, 10);
    return errno != ERANGE && end && *end == 0;
}
static bool ReadRecoveryFile(const std::wstring &path, std::vector<Hidden> &records) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    std::wstring text = Trim(ReadText(path));
    if (!text.empty() && text[0] == 0xfeff)
        text.erase(0, 1);
    if (text.empty())
        return false;
    bool ini = text[0] == L'[' || text[0] == L';';
    std::wstring section;
    Hidden record;
    unsigned fields = 0;
    bool version = false;
    auto finish = [&]() {
        if (section.find(L"icon") != 0)
            return true;
        if (fields != 31 || !record.runtime.Valid())
            return false;
        records.push_back(record);
        return true;
    };
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = Trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;
        if (!ini) {
            std::vector<uint64_t> values;
            size_t start = 0;
            while (start < line.size()) {
                size_t tab = line.find(L'\t', start);
                uint64_t value = 0;
                if (!RecoveryNumber(
                        line.substr(start, tab == std::wstring::npos ? tab : tab - start), value))
                    return false;
                values.push_back(value);
                if (tab == std::wstring::npos)
                    break;
                start = tab + 1;
            }
            // Old two-column records cannot prove the original process lifetime.
            if (values.size() < 4 || values[2] > MAXDWORD || values[1] > MAXDWORD)
                return false;
            Hidden old;
            old.owner = (HWND)(ULONG_PTR)values[0];
            old.id = (UINT)values[1];
            old.runtime = {(DWORD)values[2], values[3], old.owner, old.id};
            if (values.size() > 4)
                old.toolbar = (HWND)(ULONG_PTR)values[4];
            if (!old.runtime.Valid())
                return false;
            records.push_back(old);
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            if (!finish())
                return false;
            section = Lower(line.substr(1, line.size() - 2));
            record = Hidden{};
            fields = 0;
            continue;
        }
        size_t equals = line.find(L'=');
        if (equals == std::wstring::npos)
            return false;
        std::wstring key = Lower(Trim(line.substr(0, equals))),
                     value = Trim(line.substr(equals + 1));
        if (section == L"recovery") {
            if (key == L"version")
                version = value == L"2";
            continue;
        }
        if (section.find(L"icon") != 0)
            return false;
        if (key == L"path") {
            record.path = value;
            continue;
        }
        if (key == L"class") {
            record.ownerClass = value;
            continue;
        }
        uint64_t number = 0;
        if (!RecoveryNumber(value, number))
            return false;
        if (key == L"processid") {
            if (number > MAXDWORD)
                return false;
            record.runtime.pid = (DWORD)number;
            fields |= 1;
        } else if (key == L"processstarted") {
            record.runtime.started = number;
            fields |= 2;
        } else if (key == L"ownerwindow") {
            record.owner = record.runtime.owner = (HWND)(ULONG_PTR)number;
            fields |= 4;
        } else if (key == L"iconid") {
            if (number > MAXDWORD)
                return false;
            record.id = record.runtime.id = (UINT)number;
            fields |= 8;
        } else if (key == L"washidden") {
            if (number > 1)
                return false;
            record.originalHidden = number != 0;
            fields |= 16;
        } else if (key == L"toolbar")
            record.toolbar = (HWND)(ULONG_PTR)number;
        else if (key == L"callback")
            record.callback = (UINT)number;
    }
    return ini ? version && finish() : true;
}
static void MergeRecovery(const std::vector<Hidden> &records) {
    for (const auto &record : records) {
        auto found = std::find_if(hidden.begin(), hidden.end(), [&](const Hidden &other) {
            return other.runtime == record.runtime;
        });
        if (found == hidden.end())
            hidden.push_back(record);
    }
}
static bool ImportRecovery() {
    // 磁盘记录是恢复依据，刷新前也要合并；内存为空不能视为已经恢复成功。
    std::vector<Hidden> records;
    recoveryHealthy = ReadRecoveryFile(recoveryPath, records);
    if (recoveryHealthy && !legacyRecoveryPath.empty() && legacyRecoveryPath != recoveryPath)
        recoveryHealthy = ReadRecoveryFile(legacyRecoveryPath, records);
    if (!recoveryHealthy) {
        LogError(L"恢复记录无法完整读取，已保留文件并暂停新的隐藏操作");
        return false;
    }
    MergeRecovery(records);
    return true;
}
static bool WriteRecovery() {
    // 先落盘再隐藏；临时文件 + FlushFileBuffers + 原子替换避免留下半份正式记录。
    if (hidden.empty()) {
        bool ok = DeleteFileW(recoveryPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        if (ok && !legacyRecoveryPath.empty() && legacyRecoveryPath != recoveryPath)
            ok = DeleteFileW(legacyRecoveryPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
        recoveryHealthy = ok;
        if (!ok)
            LogError(L"恢复记录无法清理，文件已保留");
        return ok;
    }
    std::wstring text = L"; TrayGroups recovery ownership journal. Do not delete while icons are "
                        L"hidden.\r\n[Recovery]\r\nVersion=2\r\n";
    for (size_t n = 0; n < hidden.size(); n++) {
        const auto &h = hidden[n];
        text += L"\r\n[Icon" + Number(n + 1) + L"]\r\nPath=" + Escape(h.path) + L"\r\nClass=" +
                Escape(h.ownerClass) + L"\r\nProcessId=" + Number(h.runtime.pid) +
                L"\r\nProcessStarted=" + Number(h.runtime.started) + L"\r\nOwnerWindow=" +
                Number((ULONG_PTR)h.owner) + L"\r\nIconId=" + Number(h.id) + L"\r\nCallback=" +
                Number(h.callback) + L"\r\nToolbar=" + Number((ULONG_PTR)h.toolbar) +
                L"\r\nWasHidden=" + Number(h.originalHidden) + L"\r\n";
    }
    std::wstring temporary = recoveryPath + L".tmp";
    auto bytes = Utf8(text);
    DWORD written = 0;
    HANDLE file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    bool ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) &&
             written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
    }
    ok = ok && MoveFileExW(temporary.c_str(), recoveryPath.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (ok && !legacyRecoveryPath.empty() && legacyRecoveryPath != recoveryPath &&
        GetFileAttributesW(legacyRecoveryPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        ok = DeleteFileW(legacyRecoveryPath.c_str()) != FALSE;
    recoveryHealthy = ok;
    if (!ok)
        LogError(L"恢复记录落盘失败，已暂停新的隐藏操作");
    return ok;
}
static bool RuntimeAlive(const RuntimeIdentity &identity) {
    if (!identity.Valid() || !IsWindow(identity.owner))
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(identity.owner, &pid);
    if (pid != identity.pid)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return true;
    uint64_t started = ProcessStart(process);
    CloseHandle(process);
    return !started || started == identity.started;
}
static bool RecoveryMatches(const Hidden &record, const TrayItem &item) {
    return record.runtime == item.runtime &&
           (record.path.empty() || Lower(record.path) == Lower(item.path)) &&
           (record.ownerClass.empty() ||
            StableClass(record.ownerClass) == StableClass(item.ownerClass)) &&
           (!record.callback || record.callback == item.callback);
}
static bool RecoveryStillHidden(const Hidden &record, const std::vector<TrayItem> &live) {
    bool found = false;
    for (const auto &item : live)
        if (RecoveryMatches(record, item)) {
            found = true;
            if (item.state & TBSTATE_HIDDEN)
                return true;
        }
    return !found && RuntimeAlive(record.runtime);
}
static void RestoreHidden() {
    // 请求恢复后重新扫描核验。失败或尚未枚举到的存活图标必须继续保留记录。
    lastRestoredCount = 0;
    if (!ImportRecovery())
        return;
    if (hidden.empty())
        return;
    auto current = ReadTrayLocations();
    for (const auto &record : hidden) {
        if (record.originalHidden)
            continue; // This icon was already hidden before us.
        for (const auto &item : current)
            if (RecoveryMatches(record, item) && (item.state & TBSTATE_HIDDEN)) {
                bool restored = NativeVisibility(item, false);
                if (!restored && !record.toolbar && CommandMatches(item)) {
                    DWORD_PTR result = 0;
                    Send(item.toolbar, TB_HIDEBUTTON, item.command, MAKELPARAM(FALSE, 0), result);
                    InvalidateRect(item.toolbar, nullptr, TRUE);
                }
                DebugLog::Write(L"RESTORE requested owner=" + Number((ULONG_PTR)item.owner) +
                                L" id=" + Number(item.id) + L" accepted=" + Number(restored));
                break;
            }
    }
    FreeItems(current);
    current = ReadTrayLocations();
    std::vector<Hidden> pending;
    for (const auto &record : hidden) {
        if (!record.originalHidden && RecoveryStillHidden(record, current))
            pending.push_back(record);
        else {
            lastRestoredCount++;
            for (auto &item : items)
                if (RecoveryMatches(record, item) && !record.originalHidden)
                    item.state &= ~TBSTATE_HIDDEN;
        }
    }
    hidden = std::move(pending);
    FreeItems(current);
    WriteRecovery();
    DebugLog::Write(L"RESTORE verified resolved=" + Number(lastRestoredCount) + L" pending=" +
                    Number(hidden.size()));
}
static void RecoverIcons() {
    RestoreHidden();
}
static void MigrateLegacyRecovery() {
    legacyRecoveryPath = baseDir + L"\\restore-icons.tsv";
    if (legacyRecoveryPath == recoveryPath ||
        GetFileAttributesW(legacyRecoveryPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    if (ImportRecovery())
        WriteRecovery();
}

static std::wstring GuardReadyName(DWORD pid, uint64_t started) {
    return L"Local\\Crli.TrayGroups.RecoveryReady." + Number(pid) + L"." + Number(started);
}
static bool EnsureRecoveryGuard() {
    // 第一次隐藏前握手，保证主进程被单独强制结束时仍有独立进程接手恢复。
#ifdef NATIVE_TEST_BUILD
    return true; // Production process supervision is exercised separately.
#else
    if (recoveryGuard && WaitForSingleObject(recoveryGuard, 0) == WAIT_TIMEOUT)
        return true;
    if (recoveryGuard) {
        CloseHandle(recoveryGuard);
        recoveryGuard = nullptr;
    }
    DWORD pid = GetCurrentProcessId();
    uint64_t started = ProcessStart(GetCurrentProcess());
    std::wstring readyName = GuardReadyName(pid, started);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    if (!ready)
        return false;
    std::wstring command = L"\"" + ExePath() + L"\" --recovery-watch " + Number(pid) + L" " +
                           Number(started) + L" \"" + recoveryPath + L"\"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    bool ok = CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, baseDir.c_str(), &startup, &process) != FALSE;
    if (ok) {
        CloseHandle(process.hThread);
        recoveryGuard = process.hProcess;
        ok = WaitForSingleObject(ready, 3000) == WAIT_OBJECT_0 &&
             WaitForSingleObject(recoveryGuard, 0) == WAIT_TIMEOUT;
    }
    CloseHandle(ready);
    DebugLog::Write(L"RECOVERY guard ready=" + Number(ok));
    if (!ok)
        LogError(L"恢复看护未就绪，本次不会隐藏图标");
    return ok;
#endif
}
static int RunRecoveryWatch(const std::wstring &arguments) {
    // 看护不初始化界面或截图，只等待指定主进程；恢复时用实例互斥量避免更新竞态。
    size_t first = arguments.find(L' '),
           second = arguments.find(L' ', first == std::wstring::npos ? first : first + 1);
    if (first == std::wstring::npos || second == std::wstring::npos)
        return 2;
    uint64_t pidValue = 0, started = 0;
    if (!RecoveryNumber(arguments.substr(0, first), pidValue) || pidValue > MAXDWORD ||
        !RecoveryNumber(arguments.substr(first + 1, second - first - 1), started))
        return 2;
    std::wstring quoted = Trim(arguments.substr(second + 1));
    if (quoted.size() < 3 || quoted.front() != L'"' || quoted.back() != L'"')
        return 2;
    recoveryPath = quoted.substr(1, quoted.size() - 2);
    baseDir = recoveryPath.substr(0, recoveryPath.find_last_of(L'\\'));
    DebugLog::Initialize(baseDir);
    DWORD pid = (DWORD)pidValue;
    DebugLog::Write(L"RECOVERY guard started owner=" + Number(pid) + L" journal=" + recoveryPath);
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!parent || ProcessStart(parent) != started) {
        if (parent)
            CloseHandle(parent);
        return 2;
    }
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, GuardReadyName(pid, started).c_str());
    if (!ready) {
        CloseHandle(parent);
        return 2;
    }
    SetEvent(ready);
    CloseHandle(ready);
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
    // Serialize with a newly launched version so two processes cannot rewrite
    // the same journal while one is recovering the former owner's changes.
    HANDLE gate = CreateMutexW(nullptr, FALSE, L"Local\\Crli.TrayGroups.Native.Instance");
    if (!gate)
        return 3;
    DWORD acquired = WaitForSingleObject(gate, 10000);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
        CloseHandle(gate);
        return 3;
    }
    DebugLog::Write(L"RECOVERY guard observed parent exit pid=" + Number(pid));
    for (unsigned attempt = 0; attempt < 15; attempt++) {
        RestoreHidden();
        if (!recoveryHealthy || hidden.empty())
            break;
        Sleep(150);
    }
    bool ok = recoveryHealthy && hidden.empty();
    DebugLog::Write(L"RECOVERY guard finished pending=" + Number(hidden.size()));
    ReleaseMutex(gate);
    CloseHandle(gate);
    return ok ? 0 : 1;
}
