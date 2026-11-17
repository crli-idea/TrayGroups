#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
static std::wstring TokenState(DWORD pid) {
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid),token=nullptr;
    if(!process)return L"queryError="+Number(GetLastError());
    if(!OpenProcessToken(process,TOKEN_QUERY,&token)){DWORD error=GetLastError();CloseHandle(process);return L"tokenError="+Number(error);}
    TOKEN_ELEVATION elevation={};DWORD size=0;GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size);
    GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<unsigned char> buffer(size);
    DWORD level=0;if(GetTokenInformation(token,TokenIntegrityLevel,buffer.data(),size,&size)){
        auto label=(TOKEN_MANDATORY_LABEL*)buffer.data();DWORD count=*GetSidSubAuthorityCount(label->Label.Sid);level=*GetSidSubAuthority(label->Label.Sid,count-1);
    }
    CloseHandle(token);CloseHandle(process);return L"elevated="+Number(elevation.TokenIsElevated)+L" integrityRID="+Number(level);
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
    std::wstring report=L"probe "+TokenState(GetCurrentProcessId())+L"\n";
    auto current=ReadTray();bool found=false;
    for(const auto& item:current)if(Lower(item.process)==L"securityhealthsystray.exe"||Lower(item.process)==L"traygroups.exe"){
        report+=item.process+L" "+TokenState(item.runtime.pid)+L"\n";
        if(Lower(item.process)!=L"securityhealthsystray.exe")continue;
        found=true;
        // Same-state update only. Do not hide or unhide any user icon.
        NOTIFYICONDATAW data={};data.cbSize=sizeof(data);data.hWnd=item.owner;data.uID=item.id;
        data.uFlags=NIF_STATE;data.dwStateMask=NIS_HIDDEN;data.dwState=(item.state&TBSTATE_HIDDEN)?NIS_HIDDEN:0;
        SetLastError(0);BOOL result=Shell_NotifyIconW(NIM_MODIFY,&data);DWORD error=GetLastError();
        report+=L"security same-state modify="+Number(result)+L" lastError="+Number(error)+L" CommandMatches="+Number(CommandMatches(item))+L"\n";
    }
    FreeItems(current);
    // Show exactly how GUID registration changes the required addressing.
    WNDCLASSW cls={};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Crli.GuidPermissionProbe";RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=61001;
    icon.uFlags=NIF_GUID|NIF_ICON|NIF_TIP;icon.hIcon=LoadIconW(nullptr,IDI_INFORMATION);wcscpy_s(icon.szTip,L"TrayGroups GUID test");
    icon.guidItem={0xa8d7b8c1,0x96ad,0x4f14,{0xa2,0x8d,0x32,0x5b,0x76,0x71,0x19,0x84}};
    bool added=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE;
    NOTIFYICONDATAW ordinary=icon;ordinary.uFlags=NIF_STATE;ordinary.dwStateMask=NIS_HIDDEN;ordinary.dwState=0;
    SetLastError(0);BOOL viaOwner=Shell_NotifyIconW(NIM_MODIFY,&ordinary);DWORD ownerError=GetLastError();
    ordinary.uFlags=NIF_STATE|NIF_GUID;BOOL viaGuid=Shell_NotifyIconW(NIM_MODIFY,&ordinary);
    report+=L"isolated GUID icon added="+Number(added)+L" owner-ID modify="+Number(viaOwner)+L" lastError="+Number(ownerError)+L" GUID modify="+Number(viaGuid)+L"\n";
    Shell_NotifyIconW(NIM_DELETE,&icon);DestroyWindow(window);
    WriteText(L"C:\\work\\TrayGroups\\test\\permission-check.txt",report);
    return found&&added&&viaGuid?0:1;
}
