#pragma once
#include <windows.h>
#include <utility>

// 仅封装所有权，不创建线程、不维护全局缓存。借用的系统句柄不能交给这些对象。
namespace NativeHandles {
class Handle {
    HANDLE value = nullptr;

  public:
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() {
        Reset();
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value(other.Release()) {}
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other)
            Reset(other.Release());
        return *this;
    }
    Handle &operator=(HANDLE handle) {
        Reset(handle);
        return *this;
    }
    operator HANDLE() const {
        return value;
    }
    explicit operator bool() const {
        return value && value != INVALID_HANDLE_VALUE;
    }
    HANDLE Release() {
        return std::exchange(value, nullptr);
    }
    void Reset(HANDLE handle = nullptr) {
        if (value == handle)
            return;
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = handle;
    }
};

class ScreenDC {
    HDC value = GetDC(nullptr);

  public:
    ~ScreenDC() {
        if (value)
            ReleaseDC(nullptr, value);
    }
    ScreenDC() = default;
    ScreenDC(const ScreenDC &) = delete;
    ScreenDC &operator=(const ScreenDC &) = delete;
    operator HDC() const {
        return value;
    }
};
class MemoryDC {
    HDC value;

  public:
    explicit MemoryDC(HDC source) : value(source ? CreateCompatibleDC(source) : nullptr) {}
    ~MemoryDC() {
        if (value)
            DeleteDC(value);
    }
    MemoryDC(const MemoryDC &) = delete;
    MemoryDC &operator=(const MemoryDC &) = delete;
    operator HDC() const {
        return value;
    }
};
class GdiObject {
    HGDIOBJ value;

  public:
    explicit GdiObject(HGDIOBJ object) : value(object) {}
    ~GdiObject() {
        if (value)
            DeleteObject(value);
    }
    GdiObject(const GdiObject &) = delete;
    GdiObject &operator=(const GdiObject &) = delete;
    operator HGDIOBJ() const {
        return value;
    }
};
class Selection {
    HDC dc;
    HGDIOBJ previous;

  public:
    Selection(HDC dc, HGDIOBJ object) : dc(dc), previous(SelectObject(dc, object)) {}
    ~Selection() {
        if (previous && previous != HGDI_ERROR)
            SelectObject(dc, previous);
    }
    Selection(const Selection &) = delete;
    Selection &operator=(const Selection &) = delete;
    bool Valid() const {
        return previous && previous != HGDI_ERROR;
    }
};

class RemoteBuffer {
    HANDLE process; // 借用；声明顺序保证进程句柄晚于缓冲区释放。
    void *value;

  public:
    RemoteBuffer(HANDLE process, SIZE_T bytes)
        : process(process),
          value(VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
    }
    ~RemoteBuffer() {
        if (value)
            VirtualFreeEx(process, value, 0, MEM_RELEASE);
    }
    RemoteBuffer(const RemoteBuffer &) = delete;
    RemoteBuffer &operator=(const RemoteBuffer &) = delete;
    void *Get() const {
        return value;
    }
};

// 托盘记录需要按值复制：复制时 CopyIcon，移动时转交所有权，不共享裸 HICON。
class Icon {
    HICON value = nullptr;

  public:
    Icon() = default;
    Icon(HICON icon) : value(icon) {}
    ~Icon() {
        Reset();
    }
    Icon(const Icon &other) : value(other.value ? CopyIcon(other.value) : nullptr) {}
    Icon &operator=(const Icon &other) {
        if (this != &other)
            Reset(other.value ? CopyIcon(other.value) : nullptr);
        return *this;
    }
    Icon(Icon &&other) noexcept : value(other.Release()) {}
    Icon &operator=(Icon &&other) noexcept {
        if (this != &other)
            Reset(other.Release());
        return *this;
    }
    Icon &operator=(HICON icon) {
        Reset(icon);
        return *this;
    }
    operator HICON() const {
        return value;
    }
    HICON Release() {
        return std::exchange(value, nullptr);
    }
    void Reset(HICON icon = nullptr) {
        if (value == icon)
            return;
        if (value)
            DestroyIcon(value);
        value = icon;
    }
};
} // namespace NativeHandles
