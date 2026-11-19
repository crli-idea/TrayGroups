#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"

static const UINT ActCallback=WM_APP+2;
// Isolated regression harness: two icons owned by this process, no clicks on user apps.
static std::vector<std::pair<WPARAM,LPARAM>> testEvents;
static bool buttonHostMessages = false;
static LRESULT CALLBACK TestProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==ActCallback){testEvents.push_back({w,l});return 0;}
    if(buttonHostMessages&&(msg==RestoreAllApp||msg==RefreshApp||msg==WM_COMMAND))
        return MainProc(h,msg,w,l);
    if(buttonHostMessages&&msg==CaptureTimer::CompletionMessage()){CaptureTimer::OnCompletion(w!=0);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
static bool PumpUntil(size_t count) { DWORD start=GetTickCount();MSG m;do{while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(testEvents.size()>=count)return true;Sleep(1);}while(GetTickCount()-start<1000);return false; }
static int SelfTest() {
    std::wstring failures;
    auto systemBefore=ReadTrayLocations();
    WriteText(baseDir+L"\\test-progress.txt",L"creating test receiver\n");
    WNDCLASSW c={};c.lpfnWndProc=TestProc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"Crli.TrayGroups.Test";RegisterClassW(&c);HWND h=CreateWindowW(c.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,c.hInstance,nullptr);
    bool pass=true;POINT p={134,257};unsigned checks=0;
    for(UINT version:{0u,4u})for(int action=0;action<3;action++){
        WriteText(baseDir+L"\\test-progress.txt",L"callback version="+Number(version)+L" action="+Number(action)+L"\n");
        TrayItem i;i.owner=h;i.id=301;i.callback=ActCallback;i.version=version;testEvents.clear();auto expected=CallbackSequence(i.id,version,action,p);
        if(!Dispatch(i,action,p)||!PumpUntil(expected.size())||testEvents!=expected)pass=false;
        UINT end=version>=4?(action==2?WM_CONTEXTMENU:NIN_SELECT):(action==2?WM_RBUTTONUP:WM_LBUTTONUP);
        if(testEvents.empty()||(version>=4?LOWORD(testEvents.back().second):(UINT)testEvents.back().second)!=end)pass=false;
        if(version>=4&&!testEvents.empty()&&(HIWORD(testEvents.back().second)!=301||GET_X_LPARAM(testEvents.back().first)!=p.x||GET_Y_LPARAM(testEvents.back().first)!=p.y))pass=false;
        checks++;
    }
    // Verify actual Explorer enumeration and hide/restore only on this test's icons.
    WriteText(baseDir+L"\\test-progress.txt",L"adding temporary notification icons\n");
    NOTIFYICONDATAW a={};a.cbSize=sizeof(a);a.hWnd=h;a.uID=401;a.uFlags=NIF_MESSAGE|NIF_TIP|NIF_ICON;a.uCallbackMessage=ActCallback;a.hIcon=LoadIconW(nullptr,IDI_INFORMATION);wcscpy_s(a.szTip,L"TrayGroups isolated self-test legacy");
    NOTIFYICONDATAW b=a;b.uID=402;wcscpy_s(b.szTip,L"TrayGroups isolated self-test v4");
    bool addedA=Shell_NotifyIconW(NIM_ADD,&a),addedB=Shell_NotifyIconW(NIM_ADD,&b);b.uVersion=4;Shell_NotifyIconW(NIM_SETVERSION,&b);testingOwnIcons=true;
    WriteText(baseDir+L"\\test-progress.txt",L"reading Explorer icons\n");
    Sleep(100);auto own=ReadTray();int found=0;
    for(auto& i:own)if(i.owner==h&&(i.id==401||i.id==402)){
        WriteText(baseDir+L"\\test-progress.txt",L"hide/restore temporary icon="+Number(i.id)+L"\n");
        if((i.id==401&&i.version!=0)||(i.id==402&&i.version!=4))pass=false;found++;
        if(!NativeVisibility(i,true)){pass=false;failures+=L"Native hide failed: "+Number(i.id)+L"\n";}
        auto after=ReadTray();auto match=std::find_if(after.begin(),after.end(),[&](const TrayItem& t){return t.owner==h&&t.id==i.id;});if(match==after.end()||!(match->state&TBSTATE_HIDDEN))pass=false;FreeItems(after);
        if(!NativeVisibility(i,false)){pass=false;failures+=L"Native restore failed: "+Number(i.id)+L"\n";}
        after=ReadTray();match=std::find_if(after.begin(),after.end(),[&](const TrayItem& t){return t.owner==h&&t.id==i.id;});if(match==after.end()||(match->state&TBSTATE_HIDDEN))pass=false;FreeItems(after);checks++;
    }
    if(!addedA||!addedB||found!=2){pass=false;failures+=L"Missing test notification icons\n";}
    FreeItems(own);own=ReadTray();
    // Exercise popup handlers directly against isolated icons, including deferred
    // single-click and WM_CONTEXTMENU for v4. The test popup stays invisible.
    for(auto& i:own)if(i.owner==h&&(i.id==401||i.id==402)){
        items.push_back(i);
    }
    if(items.size()==2){
        bool matched=CommandMatches(items[0])&&CommandMatches(items[1]);
        auto wrong=items[0];wrong.command=items[1].command;
        if(!matched||CommandMatches(wrong)){pass=false;failures+=L"Toolbar identity revalidation failed\n";}
        checks++;
    }else pass=false;
    WNDCLASSW pc={};pc.lpfnWndProc=PopupProc;pc.hInstance=GetModuleHandleW(nullptr);pc.lpszClassName=PopupClass;pc.style=CS_DBLCLKS;RegisterClassW(&pc);
    for(size_t n=0;n<items.size();n++)for(int action=0;action<3;action++){
        popupIcons={items[n]};testEvents.clear();POINT point;GetCursorPos(&point);auto expected=CallbackSequence(items[n].id,items[n].version,action,point);
        popupWindow=CreateWindowW(PopupClass,L"",WS_POPUP,0,0,420,120,nullptr,nullptr,pc.hInstance,nullptr);
        popupList=CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|LVS_ICON,0,0,420,120,popupWindow,nullptr,pc.hInstance,nullptr);
        NMITEMACTIVATE click={};click.hdr.hwndFrom=popupList;click.hdr.code=action==0?NM_CLICK:action==1?NM_DBLCLK:NM_RCLICK;click.iItem=0;
        SendMessageW(popupWindow,WM_NOTIFY,0,(LPARAM)&click);
        bool received=PumpUntil(expected.size());
        // The user can move their mouse during enumeration. Popup forwarding
        // samples the current cursor; test event semantics independently of that.
        bool equal=testEvents.size()==expected.size();
        if(equal)for(size_t j=0;j<expected.size();j++)if(testEvents[j].second!=expected[j].second||(items[n].version<4&&testEvents[j].first!=expected[j].first))equal=false;
        if(!received||!equal){pass=false;failures+=L"popup id="+Number(items[n].id)+L" version="+Number(items[n].version)+L" action="+Number(action)+L" expected="+Number(expected.size())+L" actual="+Number(testEvents.size())+L"\n";for(auto e:testEvents)failures+=Number(e.first)+L","+Number(e.second)+L" ";failures+=L"\nexpected: ";for(auto e:expected)failures+=Number(e.first)+L","+Number(e.second)+L" ";failures+=L"\n";}ClosePopup();checks++;
    }
    std::wstring originalRecovery=recoveryPath;recoveryPath=baseDir+L"\\test-restore.tmp";
    for(auto& i:items)i.group=L"Self-test";
    bool originalFold=settingsConfig.foldIcons;settingsConfig.foldIcons=true;FoldAssigned();
    if(hidden.size()!=2){pass=false;failures+=L"Expected two hidden test icons; actual "+Number(hidden.size())+L"\n";}
    auto verify=ReadTray();for(auto& i:verify)if(i.owner==h&&(i.id==401||i.id==402)&&!(i.state&TBSTATE_HIDDEN))pass=false;FreeItems(verify);checks++;
    RestoreHidden();if(!hidden.empty()||GetFileAttributesW(recoveryPath.c_str())!=INVALID_FILE_ATTRIBUTES)pass=false;
    verify=ReadTray();for(auto& i:verify)if(i.owner==h&&(i.id==401||i.id==402)&&(i.state&TBSTATE_HIDDEN))pass=false;FreeItems(verify);checks++;


    // Simulate loss of in-memory state and restore solely from the recovery journal.
    for(auto& item:items)item.group=L"RecoveryTest";FoldAssigned();
    bool journalReady=hidden.size()==2&&GetFileAttributesW(recoveryPath.c_str())!=INVALID_FILE_ATTRIBUTES;
    hidden.clear();RecoverIcons();
    auto recovered=ReadTray();bool allRecovered=true;unsigned recoveredCount=0;
    for(const auto& item:recovered)if(item.owner==h&&(item.id==401||item.id==402)){
        recoveredCount++;if(item.state&TBSTATE_HIDDEN)allRecovered=false;
    }
    if(!journalReady||!allRecovered||recoveredCount!=2||GetFileAttributesW(recoveryPath.c_str())!=INVALID_FILE_ATTRIBUTES){pass=false;failures+=L"Journal-only recovery failed\n";}
    checks++;FreeItems(recovered);

    // A living owner with an unenumerated icon must keep its recovery journal.
    if(!items.empty()){
        auto unresolved=items[0];unresolved.id=62401;unresolved.runtime.id=62401;
        hidden.push_back({unresolved.toolbar,h,unresolved.id,unresolved.command,unresolved.runtime});WriteRecovery();
        MainProc(h,StopApp,0,0);
        if(hidden.empty()||!IsWindow(h)||GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES){pass=false;failures+=L"Exit lost unresolved recovery record\n";}
        checks++;hidden.clear();DeleteFileW(recoveryPath.c_str());settingsConfig.foldIcons=true;
        KillTimer(mainWindow,RestoreTimer);lifecycle.CancelExit();CaptureTimer::stopRequested=false;
    }
    auto savedAssignments=settingsConfig.assignments;auto savedPrograms=settingsConfig.programAssignments;auto savedPaths=settingsConfig.pathAssignments;
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();
    if(items.size()==2){
        auto key=IconKey(items[0],L"path-class-id");settingsConfig.assignments[key]=L"CollisionTest";matcher.ResolveGroups(items);FoldAssigned();
        HWND duplicate=CreateWindowW(c.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,c.hInstance,nullptr);
        NOTIFYICONDATAW duplicateIcon=a;duplicateIcon.hWnd=duplicate;duplicateIcon.uID=items[0].id;wcscpy_s(duplicateIcon.szTip,L"Isolated duplicate identity");
        bool added=Shell_NotifyIconW(NIM_ADD,&duplicateIcon)!=FALSE;
        auto collision=ReadTray();unsigned pendingCount=0;
        for(const auto& item:collision)if((item.owner==h||item.owner==duplicate)&&item.id==duplicateIcon.uID&&item.pending)pendingCount++;
        FreeItems(items);items=std::move(collision);RestoreUnassigned();
        auto after=ReadTray();bool restored=false;
        for(const auto& item:after)if(item.owner==h&&item.id==duplicateIcon.uID)restored=!(item.state&TBSTATE_HIDDEN)&&item.pending;
        if(!added||pendingCount!=2||!restored||!hidden.empty()){pass=false;failures+=L"live identity collision failed to restore the test icon\n";}
        checks++;FreeItems(after);Shell_NotifyIconW(NIM_DELETE,&duplicateIcon);DestroyWindow(duplicate);
    }else pass=false;
    settingsConfig.assignments=savedAssignments;settingsConfig.programAssignments=savedPrograms;settingsConfig.pathAssignments=savedPaths;matchState.pendingRules.clear();matchState.ruleBindings.clear();
    settingsConfig.foldIcons=originalFold;recoveryPath=originalRecovery;
    FreeItems(items);FreeItems(own);Shell_NotifyIconW(NIM_DELETE,&a);Shell_NotifyIconW(NIM_DELETE,&b);testingOwnIcons=false;
    // Round-trip Unicode group assignments, stable keys, and removal.
    WriteText(baseDir+L"\\test-progress.txt",L"configuration round-trip\n");
    auto original=settingsConfig.assignments;std::wstring oldConfig=configPath;configPath=baseDir+L"\\test-groups.tmp";
    TrayItem network;network.path=L"c:\\tools\\network.exe";network.process=L"network.exe";network.id=2;
    TrayItem notes=network;notes.path=L"c:\\tools\\notes.exe";notes.id=0;
    settingsConfig.assignments={{IconKey(network,settingsConfig.iconMatch),L"网络"},{IconKey(notes,settingsConfig.iconMatch),L"工具"}};
    bool folded=settingsConfig.foldIcons;settingsConfig.foldIcons=false;if(!SaveSettings())pass=false;settingsConfig.assignments.clear();LoadSettings();if(settingsConfig.assignments.size()!=2||matcher.GroupFor(network)!=L"网络"||settingsConfig.foldIcons)pass=false;
    DeleteFileW(configPath.c_str());settingsConfig.assignments=original;configPath=oldConfig;settingsConfig.foldIcons=folded;checks++;

    auto systemAfter=ReadTrayLocations();bool systemStates=true;unsigned compared=0;
    for(const auto& before:systemBefore){
        if(Lower(before.process)!=L"explorer.exe")continue;
        auto after=std::find_if(systemAfter.begin(),systemAfter.end(),[&](const TrayItem& item){return item.runtime==before.runtime&&item.toolbar==before.toolbar;});
        if(after==systemAfter.end())continue;
        compared++;
        if((after->state&TBSTATE_HIDDEN)!=(before.state&TBSTATE_HIDDEN)){systemStates=false;failures+=L"System icon state changed: "+FriendlyIconName(before)+L"\n";}
    }
    if(!systemStates||!compared)pass=false;checks++;
    FreeItems(systemBefore);FreeItems(systemAfter);
    WriteText(baseDir+L"\\self-test.txt",(pass?L"PASS":L"FAIL")+std::wstring(L": ")+Number(checks)+L" checks: legacy/v4 left, double, right callbacks; actual Explorer test-icon enumeration and hide/restore; Unicode settings round-trip; system icon visibility unchanged. No external app was clicked.\n"+failures);
    DestroyWindow(h);return pass?0:1;
}

