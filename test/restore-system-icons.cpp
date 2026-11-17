#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 auto current=ReadTray();std::wstring report;
 for(const auto& i:current){
  if(Lower(i.process)!=L"explorer.exe")continue;
  std::wstring cls=StableClass(i.ownerClass);
  bool target=cls==L"atl@sndvolsso.dll"||cls==L"bluetoothnotificationareaiconwindowclass"||cls==L"atl@pnidui.dll";
  if(!target)continue;
  DWORD_PTR result=0;bool ok=Send(i.toolbar,TB_HIDEBUTTON,i.command,MAKELPARAM(FALSE,0),result)&&result;
  InvalidateRect(i.toolbar,nullptr,TRUE);
  report+=cls+L" id="+Number(i.id)+L" wasHidden="+Number((i.state&TBSTATE_HIDDEN)!=0)+L" unhideSucceeded="+Number(ok)+L"\n";
 }
 FreeItems(current);auto after=ReadTray();for(const auto& i:after)if(Lower(i.process)==L"explorer.exe"){
  std::wstring cls=StableClass(i.ownerClass);
  if(cls==L"atl@sndvolsso.dll"||cls==L"bluetoothnotificationareaiconwindowclass"||cls==L"atl@pnidui.dll")report+=cls+L" afterHidden="+Number((i.state&TBSTATE_HIDDEN)!=0)+L"\n";
 }
 FreeItems(after);std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));WriteText(path+L"\\restore-system-icons.txt",report);return 0;
}
