#include "TrayIdentity.h"
#include "../common/TraySupport.h"
#include <algorithm>
#include <cstring>

namespace TrayIdentity {
using namespace TraySupport;
std::wstring StableClass(std::wstring name) {
    name = Lower(name);
    // ATL/WinForms include process-specific addresses; never persist those.
    if (name.find(L"atl:") == 0)
        name = L"atl";
    size_t p = name.find(L".app.");
    if (name.find(L"windowsforms") == 0 && p != std::wstring::npos)
        name.resize(p);
    return name;
}
static std::wstring KeyPart(const std::wstring &text) {
    std::wstring out;
    const wchar_t *hex = L"0123456789abcdef";
    for (wchar_t c : Lower(text))
        if (c == L'%' || c == L'|' || c == L'=' || c == L'\r' || c == L'\n' || c == L'[' ||
            c == L']') {
            out += L'%';
            out += hex[(c >> 4) & 15];
            out += hex[c & 15];
        } else
            out += c;
    return out;
}
std::wstring GuidKey(const GUID &guid) {
    wchar_t text[40];
    swprintf_s(text, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", guid.Data1, guid.Data2,
               guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
               guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return text;
}
std::wstring IconKey(const TrayItem &i, const std::wstring &mode) {
    // 保存的键只用于寻找候选图标；真正隐藏/转发前还必须核对运行时身份。
    // GUID 模式拿不到 GUID 时沿用路径、窗口类、编号，不能编造稳定身份。
    if (i.path.empty())
        return L""; // A PID is not a durable identity.
    std::wstring path = KeyPart(i.path);
    if (mode == L"path-guid") {
        GUID empty = {};
        if (i.usesGuid && memcmp(&i.guid, &empty, sizeof(empty)))
            return mode + L"|" + path + L"|" + GuidKey(i.guid);
        return IconKey(i, L"path-class-id");
    }
    if (mode == L"path-id")
        return mode + L"|" + path + L"|" + Number(i.id);
    return mode + L"|" + path + L"|" + KeyPart(StableClass(i.ownerClass)) + L"|" +
           (mode == L"path-class-tip" ? KeyPart(i.tip) : Number(i.id));
}
std::wstring LegacyKey(const TrayItem &i) {
    return Lower(i.path) + L"|" + Number(i.id);
}
std::vector<std::wstring> Matcher::IconKeys(const TrayItem &i) const {
    std::vector<std::wstring> keys;
    for (const auto &mode :
         {settings.iconMatch, std::wstring(L"path-guid"), std::wstring(L"path-class-id"),
          std::wstring(L"path-id"), std::wstring(L"path-class-tip")}) {
        auto key = IconKey(i, mode);
        if (!key.empty() && std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(key);
    }
    if (!i.path.empty())
        keys.push_back(LegacyKey(i));
    return keys;
}

MatchResult Matcher::FindGroup(const TrayItem &item,
                               const std::map<std::wstring, unsigned> *counts) const {
    MatchResult result;
    bool found = false;
    // 单图标规则优先于整路径/程序规则。出现冲突或重复匹配时必须待确认，
    // 不能退回更宽泛的规则，否则容易把同一 Explorer 的其他图标一起隐藏。
    for (const auto &key : IconKeys(item)) {
        auto rule = settings.assignments.find(key);
        if (rule == settings.assignments.end())
            continue;
        auto count =
            counts ? counts->find(key) : std::map<std::wstring, unsigned>::const_iterator{};
        if (state.pendingRules.count(key) ||
            (counts && count != counts->end() && count->second != 1)) {
            result.pending = true;
            result.status = L"待确认：多个图标符合规则";
            return result;
        }
        if (found && result.group != rule->second) {
            result.pending = true;
            result.status = L"待确认：分组规则不一致";
            return result;
        }
        auto binding = state.ruleBindings.find(key);
        if (item.runtime.Valid() && binding != state.ruleBindings.end() &&
            !(binding->second == item.runtime)) {
            result.pending = true;
            result.status = L"待确认：图标已重新创建";
            return result;
        }
        result.group = rule->second;
        result.key = key;
        found = true;
    }
    if (found) {
        result.status = result.group.empty() ? L"已单独取消分组" : L"单图标规则匹配";
        return result;
    }
    auto path = settings.pathAssignments.find(Lower(item.path));
    if (path != settings.pathAssignments.end()) {
        result.group = path->second;
        result.status = L"按完整路径分组";
        return result;
    }
    auto program = settings.programAssignments.find(Lower(item.process));
    if (program != settings.programAssignments.end()) {
        result.group = program->second;
        result.status = L"按程序名分组";
        return result;
    }
    result.status = L"未分组";
    return result;
}
std::wstring Matcher::GroupFor(const TrayItem &item,
                               const std::map<std::wstring, unsigned> *counts) const {
    auto match = FindGroup(item, counts);
    return match.pending ? L"" : match.group;
}
void Matcher::ResolveGroups(std::vector<TrayItem> &list) {
    // 歧义在本次运行中保持待确认，即使重复图标消失，也不自动认领剩下的那个。
    std::map<std::wstring, unsigned> counts;
    for (const auto &item : list)
        for (const auto &key : IconKeys(item))
            counts[key]++;
    for (const auto &count : counts)
        if (count.second > 1 && settings.assignments.count(count.first))
            state.pendingRules.insert(count.first);
    for (auto &item : list) {
        item.key = IconKey(item, settings.iconMatch);
        auto match = FindGroup(item, &counts);
        if (item.owner && !item.runtime.Valid() && !match.group.empty()) {
            match.pending = true;
            match.status = L"待确认：无法核对程序身份";
        }
        item.pending = match.pending;
        item.matchStatus = match.status;
        item.group = match.pending ? L"" : match.group;
        if (!match.pending && item.runtime.Valid())
            for (const auto &key : IconKeys(item))
                if (settings.assignments.count(key) && !state.ruleBindings.count(key))
                    state.ruleBindings[key] = item.runtime;
    }
}
void Matcher::ForgetRule(const std::wstring &key) {
    settings.assignments.erase(key);
    state.pendingRules.erase(key);
    state.ruleBindings.erase(key);
}
bool SameRuntime(const TrayItem &a, const TrayItem &b) {
    // PID 和 HWND 都可能被系统复用；创建时间与回调信息一起核验，避免操作新图标。
    return a.owner == b.owner && a.id == b.id && a.runtime.Valid() && b.runtime.Valid() &&
           a.runtime == b.runtime && a.usesGuid == b.usesGuid &&
           (!a.usesGuid || !memcmp(&a.guid, &b.guid, sizeof(GUID))) &&
           Lower(a.path) == Lower(b.path) &&
           StableClass(a.ownerClass) == StableClass(b.ownerClass) && a.callback == b.callback;
}

std::wstring RulePath(const std::wstring &key) {
    size_t a = key.find(L'|');
    if (a == std::wstring::npos)
        return L"";
    if (key.find(L"path-") != 0)
        return key.substr(0, a);
    size_t b = key.find(L'|', a + 1);
    if (b == std::wstring::npos)
        return L"";
    std::wstring out, part = key.substr(a + 1, b - a - 1);
    for (size_t n = 0; n < part.size(); n++) {
        if (part[n] == L'%' && n + 2 < part.size()) {
            wchar_t hex[3] = {part[n + 1], part[n + 2], 0};
            wchar_t *end = nullptr;
            long value = wcstol(hex, &end, 16);
            if (end == hex + 2) {
                out += (wchar_t)value;
                n += 2;
                continue;
            }
        }
        out += part[n];
    }
    return out;
}

} // namespace TrayIdentity
