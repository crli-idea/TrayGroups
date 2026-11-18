# 测试目录

`guid-research/` 的 [托盘 GUID 回归测试](guid-research/README.md) 直接链接正式 `TrayGuid.cpp`，验证内部 COM 快照、普通/GUID 图标识别与隐藏恢复；自测入口不参与正式构建。

测试源码、测试程序、临时配置、测试图片、报告及编译产物仅存放在此目录。测试配置与正式配置隔离。

运行 `run.ps1` 编译并执行托盘/设置页测试。所有新编译产物、临时配置和报告写入 `build/`；根目录 `build.ps1` 将正式程序输出到 `build/TrayGroups.exe`，正式程序不编译测试入口。

`run-all.ps1` 顺序执行独立模块、恢复生命周期、集成检查、完整托盘/设置页回归和截图模块测试。先执行根目录 `build.ps1`，并正常退出正在运行的 TrayGroups；桌面测试需要 Windows Explorer。`capture-integration.ps1` 使用本进程自己的隐藏工具栏核对状态扫描与图片扫描的身份、提示和可见状态一致性，检查图标句柄释放，并记录扫描耗时；不依赖 Explorer 的其他图标。它也验证默认 INI、截图消息、托盘点击恢复、恢复记录保留和日志轮换。

`core-modules.ps1` 只链接 `src/config/TrayConfig.cpp` 与 `src/tray/TrayIdentity.cpp`，不包含主程序、不初始化托盘。检查多个配置/匹配实例隔离、窗口位置局部保存、暂停开关、身份歧义和进程重建。此项及截图/集成测试也由 GitHub Actions 执行。

`capture/run.ps1` 编译截图模块测试，结果写入 `build/capture/`。`reference/` 是界面参考代码；其余只读诊断和历史修复工具均留在本目录，不参与正式构建。

`recovery-lifecycle.ps1` 仅操作测试自己注册的图标，在 `build/recovery-lifecycle/portable` 中运行正式 EXE。检查 INI 写入失败、内存记录丢失、原先隐藏状态、进程身份过期、正常退出、单独强制终止主程序后的看护恢复、升级替换和独立 `--restore`。结束时核对其他图标的隐藏状态没有变化。报告位于 `build/recovery-lifecycle/report.txt`。这些检查不模拟真实断电。

`recovery-diagnostics.cpp` 是只读托盘快照工具，输出同目录下的 `recovery-diagnostics.txt`；不会隐藏、恢复或点击图标。

发布 ZIP 直接输出项目根目录，便于本地自测；编译中间文件继续保存在 test/build/。

`core-modules.ps1` 也验证失效规则诊断、重新绑定的冲突/进程变化拒绝、显式取消分组保留，以及 RAII 句柄、远程缓冲区、图标复制/移动/容器析构。`TrayInspector.tests.h` 随设置页回归运行，检查详情和规则窗口的内容、布局、取消与确认行为。截图模块测试包含 1、7、15、90、1440 分钟及跨日边界、非法值回退；集成测试检查界面保存间隔后重新调度。
