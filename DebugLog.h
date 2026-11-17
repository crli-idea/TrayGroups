#pragma once
#include <windows.h>
#include <atomic>
#include <string>

// Bounded, thread-safe UTF-8 diagnostics beside the portable executable.
namespace DebugLog {
inline std::atomic<bool> enabled{true};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::wstring path;
inline void Initialize(const std::wstring& directory){path=directory+L"\\TrayGroups.debug.log";}
inline void Write(const std::wstring& message){
    if(!enabled.load()||path.empty())return;
    DWORD savedError=GetLastError();AcquireSRWLockExclusive(&lock);
    WIN32_FILE_ATTRIBUTE_DATA data={};
    if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)&&(data.nFileSizeHigh||data.nFileSizeLow>=1024*1024))
        MoveFileExW(path.c_str(),(path+L".1").c_str(),MOVEFILE_REPLACE_EXISTING);
    HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){
        SYSTEMTIME now;GetLocalTime(&now);wchar_t prefix[100];
        swprintf_s(prefix,L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId(),GetCurrentThreadId());
        std::wstring line=prefix+message+L"\r\n";
        int size=WideCharToMultiByte(CP_UTF8,0,line.data(),(int)line.size(),nullptr,0,nullptr,nullptr);
        std::string bytes(size,0);if(size)WideCharToMultiByte(CP_UTF8,0,line.data(),(int)line.size(),&bytes[0],size,nullptr,nullptr);
        DWORD written=0;WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr);CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);SetLastError(savedError);
}
}
