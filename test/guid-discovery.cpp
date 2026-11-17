#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
#include <cstdio>
static std::wstring GuidText(const GUID& g){wchar_t text[64];swprintf_s(text,L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return text;}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 auto all=ReadTray();std::wstring report;
 for(const auto& item:all)if(Lower(item.process)==L"securityhealthsystray.exe"){
  report+=L"path="+item.path+L" class="+item.ownerClass+L" usesGuid="+Number(item.usesGuid)+L"\n";
  NOTIFYICONDATAW test={};test.cbSize=sizeof(test);test.hWnd=item.owner;test.uID=item.id;test.uFlags=NIF_STATE|NIF_GUID;
  test.dwStateMask=NIS_HIDDEN;test.dwState=0;test.guidItem={0xba82e2dc,0xf405,0x47ad,{0xb0,0x32,0xcf,0x0f,0xaa,0x0e,0x39,0x33}};
  SetLastError(0);BOOL modified=Shell_NotifyIconW(NIM_MODIFY,&test);report+=L"GUID same-state="+Number(modified)+L" err="+Number(GetLastError())+L"\n";
  DWORD pid=0;GetWindowThreadProcessId(item.toolbar,&pid);HANDLE process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ,FALSE,pid);
  if(!process)continue;void* mem=VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  DWORD_PTR index=0,result=0;TBBUTTON button={};
  if(mem&&Send(item.toolbar,TB_COMMANDTOINDEX,item.command,0,index)&&Send(item.toolbar,TB_GETBUTTON,index,(LPARAM)mem,result)&&ReadMemory(process,(ULONG_PTR)mem,button)){
   unsigned char bytes[256]={};SIZE_T got=0;ReadProcessMemory(process,(void*)button.dwData,bytes,sizeof(bytes),&got);
   for(size_t n=0;n<got;n+=8){wchar_t line[80];swprintf_s(line,L"%03llu: %016llx\n",(unsigned long long)n,*(unsigned long long*)(bytes+n));report+=line;}
   for(size_t n=0;n+16<=got;n+=4){
    GUID guid;memcpy(&guid,bytes+n,16);NOTIFYICONIDENTIFIER id={};id.cbSize=sizeof(id);id.guidItem=guid;RECT rect={};
    HRESULT hr=Shell_NotifyIconGetRect(&id,&rect);
    if(SUCCEEDED(hr))report+=L"GUID match offset="+Number(n)+L" "+GuidText(guid)+L" rect="+std::to_wstring(rect.left)+L","+std::to_wstring(rect.top)+L","+std::to_wstring(rect.right)+L","+std::to_wstring(rect.bottom)+L"\n";
   }
  }
  if(mem)VirtualFreeEx(process,mem,0,MEM_RELEASE);CloseHandle(process);
 }

 FreeItems(all);WriteText(L"C:\\work\\TrayGroups\\test\\guid-discovery.txt",report);return 0;
}
