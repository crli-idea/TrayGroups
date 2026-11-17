# ScreenCaptureTimer 截图模块（TrayGroups 内置）

截图编码和调度以 C++ 模块方式编译进 TrayGroups.exe，不再生成独立常驻程序或第二个托盘图标。

每小时00、10、20、30、40、50分钟截图。默认保存到 Captures\yyyy-MM-dd\HH-mm.png，重名自动追加_2、_3，不覆盖已有图片。定时精度约半秒，系统忙碌会延迟。

两块或更多显示器按物理布局合成一张 PNG，支持负坐标；无屏幕覆盖的空隙填黑色。锁屏或桌面不可访问时跳过，休眠恢复后不补拍错过的时间点。

## 设置

右键 TrayGroups 的 T 图标，打开“定时截图”子菜单可立即截图、暂停、修改目录/文件名、查看资源或打开目录。修改根目录后仍会自动按日期建立子目录。

config.json 与 TrayGroups.exe 位于同一目录，首次启动自动创建默认配置。字段为 OutputDirectory、FileNameFormat。支持 HH-mm、HH-mm-ss、yyyy-MM-dd_HH-mm 等数字日期时间片段：yyyy、MM、dd、HH、hh、mm、ss、fff。HH是24小时制，hh是12小时制。便携目录需要有写入权限。

## 实现及验证

GDI每次读取32行像素，RGB转换后通过zlib逐行写PNG，不在进程里分配整张双屏位图。读取不同条带的时间略有不同，快速运动内容可能存在轻微时间差。本机4920x2160双屏一次截图实测约4.3秒。

已使用独立验证脚本检查PNG所有块的CRC、完整zlib结束标记、合成测试图每个RGB像素、实际双屏尺寸、两屏都有内容和黑色布局空隙。命名、跨年、十分钟边界、Unicode路径和配置也已测试。测试脚本和测试输出已在清理时移除，不是程序运行依赖。

资源页显示 TrayGroups 整个进程的任务管理器专用工作集、专用提交内存和总工作集；截图峰值也是整合后进程的采样值。内存随托盘图标数、设置窗口和显示器尺寸变化，不使用强制清空工作集。

## 文件与参数

activity.log 记录启动、截图、失败；last-capture.json 记录最近图片路径、布局、截图耗时和采样内存峰值。外部采样验证结果已清理；资源占用可在托盘菜单中查看。

截图功能通过 TrayGroups 的 T 菜单控制。

截图只保存本地，不上传，不自动删除。公共接口是本目录的 `CaptureTimer.h`，实现是 `ScreenCaptureTimer.cpp`；资源采样共用 `src/common/NativeResources.h`。编译依赖 Windows GDI 和 zlib；发布的 exe 已静态链接 zlib，无需另装 DLL。

截图测试源码在 `test/capture/`，自检程序、临时配置和报告输出到 `test/build/capture/`，正式程序不包含测试入口。运行仓库根目录下的 `test/capture/run.ps1` 执行截图模块测试；根目录 `build.ps1` 将功能编译到 TrayGroups.exe。
