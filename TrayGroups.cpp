#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include "NativeResources.h"

static const wchar_t* AppClass=L"Crli.TrayGroups.Native";
static const wchar_t* PopupClass=L"Crli.TrayGroups.Popup";
static const UINT TrayCallback=WM_APP+1;
static const UINT StopApp=WM_APP+3;
static const UINT RefreshApp=WM_APP+4;
static const int ResourceTimer=2, RefreshTimer=3, SingleTimer=4, ReFoldTimer=5;
static HWND mainWindow=nullptr, popupWindow=nullptr, settingsWindow=nullptr, resourceWindow=nullptr;
static HWND listWindow=nullptr, groupEdit=nullptr, foldCheck=nullptr, resourceText=nullptr;
static HWND scopeEdit=nullptr, matchEdit=nullptr, labelSizeEdit=nullptr, labelWidthEdit=nullptr, labelHeightEdit=nullptr;
static bool labelEditing=false, showingGroups=false;
static HICON appIcon=nullptr;
static std::wstring iconMatch=L"path-class-id";
static int defaultLabelSize=12, settingsX=0, settingsY=0;
static bool haveSettingsPosition=false;
static std::map<std::wstring,int> groupLabelSizes,groupLabelWidths,groupLabelHeights;
static std::map<std::wstring,std::wstring> pathAssignments;
static std::vector<std::wstring> popupGroups;
static std::vector<RECT> groupRects;
static HIMAGELIST settingsImages=nullptr;
static bool groupEditing=false, fillingSettings=false;
static HFONT appFont=nullptr;
static std::wstring baseDir, configPath, recoveryPath;
static bool foldIcons=true, shuttingDown=false, refreshing=false;
static bool stopAfterCapture=false;
static UINT taskbarCreated;
static std::wstring activeGroup;
static int pendingClick=-1, reFoldAttempts=0;
static unsigned notificationCount=0;
static unsigned forwardingFailures=0;
static std::map<UINT,std::wstring> notificationGroups;
static std::map<UINT,HICON> notificationIcons;
static std::map<std::wstring,std::wstring> assignments;
static std::map<std::wstring,std::wstring> programAssignments;
static std::map<std::wstring,std::wstring> processPaths;
static std::map<DWORD,uint64_t> processStarts;
static std::wstring latestError;
#ifdef NATIVE_TEST_BUILD
static bool testingOwnIcons=false;
static std::map<std::pair<HWND,UINT>,GUID> testIconGuids;
#endif

struct RuntimeIdentity {
    DWORD pid=0;uint64_t started=0;HWND owner=nullptr;UINT id=0;
    bool operator==(const RuntimeIdentity& other)const { return pid==other.pid&&started==other.started&&owner==other.owner&&id==other.id; }
    bool Valid()const { return pid&&started&&owner; }
};
static std::map<std::wstring,RuntimeIdentity> ruleBindings;
static std::set<std::wstring> pendingRules;
struct TrayItem {
    HWND toolbar=nullptr, owner=nullptr;
    UINT id=0, callback=0, version=0;
    int command=0;
    BYTE state=0;
    std::wstring process,path,tip,key,group,ownerClass;
    HICON image=nullptr;
    bool fallbackImage=false;
    RuntimeIdentity runtime;
    GUID guid={};bool usesGuid=false;
    bool pending=false;
    std::wstring matchStatus;
};
struct Hidden { HWND toolbar, owner; UINT id; int command; RuntimeIdentity runtime; };
static std::vector<TrayItem> items;
static std::vector<Hidden> hidden;
static std::vector<int> popupItems;
static std::vector<TrayItem> settingsItems;

static std::wstring Number(ULONG_PTR n) { return std::to_wstring((unsigned long long)n); }
static std::wstring ExePath() { wchar_t path[32768]; DWORD n=GetModuleFileNameW(nullptr,path,32768); return std::wstring(path,n); }
static std::wstring Lower(std::wstring s) { if(!s.empty())CharLowerBuffW(&s[0],(DWORD)s.size());return s; }
static std::wstring Trim(const std::wstring& s) { size_t a=s.find_first_not_of(L" \t\r\n");if(a==std::wstring::npos)return L"";return s.substr(a,s.find_last_not_of(L" \t\r\n")-a+1); }
static std::string Utf8(const std::wstring& s) { int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string r(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),&r[0],n,nullptr,nullptr);return r; }
static std::wstring Wide(const std::string& s) { int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring r(n,0);if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&r[0],n);return r; }
static bool WriteText(const std::wstring& path,const std::wstring& text) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;std::string b=Utf8(text);DWORD n=0;bool ok=WriteFile(h,b.data(),(DWORD)b.size(),&n,nullptr)&&n==b.size();CloseHandle(h);return ok;
}
static std::wstring ReadText(const std::wstring& path) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return L"";DWORD n=GetFileSize(h,nullptr);if(n>1024*1024){CloseHandle(h);return L"";}std::string b(n,0);DWORD got=0;ReadFile(h,b.empty()?nullptr:&b[0],n,&got,nullptr);CloseHandle(h);b.resize(got);return Wide(b);
}
static std::wstring Escape(std::wstring s) { for(auto& c:s)if(c==L'\t'||c==L'\r'||c==L'\n')c=L' ';return s; }
static void LogError(const std::wstring& s) { latestError=s;OutputDebugStringW((L"TrayGroups: "+s+L"\n").c_str()); }

