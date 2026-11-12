#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 std::wstring report;
 for(const wchar_t* name:{L"bthprops.cpl",L"bthudtask.exe",L"BluetoothApis.dll",L"sndvolsso.dll"}){
  std::wstring path=L"C:\\Windows\\System32\\"+std::wstring(name);
  HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)continue;
  DWORD size=GetFileSize(file,nullptr),read=0;std::vector<unsigned char> bytes(size);ReadFile(file,bytes.data(),size,&read,nullptr);CloseHandle(file);
  auto dos=(IMAGE_DOS_HEADER*)bytes.data();auto nt=(IMAGE_NT_HEADERS64*)(bytes.data()+dos->e_lfanew);auto sections=IMAGE_FIRST_SECTION(nt);
  for(unsigned n=0;n<nt->FileHeader.NumberOfSections;n++){
   auto section=sections[n];if(memcmp(section.Name,".rdata",6)&&memcmp(section.Name,".data",5))continue;
   for(size_t offset=section.PointerToRawData;offset+16<=section.PointerToRawData+section.SizeOfRawData;offset+=4){
    GUID guid;memcpy(&guid,bytes.data()+offset,16);if(!guid.Data1||!guid.Data2||!guid.Data3)continue;
    NOTIFYICONIDENTIFIER id={};id.cbSize=sizeof(id);id.guidItem=guid;RECT rect={};HRESULT hr=Shell_NotifyIconGetRect(&id,&rect);
    if(SUCCEEDED(hr))report+=path+L" guid="+GuidKey(guid)+L" hr="+Number(hr)+L" rect="+std::to_wstring(rect.left)+L","+std::to_wstring(rect.top)+L"\n";
   }
  }
 }
 WriteText(L"C:\\work\\TrayGroups\\test\\system-guids.txt",report);return 0;
}