#pragma once
#include <windows.h>
#include <string>

// 公共 UTF-8、路径及小型文本文件操作。这里只提供工具函数，不保存应用状态。
namespace TraySupport {
inline std::wstring Number(ULONG_PTR n) {
    return std::to_wstring((unsigned long long)n);
}
inline std::wstring ExePath() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    return std::wstring(path, n);
}
inline std::wstring Lower(std::wstring s) {
    if (!s.empty())
        CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}
inline std::wstring Trim(const std::wstring &s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos)
        return L"";
    return s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
}
inline std::string Utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}
inline std::wstring Wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n);
    return r;
}
inline bool WriteText(const std::wstring &path, const std::wstring &text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    std::string b = Utf8(text);
    DWORD n = 0;
    bool ok = WriteFile(h, b.data(), (DWORD)b.size(), &n, nullptr) && n == b.size();
    CloseHandle(h);
    return ok;
}
inline std::wstring ReadText(const std::wstring &path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return L"";
    DWORD n = GetFileSize(h, nullptr);
    if (n > 1024 * 1024) {
        CloseHandle(h);
        return L"";
    }
    std::string b(n, 0);
    DWORD got = 0;
    ReadFile(h, b.empty() ? nullptr : &b[0], n, &got, nullptr);
    CloseHandle(h);
    b.resize(got);
    return Wide(b);
}
inline std::wstring Escape(std::wstring s) {
    for (auto &c : s)
        if (c == L'\t' || c == L'\r' || c == L'\n')
            c = L' ';
    return s;
}
inline std::wstring FileName(const std::wstring &path) {
    size_t p = path.find_last_of(L"\\/");
    return Lower(path.substr(p == std::wstring::npos ? 0 : p + 1));
}

} // namespace TraySupport
