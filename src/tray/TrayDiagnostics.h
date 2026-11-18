#pragma once
#include "TrayIdentity.h"

// 诊断只读取配置和快照；失配不能直接证明软件已卸载，不自动删除任何规则。
namespace TrayDiagnostics {
enum class Scope { Icon, Path, Program };
struct Rule {
    Scope scope = Scope::Icon;
    std::wstring key, group, reason;
    unsigned matches = 0;
};
std::wstring IconDetails(const TrayItem &item, const std::wstring &mode, bool foldEnabled);
std::vector<Rule> UnmatchedRules(const TrayConfig::Settings &settings,
                                 const std::vector<TrayItem> &icons);
// 仅修改调用者的编辑副本。目标必须唯一且能核对运行时身份；不覆盖已有目标规则。
bool Rebind(TrayConfig::Settings &settings, const Rule &rule, const TrayItem &target,
            const std::vector<TrayItem> &live, std::wstring &error);
void Remove(TrayConfig::Settings &settings, const Rule &rule);
} // namespace TrayDiagnostics
