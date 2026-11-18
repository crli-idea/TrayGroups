#pragma once

// 模态窗口使用本线程定时器驱动，只查找本测试进程的窗口，不点击用户程序。
static std::vector<TrayItem> inspectorTestIcons;
static bool inspectorUiOk = true, inspectorCancel = false, inspectorDetails = false;
static std::wstring inspectorExpectedKey;
static std::vector<TrayItem> InspectorRead(bool) { return inspectorTestIcons; }
static void CALLBACK InspectorTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = FindWindowW(L"#32770", inspectorDetails ? L"图标详情" : L"失效规则管理");
    DWORD pid = 0;
    if (dialog) GetWindowThreadProcessId(dialog, &pid);
    if (pid != GetCurrentProcessId()) return;
    KillTimer(nullptr, timer);
    RECT client;GetClientRect(dialog,&client);
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        RECT bounds;GetWindowRect(child,&bounds);MapWindowPoints(nullptr,dialog,(POINT*)&bounds,2);
        inspectorUiOk&=bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom;
    }
    if (inspectorDetails) {
        wchar_t text[16384]={};GetDlgItemTextW(dialog,IDC_DETAIL_TEXT,text,16384);
        inspectorUiOk&=std::wstring(text).find(inspectorExpectedKey)!=std::wstring::npos;
        inspectorUiOk&=(GetWindowLongPtrW(GetDlgItem(dialog,IDC_DETAIL_TEXT),GWL_STYLE)&ES_READONLY)!=0;
        SaveTestWindow(dialog,baseDir+L"\\icon-details-preview.bmp");
        SendMessageW(dialog,WM_COMMAND,IDOK,0);
    } else {
        inspectorUiOk&=ListView_GetItemCount(GetDlgItem(dialog,IDC_RULE_LIST))==1;
        SendDlgItemMessageW(dialog,IDC_RULE_TARGET,CB_SETCURSEL,0,0);
        SendMessageW(dialog,WM_COMMAND,IDC_RULE_REBIND,0);
        inspectorUiOk&=ListView_GetItemCount(GetDlgItem(dialog,IDC_RULE_LIST))==0;
        SaveTestWindow(dialog,baseDir+L"\\rule-manager-preview.bmp");
        SendMessageW(dialog,WM_COMMAND,inspectorCancel?IDCANCEL:IDOK,0);
    }
}
static bool InspectorRegression(std::wstring &report) {
    TrayItem target;target.path=ExePath();target.process=L"InspectorTest.exe";target.owner=mainWindow;
    target.id=12001;target.runtime={GetCurrentProcessId(),ProcessStart(GetCurrentProcess()),mainWindow,target.id};
    target.ownerClass=L"InspectorTest";target.addressKnown=true;target.usesGuid=true;
    CoCreateGuid(&target.guid);
    inspectorExpectedKey=IconKey(target,L"path-guid");
    inspectorTestIcons={target};
    auto previous=target;previous.guid.Data1^=1;
    auto oldKey=IconKey(previous,L"path-guid");
    TrayConfig::Settings config;config.assignments[oldKey]=L"测试组";
    TrayIdentity::MatchState state;state.pendingRules.insert(oldKey);
    inspectorUiOk=true;inspectorDetails=true;
    auto timer=SetTimer(nullptr,0,60,InspectorTimer);
    if(!timer)return false;
    TrayInspector::ShowIcon(mainWindow,target,L"path-guid",true);KillTimer(nullptr,timer);
    inspectorDetails=false;inspectorCancel=true;
    timer=SetTimer(nullptr,0,60,InspectorTimer);if(!timer)return false;
    bool cancelled=TrayInspector::ManageRules(mainWindow,config,state,InspectorRead);KillTimer(nullptr,timer);
    inspectorUiOk&=!cancelled&&config.assignments.count(oldKey)&&state.pendingRules.count(oldKey);
    inspectorCancel=false;
    timer=SetTimer(nullptr,0,60,InspectorTimer);if(!timer)return false;
    bool accepted=TrayInspector::ManageRules(mainWindow,config,state,InspectorRead);KillTimer(nullptr,timer);
    inspectorUiOk&=accepted&&config.assignments[inspectorExpectedKey]==L"测试组"&&
        !config.assignments.count(oldKey)&&!state.pendingRules.count(oldKey);
    inspectorTestIcons.clear();
    report+=(inspectorUiOk?L"PASS: ":L"FAIL: ")+std::wstring(L"icon details and rule manager show at current DPI, reject cancel changes and stage confirmed rebind\n");
    return inspectorUiOk;
}