static int LabelSize(int value) { return std::max(8,std::min(32,value)); }
static int GroupLabelSize(const std::wstring& name) { auto i=groupLabelSizes.find(name);return i==groupLabelSizes.end()?defaultLabelSize:i->second; }
static int GroupLabelWidth(const std::wstring& group) { auto i=groupLabelWidths.find(group);return i==groupLabelWidths.end()?280:i->second; }
static int GroupLabelHeight(const std::wstring& group) { auto i=groupLabelHeights.find(group);return i==groupLabelHeights.end()?0:i->second; }
static bool ValidMatch(const std::wstring& value) { return value==L"path-guid"||value==L"path-class-id"||value==L"path-id"||value==L"path-class-tip"; }
static void LoadSettings() {
    assignments.clear();programAssignments.clear();pathAssignments.clear();groupLabelSizes.clear();groupLabelWidths.clear();groupLabelHeights.clear();
    iconMatch=L"path-class-id";defaultLabelSize=12;foldIcons=true;haveSettingsPosition=false;
    std::wstring text=ReadText(configPath),section;size_t start=0;bool hasX=false,hasY=false;
    if(!text.empty()&&text[0]==0xfeff)text.erase(0,1);
    while(start<text.size()){
        size_t end=text.find(L'\n',start);if(end==std::wstring::npos)end=text.size();
        std::wstring line=Trim(text.substr(start,end-start));start=end+1;
        if(line.empty()||line[0]==L';'||line[0]==L'#')continue;
        if(line.front()==L'['&&line.back()==L']'){section=Lower(line.substr(1,line.size()-2));continue;}
        size_t pos=line.find(L'=');if(pos==std::wstring::npos)continue;
        std::wstring key=Trim(line.substr(0,pos)),val=Trim(line.substr(pos+1)),low=Lower(key);
        if(section==L"settings"){
            if(low==L"foldoriginalicons")foldIcons=val==L"1";
            else if(low==L"iconmatch"&&ValidMatch(val))iconMatch=val;
            else if(low==L"defaultgrouplabelsize")defaultLabelSize=LabelSize(_wtoi(val.c_str()));
        }else if(section==L"programs")programAssignments[low]=val;
        else if(section==L"paths")pathAssignments[low]=val;
        else if(section==L"icons")assignments[low]=val; // Empty means explicitly ungrouped.
        else if(section==L"grouplabelsizes")groupLabelSizes[key]=LabelSize(_wtoi(val.c_str()));
        else if(section==L"grouplabelwidths")groupLabelWidths[key]=std::max(120,std::min(600,_wtoi(val.c_str())));
        else if(section==L"grouplabelheights"){int h=_wtoi(val.c_str());groupLabelHeights[key]=h==0?0:std::max(32,std::min(160,h));}
        else if(section==L"window"){
            if(low==L"left"){settingsX=_wtoi(val.c_str());hasX=true;}
            else if(low==L"top"){settingsY=_wtoi(val.c_str());hasY=true;}
        }
    }
    haveSettingsPosition=hasX&&hasY;
}
static std::wstring WindowSection() { return L"[Window]\nLeft="+std::to_wstring(settingsX)+L"\nTop="+std::to_wstring(settingsY)+L"\n"; }
static bool AtomicConfig(const std::wstring& text) {
    std::wstring tmp=configPath+L".tmp";
    return WriteText(tmp,text)&&MoveFileExW(tmp.c_str(),configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
static bool SaveSettings() {
    std::wstring text=L"; TrayGroups UTF-8. Right-click T -> Reload configuration after manual edits.\n[Settings]\nFoldOriginalIcons="+std::wstring(foldIcons?L"1":L"0")+L"\n"
        L"; Per-icon identity modes: path-class-id (default), path-guid, path-id, path-class-tip.\nIconMatch="+iconMatch+
        L"\nDefaultGroupLabelSize="+std::to_wstring(defaultLabelSize)+L"\n\n[Programs]\n; Executable filename: all matching icons (explicit broad rules).\n";
    for(const auto& i:programAssignments)text+=Escape(i.first)+L"="+Escape(i.second)+L"\n";
    text+=L"\n[Paths]\n; Full executable path: all icons belonging to this exact path.\n";
    for(const auto& i:pathAssignments)text+=Escape(i.first)+L"="+Escape(i.second)+L"\n";
    text+=L"\n[Icons]\n; Per-icon overrides. Empty value means explicitly ungrouped.\n";
    for(const auto& i:assignments)text+=Escape(i.first)+L"="+Escape(i.second)+L"\n";
    text+=L"\n[GroupLabelSizes]\n; Group name = font size in points (8..32). Ungrouped uses the empty key.\n";
    for(const auto& i:groupLabelSizes)text+=Escape(i.first)+L"="+std::to_wstring(i.second)+L"\n";
    text+=L"\n[GroupLabelWidths]\n; Width in logical pixels (120..600).\n";
    for(const auto& i:groupLabelWidths)text+=Escape(i.first)+L"="+std::to_wstring(i.second)+L"\n";
    text+=L"\n[GroupLabelHeights]\n; Minimum height in logical pixels (32..160), 0 = automatic.\n";
    for(const auto& i:groupLabelHeights)text+=Escape(i.first)+L"="+std::to_wstring(i.second)+L"\n";
    if(haveSettingsPosition)text+=L"\n"+WindowSection();
    return AtomicConfig(text);
}
static void RememberSettingsPosition(HWND h) {
    if(IsIconic(h))return;RECT r;if(!GetWindowRect(h,&r))return;
    settingsX=r.left;settingsY=r.top;haveSettingsPosition=true;
    // Only merge the Window section: closing must not save unsaved group edits.
    std::wstring original=ReadText(configPath),text;size_t start=0;bool skip=false;
    while(start<original.size()){
        size_t end=original.find(L'\n',start);if(end==std::wstring::npos)end=original.size();
        std::wstring line=original.substr(start,end-start),trimmed=Trim(line);start=end+1;
        if(!trimmed.empty()&&trimmed.front()==L'['&&trimmed.back()==L']')skip=Lower(trimmed)==L"[window]";
        if(!skip)text+=line+L"\n";
    }
    size_t last=text.find_last_not_of(L" \t\r\n");text=last==std::wstring::npos?L"":text.substr(0,last+1);
    if(!AtomicConfig(text+L"\n\n"+WindowSection()))LogError(L"无法保存设置窗口位置");
}

static std::wstring ClassOf(HWND w) { wchar_t c[128]={};GetClassNameW(w,c,128);return c; }

static std::wstring OwnerClass(HWND owner,DWORD pid) {
    std::wstring cls=ClassOf(owner);
    if(Lower(cls).find(L"atl:")==0){
        ULONG_PTR address=wcstoull(cls.c_str()+4,nullptr,16);
        HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,pid);
        if(process){
            wchar_t file[32768];DWORD n=GetMappedFileNameW(process,(void*)address,file,32768);CloseHandle(process);
            if(n){std::wstring path(file,n);size_t slash=path.find_last_of(L"\\/");return L"atl@"+Lower(path.substr(slash==std::wstring::npos?0:slash+1));}
        }
        return L"atl";
    }
    return cls;
}
static bool Send(HWND h,UINT msg,WPARAM w,LPARAM l,DWORD_PTR& result) { return SendMessageTimeoutW(h,msg,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,300,&result)!=0; }
template<class T> static bool ReadMemory(HANDLE p,ULONG_PTR a,T& v) { SIZE_T n=0;return ReadProcessMemory(p,(void*)a,&v,sizeof(v),&n)&&n==sizeof(v); }
struct RemoteTrayData { uint64_t owner; uint32_t id,callback,reserved,version; uint64_t icon; };
static_assert(sizeof(RemoteTrayData)==32,"Unexpected Explorer tray layout");
static uint64_t ProcessStart(HANDLE process) {
    FILETIME created,exit,kernel,user;if(!GetProcessTimes(process,&created,&exit,&kernel,&user))return 0;
    return ((uint64_t)created.dwHighDateTime<<32)|created.dwLowDateTime;
}
static std::wstring ProcessPath(DWORD pid) {
    std::wstring cacheKey=Number(pid);auto found=processPaths.find(cacheKey);if(found!=processPaths.end())return found->second;
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768];DWORD n=32768;std::wstring s;
    if(p){processStarts[pid]=ProcessStart(p);if(QueryFullProcessImageNameW(p,0,path,&n))s.assign(path,n);CloseHandle(p);}processPaths[cacheKey]=s;return s;
}

