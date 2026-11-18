#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include "TrayInspector.h"
#include "TraySettingsIds.h"
#include "UiStyle.h"
#include "../common/TraySupport.h"
#include <commctrl.h>
#include <algorithm>

namespace TrayInspector {
namespace {
INT_PTR CALLBACK DetailsProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        UiStyle::Attach(window);
        SetDlgItemTextW(window, IDC_DETAIL_TEXT, reinterpret_cast<const wchar_t *>(l));
        SendDlgItemMessageW(window, IDC_DETAIL_TEXT, EM_SETSEL, 0, 0);
        SendDlgItemMessageW(window, IDC_DETAIL_TEXT, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                            MAKELPARAM(UiStyle::Scale(window, 10), UiStyle::Scale(window, 10)));
        SetFocus(GetDlgItem(window, IDOK));
        return FALSE;
    }
    if (message == WM_CLOSE ||
        (message == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL))) {
        EndDialog(window, 0);
        return TRUE;
    }
    return FALSE;
}
struct RuleEditor {
    TrayConfig::Settings draft;
    std::vector<TrayDiagnostics::Rule> rules;
    std::vector<TrayItem> icons;
    std::vector<std::wstring> changed;
    ReadIcons read;
    bool modified = false;
};
void FillRules(HWND window, RuleEditor &editor) {
    HWND list = GetDlgItem(window, IDC_RULE_LIST);
    ListView_DeleteAllItems(list);
    editor.rules = TrayDiagnostics::UnmatchedRules(editor.draft, editor.icons);
    for (size_t n = 0; n < editor.rules.size(); ++n) {
        auto &rule = editor.rules[n];
        LVITEMW row = {};
        row.mask = LVIF_TEXT;
        row.iItem = (int)n;
        row.pszText = const_cast<wchar_t *>(rule.reason.c_str());
        ListView_InsertItem(list, &row);
        ListView_SetItemText(
            list, n, 1,
            const_cast<wchar_t *>(rule.group.empty() ? L"（取消分组）" : rule.group.c_str()));
        ListView_SetItemText(list, n, 2, const_cast<wchar_t *>(rule.key.c_str()));
    }
    SetDlgItemTextW(
        window, IDC_RULE_STATUS,
        editor.rules.empty()
            ? L"当前未发现失配规则。程序未启动时的规则无需删除。"
            : L"选择旧规则和当前图标，再点重新绑定。路径/程序名规则会匹配该范围的所有图标。");
    if (!editor.rules.empty())
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}
void RefreshIcons(HWND window, RuleEditor &editor) {
    editor.icons = editor.read(false);
    HWND combo = GetDlgItem(window, IDC_RULE_TARGET);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto &icon : editor.icons) {
        std::wstring label = icon.process + L" — " + icon.tip + L" [" +
                             TraySupport::Number(icon.id) + L"] " + icon.path;
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    SendMessageW(combo, CB_SETCURSEL, editor.icons.empty() ? -1 : 0, 0);
    FillRules(window, editor);
}
INT_PTR CALLBACK RulesProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *editor = reinterpret_cast<RuleEditor *>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        UiStyle::Attach(window);
        editor = reinterpret_cast<RuleEditor *>(l);
        SetWindowLongPtrW(window, DWLP_USER, l);
        HWND list = GetDlgItem(window, IDC_RULE_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        int dpi = GetDpiForWindow(window);
        int widths[] = {245, 95, 450};
        const wchar_t *titles[] = {L"当前状态", L"分组", L"原规则"};
        for (int n = 0; n < 3; ++n) {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t *>(titles[n]);
            column.cx = MulDiv(widths[n], dpi, 96);
            ListView_InsertColumn(list, n, &column);
        }
        RefreshIcons(window, *editor);
        return TRUE;
    }
    if (!editor)
        return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message != WM_COMMAND)
        return FALSE;
    int command = LOWORD(w);
    if (command == IDOK) {
        EndDialog(window, IDOK);
        return TRUE;
    }
    if (command == IDC_RULE_REFRESH) {
        RefreshIcons(window, *editor);
        return TRUE;
    }
    if (command != IDC_RULE_REBIND && command != IDC_RULE_DELETE)
        return FALSE;
    int row = ListView_GetNextItem(GetDlgItem(window, IDC_RULE_LIST), -1, LVNI_SELECTED);
    if (row < 0 || row >= (int)editor->rules.size()) {
        SetDlgItemTextW(window, IDC_RULE_STATUS, L"请先选择一条规则。");
        return TRUE;
    }
    auto rule = editor->rules[row];
    if (command == IDC_RULE_DELETE)
        TrayDiagnostics::Remove(editor->draft, rule);
    else {
        int target = (int)SendDlgItemMessageW(window, IDC_RULE_TARGET, CB_GETCURSEL, 0, 0);
        if (target < 0 || target >= (int)editor->icons.size())
            return TRUE;
        auto live = editor->read(false);
        std::wstring error;
        if (!TrayDiagnostics::Rebind(editor->draft, rule, editor->icons[target], live, error)) {
            SetDlgItemTextW(window, IDC_RULE_STATUS, error.c_str());
            return TRUE;
        }
        editor->changed.push_back(
            TrayIdentity::IconKey(editor->icons[target], editor->draft.iconMatch));
    }
    editor->changed.push_back(rule.key);
    editor->modified = true;
    FillRules(window, *editor);
    SetDlgItemTextW(window, IDC_RULE_STATUS, L"修改已暂存。点确定返回，再在设置窗口点应用后生效。");
    return TRUE;
}
} // namespace
void ShowIcon(HWND parent, const TrayItem &item, const std::wstring &mode, bool foldEnabled) {
    auto text = TrayDiagnostics::IconDetails(item, mode, foldEnabled);
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ICON_DETAILS), parent,
                    DetailsProc, (LPARAM)text.c_str());
}
bool ManageRules(HWND parent, TrayConfig::Settings &settings, TrayIdentity::MatchState &state,
                 ReadIcons read) {
    RuleEditor editor;
    editor.draft = settings;
    editor.read = read;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULES), parent, RulesProc,
                        (LPARAM)&editor) != IDOK ||
        !editor.modified)
        return false;
    settings = std::move(editor.draft);
    for (const auto &key : editor.changed) {
        state.ruleBindings.erase(key);
        state.pendingRules.erase(key);
    }
    return true;
}
} // namespace TrayInspector
