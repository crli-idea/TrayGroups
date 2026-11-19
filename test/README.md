# 测试目录

`NativeTrayPanel.tests.h` 验证系统 ListView 多组同时展开、四列、图标快照隔离、重绘句柄稳定、固定截图区、滚动到达最后图标和 Esc；自测图在 `build/native-panel-preview.bmp`。`core-modules.ps1` 验证配置默认值、往返和非法参数处理。`ui-preview/` 是独立离线 HTML 设计预览，不参与原生程序运行。

`guid-research/` 的 [托盘 GUID 回归测试](guid-research/README.md) 直接链接正式 `TrayGuid.cpp`，验证内部 COM 快照、普通/GUID 图标识别与隐藏恢复；自测入口不参与正式构建。

测试源码、测试程序、临时配置、测试图片、报告及编译产物仅存放在此目录。测试配置与正式配置隔离。

运行 `run.ps1` 编译并执行托盘/设置页测试。所有新编译产物、临时配置和报告写入 `build/`；根目录 `build.ps1` 将正式程序输出到 `build/TrayGroups.exe`，正式程序不编译测试入口。

`run-all.ps1` 顺序执行独立模块、恢复生命周期、集成检查、完整托盘/设置页回归和截图模块测试。先执行根目录 `build.ps1`，并正常退出正在运行的 TrayGroups；桌面测试需要 Windows Explorer。`capture-integration.ps1` 使用本进程自己的隐藏工具栏核对状态扫描与图片扫描的身份、提示和可见状态一致性，检查图标句柄释放，并记录扫描耗时；不依赖 Explorer 的其他图标。它也验证默认 INI、截图消息、托盘点击恢复、恢复记录保留和日志轮换。

`core-modules.ps1` 只链接 `src/config/TrayConfig.cpp` 与 `src/tray/TrayIdentity.cpp`，不包含主程序、不初始化托盘。检查多个配置/匹配实例隔离、窗口位置局部保存、暂停开关、身份歧义和进程重建。此项及截图/集成测试也由 GitHub Actions 执行。

`capture/run.ps1` 编译截图模块测试，结果写入 `build/capture/`。`reference/` 是界面参考代码；其余只读诊断和历史修复工具均留在本目录，不参与正式构建。

`recovery-lifecycle.ps1` 仅操作测试自己注册的图标，在 `build/recovery-lifecycle/portable` 中运行正式 EXE。检查 INI 写入失败、内存记录丢失、原先隐藏状态、进程身份过期、正常退出、单独强制终止主程序后的看护恢复、升级替换和独立 `--restore`。结束时核对其他图标的隐藏状态没有变化。报告位于 `build/recovery-lifecycle/report.txt`。这些检查不模拟真实断电。

`recovery-diagnostics.cpp` 是只读托盘快照工具，输出同目录下的 `recovery-diagnostics.txt`；不会隐藏、恢复或点击图标。

发布 ZIP 直接输出项目根目录，便于本地自测；编译中间文件继续保存在 test/build/。

`GuiResources.ps1` 为独立 GUI 测试嵌入与正式程序相同的 Common Controls v6 清单，确保系统主题和控件子类接口可用。界面回归检查两种缩放下的控件边界，以及主操作按钮的 Tab、Enter 和 Esc 行为；预览图片也写入 `build/`。

`core-modules.ps1` 也验证失效规则诊断、重新绑定的冲突/进程变化拒绝、显式取消分组保留，以及 RAII 句柄、远程缓冲区、图标复制/移动/容器析构。`TrayInspector.tests.h` 随设置页回归运行，检查详情和规则窗口的内容、布局、取消与确认行为。截图模块测试包含 1、7、15、90、1440 分钟及跨日边界、非法值回退；集成测试检查界面保存间隔后重新调度。


系统控件回归覆盖六个 PropertySheet 页面、应用/确定/取消、截图页保存与取消、窗口位置、真实测试托盘图标的左键/双击/右键、四列分组布局、100 图标滚动及固定底栏。UI 报告使用任务管理器专用工作集口径，设置和面板均检查小于 7 MiB；输出在 `test/build/ui-self-test-*.txt`。原型/测试图示中的“示例”图标为合成数据，不代表已安装应用。

本地可设置 `$env:TRAYGROUPS_TEST_REAL_CAPTURE='1'` 后运行 `run.ps1`，额外在设置窗口打开时截取真实桌面，检查 PNG 的尺寸等于整个物理虚拟桌面，并检查采样峰值小于 7 MiB。图片只保存在忽略的 `test/build/native-ui-capture/Captures/`，不会打包或上传；CI 默认不启用真实桌面截图。


`SettingsButtons.tests.h` 逐项枚举六个设置页和标准按钮栏中的可见按钮/复选框，使用 `BM_CLICK` 检查点击后的实际状态（当前 17/17），并覆盖 T 面板三个按钮。检查详情、规则管理的重新绑定/删除/刷新/确定/取消/X、资源窗口 X，以及六个设置页面的标题栏 `SC_CLOSE`、消息预处理关闭、Alt+F4、未应用草稿丢弃和句柄释放。截图按钮使用测试编码器，打开目录按钮检查首次创建目录及 Explorer 请求；不点击其他程序的托盘图标，不弹出 Explorer 窗口。真实 PNG 编码由独立截图测试及可选真实桌面测试检查。

`settings-shell.ps1` 在正常退出常驻程序后，复制正式 Release EXE 到 `build/settings-shell/portable/`，通过真实主消息循环依次打开六个页面、发送标题栏系统关闭命令，检查窗口销毁且托盘进程仍存活，再通过第二次启动重新打开。此测试不定义 `NATIVE_TEST_BUILD`，结果在 `build/settings-shell/report.txt`；已加入 `run-all.ps1`。


`wallpaper.ps1` 测试通用 EDID 解码与校验、序列号身份、无序列号/重复序列号降级、准确设备路径写入、热插拔/换屏拒绝、图片格式和内容验证、全局显示方式、复制模式及失败撤销。它注入只存在于测试构建的读写接口，不修改用户真实壁纸；同一测试也由 Actions 执行。

`WallpaperPage.tests.h` 随 `run.ps1` 测试壁纸页：读取真实拓扑、识别标记的屏幕位置与释放、刷新、浏览/取消、有限尺寸预览、未连接屏幕禁用、切换/刷新保留草稿、标准应用、X 取消、重新打开以及缩略图句柄释放和 7 MiB 内存检查。按钮覆盖随六个属性页更新为 17/17。`settings-shell.ps1` 的正式 Release EXE 关闭回归随之覆盖六页。

默认图片测试还验证按物理分辨率查询 `wallpapers.ini`、相对 EXE 目录解析路径，以及“默认图片”按钮只填写草稿、不直接改 Windows。`package.ps1` 按白名单包含两个 INI、两张壁纸和 EXE/README，并核对图片实际分辨率与名称一致。

右键菜单回归枚举实际构建的菜单，通过主窗口命令入口验证全部 15 项。包括重新加载时同步已打开的设置页、暂停后的勾选和状态、一次显示器枚举内重新应用壁纸，以及退出进入恢复流程。文件打开、菜单触发的截图和壁纸应用检查使用隔离回调。恢复生命周期测试另检查内容相同的恢复记录不会在例行刷新中重写。
