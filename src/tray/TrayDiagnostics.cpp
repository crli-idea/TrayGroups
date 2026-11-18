#include "TrayDiagnostics.h"
#include "../common/TraySupport.h"
#include <algorithm>
#include <commctrl.h>

namespace TrayDiagnostics {
using namespace TraySupport;
using namespace TrayIdentity;
namespace {
std::wstring RuleMode(const std::wstring &key) {
    return key.substr(0, key.find(L'|'));
}
bool Matches(const Rule &rule, const TrayItem &item, const std::wstring &mode) {
    if (rule.scope == Scope::Path)
        return Lower(item.path) == rule.key;
    if (rule.scope == Scope::Program)
        return Lower(item.process) == rule.key;
    return IconKey(item, mode) == rule.key;
}
std::map<std::wstring, std::wstring> &Rules(TrayConfig::Settings &settings, Scope scope) {
    return scope == Scope::Icon   ? settings.assignments
           : scope == Scope::Path ? settings.pathAssignments
                                  : settings.programAssignments;
}
} // namespace
std::wstring IconDetails(const TrayItem &item, const std::wstring &mode, bool foldEnabled) {
    std::wstring state;
    if (item.pending)
        state = item.matchStatus;
    else if (item.group.empty())
        state = L"未分组，交由 Windows 显示";
    else if (!foldEnabled)
        state = L"折叠已暂停";
    else if (!item.addressKnown)
        state = L"未取得可信注册信息，不能确认隐藏或恢复地址";
    else if (item.state & TBSTATE_HIDDEN)
        state = L"Explorer 已确认隐藏";
    else
        state = L"仍显示在 Windows；系统拒绝、图标自行显示或尚未应用，可查看本地日志";
    return L"程序：" + item.process + L"\r\n提示：" + item.tip + L"\r\n路径：" + item.path +
           L"\r\n窗口类别：" + item.ownerClass + L"\r\n图标编号：" + Number(item.id) +
           L"\r\n真实 GUID：" +
           (item.usesGuid       ? GuidKey(item.guid)
            : item.addressKnown ? L"无（普通窗口＋编号注册）"
                                : L"未取得（可能已隐藏或查询失败）") +
           L"\r\n寻址方式：" +
           (item.addressKnown ? item.usesGuid ? L"GUID" : L"窗口＋编号" : L"未知") +
           L"\r\n当前匹配键：" + IconKey(item, mode) + L"\r\n分组：" +
           (item.group.empty() ? L"未分组" : item.group) + L"\r\n匹配结果：" + item.matchStatus +
           L"\r\n显示状态：" + state + L"\r\n\r\n运行时核验\r\n进程 ID：" +
           Number(item.runtime.pid) + L"\r\n进程创建时间：" + Number(item.runtime.started) +
           L"\r\n窗口：" + Number((ULONG_PTR)item.owner) + L"\r\n回调消息：" +
           Number(item.callback) + L"\r\n图标协议版本：" + Number(item.version);
}
std::vector<Rule> UnmatchedRules(const TrayConfig::Settings &settings,
                                 const std::vector<TrayItem> &icons) {
    std::vector<Rule> result;
    for (Scope scope : {Scope::Icon, Scope::Path, Scope::Program}) {
        const auto &rules = scope == Scope::Icon   ? settings.assignments
                            : scope == Scope::Path ? settings.pathAssignments
                                                   : settings.programAssignments;
        for (const auto &entry : rules) {
            Rule rule{scope, entry.first, entry.second, L"", 0};
            const TrayItem *pending = nullptr;
            for (const auto &icon : icons) {
                if (Matches(rule, icon, settings.iconMatch)) {
                    ++rule.matches;
                    if (icon.pending)
                        pending = &icon;
                }
            }
            bool inactive =
                scope == Scope::Icon && RuleMode(rule.key) != settings.iconMatch &&
                !(settings.iconMatch == L"path-guid" && RuleMode(rule.key) == L"path-class-id");
            if (inactive)
                rule.reason = L"未启用的识别方式";
            else if (rule.matches > 1 && scope == Scope::Icon)
                rule.reason = L"多个图标匹配，需重新选择";
            else if (pending && scope == Scope::Icon)
                rule.reason = pending->matchStatus;
            else if (rule.matches)
                continue;
            else {
                std::wstring path = scope == Scope::Icon   ? RulePath(rule.key)
                                    : scope == Scope::Path ? rule.key
                                                           : L"";
                DWORD attributes =
                    path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
                if (!path.empty() && attributes == INVALID_FILE_ATTRIBUTES &&
                    (GetLastError() == ERROR_FILE_NOT_FOUND ||
                     GetLastError() == ERROR_PATH_NOT_FOUND))
                    rule.reason = L"程序路径不存在或已经变化";
                else if (!path.empty() &&
                         std::any_of(icons.begin(), icons.end(),
                                     [&](const TrayItem &i) { return Lower(i.path) == path; }))
                    rule.reason = L"程序仍在，GUID、窗口类别或编号已经变化";
                else
                    rule.reason = L"当前未找到图标；程序可能未启动";
            }
            result.push_back(std::move(rule));
        }
    }
    return result;
}
void Remove(TrayConfig::Settings &settings, const Rule &rule) {
    Rules(settings, rule.scope).erase(rule.key);
}
bool Rebind(TrayConfig::Settings &settings, const Rule &rule, const TrayItem &target,
            const std::vector<TrayItem> &live, std::wstring &error) {
    auto &source = Rules(settings, rule.scope);
    auto previous = source.find(rule.key);
    if (previous == source.end() || previous->second != rule.group) {
        error = L"原规则已变化，请刷新。";
        return false;
    }
    auto key = rule.scope == Scope::Icon   ? IconKey(target, settings.iconMatch)
               : rule.scope == Scope::Path ? Lower(target.path)
                                           : Lower(target.process);
    unsigned count = 0;
    bool same = false;
    for (const auto &icon : live) {
        if (rule.scope == Scope::Icon && IconKey(icon, settings.iconMatch) == key)
            ++count;
        if (SameRuntime(target, icon))
            same = true;
    }
    if (key.empty() || !same || (rule.scope == Scope::Icon && count != 1)) {
        error = L"当前图标已变化或无法唯一区分，请刷新后重新选择。";
        return false;
    }
    if (key != rule.key && source.count(key)) {
        error = L"目标已有规则，请先处理已有规则，避免覆盖。";
        return false;
    }
    source.erase(previous);
    source[key] = rule.group;
    error.clear();
    return true;
}
} // namespace TrayDiagnostics
