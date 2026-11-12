#define NATIVE_TEST_BUILD
#include "../ScreenCaptureTimer.cpp"
using namespace CaptureTimer;

static int SelfTest(){bool ok=true;SYSTEMTIME t={};t.wYear=2026;t.wMonth=12;t.wDay=31;t.wHour=23;t.wMinute=59;t.wSecond=59;auto next=FromTicks(Next(Ticks(t)));ok&=next.wYear==2027&&next.wMonth==1&&next.wDay==1&&next.wHour==0&&next.wMinute==0;t.wHour=16;t.wMinute=10;t.wSecond=0;next=FromTicks(Next(Ticks(t)));ok&=next.wHour==16&&next.wMinute==20;t.wMinute=9;t.wSecond=59;t.wMilliseconds=999;next=FromTicks(Next(Ticks(t)));ok&=next.wHour==16&&next.wMinute==10;t.wMinute=10;t.wSecond=7;ok&=FormatName(L"HH-mm",t)==L"16-10";ok&=FormatName(L"HH-mm-ss",t)==L"16-10-07";ok&=FormatName(L"HH:mm",t).empty();ok&=JsonValue(L"{\"OutputDirectory\":\"C:\\\\图片\\\\days\"}",L"OutputDirectory")==L"C:\\图片\\days";
    std::wstring file=root+L"\\encoder-test.png";HANDLE h=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)ok=false;else{PngWriter writer(h);ok&=writer.Begin(31,17);std::vector<unsigned char> row(31*3);for(int y=0;y<17;y++){for(int x=0;x<31;x++){row[x*3]=(x*3+y)%256;row[x*3+1]=(y*11)%256;row[x*3+2]=(x*7)%256;}ok&=writer.Row(row.data(),31);}ok&=writer.Finish();CloseHandle(h);}TextFile(root+L"\\native-self-test.txt",ok?L"PASS: boundary rollover, 24-hour naming, custom naming, invalid-name rejection, Unicode JSON, and streaming PNG generation.\n":L"FAIL\n");return ok?0:1;}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int) {
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);root=exe;
    root.resize(root.find_last_of(L'\\'));configPath=root+L"\\config.json";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return SelfTest();
}
