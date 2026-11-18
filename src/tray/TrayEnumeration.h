// 主程序私有的 Explorer 托盘扫描实现，不独立保存应用状态。
// 进程/窗口缓存只在单次扫描中使用，避免长期缓存误命中复用的 PID 或 HWND。
#pragma once

struct TrayProcessInfo {
    std::wstring path;
    uint64_t started = 0;
};
struct TrayScanContext {
    std::vector<TrayItem> &items;
    bool includeImages;
    // Cache only within this scan: PIDs and HWNDs may be reused after it ends.
    std::map<DWORD, TrayProcessInfo> processes;
    std::map<HWND, std::wstring> classes;
    const TrayProcessInfo &Process(DWORD pid) {
        auto found = processes.find(pid);
        if (found != processes.end())
            return found->second;
        TrayProcessInfo info;
        NativeHandles::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (process) {
            info.started = ProcessStart(process);
            wchar_t path[32768];
            DWORD size = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &size))
                info.path.assign(path, size);
        }
        return processes.emplace(pid, std::move(info)).first->second;
    }
    const std::wstring &Class(HWND owner, DWORD pid) {
        auto found = classes.find(owner);
        if (found != classes.end())
            return found->second;
        return classes.emplace(owner, OwnerClass(owner, pid)).first->second;
    }
};
static void ReadToolbar(HWND toolbar, TrayScanContext &scan) {
    // 只在目标 Explorer 中临时申请读取缓冲；所有出口都必须释放缓冲并关闭进程句柄。
    auto &out = scan.items;
    DWORD pid = 0;
    GetWindowThreadProcessId(toolbar, &pid);
    NativeHandles::Handle process(OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid));
    if (!process)
        return;
    NativeHandles::RemoteBuffer buffer(process, 4096);
    void *memory = buffer.Get();
    if (!memory) {
        return;
    }
    DWORD_PTR count = 0;
    if (Send(toolbar, TB_BUTTONCOUNT, 0, 0, count) && count < 512)
        for (size_t j = 0; j < count; j++) {
            DWORD_PTR ok = 0;
            TBBUTTON button = {};
            RemoteTrayData data = {};
            if (!Send(toolbar, TB_GETBUTTON, j, (LPARAM)memory, ok) || !ok ||
                !ReadMemory(process, (ULONG_PTR)memory, button) || !button.dwData ||
                !ReadMemory(process, button.dwData, data) || !IsWindow((HWND)data.owner))
                continue;
            DWORD ownerPid = 0;
            GetWindowThreadProcessId((HWND)data.owner, &ownerPid);
            if (ownerPid == GetCurrentProcessId()
#ifdef NATIVE_TEST_BUILD
                && !testingOwnIcons
#endif
            )
                continue;
            TrayItem item;
            item.toolbar = toolbar;
            item.command = button.idCommand;
            item.owner = (HWND)data.owner;
            item.id = data.id;
            item.callback = data.callback;
            item.version = data.version;
            item.state = button.fsState;
            const auto &owner = scan.Process(ownerPid);
            item.path = owner.path;
            size_t slash = item.path.find_last_of(L"\\/");
            item.process = item.path.empty()
                               ? L"PID " + Number(ownerPid)
                               : item.path.substr(slash == std::wstring::npos ? 0 : slash + 1);
            item.ownerClass = scan.Class(item.owner, ownerPid);
            item.runtime = {ownerPid, owner.started, item.owner, item.id};
            DWORD_PTR chars = 0;
            if (Send(toolbar, TB_GETBUTTONTEXTW, item.command, (LPARAM)memory, chars) &&
                chars > 0 && chars < 1500) {
                std::vector<wchar_t> text(chars + 1, 0);
                SIZE_T got = 0;
                if (ReadProcessMemory(process, memory, text.data(), chars * sizeof(wchar_t), &got))
                    item.tip = text.data();
            }
            if (scan.includeImages && data.icon)
                item.image = CopyIcon((HICON)data.icon);
            // Avoid initializing shell icon handlers (COM/thread pools) on each scan.
            // File icons are extracted only for a group that the user actually opens.
            if (scan.includeImages && !item.image) {
                item.image = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
                item.fallbackImage = true;
            }
            out.push_back(std::move(item));
        }
}
static BOOL CALLBACK ReadChild(HWND h, LPARAM p) {
    if (ClassOf(h) == L"ToolbarWindow32")
        ReadToolbar(h, *(TrayScanContext *)p);
    return TRUE;
}
static BOOL CALLBACK ReadRoot(HWND h, LPARAM p) {
    auto c = ClassOf(h);
    if (c == L"Shell_TrayWnd" || c == L"NotifyIconOverflowWindow" || c == L"Shell_SecondaryTrayWnd")
        EnumChildWindows(h, ReadChild, p);
    return TRUE;
}
static std::vector<TrayItem> ReadTrayLocations(bool includeImages = false) {
    // 保留每个工具栏位置供恢复核验；返回图标副本的所有权交给调用者 FreeItems。
    std::vector<TrayItem> result;
    TrayScanContext scan{result, includeImages, {}, {}};
    EnumWindows(ReadRoot, (LPARAM)&scan);
    ResolveTrayGuids(result);
    return result;
}
static std::vector<TrayItem> ReadTray(bool includeImages = true) {
    auto locations = ReadTrayLocations(includeImages);
    std::vector<TrayItem> result;
    for (auto &item : locations) {
        auto match = std::find_if(result.begin(), result.end(), [&](const TrayItem &existing) {
            return existing.owner == item.owner && existing.id == item.id;
        });
        if (match == result.end()) {
            result.push_back(std::move(item));
        } else if ((match->state & TBSTATE_HIDDEN) && !(item.state & TBSTATE_HIDDEN)) {
            *match = std::move(item);
        }
    }
    FreeItems(locations);
    matcher.ResolveGroups(result);
    return result;
}