static std::wstring FileName(const std::wstring& path) { size_t p=path.find_last_of(L"\\/");return Lower(path.substr(p==std::wstring::npos?0:p+1)); }
static std::wstring StableClass(std::wstring name) {
    name=Lower(name);
    // ATL/WinForms include process-specific addresses; never persist those.
    if(name.find(L"atl:")==0)name=L"atl";
    size_t p=name.find(L".app.");if(name.find(L"windowsforms")==0&&p!=std::wstring::npos)name.resize(p);
    return name;
}
static std::wstring KeyPart(const std::wstring& text) {
    std::wstring out;const wchar_t* hex=L"0123456789abcdef";
    for(wchar_t c:Lower(text))if(c==L'%'||c==L'|'||c==L'='||c==L'\r'||c==L'\n'||c==L'['||c==L']'){
        out+=L'%';out+=hex[(c>>4)&15];out+=hex[c&15];
    }else out+=c;
    return out;
}
static std::wstring GuidKey(const GUID& guid) {
    wchar_t text[40];swprintf_s(text,L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        guid.Data1,guid.Data2,guid.Data3,guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    return text;
}
static std::wstring IconKey(const TrayItem& i,const std::wstring& mode) {
    if(i.path.empty())return L""; // A PID is not a durable identity.
    std::wstring path=KeyPart(i.path);
    if(mode==L"path-guid"){
        GUID empty={};
        if(i.usesGuid&&memcmp(&i.guid,&empty,sizeof(empty)))return mode+L"|"+path+L"|"+GuidKey(i.guid);
        return IconKey(i,L"path-class-id");
    }
    if(mode==L"path-id")return mode+L"|"+path+L"|"+Number(i.id);
    return mode+L"|"+path+L"|"+KeyPart(StableClass(i.ownerClass))+L"|"+(mode==L"path-class-tip"?KeyPart(i.tip):Number(i.id));
}
static std::wstring LegacyKey(const TrayItem& i) { return Lower(i.path)+L"|"+Number(i.id); }
static std::vector<std::wstring> IconKeys(const TrayItem& i) {
    std::vector<std::wstring> keys;
    for(const auto& mode:{iconMatch,std::wstring(L"path-guid"),std::wstring(L"path-class-id"),std::wstring(L"path-id"),std::wstring(L"path-class-tip")}){
        auto key=IconKey(i,mode);if(!key.empty()&&std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);
    }
    if(!i.path.empty())keys.push_back(LegacyKey(i));return keys;
}

struct MatchResult { std::wstring group,key,status;bool pending=false; };
static MatchResult FindGroup(const TrayItem& item,const std::map<std::wstring,unsigned>* counts=nullptr) {
    MatchResult result;bool found=false;
    // All saved per-icon selectors must agree. A collision blocks broad fallback.
    for(const auto& key:IconKeys(item)){
        auto rule=assignments.find(key);if(rule==assignments.end())continue;
        auto count=counts?counts->find(key):std::map<std::wstring,unsigned>::const_iterator{};
        if(pendingRules.count(key)||(counts&&count!=counts->end()&&count->second!=1)){
            result.pending=true;result.status=L"待确认：多个图标符合规则";return result;
        }
        if(found&&result.group!=rule->second){
            result.pending=true;result.status=L"待确认：分组规则不一致";return result;
        }
        auto binding=ruleBindings.find(key);
        if(item.runtime.Valid()&&binding!=ruleBindings.end()&&!(binding->second==item.runtime)){
            result.pending=true;result.status=L"待确认：图标已重新创建";return result;
        }
        result.group=rule->second;result.key=key;found=true;
    }
    if(found){result.status=result.group.empty()?L"已单独取消分组":L"单图标规则匹配";return result;}
    auto path=pathAssignments.find(Lower(item.path));
    if(path!=pathAssignments.end()){result.group=path->second;result.status=L"按完整路径分组";return result;}
    auto program=programAssignments.find(Lower(item.process));
    if(program!=programAssignments.end()){result.group=program->second;result.status=L"按程序名分组";return result;}
    result.status=L"未分组";return result;
}
static std::wstring GroupFor(const TrayItem& item,const std::map<std::wstring,unsigned>* counts=nullptr) {
    auto match=FindGroup(item,counts);return match.pending?L"":match.group;
}
static void ResolveGroups(std::vector<TrayItem>& list) {
    std::map<std::wstring,unsigned> counts;
    for(const auto& item:list)for(const auto& key:IconKeys(item))counts[key]++;
    for(const auto& count:counts)if(count.second>1&&assignments.count(count.first))pendingRules.insert(count.first);
    for(auto& item:list){
        item.key=IconKey(item,iconMatch);auto match=FindGroup(item,&counts);
        if(item.owner&&!item.runtime.Valid()&&!match.group.empty()){match.pending=true;match.status=L"待确认：无法核对程序身份";}
        item.pending=match.pending;item.matchStatus=match.status;item.group=match.pending?L"":match.group;
        if(!match.pending&&item.runtime.Valid())for(const auto& key:IconKeys(item))if(assignments.count(key)&&!ruleBindings.count(key))ruleBindings[key]=item.runtime;
    }
}
static void ForgetRule(const std::wstring& key) {
    assignments.erase(key);pendingRules.erase(key);ruleBindings.erase(key);
}
static bool SameRuntime(const TrayItem& a,const TrayItem& b) {
    return a.owner==b.owner&&a.id==b.id&&a.runtime.Valid()&&b.runtime.Valid()&&a.runtime==b.runtime
        &&a.usesGuid==b.usesGuid&&(!a.usesGuid||!memcmp(&a.guid,&b.guid,sizeof(GUID)))
        &&Lower(a.path)==Lower(b.path)&&StableClass(a.ownerClass)==StableClass(b.ownerClass)&&a.callback==b.callback;
}

static void FreeItems(std::vector<TrayItem>& list) { for(auto& i:list)if(i.image)DestroyIcon(i.image);list.clear(); }
static bool ResolveIconGuid(const TrayItem& item,GUID& guid) {
#ifdef NATIVE_TEST_BUILD
    auto test=testIconGuids.find({item.owner,item.id});
    if(test!=testIconGuids.end()){guid=test->second;return true;}
#endif
    wchar_t system[MAX_PATH]={};GetSystemDirectoryW(system,MAX_PATH);
    std::wstring systemPath=Lower(system),path=Lower(item.path),cls=StableClass(item.ownerClass);
    GUID security={};
    if(path==systemPath+L"\\securityhealthsystray.exe"&&cls==L"atl@securityhealthsso.dll"&&item.id==100)
        security={0xba82e2dc,0xf405,0x47ad,{0xb0,0x32,0xcf,0x0f,0xaa,0x0e,0x39,0x33}};
    else {
        size_t slash=systemPath.find_last_of(L'\\');std::wstring explorer=systemPath.substr(0,slash)+L"\\explorer.exe";
        if(path!=explorer)return false;
        if(cls==L"atl@sndvolsso.dll"&&item.id==100)
            security={0x7820ae73,0x23e3,0x4229,{0x82,0xc1,0xe4,0x1c,0xb6,0x7d,0x5b,0x9c}};
        else if(cls==L"bluetoothnotificationareaiconwindowclass"&&item.id==0)
            security={0x6da68f06,0x00f1,0x4e6e,{0x81,0x58,0x7f,0x1f,0xfd,0x4f,0x9d,0xb9}};
        else return false;
    }
    // Hidden icons may have no rectangle. Keep their verified GUID available
    // for persisted matching and restoration while they are hidden.
    guid=security;return true;
}
static void ReadToolbar(HWND toolbar,std::vector<TrayItem>& out) {
    DWORD pid=0;GetWindowThreadProcessId(toolbar,&pid);HANDLE process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ,FALSE,pid);if(!process)return;
    void* memory=VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!memory){CloseHandle(process);return;}
    DWORD_PTR count=0;
    if(Send(toolbar,TB_BUTTONCOUNT,0,0,count)&&count<512)for(size_t j=0;j<count;j++) {
        DWORD_PTR ok=0;TBBUTTON button={};RemoteTrayData data={};
        if(!Send(toolbar,TB_GETBUTTON,j,(LPARAM)memory,ok)||!ok||!ReadMemory(process,(ULONG_PTR)memory,button)||!button.dwData||!ReadMemory(process,button.dwData,data)||!IsWindow((HWND)data.owner))continue;
        DWORD ownerPid=0;GetWindowThreadProcessId((HWND)data.owner,&ownerPid);if(ownerPid==GetCurrentProcessId()
#ifdef NATIVE_TEST_BUILD
            &&!testingOwnIcons
#endif
        )continue;
        TrayItem item;item.toolbar=toolbar;item.command=button.idCommand;item.owner=(HWND)data.owner;item.id=data.id;item.callback=data.callback;item.version=data.version;item.state=button.fsState;
        item.path=ProcessPath(ownerPid);size_t slash=item.path.find_last_of(L"\\/");item.process=item.path.empty()?L"PID "+Number(ownerPid):item.path.substr(slash==std::wstring::npos?0:slash+1);
        item.ownerClass=OwnerClass(item.owner,ownerPid);item.usesGuid=ResolveIconGuid(item,item.guid);
        item.runtime={ownerPid,processStarts[ownerPid],item.owner,item.id};
        DWORD_PTR chars=0;if(Send(toolbar,TB_GETBUTTONTEXTW,item.command,(LPARAM)memory,chars)&&chars>0&&chars<1500){std::vector<wchar_t> text(chars+1,0);SIZE_T got=0;if(ReadProcessMemory(process,memory,text.data(),chars*sizeof(wchar_t),&got))item.tip=text.data();}
        if(data.icon)item.image=CopyIcon((HICON)data.icon);
        // Avoid initializing shell icon handlers (COM/thread pools) on each scan.
        // File icons are extracted only for a group that the user actually opens.
        if(!item.image){item.image=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));item.fallbackImage=true;}
        out.push_back(std::move(item));
    }
    VirtualFreeEx(process,memory,0,MEM_RELEASE);CloseHandle(process);
}
static BOOL CALLBACK ReadChild(HWND h,LPARAM p) { if(ClassOf(h)==L"ToolbarWindow32")ReadToolbar(h,*(std::vector<TrayItem>*)p);return TRUE; }
static BOOL CALLBACK ReadRoot(HWND h,LPARAM p) { auto c=ClassOf(h);if(c==L"Shell_TrayWnd"||c==L"NotifyIconOverflowWindow"||c==L"Shell_SecondaryTrayWnd")EnumChildWindows(h,ReadChild,p);return TRUE; }
static std::vector<TrayItem> ReadTrayLocations() {
    std::vector<TrayItem> result;processPaths.clear();processStarts.clear();EnumWindows(ReadRoot,(LPARAM)&result);return result;
}
static std::vector<TrayItem> ReadTray() {
    auto locations=ReadTrayLocations();std::vector<TrayItem> result;
    for(auto& item:locations){
        auto match=std::find_if(result.begin(),result.end(),[&](const TrayItem& existing){return existing.owner==item.owner&&existing.id==item.id;});
        if(match==result.end()){result.push_back(std::move(item));item.image=nullptr;}
        else if((match->state&TBSTATE_HIDDEN)&&!(item.state&TBSTATE_HIDDEN)){
            if(match->image)DestroyIcon(match->image);*match=std::move(item);item.image=nullptr;
        }
    }
    FreeItems(locations);ResolveGroups(result);return result;
}

