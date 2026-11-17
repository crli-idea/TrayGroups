#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int){
    HWND owner=FindWindowW(AppClass,nullptr);std::wstring args=Trim(command?command:L"");std::wstring report=L"args=["+args+L"] owner="+Number((ULONG_PTR)owner)+L"\n";
    DWORD pid=0;DWORD thread=GetWindowThreadProcessId(owner,&pid);report+=L"pid="+Number(pid)+L" thread="+Number(thread)+L"\n";
    DWORD_PTR result=0;bool responsive=owner&&SendMessageTimeoutW(owner,WM_NULL,0,0,SMTO_ABORTIFHUNG,1000,&result);
    report+=L"WM_NULL responsive="+Number(responsive)+L" win32="+Number(GetLastError())+L"\n";
    GUITHREADINFO info={};info.cbSize=sizeof(info);GetGUIThreadInfo(thread,&info);report+=L"guiFlags="+Number(info.flags)+L" menuOwner="+Number((ULONG_PTR)info.hwndMenuOwner)+L"\n";
    auto all=ReadTrayLocations();for(const auto& item:all)if(item.owner==owner)
        report+=L"tray id="+Number(item.id)+L" callback="+Number(item.callback)+L" version="+Number(item.version)+L" state="+Number(item.state)+L"\n";
    FreeItems(all);
    if(args==L"--exercise"&&responsive){
        auto observe=[&](const wchar_t* label){
            Sleep(1200);HWND popup=FindWindowW(PopupClass,nullptr);GetGUIThreadInfo(thread,&info);
            report+=std::wstring(label)+L" popup="+Number((ULONG_PTR)popup)+L" visible="+Number(popup&&IsWindowVisible(popup))+L" guiFlags="+Number(info.flags)+L" menuOwner="+Number((ULONG_PTR)info.hwndMenuOwner)+L"\n";
            if(popup){wchar_t text[300]={};GetDlgItemTextW(popup,CaptureStatusControl,text,300);report+=L"capture-status="+std::wstring(text)+L"\n";PostMessageW(popup,WM_CLOSE,0,0);}
            if(info.hwndMenuOwner)PostMessageW(owner,WM_CANCELMODE,0,0);Sleep(150);
        };
        if(!FindWindowW(PopupClass,nullptr)&&!info.hwndMenuOwner){
            auto post=[&](UINT message,WPARAM w,LPARAM l){SetLastError(0);bool sent=PostMessageW(owner,message,w,l)!=FALSE;report+=L"post message="+Number(message)+L" ok="+Number(sent)+L" win32="+Number(GetLastError())+L"\n";};
            post(TrayCallback,1,WM_LBUTTONUP);observe(L"legacy-left");
            post(TrayCallback,1,WM_RBUTTONUP);observe(L"legacy-right");
            post(TrayCallback,0,MAKELPARAM(NIN_SELECT,1));observe(L"version4-left");
            HWND popup=FindWindowW(PopupClass,nullptr);if(popup)PostMessageW(popup,WM_CLOSE,0,0);Sleep(100);
            post(TrayCallback,0,MAKELPARAM(WM_CONTEXTMENU,1));observe(L"version4-right");
            PostMessageW(owner,WM_CANCELMODE,0,0);Sleep(100);
            post(WM_APP+8,1,1);observe(L"direct-panel");popup=FindWindowW(PopupClass,nullptr);if(popup)PostMessageW(popup,WM_CLOSE,0,0);Sleep(100);
            post(WM_APP+8,2,0);observe(L"direct-menu");PostMessageW(owner,WM_CANCELMODE,0,0);
        }else report+=L"exercise skipped: existing popup or menu\n";
    }
    if(args==L"--capture"&&responsive){
        PostMessageW(owner,WM_COMMAND,CaptureTimer::CaptureNowCommand,0);Sleep(7000);
        PostMessageW(owner,WM_APP+8,1,1);Sleep(500);
        HWND popup=FindWindowW(PopupClass,nullptr);wchar_t status[300]={};if(popup)GetDlgItemTextW(popup,CaptureStatusControl,status,300);
        report+=L"after-real-capture="+std::wstring(status)+L"\n";
        if(popup)PostMessageW(popup,WM_CLOSE,0,0);
    }
    std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));WriteText(path+L"\\click-diagnostics.txt",report);return responsive?0:1;
}
