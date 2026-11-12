#define NATIVE_TEST_BUILD
#include "../TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 auto current=ReadTray();std::wstring out;
 for(const auto& item:current){
  RECT toolbarRect={};GetWindowRect(item.toolbar,&toolbarRect);DWORD pid=0;GetWindowThreadProcessId(item.toolbar,&pid);
  HANDLE process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ,FALSE,pid);if(!process)continue;
  void* memory=VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD_PTR index=0,ok=0;RECT button={};
  if(memory&&Send(item.toolbar,TB_COMMANDTOINDEX,item.command,0,index)&&index!=(DWORD_PTR)-1&&Send(item.toolbar,TB_GETITEMRECT,index,(LPARAM)memory,ok)&&ok)ReadMemory(process,(ULONG_PTR)memory,button);
  if(memory)VirtualFreeEx(process,memory,0,MEM_RELEASE);CloseHandle(process);
  out+=item.process+L"\t"+FriendlyIconName(item)+L"\t"+ClassOf(GetParent(item.toolbar))+L"\thidden="+Number((item.state&TBSTATE_HIDDEN)!=0)+L"\tindex="+Number(index)+L"\tcommand="+Number(item.command)+L"\ttoolbar="+Number(toolbarRect.left)+L","+Number(toolbarRect.top)+L"\tbutton="+Number(button.left)+L","+Number(button.top)+L","+Number(button.right)+L","+Number(button.bottom)+L"\n";
 }
 FreeItems(current);std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));WriteText(path+L"\\toolbar-geometry.tsv",out);return 0;
}