static bool CommandMatches(const TrayItem& item);
static bool NativeVisibility(const TrayItem& item,bool hide) {
    auto current=ReadTrayLocations();bool valid=false,usesGuid=false;GUID guid={};
    for(const auto& live:current)if(SameRuntime(item,live)){valid=true;usesGuid=live.usesGuid;guid=live.guid;break;}
    FreeItems(current);if(!valid)return false;
    NOTIFYICONDATAW icon={};icon.cbSize=sizeof(icon);icon.hWnd=item.owner;icon.uID=item.id;
    icon.uFlags=NIF_STATE;if(usesGuid){icon.uFlags|=NIF_GUID;icon.guidItem=guid;}
    icon.dwStateMask=NIS_HIDDEN;icon.dwState=hide?NIS_HIDDEN:0;
    return Shell_NotifyIconW(NIM_MODIFY,&icon)!=FALSE;
}
static bool VisibleAtRuntime(const RuntimeIdentity& runtime) {
    auto current=ReadTrayLocations();bool visible=false;
    for(const auto& item:current)if(item.runtime==runtime&&!(item.state&TBSTATE_HIDDEN))visible=true;
    FreeItems(current);return visible;
}
static bool WriteRecovery() {
    std::wstring s;for(const auto& h:hidden)s+=Number((ULONG_PTR)h.owner)+L"\t"+Number(h.id)+L"\t"+Number(h.runtime.pid)+L"\t"+Number(h.runtime.started)+L"\t"+Number((ULONG_PTR)h.toolbar)+L"\n";
    std::wstring temporary=recoveryPath+L".tmp";
    bool ok=WriteText(temporary,s)&&MoveFileExW(temporary.c_str(),recoveryPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok)LogError(L"无法写入图标恢复记录");return ok;
}


static bool RuntimeAlive(const RuntimeIdentity& identity);
static void RecoverIcons() {
    std::wstring text=ReadText(recoveryPath);std::vector<Hidden> records;size_t start=0;
    while(start<text.size()){
        size_t end=text.find(L'\n',start);if(end==std::wstring::npos)end=text.size();
        std::wstring line=text.substr(start,end-start);start=end+1;std::vector<ULONG_PTR> fields;size_t at=0;
        while(at<line.size()){fields.push_back(wcstoull(line.c_str()+at,nullptr,10));size_t tab=line.find(L'\t',at);if(tab==std::wstring::npos)break;at=tab+1;}
        if(fields.size()<2)continue;
        Hidden record={};record.owner=(HWND)fields[0];record.id=(UINT)fields[1];
        if(fields.size()>=4)record.runtime={(DWORD)fields[2],(uint64_t)fields[3],record.owner,record.id};
        if(fields.size()>=5)record.toolbar=(HWND)fields[4];records.push_back(record);
    }
    if(records.empty())return;auto current=ReadTrayLocations();bool failed=false;
    for(const auto& record:records){
        bool resolved=false;
        for(const auto& item:current){
            if(item.owner!=record.owner||item.id!=record.id)continue;
            if(record.runtime.Valid()&&!(record.runtime==item.runtime))continue;
            resolved=true;
            if(!(item.state&TBSTATE_HIDDEN))continue;
            bool restored=NativeVisibility(item,false);
            // Compatibility recovery only: old builds edited toolbar state directly.
            if(!restored&&!record.toolbar&&CommandMatches(item)){
                DWORD_PTR result=0;restored=Send(item.toolbar,TB_HIDEBUTTON,item.command,MAKELPARAM(FALSE,0),result)&&result;
                InvalidateRect(item.toolbar,nullptr,TRUE);
            }
            if(!restored||!VisibleAtRuntime(item.runtime))failed=true;
        }
        if(!resolved&&RuntimeAlive(record.runtime))failed=true;
    }
    FreeItems(current);if(!failed)DeleteFileW(recoveryPath.c_str());else LogError(L"部分图标尚未恢复，已保留恢复记录");
}



static bool RuntimeAlive(const RuntimeIdentity& identity) {
    if(!identity.Valid()||!IsWindow(identity.owner))return false;
    DWORD pid=0;GetWindowThreadProcessId(identity.owner,&pid);if(pid!=identity.pid)return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return true; // No permission is not evidence that the original exited.
    uint64_t started=ProcessStart(process);CloseHandle(process);return !started||started==identity.started;
}
static void RestoreHidden() {
    if(hidden.empty())return;auto current=ReadTrayLocations();std::vector<Hidden> failed;
    for(const auto& record:hidden){
        auto item=std::find_if(current.begin(),current.end(),[&](const TrayItem& value){return value.runtime==record.runtime&&value.toolbar==record.toolbar;});
        if(item==current.end()){
            item=std::find_if(current.begin(),current.end(),[&](const TrayItem& value){return value.runtime==record.runtime;});
        }
        if(item==current.end()){if(RuntimeAlive(record.runtime))failed.push_back(record);continue;}
        if(VisibleAtRuntime(record.runtime))continue;
        if(!NativeVisibility(*item,false)||!VisibleAtRuntime(record.runtime))failed.push_back(record);
    }
    for(auto& item:items)if(std::any_of(hidden.begin(),hidden.end(),[&](const Hidden& record){return record.runtime==item.runtime;})&&std::none_of(failed.begin(),failed.end(),[&](const Hidden& record){return record.runtime==item.runtime;}))item.state&=~TBSTATE_HIDDEN;
    FreeItems(current);hidden=failed;
    if(hidden.empty())DeleteFileW(recoveryPath.c_str());else {WriteRecovery();LogError(L"部分图标尚未恢复，已保留恢复记录");}
}

static bool OwnsHidden(const TrayItem& i) { return std::any_of(hidden.begin(),hidden.end(),[&](const Hidden& h){return h.owner==i.owner&&h.id==i.id&&h.runtime==i.runtime;}); }
static bool CommandMatches(const TrayItem& item) {
    DWORD pid=0;GetWindowThreadProcessId(item.toolbar,&pid);
    HANDLE process=OpenProcess(PROCESS_VM_OPERATION|PROCESS_VM_READ,FALSE,pid);
    if(!process)return false;
    void* memory=VirtualAllocEx(process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    DWORD_PTR index=0,read=0;TBBUTTON button={};RemoteTrayData data={};
    bool ok=memory&&Send(item.toolbar,TB_COMMANDTOINDEX,item.command,0,index)&&index!=(DWORD_PTR)-1
        &&Send(item.toolbar,TB_GETBUTTON,index,(LPARAM)memory,read)&&read
        &&ReadMemory(process,(ULONG_PTR)memory,button)&&button.idCommand==item.command
        &&button.dwData&&ReadMemory(process,button.dwData,data)
        &&(HWND)data.owner==item.owner&&data.id==item.id&&data.callback==item.callback;
    if(ok){
        DWORD ownerPid=0;GetWindowThreadProcessId(item.owner,&ownerPid);
        HANDLE ownerProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,ownerPid);
        uint64_t started=ownerProcess?ProcessStart(ownerProcess):0;if(ownerProcess)CloseHandle(ownerProcess);
        RuntimeIdentity live={ownerPid,started,item.owner,item.id};
        ok=item.runtime.Valid()&&live==item.runtime;
    }
    if(memory)VirtualFreeEx(process,memory,0,MEM_RELEASE);CloseHandle(process);return ok;
}


