#pragma once
#include <map>
#include <string>
#include <vector>

// 只管理 groups.ini 的可持久化设置；运行时绑定和恢复日志不属于配置。
namespace TrayConfig {
int LabelSize(int value);
bool ValidMatch(const std::wstring &value);

// 系统图标视图尺寸采用 96 DPI 逻辑像素；字体和组标题遵循系统主题。
struct Panel {
    int width = 420, iconSize = 28, columns = 4;
    bool showNames = true, showCapture = true;
    std::vector<std::wstring> groupOrder = {L"效率工具", L"影音娱乐", L"连接服务"};
};

struct Settings {
    Panel panel;
    std::wstring iconMatch = L"path-guid";
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
