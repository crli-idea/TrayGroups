#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 std::wstring report;auto icons=ReadTray();
 for(const auto& item:icons)if(Lower(item.process)==L"explorer.exe"||Lower(item.process)==L"eartrumpet.exe")report+=item.process+L" class="+item.ownerClass+L" path="+item.path+L" id="+Number(item.id)+L" tip="+item.tip+L"\n";
 for(unsigned value:{0x7820ae73u,0x7820ae74u,0x7820ae75u,0x7820ae76u}){
  GUID guid={value,0x23e3,0x4229,{0x82,0xc1,0xe4,0x1c,0xb6,0x7d,0x5b,0x9c}};
  NOTIFYICONIDENTIFIER id={};id.cbSize=sizeof(id);id.guidItem=guid;RECT area={};HRESULT hr=Shell_NotifyIconGetRect(&id,&area);
  report+=L"guid-start="+Number(value)+L" hr="+Number((DWORD)hr)+L" rect="+std::to_wstring(area.left)+L","+std::to_wstring(area.top)+L","+std::to_wstring(area.right)+L","+std::to_wstring(area.bottom)+L"\n";
 }
 FreeItems(icons);WriteText(L"C:\\work\\TrayGroups\\test\\bluetooth-volume-check.txt",report);return 0;
}