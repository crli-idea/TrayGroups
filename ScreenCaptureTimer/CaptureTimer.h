#pragma once
#include <windows.h>
#include <string>

// Public host interface. Capture state and encoding stay in the implementation.
namespace CaptureTimer {
inline constexpr UINT CaptureNowCommand=2101,PauseCommand=2102,SettingsCommand=2103,ResourcesCommand=2104,FolderCommand=2105;
void Initialize(HWND owner,HINSTANCE instance,const std::wstring& directory);
UINT CompletionMessage();
UINT TimerId();
bool IsBusy();
void OnTimer();
void OnSessionChange(WPARAM event);
void OnCompletion(bool success);
void PrepareStop();
std::wstring StatusText();
void AddMenu(HMENU parent);
bool HandleMenu(UINT command);
void Shutdown();
}
