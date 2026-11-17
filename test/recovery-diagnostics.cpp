#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"

// Read-only snapshot. Does not change icon state or send application callbacks.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    auto icons=ReadTrayLocations();unsigned hiddenCount=0;std::wstring report;
    HWND host=FindWindowW(AppClass,nullptr);DWORD hostPid=0;if(host)GetWindowThreadProcessId(host,&hostPid);
    report=L"host="+Number((ULONG_PTR)host)+L" pid="+Number(hostPid)+L"\n";
    for(const auto& item:icons){
        bool hidden=(item.state&TBSTATE_HIDDEN)!=0;hiddenCount+=hidden;
        report+=item.process+L"\t"+Escape(item.tip)+L"\tclass="+StableClass(item.ownerClass)+
            L"\tpid="+Number(item.runtime.pid)+L"\tstarted="+Number(item.runtime.started)+
            L"\towner="+Number((ULONG_PTR)item.owner)+L"\tid="+Number(item.id)+
            L"\ttoolbar="+Number((ULONG_PTR)item.toolbar)+L"\tparent="+ClassOf(GetParent(item.toolbar))+
            L"\thidden="+Number(hidden)+L"\tguid="+(item.usesGuid?GuidKey(item.guid):L"none")+L"\n";
    }
    report+=L"entries="+Number(icons.size())+L" hidden="+Number(hiddenCount)+L"\n";
    FreeItems(icons);std::wstring output=ExePath();output.resize(output.find_last_of(L'\\'));
    return WriteText(output+L"\\recovery-diagnostics.txt",report)?0:1;
}
