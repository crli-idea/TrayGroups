
#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto all=ReadTrayLocations();std::wstring report;
    for(const auto& item:all){
        RECT toolbar={};GetWindowRect(item.toolbar,&toolbar);
        report+=item.process+L"\t"+Escape(FriendlyIconName(item))+L"\towner="+Number((ULONG_PTR)item.owner)+L"\tid="+Number(item.id)+L"\ttoolbar="+Number((ULONG_PTR)item.toolbar)+L"\tparent="+ClassOf(GetParent(item.toolbar))+L"\thidden="+Number((item.state&TBSTATE_HIDDEN)!=0)+L"\tcommand="+Number(item.command)+L"\tleft="+std::to_wstring(toolbar.left)+L"\ttop="+std::to_wstring(toolbar.top)+L"\n";
    }
    FreeItems(all);std::wstring exe=ExePath();exe.resize(exe.find_last_of(L'\\'));WriteText(exe+L"\\tray-locations.tsv",report);return 0;
}
