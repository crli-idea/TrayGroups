#pragma once
#include <map>
#include <string>

// 只管理 groups.ini 的可持久化设置；运行时绑定和恢复日志不属于配置。
namespace TrayConfig {
int LabelSize(int value);
bool ValidMatch(const std::wstring &value);

struct Settings {
    std::wstring iconMatch = L"path-class-id";
    int defaultLabelSize = 12, settingsX = 0, settingsY = 0;
    bool haveSettingsPosition = false, foldIcons = true, debugLogging = true;
    std::map<std::wstring, int> groupLabelSizes, groupLabelWidths, groupLabelHeights;
    std::map<std::wstring, std::wstring> assignments, programAssignments, pathAssignments;

    void Load(const std::wstring &path);
    bool Save(const std::wstring &path) const;
    int GroupLabelSize(const std::wstring &name) const;
    int GroupLabelWidth(const std::wstring &name) const;
    int GroupLabelHeight(const std::wstring &name) const;

    // 关闭设置窗口只合并位置，不能顺带保存用户尚未应用的分组编辑。
    bool SaveWindowPosition(const std::wstring &path, int left, int top);

  private:
    std::wstring WindowSection() const;
};

// 恢复全部后暂停折叠，只修改开关并保留文件内其他设置及未知字段。
bool PauseFolding(const std::wstring &path);
} // namespace TrayConfig
