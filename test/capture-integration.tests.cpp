#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"

static bool outcome=true;
static std::atomic<int> workerPriority{THREAD_PRIORITY_ERROR_RETURN};
static bool FakeCapture(const CaptureTimer::Settings&){workerPriority=GetThreadPriority(GetCurrentThread());return outcome;}
static LRESULT CALLBACK Receiver(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==CaptureTimer::CompletionMessage()||msg==WM_COMMAND)return MainProc(h,msg,w,l);
    return DefWindowProcW(h,msg,w,l);
}
static bool WaitForCompletion(){
    ULONGLONG end=GetTickCount64()+5000;
    while(CaptureTimer::IsBusy()&&GetTickCount64()<end){
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        Sleep(1);
    }
    return !CaptureTimer::IsBusy();
}
template<class Check> static void CheckTrayScanning(Check check,std::wstring& report){
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    HWND toolbar=CreateWindowExW(0,TOOLBARCLASSNAMEW,L"",WS_CHILD,0,0,0,0,mainWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(toolbar,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
    std::vector<RemoteTrayData> records(24);std::vector<TBBUTTON> buttons(24);
    HICON image=LoadIconW(nullptr,IDI_INFORMATION);
    for(size_t n=0;n<records.size();n++){
        records[n]={(uint64_t)mainWindow,(UINT)n+100,WM_APP+50,0,4,(uint64_t)image};
        buttons[n].idCommand=(int)n+100;buttons[n].fsState=TBSTATE_ENABLED;
        buttons[n].fsStyle=BTNS_BUTTON;buttons[n].dwData=(DWORD_PTR)&records[n];
        buttons[n].iString=(INT_PTR)L"Isolated tray scan test";
    }
    bool ready=toolbar&&SendMessageW(toolbar,TB_ADDBUTTONSW,buttons.size(),(LPARAM)buttons.data());
    testingOwnIcons=true;
    auto scan=[&](bool images){std::vector<TrayItem> list;TrayScanContext context{list,images,{},{}};ReadToolbar(toolbar,context);return list;};
    auto light=scan(false),full=scan(true);bool equal=ready&&light.size()==24&&full.size()==24;
    unsigned lightImages=0,fullImages=0;
    for(size_t n=0;n<light.size()&&n<full.size();n++){
        equal&=SameRuntime(light[n],full[n])&&light[n].tip==full[n].tip&&light[n].state==full[n].state&&IconKey(light[n],iconMatch)==IconKey(full[n],iconMatch);
        lightImages+=light[n].image!=nullptr;fullImages+=full[n].image!=nullptr;
    }
    check(equal,L"metadata scan preserves all 24 icon identities, tips and visibility states");
    check(equal&&lightImages==0&&fullImages==24,L"background scan creates no icon copies; UI scan retains all images");
    FreeItems(light);FreeItems(full);
    LARGE_INTEGER frequency,start,end;QueryPerformanceFrequency(&frequency);
    auto measure=[&](bool images){QueryPerformanceCounter(&start);for(int n=0;n<20;n++){auto list=scan(images);FreeItems(list);}QueryPerformanceCounter(&end);return (end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart;};
    DWORD before=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    double fullMs=measure(true),lightMs=measure(false);
    check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==before,L"repeated metadata and image scans release their icon handles");
    wchar_t timing[200];swprintf_s(timing,L"BENCH 20 scans x 24 icons: with images=%.3f ms; metadata=%.3f ms\n",fullMs,lightMs);report+=timing;
    testingOwnIcons=false;DestroyWindow(toolbar);
}
template<class Check> static void CheckRecoveryJournal(Check check){
    std::wstring previous=recoveryPath;recoveryPath=baseDir+L"\\durable-recovery.ini";hidden.clear();
    Hidden record;record.owner=mainWindow;record.id=62100;record.runtime={GetCurrentProcessId(),ProcessStart(GetCurrentProcess()),mainWindow,record.id};
    hidden={record};bool wrote=WriteRecovery();hidden.clear();RestoreHidden();
    check(wrote&&hidden.size()==1&&!ReadText(recoveryPath).empty(),L"disk ownership journal survives memory loss and an unenumerated live icon");
    RestoreUnassigned();FoldAssigned();
    check(hidden.size()==1&&!ReadText(recoveryPath).empty(),L"refresh preserves a pending disk recovery record");
    std::vector<Hidden> parsed;check(ReadRecoveryFile(recoveryPath,parsed)&&parsed.size()==1&&!parsed[0].originalHidden,L"readable recovery INI retains the original visibility state");
    hidden.clear();DeleteFileW(recoveryPath.c_str());
    std::wstring corrupt=L"[Recovery]\nVersion=2\n[Icon1]\nIconId=62100\n";WriteText(recoveryPath,corrupt);RestoreHidden();
    check(!recoveryHealthy&&ReadText(recoveryPath)==corrupt,L"invalid ownership journals are preserved rather than overwritten");
    DeleteFileW(recoveryPath.c_str());recoveryHealthy=true;
    std::wstring old=Number((ULONG_PTR)mainWindow)+L"\t"+Number(record.id)+L"\t"+Number(record.runtime.pid)+L"\t"+Number(record.runtime.started)+L"\n";
    WriteText(recoveryPath,old);parsed.clear();check(ReadRecoveryFile(recoveryPath,parsed)&&parsed.size()==1&&parsed[0].runtime==record.runtime,L"legacy TSV ownership journals remain readable");
    DeleteFileW(recoveryPath.c_str());hidden.clear();recoveryPath=previous;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    baseDir=ExePath();baseDir.resize(baseDir.find_last_of(L'\\'));baseDir+=L"\\capture-integration";
    CaptureTimer::MakeDirectories(baseDir);configPath=baseDir+L"\\groups.ini";recoveryPath=baseDir+L"\\restore-icons.tsv";
    DebugLog::Initialize(baseDir);DebugLog::enabled=true;
    std::wstring report;bool ok=true;
    auto check=[&](bool value,const wchar_t* description){report+=(value?L"PASS ":L"FAIL ")+std::wstring(description)+L"\n";ok&=value;};
    WNDCLASSW wc={};wc.hInstance=instance;wc.lpfnWndProc=Receiver;wc.lpszClassName=L"Crli.Capture.IntegrationTest";RegisterClassW(&wc);
    mainWindow=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    CheckTrayScanning(check,report);
    CheckRecoveryJournal(check);
    std::wstring originalConfig=configPath;configPath=baseDir+L"\\..\\..\\..\\defaults\\groups.ini";LoadSettings();
    check(!ReadText(configPath).empty()&&assignments.empty()&&pathAssignments.empty()&&programAssignments.empty()&&iconMatch==L"path-class-id"&&foldIcons,L"shipped INI loads portable defaults without personal grouping rules");configPath=originalConfig;
    check(NativeResources::ThreadCount()>0,L"shared resource sampler reports this process's threads");
    CaptureTimer::Initialize(mainWindow,instance,baseDir);CaptureTimer::testCapture=FakeCapture;
    KillTimer(mainWindow,CaptureTimer::TimerId());
    check(CaptureTimer::completionMessage==CaptureTimer::CompletionMessage(),L"worker and host use the same completion message");
    check(GetFileAttributesW((baseDir+L"\\config.json").c_str())!=INVALID_FILE_ATTRIBUTES,L"portable capture config created beside host");
    notificationGroups[1]=L"";
    for(bool legacy:{false,true}){
        MainProc(mainWindow,TrayCallback,1,legacy?WM_LBUTTONUP:MAKELPARAM(NIN_SELECT,1));
        MSG click={};bool found=PeekMessageW(&click,mainWindow,WM_APP+8,WM_APP+8,PM_REMOVE)!=0;
        check(found&&click.wParam==1&&click.lParam==1,legacy?L"legacy tray click opens T panel":L"version 4 tray click opens T panel");
    }
    notificationGroups.clear();
    for(bool right:{false,true}){
        MainProc(mainWindow,TrayCallback,0,MAKELPARAM(right?WM_CONTEXTMENU:NIN_SELECT,1));
        MSG click={};bool found=PeekMessageW(&click,mainWindow,WM_APP+8,WM_APP+8,PM_REMOVE)!=0;
        check(found&&click.wParam==(right?2u:1u),right?L"right click survives loss of tray registration cache":L"left click survives loss of tray registration cache");
    }
    CaptureTimer::HandleMenu(CaptureTimer::CaptureNowCommand);
    check(WaitForCompletion()&&CaptureTimer::StatusText().find(L"已保存")!=std::wstring::npos,L"successful worker updates visible status through host message loop");
    check(workerPriority==THREAD_PRIORITY_BELOW_NORMAL&&GetThreadPriority(GetCurrentThread())==THREAD_PRIORITY_NORMAL,L"capture yields CPU priority while tray UI keeps normal priority");
    outcome=false;CaptureTimer::HandleMenu(CaptureTimer::CaptureNowCommand);
    check(WaitForCompletion()&&CaptureTimer::StatusText().find(L"失败")!=std::wstring::npos,L"failed worker clears busy state and shows failure");
    wc.lpfnWndProc=PopupProc;wc.lpszClassName=PopupClass;RegisterClassW(&wc);
    popupWindow=NewPopup();AttachCaptureControls(20);
    check(GetDlgItem(popupWindow,CaptureStatusControl)&&GetDlgItem(popupWindow,CaptureTimer::SettingsCommand)&&GetDlgItem(popupWindow,IDC_RESTORE_ALL),L"T panel has screenshot controls and a restore-all button");
    outcome=true;HWND popup=popupWindow;
    SendMessageW(popup,WM_COMMAND,CaptureTimer::CaptureNowCommand,0);
    MSG command={};bool posted=PeekMessageW(&command,mainWindow,WM_COMMAND,WM_COMMAND,PM_REMOVE)!=0;
    if(posted)DispatchMessageW(&command);
    check(posted&&command.wParam==CaptureTimer::CaptureNowCommand&&WaitForCompletion(),L"T immediate-capture button dispatches to worker");
    DebugLog::Write(L"INTEGRATION log marker");
    check(ReadText(DebugLog::path).find(L"COMPLETE received success=1")!=std::wstring::npos,L"local log records capture completion");
    WriteText(DebugLog::path,std::wstring(1024*1024,L'x'));DebugLog::Write(L"ROTATED marker");
    check(GetFileAttributesW((DebugLog::path+L".1").c_str())!=INVALID_FILE_ATTRIBUTES&&ReadText(DebugLog::path).find(L"ROTATED marker")!=std::wstring::npos,L"debug log rotates at one MiB");
    CaptureTimer::Shutdown();notificationGroups.clear();DestroyWindow(mainWindow);mainWindow=nullptr;
    WriteText(baseDir+L"\\report.txt",report);return ok?0:1;
}
