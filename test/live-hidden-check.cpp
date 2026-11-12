#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
#include <cstdio>
static void CaptureTaskbar(const std::wstring& path) {
    HWND tray=FindWindowW(L"Shell_TrayWnd",nullptr);RECT area;if(!tray||!GetWindowRect(tray,&area))return;
    int width=std::min(1000,(int)(area.right-area.left)),height=area.bottom-area.top;
    if(height<=0||height>300)return;
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);BITMAPINFO info={};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(mem,bitmap);
    BitBlt(mem,0,0,width,height,screen,area.right-width,area.top,SRCCOPY);
    BITMAPFILEHEADER header={};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);DWORD written=0;
    if(file!=INVALID_HANDLE_VALUE){WriteFile(file,&header,sizeof(header),&written,nullptr);WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr);WriteFile(file,pixels,width*height*4,&written,nullptr);CloseHandle(file);}
    SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(nullptr,screen);
}

static void CapturePopup() {
    HWND host=FindWindowW(AppClass,nullptr);if(!host)return;
    PostMessageW(host,WM_APP+8,1,1);Sleep(200);
    HWND popup=FindWindowW(PopupClass,nullptr);if(!popup)return;
    RECT bounds;GetWindowRect(popup,&bounds);int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);BITMAPINFO info={};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(mem,bitmap);
    PrintWindow(popup,mem,0);
    BITMAPFILEHEADER header={};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
    HANDLE file=CreateFileW(L"C:\\work\\TrayGroups\\test\\live-T-popup.bmp",GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,0,nullptr);DWORD written=0;
    if(file!=INVALID_HANDLE_VALUE){WriteFile(file,&header,sizeof(header),&written,nullptr);WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr);WriteFile(file,pixels,width*height*4,&written,nullptr);CloseHandle(file);}
    SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(nullptr,screen);
    PostMessageW(popup,WM_CLOSE,0,0);
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR argument,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(Trim(argument?argument:L"")==L"popup"){CapturePopup();return 0;}
    baseDir=L"C:\\work\\TrayGroups";configPath=baseDir+L"\\groups.ini";LoadSettings();
    auto icons=ReadTray();std::wstring report=L"fold="+Number(foldIcons)+L"\n";
    for(const auto& item:icons)report+=item.process+L"\t"+FriendlyIconName(item)+L"\tgroup="+item.group+L"\thidden="+Number((item.state&TBSTATE_HIDDEN)!=0)+L"\teligible="+Number(PopupEligible(item))+L"\towner="+Number((ULONG_PTR)item.owner)+L"\tid="+Number(item.id)+L"\tmatch="+item.matchStatus+L"\n";
    FreeItems(icons);
    std::wstring stage=Trim(argument?argument:L"");if(stage!=L"before"&&stage!=L"after"&&stage!=L"restored")stage=L"after";
    WriteText(baseDir+L"\\test\\live-"+stage+L".tsv",report);
    CaptureTaskbar(baseDir+L"\\test\\live-taskbar-"+stage+L".bmp");
    return 0;
}
