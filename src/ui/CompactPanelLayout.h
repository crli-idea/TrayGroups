#pragma once
#include <windows.h>
#include "../config/TrayConfig.h"
#include <algorithm>
#include <set>
#include <vector>

// 纯布局计算：图标矩形和组名矩形共享内容坐标，滚动只修改偏移，不重建窗口。
namespace CompactPanel {
struct Group {
    std::wstring name;
    int count = 0, headerHeight = 0;
};
struct Section {
    RECT heading = {};
    int first = 0, count = 0;
};
struct Layout {
    std::vector<Section> sections;
    std::vector<RECT> tiles;
    int height = 0;
};
inline std::vector<std::wstring> Order(const std::set<std::wstring> &names,
                                       const std::vector<std::wstring> &preferred) {
    std::vector<std::wstring> result;
    for (const auto &name : preferred)
        if (!name.empty() && names.count(name) &&
            std::find(result.begin(), result.end(), name) == result.end())
            result.push_back(name);
    for (const auto &name : names)
        if (!name.empty() && std::find(result.begin(), result.end(), name) == result.end())
            result.push_back(name);
    return result;
}
inline Layout Build(const std::vector<Group> &groups, const TrayConfig::Panel &options, int width,
                    int dpi) {
    auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };
    Layout result;
    const int columns = std::clamp(options.columns, 3, 6), gap = px(6), margin = px(18);
    const int tileWidth = std::max(px(20), (width - margin * 2 - gap * (columns - 1)) / columns);
    const int rowHeight = px(options.iconSize + (options.showNames ? 29 : 18));
    int y = px(6);
    for (const auto &group : groups) {
        int heading = std::max(px(options.labelSize + 12), group.headerHeight);
        Section section{
            {margin, y, width - margin, y + heading}, (int)result.tiles.size(), group.count};
        result.sections.push_back(section);
        y += heading + px(4);
        for (int n = 0; n < group.count; ++n) {
            int x = margin + n % columns * (tileWidth + gap),
                top = y + n / columns * (rowHeight + gap);
            result.tiles.push_back({x, top, x + tileWidth, top + rowHeight});
        }
        y += group.count ? ((group.count + columns - 1) / columns) * (rowHeight + gap) - gap
                         : px(20);
        y += px(options.spacing);
    }
    result.height = y + px(4);
    return result;
}
inline int ScrollLimit(int contentHeight, int viewportHeight) {
    return std::max(0, contentHeight - viewportHeight);
}
} // namespace CompactPanel