static void RestoreUnassigned() {
    std::vector<Hidden> keep;
    for(const auto& record:hidden){
        auto current=std::find_if(items.begin(),items.end(),[&](const TrayItem& item){return item.runtime==record.runtime;});
        if(current==items.end()){if(RuntimeAlive(record.runtime))keep.push_back(record);continue;}
        if(!current->group.empty()&&!current->pending){keep.push_back(record);continue;}
        if(VisibleAtRuntime(record.runtime)){current->state&=~TBSTATE_HIDDEN;continue;}
        if(!NativeVisibility(*current,false)||!VisibleAtRuntime(record.runtime))keep.push_back(record);
        else current->state&=~TBSTATE_HIDDEN;
    }
    hidden=keep;if(hidden.empty())DeleteFileW(recoveryPath.c_str());else WriteRecovery();
}
static void FoldAssigned() {
    if(!foldIcons)return;
    // Journal exact identities before asking Explorer to hide them.
    std::vector<size_t> todo;size_t previous=hidden.size();
    for(size_t n=0;n<items.size();n++){
        auto& item=items[n];
        if(item.group.empty()||item.pending||(item.state&TBSTATE_HIDDEN)||!CommandMatches(item))continue;
        // Applications can reveal their icon again after a click or state update.
        // Reapply the fold even when this runtime already has a recovery record.
        if(!OwnsHidden(item))hidden.push_back({item.toolbar,item.owner,item.id,item.command,item.runtime});
        todo.push_back(n);
    }
    if(!todo.empty()&&!WriteRecovery()){hidden.resize(previous);return;}
    for(size_t n:todo){
        auto& item=items[n];
        if(!NativeVisibility(item,true)){
            hidden.erase(std::remove_if(hidden.begin(),hidden.end(),[&](const Hidden& record){return record.runtime==item.runtime;}),hidden.end());
            LogError(L"此图标不支持折叠，已保留原图标");
        }
    }
    // Display only icons Explorer confirms hidden; a successful request alone
    // is not proof that the original has disappeared.
    if(!todo.empty()){
        auto live=ReadTrayLocations();
        for(auto& item:items){
            bool found=false,visible=false;
            for(const auto& current:live)if(current.runtime==item.runtime){found=true;if(!(current.state&TBSTATE_HIDDEN))visible=true;}
            if(found){if(visible)item.state&=~TBSTATE_HIDDEN;else item.state|=TBSTATE_HIDDEN;}
        }
        FreeItems(live);
    }
    // Keep all successful requests journaled, including delayed Explorer updates.
    if(hidden.empty())DeleteFileW(recoveryPath.c_str());else WriteRecovery();
}

static HICON MakeAppIcon() {
    HICON embedded=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,32,32,0);if(embedded)return embedded;
    HDC dc=GetDC(nullptr),mem=CreateCompatibleDC(dc);BITMAPV5HEADER header={};header.bV5Size=sizeof(header);header.bV5Width=32;header.bV5Height=-32;header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5RedMask=0x00ff0000;header.bV5GreenMask=0x0000ff00;header.bV5BlueMask=0x000000ff;header.bV5AlphaMask=0xff000000;
    void* pixels=nullptr;HBITMAP color=CreateDIBSection(dc,(BITMAPINFO*)&header,DIB_RGB_COLORS,&pixels,nullptr,0);HBITMAP old=(HBITMAP)SelectObject(mem,color);DWORD* p=(DWORD*)pixels;for(int y=0;y<32;y++)for(int x=0;x<32;x++)p[y*32+x]=((x-16)*(x-16)+(y-16)*(y-16)<225)?0xff2378bb:0;
    SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(255,255,255));HFONT f=CreateFontW(-21,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Segoe UI");HFONT of=(HFONT)SelectObject(mem,f);RECT r={0,1,32,32};std::wstring text=L"T";DrawTextW(mem,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(mem,of);DeleteObject(f);
    for(int i=0;i<1024;i++)if(p[i]&0x00ffffff)p[i]|=0xff000000;
    SelectObject(mem,old);HBITMAP mask=CreateBitmap(32,32,1,1,nullptr);ICONINFO ii={};ii.fIcon=TRUE;ii.hbmColor=color;ii.hbmMask=mask;HICON icon=CreateIconIndirect(&ii);DeleteObject(mask);DeleteObject(color);DeleteDC(mem);ReleaseDC(nullptr,dc);return icon;
}
static void DeleteNotifications() {
    for(auto& p:notificationIcons){NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=mainWindow;n.uID=p.first;Shell_NotifyIconW(NIM_DELETE,&n);DestroyIcon(p.second);}notificationIcons.clear();notificationGroups.clear();notificationCount=0;
}
static void AddNotification(UINT id,const std::wstring& group,HICON icon) {
    NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=mainWindow;n.uID=id;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=TrayCallback;n.hIcon=icon;
    std::wstring tip=id==1?L"TrayGroups 托盘分组器":group;wcsncpy_s(n.szTip,tip.c_str(),_TRUNCATE);
    if(Shell_NotifyIconW(NIM_ADD,&n)){n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);notificationIcons[id]=icon;notificationGroups[id]=group;notificationCount++;}else DestroyIcon(icon);
}

static std::set<std::wstring> GroupNames() {
    std::set<std::wstring> groups;
    for(const auto& i:assignments)if(!i.second.empty())groups.insert(i.second);
    for(const auto& i:pathAssignments)if(!i.second.empty())groups.insert(i.second);
    for(const auto& i:programAssignments)if(!i.second.empty())groups.insert(i.second);
    for(const auto& i:groupLabelSizes)if(!i.first.empty())groups.insert(i.first);
    for(const auto& i:groupLabelWidths)if(!i.first.empty())groups.insert(i.first);
    for(const auto& i:groupLabelHeights)if(!i.first.empty())groups.insert(i.first);
    return groups;
}
static void SyncNotifications() {
    if(notificationIcons.size()==1&&notificationIcons.count(1))return;
    DeleteNotifications();AddNotification(1,L"",appIcon?CopyIcon(appIcon):MakeAppIcon());
}

static void Refresh(bool restore=false) {
    if(refreshing)return;refreshing=true;
    if(restore)RestoreHidden();
    FreeItems(items);items=ReadTray();SyncNotifications();RestoreUnassigned();FoldAssigned();refreshing=false;
}
static std::vector<std::pair<WPARAM,LPARAM>> CallbackSequence(UINT id,UINT version,int action,POINT pos) {
    std::vector<std::pair<WPARAM,LPARAM>> result;
    auto add=[&](UINT event){if(version>=4)result.push_back({MAKEWPARAM((short)pos.x,(short)pos.y),MAKELPARAM(event,(WORD)id)});else result.push_back({id,event});};
    add(WM_MOUSEMOVE);
    if(action==2){add(WM_RBUTTONDOWN);add(version>=4?WM_CONTEXTMENU:WM_RBUTTONUP);}
    else if(action==1){add(WM_LBUTTONDOWN);add(version>=4?NIN_SELECT:WM_LBUTTONUP);add(WM_LBUTTONDBLCLK);add(version>=4?NIN_SELECT:WM_LBUTTONUP);}
    else {add(WM_LBUTTONDOWN);add(version>=4?NIN_SELECT:WM_LBUTTONUP);}
    return result;
}
static bool Dispatch(const TrayItem& item,int action,POINT pos) {
    if(!IsWindow(item.owner)){LogError(L"程序图标已重建，请刷新后重试");return false;}
    DWORD pid=0;GetWindowThreadProcessId(item.owner,&pid);
    if(item.runtime.Valid()){
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        RuntimeIdentity live={pid,process?ProcessStart(process):0,item.owner,item.id};if(process)CloseHandle(process);
        if(!(live==item.runtime)){LogError(L"图标已重建，请刷新后重试");return false;}
    }
    AllowSetForegroundWindow(pid);
    bool ok=true;for(auto m:CallbackSequence(item.id,item.version,action,pos))if(!PostMessageW(item.owner,item.callback,m.first,m.second))ok=false;
    if(!ok){forwardingFailures++;LogError(L"操作转发被拒绝（可能是程序权限不同），可改用原托盘图标");}return ok;
}
static bool Forward(TrayItem identity,int action,POINT pos) {
    // Resolve latest callback/version after an application changes its icon.
    auto current=ReadTray();auto match=std::find_if(current.begin(),current.end(),[&](const TrayItem& t){return SameRuntime(t,identity);});
    bool ok=false;if(match!=current.end())ok=Dispatch(*match,action,pos);else LogError(L"原图标已退出，请刷新");FreeItems(current);return ok;
}
static void ClosePopup() { if(popupWindow)DestroyWindow(popupWindow); }
static void ShowSettings();
static void ShowResources();
static std::wstring ResourceReport(bool json=false) {
    auto actual=NativeResources::Read();
    PROCESS_MEMORY_COUNTERS_EX m={};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof(m));
    FILETIME a,b,k,u;GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER kt,ut;kt.LowPart=k.dwLowDateTime;kt.HighPart=k.dwHighDateTime;ut.LowPart=u.dwLowDateTime;ut.HighPart=u.dwHighDateTime;
    DWORD handles=0;GetProcessHandleCount(GetCurrentProcess(),&handles);unsigned threads=0;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap!=INVALID_HANDLE_VALUE){THREADENTRY32 te={};te.dwSize=sizeof(te);if(Thread32First(snap,&te))do{if(te.th32OwnerProcessID==GetCurrentProcessId())threads++;}while(Thread32Next(snap,&te));CloseHandle(snap);}
    wchar_t report[2304]={};
    double cpu=(kt.QuadPart+ut.QuadPart)/10000000.0;
    static double previousCpu=0;static ULONGLONG previousWall=0;ULONGLONG wall=GetTickCount64();SYSTEM_INFO sys;GetSystemInfo(&sys);double percent=previousWall&&wall>previousWall?(cpu-previousCpu)*100000.0/(wall-previousWall)/std::max(1ul,sys.dwNumberOfProcessors):0;previousCpu=cpu;previousWall=wall;
    if(json)swprintf_s(report,L"{\"pid\":%lu,\"taskManagerPrivateWorkingSetBytes\":%llu,\"workingSetBytes\":%llu,\"privateBytes\":%llu,\"cpuSeconds\":%.3f,\"cpuPercent\":%.3f,\"threads\":%u,\"handles\":%lu,\"detectedIcons\":%llu,\"groupIcons\":%u,\"hiddenOriginals\":%llu,\"forwardingFailures\":%u}",GetCurrentProcessId(),(unsigned long long)actual.privateWorking,(unsigned long long)m.WorkingSetSize,(unsigned long long)m.PrivateUsage,cpu,percent,threads,handles,(unsigned long long)items.size(),(unsigned)GroupNames().size(),(unsigned long long)hidden.size(),forwardingFailures);
    else swprintf_s(report,L"TrayGroups.exe · 原生 Win32\r\n\r\n任务管理器口径（专用工作集）：%.2f MiB\r\n专用提交内存：%.2f MiB\r\n总工作集（含共享内存）：%.2f MiB\r\nCPU 当前：%.2f %%    累计：%.2f 秒\r\n线程：%u    句柄：%lu\r\n分组数量：%u    托盘项目：%llu\r\n已折叠原图标：%llu\r\n\r\n每个程序 < 3 MiB：%ls\r\n不强制清空工作集。",actual.privateWorking/1048576.0,m.PrivateUsage/1048576.0,m.WorkingSetSize/1048576.0,percent,cpu,threads,handles,(unsigned)GroupNames().size(),(unsigned long long)items.size(),(unsigned long long)hidden.size(),actual.privateWorking<3*1048576?L"当前达到":L"当前未达到");
    std::wstring result=report;if(!json&&!latestError.empty())result+=L"\r\n最近状态："+latestError;return result;
}

