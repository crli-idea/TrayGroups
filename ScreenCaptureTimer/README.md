# ScreenCaptureTimer 定时双屏截图（C++ 原生版）

ScreenCaptureTimer.exe 现在由 C++ 实现，不需要 .NET 或 Python。旧.NET版本及测试残留已清理，当前只运行原生版。

每小时00、10、20、30、40、50分钟截图。默认保存到 Captures\yyyy-MM-dd\HH-mm.png，重名自动追加_2、_3，不覆盖已有图片。定时精度约半秒，系统忙碌会延迟。

两块或更多显示器按物理布局合成一张 PNG，支持负坐标；无屏幕覆盖的空隙填黑色。锁屏或桌面不可访问时跳过，休眠恢复后不补拍错过的时间点。

## 设置

左键托盘图标打开设置。右键可立即截图、暂停、修改目录/文件名、查看资源、打开目录、切换登录后自动启动、退出。修改根目录后仍会自动按日期建立子目录。

config.json 与 exe 位于同一目录，首次启动自动创建默认配置。字段为 OutputDirectory、FileNameFormat。支持 HH-mm、HH-mm-ss、yyyy-MM-dd_HH-mm 等数字日期时间片段：yyyy、MM、dd、HH、hh、mm、ss、fff。HH是24小时制，hh是12小时制。退出程序后也可手动编辑配置再启动。便携目录需要有写入权限。

## 实现及验证

GDI每次读取32行像素，RGB转换后通过zlib逐行写PNG，不在进程里分配整张双屏位图。读取不同条带的时间略有不同，快速运动内容可能存在轻微时间差。本机4920x2160双屏一次截图实测约4.3秒。

已使用独立验证脚本检查PNG所有块的CRC、完整zlib结束标记、合成测试图每个RGB像素、实际双屏尺寸、两屏都有内容和黑色布局空隙。命名、跨年、十分钟边界、Unicode路径和配置也已测试。测试脚本和测试输出已在清理时移除，不是程序运行依赖。

资源页显示任务管理器常见内存列的专用工作集、专用提交内存和含共享DLL的总工作集。当前采样：常驻专用工作集约1.1 MiB，截图采样峰值约1.7 MiB。不是对所有未来瞬时峰值的保证，不使用强制清空工作集。

## 文件与参数

activity.log 记录启动、截图、失败；last-capture.json 记录最近图片路径、布局、截图耗时和采样内存峰值。外部采样验证结果已清理；资源占用可在托盘菜单中查看。

--settings 打开设置；--capture-now 通知后台实例截图；--capture-once 截一次后退出；--stop 正常退出。

截图只保存本地，不上传，不自动删除。源码是 ScreenCaptureTimer.cpp、NativeResources.h。编译依赖Windows GDI和zlib；发布的.exe已静态链接zlib，无需另装DLL。

测试代码、自检程序、临时配置和测试报告统一放在 test 目录，正式程序不包含测试入口。运行 test\run.ps1 执行截图测试；仓库根目录 build.ps1 会将 TrayGroups 和 ScreenCaptureTimer 分别编译到各自的 test\build 目录。
