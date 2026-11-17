#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int){
 if(command&&wcsncmp(command,L"--worker ",9)==0){
   unsigned long long owner=0;unsigned id=0,state=0;
   if(swscanf(command+9,L"%llu %u %u",&owner,&id,&state)!=3)return 2;
   NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=(HWND)(ULONG_PTR)owner;n.uID=id;n.uFlags=NIF_STATE;n.dwStateMask=NIS_HIDDEN;n.dwState=state?NIS_HIDDEN:0;
   return Shell_NotifyIconW(NIM_MODIFY,&n)?0:3;
 }
 auto external=[&](HWND owner,UINT id,bool hide){
   std::wstring cmd=L"\""+ExePath()+L"\" --worker "+Number((ULONG_PTR)owner)+L" "+Number(id)+L" "+Number(hide);
   STARTUPINFOW si={};si.cb=sizeof(si);PROCESS_INFORMATION pi={};
   if(!CreateProcessW(nullptr,&cmd[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;
   bool complete=WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0;DWORD code=1;if(complete)GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return complete&&code==0;
 };

 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 WNDCLASSW cls={};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Crli.StateProbe";RegisterClassW(&cls);
 HWND owner=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
 NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=owner;icon.uID=61401;icon.uFlags=NIF_ICON|NIF_TIP;icon.hIcon=LoadIconW(nullptr,IDI_INFORMATION);wcscpy_s(icon.szTip,L"TrayGroups isolated visibility probe");
 bool added=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE;testingOwnIcons=true;
 icon.uFlags=NIF_STATE;icon.dwStateMask=NIS_HIDDEN;icon.dwState=NIS_HIDDEN;bool hid=external(owner,icon.uID,true);
 auto current=ReadTrayLocations();int hidden=-1;for(const auto& item:current)if(item.owner==owner&&item.id==icon.uID)hidden=(item.state&TBSTATE_HIDDEN)!=0;FreeItems(current);
 icon.dwState=0;bool restored=external(owner,icon.uID,false);
 current=ReadTrayLocations();int visible=-1;for(const auto& item:current)if(item.owner==owner&&item.id==icon.uID)visible=!(item.state&TBSTATE_HIDDEN);FreeItems(current);
 Shell_NotifyIconW(NIM_DELETE,&icon);DestroyWindow(owner);
 std::wstring exe=ExePath();exe.resize(exe.find_last_of(L'\\'));WriteText(exe+L"\\state-probe.txt",L"added="+Number(added)+L" hiddenCall="+Number(hid)+L" hidden="+std::to_wstring(hidden)+L" restoreCall="+Number(restored)+L" visible="+std::to_wstring(visible)+L"\n");return added&&hid&&hidden==1&&restored&&visible==1?0:1;
}
