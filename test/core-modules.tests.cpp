// 只链接配置和身份模块；不包含主程序，也不枚举/修改真实托盘图标。
#include "../src/config/TrayConfig.h"
#include "../src/tray/TrayIdentity.h"
#include "../src/tray/TrayDiagnostics.h"
#include "../src/common/TraySupport.h"
#include <iostream>

static bool passed=true;
static void Check(bool value,const char* message) {
    std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';
    passed&=value;
}

int main() {
    using namespace TraySupport;
    std::wstring folder=ExePath();
    folder.resize(folder.find_last_of(L'\\'));
    folder+=L"\\core-modules";
    if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 2;
    std::wstring path=folder+L"\\settings.ini";

    TrayItem icon;
    icon.path=L"C:\\Tools=Local\\Same.exe";
    icon.process=L"Same.exe";
    icon.ownerClass=L"FirstClass";
    icon.id=7;
    icon.owner=(HWND)101;
    icon.runtime={1234,1000,icon.owner,icon.id};
    icon.callback=WM_APP+20;
    auto key=TrayIdentity::IconKey(icon,L"path-class-id");

    TrayConfig::Settings config;
    config.assignments[key]=L"工具";
    config.groupLabelSizes[L"工具"]=18;
    bool saved=config.Save(path);
    TrayConfig::Settings loaded;
    loaded.Load(path);
    config.assignments[key]=L"尚未应用";
    Check(saved&&loaded.assignments[key]==L"工具"&&loaded.GroupLabelSize(L"工具")==18,
          "independent settings objects retain Unicode rules without shared globals");

    WriteText(path,ReadText(path)+L"\n[Custom]\nValue=keep\n");
    bool positioned=config.SaveWindowPosition(path,-500,80);
    loaded.Load(path);
    Check(positioned&&loaded.assignments[key]==L"工具"&&loaded.settingsX==-500&&
          loaded.settingsY==80&&loaded.haveSettingsPosition&&ReadText(path).find(L"Value=keep")!=std::wstring::npos,
          "saving position preserves unapplied groups and unknown settings");
    bool paused=TrayConfig::PauseFolding(path);
    loaded.Load(path);
    Check(paused&&!loaded.foldIcons&&loaded.settingsX==-500&&loaded.assignments[key]==L"工具"&&
          ReadText(path).find(L"Value=keep")!=std::wstring::npos,
          "restore pause changes only the folding flag");

    TrayItem another=icon;
    another.ownerClass=L"SecondClass";
    Check(TrayIdentity::IconKey(another,L"path-class-id")!=key&&TrayIdentity::RulePath(key)==Lower(icon.path),
          "same executable icons stay distinct and escaped paths remain readable");

    TrayIdentity::MatchState state;
    TrayIdentity::Matcher matcher(loaded,state);
    loaded.pathAssignments[Lower(icon.path)]=L"整路径";
    std::vector<TrayItem> list={icon,icon};
    matcher.ResolveGroups(list);
    bool collision=list[0].pending&&list[1].pending&&list[0].group.empty();
    list={icon};
    matcher.ResolveGroups(list);
    Check(collision&&list[0].pending&&list[0].group.empty(),
          "ambiguous selectors remain pending instead of falling back to broad rules");

    TrayIdentity::MatchState separateState;
    TrayIdentity::Matcher separate(loaded,separateState);
    list={icon};
    separate.ResolveGroups(list);
    Check(!list[0].pending&&list[0].group==L"工具"&&state.pendingRules.count(key),
          "separate matchers cannot overwrite each other's runtime confirmations");
    another=icon;
    another.runtime.started++;
    list={another};
    separate.ResolveGroups(list);
    Check(list[0].pending&&list[0].group.empty()&&!TrayIdentity::SameRuntime(icon,another),
          "a restarted process cannot inherit an earlier runtime confirmation");
    separate.ForgetRule(key);
    loaded.assignments[key]=L"重新确认";
    separate.ResolveGroups(list);
    Check(!list[0].pending&&list[0].group==L"重新确认",
          "explicit reselection confirms a recreated icon through the module API");
    TrayItem guidIcon=icon;
    guidIcon.usesGuid=true;guidIcon.addressKnown=true;
    guidIcon.guid={0x12345678,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
    loaded.iconMatch=L"path-guid";loaded.assignments.clear();
    loaded.assignments[TrayIdentity::IconKey(guidIcon,loaded.iconMatch)]=L"GUID";
    TrayIdentity::MatchState guidState;
    TrayIdentity::Matcher guidMatcher(loaded,guidState);
    list={guidIcon};guidMatcher.ResolveGroups(list);
    guidIcon.owner=(HWND)202;guidIcon.runtime.owner=guidIcon.owner;guidIcon.runtime.started++;
    list={guidIcon};guidMatcher.ResolveGroups(list);
    Check(!list[0].pending&&list[0].group==L"GUID",
          "same path and real GUID can rebind after window or process recreation");
    guidIcon.guid.Data1++;list={guidIcon};guidMatcher.ResolveGroups(list);
    Check(list[0].group==L"整路径",
          "a changed GUID does not inherit the earlier per-icon assignment");
    TrayConfig::Settings auditConfig;
    TrayItem liveIcon=icon;liveIcon.path=ExePath();liveIcon.usesGuid=true;liveIcon.addressKnown=true;
    liveIcon.guid={0x13572468,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
    auto currentKey=TrayIdentity::IconKey(liveIcon,L"path-guid");
    auto oldIcon=liveIcon;oldIcon.guid.Data1++;
    auto oldKey=TrayIdentity::IconKey(oldIcon,L"path-guid");
    auditConfig.assignments[oldKey]=L"工具";
    auto outdated=TrayDiagnostics::UnmatchedRules(auditConfig,{liveIcon});
    Check(outdated.size()==1&&outdated[0].reason.find(L"已经变化")!=std::wstring::npos,
          "diagnostics detects changed identity under an existing executable path");
    std::wstring error;
    auto draft=auditConfig;
    bool rebound=outdated.size()==1&&TrayDiagnostics::Rebind(draft,outdated[0],liveIcon,{liveIcon},error);
    Check(rebound&&draft.assignments[currentKey]==L"工具"&&!draft.assignments.count(oldKey)&&auditConfig.assignments.count(oldKey),
          "rebind preserves group, removes old key and leaves original settings untouched");
    auto duplicate=liveIcon;duplicate.owner=(HWND)303;duplicate.runtime.owner=duplicate.owner;
    draft=auditConfig;
    Check(!TrayDiagnostics::Rebind(draft,outdated[0],liveIcon,{liveIcon,duplicate},error)&&draft.assignments==auditConfig.assignments,
          "ambiguous destination cannot be rebound or mutate the draft");
    auto restarted=liveIcon;restarted.runtime.started++;
    Check(!TrayDiagnostics::Rebind(draft,outdated[0],liveIcon,{restarted},error),
          "a recreated destination is rejected before rebind");
    draft.assignments[currentKey]=L"已有规则";
    Check(!TrayDiagnostics::Rebind(draft,outdated[0],liveIcon,{liveIcon},error)&&draft.assignments[currentKey]==L"已有规则",
          "rebind never overwrites an existing destination rule");
    draft=auditConfig;draft.assignments[oldKey]=L"";
    auto excluded=TrayDiagnostics::UnmatchedRules(draft,{liveIcon});
    Check(excluded.size()==1&&TrayDiagnostics::Rebind(draft,excluded[0],liveIcon,{liveIcon},error)&&draft.assignments.count(currentKey)&&draft.assignments[currentKey].empty(),
          "explicit ungroup exclusions survive rebind");
    Check(TrayDiagnostics::UnmatchedRules(draft,{liveIcon}).empty(),
          "an active per-icon rule is not listed as outdated");
    draft.assignments[L"path-id|c:\\nonexistent-traygroups-test\\missing.exe|9"]=L"旧规则";
    auto inactive=TrayDiagnostics::UnmatchedRules(draft,{liveIcon});
    Check(inactive.size()==1&&inactive[0].reason.find(L"未启用")!=std::wstring::npos,
          "inactive matching modes are explained instead of automatically migrated");
    TrayDiagnostics::Remove(draft,inactive[0]);
    auto details=TrayDiagnostics::IconDetails(liveIcon,L"path-guid",true);
    Check(details.find(TrayIdentity::GuidKey(liveIcon.guid))!=std::wstring::npos&&details.find(currentKey)!=std::wstring::npos,
          "icon details contain real GUID and the current exact matching key");
    liveIcon.usesGuid=false;liveIcon.addressKnown=false;
    Check(TrayDiagnostics::IconDetails(liveIcon,L"path-guid",true).find(L"未取得")!=std::wstring::npos,
          "unknown registration is not falsely labelled as an icon without GUID");
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    {NativeHandles::Handle first(event);NativeHandles::Handle second(std::move(first));Check(!first&&second,"handle move transfers sole ownership");}
    DWORD flags=0;
    Check(!GetHandleInformation(event,&flags),"handle is closed automatically on scope exit");
    void* remote=nullptr;
    {NativeHandles::RemoteBuffer buffer(GetCurrentProcess(),4096);remote=buffer.Get();}
    MEMORY_BASIC_INFORMATION info={};
    Check(remote&&VirtualQueryEx(GetCurrentProcess(),remote,&info,sizeof(info))&&info.State==MEM_FREE,
          "remote allocation is released automatically");
    DWORD iconBaseline=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    bool copied=false,moved=false;
    {
        TrayItem original;original.image=CopyIcon(LoadIconW(nullptr,MAKEINTRESOURCEW(32512)));
        TrayItem copy=original;copied=copy.image&&copy.image!=original.image;
        HICON transferred=copy.image;
        TrayItem move=std::move(copy);moved=move.image==transferred&&!copy.image;
        std::vector<TrayItem> owned={original,move};
        original.image=nullptr;
        ICONINFO imageInfo={};copied&=GetIconInfo(owned[0].image,&imageInfo)!=FALSE;
        if(imageInfo.hbmColor)DeleteObject(imageInfo.hbmColor);if(imageInfo.hbmMask)DeleteObject(imageInfo.hbmMask);
    }
    Check(copied&&moved&&GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==iconBaseline,
          "icon copies own independent handles, moves transfer ownership and vector destruction leaks no icons");
    DeleteFileW(path.c_str());
    return passed?0:1;
}
