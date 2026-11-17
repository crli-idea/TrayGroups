#pragma once
#include "TrayTypes.h"
#include "../config/TrayConfig.h"
#include <map>
#include <set>
#include <vector>

namespace TrayIdentity {
std::wstring StableClass(std::wstring name);
std::wstring GuidKey(const GUID &guid);
std::wstring IconKey(const TrayItem &item, const std::wstring &mode);
std::wstring LegacyKey(const TrayItem &item);
std::wstring RulePath(const std::wstring &key);
bool SameRuntime(const TrayItem &first, const TrayItem &second);

// 运行期间的确认结果：不能写入 groups.ini，也不能跨进程重启复用。
struct MatchState {
    std::map<std::wstring, RuntimeIdentity> ruleBindings;
    std::set<std::wstring> pendingRules;
};
struct MatchResult {
    std::wstring group, key, status;
    bool pending = false;
};

// 显式接收配置及运行时状态，不读取主窗口、托盘或其他全局变量。
class Matcher {
  public:
    Matcher(TrayConfig::Settings &settings, MatchState &state) : settings(settings), state(state) {}
    std::vector<std::wstring> IconKeys(const TrayItem &item) const;
    MatchResult FindGroup(const TrayItem &item,
                          const std::map<std::wstring, unsigned> *counts = nullptr) const;
    std::wstring GroupFor(const TrayItem &item,
                          const std::map<std::wstring, unsigned> *counts = nullptr) const;
    void ResolveGroups(std::vector<TrayItem> &list);
    void ForgetRule(const std::wstring &key);

  private:
    TrayConfig::Settings &settings;
    MatchState &state;
};
} // namespace TrayIdentity
