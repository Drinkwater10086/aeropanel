#pragma once
#include <tlhelp32.h>
#include <string_view>
// PresentMon 2.3.1 --output_stdout --v1_metrics, verified against actual CSV.
static HANDLE fpsProcess=NULL,fpsPipe=NULL,fpsErrorPipe=NULL,fpsJob=NULL;
static DWORD fpsTarget=0,lastForeground=0,configuredTarget=0;
static bool fixedFpsTarget=false,captureEnabled=true;
static std::wstring fpsTargetName,foregroundName;
static std::string csvPending,errorPending,lastCaptureError;
static int pidColumn=-1,msColumn=-1,appColumn=-1,chainColumn=-1,runtimeColumn=-1;
static std::deque<double> frameTimes;
static double totalMs=0;
static unsigned long long totalFrames=0;
static ULONGLONG lastFrameTick=0,captureStarted=0;
static std::wstring fpsStatus=L"FPS：正在启动采集";
struct RecentFrame{double ms;ULONGLONG tick;};
struct FrameStream{std::wstring name,runtime;std::deque<RecentFrame> recent;ULONGLONG last=0;};
using FrameKey=std::pair<DWORD,std::string>;
static std::map<FrameKey,FrameStream> streams;
static FrameKey activeStream={0,""};

static std::vector<std::string> CsvRow(std::string_view line){
    std::vector<std::string> fields;std::string cell;bool quoted=false;
    for(size_t i=0;i<line.size();++i){char c=line[i];
        if(c=='"'){if(quoted&&i+1<line.size()&&line[i+1]=='"'){cell+='"';++i;}else quoted=!quoted;}
        else if(c==','&&!quoted){fields.push_back(cell);cell.clear();}else if(c!='\r')cell+=c;
    }fields.push_back(cell);return fields;
}
// Keep only the five columns the overlay uses, including quoted CSV cells.
static bool ReadFrameColumns(std::string_view line,const int (&wanted)[5],std::string (&values)[5]){
    const int last=*std::max_element(wanted,wanted+5);
    std::string* output=nullptr;
    auto select=[&](int column){
        output=nullptr;
        for(size_t i=0;i<5;++i)if(wanted[i]==column){output=&values[i];break;}
    };
    for(auto& value:values)value.clear();
    int column=0;
    bool quoted=false;
    select(column);
    for(size_t i=0;i<line.size();++i){
        char c=line[i];
        if(c=='"'){
            if(quoted&&i+1<line.size()&&line[i+1]=='"'){
                if(output)*output+='"';
                ++i;
            }else quoted=!quoted;
        }else if(c==','&&!quoted){
            if(column==last)return true;
            select(++column);
        }else if(output&&c!='\r')*output+=c;
    }
    return column>=last&&!quoted;
}
static std::wstring ProcessName(DWORD pid){
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t text[MAX_PATH]=L"";DWORD n=MAX_PATH;
    if(p){QueryFullProcessImageNameW(p,0,text,&n);CloseHandle(p);}
    if(!text[0]){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot!=INVALID_HANDLE_VALUE){PROCESSENTRY32W item={};item.dwSize=sizeof(item);
            if(Process32FirstW(snapshot,&item))do{if(item.th32ProcessID==pid){wcsncpy(text,item.szExeFile,MAX_PATH-1);break;}}while(Process32NextW(snapshot,&item));
            CloseHandle(snapshot);
        }
    }
    wchar_t* slash=wcsrchr(text,L'\\');return slash?slash+1:text;
}
static bool EligibleAutomatic(const FrameStream& stream){
    const wchar_t* names[]={L"dwm.exe",L"DesktopOverlayHost.exe",L"DesktopOverlayHost64.exe",L"RTSS.exe",L"RTSSDeskLite.exe",L"SensorPanel.exe",L"AeroPanel.exe",L"PresentMon.exe"};
    for(auto name:names)if(!_wcsicmp(stream.name.c_str(),name))return false;
    return true;
}
static void TrackForeground(){
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(!pid||pid==GetCurrentProcessId()||pid==lastForeground)return;
    lastForeground=pid;foregroundName=ProcessName(pid);
}
static bool IsFrameSource(DWORD id){return id>=0x50&&id<=0x56;}
static bool DesktopIdle(){
    if(!captureEnabled||fixedFpsTarget||configuredTarget)return false;
    const wchar_t* desktopNames[]={L"explorer.exe",L"ShellExperienceHost.exe",L"StartMenuExperienceHost.exe",L"SearchHost.exe"};
    for(auto name:desktopNames)if(!_wcsicmp(foregroundName.c_str(),name))return true;
    if(!fpsProcess||pidColumn<0)return false;
    ULONGLONG now=GetTickCount64();
    for(const auto& entry:streams)if(EligibleAutomatic(entry.second)&&now-entry.second.last<=2500)return false;
    return true;
}
static bool BrowserVideoActive(){
    return captureEnabled&&!fixedFpsTarget&&!configuredTarget&&
        !_wcsicmp(foregroundName.c_str(),L"chrome.exe")&&videoBridge::received&&
        GetTickCount64()-videoBridge::received<=2500;
}
static std::wstring FrameApiLabel(){
    if(BrowserVideoActive())return L"Video";
    if(DesktopIdle())return L"GDI";
    auto found=streams.find(activeStream);
    if(found==streams.end()||GetTickCount64()-found->second.last>2500)return L"FPS";
    auto detected=DetectedApi(activeStream.first);if(!detected.empty())return detected;
    const auto& runtime=found->second.runtime;
    if(runtime==L"D3D9")return L"D3D9";
    if(runtime==L"DXGI")return L"DXGI";
    // Kernel-only presents cannot reliably identify a graphics API.
    return L"FPS";
}
static double FrameValue(DWORD id){
    if(BrowserVideoActive()){
        switch(id){
        case 0x50:return videoBridge::fps;
        case 0x51:return videoBridge::ms;
        case 0x53:return videoBridge::average;
        case 0x55:return videoBridge::low>0?videoBridge::low:HUGE_VAL;
        default:return HUGE_VAL;
        }
    }
    // Desktop values describe this OSD's fixed 1 Hz refresh, not game rendering latency.
    if(DesktopIdle())return id==0x51?1000.0:IsFrameSource(id)?1.0:HUGE_VAL;
    if(!fpsProcess||frameTimes.empty()||GetTickCount64()-lastFrameTick>2500)return HUGE_VAL;
    if(id==0x50||id==0x51){ULONGLONG now=GetTickCount64();double recent=0;unsigned count=0;auto found=streams.find(activeStream);
        if(found!=streams.end())for(const auto& f:found->second.recent)if(now-f.tick<=1200){recent+=f.ms;++count;}
        if(!count){recent=frameTimes.back();count=1;}
        return id==0x50?1000.0*count/recent:recent/count;
    }
    if(id==0x53)return totalMs>0?1000.0*totalFrames/totalMs:HUGE_VAL;
    if(id==0x52)return 1000.0/ *std::max_element(frameTimes.begin(),frameTimes.end());
    if(id==0x54)return 1000.0/ *std::min_element(frameTimes.begin(),frameTimes.end());
    static std::vector<double> sorted;sorted.assign(frameTimes.begin(),frameTimes.end());
    size_t n=(size_t)ceil(sorted.size()*(id==0x55?0.01:0.001));if(!n)n=1;
    if(n<sorted.size())std::nth_element(sorted.begin(),sorted.begin()+n,sorted.end(),std::greater<double>());
    double sum=0;for(size_t i=0;i<n;++i)sum+=sorted[i];return 1000.0*n/sum;
}
static void SelectFrameStream(){
    ULONGLONG now=GetTickCount64();FrameKey best={0,""};int bestPriority=-1;size_t bestCount=0;
    DWORD pinned=fixedFpsTarget?fpsTarget:configuredTarget;
    for(const auto& entry:streams){const auto& key=entry.first;const auto& value=entry.second;
        if(now-value.last>2500||value.recent.empty())continue;
        const bool raw=key.second.rfind("raw-present-",0)==0;
        if(raw&&key.first!=(pinned?pinned:lastForeground))continue;
        int priority=0;
        if(pinned){if(key.first!=pinned)continue;priority=10;}
        else {if(!EligibleAutomatic(value))continue;
            if(key.first==lastForeground)priority=5;
            else if(!foregroundName.empty()&&!_wcsicmp(foregroundName.c_str(),value.name.c_str()))priority=4;
            else if(key==activeStream)priority=2;
        }
        priority=priority*2+!raw;
        if(priority>bestPriority||(priority==bestPriority&&value.recent.size()>bestCount)){
            best=key;bestPriority=priority;bestCount=value.recent.size();
        }
    }
    if(!best.first){
        fpsStatus=pinned?L"FPS：等待指定进程的帧事件":L"FPS：等待游戏或其他 3D 程序的帧事件";return;
    }
    const auto& stream=streams[best];
    if(best!=activeStream){activeStream=best;fpsTarget=best.first;fpsTargetName=stream.name;
        frameTimes.clear();totalMs=0;totalFrames=0;
        for(const auto& f:stream.recent){frameTimes.push_back(f.ms);totalMs+=f.ms;++totalFrames;}
        lastFrameTick=stream.last;std::wstring log=L"FPS target selected: "+fpsTargetName+L" PID="+std::to_wstring(fpsTarget);Log(log.c_str());
    }
    fpsStatus=L"FPS："+stream.name+L" · 已接收 "+std::to_wstring(totalFrames)+L" 帧";
}
static void StoreFrame(const FrameKey& key,FrameStream& stream,double ms){
    ULONGLONG tick=GetTickCount64();stream.last=tick;stream.recent.push_back({ms,tick});
    while(stream.recent.size()>512||(!stream.recent.empty()&&tick-stream.recent.front().tick>2000))stream.recent.pop_front();
    if(key==activeStream){frameTimes.push_back(ms);if(frameTimes.size()>4096)frameTimes.pop_front();totalMs+=ms;++totalFrames;lastFrameTick=tick;}
}
static void ReadRawPresent(const std::string& line){
    DWORD pid=0;unsigned event=0;unsigned long long chain=0;long long stamp=0;
    if(sscanf(line.c_str(),"PRESENT,%lu,%u,%llu,%lld",&pid,&event,&chain,&stamp)!=4||
        !pid||!chain||stamp<=0||(event!=1&&event!=42&&event!=178))return;
    DWORD pinned=fixedFpsTarget?fpsTarget:configuredTarget;
    if(pid!=(pinned?pinned:lastForeground))return;
    const auto name=pid==lastForeground?foregroundName:ProcessName(pid);
    FrameStream source;
    source.name=name;
    if(!pinned&&(name.empty()||!EligibleAutomatic(source)))return;
    static std::map<FrameKey,long long> previous;
    if(previous.size()>=128)previous.clear();
    // Keep legacy and detailed events apart. Some runtimes emit both per call.
    FrameKey key={pid,"raw-present-"+std::to_string(event)+"-"+std::to_string(chain)};
    auto& last=previous[key];const auto delta=stamp-last;const auto old=last;last=stamp;
    static const long long frequency=[](){LARGE_INTEGER f={};QueryPerformanceFrequency(&f);return f.QuadPart;}();
    if(!old||!frequency||delta<=0||delta>=frequency*2)return;
    if(streams.size()>=128&&!streams.count(key))return;
    auto& stream=streams[key];stream.name=name;stream.runtime=event==1?L"D3D9":L"DXGI";
    StoreFrame(key,stream,1000.0*delta/frequency);
}
static void ParseFrameLine(std::string_view line){
    if(pidColumn<0){auto fields=CsvRow(line);for(size_t i=0;i<fields.size();++i){
        if(!_stricmp(fields[i].c_str(),"ProcessID"))pidColumn=(int)i;
        if(!_stricmp(fields[i].c_str(),"msBetweenPresents"))msColumn=(int)i;
        if(!_stricmp(fields[i].c_str(),"Application"))appColumn=(int)i;
        if(!_stricmp(fields[i].c_str(),"Runtime"))runtimeColumn=(int)i;
        if(!_stricmp(fields[i].c_str(),"SwapChainAddress"))chainColumn=(int)i;
    }if(pidColumn>=0&&msColumn>=0)Log(L"PresentMon schema verified: ProcessID, msBetweenPresents, SwapChainAddress.");return;}
    if(msColumn<0)return;
    const int columns[]={pidColumn,msColumn,appColumn,chainColumn,runtimeColumn};
    static std::string fields[5];
    if(!ReadFrameColumns(line,columns,fields))return;
    DWORD pid=strtoul(fields[0].c_str(),NULL,10);if(!pid||pid==GetCurrentProcessId())return;
    double ms=strtod(fields[1].c_str(),NULL);if(!(ms>0&&ms<10000&&isfinite(ms)))return;
    FrameKey key={pid,fields[3]};
    if(streams.size()>=128&&streams.find(key)==streams.end())return;
    auto& stream=streams[key];
    if(runtimeColumn>=0)stream.runtime=fields[4]=="DXGI"?L"DXGI":fields[4]=="D3D9"?L"D3D9":L"";
    if(stream.name.empty()){stream.name=Wide(fields[2].c_str(),fields[2].size());
        if(stream.name.empty()||stream.name==L"<unknown>")stream.name=ProcessName(pid);
        size_t slash=stream.name.find_last_of(L"\\/");if(slash!=std::wstring::npos)stream.name.erase(0,slash+1);
        if(stream.name.empty())stream.name=L"PID "+std::to_wstring(pid);
    }
    StoreFrame(key,stream,ms);
}
static void ReadFrames(){
    ReadApi();
    if(!fpsProcess){if(!captureEnabled)fpsStatus=L"FPS：采集已关闭";return;}
    DWORD available=0;char buffer[16384];
    for(int n=0;n<64&&PeekNamedPipe(fpsPipe,NULL,0,NULL,&available,NULL)&&available;++n){
        DWORD got=0;if(!ReadFile(fpsPipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,NULL)||!got)break;
        csvPending.append(buffer,got);size_t end,start=0;
        while((end=csvPending.find('\n',start))!=std::string::npos){ParseFrameLine(std::string_view(csvPending).substr(start,end-start));start=end+1;}
        if(start)csvPending.erase(0,start);
        if(csvPending.size()>65536)csvPending.clear();
    }
    if(fpsErrorPipe)for(int n=0;n<8&&PeekNamedPipe(fpsErrorPipe,NULL,0,NULL,&available,NULL)&&available;++n){
        DWORD got=0;if(!ReadFile(fpsErrorPipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,NULL)||!got)break;
        errorPending.append(buffer,got);size_t end;
        while((end=errorPending.find('\n'))!=std::string::npos){std::string line=errorPending.substr(0,end);errorPending.erase(0,end+1);
            if(!line.empty()){lastCaptureError=line.substr(0,400);std::wstring message=L"PresentMon: "+Wide(line.c_str(),std::min<size_t>(line.size(),500));Log(message.c_str());}}
        if(errorPending.size()>4096)errorPending.clear();
    }
    ULONGLONG now=GetTickCount64();for(auto it=streams.begin();it!=streams.end();)if(now-it->second.last>10000)it=streams.erase(it);else ++it;
    SelectFrameStream();DWORD code=0;
    if(pidColumn<0&&now-lastFrameTick>2500&&now-captureStarted>8000)fpsStatus=L"帧率数据暂不可用：采集器未收到画面帧";
    if(GetExitCodeProcess(fpsProcess,&code)&&code!=STILL_ACTIVE){
        CloseHandle(fpsProcess);fpsProcess=NULL;if(fpsPipe)CloseHandle(fpsPipe);fpsPipe=NULL;
        if(fpsErrorPipe)CloseHandle(fpsErrorPipe);fpsErrorPipe=NULL;if(fpsJob)CloseHandle(fpsJob);fpsJob=NULL;
        fpsStatus=L"FPS 采集器退出，代码 "+std::to_wstring(code);
        if(!lastCaptureError.empty())fpsStatus+=L"："+Wide(lastCaptureError.c_str(),lastCaptureError.size());Log(fpsStatus.c_str());
    }
}
static std::wstring PresentMonPath(){return std::wstring(folder)+L"\\runtime\\PresentMon.exe";}
static std::wstring SessionName(){return L"AeroPanel-"+std::to_wstring(GetCurrentProcessId());}
static void StopFrames(){
    StopApi();
    if(!fpsProcess)return;
    std::wstring command=L"\""+PresentMonPath()+L"\" --session_name "+SessionName()+L" --terminate_existing_session";
    STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(CreateProcessW(PresentMonPath().c_str(),&command[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,folder,&si,&pi)){
        WaitForSingleObject(pi.hProcess,3000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
    if(WaitForSingleObject(fpsProcess,3000)==WAIT_TIMEOUT)TerminateProcess(fpsProcess,0);
    CloseHandle(fpsProcess);fpsProcess=NULL;if(fpsPipe)CloseHandle(fpsPipe);fpsPipe=NULL;
    if(fpsErrorPipe)CloseHandle(fpsErrorPipe);fpsErrorPipe=NULL;if(fpsJob)CloseHandle(fpsJob);fpsJob=NULL;
    frameTimes.clear();streams.clear();activeStream={0,""};fpsStatus=L"FPS：采集已关闭";
}
static void StartFrames(){
    if(fpsProcess)return;
    if(!EnsureCaptureRuntime()){fpsStatus=L"FPS：内嵌采集器释放失败，请检查目录写权限";return;}
    if(GetFileAttributesW(PresentMonPath().c_str())==INVALID_FILE_ATTRIBUTES){fpsStatus=L"FPS：同目录缺少 PresentMon.exe";return;}
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE writePipe=NULL,errorWrite=NULL;
    if(!CreatePipe(&fpsPipe,&writePipe,&sa,65536)){fpsStatus=L"FPS：无法创建采集管道";return;}
    if(!CreatePipe(&fpsErrorPipe,&errorWrite,&sa,8192)){CloseHandle(fpsPipe);CloseHandle(writePipe);fpsPipe=NULL;return;}
    SetHandleInformation(fpsPipe,HANDLE_FLAG_INHERIT,0);SetHandleInformation(fpsErrorPipe,HANDLE_FLAG_INHERIT,0);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    // Keep display tracking: Delta Force emits Runtime=Other kernel presents,
    // which disappear with --no_track_display (verified with the actual game).
    std::wstring command=L"\""+PresentMonPath()+L"\" --output_stdout --v1_metrics --no_console_stats --no_track_input --no_track_gpu --session_name "+SessionName();
    STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={};si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    si.hStdOutput=writePipe;si.hStdError=errorWrite;si.hStdInput=input;
    BOOL ok=CreateProcessW(PresentMonPath().c_str(),&command[0],NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,folder,&si,&pi);
    CloseHandle(writePipe);CloseHandle(errorWrite);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
    if(!ok){CloseHandle(fpsPipe);CloseHandle(fpsErrorPipe);fpsPipe=fpsErrorPipe=NULL;fpsStatus=L"FPS：无法启动采集器";return;}
    fpsJob=CreateJobObjectW(NULL,NULL);if(fpsJob){JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit={};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(fpsJob,JobObjectExtendedLimitInformation,&limit,sizeof(limit));AssignProcessToJobObject(fpsJob,pi.hProcess);}
    fpsProcess=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    csvPending.clear();errorPending.clear();lastCaptureError.clear();pidColumn=msColumn=appColumn=chainColumn=runtimeColumn=-1;
    frameTimes.clear();streams.clear();activeStream={0,""};totalMs=0;totalFrames=0;lastFrameTick=0;captureStarted=GetTickCount64();fpsStatus=L"FPS：正在等待帧事件";
    StartApi();
    Log(L"Independent PresentMon capture started.");
}