static bool IdentityRegression(std::wstring& report) {
    bool ok=true;auto check=[&](bool result,const wchar_t* name){report+=(result?L"PASS: ":L"FAIL: ")+std::wstring(name)+L"\n";if(!result)ok=false;};
    HMENU captureMenu=CreatePopupMenu();CaptureTimer::AddMenu(captureMenu);
    check(GetMenuItemCount(captureMenu)==1&&GetMenuItemCount(GetSubMenu(captureMenu,0))==7,L"scheduled capture is available as a submenu without another tray entry");DestroyMenu(captureMenu);
    auto previousConfig=configPath;configPath=baseDir+L"\\default-identity.ini";DeleteFileW(configPath.c_str());LoadSettings();
    check(settingsConfig.iconMatch==L"path-guid",L"new configuration defaults to full-path and actual GUID identity");
    configPath=previousConfig;settingsConfig.iconMatch=L"path-class-id";
    configPath=baseDir+L"\\identity-config.ini";settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();settingsConfig.groupLabelSizes.clear();settingsConfig.groupLabelWidths.clear();settingsConfig.groupLabelHeights.clear();settingsConfig.iconMatch=L"path-class-id";
    TrayItem a;a.process=L"explorer.exe";a.path=L"C:\\Windows\\explorer.exe";a.ownerClass=L"BluetoothNotificationAreaIconWindowClass";a.id=0;a.tip=L"蓝牙设备";
    TrayItem b=a;b.ownerClass=L"VolumeWindow";b.tip=L"耳机: 12%";
    TrayItem c=b;c.path=L"D:\\Other\\explorer.exe";
    settingsConfig.assignments[IconKey(a,settingsConfig.iconMatch)]=L"连接";settingsConfig.assignments[IconKey(b,settingsConfig.iconMatch)]=L"音量";
    check(IconKey(a,settingsConfig.iconMatch)!=IconKey(b,settingsConfig.iconMatch)&&matcher.GroupFor(a)==L"连接"&&matcher.GroupFor(b)==L"音量"&&matcher.GroupFor(c).empty(),L"same executable and same icon ID distinguished by owner class; different full paths stay separate");
    wchar_t systemDir[MAX_PATH]={};GetSystemDirectoryW(systemDir,MAX_PATH);
    std::wstring explorerPath=std::wstring(systemDir).substr(0,std::wstring(systemDir).find_last_of(L'\\'))+L"\\explorer.exe";
    TrayItem volume;volume.path=explorerPath;volume.ownerClass=L"ATL@sndvolsso.dll";volume.id=100;
    TrayItem bluetooth;bluetooth.path=explorerPath;bluetooth.ownerClass=L"BluetoothNotificationAreaIconWindowClass";bluetooth.id=0;
    check(IconKey(volume,L"path-class-id")!=IconKey(bluetooth,L"path-class-id"),
        L"Explorer volume and Bluetooth icons have distinct path, window-class, and icon-ID fallback identities");
    TrayItem systemAudio=a;systemAudio.ownerClass=L"atl@sndvolsso.dll";systemAudio.group=L"";
    TrayItem systemBluetooth=a;systemBluetooth.ownerClass=L"BluetoothNotificationAreaIconWindowClass";systemBluetooth.group=L"";
    systemAudio.tip=L"原程序音量提示";systemBluetooth.tip=L"原程序蓝牙提示";
    check(FriendlyIconName(systemAudio)==systemAudio.tip&&FriendlyIconName(systemBluetooth)==systemBluetooth.tip,L"labels use application tips without name tables");
    systemAudio.group=L"音量";
    auto changing=b;changing.tip=L"耳机: 87%";changing.owner=(HWND)0x1234;check(matcher.GroupFor(changing)==L"音量",L"dynamic tooltip and window handle do not affect default identity");
    a.ownerClass=L"ATL:0000ABCD";b=a;b.ownerClass=L"ATL:0000DEF0";check(IconKey(a,settingsConfig.iconMatch)==IconKey(b,settingsConfig.iconMatch),L"process-specific ATL addresses normalized");
    a.ownerClass=L"Bluetooth";b=a;settingsConfig.programAssignments[L"explorer.exe"]=L"整程序";settingsConfig.assignments[IconKey(a,settingsConfig.iconMatch)]=L"";
    check(matcher.GroupFor(a).empty(),L"explicit ungrouped icon overrides broad executable rule");
    std::vector<TrayItem> ambiguous={a,b};matcher.ResolveGroups(ambiguous);
    check(ambiguous[0].group.empty()&&ambiguous[1].group.empty()&&ambiguous[0].pending&&ambiguous[1].pending,L"ambiguous per-icon rule blocks executable fallback and leaves both icons pending");
    std::vector<TrayItem> remaining={a};matcher.ResolveGroups(remaining);
    check(remaining[0].pending&&remaining[0].group.empty(),L"ambiguity stays pending after a duplicate exits");
    matchState.pendingRules.clear();matchState.ruleBindings.clear();
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments[Lower(c.path)]=L"独立路径";
    check(matcher.GroupFor(c)==L"独立路径"&&matcher.GroupFor(a).empty(),L"full-path grouping does not affect same filename elsewhere");
    settingsConfig.iconMatch=L"path-id";settingsConfig.assignments[IconKey(c,settingsConfig.iconMatch)]=L"按ID";
    settingsConfig.iconMatch=L"path-class-tip";settingsConfig.assignments[IconKey(a,settingsConfig.iconMatch)]=L"按提示";
    check(matcher.GroupFor(c)==L"独立路径"&&matcher.GroupFor(a)==L"按提示",L"only the selected naming mode applies");
    a.path=L"C:\\test=folder\\explorer.exe";a.tip=L"Name=first|second\nnext";auto escaped=IconKey(a,settingsConfig.iconMatch);settingsConfig.assignments[escaped]=L"转义";
    check(escaped.find(L'=')==std::wstring::npos&&RulePath(escaped)==Lower(a.path),L"INI delimiters and multiline tooltip escaped without changing the saved path");
    settingsConfig.groupLabelSizes[L"连接"]=9;settingsConfig.groupLabelSizes[L"音量"]=24;settingsConfig.settingsX=-500;settingsConfig.settingsY=80;settingsConfig.haveSettingsPosition=true;
    settingsConfig.assignments[IconKey(c,settingsConfig.iconMatch)]=L"";
    bool saved=SaveSettings();settingsConfig.assignments.clear();settingsConfig.pathAssignments.clear();settingsConfig.groupLabelSizes.clear();settingsConfig.groupLabelWidths.clear();settingsConfig.groupLabelHeights.clear();LoadSettings();
    check(saved&&settingsConfig.assignments.count(escaped)&&matcher.GroupFor(c).empty()&&settingsConfig.GroupLabelSize(L"连接")==9&&settingsConfig.GroupLabelSize(L"音量")==24&&settingsConfig.haveSettingsPosition&&settingsConfig.settingsX==-500&&settingsConfig.settingsY==80&&settingsConfig.iconMatch==L"path-class-tip",L"INI round-trip: identities, explicit exclusions, paths, per-group sizes and negative window coordinates");
    WriteText(configPath,ReadText(configPath)+L"\n[Unknown]\nValue=keep\n");
    check(LabelSize(2)==8&&LabelSize(100)==32,L"font size bounds");
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();settingsConfig.iconMatch=L"path-class-id";
    TrayItem first;first.path=L"C:\\test\\same.exe";first.process=L"same.exe";first.ownerClass=L"SameClass";first.id=7;first.owner=(HWND)101;first.callback=WM_APP+30;first.runtime={1234,1000,first.owner,7};
    TrayItem second=first;second.owner=(HWND)102;second.runtime.owner=second.owner;
    auto key=IconKey(first,settingsConfig.iconMatch);settingsConfig.assignments[key]=L"\u5355\u4e2a";settingsConfig.programAssignments[L"same.exe"]=L"\u5168\u90e8";settingsConfig.pathAssignments[Lower(first.path)]=L"\u8def\u5f84";
    std::vector<TrayItem> live={first};matcher.ResolveGroups(live);
    check(!live[0].pending&&live[0].group==L"\u5355\u4e2a"&&matchState.ruleBindings.count(key),L"single selector binds to the current process lifetime and owning window");
    live={second};matcher.ResolveGroups(live);
    check(live[0].pending&&live[0].group.empty(),L"same saved selector cannot transfer to a different owning window during this run");
    auto restarted=first;restarted.runtime.started++;
    check(!SameRuntime(first,restarted),L"PID and window reuse across process restarts is rejected");
    auto otherId=first;otherId.id++;otherId.runtime.id=otherId.id;check(!SameRuntime(first,otherId),L"different tray icon IDs remain distinct at runtime");
    check(!SameRuntime(first,second),L"same path, class and icon ID still represent distinct runtime windows");
    matcher.ForgetRule(key);settingsConfig.assignments[key]=L"\u5df2\u786e\u8ba4";live={second};matcher.ResolveGroups(live);
    check(!live[0].pending&&live[0].group==L"\u5df2\u786e\u8ba4",L"explicit reselection confirms a recreated icon");
    settingsConfig.assignments[IconKey(second,L"path-id")]=L"\u53e6\u4e00\u7ec4";live={second};matcher.ResolveGroups(live);
    check(!live[0].pending&&live[0].group==L"\u5df2\u786e\u8ba4",L"inactive naming modes do not compete with the current selector");
    matchState.pendingRules.clear();matchState.ruleBindings.clear();settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();

    WriteText(baseDir+L"\\identity-self-test.txt",report);return ok;
}
static bool SaveTestWindow(HWND h,const std::wstring& file) {
    RECT r;GetWindowRect(h,&r);int width=r.right-r.left,height=r.bottom-r.top;
    HDC screen=GetWindowDC(h),mem=CreateCompatibleDC(screen);BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bmp=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);HGDIOBJ old=SelectObject(mem,bmp);if(!PrintWindow(h,mem,0))BitBlt(mem,0,0,width,height,screen,0,0,SRCCOPY);
    BITMAPFILEHEADER fh={};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(BITMAPINFOHEADER);fh.bfSize=fh.bfOffBits+width*height*4;
    HANDLE out=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);DWORD written=0;bool ok=out!=INVALID_HANDLE_VALUE;
    if(ok){ok=WriteFile(out,&fh,sizeof(fh),&written,nullptr)&&WriteFile(out,&bi.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr)&&WriteFile(out,pixels,width*height*4,&written,nullptr);CloseHandle(out);}
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(h,screen);return ok;
}

