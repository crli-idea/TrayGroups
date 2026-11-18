#pragma once

static bool CompactPopupRegression(std::wstring &report) {
    bool ok=true;auto check=[&](bool value,const wchar_t *name){report+=(value?L"PASS: ":L"FAIL: ")+std::wstring(name)+L"\n";ok&=value;};
    ShowGroups();
    check(std::find(popupGroups.begin(),popupGroups.end(),L"")==popupGroups.end(),L"compact panel excludes ungrouped Windows icons");
    TrayItem visible;visible.group=L"Named";
    auto eligible=visible;eligible.state=TBSTATE_HIDDEN;eligible.addressKnown=true;
    auto pending=eligible;pending.pending=true;auto ungrouped=eligible;ungrouped.group.clear();
    check(!PopupEligible(visible)&&PopupEligible(eligible)&&!PopupEligible(pending)&&!PopupEligible(ungrouped),L"compact panel only admits confirmed hidden grouped icons");
    ClosePopup();
    auto previous=settingsConfig;auto previousState=matchState;
    FreeItems(items);settingsConfig=TrayConfig::Settings{};settingsConfig.foldIcons=true;
    settingsConfig.panel.groupOrder={L"效率工具",L"影音娱乐",L"连接服务"};
    const wchar_t *names[]={L"Snipaste",L"Everything",L"便利贴",L"VS Code",L"流量监控",L"EarTrumpet",L"音乐",L"Steam",L"蓝牙",L"Clash Verge",L"微信",L"安全中心"};
    HICON samples[]={LoadIconW(nullptr,IDI_INFORMATION),LoadIconW(nullptr,IDI_APPLICATION),LoadIconW(nullptr,IDI_WARNING)};
    for(int n=0;n<12;++n){
        TrayItem item=eligible;item.owner=mainWindow;item.id=2000+n;
        item.process=names[n];item.tip=names[n];item.group=n<5?L"效率工具":n<8?L"影音娱乐":L"连接服务";
        item.image=CopyIcon(samples[n%3]);items.push_back(std::move(item));
        settingsConfig.programAssignments[names[n]]=items.back().group;
    }
    OpenCompactPanel();
    check(popupIcons.size()==12&&popupLayout.sections.size()==3&&popupGroups==settingsConfig.panel.groupOrder,
          L"all three selected groups and twelve icons are expanded simultaneously in selected order");
    RECT windowRect;GetWindowRect(popupWindow,&windowRect);
    check(windowRect.right-windowRect.left==PanelPx(420)&&popupOptions.iconSize==28&&popupOptions.columns==4&&PanelAccent()==RGB(49,161,139),
          L"compact native panel uses selected width, icon size, four columns and teal accent");
    auto first=TileRect(0),fourth=TileRect(3),fifth=TileRect(4);
    check(first.top==fourth.top&&fifth.top>first.top,L"the fifth icon wraps to a second row within its group");
    RedrawWindow(popupWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    SaveTestWindow(popupWindow,baseDir+L"\\compact-panel-preview.bmp");
    DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),handles=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int repeat=0;repeat<12;++repeat)RedrawWindow(popupWindow,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    check(gdi==GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)&&handles==GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS),L"repainting compact groups does not allocate or leak GDI objects or icon handles");
    RECT footer;GetWindowRect(GetDlgItem(popupWindow,CaptureTimer::CaptureNowCommand),&footer);
    auto snapshotName=popupIcons.front().process;
    FreeItems(items);
    check(popupIcons.size()==12&&popupIcons.front().image&&popupIcons.front().process==snapshotName,L"open panel owns its icon snapshot independently of background scan results");
    auto snapshot=popupIcons;ClosePopup();
    for(int n=0;n<100;++n){auto item=snapshot[n%12];item.id=3000+n;items.push_back(std::move(item));}
    OpenCompactPanel();
    RECT before;GetWindowRect(popupWindow,&before);
    check(CompactPanel::ScrollLimit(popupLayout.height,popupViewport.bottom-popupViewport.top)>0,L"large compact panels scroll instead of hiding groups behind a page selector");
    ScrollPopup(1000000);
    RECT last=TileRect((int)popupIcons.size()-1);
    RECT footerAfter;GetWindowRect(GetDlgItem(popupWindow,CaptureTimer::CaptureNowCommand),&footerAfter);
    check(last.bottom<=popupViewport.bottom&&last.top>=popupViewport.top,L"scrolling reaches the final icon without clipping its label");
    check(TileAt({last.left+2,last.top+2})==(int)popupIcons.size()-1&&TileAt({last.left+2,popupViewport.bottom+5})==-1,
          L"scrolled hit testing targets current icon rectangles and excludes the fixed footer");
    ScrollPopup(-100);check(popupScroll==0,L"scrolling clamps to content bounds");
    FocusPopupIcon((int)popupIcons.size()-1);
    auto focused=TileRect(popupFocus);
    check(popupScroll>0&&popupFocus==(int)popupIcons.size()-1&&focused.top>=popupViewport.top&&focused.bottom<=popupViewport.bottom,L"keyboard selection scrolls the final icon into view");
    RECT after;GetWindowRect(popupWindow,&after);
    check(EqualRect(&before,&after),L"scroll and keyboard navigation never move the popup window");
    RECT footerCurrent;GetWindowRect(GetDlgItem(popupWindow,CaptureTimer::CaptureNowCommand),&footerCurrent);
    check(EqualRect(&footerAfter,&footerCurrent),L"capture controls stay fixed while group content scrolls");
    SendMessageW(GetDlgItem(popupWindow,CaptureTimer::CaptureNowCommand),WM_KEYDOWN,VK_ESCAPE,0);
    check(!popupWindow,L"Escape closes the panel even when a footer button has keyboard focus");
    settingsConfig.panel.showCapture=false;OpenCompactPanel();
    check(!GetDlgItem(popupWindow,CaptureTimer::CaptureNowCommand),L"capture footer visibility follows the portable panel setting");ClosePopup();
    settingsConfig=previous;matchState=previousState;FreeItems(items);
    return ok;
}
