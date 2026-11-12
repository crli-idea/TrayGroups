#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 auto current=ReadTray();std::wstring report;
 for(const auto& i:current)if(Lower(i.process)==L"explorer.exe"){
  std::wstring cls=ClassOf(i.owner),mapped;DWORD pid=0;GetWindowThreadProcessId(i.owner,&pid);
  if(Lower(cls).find(L"atl:")==0){ULONG_PTR address=wcstoull(cls.c_str()+4,nullptr,16);HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,pid);wchar_t file[32768];if(process){DWORD count=GetMappedFileNameW(process,(void*)address,file,32768);if(count)mapped.assign(file,count);CloseHandle(process);}}
  report+=i.tip+L"\t"+cls+L"\t"+mapped+L"\tid="+Number(i.id)+L"\n";
 }
 std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));WriteText(path+L"\\host-identities.tsv",report);FreeItems(current);return 0;
}