static void ShowGroup(const std::wstring& group);
static void ShowGroups();
static int popupPage=0,popupPageSize=16,popupWidth=420,popupItemTop=40;
static int popupItemPage=0,popupItemPageSize=16,popupItemPages=1;
static RECT TileRect(int n) { int col=n%4,row=n/4;return RECT{12+col*100,popupItemTop+row*72,108+col*100,popupItemTop+(row+1)*72-4}; }
static int TileAt(POINT pt) {
    for(size_t n=0;n<popupItems.size();n++){RECT r=TileRect((int)n);if(PtInRect(&r,pt))return (int)n;}return -1;
}
static bool PopupEligible(const TrayItem& item) {
    return !item.group.empty()&&!item.pending&&(item.state&TBSTATE_HIDDEN);
}
static void PerformTile(int tile,int action) {
    if(tile<0||tile>=(int)popupItems.size())return;TrayItem item=items[popupItems[tile]];POINT pt;GetCursorPos(&pt);ClosePopup();
    if(Forward(item,action,pt)&&foldIcons&&mainWindow){reFoldAttempts=6;SetTimer(mainWindow,ReFoldTimer,250,nullptr);}
}
static HFONT LabelFont(int points,int dpi) { return CreateFontW(-MulDiv(points,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI"); }
static bool preservePopupPosition=false;
static POINT popupOrigin={};
static RECT CursorWorkArea() {
    POINT cursor;if(preservePopupPosition)cursor=popupOrigin;else GetCursorPos(&cursor);MONITORINFO info={};info.cbSize=sizeof(info);
    GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&info);return info.rcWork;
}
static void PlacePopup(int width,int height) {
    RECT work=CursorWorkArea();POINT pos;GetCursorPos(&pos);
    if(preservePopupPosition){
        int x=std::max((int)work.left,std::min((int)popupOrigin.x,(int)work.right-width));
        int y=std::max((int)work.top,std::min((int)popupOrigin.y,(int)work.bottom-height));
        SetWindowPos(popupWindow,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);return;
    }
    int x=std::max((int)work.left,std::min((int)pos.x-width/2,(int)work.right-width));
    int y=std::max((int)work.top,std::min((int)pos.y-height,(int)work.bottom-height));
    SetWindowPos(popupWindow,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);
}
static std::wstring ItemLabel(const TrayItem& i) {
    if(!i.tip.empty())return i.tip.substr(0,i.tip.find_first_of(L"\r\n"));
    return i.process;
}
static std::wstring FriendlyIconName(const TrayItem& i) {
    std::wstring cls=StableClass(i.ownerClass);
    if(cls==L"atl@sndvolsso.dll")return L"系统音量";
    if(cls==L"bluetoothnotificationareaiconwindowclass")return L"蓝牙设备";
    if(cls==L"atl@pnidui.dll")return L"网络状态";
    if(cls==L"atl@securityhealthsso.dll")return L"Windows 安全中心";
    std::wstring label=ItemLabel(i);
    return label.empty()?L"程序图标":label;
}
static int PopupFooter() {
    if(showingGroups)return groupRects.empty()?42:groupRects.back().bottom+6;
    return 44+((int)popupItems.size()+3)/4*72;
}
static LRESULT CALLBACK PopupProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    switch(msg){
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);SetBkMode(dc,TRANSPARENT);HFONT old=(HFONT)SelectObject(dc,appFont);
        RECT title={12,8,popupWidth-12,34};std::wstring text=showingGroups?L"分组列表":L"‹ 分组    "+activeGroup;
        DrawTextW(dc,text.c_str(),-1,&title,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(showingGroups){
            int dpi=GetDpiForWindow(h);
            for(size_t n=0;n<groupRects.size();n++){
                const auto& group=popupGroups[popupPage*popupPageSize+n];RECT r=groupRects[n];DrawEdge(dc,&r,EDGE_ETCHED,BF_RECT);
                HFONT font=LabelFont(GroupLabelSize(group),dpi),prior=(HFONT)SelectObject(dc,font);
                InflateRect(&r,-12,-4);std::wstring label=group;
                if(group==activeGroup)FillRect(dc,&r,(HBRUSH)(COLOR_HIGHLIGHT+1));
                SetTextColor(dc,group==activeGroup?GetSysColor(COLOR_HIGHLIGHTTEXT):GetSysColor(COLOR_WINDOWTEXT));
                DrawTextW(dc,label.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                SelectObject(dc,prior);DeleteObject(font);
            }
            SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
            RECT selected={12,popupItemTop-28,popupWidth-12,popupItemTop-4};
            std::wstring groupName=activeGroup;
            DrawTextW(dc,groupName.c_str(),-1,&selected,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        }
        {
            for(size_t n=0;n<popupItems.size();n++){
                auto& i=items[popupItems[n]];RECT r=TileRect((int)n);DrawEdge(dc,&r,EDGE_ETCHED,BF_RECT);
                DrawIconEx(dc,r.left+35,r.top+6,i.image,24,24,0,nullptr,DI_NORMAL);
                RECT label={r.left+3,r.top+36,r.right-3,r.bottom-2};std::wstring name=ItemLabel(i);
                DrawTextW(dc,name.c_str(),-1,&label,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
            if(popupItems.empty()){
                RECT empty={12,popupItemTop+12,popupWidth-12,popupItemTop+66};
                const wchar_t* message=!foldIcons?L"折叠已暂停；可在设置的高级页开启":popupGroups.empty()?L"暂无分组；右键 T 打开设置":L"此组暂无隐藏图标";
                DrawTextW(dc,message,-1,&empty,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);
            }
        }
        if(showingGroups&&popupGroups.size()>(size_t)popupPageSize){
            RECT footer={12,PopupFooter(),popupWidth-12,PopupFooter()+26};
            DrawTextW(dc,L"‹ 上一页                        下一页 ›",-1,&footer,DT_SINGLELINE|DT_CENTER);
        }
        if(showingGroups&&popupItemPages>1){
            int bottom=popupItemTop+std::max(1,((int)popupItems.size()+3)/4)*72;
            RECT footer={12,bottom,popupWidth-12,bottom+26};
            std::wstring page=L"‹ 上一页           "+Number(popupItemPage+1)+L" / "+Number(popupItemPages)+L"           下一页 ›";
            DrawTextW(dc,page.c_str(),-1,&footer,DT_SINGLELINE|DT_CENTER);
        }
        SelectObject(dc,old);EndPaint(h,&ps);return 0;
    }
    case WM_LBUTTONUP:{
        POINT pt={GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(pt.y<34){if(!showingGroups){popupPage=0;PostMessageW(h,WM_APP+20,0,0);}return 0;}
        int itemFooter=popupItemTop+std::max(1,((int)popupItems.size()+3)/4)*72;
        if(showingGroups&&popupItemPages>1&&pt.y>=itemFooter){
            int next=popupItemPage+(pt.x<popupWidth/2?-1:1);
            if(next>=0&&next<popupItemPages){popupItemPage=next;PostMessageW(h,WM_APP+20,0,0);}return 0;
        }
        if(pt.y>=PopupFooter()&&(!showingGroups||pt.y<popupItemTop)){
            if(showingGroups&&popupGroups.size()<=(size_t)popupPageSize)return 0;
            int direction=pt.x<popupWidth/2?-1:1;if(popupPage+direction<0)return 0;
            popupPage+=direction;PostMessageW(h,WM_APP+20,showingGroups?0:1,0);return 0;
        }
        if(showingGroups){for(size_t n=0;n<groupRects.size();n++)if(PtInRect(&groupRects[n],pt)){
            activeGroup=popupGroups[popupPage*popupPageSize+n];popupItemPage=0;PostMessageW(h,WM_APP+20,0,0);return 0;
        }}
        pendingClick=TileAt(pt);if(pendingClick>=0)SetTimer(h,SingleTimer,GetDoubleClickTime(),nullptr);return 0;
    }
    case WM_APP+20:{
        RECT placement;if(GetWindowRect(h,&placement)){popupOrigin={placement.left,placement.top};preservePopupPosition=true;}
        std::wstring group=activeGroup;if(w)ShowGroup(group);else ShowGroups();
        preservePopupPosition=false;return 0;
    }
    case WM_LBUTTONDBLCLK:{KillTimer(h,SingleTimer);pendingClick=-1;POINT pt={GET_X_LPARAM(l),GET_Y_LPARAM(l)};PerformTile(TileAt(pt),1);return 0;}
    case WM_RBUTTONUP:{POINT pt={GET_X_LPARAM(l),GET_Y_LPARAM(l)};KillTimer(h,SingleTimer);PerformTile(TileAt(pt),2);return 0;}
    case WM_TIMER:if(w==SingleTimer){KillTimer(h,SingleTimer);PerformTile(pendingClick,0);pendingClick=-1;}return 0;
    case WM_KEYDOWN:if(w==VK_ESCAPE)DestroyWindow(h);return 0;
    case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE)PostMessageW(h,WM_CLOSE,0,0);return 0;
    case WM_DESTROY:popupWindow=nullptr;pendingClick=-1;return 0;
    }
    return DefWindowProcW(h,msg,w,l);
}
static HWND NewPopup() {
    RECT work=CursorWorkArea();
    return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,PopupClass,L"托盘分组",WS_POPUP|WS_BORDER,work.left+8,work.top+8,420,100,mainWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
}
static void FillPopupItems(const std::wstring& group) {
    std::vector<int> matching;
    for(size_t n=0;n<items.size();n++)if(items[n].group==group&&PopupEligible(items[n]))matching.push_back((int)n);
    popupItemPages=std::max(1,((int)matching.size()+popupItemPageSize-1)/popupItemPageSize);
    popupItemPage=std::max(0,std::min(popupItemPage,popupItemPages-1));
    auto first=matching.begin()+std::min((int)matching.size(),popupItemPage*popupItemPageSize);
    popupItems.assign(first,first+std::min(popupItemPageSize,(int)(matching.end()-first)));
    for(int n:popupItems){
        auto& item=items[n];
        if(item.fallbackImage&&!item.path.empty()){
            HICON extracted=nullptr;
            if(ExtractIconExW(item.path.c_str(),0,nullptr,&extracted,1)&&extracted){DestroyIcon(item.image);item.image=extracted;item.fallbackImage=false;}
        }
    }
}
static void AttachPopupTooltips() {
    HWND tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,popupWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,420);
    for(size_t n=0;n<popupItems.size();n++){
        TOOLINFOW t={};t.cbSize=sizeof(t);t.uFlags=TTF_SUBCLASS;t.hwnd=popupWindow;t.uId=n+1;t.rect=TileRect((int)n);
        auto& item=items[popupItems[n]];t.lpszText=(LPWSTR)(item.tip.empty()?item.process.c_str():item.tip.c_str());
        SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);
    }
}
static void ShowGroups() {
    ClosePopup();Refresh();showingGroups=true;auto groups=GroupNames();popupGroups.assign(groups.begin(),groups.end());
    if(std::find(popupGroups.begin(),popupGroups.end(),activeGroup)==popupGroups.end()){
        activeGroup=popupGroups.empty()?L"":popupGroups.front();
        for(const auto& item:items)if(PopupEligible(item)){activeGroup=item.group;break;}
    }
    popupWindow=NewPopup();if(!popupWindow)return;
    RECT work=CursorWorkArea();int dpi=GetDpiForWindow(popupWindow);
    int maximumHeight=0;
    for(const auto& group:popupGroups)maximumHeight=std::max(maximumHeight,std::max(MulDiv(GroupLabelSize(group),dpi,72)+24,MulDiv(GroupLabelHeight(group),dpi,96))+6);
    popupPageSize=std::max(1,std::min(8,((int)(work.bottom-work.top)-250)/std::max(1,maximumHeight)));
    popupPage=std::max(0,std::min(popupPage,((int)popupGroups.size()-1)/popupPageSize));
    groupRects.clear();int top=40;popupWidth=420;
    for(size_t n=popupPage*popupPageSize;n<popupGroups.size()&&groupRects.size()<(size_t)popupPageSize;n++){
        int height=std::max(MulDiv(GroupLabelSize(popupGroups[n]),dpi,72)+24,MulDiv(GroupLabelHeight(popupGroups[n]),dpi,96));
        int width=std::min((int)(work.right-work.left)-28,MulDiv(GroupLabelWidth(popupGroups[n]),dpi,96));
        groupRects.push_back(RECT{12,top,12+width,top+height});top+=height+6;popupWidth=std::max(popupWidth,width+24);
    }
    popupItemTop=PopupFooter()+(popupGroups.size()>(size_t)popupPageSize?58:34);
    popupItemPageSize=4*std::max(1,std::min(4,((int)(work.bottom-work.top)-popupItemTop-140)/72));
    FillPopupItems(activeGroup);
    int height=popupItemTop+std::max(1,((int)popupItems.size()+3)/4)*72+(popupItemPages>1?34:12);
    PlacePopup(popupWidth,height);AttachPopupTooltips();SetForegroundWindow(popupWindow);
}
static void ShowGroup(const std::wstring& group) {
    if(group.empty()){ShowGroups();return;}
    ClosePopup();Refresh();showingGroups=false;popupWidth=420;popupItemTop=40;activeGroup=group;std::vector<int> all;
    for(size_t n=0;n<items.size();n++)if(items[n].group==group&&PopupEligible(items[n]))all.push_back((int)n);
    RECT work=CursorWorkArea();int pageSize=4*std::max(1,std::min(4,((int)(work.bottom-work.top)-110)/72));
    popupPage=std::max(0,std::min(popupPage,all.empty()?0:((int)all.size()-1)/pageSize));
    auto first=all.begin()+std::min((int)all.size(),popupPage*pageSize);
    popupItems.assign(first,first+std::min(pageSize,(int)(all.end()-first)));
    for(int n:popupItems){auto& i=items[n];if(i.fallbackImage&&!i.path.empty()){
        HICON extracted=nullptr;if(ExtractIconExW(i.path.c_str(),0,nullptr,&extracted,1)&&extracted){DestroyIcon(i.image);i.image=extracted;i.fallbackImage=false;}
    }}
    popupWindow=NewPopup();if(!popupWindow)return;
    PlacePopup(420,std::max(110,PopupFooter()+34));
    AttachPopupTooltips();
    SetForegroundWindow(popupWindow);
}

static HWND Control(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,width,height,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)appFont,TRUE);return c;
}

