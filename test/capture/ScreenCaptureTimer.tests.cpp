#define NATIVE_TEST_BUILD
#include "../../src/capture/ScreenCaptureTimer.cpp"
using namespace CaptureTimer;

static bool IntervalTests() {
    bool ok=true;
    SYSTEMTIME t={};t.wYear=2026;t.wMonth=11;t.wDay=18;t.wHour=13;t.wMinute=8;t.wSecond=23;
    settings.intervalMinutes=15;auto next=FromTicks(Next(Ticks(t)));ok&=next.wHour==13&&next.wMinute==15&&next.wSecond==0;
    t.wMinute=15;t.wSecond=0;next=FromTicks(Next(Ticks(t)));ok&=next.wMinute==30;
    settings.intervalMinutes=90;t.wHour=13;t.wMinute=35;next=FromTicks(Next(Ticks(t)));ok&=next.wHour==15&&next.wMinute==0;
    settings.intervalMinutes=7;t.wHour=23;t.wMinute=59;next=FromTicks(Next(Ticks(t)));ok&=next.wDay==19&&next.wHour==0&&next.wMinute==0;
    settings.intervalMinutes=1440;t.wHour=0;t.wMinute=0;next=FromTicks(Next(Ticks(t)));ok&=next.wDay==19&&next.wHour==0;
    settings.intervalMinutes=1;t.wHour=23;t.wMinute=59;t.wMilliseconds=999;next=FromTicks(Next(Ticks(t)));ok&=next.wDay==19&&next.wMinute==0;
    ok&=!ValidInterval(0)&&!ValidInterval(1441)&&ValidInterval(1)&&ValidInterval(1440);
    settings.directory=root+L"\\测试";settings.format=L"HH-mm-ss";settings.intervalMinutes=15;
    ok&=Save(settings);settings.intervalMinutes=60;Load();ok&=settings.intervalMinutes==15&&settings.format==L"HH-mm-ss";
    TextFile(configPath,L"{\"FileNameFormat\":\"HH-mm\"}");Load();ok&=settings.intervalMinutes==10;
    for(const auto* invalid:{L"0",L"1441",L"-1",L"1.5",L"\"15\"",L"15junk",L"999999999999999999999999"}) {
        TextFile(configPath,L"{\"IntervalMinutes\":"+std::wstring(invalid)+L"}");Load();ok&=settings.intervalMinutes==10;
    }
    TextFile(configPath,L"{\"IntervalMinutes\": 60 }");Load();ok&=settings.intervalMinutes==60;
    settings.intervalMinutes=0;ok&=!Save(settings);
    settings.intervalMinutes=10;
    return ok;
}

static int SelfTest(){bool ok=IntervalTests();SYSTEMTIME t={};t.wYear=2026;t.wMonth=12;t.wDay=31;t.wHour=23;t.wMinute=59;t.wSecond=59;auto next=FromTicks(Next(Ticks(t)));ok&=next.wYear==2027&&next.wMonth==1&&next.wDay==1&&next.wHour==0&&next.wMinute==0;t.wHour=16;t.wMinute=10;t.wSecond=0;next=FromTicks(Next(Ticks(t)));ok&=next.wHour==16&&next.wMinute==20;t.wMinute=9;t.wSecond=59;t.wMilliseconds=999;next=FromTicks(Next(Ticks(t)));ok&=next.wHour==16&&next.wMinute==10;t.wMinute=10;t.wSecond=7;ok&=FormatName(L"HH-mm",t)==L"16-10";ok&=FormatName(L"HH-mm-ss",t)==L"16-10-07";ok&=FormatName(L"HH:mm",t).empty();ok&=JsonValue(L"{\"OutputDirectory\":\"C:\\\\图片\\\\days\"}",L"OutputDirectory")==L"C:\\图片\\days";
    std::wstring file=root+L"\\encoder-test.png";HANDLE h=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)ok=false;else{PngWriter writer(h);ok&=writer.Begin(31,17);std::vector<unsigned char> row(31*3);for(int y=0;y<17;y++){for(int x=0;x<31;x++){row[x*3]=(x*3+y)%256;row[x*3+1]=(y*11)%256;row[x*3+2]=(x*7)%256;}ok&=writer.Row(row.data(),31);}ok&=writer.Finish();CloseHandle(h);}TextFile(root+L"\\native-self-test.txt",ok?L"PASS: configurable intervals, midnight alignment, config validation, boundary rollover, 24-hour naming, custom naming, invalid-name rejection, Unicode JSON, and streaming PNG generation.\n":L"FAIL\n");return ok?0:1;}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR command,int) {
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);root=exe;
    root.resize(root.find_last_of(L'\\'));configPath=root+L"\\config.json";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return SelfTest();
}