static bool HiddenPopupRegression(std::wstring& report) {
    bool ok=true;
    auto check=[&](bool passed,const wchar_t* label){report+=(passed?L"PASS: ":L"FAIL: ")+std::wstring(label)+L"\n";if(!passed)ok=false;};
    std::wstring oldConfig=configPath,oldRecovery=recoveryPath;
    configPath=baseDir+L"\\hidden-popup-config.ini";recoveryPath=baseDir+L"\\hidden-popup-restore.tsv";
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();
    settingsConfig.groupLabelSizes.clear();settingsConfig.groupLabelWidths.clear();settingsConfig.groupLabelHeights.clear();settingsConfig.iconMatch=L"path-class-id";settingsConfig.foldIcons=true;
    testingOwnIcons=true;
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=mainWindow;icon.uFlags=NIF_MESSAGE|NIF_TIP|NIF_ICON;
    icon.uCallbackMessage=ActCallback;icon.hIcon=LoadIconW(nullptr,IDI_INFORMATION);
    icon.uID=701;wcscpy_s(icon.szTip,L"TrayGroups click then hide test");
    NOTIFYICONDATAW untouched=icon;untouched.uID=702;wcscpy_s(untouched.szTip,L"TrayGroups leave to Windows test");
    bool added=Shell_NotifyIconW(NIM_ADD,&icon)&&Shell_NotifyIconW(NIM_ADD,&untouched);
    icon.uVersion=4;Shell_NotifyIconW(NIM_SETVERSION,&icon);
    check(added,L"isolated grouped and ungrouped test icons added");
    SaveSettings();ShowSettings();
    int row=-1;TrayItem identity;
    for(size_t n=0;n<settingsItems.size();n++)if(settingsItems[n].owner==mainWindow&&settingsItems[n].id==701){row=(int)n;identity=settingsItems[n];identity.image=nullptr;}
    check(row>=0&&VisibleAtRuntime(identity.runtime),L"selected test icon starts visible in Windows");
    if(row>=0){
        ListView_SetItemState(listWindow,-1,0,LVIS_SELECTED);
        ListView_SetItemState(listWindow,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        // Click the icon before assigning it, then choose the Hidden group via real settings controls.
        testEvents.clear();Dispatch(identity,0,POINT{120,120});PumpUntil(3);
        SetWindowTextW(groupEdit,L"\u9690\u85cf");
        SendMessageW(settingsWindow,WM_COMMAND,MAKEWPARAM(IDC_GROUP,CBN_EDITCHANGE),(LPARAM)groupEdit);
        SendMessageW(foldCheck,BM_SETCHECK,BST_CHECKED,0);
        SendMessageW(GetDlgItem(settingsWindow,IDC_APPLY),BM_CLICK,0,0);
        check(!VisibleAtRuntime(identity.runtime)&&hidden.size()==1,L"click icon then assign Hidden and Apply really hides only that icon");
        for(const auto& item:settingsItems)if(item.owner==mainWindow&&item.id==702)check(VisibleAtRuntime(item.runtime),L"ungrouped test icon remains visible in Windows");
        SaveTestWindow(settingsWindow,baseDir+L"\\hidden-popup-assigned.bmp");
    }
    if(settingsWindow)SendMessageW(settingsWindow,WM_CLOSE,0,0);
    for(int action=0;action<3;action++){
        // Exercise the actual T handler, not just the popup construction function.
        MainProc(mainWindow,WM_APP+8,1,1);
        bool onlyHidden=popupIcons.size()==1&&popupIcons[0].owner==mainWindow&&popupIcons[0].id==701;
        check(onlyHidden&&popupGroups==std::vector<std::wstring>{L"\u9690\u85cf"},L"T immediately shows only hidden grouped icon and no ungrouped entry");
        if(popupWindow&&onlyHidden){
            RedrawWindow(popupWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            if(action==0)SaveTestWindow(popupWindow,baseDir+L"\\hidden-popup-visible.bmp");
            RECT tile={};ListView_GetItemRect(popupList,0,&tile,LVIR_ICON);testEvents.clear();
            LPARAM point=MAKELPARAM((tile.left+tile.right)/2,(tile.top+tile.bottom)/2);
            if(action==1)SendMessageW(popupList,WM_LBUTTONDBLCLK,MK_LBUTTON,point);
            else {PostMessageW(popupList,action==0?WM_LBUTTONDOWN:WM_RBUTTONDOWN,action==0?MK_LBUTTON:MK_RBUTTON,point);
                  PostMessageW(popupList,action==0?WM_LBUTTONUP:WM_RBUTTONUP,0,point);}
            // Drive the single-click timer explicitly so desktop focus changes
            // during an automated screenshot cannot cancel the pending action.
            if(action==0&&popupWindow)SendMessageW(popupWindow,WM_TIMER,SingleTimer,0);
            auto expected=CallbackSequence(701,4,action,POINT{});
            bool delivered=PumpUntil(expected.size()),same=testEvents.size()==expected.size();
            if(same)for(size_t n=0;n<expected.size();n++)if(testEvents[n].second!=expected[n].second)same=false;
            check(delivered&&same&&!popupWindow,(L"hidden popup forwards original "+std::wstring(action==0?L"left click":action==1?L"double click":L"right click")+L" and closes").c_str());
        }else ClosePopup();
    }
    for(int action : {0,2}) {
        ShowGroups();ListView_SetItemState(popupList,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        testEvents.clear();SendMessageW(popupList,WM_KEYDOWN,action==0?VK_RETURN:VK_APPS,0);
        auto expected=CallbackSequence(701,4,action,POINT{});
        bool delivered=PumpUntil(expected.size()),same=testEvents.size()==expected.size();
        if(same)for(size_t n=0;n<expected.size();++n)if(testEvents[n].second!=expected[n].second)same=false;
        check(delivered&&same&&!popupWindow,action==0?L"Enter forwards original left-click from selected system list item":L"keyboard menu key forwards original context menu from selected system list item");
    }
    MainProc(mainWindow,WM_APP+8,1,1);
    RECT original={};
    if(popupWindow){
        RECT work=CursorWorkArea();
        SetWindowPos(popupWindow,nullptr,work.left+60,work.top+60,0,0,SWP_NOSIZE|SWP_NOZORDER);
        GetWindowRect(popupWindow,&original);
    }
    bool stable=popupWindow!=nullptr;
    for(int repeat=0;repeat<8&&popupWindow;repeat++){
        RECT label={};ListView_GetGroupRect(popupList,0,LVGGR_HEADER,&label);MapWindowPoints(popupList,popupWindow,(POINT*)&label,2);
        SendMessageW(popupWindow,WM_LBUTTONUP,0,MAKELPARAM(label.left+10,label.top+10));
        // Consume the posted rebuild without depending on the desktop cursor.
        MSG message;
        if(PeekMessageW(&message,popupWindow,WM_APP+20,WM_APP+20,PM_REMOVE))DispatchMessageW(&message);
        RECT next={};if(popupWindow)GetWindowRect(popupWindow,&next);
        if(next.left!=original.left||next.top!=original.top)stable=false;
    }
    check(stable,L"repeated Hidden tab clicks preserve popup screen position without drifting left or up");
    ClosePopup();
    // Simulate a program revealing its own icon after receiving an operation.
    check(NativeVisibility(identity,false)&&VisibleAtRuntime(identity.runtime),L"simulate application revealing the previously folded icon after a click");
    Refresh();
    check(!VisibleAtRuntime(identity.runtime)&&hidden.size()==1,L"refresh folds a reappearing icon again without duplicating recovery records");
    // The production deferred timer uses the same path after a forwarded operation.
    NativeVisibility(identity,false);reFoldAttempts=1;MainProc(mainWindow,WM_TIMER,ReFoldTimer,0);
    check(!VisibleAtRuntime(identity.runtime),L"post-click timer folds a program icon revealed by its own update");
    RestoreHidden();settingsConfig.foldIcons=false;Refresh();
    ShowGroups();check(popupIcons.empty(),L"paused folding does not duplicate visible Windows icons in T");
    ClosePopup();ShowGroups();check(popupIcons.empty()&&std::find(popupGroups.begin(),popupGroups.end(),L"")==popupGroups.end(),L"empty-group requests never expose Windows-owned ungrouped icons");
    ClosePopup();RestoreHidden();
    check(VisibleAtRuntime(identity.runtime)&&hidden.empty()&&GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES,L"all hidden test icons restored and recovery journal removed");
    KillTimer(mainWindow,ReFoldTimer);reFoldAttempts=0;
    Shell_NotifyIconW(NIM_DELETE,&icon);Shell_NotifyIconW(NIM_DELETE,&untouched);
    testingOwnIcons=false;FreeItems(items);FreeItems(settingsItems);
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();
    configPath=oldConfig;recoveryPath=oldRecovery;LoadSettings();
    return ok;
}


static bool GuidRegression(std::wstring& report) {
    bool ok=true;auto check=[&](bool passed,const wchar_t* label){report+=(passed?L"PASS: ":L"FAIL: ")+std::wstring(label)+L"\n";if(!passed)ok=false;};
    auto previousRecovery=recoveryPath;recoveryPath=baseDir+L"\\guid-test-restore.tsv";
    auto actual=ReadTray();auto snapshot=TrayGuid::ReadSnapshot();
    for(const auto& item:actual)if(item.usesGuid&&!(item.state&TBSTATE_HIDDEN)){
        GUID resolved={};check(TrayGuid::FindLiveGuid(snapshot,item.owner,item.id,item.path,resolved)&&IsEqualGUID(resolved,item.guid),L"live GUID comes from generic Explorer metadata");
        check(!TrayGuid::FindLiveGuid(snapshot,item.owner,item.id,item.path+L".wrong",resolved),L"wrong executable path cannot claim a live GUID");
    }
    FreeItems(actual);
    bool oldFold=settingsConfig.foldIcons;settingsConfig.foldIcons=true;testingOwnIcons=true;
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();
    GUID guid={};check(SUCCEEDED(CoCreateGuid(&guid)),L"generate random GUID without a program lookup");
    settingsConfig.iconMatch=L"path-guid";
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=mainWindow;icon.uID=703;icon.guidItem=guid;
    icon.uFlags=NIF_GUID|NIF_ICON|NIF_TIP|NIF_MESSAGE;icon.hIcon=LoadIconW(nullptr,IDI_INFORMATION);icon.uCallbackMessage=ActCallback;
    wcscpy_s(icon.szTip,L"TrayGroups GUID hiding regression");
    SetLastError(ERROR_SUCCESS);bool added=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE;DWORD addError=added?ERROR_SUCCESS:GetLastError();icon.uVersion=4;Shell_NotifyIconW(NIM_SETVERSION,&icon);
    FreeItems(items);items=ReadTray();TrayItem identity;bool found=false;
    for(const auto& item:items)if(item.owner==mainWindow&&item.id==703){identity=item;identity.image=nullptr;found=true;}
    report+=L"GUID registration: added="+Number(added)+L" win32="+Number(addError)+L" found="+Number(found)+L"\n";
    check(added&&found&&identity.usesGuid,L"GUID-addressed tray icon is recognized separately from owner and numeric ID");
    if(found){
        auto key=IconKey(identity,L"path-guid");auto changed=identity;
        changed.ownerClass=L"ChangedClass";changed.id=904;changed.owner=(HWND)987;changed.tip=L"Different tooltip";
        check(key.find(L"path-guid|")==0&&key==IconKey(changed,L"path-guid"),L"path plus GUID ignores callback window, numeric ID and tooltip changes");
        changed=identity;changed.guid.Data1^=1;
        check(key!=IconKey(changed,L"path-guid"),L"two GUID icons from the same executable have different persisted identities");
        changed=identity;changed.path=L"D:\\Other\\Program.exe";
        check(key!=IconKey(changed,L"path-guid"),L"same GUID under a different executable path is a distinct persisted identity");
        changed=identity;changed.usesGuid=false;
        check(IconKey(changed,L"path-guid")==IconKey(changed,L"path-class-id"),L"missing GUID falls back to existing path class and numeric ID instead of inventing one");
        settingsConfig.assignments[IconKey(identity,L"path-class-id")]=L"GUID group";
        matcher.ResolveGroups(items);check(matcher.GroupFor(identity).empty(),L"old class-id rules are not aliases for the default GUID key");
        settingsConfig.assignments.clear();settingsConfig.iconMatch=L"path-guid";settingsConfig.assignments[key]=L"GUID group";
        auto oldConfig=configPath;configPath=baseDir+L"\\guid-identity.ini";
        bool saved=SaveSettings();settingsConfig.assignments.clear();LoadSettings();
        check(saved&&settingsConfig.iconMatch==L"path-guid"&&matcher.GroupFor(identity)==L"GUID group",L"path plus GUID matching mode and assignment persist through INI save and load");
        configPath=oldConfig;
        matcher.ResolveGroups(items);FoldAssigned();
        check(hidden.size()==1&&!VisibleAtRuntime(identity.runtime),L"GUID-addressed tray icon really hides through Shell notification state API");
        ClosePopup();ShowGroups();bool present=false;
        for(const auto& icon:popupIcons)if(icon.owner==mainWindow&&icon.id==703)present=true;
        check(present,L"GUID hidden icon appears in T popup");
        ClosePopup();
        testEvents.clear();POINT pos={100,100};bool sent=Forward(identity,2,pos);auto expected=CallbackSequence(703,4,2,pos);
        check(sent&&PumpUntil(expected.size())&&testEvents==expected,L"GUID hidden icon retains original right-click callback");
        RestoreHidden();
        check(hidden.empty()&&VisibleAtRuntime(identity.runtime),L"GUID-addressed tray icon restores normally");
        Refresh();check(hidden.size()==1&&!VisibleAtRuntime(identity.runtime),L"GUID-addressed icon folds again after restore");
        hidden.clear();RecoverIcons();
        check(VisibleAtRuntime(identity.runtime)&&GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES,L"GUID icon restores from persisted journal after in-memory state is lost");
    }
    RestoreHidden();Shell_NotifyIconW(NIM_DELETE,&icon);
    FreeItems(items);testingOwnIcons=false;settingsConfig.foldIcons=oldFold;recoveryPath=previousRecovery;
    settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();matchState.pendingRules.clear();matchState.ruleBindings.clear();LoadSettings();
    return ok;
}

#include "TrayInspector.tests.h"
#include "NativeTrayPanel.tests.h"
#include "WallpaperPage.tests.h"
#include "SettingsButtons.tests.h"

static int UiSelfTest() {
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&common);
    NONCLIENTMETRICSW metrics={};metrics.cbSize=sizeof(metrics);SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);appFont=CreateFontIndirectW(&metrics.lfMessageFont);appIcon=MakeAppIcon();
    WNDCLASSW c={};c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);c.hIcon=appIcon;c.lpfnWndProc=PopupProc;c.lpszClassName=PopupClass;c.style=CS_DBLCLKS;RegisterClassW(&c);
    c.lpfnWndProc=TestProc;c.lpszClassName=L"Crli.TrayGroups.UiReceiver";RegisterClassW(&c);mainWindow=CreateWindowW(c.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,c.hInstance,nullptr);
    c.lpfnWndProc=ResourceProc;c.lpszClassName=L"Crli.TrayGroups.Resources";RegisterClassW(&c);
    CaptureTimer::MakeDirectories(baseDir+L"\\native-ui-capture");
    CaptureTimer::Initialize(mainWindow,GetModuleHandleW(nullptr),baseDir+L"\\native-ui-capture");
    KillTimer(mainWindow,CaptureTimer::TimerId());
    UINT testDpi=GetDpiForWindow(mainWindow);std::wstring report;bool ok=IdentityRegression(report);auto check=[&](bool result,const wchar_t* name){report+=(result?L"PASS: ":L"FAIL: ")+std::wstring(name)+L"\n";if(!result)ok=false;};
    configPath=baseDir+L"\\ui-test-config.ini";settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();settingsConfig.groupLabelSizes.clear();settingsConfig.groupLabelWidths.clear();settingsConfig.groupLabelHeights.clear();settingsConfig.iconMatch=L"path-class-id";settingsConfig.haveSettingsPosition=false;
    if(!HiddenPopupRegression(report))ok=false;
    if(!GuidRegression(report))ok=false;
    if(!InspectorRegression(report))ok=false;
    configPath=baseDir+L"\\ui-test-config.ini";settingsConfig.assignments.clear();settingsConfig.programAssignments.clear();settingsConfig.pathAssignments.clear();settingsConfig.groupLabelSizes.clear();settingsConfig.groupLabelWidths.clear();settingsConfig.groupLabelHeights.clear();settingsConfig.foldIcons=false;settingsConfig.iconMatch=L"path-class-id";settingsConfig.haveSettingsPosition=false;
    settingsConfig=TrayConfig::Settings{};settingsConfig.foldIcons=false;settingsConfig.iconMatch=L"path-class-id";SaveSettings();
    ShowSettings();check(settingsWindow&&IsWindowVisible(settingsWindow)&&ListView_GetItemCount(listWindow)>0,L"settings window visible with detected program icons");
    if(!settingsWindow||ListView_GetItemCount(listWindow)==0){WriteText(baseDir+L"\\ui-self-test.txt",report);return 1;}
    check(TabCtrl_GetItemCount(settingsTabs)==SettingsPageCount&&IsWindowVisible(settingsPages[0])&&!IsWindowVisible(settingsPages[1])&&!IsWindowVisible(settingsPages[2]),L"six system property pages with only the current page visible");
    check(SendMessageW(settingsWindow,WM_GETICON,ICON_SMALL,0)!=0,L"settings window has the T application icon");
    check((GetWindowLongPtrW(GetDlgItem(settingsWindow,IDC_APPLY),GWL_STYLE)&BS_TYPEMASK)!=BS_OWNERDRAW,
          L"standard property sheet buttons use system painting");
    check(IsAppThemed()&&IsThemeActive(),L"common controls v6 visual styles are enabled");
    int order[6]={};ListView_GetColumnOrderArray(listWindow,6,order);
    check(order[0]==0&&order[3]==5&&order[4]==3,L"matching and hidden status appear before full executable paths");
    HDC fontDc=GetDC(groupEdit);HFONT settingsFont=(HFONT)SendMessageW(groupEdit,WM_GETFONT,0,0);HGDIOBJ oldFont=SelectObject(fontDc,settingsFont);
    wchar_t face[LF_FACESIZE]={};GetTextFaceW(fontDc,LF_FACESIZE,face);SelectObject(fontDc,oldFont);ReleaseDC(groupEdit,fontDc);
    report+=L"SettingsFont="+std::wstring(face)+L"\n";

    std::set<std::wstring> explorerKeys;unsigned explorerCount=0;bool explorerUnique=true;std::wstring inventory;
    for(const auto& i:settingsItems)if(Lower(i.process)==L"explorer.exe"){
        auto key=IconKey(i,L"path-class-id");if(!explorerKeys.insert(key).second)explorerUnique=false;explorerCount++;
        inventory+=Escape(i.tip)+L"\t"+key+L"\n";
    }
    WriteText(baseDir+L"\\explorer-identities.tsv",inventory);
    check(explorerCount<2||explorerUnique,L"actual Explorer-hosted icons have distinct persisted identities");

    // Use synthetic explorer rows for deterministic UI edits. Never click a user app.
    FreeItems(settingsItems);ListView_DeleteAllItems(listWindow);
    TrayItem a;a.process=L"explorer.exe";a.path=L"C:\\Windows\\explorer.exe";a.ownerClass=L"Bluetooth";a.id=0;a.tip=L"蓝牙";
    TrayItem b=a;b.ownerClass=L"Volume";b.tip=L"音量";
    TrayItem d=a;d.path=L"D:\\Other\\explorer.exe";
    settingsItems={a,b,d};
    for(int n=0;n<3;n++){LVITEMW row={};row.mask=LVIF_TEXT;row.iItem=n;row.pszText=(LPWSTR)settingsItems[n].process.c_str();ListView_InsertItem(listWindow,&row);}
    ListView_SetItemState(listWindow,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    COMBOBOXINFO box={};box.cbSize=sizeof(box);bool editable=GetComboBoxInfo(groupEdit,&box)&&box.hwndItem&&IsWindowEnabled(box.hwndItem);check(editable,L"group name edit accepts keyboard input");
    if(editable){
        SetWindowTextW(groupEdit,L"");SetFocus(box.hwndItem);for(wchar_t ch:std::wstring(L"测试分组"))SendMessageW(box.hwndItem,WM_CHAR,ch,1);
        wchar_t text[128];GetWindowTextW(groupEdit,text,128);check(std::wstring(text)==L"测试分组"&&groupEditing,L"Chinese group name via WM_CHAR");
        NMLISTVIEW change={};change.hdr.hwndFrom=listWindow;change.hdr.code=LVN_ITEMCHANGED;change.iItem=0;change.uChanged=LVIF_STATE;change.uOldState=LVIS_SELECTED;change.uNewState=LVIS_SELECTED|LVIS_FOCUSED;SendMessageW(settingsWindow,WM_NOTIFY,0,(LPARAM)&change);
        GetWindowTextW(groupEdit,text,128);check(std::wstring(text)==L"测试分组",L"list focus does not erase typed group name");
    }
    SelectSettingsPage(1);SelectSettingsPage(2);SelectSettingsPage(0);
    check(ControlText(groupEdit)==L"测试分组"&&groupEditing,L"switching tabs preserves unsaved Chinese group input");
    check(ApplyGroupEdit()&&settingsMatcher.GroupFor(a)==L"测试分组"&&settingsMatcher.GroupFor(b).empty()&&settingsMatcher.GroupFor(d).empty()&&settingsDraft.programAssignments.empty(),L"default edit changes only selected explorer icon");
    SetDlgItemInt(settingsPages[1],IDC_PANEL_WIDTH,480,FALSE);
    SetDlgItemInt(settingsPages[1],IDC_PANEL_ICON_SIZE,32,FALSE);
    SetDlgItemInt(settingsPages[1],IDC_PANEL_COLUMNS,5,FALSE);
    check(IsWindowEnabled(GetDlgItem(settingsWindow,IDC_APPLY)),L"editing appearance enables the system Apply button");
    settingsConfig.settingsX=140;settingsConfig.settingsY=100;settingsConfig.haveSettingsPosition=true;RECT expected=SettingsRect();SetWindowPos(settingsWindow,nullptr,expected.left,expected.top,0,0,SWP_NOSIZE|SWP_NOZORDER);
    RememberSettingsPosition(settingsWindow);
    auto beforeClose=ReadText(configPath);SetWindowTextW(groupEdit,L"不保存的分组");ApplyGroupEdit();
    SetDlgItemInt(settingsPages[1],IDC_PANEL_WIDTH,540,FALSE);
    SendMessageW(settingsWindow,WM_CLOSE,0,0);LoadSettings();check(settingsConfig.panel.width!=540,L"cancel discards unapplied panel appearance edits");
    check(matcher.GroupFor(a).empty()&&ReadText(configPath).find(L"不保存的分组")==std::wstring::npos,L"closing remembers position without saving unapplied group edits");
    ShowSettings();RECT actual;GetWindowRect(settingsWindow,&actual);
    check(actual.left==expected.left&&actual.top==expected.top,L"settings restores exact previous screen position after reopening");
    check(ImageList_GetImageCount(settingsImages)==ListView_GetItemCount(listWindow),L"one icon image per actual detected row");
    for(int page=0;page<SettingsPageCount;page++){
        SelectSettingsPage(page);RedrawWindow(settingsWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        SaveTestWindow(settingsWindow,baseDir+L"\\settings-"+Number(GetDpiForWindow(settingsWindow))+L"-page-"+Number(page)+L".bmp");
        RECT client;GetClientRect(settingsPages[page],&client);bool fits=true;
        for(HWND control=GetWindow(settingsPages[page],GW_CHILD);control;control=GetWindow(control,GW_HWNDNEXT)){
            RECT bounds;GetWindowRect(control,&bounds);MapWindowPoints(nullptr,settingsPages[page],(POINT*)&bounds,2);
            if(bounds.left<0||bounds.top<0||bounds.right>client.right||bounds.bottom>client.bottom)fits=false;
        }
        check(fits,(L"settings page controls fit at DPI "+Number(GetDpiForWindow(settingsWindow))+L", page "+Number(page)).c_str());
    }
    SelectSettingsPage(0);
    RECT tabBounds,pageBounds;GetWindowRect(settingsTabs,&tabBounds);GetWindowRect(settingsPages[0],&pageBounds);
    check(pageBounds.top>tabBounds.top+MulDiv(15,GetDpiForWindow(settingsWindow),96),L"tab labels remain above the page content");
    SetFocus(GetDlgItem(settingsWindow,IDC_APPLY));
    MSG key={};key.hwnd=GetFocus();key.message=WM_KEYDOWN;key.wParam=VK_TAB;
    ProcessSettingsMessage(key);
    check(GetFocus()&&GetFocus()!=GetDlgItem(settingsWindow,IDC_APPLY)&&IsChild(settingsWindow,GetFocus()),L"Tab key navigates system property sheet controls");
    auto openMemory=NativeResources::Read();report+=L"SettingsOpenPrivateWorkingSetBytes="+Number(openMemory.privateWorking)+L"\n";
    check(openMemory.privateWorking<7*1048576,L"open settings private working set is below 7 MiB");
    bool previousFold=settingsConfig.foldIcons;settingsConfig.foldIcons=false;SendMessageW(foldCheck,BM_SETCHECK,BST_UNCHECKED,0);
    SettingsChanged();SendMessageW(settingsWindow,WM_NEXTDLGCTL,(WPARAM)GetDlgItem(settingsWindow,IDC_APPLY),TRUE);
    MSG applyKey={};applyKey.hwnd=GetFocus();applyKey.message=WM_KEYDOWN;applyKey.wParam=VK_RETURN;
    check(ProcessSettingsMessage(applyKey)!=FALSE,L"system Apply button retains native Enter-key behavior");
    check(settingsWindow&&ReadText(configPath).find(L"FoldOriginalIcons=0")!=std::wstring::npos&&!IsWindowEnabled(GetDlgItem(settingsWindow,IDC_APPLY)),L"Apply persists settings and refreshes the native dialog");
    settingsConfig.foldIcons=previousFold;
    SetDlgItemInt(settingsPages[1],IDC_PANEL_WIDTH,480,FALSE);
    SetDlgItemInt(settingsPages[1],IDC_PANEL_COLUMNS,5,FALSE);
    unsigned oldInterval=CaptureTimer::GetPreferences().intervalMinutes;
    unsigned changedInterval=oldInterval==15?20:15;
    SetDlgItemInt(settingsPages[2],IDC_CAPTURE_INTERVAL,changedInterval,FALSE);
    SendMessageW(GetDlgItem(settingsWindow,IDC_APPLY),BM_CLICK,0,0);
    check(settingsConfig.panel.width==480&&settingsConfig.panel.columns==5&&
          CaptureTimer::GetPreferences().intervalMinutes==changedInterval,
          L"one system Apply commits panel dimensions and capture interval from different pages");
    ShowSettings(2);
    check(TabCtrl_GetCurSel(settingsTabs)==2&&IsWindowVisible(settingsPages[2]),L"capture settings command selects the existing capture property page");
    SetDlgItemInt(settingsPages[2],IDC_CAPTURE_INTERVAL,changedInterval+1,FALSE);
    UpdateWindow(settingsWindow);SaveTestWindow(settingsWindow,baseDir+L"\\settings-preview.bmp");
    wchar_t realCapture[2]={};
    if(GetEnvironmentVariableW(L"TRAYGROUPS_TEST_REAL_CAPTURE",realCapture,2)) {
        auto captureContext=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        bool captured=CaptureTimer::Capture(CaptureTimer::settings);
        check(captured,L"real desktop capture succeeds while the native settings sheet stays open");
        report+=L"RealCapturePeakPrivateWorkingSetBytes="+Number(CaptureTimer::capturePeak.load())+L"\n";
        check(CaptureTimer::capturePeak.load()<7*1048576,L"capture sampled peak with settings open stays below 7 MiB");
        auto file=CaptureTimer::JsonValue(ReadText(CaptureTimer::root+L"\\last-capture.json"),L"file");
        NativeHandles::Handle image(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        unsigned char header[24]={};DWORD count=0;
        bool read=image&&ReadFile(image,header,sizeof(header),&count,nullptr)&&count==sizeof(header);
        auto number=[&](int n){return (unsigned(header[n])<<24)|(unsigned(header[n+1])<<16)|(unsigned(header[n+2])<<8)|header[n+3];};
        check(read&&header[0]==137&&header[1]=='P'&&header[2]=='N'&&header[3]=='G'&&
              number(16)==unsigned(GetSystemMetrics(SM_CXVIRTUALSCREEN))&&number(20)==unsigned(GetSystemMetrics(SM_CYVIRTUALSCREEN)),
              L"real PNG dimensions cover the physical virtual desktop across all monitors");
        report+=L"RealCaptureDesktop="+Number(number(16))+L"x"+Number(number(20))+L"\n";
        SetThreadDpiAwarenessContext(captureContext);
    }
    SetFocus(GetDlgItem(settingsWindow,IDC_CLOSE));key.hwnd=GetFocus();key.wParam=VK_ESCAPE;
    ProcessSettingsMessage(key);
    check(!settingsWindow,L"Escape closes the modeless settings dialog");
    check(CaptureTimer::GetPreferences().intervalMinutes==changedInterval,L"Cancel discards unapplied capture interval changes");
    for(int closePage=0;closePage<SettingsPageCount;++closePage) {
        ShowSettings(closePage);HWND closing=settingsWindow;
        SendMessageW(closing,WM_SYSCOMMAND,SC_CLOSE,0);
        check(!settingsWindow&&!IsWindow(closing),(L"title-bar X / SC_CLOSE destroys settings page "+Number(closePage)).c_str());
        if(settingsWindow)SendMessageW(settingsWindow,WM_CLOSE,0,0);
    }
    ShowSettings();SendMessageW(settingsWindow,WM_COMMAND,IDOK,0);
    check(!settingsWindow,L"standard OK applies and destroys the modeless property sheet");
    if(settingsWindow)SendMessageW(settingsWindow,WM_CLOSE,0,0);
    // Real Explorer icons owned only by the test: exactly one T despite many groups.
    SyncNotifications();testingOwnIcons=true;WaitForButtonIcon(1);auto tray=ReadTray();int count=0;for(const auto& i:tray)if(i.owner==mainWindow)count++;
    check(notificationIcons.size()==1&&notificationIcons.count(1)&&count==1,L"exactly one T tray entry; no numbered group icon");
    FreeItems(tray);testingOwnIcons=false;
    if(!SettingsButtonsRegression(report))ok=false;
    if(!NativePanelRegression(report))ok=false;
    ClosePopup();DeleteNotifications();CaptureTimer::Shutdown();DestroyWindow(mainWindow);mainWindow=nullptr;
    auto memory=NativeResources::Read();report+=L"PrivateWorkingSetBytes="+Number(memory.privateWorking)+L"\n";
    WriteText(baseDir+L"\\ui-self-test.txt",report);
    WriteText(baseDir+L"\\ui-self-test-"+Number(testDpi)+L".txt",report);
    appIcon=nullptr;DeleteObject(appFont);appFont=nullptr;return ok?0:1;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int) {
    baseDir=ExePath();baseDir.resize(baseDir.find_last_of(L'\\'));
    configPath=baseDir+L"\\groups.ini";recoveryPath=baseDir+L"\\restore-icons.tsv";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring mode=Trim(command?command:L"");
    if(mode==L"--ui96")SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    return mode==L"--ui"||mode==L"--ui96"?UiSelfTest():SelfTest();
}
