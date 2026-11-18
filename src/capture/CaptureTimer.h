#pragma once
#include <windows.h>
#include <string>

// 截图模块的公共接口：主程序只调用这些操作，调度和 PNG 编码留在 .cpp 中。
// 除后台截图工作线程外，这些函数均由主窗口消息线程调用；本头文件不创建线程。
namespace CaptureTimer {
inline constexpr UINT CaptureNowCommand = 2101, PauseCommand = 2102, SettingsCommand = 2103,
                      ResourcesCommand = 2104, FolderCommand = 2105;
// 使用宿主窗口接收计时和完成消息；配置、截图目录以 directory 为便携根目录。
void Initialize(HWND owner, HINSTANCE instance, const std::wstring &directory);
// 宿主必须把这两种消息分别交回 OnCompletion / OnTimer，保持截图状态与 UI 同步。
UINT CompletionMessage();
UINT TimerId();
bool IsBusy();
// 属性页编辑使用值副本；校验不写盘，应用成功后重新计算下次截图时间。
struct Preferences {
    std::wstring directory, format;
    unsigned intervalMinutes = 10;
};
Preferences GetPreferences();
bool ValidatePreferences(const Preferences &value, std::wstring &error);
bool SavePreferences(const Preferences &value, std::wstring &error);
void OnTimer();
// 锁屏跳过截图，解锁后按所选间隔重新对齐，不补拍错过的时间点。
void OnSessionChange(WPARAM event);
void OnCompletion(bool success);
// 先阻止新任务，再等待 IsBusy() 为 false，最后才可 Shutdown；不会强杀正在写图的线程。
void PrepareStop();
std::wstring StatusText();
// 菜单仅添加在宿主的 T 图标下，不注册额外截图托盘图标。
void AddMenu(HMENU parent);
bool HandleMenu(UINT command);
void Shutdown();
} // namespace CaptureTimer
