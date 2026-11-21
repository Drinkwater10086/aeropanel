#pragma once
#include <tlhelp32.h>
#include <commdlg.h>
#include <shlobj.h>
#include <objbase.h>
#include <io.h>
#include "ResolutionPolicy.h"
namespace resolution {
struct Config {bool enabled=false,automatic=true,startup=false;int platform=2,width=2880,height=2160,refreshHz=0;std::wstring path,monitor;};
struct SavedMode {DWORD magic=0x44535532,version=1;wchar_t device[32]={};DEVMODEW mode={};};
static Config config,session;
static Policy policy;
static SavedMode original;
static std::wstring recoveryFile;
static std::wstring startupOverride; // Isolated test backend; empty in the application.
static std::wstring status=L"自动切换已关闭";
static ULONGLONG nextPoll=0,retryAt=0,previewUntil=0;
static bool pendingRecovery=false,preview=false;
static unsigned long pollCount=0;
static std::vector<Config> foregroundRules;
static ULONGLONG nextDeltaScan=0;
static Presence ruleDeltaGame=Presence::Unknown,ruleDeltaLauncher=Presence::Unknown;
static std::wstring RulesPath(){return std::wstring(iniPath)+L".rules";}
static void ParseForegroundRule(const wchar_t* text,std::vector<Config>& rules){
    unsigned width=0,height=0,automatic=0;int end=0;
    if(swscanf(text,L"%u,%u,%u,%n",&width,&height,&automatic,&end)!=3||
       end<=0||!text[end]||width<320||width>16384||height<200||height>16384||automatic>1)return;
    Config rule;rule.platform=4;rule.enabled=true;rule.path=text+end;
    if(rule.path.rfind(L"@monitor:",0)==0){
        const auto end=rule.path.find(L',',9);
        if(end==std::wstring::npos||end==9||end+1==rule.path.size())return;
        rule.monitor=rule.path.substr(9,end-9);
        rule.path.erase(0,end+1);
    }
    if(rule.path.rfind(L"@hz:",0)==0){
        wchar_t* end=nullptr;
        unsigned long hz=wcstoul(rule.path.c_str()+4,&end,10);
        if(end==rule.path.c_str()+4||*end!=L','||!end[1]||hz<20||hz>1000)return;
        rule.refreshHz=(int)hz;
        rule.path=std::wstring(end+1);
    }
    if(rule.path.size()==8&&rule.path.rfind(L"@delta:",0)==0&&rule.path[7]>=L'0'&&rule.path[7]<=L'2'){
        rule.platform=rule.path[7]-L'0';
        rule.path.clear();
    }
    rule.width=(int)width;rule.height=(int)height;rule.automatic=automatic!=0;
    rules.push_back(std::move(rule));
}
static void ReadForegroundRules(){
    std::vector<Config> loaded;
    const auto path=RulesPath();
    FILE* file=_wfopen(path.c_str(),L"rb");
    if(file){
        // UTF-16LE code units on Windows, one rule per line.
        std::wstring line;
        wchar_t buffer[1024];
        size_t count;
        while((count=fread(buffer,sizeof(wchar_t),1024,file))!=0){
            for(size_t i=0;i<count;++i){
                if(buffer[i]==L'\n'){ParseForegroundRule(line.c_str(),loaded);line.clear();}
                else line+=buffer[i];
            }
        }
        const bool ok=!ferror(file)&&line.empty();
        fclose(file);
        if(!ok){Log(L"Resolution: could not read the rule file.");return;}
    }else{
        DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES||GetLastError()!=ERROR_FILE_NOT_FOUND){
            Log(L"Resolution: could not open the rule file.");return;
        }
        // Existing installations used at most 24 numbered INI slots.
        for(size_t i=0;i<24;++i){
            const auto key=std::to_wstring(i);
            wchar_t text[1200]={};
            GetPrivateProfileStringW(L"ForegroundResolution",key.c_str(),L"",text,1200,iniPath);
            ParseForegroundRule(text,loaded);
        }
    }
    foregroundRules.swap(loaded);
}
static bool SaveForegroundRules(const std::vector<Config>& rules){
    const auto path=RulesPath(),temp=path+L".tmp";
    FILE* file=_wfopen(temp.c_str(),L"wb");
    if(!file)return false;
    bool ok=true;
    for(const auto& rule:rules){
        const auto target=rule.platform<3?L"@delta:"+std::to_wstring(rule.platform):rule.path;
        const auto line=std::to_wstring(rule.width)+L","+std::to_wstring(rule.height)+L","+
            std::to_wstring(rule.automatic)+L","+(rule.monitor.empty()?L"":L"@monitor:"+rule.monitor+L",")+(rule.refreshHz?L"@hz:"+std::to_wstring(rule.refreshHz)+L",":L"")+target+L"\n";
        if(fwrite(line.data(),sizeof(wchar_t),line.size(),file)!=line.size()){ok=false;break;}
    }
    if(fflush(file)||_commit(_fileno(file)))ok=false;
    if(fclose(file))ok=false;
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());return false;}
    foregroundRules=rules;
    nextPoll=retryAt=nextDeltaScan=0;
    return true;
}
static const Config* MatchForegroundRule(const std::wstring& path){
    if(path.empty())return nullptr;
    for(const auto& rule:foregroundRules)if(!_wcsicmp(rule.path.c_str(),path.c_str()))return &rule;
    return nullptr;
}
static std::wstring ForegroundExecutable(){
    static HWND previousWindow=nullptr;
    static DWORD previousPid=0;
    static ULONGLONG checkedAt=0;
    static std::wstring cachedPath;
    const HWND window=GetForegroundWindow();
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid||pid==GetCurrentProcessId())return L"";
    const auto now=GetTickCount64();
    if(window==previousWindow&&pid==previousPid&&now-checkedAt<2000)return cachedPath;
    previousWindow=window;previousPid=pid;checkedAt=now;cachedPath.clear();
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return L"";
    wchar_t path[1024];DWORD size=1024;
    if(QueryFullProcessImageNameW(process,0,path,&size))cachedPath.assign(path,size);
    CloseHandle(process);
    return cachedPath;
}
static void Fields(DEVMODEW& m){m.dmSize=sizeof(m);m.dmDriverExtra=0;m.dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFLAGS|DM_DISPLAYFREQUENCY;}
static bool Exists(const std::wstring& p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
static void Note(const std::wstring& text){status=text;Log((L"Resolution: "+text).c_str());}
struct MonitorChoice {std::wstring id,device,label;};
static std::vector<MonitorChoice> Monitors(){
    std::vector<MonitorChoice> result;
    for(DWORD i=0;;++i){
        DISPLAY_DEVICEW adapter={};adapter.cb=sizeof(adapter);
        if(!EnumDisplayDevicesW(NULL,i,&adapter,0))break;
        if(!(adapter.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))continue;
        DISPLAY_DEVICEW monitor={};monitor.cb=sizeof(monitor);
        for(DWORD j=0;EnumDisplayDevicesW(adapter.DeviceName,j,&monitor,EDD_GET_DEVICE_INTERFACE_NAME);++j){
            if((monitor.StateFlags&DISPLAY_DEVICE_ACTIVE)&&monitor.DeviceID[0]){
                std::wstring label=monitor.DeviceString;
                label+=L" ("+std::wstring(adapter.DeviceName+4)+L")";
                if(adapter.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)label+=L" · 主屏";
                result.push_back({monitor.DeviceID,adapter.DeviceName,std::move(label)});
                break;
            }
            monitor={};monitor.cb=sizeof(monitor);
        }
    }
    return result;
}
static bool CurrentDevice(const wchar_t* device,SavedMode& saved){
    if(!device||!*device||wcslen(device)>=32)return false;
    saved={};wcscpy(saved.device,device);
    saved.mode.dmSize=sizeof(saved.mode);
    return EnumDisplaySettingsExW(saved.device,ENUM_CURRENT_SETTINGS,&saved.mode,0)!=FALSE;
}
static bool Current(SavedMode& saved){
    for(DWORD i=0;;++i){
        DISPLAY_DEVICEW adapter={};adapter.cb=sizeof(adapter);
        if(!EnumDisplayDevicesW(NULL,i,&adapter,0))return false;
        if(adapter.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)return CurrentDevice(adapter.DeviceName,saved);
    }
}
struct DisplayBackend {
    bool (*current)(SavedMode&)=Current;
    decltype(&EnumDisplaySettingsExW) enumerate=EnumDisplaySettingsExW;
    decltype(&ChangeDisplaySettingsExW) change=ChangeDisplaySettingsExW;
};
static DisplayBackend display;
static bool CurrentFor(const Config& cfg,SavedMode& saved){
    if(cfg.monitor.empty())return display.current(saved);
    for(const auto& monitor:Monitors())
        if(!_wcsicmp(cfg.monitor.c_str(),monitor.id.c_str()))return CurrentDevice(monitor.device.c_str(),saved);
    return false;
}
static bool LoadRecovery(SavedMode& saved){
    FILE* f=_wfopen(recoveryFile.c_str(),L"rb");if(!f)return false;
    bool ok=fread(&saved,1,sizeof(saved),f)==sizeof(saved)&&fgetc(f)==EOF;fclose(f);
    return ok&&saved.magic==0x44535532&&saved.version==1&&saved.device[0]&&saved.device[31]==0&&
        saved.mode.dmSize==sizeof(DEVMODEW)&&saved.mode.dmPelsWidth>=320&&saved.mode.dmPelsWidth<=16384&&
        saved.mode.dmPelsHeight>=200&&saved.mode.dmPelsHeight<=16384&&saved.mode.dmBitsPerPel>=16;
}
static bool SaveRecovery(const SavedMode& saved){
    std::wstring temp=recoveryFile+L".tmp";FILE* f=_wfopen(temp.c_str(),L"wb");if(!f)return false;
    bool ok=fwrite(&saved,1,sizeof(saved),f)==sizeof(saved);
    if(fflush(f)||_commit(_fileno(f)))ok=false;if(fclose(f))ok=false;
    if(ok)ok=MoveFileExW(temp.c_str(),recoveryFile.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp.c_str());return ok;
}
static void ReadConfig(){
    config.enabled=GetPrivateProfileIntW(L"Resolution",L"Enabled",0,iniPath)!=0;
    config.automatic=GetPrivateProfileIntW(L"Resolution",L"Auto4x3",1,iniPath)!=0;
    config.platform=Clamp(GetPrivateProfileIntW(L"Resolution",L"Platform",2,iniPath),0,4);
    config.width=GetPrivateProfileIntW(L"Resolution",L"Width",2880,iniPath);
    config.height=GetPrivateProfileIntW(L"Resolution",L"Height",2160,iniPath);
    config.refreshHz=GetPrivateProfileIntW(L"Resolution",L"RefreshHz",0,iniPath);
    if(config.refreshHz<20||config.refreshHz>1000)config.refreshHz=0;
    wchar_t path[1024];GetPrivateProfileStringW(L"Resolution",L"Program",L"",path,1024,iniPath);config.path=path;GetPrivateProfileStringW(L"Resolution",L"Monitor",L"",path,1024,iniPath);config.monitor=path;
}
static std::wstring StartupPath(){if(!startupOverride.empty())return startupOverride;wchar_t p[MAX_PATH];if(FAILED(SHGetFolderPathW(NULL,CSIDL_STARTUP,NULL,SHGFP_TYPE_CURRENT,p)))return L"";return std::wstring(p)+L"\\AeroPanel.lnk";}
static bool SetStartup(bool enabled){
    std::wstring path=StartupPath();if(path.empty())return false;
    if(!enabled)return !Exists(path)||DeleteFileW(path.c_str());
    HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return false;
    IShellLinkW* link=NULL;IPersistFile* file=NULL;
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,IID_IShellLinkW,(void**)&link);
    wchar_t exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(SUCCEEDED(hr))hr=link->SetPath(exe);
    if(SUCCEEDED(hr))hr=link->SetWorkingDirectory(folder);
    if(SUCCEEDED(hr))hr=link->SetArguments(L"--background");
    if(SUCCEEDED(hr))hr=link->QueryInterface(IID_IPersistFile,(void**)&file);
    if(SUCCEEDED(hr))hr=file->Save(path.c_str(),TRUE);
    if(file)file->Release();if(link)link->Release();if(SUCCEEDED(init))CoUninitialize();return SUCCEEDED(hr);
}
static bool SaveConfig(const Config& value){
    if(value.platform<0||value.platform>4||value.width<320||value.width>16384||value.height<200||value.height>16384)return false;
    auto put=[&](const wchar_t* k,const std::wstring& v){return WritePrivateProfileStringW(L"Resolution",k,v.c_str(),iniPath)!=FALSE;};
    if(!put(L"Monitor",value.monitor)||!put(L"Program",value.path)||!put(L"Platform",std::to_wstring(value.platform))||
        !put(L"RefreshHz",std::to_wstring(value.refreshHz))||!put(L"Auto4x3",std::to_wstring(value.automatic))||!put(L"Width",std::to_wstring(value.width))||
        !put(L"Height",std::to_wstring(value.height)))return false;
    if((value.startup||Exists(StartupPath()))&&!SetStartup(value.startup))return false;
    if(!put(L"Enabled",std::to_wstring(value.enabled)))return false;
    config=value;nextPoll=0;retryAt=0;
    Note(policy.active&&session.platform!=4?L"设置已保存，本次目标退出并恢复后使用新配置":config.enabled?L"自动切换已启用，等待目标程序":L"自动切换已关闭");return true;
}
static bool Calculate4x3(DWORD desktopHeight,int& width,int& height){
    if(desktopHeight<200||desktopHeight>12288||desktopHeight%3)return false;
    height=(int)desktopHeight;
    width=height/3*4;
    return width>=320;
}
static bool FindMode(const Config& cfg,SavedMode& before,DEVMODEW& target,LONG& code,const SavedMode* baseline=nullptr){
    code=DISP_CHANGE_BADMODE;
    if(baseline)before=*baseline;
    else if(!CurrentFor(cfg,before)){code=DISP_CHANGE_FAILED;return false;}
    int width=cfg.width,height=cfg.height;
    if(cfg.automatic&&!Calculate4x3(before.mode.dmPelsHeight,width,height)){code=-100;return false;}
    if(width<320||width>16384||height<200||height>16384){code=-101;return false;}
    bool found=false;long diff=LONG_MAX;DEVMODEW mode={};
    for(DWORD i=0;;++i){mode={};mode.dmSize=sizeof(mode);if(!display.enumerate(before.device,i,&mode,0))break;
        if(mode.dmPelsWidth!=(DWORD)width||mode.dmPelsHeight!=(DWORD)height||mode.dmBitsPerPel!=before.mode.dmBitsPerPel)continue;
        if(cfg.refreshHz&&mode.dmDisplayFrequency!=(DWORD)cfg.refreshHz)continue;
        long delta=labs((long)mode.dmDisplayFrequency-(long)before.mode.dmDisplayFrequency);
        if(!found||delta<diff){target=mode;diff=delta;found=true;}
    }
    if(!found){target=before.mode;target.dmPelsWidth=width;target.dmPelsHeight=height;if(cfg.refreshHz)target.dmDisplayFrequency=(DWORD)cfg.refreshHz;}
    Fields(target);code=display.change(before.device,&target,NULL,CDS_TEST,NULL);return code==DISP_CHANGE_SUCCESSFUL;
}
static bool Restore(){
    SavedMode saved;if(!LoadRecovery(saved)){Note(L"恢复记录缺失或损坏；保持当前显示模式");return false;}
    Fields(saved.mode);LONG code=display.change(saved.device,&saved.mode,NULL,0,NULL);
    if(code!=DISP_CHANGE_SUCCESSFUL){Note(L"恢复失败，记录已保留，代码 "+std::to_wstring(code));return false;}
    if(!DeleteFileW(recoveryFile.c_str())){Note(L"模式已恢复，但恢复记录无法清除；自动切换暂停");pendingRecovery=true;return false;}
    preview=false;previewUntil=0;pendingRecovery=false;policy.active=false;
    Note(L"已恢复切换前的显示模式");return true;
}
static bool Apply(const Config& value,bool isPreview){
    if(Exists(recoveryFile)){pendingRecovery=true;Note(L"存在恢复记录，请先恢复原显示模式");return false;}
    SavedMode before;DEVMODEW target;LONG code;
    if(!FindMode(value,before,target,code)){Note(L"目标显示模式未通过验证，代码 "+std::to_wstring(code));return false;}
    if(target.dmPelsWidth==before.mode.dmPelsWidth&&target.dmPelsHeight==before.mode.dmPelsHeight&&target.dmDisplayFrequency==before.mode.dmDisplayFrequency){Note(L"当前模式已是目标模式，无需切换");return false;}
    if(!SaveRecovery(before)){Note(L"无法保存恢复记录，已取消切换");return false;}
    original=before;session=value;
    code=display.change(before.device,&target,NULL,0,NULL);
    if(code!=DISP_CHANGE_SUCCESSFUL){pendingRecovery=true;Note(L"切换失败，尝试恢复，代码 "+std::to_wstring(code));Restore();return false;}
    preview=isPreview;previewUntil=isPreview?GetTickCount64()+15000:0;
    Note(isPreview?L"试用中，15 秒后自动恢复":L"已切换为 "+std::to_wstring(target.dmPelsWidth)+L" × "+std::to_wstring(target.dmPelsHeight)+L" @ "+std::to_wstring(target.dmDisplayFrequency)+L" Hz");return true;
}
// Retain the original recovery record while moving between active rules.
static bool SwitchActiveRule(const Config& value){
    SavedMode baseline,current;
    if(!LoadRecovery(baseline)){
        pendingRecovery=true;
        Note(L"恢复记录缺失或损坏，已暂停自动切换");
        return false;
    }
    if(!CurrentFor(value,current)){
        Note(L"目标显示器不可用，尝试恢复原显示模式");
        Restore();return false;
    }
    if(_wcsicmp(current.device,baseline.device)){
        if(!Restore()||!Apply(value,false))return false;
        policy.active=true;
        return true;
    }
    DEVMODEW target={};LONG code;
    if(!FindMode(value,baseline,target,code,&baseline)){
        Note(L"新规则未通过模式验证，代码 "+std::to_wstring(code));
        Restore();
        return false;
    }
    const auto& actual=current.mode;
    const bool sameMode=target.dmPelsWidth==actual.dmPelsWidth&&target.dmPelsHeight==actual.dmPelsHeight&&
        target.dmDisplayFrequency==actual.dmDisplayFrequency&&target.dmBitsPerPel==actual.dmBitsPerPel;
    if(!sameMode){
        code=display.change(baseline.device,&target,NULL,0,NULL);
        if(code!=DISP_CHANGE_SUCCESSFUL){
            Note(L"切换新规则失败，代码 "+std::to_wstring(code));
            Restore();
            return false;
        }
    }
    session=value;
    Note(L"已应用新规则： "+std::to_wstring(target.dmPelsWidth)+L" × "+std::to_wstring(target.dmPelsHeight));
    return true;
}
struct ProcessInfo {std::wstring name,path;DWORD session=0;bool identifiable=false;};
static Presence MatchPath(const Config& cfg,const std::vector<ProcessInfo>& entries,DWORD ownSession){
    size_t slash=cfg.path.find_last_of(L"\\/");std::wstring name=cfg.path.substr(slash==std::wstring::npos?0:slash+1);
    if(name.empty())return Presence::Unknown;bool unknown=false;
    for(const auto& p:entries){if(_wcsicmp(name.c_str(),p.name.c_str()))continue;
        if(!p.identifiable){unknown=true;continue;}if(p.session!=ownSession)continue;
        if(!_wcsicmp(cfg.path.c_str(),p.path.c_str()))return Presence::Present;
    }return unknown?Presence::Unknown:Presence::Absent;
}
static bool Scan(const Config& cfg,Presence& game,Presence& launcher){
    DWORD ownSession;if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession))return false;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W item={};item.dwSize=sizeof(item);bool ok=Process32FirstW(snapshot,&item)!=FALSE;
    game=launcher=Presence::Absent;std::vector<ProcessInfo> entries;
    size_t slash=cfg.path.find_last_of(L"\\/");std::wstring base=cfg.path.substr(slash==std::wstring::npos?0:slash+1);
    if(ok)do{
        bool isGame=!_wcsicmp(item.szExeFile,L"DeltaForceClient-Win64-Shipping.exe");
        bool isLauncher=(cfg.platform==0||cfg.platform==1)&&(!_wcsicmp(item.szExeFile,L"df_launcher.exe")||(cfg.platform==1&&!_wcsicmp(item.szExeFile,L"delta_force_launcher.exe")));
        bool isCustom=cfg.platform==3&&!_wcsicmp(item.szExeFile,base.c_str());
        if(!isGame&&!isLauncher&&!isCustom)continue;
        DWORD sid=0;bool knownSession=ProcessIdToSessionId(item.th32ProcessID,&sid)!=FALSE;
        if(knownSession&&sid!=ownSession)continue;
        if(cfg.platform!=3){if(isGame)game=knownSession?Presence::Present:Presence::Unknown;if(isLauncher)launcher=knownSession?Presence::Present:Presence::Unknown;continue;}
        if(isCustom){ProcessInfo info;info.name=item.szExeFile;info.session=sid;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,item.th32ProcessID);
            if(process){wchar_t path[1024];DWORD length=1024;
                if(knownSession&&QueryFullProcessImageNameW(process,0,path,&length)){info.path=path;info.identifiable=true;}CloseHandle(process);}
            else if(GetLastError()==ERROR_INVALID_PARAMETER)continue;
            entries.push_back(info);
        }
    }while(Process32NextW(snapshot,&item));
    DWORD end=GetLastError();CloseHandle(snapshot);if(!ok||end!=ERROR_NO_MORE_FILES)return false;
    if(cfg.platform==3){game=MatchPath(cfg,entries,ownSession);launcher=Presence::Absent;}return true;
}
static std::wstring (*foregroundExecutable)()=ForegroundExecutable;
static bool (*scanProcesses)(const Config&,Presence&,Presence&)=Scan;
static bool ScanRuleProcesses(std::vector<ProcessInfo>& entries,DWORD& ownSession){
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession))return false;
    struct IgnoreCase {
        bool operator()(const std::wstring& a,const std::wstring& b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}
    };
    std::map<std::wstring,bool,IgnoreCase> names;
    auto add=[&](const Config& rule){
        if(!rule.path.empty())names.emplace(rule.path.substr(rule.path.find_last_of(L"\\/")+1),true);
    };
    if(config.enabled)for(const auto& rule:foregroundRules)add(rule);
    if(policy.active)add(session);
    if(names.empty())return true;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W item={sizeof(item)};
    bool ok=Process32FirstW(snapshot,&item)!=FALSE;
    if(ok)do{
        if(!names.count(item.szExeFile))continue;
        ProcessInfo info;info.name=item.szExeFile;
        const bool known=ProcessIdToSessionId(item.th32ProcessID,&info.session)!=FALSE;
        if(known&&info.session!=ownSession)continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,item.th32ProcessID);
        if(process){
            wchar_t path[1024];DWORD size=1024;
            if(known&&QueryFullProcessImageNameW(process,0,path,&size)){info.path.assign(path,size);info.identifiable=true;}
            CloseHandle(process);
        }else if(GetLastError()==ERROR_INVALID_PARAMETER)continue;
        entries.push_back(std::move(info));
    }while(Process32NextW(snapshot,&item));
    const DWORD end=GetLastError();CloseHandle(snapshot);
    return ok&&end==ERROR_NO_MORE_FILES;
}
static bool (*scanRuleProcesses)(std::vector<ProcessInfo>&,DWORD&)=ScanRuleProcesses;
static void Tick(){
    ULONGLONG now=GetTickCount64();
    if(preview){if(now>=previewUntil&&now>=retryAt){if(!Restore())retryAt=now+3000;}return;}
    if(pendingRecovery)return;
    if((policy.active&&session.platform==4)||(!policy.active&&config.platform==4)){
        if((!config.enabled&&!policy.active)||now<nextPoll)return;
        nextPoll=now+1000;
        if(now<retryAt)return;
        std::vector<ProcessInfo> entries;DWORD ownSession=0;
        if(!scanRuleProcesses(entries,ownSession))return;
        if(policy.active&&config.enabled&&MatchPath(session,entries,ownSession)!=Presence::Absent)return;
        const Config* match=nullptr;
        if(config.enabled){
            const auto* foreground=MatchForegroundRule(foregroundExecutable());
            if(foreground&&!foreground->path.empty()&&MatchPath(*foreground,entries,ownSession)==Presence::Present)
                match=foreground;
            else for(const auto& rule:foregroundRules)
                if(!rule.path.empty()&&MatchPath(rule,entries,ownSession)==Presence::Present){match=&rule;break;}
        }
        if(policy.active){
            if(match){if(!SwitchActiveRule(*match))retryAt=now+3000;}
            else if(!Restore())retryAt=now+5000;
        }else if(match){
            if(Apply(*match,false))policy.active=true;
            else retryAt=now+3000;
        }
        return;
    }
    if(pendingRecovery||(!config.enabled&&!policy.active)||now<nextPoll||now<retryAt)return;
    ++pollCount;nextPoll=now+(policy.active?1000:2000);
    const Config& cfg=policy.active?session:config;Presence game,launcher;
    if(!scanProcesses(cfg,game,launcher)){retryAt=now+5000;Note(L"进程枚举失败，保持当前显示模式");return;}
    if(config.platform==4&&cfg.platform<3){
        ruleDeltaGame=game;ruleDeltaLauncher=launcher;nextDeltaScan=now+1000;
    }
    Action action=policy.Decide(config.enabled,cfg.platform,game,launcher);
    if(game==Presence::Unknown&&action==Action::None){status=L"无法确认同名进程路径，保持当前显示模式";return;}
    if(action==Action::Apply){if(Apply(cfg,false))policy.Applied(game);else retryAt=now+30000;}
    if(action==Action::Restore){if(Restore())policy.Restored(launcher);else retryAt=now+5000;}
}
static void MigrateSingleRule(){
    if(config.platform==4)return;
    if(GetPrivateProfileIntW(L"Resolution",L"Platform",UINT(-1),iniPath)!=UINT(-1)&&
       (config.platform<3||!config.path.empty())){
        Config legacy=config;
        if(legacy.platform==3)legacy.platform=4;
        else legacy.path.clear();
        const auto existing=std::find_if(foregroundRules.begin(),foregroundRules.end(),[&](const Config& rule){
            return legacy.platform<3?rule.platform<3:
                rule.platform==4&&!_wcsicmp(rule.path.c_str(),legacy.path.c_str());
        });
        if(existing==foregroundRules.end())foregroundRules.push_back(std::move(legacy));
        else if(config.enabled)*existing=std::move(legacy);
        if(SaveForegroundRules(foregroundRules))
            WritePrivateProfileStringW(L"Resolution",L"Platform",L"4",iniPath);
    }
    config.platform=4;
}
static void Init(){
    recoveryFile=std::wstring(folder)+L"\\display-recovery.bin";ReadConfig();ReadForegroundRules();MigrateSingleRule();pendingRecovery=Exists(recoveryFile);
    if(pendingRecovery){Note(L"发现未恢复记录，请打开分辨率板块恢复");return;}
    HANDLE legacy=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\DeltaResolutionWatcher");
    if(legacy){CloseHandle(legacy);config.enabled=false;Note(L"旧版分辨率工具正在运行，请先退出旧版");return;}
    status=config.enabled?L"自动切换已启用，等待目标程序":L"自动切换已关闭";
}
static void Shutdown(){if(policy.active||preview)Restore();}
}
