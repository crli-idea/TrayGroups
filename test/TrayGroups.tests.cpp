#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"

static const UINT ActCallback=WM_APP+2;
// Isolated regression harness: two icons owned by this process, no clicks on user apps.
static std::vector<std::pair<WPARAM,LPARAM>> testEvents;
static LRESULT CALLBACK TestProc(HWND h,UINT msg,WPARAM w,LPARAM l) { if(msg==ActCallback){testEvents.push_back({w,l});return 0;}return DefWindowProcW(h,msg,w,l); }
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
        items.push_back(i);items.back().image=CopyIcon(i.image);
    }
    if(items.size()==2){
        bool matched=CommandMatches(items[0])&&CommandMatches(items[1]);
        auto wrong=items[0];wrong.command=items[1].command;
        if(!matched||CommandMatches(wrong)){pass=false;failures+=L"Toolbar identity revalidation failed\n";}
        checks++;
    }else pass=false;
    WNDCLASSW pc={};pc.lpfnWndProc=PopupProc;pc.hInstance=GetModuleHandleW(nullptr);pc.lpszClassName=PopupClass;pc.style=CS_DBLCLKS;RegisterClassW(&pc);
    for(size_t n=0;n<items.size();n++)for(int action=0;action<3;action++){
        popupItems={(int)n};testEvents.clear();POINT point;GetCursorPos(&point);auto expected=CallbackSequence(items[n].id,items[n].version,action,point);
        popupWindow=CreateWindowW(PopupClass,L"",WS_POPUP,0,0,420,120,nullptr,nullptr,pc.hInstance,nullptr);
        SendMessageW(popupWindow,action==0?WM_LBUTTONUP:action==1?WM_LBUTTONDBLCLK:WM_RBUTTONUP,0,MAKELPARAM(40,60));
        bool received=PumpUntil(expected.size());
        // The user can move their mouse during enumeration. Popup forwarding
        // samples the current cursor; test event semantics independently of that.
        bool equal=testEvents.size()==expected.size();
        if(equal)for(size_t j=0;j<expected.size();j++)if(testEvents[j].second!=expected[j].second||(items[n].version<4&&testEvents[j].first!=expected[j].first))equal=false;
        if(!received||!equal){pass=false;failures+=L"popup id="+Number(items[n].id)+L" version="+Number(items[n].version)+L" action="+Number(action)+L" expected="+Number(expected.size())+L" actual="+Number(testEvents.size())+L"\n";for(auto e:testEvents)failures+=Number(e.first)+L","+Number(e.second)+L" ";failures+=L"\nexpected: ";for(auto e:expected)failures+=Number(e.first)+L","+Number(e.second)+L" ";failures+=L"\n";}ClosePopup();checks++;
    }
    std::wstring originalRecovery=recoveryPath;recoveryPath=baseDir+L"\\test-restore.tmp";
    for(auto& i:items)i.group=L"Self-test";
    bool originalFold=foldIcons;foldIcons=true;FoldAssigned();
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
        checks++;hidden.clear();DeleteFileW(recoveryPath.c_str());foldIcons=true;
    }
    auto savedAssignments=assignments;auto savedPrograms=programAssignments;auto savedPaths=pathAssignments;
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();
    if(items.size()==2){
        auto key=IconKey(items[0],L"path-class-id");assignments[key]=L"CollisionTest";ResolveGroups(items);FoldAssigned();
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
    assignments=savedAssignments;programAssignments=savedPrograms;pathAssignments=savedPaths;pendingRules.clear();ruleBindings.clear();
    foldIcons=originalFold;recoveryPath=originalRecovery;
    FreeItems(items);FreeItems(own);Shell_NotifyIconW(NIM_DELETE,&a);Shell_NotifyIconW(NIM_DELETE,&b);testingOwnIcons=false;
    // Round-trip Unicode group assignments, stable keys, and removal.
    WriteText(baseDir+L"\\test-progress.txt",L"configuration round-trip\n");
    auto original=assignments;std::wstring oldConfig=configPath;configPath=baseDir+L"\\test-groups.tmp";assignments={{L"c:\\tools\\network.exe|2",L"网络"},{L"c:\\tools\\notes.exe|0",L"工具"}};
    TrayItem network;network.path=L"c:\\tools\\network.exe";network.process=L"network.exe";network.id=2;
    bool folded=foldIcons;foldIcons=false;if(!SaveSettings())pass=false;assignments.clear();LoadSettings();if(assignments.size()!=2||GroupFor(network)!=L"网络"||foldIcons)pass=false;
    DeleteFileW(configPath.c_str());assignments=original;configPath=oldConfig;foldIcons=folded;checks++;

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
    check(iconMatch==L"path-class-id",L"new configuration defaults to full-path, stable-window-class, and icon-ID identity");
    configPath=previousConfig;iconMatch=L"path-class-id";
    configPath=baseDir+L"\\identity-config.ini";assignments.clear();programAssignments.clear();pathAssignments.clear();groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();iconMatch=L"path-class-id";
    TrayItem a;a.process=L"explorer.exe";a.path=L"C:\\Windows\\explorer.exe";a.ownerClass=L"BluetoothNotificationAreaIconWindowClass";a.id=0;a.tip=L"蓝牙设备";
    TrayItem b=a;b.ownerClass=L"VolumeWindow";b.tip=L"耳机: 12%";
    TrayItem c=b;c.path=L"D:\\Other\\explorer.exe";
    assignments[IconKey(a,iconMatch)]=L"连接";assignments[IconKey(b,iconMatch)]=L"音量";
    check(IconKey(a,iconMatch)!=IconKey(b,iconMatch)&&GroupFor(a)==L"连接"&&GroupFor(b)==L"音量"&&GroupFor(c).empty(),L"same executable and same icon ID distinguished by owner class; different full paths stay separate");
    wchar_t systemDir[MAX_PATH]={};GetSystemDirectoryW(systemDir,MAX_PATH);
    std::wstring explorerPath=std::wstring(systemDir).substr(0,std::wstring(systemDir).find_last_of(L'\\'))+L"\\explorer.exe";
    TrayItem volume;volume.path=explorerPath;volume.ownerClass=L"ATL@sndvolsso.dll";volume.id=100;
    TrayItem bluetooth;bluetooth.path=explorerPath;bluetooth.ownerClass=L"BluetoothNotificationAreaIconWindowClass";bluetooth.id=0;
    check(IconKey(volume,L"path-class-id")!=IconKey(bluetooth,L"path-class-id"),
        L"Explorer volume and Bluetooth icons have distinct path, window-class, and icon-ID fallback identities");
    TrayItem systemAudio=a;systemAudio.ownerClass=L"atl@sndvolsso.dll";systemAudio.group=L"";
    TrayItem systemBluetooth=a;systemBluetooth.ownerClass=L"BluetoothNotificationAreaIconWindowClass";systemBluetooth.group=L"";
    check(FriendlyIconName(systemAudio)==L"系统音量"&&FriendlyIconName(systemBluetooth)==L"蓝牙设备",L"system icon names are readable in settings");
    systemAudio.group=L"音量";
    auto changing=b;changing.tip=L"耳机: 87%";changing.owner=(HWND)0x1234;check(GroupFor(changing)==L"音量",L"dynamic tooltip and window handle do not affect default identity");
    a.ownerClass=L"ATL:0000ABCD";b=a;b.ownerClass=L"ATL:0000DEF0";check(IconKey(a,iconMatch)==IconKey(b,iconMatch),L"process-specific ATL addresses normalized");
    a.ownerClass=L"Bluetooth";b=a;programAssignments[L"explorer.exe"]=L"整程序";assignments[IconKey(a,iconMatch)]=L"";
    check(GroupFor(a).empty(),L"explicit ungrouped icon overrides broad executable rule");
    std::vector<TrayItem> ambiguous={a,b};ResolveGroups(ambiguous);
    check(ambiguous[0].group.empty()&&ambiguous[1].group.empty()&&ambiguous[0].pending&&ambiguous[1].pending,L"ambiguous per-icon rule blocks executable fallback and leaves both icons pending");
    std::vector<TrayItem> remaining={a};ResolveGroups(remaining);
    check(remaining[0].pending&&remaining[0].group.empty(),L"ambiguity stays pending after a duplicate exits");
    pendingRules.clear();ruleBindings.clear();
    assignments.clear();programAssignments.clear();pathAssignments[Lower(c.path)]=L"独立路径";
    check(GroupFor(c)==L"独立路径"&&GroupFor(a).empty(),L"full-path grouping does not affect same filename elsewhere");
    iconMatch=L"path-id";assignments[IconKey(c,iconMatch)]=L"按ID";
    iconMatch=L"path-class-tip";assignments[IconKey(a,iconMatch)]=L"按提示";
    check(GroupFor(c)==L"按ID"&&GroupFor(a)==L"按提示",L"previous matching modes remain valid after changing the default");
    a.path=L"C:\\test=folder\\explorer.exe";a.tip=L"Name=first|second\nnext";auto escaped=IconKey(a,iconMatch);assignments[escaped]=L"转义";
    check(escaped.find(L'=')==std::wstring::npos&&RulePath(escaped)==Lower(a.path),L"INI delimiters and multiline tooltip escaped without changing the saved path");
    groupLabelSizes[L"连接"]=9;groupLabelSizes[L"音量"]=24;settingsX=-500;settingsY=80;haveSettingsPosition=true;
    assignments[IconKey(c,L"path-class-id")]=L"";
    bool saved=SaveSettings();assignments.clear();pathAssignments.clear();groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();LoadSettings();
    check(saved&&assignments.count(escaped)&&GroupFor(c).empty()&&GroupLabelSize(L"连接")==9&&GroupLabelSize(L"音量")==24&&haveSettingsPosition&&settingsX==-500&&settingsY==80&&iconMatch==L"path-class-tip",L"INI round-trip: identities, explicit exclusions, paths, per-group sizes and negative window coordinates");
    WriteText(configPath,ReadText(configPath)+L"\n[Unknown]\nValue=keep\n");
    check(LabelSize(2)==8&&LabelSize(100)==32,L"font size bounds");
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();iconMatch=L"path-class-id";
    TrayItem first;first.path=L"C:\\test\\same.exe";first.process=L"same.exe";first.ownerClass=L"SameClass";first.id=7;first.owner=(HWND)101;first.callback=WM_APP+30;first.runtime={1234,1000,first.owner,7};
    TrayItem second=first;second.owner=(HWND)102;second.runtime.owner=second.owner;
    auto key=IconKey(first,iconMatch);assignments[key]=L"\u5355\u4e2a";programAssignments[L"same.exe"]=L"\u5168\u90e8";pathAssignments[Lower(first.path)]=L"\u8def\u5f84";
    std::vector<TrayItem> live={first};ResolveGroups(live);
    check(!live[0].pending&&live[0].group==L"\u5355\u4e2a"&&ruleBindings.count(key),L"single selector binds to the current process lifetime and owning window");
    live={second};ResolveGroups(live);
    check(live[0].pending&&live[0].group.empty(),L"same saved selector cannot transfer to a different owning window during this run");
    auto restarted=first;restarted.runtime.started++;
    check(!SameRuntime(first,restarted),L"PID and window reuse across process restarts is rejected");
    auto otherId=first;otherId.id++;otherId.runtime.id=otherId.id;check(!SameRuntime(first,otherId),L"different tray icon IDs remain distinct at runtime");
    check(!SameRuntime(first,second),L"same path, class and icon ID still represent distinct runtime windows");
    ForgetRule(key);assignments[key]=L"\u5df2\u786e\u8ba4";live={second};ResolveGroups(live);
    check(!live[0].pending&&live[0].group==L"\u5df2\u786e\u8ba4",L"explicit reselection confirms a recreated icon");
    assignments[IconKey(second,L"path-id")]=L"\u53e6\u4e00\u7ec4";live={second};ResolveGroups(live);
    check(live[0].pending&&live[0].group.empty(),L"conflicting saved selectors block matching instead of choosing precedence");
    pendingRules.clear();ruleBindings.clear();assignments.clear();programAssignments.clear();pathAssignments.clear();

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
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();
    groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();iconMatch=L"path-class-id";foldIcons=true;
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
        SendMessageW(settingsWindow,WM_COMMAND,IDC_APPLY,0);
        check(!VisibleAtRuntime(identity.runtime)&&hidden.size()==1,L"click icon then assign Hidden and Apply really hides only that icon");
        for(const auto& item:settingsItems)if(item.owner==mainWindow&&item.id==702)check(VisibleAtRuntime(item.runtime),L"ungrouped test icon remains visible in Windows");
        SaveTestWindow(settingsWindow,baseDir+L"\\hidden-popup-assigned.bmp");
    }
    if(settingsWindow)SendMessageW(settingsWindow,WM_CLOSE,0,0);
    for(int action=0;action<3;action++){
        // Exercise the actual T handler, not just the popup construction function.
        MainProc(mainWindow,WM_APP+8,1,1);
        bool onlyHidden=popupItems.size()==1&&items[popupItems[0]].owner==mainWindow&&items[popupItems[0]].id==701;
        check(onlyHidden&&popupGroups==std::vector<std::wstring>{L"\u9690\u85cf"}&&activeGroup==L"\u9690\u85cf",L"T immediately shows only hidden grouped icon and no ungrouped entry");
        if(popupWindow&&onlyHidden){
            RedrawWindow(popupWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            if(action==0)SaveTestWindow(popupWindow,baseDir+L"\\hidden-popup-visible.bmp");
            RECT tile=TileRect(0);testEvents.clear();
            SendMessageW(popupWindow,action==0?WM_LBUTTONUP:action==1?WM_LBUTTONDBLCLK:WM_RBUTTONUP,0,MAKELPARAM(tile.left+20,tile.top+20));
            // Drive the single-click timer explicitly so desktop focus changes
            // during an automated screenshot cannot cancel the pending action.
            if(action==0&&popupWindow)SendMessageW(popupWindow,WM_TIMER,SingleTimer,0);
            auto expected=CallbackSequence(701,4,action,POINT{});
            bool delivered=PumpUntil(expected.size()),same=testEvents.size()==expected.size();
            if(same)for(size_t n=0;n<expected.size();n++)if(testEvents[n].second!=expected[n].second)same=false;
            check(delivered&&same&&!popupWindow,(L"hidden popup forwards original "+std::wstring(action==0?L"left click":action==1?L"double click":L"right click")+L" and closes").c_str());
        }else ClosePopup();
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
        RECT label=groupRects[0];
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
    RestoreHidden();foldIcons=false;Refresh();
    ShowGroups();check(popupItems.empty(),L"paused folding does not duplicate visible Windows icons in T");
    ClosePopup();ShowGroup(L"");check(showingGroups&&popupItems.empty()&&std::find(popupGroups.begin(),popupGroups.end(),L"")==popupGroups.end(),L"empty-group requests never expose Windows-owned ungrouped icons");
    ClosePopup();RestoreHidden();
    check(VisibleAtRuntime(identity.runtime)&&hidden.empty()&&GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES,L"all hidden test icons restored and recovery journal removed");
    KillTimer(mainWindow,ReFoldTimer);reFoldAttempts=0;
    Shell_NotifyIconW(NIM_DELETE,&icon);Shell_NotifyIconW(NIM_DELETE,&untouched);
    testingOwnIcons=false;FreeItems(items);FreeItems(settingsItems);
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();
    configPath=oldConfig;recoveryPath=oldRecovery;LoadSettings();
    return ok;
}


static bool GuidRegression(std::wstring& report) {
    bool ok=true;auto check=[&](bool passed,const wchar_t* label){report+=(passed?L"PASS: ":L"FAIL: ")+std::wstring(label)+L"\n";if(!passed)ok=false;};
    auto previousRecovery=recoveryPath;recoveryPath=baseDir+L"\\guid-test-restore.tsv";
    auto actual=ReadTray();bool securityFound=false;
    for(const auto& item:actual)if(Lower(item.process)==L"securityhealthsystray.exe"){
        GUID guid={};securityFound=true;check(ResolveIconGuid(item,guid)&&item.usesGuid,L"actual Windows Security executable path and class resolve to its verified notification GUID");
        TrayItem wrong=item;wrong.path=L"C:\\Other\\SecurityHealthSystray.exe";check(!ResolveIconGuid(wrong,guid),L"same executable name outside the system directory cannot claim the Security icon GUID");
    }
    report+=securityFound?L"Windows Security GUID runtime identification checked.\n":L"Windows Security icon unavailable for runtime identification.\n";
    for(const auto& item:actual)if(Lower(item.process)==L"explorer.exe"&&(StableClass(item.ownerClass)==L"atl@sndvolsso.dll"||StableClass(item.ownerClass)==L"bluetoothnotificationareaiconwindowclass")){
        GUID guid={};check(ResolveIconGuid(item,guid)&&item.usesGuid,L"actual Bluetooth or volume icon resolves its verified system GUID");
        TrayItem hiddenCopy=item;hiddenCopy.state|=TBSTATE_HIDDEN;
        GUID hiddenGuid={};check(ResolveIconGuid(hiddenCopy,hiddenGuid)&&!memcmp(&guid,&hiddenGuid,sizeof(GUID)),L"system GUID identity remains available when its icon is hidden");

        auto changed=item;changed.path=L"D:\\\\fake\\\\explorer.exe";check(!ResolveIconGuid(changed,guid),L"non-system explorer executable cannot claim Bluetooth or volume GUID");
    }
    FreeItems(actual);
    bool oldFold=foldIcons;foldIcons=true;testingOwnIcons=true;
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();
    GUID guid={0x58c7a6be,0x6134,0x4c73,{0xb6,0x24,0x82,0x22,0xce,0x53,0xb4,0x19}};
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=mainWindow;icon.uID=703;icon.guidItem=guid;
    icon.uFlags=NIF_GUID|NIF_ICON|NIF_TIP|NIF_MESSAGE;icon.hIcon=LoadIconW(nullptr,IDI_INFORMATION);icon.uCallbackMessage=ActCallback;
    wcscpy_s(icon.szTip,L"TrayGroups GUID hiding regression");
    testIconGuids[{mainWindow,703}]=guid;
    bool added=Shell_NotifyIconW(NIM_ADD,&icon)!=FALSE;icon.uVersion=4;Shell_NotifyIconW(NIM_SETVERSION,&icon);
    FreeItems(items);items=ReadTray();TrayItem identity;bool found=false;
    for(const auto& item:items)if(item.owner==mainWindow&&item.id==703){identity=item;identity.image=nullptr;found=true;}
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
        assignments[IconKey(identity,L"path-class-id")]=L"GUID group";
        ResolveGroups(items);check(GroupFor(identity)==L"GUID group",L"existing per-icon rules continue matching GUID-aware icons");
        assignments.clear();iconMatch=L"path-guid";assignments[key]=L"GUID group";
        auto oldConfig=configPath;configPath=baseDir+L"\\guid-identity.ini";
        bool saved=SaveSettings();assignments.clear();LoadSettings();
        check(saved&&iconMatch==L"path-guid"&&GroupFor(identity)==L"GUID group",L"path plus GUID matching mode and assignment persist through INI save and load");
        configPath=oldConfig;
        ResolveGroups(items);FoldAssigned();
        check(hidden.size()==1&&!VisibleAtRuntime(identity.runtime),L"GUID-addressed tray icon really hides through Shell notification state API");
        ClosePopup();ShowGroups();bool present=false;
        for(int n:popupItems)if(items[n].owner==mainWindow&&items[n].id==703)present=true;
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
    RestoreHidden();Shell_NotifyIconW(NIM_DELETE,&icon);testIconGuids.erase({mainWindow,703});
    FreeItems(items);testingOwnIcons=false;foldIcons=oldFold;recoveryPath=previousRecovery;
    assignments.clear();programAssignments.clear();pathAssignments.clear();pendingRules.clear();ruleBindings.clear();LoadSettings();
    return ok;
}

static int UiSelfTest() {
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&common);
    NONCLIENTMETRICSW metrics={};metrics.cbSize=sizeof(metrics);SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);appFont=CreateFontIndirectW(&metrics.lfMessageFont);appIcon=MakeAppIcon();
    WNDCLASSW c={};c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);c.hIcon=appIcon;c.lpfnWndProc=PopupProc;c.lpszClassName=PopupClass;c.style=CS_DBLCLKS;RegisterClassW(&c);
    c.lpfnWndProc=TestProc;c.lpszClassName=L"Crli.TrayGroups.UiReceiver";RegisterClassW(&c);mainWindow=CreateWindowW(c.lpszClassName,L"",0,0,0,0,0,nullptr,nullptr,c.hInstance,nullptr);
    UINT testDpi=GetDpiForWindow(mainWindow);std::wstring report;bool ok=IdentityRegression(report);auto check=[&](bool result,const wchar_t* name){report+=(result?L"PASS: ":L"FAIL: ")+std::wstring(name)+L"\n";if(!result)ok=false;};
    configPath=baseDir+L"\\ui-test-config.ini";assignments.clear();programAssignments.clear();pathAssignments.clear();groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();iconMatch=L"path-class-id";haveSettingsPosition=false;
    if(!HiddenPopupRegression(report))ok=false;
    if(!GuidRegression(report))ok=false;
    configPath=baseDir+L"\\ui-test-config.ini";assignments.clear();programAssignments.clear();pathAssignments.clear();groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();foldIcons=false;iconMatch=L"path-class-id";haveSettingsPosition=false;
    ShowSettings();check(settingsWindow&&IsWindowVisible(settingsWindow)&&ListView_GetItemCount(listWindow)>0,L"settings window visible with detected program icons");
    if(!settingsWindow||ListView_GetItemCount(listWindow)==0){WriteText(baseDir+L"\\ui-self-test.txt",report);return 1;}
    check(TabCtrl_GetItemCount(settingsTabs)==3&&IsWindowVisible(settingsPages[0])&&!IsWindowVisible(settingsPages[1])&&!IsWindowVisible(settingsPages[2]),L"three native settings pages with only the current page visible");
    check(SendMessageW(settingsWindow,WM_GETICON,ICON_SMALL,0)!=0,L"settings window has the T application icon");
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
    check(ApplyGroupEdit()&&GroupFor(a)==L"测试分组"&&GroupFor(b).empty()&&GroupFor(d).empty()&&programAssignments.empty(),L"default edit changes only selected explorer icon");
    SetWindowTextW(appearanceGroup,L"测试分组");UpdateLabelSize();
    SetWindowTextW(labelSizeEdit,L"24");SetWindowTextW(labelWidthEdit,L"320");SetWindowTextW(labelHeightEdit,L"90");SendMessageW(settingsWindow,WM_COMMAND,MAKEWPARAM(115,EN_CHANGE),(LPARAM)labelSizeEdit);
    check(SaveLabelSize()&&GroupLabelSize(L"测试分组")==24&&GroupLabelWidth(L"测试分组")==320&&GroupLabelHeight(L"测试分组")==90,L"per-group label size saved from the settings editor");
    SetWindowTextW(appearanceGroup,L"独立外观");UpdateLabelSize();SetWindowTextW(labelSizeEdit,L"18");
    SetWindowTextW(appearanceGroup,L"下一个组");SendMessageW(settingsWindow,WM_COMMAND,MAKEWPARAM(IDC_APPEARANCE_GROUP,CBN_EDITCHANGE),(LPARAM)appearanceGroup);
    check(GroupLabelSize(L"独立外观")==18&&ControlText(groupEdit)==L"测试分组"&&!groupEditing,L"appearance group changes retain edits without changing icon assignments");
    SetWindowTextW(appearanceGroup,L"测试分组");UpdateLabelSize();
    SendMessageW(scopeEdit,CB_SETCURSEL,1,0);SetWindowTextW(groupEdit,L"路径分组");check(ApplyGroupEdit()&&GroupFor(a)==L"路径分组"&&GroupFor(b)==L"路径分组"&&GroupFor(d).empty(),L"path scope groups same path without affecting another explorer installation");
    SendMessageW(scopeEdit,CB_SETCURSEL,0,0);SetWindowTextW(groupEdit,L"");check(ApplyGroupEdit()&&GroupFor(a).empty()&&GroupFor(b)==L"路径分组",L"single-icon ungroup excludes it from an inherited path rule");
    check(SaveSettings(),L"group configuration saved");
    settingsX=140;settingsY=100;haveSettingsPosition=true;RECT expected=SettingsRect();SetWindowPos(settingsWindow,nullptr,expected.left,expected.top,0,0,SWP_NOSIZE|SWP_NOZORDER);
    RememberSettingsPosition(settingsWindow);
    auto beforeClose=ReadText(configPath);SetWindowTextW(groupEdit,L"不保存的分组");ApplyGroupEdit();
    SetWindowTextW(appearanceGroup,L"不保存的外观");UpdateLabelSize();SetWindowTextW(labelSizeEdit,L"30");SaveLabelSize();
    SendMessageW(settingsWindow,WM_CLOSE,0,0);LoadSettings();check(!groupLabelSizes.count(L"不保存的外观"),L"closing discards unapplied appearance edits");
    check(GroupFor(a).empty()&&ReadText(configPath).find(L"不保存的分组")==std::wstring::npos,L"closing remembers position without saving unapplied group edits");
    ShowSettings();RECT actual;GetWindowRect(settingsWindow,&actual);
    check(actual.left==expected.left&&actual.top==expected.top,L"settings restores exact previous screen position after reopening");
    check(ImageList_GetImageCount(settingsImages)==ListView_GetItemCount(listWindow),L"one icon image per actual detected row");
    for(int page=0;page<3;page++){
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
    IsDialogMessageW(settingsWindow,&key);
    check(GetFocus()==GetDlgItem(settingsWindow,IDC_CLOSE),L"Tab key navigates native dialog controls");
    auto openMemory=NativeResources::Read();report+=L"SettingsOpenPrivateWorkingSetBytes="+Number(openMemory.privateWorking)+L"\n";
    check(openMemory.privateWorking<3*1048576,L"open settings private working set is below 3 MiB");
    bool previousFold=foldIcons;foldIcons=false;SendMessageW(foldCheck,BM_SETCHECK,BST_UNCHECKED,0);
    SendMessageW(settingsWindow,WM_COMMAND,IDC_APPLY,0);
    check(ReadText(configPath).find(L"FoldOriginalIcons=0")!=std::wstring::npos&&ControlText(settingsWindow).find(L"\u5df2\u5e94\u7528")!=std::wstring::npos,L"Apply persists settings and refreshes the native dialog");
    foldIcons=previousFold;
    UpdateWindow(settingsWindow);SaveTestWindow(settingsWindow,baseDir+L"\\settings-preview.bmp");
    SetFocus(GetDlgItem(settingsWindow,IDC_CLOSE));key.hwnd=GetFocus();key.wParam=VK_ESCAPE;
    IsDialogMessageW(settingsWindow,&key);
    check(!settingsWindow,L"Escape closes the modeless settings dialog");
    if(settingsWindow)SendMessageW(settingsWindow,WM_CLOSE,0,0);
    // Real Explorer icons owned only by the test: exactly one T despite many groups.
    SyncNotifications();testingOwnIcons=true;auto tray=ReadTray();int count=0;for(const auto& i:tray)if(i.owner==mainWindow)count++;
    check(notificationIcons.size()==1&&notificationIcons.count(1)&&count==1,L"exactly one T tray entry; no numbered group icon");
    FreeItems(tray);testingOwnIcons=false;
    popupPage=0;ShowGroups();
    check(std::find(popupGroups.begin(),popupGroups.end(),L"")==popupGroups.end()&&!activeGroup.empty(),L"T popup omits ungrouped and chooses a named group");
    TrayItem visibleRow;visibleRow.group=L"Named";
    TrayItem hiddenRow=visibleRow;hiddenRow.state=TBSTATE_HIDDEN;
    TrayItem ungroupedRow=hiddenRow;ungroupedRow.group.clear();
    TrayItem pendingRow=hiddenRow;pendingRow.pending=true;
    check(!PopupEligible(visibleRow)&&PopupEligible(hiddenRow)&&!PopupEligible(ungroupedRow)&&!PopupEligible(pendingRow),L"popup includes only hidden named-group icons, excluding visible, ungrouped and uncertain icons");
    check(popupWindow&&showingGroups&&std::find(popupGroups.begin(),popupGroups.end(),L"路径分组")!=popupGroups.end(),L"T popup contains all configured groups");
    size_t index=std::find(popupGroups.begin(),popupGroups.end(),L"测试分组")-popupGroups.begin();
    if(index<groupRects.size()){
        int dpi=GetDpiForWindow(popupWindow),height=groupRects[index].bottom-groupRects[index].top,width=groupRects[index].right-groupRects[index].left;
        report+=L"Label geometry: dpi="+Number(dpi)+L" width="+Number(width)+L" height="+Number(height)+L"\n";
        check(height>=std::max(MulDiv(24,dpi,72)+24,MulDiv(90,dpi,96))&&width==MulDiv(320,dpi,96),L"group label dimensions follow that group's configured font size");
    }
    UpdateWindow(popupWindow);SaveTestWindow(popupWindow,baseDir+L"\\groups-preview.bmp");
    size_t target=std::find(popupGroups.begin(),popupGroups.end(),L"路径分组")-popupGroups.begin();
    if(target<groupRects.size()){
        RECT r=groupRects[target];SendMessageW(popupWindow,WM_LBUTTONUP,0,MAKELPARAM(r.left+10,r.top+10));MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        check(popupWindow&&showingGroups&&activeGroup==L"路径分组"&&popupItemTop>groupRects.back().bottom,L"group list remains above the selected group's icon view");
    }
    TrayItem extra;extra.process=L"sample.exe";extra.group=activeGroup;extra.owner=mainWindow;extra.state=TBSTATE_HIDDEN;
    for(int i=0;i<20;i++){extra.id=i+100;items.push_back(extra);}
    popupItemPage=0;FillPopupItems(activeGroup);
    check(popupItemPages>1&&popupItems.size()==(size_t)popupItemPageSize,L"overflowing group icons have a first page");
    if(popupItemPages>1){
        int footer=popupItemTop+std::max(1,((int)popupItems.size()+3)/4)*72;
        check(footer>popupItemTop&&popupItemPages>=2,L"remaining program icons have a distinct page");
        popupItemPage=1;FillPopupItems(activeGroup);
        check(popupItemPage==1&&!popupItems.empty(),L"next page exposes remaining program icons");
    }
    if(popupWindow){SendMessageW(popupWindow,WM_LBUTTONUP,0,MAKELPARAM(30,15));MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}check(popupWindow&&showingGroups,L"group list remains visible when its header is clicked");}
    ClosePopup();DeleteNotifications();DestroyWindow(mainWindow);mainWindow=nullptr;
    auto memory=NativeResources::Read();report+=L"PrivateWorkingSetBytes="+Number(memory.privateWorking)+L"\n";
    WriteText(baseDir+L"\\ui-self-test.txt",report);
    WriteText(baseDir+L"\\ui-self-test-"+Number(testDpi)+L".txt",report);
    DestroyIcon(appIcon);appIcon=nullptr;DeleteObject(appFont);appFont=nullptr;return ok?0:1;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int) {
    baseDir=ExePath();baseDir.resize(baseDir.find_last_of(L'\\'));
    configPath=baseDir+L"\\groups.ini";recoveryPath=baseDir+L"\\restore-icons.tsv";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring mode=Trim(command?command:L"");
    if(mode==L"--ui96")SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    return mode==L"--ui"||mode==L"--ui96"?UiSelfTest():SelfTest();
}
