#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
 auto current=ReadTray();std::wstring report;
 for(const auto& i:current) report+=i.process+L"\t"+i.tip+L"\tclass="+ClassOf(i.owner)+L"\tcallback="+Number(i.callback)+L"\tid="+Number(i.id)+L"\ttoolbar="+Number((ULONG_PTR)i.toolbar)+L"\tparent="+ClassOf(GetParent(i.toolbar))+L"\tcommand="+Number(i.command)+L"\thidden="+Number((i.state&TBSTATE_HIDDEN)!=0)+L"\tgroup="+i.group+L"\tkey="+i.key+L"\n";
 std::wstring path=ExePath();path.resize(path.find_last_of(L'\\'));WriteText(path+L"\\inventory.tsv",report);FreeItems(current);return 0;
}
