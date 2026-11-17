#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 auto all=ReadTrayLocations();std::set<HWND> toolbars;for(const auto& item:all)if(ClassOf(GetParent(item.toolbar))==L"SysPager")toolbars.insert(item.toolbar);
 for(HWND toolbar:toolbars){DWORD_PTR result=0;Send(toolbar,TB_AUTOSIZE,0,0,result);Send(GetParent(toolbar),PGM_RECALCSIZE,0,0,result);}
 HWND root=FindWindowW(L"Shell_TrayWnd",nullptr);RECT client={};GetClientRect(root,&client);DWORD_PTR result=0;Send(root,WM_SIZE,SIZE_RESTORED,MAKELPARAM(client.right,client.bottom),result);RedrawWindow(root,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
 FreeItems(all);return 0;
}
