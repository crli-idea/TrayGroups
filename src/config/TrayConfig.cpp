#include "TrayConfig.h"
#include "../common/TraySupport.h"
#include <algorithm>

namespace TrayConfig {
using namespace TraySupport;
int LabelSize(int value) {
    return std::max(8, std::min(32, value));
}
int Settings::GroupLabelSize(const std::wstring &name) const {
    auto i = groupLabelSizes.find(name);
    return i == groupLabelSizes.end() ? defaultLabelSize : i->second;
}
int Settings::GroupLabelWidth(const std::wstring &group) const {
    auto i = groupLabelWidths.find(group);
    return i == groupLabelWidths.end() ? 280 : i->second;
}
int Settings::GroupLabelHeight(const std::wstring &group) const {
    auto i = groupLabelHeights.find(group);
    return i == groupLabelHeights.end() ? 0 : i->second;
}
bool ValidMatch(const std::wstring &value) {
    return value == L"path-guid" || value == L"path-class-id" || value == L"path-id" ||
           value == L"path-class-tip";
}
void Settings::Load(const std::wstring &configPath) {
    assignments.clear();
    programAssignments.clear();
    pathAssignments.clear();
    groupLabelSizes.clear();
    groupLabelWidths.clear();
    groupLabelHeights.clear();
    iconMatch = L"path-guid";
    defaultLabelSize = 12;
    foldIcons = true;
    haveSettingsPosition = false;
    debugLogging = true;
    panel = Panel{};
    bool ordered = false;
    std::wstring text = ReadText(configPath), section;
    size_t start = 0;
    bool hasX = false, hasY = false;
    if (!text.empty() && text[0] == 0xfeff)
        text.erase(0, 1);
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;
        if (line.front() == L'[' && line.back() == L']') {
            section = Lower(line.substr(1, line.size() - 2));
            continue;
        }
        size_t pos = line.find(L'=');
        if (pos == std::wstring::npos)
            continue;
        std::wstring key = Trim(line.substr(0, pos)), val = Trim(line.substr(pos + 1)),
                     low = Lower(key);
        if (section == L"settings") {
            if (low == L"foldoriginalicons")
                foldIcons = val == L"1";
            else if (low == L"debuglogging")
                debugLogging = val == L"1";
            else if (low == L"iconmatch" && ValidMatch(val))
                iconMatch = val;
            else if (low == L"defaultgrouplabelsize")
                defaultLabelSize = LabelSize(_wtoi(val.c_str()));
        } else if (section == L"panel") {
            int number = _wtoi(val.c_str());
            if (low == L"width")
                panel.width = std::clamp(number, 340, 600);
            else if (low == L"iconsize")
                panel.iconSize = std::clamp(number, 20, 44);
            else if (low == L"columns")
                panel.columns = std::clamp(number, 3, 6);
            else if (low == L"shownames")
                panel.showNames = val != L"0";
            else if (low == L"showcapture")
                panel.showCapture = val != L"0";
        } else if (section == L"grouporder") {
            if (!ordered) {
                panel.groupOrder.clear();
                ordered = true;
            }
            if (!val.empty() && std::find(panel.groupOrder.begin(), panel.groupOrder.end(), val) ==
                                    panel.groupOrder.end())
                panel.groupOrder.push_back(val);
        } else if (section == L"programs")
            programAssignments[low] = val;
        else if (section == L"paths")
            pathAssignments[low] = val;
        else if (section == L"icons")
            assignments[low] = val; // Empty means explicitly ungrouped.
        else if (section == L"grouplabelsizes")
            groupLabelSizes[key] = LabelSize(_wtoi(val.c_str()));
        else if (section == L"grouplabelwidths")
            groupLabelWidths[key] = std::max(120, std::min(600, _wtoi(val.c_str())));
        else if (section == L"grouplabelheights") {
            int h = _wtoi(val.c_str());
            groupLabelHeights[key] = h == 0 ? 0 : std::max(32, std::min(160, h));
        } else if (section == L"window") {
            if (low == L"left") {
                settingsX = _wtoi(val.c_str());
                hasX = true;
            } else if (low == L"top") {
                settingsY = _wtoi(val.c_str());
                hasY = true;
            }
        }
    }
    haveSettingsPosition = hasX && hasY;
}
std::wstring Settings::WindowSection() const {
    return L"[Window]\nLeft=" + std::to_wstring(settingsX) + L"\nTop=" +
           std::to_wstring(settingsY) + L"\n";
}
static bool AtomicConfig(const std::wstring &configPath, const std::wstring &text) {
    std::wstring tmp = configPath + L".tmp";
    return WriteText(tmp, text) && MoveFileExW(tmp.c_str(), configPath.c_str(),
                                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
bool Settings::Save(const std::wstring &configPath) const {
    std::wstring text =
        L"; TrayGroups UTF-8. Right-click T -> Reload configuration after manual "
        L"edits.\n[Settings]\nFoldOriginalIcons=" +
        std::wstring(foldIcons ? L"1" : L"0") +
        L"\n"
        L"DebugLogging=" +
        std::wstring(debugLogging ? L"1" : L"0") +
        L"\n; Per-icon identity modes: path-guid (default), path-class-id, path-id, "
        L"path-class-tip.\nIconMatch=" +
        iconMatch + L"\nDefaultGroupLabelSize=" + std::to_wstring(defaultLabelSize) +
        L"\n\n[Programs]\n; Executable filename: all matching icons (explicit broad rules).\n";
    for (const auto &i : programAssignments)
        text += Escape(i.first) + L"=" + Escape(i.second) + L"\n";
    text += L"\n[Paths]\n; Full executable path: all icons belonging to this exact path.\n";
    for (const auto &i : pathAssignments)
        text += Escape(i.first) + L"=" + Escape(i.second) + L"\n";
    text += L"\n[Icons]\n; Per-icon overrides. Empty value means explicitly ungrouped.\n";
    for (const auto &i : assignments)
        text += Escape(i.first) + L"=" + Escape(i.second) + L"\n";
    text += L"\n[GroupLabelSizes]\n; Group name = font size in points (8..32). Ungrouped uses the "
            L"empty key.\n";
    for (const auto &i : groupLabelSizes)
        text += Escape(i.first) + L"=" + std::to_wstring(i.second) + L"\n";
    text += L"\n[GroupLabelWidths]\n; Width in logical pixels (120..600).\n";
    for (const auto &i : groupLabelWidths)
        text += Escape(i.first) + L"=" + std::to_wstring(i.second) + L"\n";
    text +=
        L"\n[GroupLabelHeights]\n; Minimum height in logical pixels (32..160), 0 = automatic.\n";
    for (const auto &i : groupLabelHeights)
        text += Escape(i.first) + L"=" + std::to_wstring(i.second) + L"\n";
    text += L"\n[Panel]\n; Native system icon view, dimensions in logical pixels.\nWidth=" +
            std::to_wstring(panel.width) + L"\nIconSize=" + std::to_wstring(panel.iconSize) +
            L"\nColumns=" + std::to_wstring(panel.columns) + L"\nShowNames=" +
            std::to_wstring(panel.showNames) + L"\nShowCapture=" +
            std::to_wstring(panel.showCapture) +
            L"\n\n[GroupOrder]\n; Only existing groups are displayed, in file order.\n";
    for (size_t n = 0; n < panel.groupOrder.size(); ++n)
        text += std::to_wstring(n + 1) + L"=" + Escape(panel.groupOrder[n]) + L"\n";
    if (haveSettingsPosition)
        text += L"\n" + WindowSection();
    return AtomicConfig(configPath, text);
}
bool Settings::SaveWindowPosition(const std::wstring &configPath, int left, int top) {
    settingsX = left;
    settingsY = top;
    haveSettingsPosition = true;
    // Only merge the Window section: closing must not save unsaved group edits.
    std::wstring original = ReadText(configPath), text;
    size_t start = 0;
    bool skip = false;
    while (start < original.size()) {
        size_t end = original.find(L'\n', start);
        if (end == std::wstring::npos)
            end = original.size();
        std::wstring line = original.substr(start, end - start), trimmed = Trim(line);
        start = end + 1;
        if (!trimmed.empty() && trimmed.front() == L'[' && trimmed.back() == L']')
            skip = Lower(trimmed) == L"[window]";
        if (!skip)
            text += line + L"\n";
    }
    size_t last = text.find_last_not_of(L" \t\r\n");
    text = last == std::wstring::npos ? L"" : text.substr(0, last + 1);
    return AtomicConfig(configPath, text + L"\n\n" + WindowSection());
}

bool PauseFolding(const std::wstring &configPath) {
    // Change only the pause flag, preserving any unapplied settings in the UI.
    std::wstring text = ReadText(configPath), section;
    size_t at = 0, insert = std::wstring::npos;
    while (at < text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = Trim(text.substr(at, end - at));
        if (!line.empty() && line.front() == L'[' && line.back() == L']') {
            section = Lower(line.substr(1, line.size() - 2));
            if (section == L"settings")
                insert = std::min(end + 1, text.size());
        } else if (section == L"settings") {
            size_t equals = line.find(L'=');
            if (equals != std::wstring::npos &&
                Lower(Trim(line.substr(0, equals))) == L"foldoriginalicons") {
                text.replace(at, end - at, L"FoldOriginalIcons=0");
                return AtomicConfig(configPath, text);
            }
        }
        at = end + 1;
    }
    if (insert == std::wstring::npos)
        text = L"[Settings]\nFoldOriginalIcons=0\n\n" + text;
    else
        text.insert(insert, L"\nFoldOriginalIcons=0\n");
    return AtomicConfig(configPath, text);
}

} // namespace TrayConfig
