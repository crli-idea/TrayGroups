# 测试目录

测试源码、测试程序、临时配置、测试图片、报告及编译产物仅存放在此目录。测试配置与正式配置隔离。

运行 run.ps1 编译并执行测试。根目录 build.ps1 将正式程序输出到 test\build；正式程序不编译测试入口。

`run-all.ps1` 顺序执行恢复生命周期、集成检查、完整托盘/设置页回归和截图模块测试。先执行根目录 `build.ps1`，并正常退出正在运行的 TrayGroups；桌面测试需要 Windows Explorer。`capture-integration.ps1` 使用本进程自己的隐藏工具栏核对状态扫描与图片扫描的身份、提示和可见状态一致性，检查图标句柄释放，并记录扫描耗时；不依赖 Explorer 的其他图标。它也验证默认 INI、截图消息、托盘点击恢复、恢复记录保留和日志轮换。

`recovery-lifecycle.ps1` 仅操作测试自己注册的图标，在 `build/recovery-lifecycle/portable` 中运行正式 EXE。检查 INI 写入失败、内存记录丢失、原先隐藏状态、进程身份过期、正常退出、单独强制终止主程序后的看护恢复、升级替换和独立 `--restore`。结束时核对其他图标的隐藏状态没有变化。报告位于 `build/recovery-lifecycle/report.txt`。这些检查不模拟真实断电。

`recovery-diagnostics.cpp` 是只读托盘快照工具，输出同目录下的 `recovery-diagnostics.txt`；不会隐藏、恢复或点击图标。
