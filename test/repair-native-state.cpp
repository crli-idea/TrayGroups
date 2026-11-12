#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 auto all=ReadTrayLocations();std::wstring report;
 for(const auto& item:all)if(Lower(item.process)==L"explorer.exe"){
  auto cls=StableClass(item.ownerClass);if(cls!=L"atl@sndvolsso.dll"&&cls!=L"bluetoothnotificationareaiconwindowclass"&&cls!=L"atl@pnidui.dll")continue;
  NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=item.owner;n.uID=item.id;n.uFlags=NIF_STATE;n.dwStateMask=NIS_HIDDEN;n.dwState=cls==L"atl@pnidui.dll"?(item.state&TBSTATE_HIDDEN?NIS_HIDDEN:0):0;
  bool ok=Shell_NotifyIconW(NIM_MODIFY,&n)!=FALSE;report+=FriendlyIconName(item)+L" desiredHidden="+Number(n.dwState!=0)+L" success="+Number(ok)+L"\n";
 }
 FreeItems(all);std::wstring exe=ExePath();exe.resize(exe.find_last_of(L'\\'));WriteText(exe+L"\\repair-native-state.txt",report);return 0;
}