#include "TraySettings.h"

#define SCREEN_CAPTURE_EMBEDDED
#include "ScreenCaptureTimer/ScreenCaptureTimer.cpp"

static LRESULT CALLBACK ResourceProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_TIMER){SetWindowTextW(resourceText,ResourceReport().c_str());return 0;}
    if(msg==WM_DESTROY){KillTimer(h,ResourceTimer);resourceWindow=nullptr;return 0;}
    return DefWindowProcW(h,msg,w,l);
}
static void ShowResources() {
    if(resourceWindow){SetForegroundWindow(resourceWindow);return;}
    resourceWindow=CreateWindowExW(WS_EX_APPWINDOW,L"Crli.TrayGroups.Resources",L"TrayGroups · 资源占用",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,540,420,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    resourceText=Control(resourceWindow,L"STATIC",ResourceReport().c_str(),0,18,16,495,350,0);SetTimer(resourceWindow,ResourceTimer,1000,nullptr);ShowWindow(resourceWindow,SW_SHOW);SetForegroundWindow(resourceWindow);
}
static void RootMenu() {
    ClosePopup();HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"设置分组…");AppendMenuW(menu,MF_STRING,2,L"资源占用");CaptureTimer::AddMenu(menu);AppendMenuW(menu,MF_STRING,4,L"重新加载配置并刷新");AppendMenuW(menu,MF_STRING,7,L"编辑 groups.ini 配置文件");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"恢复全部原图标并暂停折叠");AppendMenuW(menu,MF_STRING,6,L"退出（恢复原图标）");POINT pt;GetCursorPos(&pt);SetForegroundWindow(mainWindow);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,mainWindow,nullptr);DestroyMenu(menu);PostMessageW(mainWindow,WM_NULL,0,0);
    if(CaptureTimer::HandleMenu(id))return;
    if(id==1)ShowSettings();else if(id==2)ShowResources();else if(id==4){RestoreHidden();LoadSettings();Refresh();if(settingsWindow)FillSettings();}else if(id==5){foldIcons=false;SaveSettings();RestoreHidden();Refresh();}else if(id==6)PostMessageW(mainWindow,StopApp,0,0);else if(id==7){std::wstring cmd=L"notepad.exe \""+configPath+L"\"";STARTUPINFOW si={};si.cb=sizeof(si);PROCESS_INFORMATION pi={};if(CreateProcessW(nullptr,&cmd[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}}
}
static LRESULT CALLBACK MainProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    if(msg==TrayCallback){UINT id=HIWORD(l),event=LOWORD(l);if(!notificationGroups.count(id))return 0;if(event==WM_CONTEXTMENU)PostMessageW(h,WM_APP+8,2,0);else if(event==NIN_SELECT||event==NIN_KEYSELECT)PostMessageW(h,WM_APP+8,1,id);return 0;}
    if(msg==WM_APP+8){if(w==2)RootMenu();else if(l==1){if(popupWindow)ClosePopup();else{popupPage=0;ShowGroups();}}return 0;}
    if(msg==WM_TIMER&&w==RefreshTimer){if(!popupWindow&&!settingsWindow)Refresh();return 0;}
    if(msg==WM_TIMER&&w==CaptureTimer::TimerId()){CaptureTimer::OnTimer();return 0;}
    if(msg==WM_TIMER&&w==ReFoldTimer){
        if(!popupWindow&&!settingsWindow)Refresh();
        if(--reFoldAttempts<=0)KillTimer(h,ReFoldTimer);return 0;
    }
    if(msg==StopApp){RestoreHidden();if(CaptureTimer::IsBusy()){stopAfterCapture=true;return 0;}if(hidden.empty())DestroyWindow(h);else{foldIcons=false;LogError(L"退出已暂停：请先恢复剩余图标");}return 0;}
    if(msg==RefreshApp){RestoreHidden();LoadSettings();Refresh();if(settingsWindow)FillSettings();return 0;}
    if(msg==WM_COMMAND&&LOWORD(w)==1){ShowSettings();return 0;}
    if(msg==WM_WTSSESSION_CHANGE){CaptureTimer::OnSessionChange(w);return 0;}
    if(msg==WM_QUERYENDSESSION){if(CaptureTimer::IsBusy())return FALSE;RestoreHidden();return TRUE;}
    if(msg==taskbarCreated){hidden.clear();notificationGroups.clear();notificationCount=0;for(auto& p:notificationIcons)DestroyIcon(p.second);notificationIcons.clear();SetTimer(h,RefreshTimer,30000,nullptr);Refresh();return 0;}
    if(msg==WM_DESTROY){shuttingDown=true;KillTimer(h,RefreshTimer);KillTimer(h,ReFoldTimer);ClosePopup();if(settingsWindow)DestroyWindow(settingsWindow);if(resourceWindow)DestroyWindow(resourceWindow);CaptureTimer::Shutdown();WTSUnRegisterSessionNotification(h);RestoreHidden();DeleteNotifications();FreeItems(items);PostQuitMessage(0);return 0;}
    if(msg==CaptureTimer::CompletionMessage()){CaptureTimer::OnCompletion();if(stopAfterCapture)PostMessageW(h,StopApp,0,0);return 0;}
    return DefWindowProcW(h,msg,w,l);
}

