#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"

static HWND owner=nullptr;
static GUID ownedGuid={};
static std::wstring report;
static bool passed=true;
static void Check(bool value,const wchar_t* label){report+=(value?L"PASS ":L"FAIL ")+std::wstring(label)+L"\n";passed&=value;}
static void Pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
template<class Condition> static bool Until(Condition condition,DWORD timeout=8000){
    ULONGLONG end=GetTickCount64()+timeout;do{Pump();if(condition())return true;Sleep(30);}while(GetTickCount64()<end);return false;
}
static bool PresentHidden(UINT id){auto all=ReadTrayLocations();bool result=false;for(const auto& i:all)if(i.owner==owner&&i.id==id)result=(i.state&TBSTATE_HIDDEN)!=0;FreeItems(all);return result;}
static TrayItem FindIcon(UINT id){auto all=ReadTrayLocations();TrayItem found;for(const auto& i:all)if(i.owner==owner&&i.id==id)found=i;FreeItems(all);return found;}
static bool AddIcons(HINSTANCE instance){
    WNDCLASSW wc={};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"Crli.Recovery.IsolatedOwner";RegisterClassW(&wc);
    owner=mainWindow=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=owner;n.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE|NIF_STATE;n.uCallbackMessage=WM_APP+51;
    n.hIcon=LoadIconW(nullptr,IDI_INFORMATION);n.dwStateMask=NIS_HIDDEN;n.uID=8101;wcscpy_s(n.szTip,L"TrayGroups recovery owned test");
    if(FAILED(CoCreateGuid(&ownedGuid)))return false;
    n.uFlags|=NIF_GUID;n.guidItem=ownedGuid;
    bool ok=Shell_NotifyIconW(NIM_ADD,&n);n.uFlags&=~NIF_GUID;n.guidItem={};n.uID=8102;n.dwState=NIS_HIDDEN;wcscpy_s(n.szTip,L"TrayGroups originally hidden test");
    return Shell_NotifyIconW(NIM_ADD,&n)&&ok;
}
static Hidden Record(const TrayItem& item){Hidden r={item.toolbar,item.owner,item.id,item.command,item.runtime};r.path=item.path;r.ownerClass=item.ownerClass;r.callback=item.callback;r.guid=item.guid;r.usesGuid=item.usesGuid;return r;}
static void JournalTests(){
    auto a=FindIcon(8101),b=FindIcon(8102);a.group=b.group=L"Test";items={a,b};hidden.clear();settingsConfig.foldIcons=true;
    FoldAssigned();Check(PresentHidden(8101)&&PresentHidden(8102)&&hidden.size()==1,L"only the newly hidden icon is journaled; pre-hidden icon is untouched");
    auto text=ReadText(recoveryPath);Check(text.find(L"WasHidden=0")!=std::wstring::npos&&text.find(L"Version=3")!=std::wstring::npos&&text.find(GuidKey(ownedGuid))!=std::wstring::npos,L"GUID and original state are committed to version 3 journal before hiding");
    hidden.clear();RestoreAllOwnedIcons();
    Check(!PresentHidden(8101)&&PresentHidden(8102)&&hidden.empty(),L"Restore All imports the disk journal and preserves the pre-hidden icon");
    Check(ReadText(configPath).find(L"CustomSetting=keep")!=std::wstring::npos&&ReadText(configPath).find(L"FoldOriginalIcons=0")!=std::wstring::npos,L"restore pauses folding without overwriting unrelated settings");
    Hidden missing=Record(a);missing.id=missing.runtime.id=8199;hidden={missing};WriteRecovery();hidden.clear();RecoverIcons();
    auto pending=ReadText(recoveryPath);RestoreUnassigned();settingsConfig.foldIcons=true;items.clear();FoldAssigned();
    Check(!hidden.empty()&&!pending.empty()&&ReadText(recoveryPath).find(L"8199")!=std::wstring::npos,L"failed startup recovery survives the following refresh instead of losing its journal");
    // Clear this test's deliberately unresolvable record.
    hidden.clear();DeleteFileW(recoveryPath.c_str());RestoreHidden();items={a,b};
    Hidden original=Record(b);original.originalHidden=true;hidden={original};WriteRecovery();hidden.clear();RecoverIcons();
    Check(PresentHidden(8102)&&hidden.empty(),L"WasHidden=1 records never unhide a previously hidden icon");
    hidden={original};WriteRecovery();hidden.clear();items={a,b};items[1].group.clear();RestoreUnassigned();
    Check(PresentHidden(8102)&&hidden.empty(),L"refresh also preserves an originally hidden icon when its group is removed");
    Hidden stale=Record(b);stale.runtime.started--;hidden={stale};WriteRecovery();hidden.clear();RecoverIcons();
    Check(PresentHidden(8102)&&hidden.empty(),L"stale process lifetime after restart cannot modify a new icon");
    hidden={Record(a)};WriteRecovery();std::wstring committed=ReadText(recoveryPath);
    HANDLE locked=CreateFileW(recoveryPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    items={a,b};settingsConfig.foldIcons=true;FoldAssigned();
    Check(locked!=INVALID_HANDLE_VALUE&&!recoveryHealthy&&!PresentHidden(8101)&&ReadText(recoveryPath)==committed,L"journal commit failure prevents hiding and retains the previous committed file");
    if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);hidden.clear();DeleteFileW(recoveryPath.c_str());recoveryHealthy=true;
    std::wstring corrupt=L"[Recovery]\nVersion=2\n[Icon1]\nIconId=8101\n";WriteText(recoveryPath,corrupt);hidden.clear();RestoreHidden();FoldAssigned();
    Check(!recoveryHealthy&&ReadText(recoveryPath)==corrupt&&!PresentHidden(8101),L"corrupt recovery files are retained and block new hides");
    DeleteFileW(recoveryPath.c_str());recoveryHealthy=true;hidden.clear();items={a,b};settingsConfig.foldIcons=true;FoldAssigned();
    CaptureTimer::busy=true;MainProc(owner,StopApp,0,0);
    Check(IsWindow(owner)&&!PresentHidden(8101)&&lifecycle.IsStopping(),L"exit restores icons while waiting for the capture worker");
    Refresh();Check(!PresentHidden(8101),L"refresh cannot hide icons again after exit has begun");
    CaptureTimer::busy=false;MainProc(owner,StopApp,0,0);
    Check(!IsWindow(owner)&&hidden.empty(),L"normal exit waits for verified restoration before destroying its window");
    mainWindow=owner=nullptr;lifecycle.CancelExit();CaptureTimer::stopRequested=false;
    FreeItems(items);DeleteFileW(recoveryPath.c_str());
}
static HANDLE FindGuard(DWORD parent){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0),guard=nullptr;PROCESSENTRY32W e={};e.dwSize=sizeof(e);
    if(snapshot!=INVALID_HANDLE_VALUE){if(Process32FirstW(snapshot,&e))do{if(e.th32ParentProcessID==parent&&_wcsicmp(e.szExeFile,L"TrayGroups.exe")==0){guard=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID);break;}}while(Process32NextW(snapshot,&e));CloseHandle(snapshot);}return guard;
}
static PROCESS_INFORMATION StartPortable(const std::wstring& folder,const wchar_t* args){
    PROCESS_INFORMATION p={};STARTUPINFOW startup={};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    std::wstring cmd=L"\""+folder+L"\\TrayGroups.exe\" "+args;
    CreateProcessW(nullptr,&cmd[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&startup,&p);if(p.hThread)CloseHandle(p.hThread);return p;
}
static void PortableTests(HINSTANCE instance,const std::wstring& binary){
    AddIcons(instance);Check(Until([]{return FindIcon(8101).runtime.Valid()&&PresentHidden(8102);}),L"production lifecycle test icons are ready");
    auto a=FindIcon(8101),b=FindIcon(8102);std::wstring folder=baseDir+L"\\portable";CaptureTimer::MakeDirectories(folder);
    Check(CopyFileW(binary.c_str(),(folder+L"\\TrayGroups.exe").c_str(),FALSE)!=FALSE,L"production executable copied to isolated portable folder");
    std::wstring config=L"[Settings]\nFoldOriginalIcons=1\nDebugLogging=1\n[Icons]\n"+IconKey(a,L"path-guid")+L"=Test\n"+IconKey(b,L"path-guid")+L"=Test\n";
    WriteText(folder+L"\\groups.ini",config);
    for(bool crash:{true,false}){
        PROCESS_INFORMATION p=StartPortable(folder,L"--background");
        bool hiddenNow=p.hProcess&&Until([&]{HWND host=FindWindowW(AppClass,nullptr);DWORD pid=0;if(host)GetWindowThreadProcessId(host,&pid);return pid==p.dwProcessId&&PresentHidden(8101);});Check(hiddenNow,crash?L"production app hides the selected icon before forced termination":L"production app reopens after upgrade with its portable configuration");
        HANDLE guard=p.hProcess?FindGuard(p.dwProcessId):nullptr;Check(guard!=nullptr,L"production recovery guard is running before hide");
        if(p.hProcess){
            if(crash)TerminateProcess(p.hProcess,42);else{HWND host=FindWindowW(AppClass,nullptr);DWORD pid=0;GetWindowThreadProcessId(host,&pid);if(pid==p.dwProcessId)PostMessageW(host,StopApp,0,0);}
            bool stopped=WaitForSingleObject(p.hProcess,8000)==WAIT_OBJECT_0;
            Check(stopped,crash?L"only the isolated app process is force-terminated":L"production normal exit completes");
            if(!stopped)TerminateProcess(p.hProcess,43);CloseHandle(p.hProcess);
        }
        Check(Until([&]{return VisibleAtRuntime(a.runtime)&&PresentHidden(8102);}),crash?L"guard restores owned icon after parent crash while pre-hidden icon stays hidden":L"normal exit restores owned icon and preserves pre-hidden icon");
        if(guard){bool done=WaitForSingleObject(guard,8000)==WAIT_OBJECT_0;DWORD code=99;GetExitCodeProcess(guard,&code);Check(done&&code==0,L"recovery guard exits successfully after cleanup");report+=L"guard exit code="+Number(code)+L"\n";CloseHandle(guard);}
        Check(GetFileAttributesW((folder+L"\\restore-icons.ini").c_str())==INVALID_FILE_ATTRIBUTES,L"production journal is cleared only after verified restore");
        Check(CopyFileW(binary.c_str(),(folder+L"\\TrayGroups.exe").c_str(),FALSE)!=FALSE,L"upgrade can replace executable after parent and guard exit");
    }
    std::wstring previous=recoveryPath;recoveryPath=folder+L"\\restore-icons.ini";hidden={Record(a)};WriteRecovery();hidden.clear();
    NativeVisibility(a,true);PROCESS_INFORMATION repair=StartPortable(folder,L"--restore");
    bool finished=repair.hProcess&&WaitForSingleObject(repair.hProcess,8000)==WAIT_OBJECT_0;DWORD code=99;
    if(repair.hProcess){GetExitCodeProcess(repair.hProcess,&code);if(!finished)TerminateProcess(repair.hProcess,44);CloseHandle(repair.hProcess);}
    Check(finished&&code==0&&VisibleAtRuntime(a.runtime)&&PresentHidden(8102),L"standalone --restore recovers from the INI with no resident app or guard");
    Check(GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES,L"standalone recovery verifies visibility before clearing the INI");
    recoveryPath=previous;
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=owner;icon.uID=8101;icon.uFlags=NIF_GUID;icon.guidItem=ownedGuid;Shell_NotifyIconW(NIM_DELETE,&icon);icon.uFlags=0;icon.uID=8102;Shell_NotifyIconW(NIM_DELETE,&icon);DestroyWindow(owner);owner=mainWindow=nullptr;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));std::wstring binary=path+L"\\TrayGroups.exe";
    baseDir=path+L"\\recovery-lifecycle";CaptureTimer::MakeDirectories(baseDir);configPath=baseDir+L"\\groups.ini";recoveryPath=baseDir+L"\\restore-icons.ini";
    if(FindWindowW(AppClass,nullptr)){WriteText(baseDir+L"\\report.txt",L"FAIL: close the production app before lifecycle tests\n");return 2;}
    DebugLog::Initialize(baseDir);testingOwnIcons=true;auto before=ReadTrayLocations();
    Check(AddIcons(instance)&&Until([]{return FindIcon(8101).runtime.Valid()&&PresentHidden(8102);}),L"isolated visible and pre-hidden icons registered");
    WriteText(configPath,L"[Settings]\nFoldOriginalIcons=1\nCustomSetting=keep\n");JournalTests();PortableTests(instance,binary);
    auto after=ReadTrayLocations();bool unchanged=true;
    for(const auto& first:before)for(const auto& last:after)if(first.runtime==last.runtime&&first.toolbar==last.toolbar)unchanged&=(first.state&TBSTATE_HIDDEN)==(last.state&TBSTATE_HIDDEN);
    Check(unchanged,L"all pre-existing user tray icon states are unchanged by these tests");FreeItems(before);FreeItems(after);
    WriteText(baseDir+L"\\report.txt",report);return passed?0:1;
}
