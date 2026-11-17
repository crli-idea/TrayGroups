#define NATIVE_TEST_BUILD
#include "../src/app/TrayGroups.cpp"
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 baseDir=L"C:\\work\\TrayGroups";recoveryPath=baseDir+L"\\restore-icons.tsv";
 RecoverIcons();return GetFileAttributesW(recoveryPath.c_str())==INVALID_FILE_ATTRIBUTES?0:1;
}