#ifndef NATIVE_TEST_BUILD
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR cmd,int) {
    baseDir=ExePath();baseDir.resize(baseDir.find_last_of(L'\\'));configPath=baseDir+L"\\groups.ini";recoveryPath=baseDir+L"\\restore-icons.tsv";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring args=Trim(cmd?cmd:L"");
    HWND existing=FindWindowW(AppClass,nullptr);
    if(args==L"--stop"){if(existing)PostMessageW(existing,StopApp,0,0);return 0;}
    if(args==L"--reload"){if(existing)PostMessageW(existing,RefreshApp,0,0);return 0;}
    if(args==L"--restore"){if(existing)PostMessageW(existing,StopApp,0,0);else RecoverIcons();return 0;}
    if(existing){if(args!=L"--background")PostMessageW(existing,WM_COMMAND,1,0);return 0;}
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\Crli.TrayGroups.Native.Instance");if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){if(mutex)CloseHandle(mutex);return 0;}
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&common);
    LOGFONTW font={};NONCLIENTMETRICSW metrics={};metrics.cbSize=sizeof(metrics);if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0))font=metrics.lfMessageFont;else{font.lfHeight=-14;wcscpy_s(font.lfFaceName,L"Microsoft YaHei UI");}appFont=CreateFontIndirectW(&font);
    appIcon=MakeAppIcon();
    struct Registration{const wchar_t* name;WNDPROC proc;UINT style;};Registration classes[]={{AppClass,MainProc,0},{PopupClass,PopupProc,CS_DBLCLKS},{L"Crli.TrayGroups.Resources",ResourceProc,0}};
    for(auto c:classes){WNDCLASSW wc={};wc.lpfnWndProc=c.proc;wc.hInstance=instance;wc.lpszClassName=c.name;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.style=c.style;wc.hIcon=appIcon;RegisterClassW(&wc);}
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");LoadSettings();if(GetFileAttributesW(configPath.c_str())==INVALID_FILE_ATTRIBUTES)SaveSettings();RecoverIcons();mainWindow=CreateWindowExW(WS_EX_TOOLWINDOW,AppClass,L"TrayGroups 托盘分组器",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!mainWindow){CloseHandle(mutex);return 1;}ShowWindow(mainWindow,SW_HIDE);CaptureTimer::Initialize(mainWindow,instance,baseDir);WTSRegisterSessionNotification(mainWindow,NOTIFY_FOR_THIS_SESSION);Refresh();SetTimer(mainWindow,RefreshTimer,30000,nullptr);if(args!=L"--background")ShowSettings();
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(settingsWindow&&IsDialogMessageW(settingsWindow,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(appFont)DeleteObject(appFont);if(appIcon)DestroyIcon(appIcon);ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}

#endif
