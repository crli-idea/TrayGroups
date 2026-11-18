#pragma once
#include "../tray/TrayDiagnostics.h"

namespace TrayInspector {
using ReadIcons = std::vector<TrayItem> (*)(bool);
void ShowIcon(HWND parent, const TrayItem &item, const std::wstring &mode, bool foldEnabled);
// 对话框内独立编辑，确定后交回设置页，仍需点“应用”保存；取消不修改配置。
bool ManageRules(HWND parent, TrayConfig::Settings &settings, TrayIdentity::MatchState &state,
                 ReadIcons read);
} // namespace TrayInspector
