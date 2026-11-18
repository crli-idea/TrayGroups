#pragma once

static bool NativePanelRegression(std::wstring &report) {
    bool ok = true;
    auto check = [&](bool value, const wchar_t *name) {
        report += (value ? L"PASS: " : L"FAIL: ") + std::wstring(name) + L"\n";
        ok &= value;
    };
    auto previous = settingsConfig;
    auto previousState = matchState;
    FreeItems(items);
    settingsConfig = TrayConfig::Settings{};
    TrayItem eligible;
    eligible.state = TBSTATE_HIDDEN;
    eligible.addressKnown = true;
    eligible.group = L"效率工具";
    auto pending = eligible;
    pending.pending = true;
    auto ungrouped = eligible;
    ungrouped.group.clear();
    auto visible = eligible;
    visible.state = 0;
    check(PopupEligible(eligible) && !PopupEligible(pending) && !PopupEligible(ungrouped) &&
              !PopupEligible(visible),
          L"native panel only includes confirmed hidden grouped icons");
    const wchar_t *groups[] = {L"效率工具", L"影音娱乐", L"连接服务"};
    for (int n = 0; n < 12; ++n) {
        auto item = eligible;
        item.process = L"示例 " + Number(n + 1);
        item.tip = item.process;
        item.group = groups[n < 5 ? 0 : n < 8 ? 1 : 2];
        item.image = CopyIcon(appIcon);
        items.push_back(std::move(item));
        settingsConfig.programAssignments[items.back().process] = items.back().group;
    }
    OpenTrayPanel();
    RedrawWindow(popupWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    wchar_t cls[64] = {};
    GetClassNameW(popupList, cls, 64);
    check(!wcscmp(cls, WC_LISTVIEWW) && ListView_IsGroupViewEnabled(popupList) &&
              ListView_GetItemCount(popupList) == 12 &&
              popupGroups == settingsConfig.panel.groupOrder,
          L"one system ListView expands all groups in configured order with real icon snapshots");
    auto first = TileRect(0), fourth = TileRect(3), fifth = TileRect(4);
    check(first.top == fourth.top && fifth.top > first.top,
          L"system icon layout has four columns and wraps the fifth icon");
    SaveTestWindow(popupWindow, baseDir + L"\\native-panel-preview.bmp");
    DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int repeat = 0; repeat < 15; ++repeat)
        RedrawWindow(popupWindow, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    check(gdi == GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
          L"repainting system controls does not leak GDI objects");
    auto snapshot = popupIcons;
    FreeItems(items);
    check(popupIcons.size() == 12 && popupIcons.front().image,
          L"open system list owns images independently of background scan results");
    ClosePopup();
    for (int n = 0; n < 100; ++n)
        items.push_back(snapshot[n % 12]);
    OpenTrayPanel();
    RECT before, footer;
    GetWindowRect(popupWindow, &before);
    GetWindowRect(GetDlgItem(popupWindow, CaptureTimer::CaptureNowCommand), &footer);
    SendMessageW(popupList, WM_KEYDOWN, VK_END, 0);
    ListView_EnsureVisible(popupList, 99, FALSE);
    auto last = TileRect(99);
    RECT client;
    GetClientRect(popupList, &client);
    MapWindowPoints(popupList, popupWindow, (POINT *)&client, 2);
    check(last.top >= client.top && last.bottom <= client.bottom,
          L"system scrolling reaches the final icon without covering fixed footer controls");
    ListView_SetItemState(popupList, 99, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    RECT after, footerAfter;
    GetWindowRect(popupWindow, &after);
    GetWindowRect(GetDlgItem(popupWindow, CaptureTimer::CaptureNowCommand), &footerAfter);
    check(EqualRect(&before, &after) && EqualRect(&footer, &footerAfter),
          L"native scrolling leaves popup position and footer unchanged");
    auto memory = NativeResources::Read();
    report += L"PanelOpenPrivateWorkingSetBytes=" + Number(memory.privateWorking) + L"\n";
    check(memory.privateWorking < 7 * 1048576, L"open panel private working set stays below 7 MiB");
    SendMessageW(GetDlgItem(popupWindow, CaptureTimer::CaptureNowCommand), WM_KEYDOWN, VK_ESCAPE,
                 0);
    check(!popupWindow && !popupList && !popupImages && popupIcons.empty(),
          L"Escape from footer releases the panel, image list and snapshots");
    settingsConfig.panel.showCapture = false;
    OpenTrayPanel();
    check(!GetDlgItem(popupWindow, CaptureTimer::CaptureNowCommand),
          L"capture footer can be disabled");
    ClosePopup();
    FreeItems(items);
    settingsConfig = previous;
    matchState = previousState;
    return ok;
}
