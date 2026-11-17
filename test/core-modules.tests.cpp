// 只链接配置和身份模块；不包含主程序，也不枚举/修改真实托盘图标。
#include "../src/config/TrayConfig.h"
#include "../src/tray/TrayIdentity.h"
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
    DeleteFileW(path.c_str());
    return passed?0:1;
}
