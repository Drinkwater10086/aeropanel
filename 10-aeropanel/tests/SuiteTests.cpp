#define wWinMain SuiteEntryNotUsed
#include "../src/AeroPanel.cpp"
#undef wWinMain
#include <assert.h>
static unsigned passed=0,failed=0;
static FILE* results;
static void Check(bool ok,const char* name){fprintf(results,"%s %s\n",ok?"PASS":"FAIL",name);fflush(results);if(ok)++passed;else ++failed;}
static resolution::SavedMode fakeMode;
static unsigned changed=0,checked=0;
static bool rejectCheck=false,rejectChange=false,rejectRestore=false;
static bool FakeCurrent(resolution::SavedMode& m){m=fakeMode;return true;}
static BOOL WINAPI FakeEnum(LPCWSTR,DWORD i,DEVMODEW* mode,DWORD){
    if(i>2)return FALSE;*mode=fakeMode.mode;mode->dmPelsWidth=2880;mode->dmDisplayFrequency=i==0?120:i==1?144:240;return TRUE;
}
static LONG WINAPI FakeChange(LPCWSTR,DEVMODEW* mode,HWND,DWORD flags,LPVOID){
    if(flags==CDS_TEST){++checked;return rejectCheck?DISP_CHANGE_BADMODE:DISP_CHANGE_SUCCESSFUL;}
    ++changed;if((mode->dmPelsWidth==3840&&rejectRestore)||(mode->dmPelsWidth!=3840&&rejectChange))return DISP_CHANGE_FAILED;
    fakeMode.mode=*mode;return DISP_CHANGE_SUCCESSFUL;
}
static void ResetFake(){
    fakeMode={};wcscpy(fakeMode.device,L"\\\\.\\DISPLAY1");fakeMode.mode.dmSize=sizeof(DEVMODEW);
    fakeMode.mode.dmPelsWidth=3840;fakeMode.mode.dmPelsHeight=2160;fakeMode.mode.dmBitsPerPel=32;fakeMode.mode.dmDisplayFrequency=144;
    resolution::policy={};resolution::pendingRecovery=false;resolution::preview=false;resolution::retryAt=resolution::nextPoll=0;
    changed=checked=0;rejectChange=rejectCheck=rejectRestore=false;
    DeleteFileW(resolution::recoveryFile.c_str());
    resolution::display.current=FakeCurrent;resolution::display.enumerate=FakeEnum;resolution::display.change=FakeChange;
}
static void PolicyTests(){
    using namespace resolution;Policy p;
    Check(p.Decide(true,0,Presence::Absent,Presence::Present)==Action::Apply,"steam_launcher_early_switch");p.Applied(Presence::Absent);
    Check(p.Decide(true,0,Presence::Absent,Presence::Present)==Action::None,"launcher_waits_for_game");
    Check(p.Decide(true,0,Presence::Present,Presence::Present)==Action::None&&p.gameSeen,"game_seen_latched");
    Check(p.Decide(true,0,Presence::Absent,Presence::Present)==Action::Restore,"game_exit_restore_with_launcher_open");p.Restored(Presence::Present);
    Check(p.Decide(true,0,Presence::Absent,Presence::Present)==Action::None,"launcher_not_retriggered");
    p.Decide(true,0,Presence::Absent,Presence::Absent);
    Check(p.Decide(true,1,Presence::Absent,Presence::Present)==Action::Apply,"official_launcher_new_session");
    p={};Check(p.Decide(true,2,Presence::Absent,Presence::Present)==Action::None,"wegame_waits_for_game");
    Check(p.Decide(true,2,Presence::Present,Presence::Absent)==Action::Apply,"wegame_game_switch");p.Applied(Presence::Present);
    Check(p.Decide(true,2,Presence::Unknown,Presence::Absent)==Action::None,"unknown_presence_keeps_active_mode");
    Check(p.Decide(false,2,Presence::Unknown,Presence::Absent)==Action::Restore,"disable_restores_even_when_access_denied");
    p={};Check(p.Decide(false,3,Presence::Present,Presence::Absent)==Action::None,"disabled_never_switches");
    Check(p.Decide(true,3,Presence::Present,Presence::Absent)==Action::Apply,"custom_target_switch");p.Applied(Presence::Present);
    Check(p.Decide(true,3,Presence::Absent,Presence::Absent)==Action::Restore,"custom_last_instance_exit_restore");
    Config c;c.platform=3;c.path=L"C:\\测试甲\\游戏.exe";
    std::vector<ProcessInfo> entries={{L"游戏.exe",L"C:\\测试乙\\游戏.exe",7,true}};
    Check(MatchPath(c,entries,7)==Presence::Absent,"same_basename_different_path_ignored");
    entries.push_back({L"游戏.exe",c.path,7,true});Check(MatchPath(c,entries,7)==Presence::Present,"unicode_exact_path_matches");
    Check(MatchPath(c,entries,8)==Presence::Absent,"other_windows_session_ignored");
    entries={{L"游戏.exe",L"",7,false}};Check(MatchPath(c,entries,7)==Presence::Unknown,"path_access_denial_is_unknown");
}
static void DisplayTests(){
    using namespace resolution;Config c;c.automatic=true;c.enabled=true;ResetFake();SavedMode before;DEVMODEW target;LONG code;
    Check(FindMode(c,before,target,code)&&target.dmPelsWidth==2880&&target.dmPelsHeight==2160&&target.dmDisplayFrequency==144,"auto_4x3_prefers_existing_refresh");
    Check(changed==0&&checked==1,"mode_check_never_changes_screen");
    Check(Apply(c,false)&&Exists(recoveryFile)&&fakeMode.mode.dmPelsWidth==2880,"apply_saves_recovery_before_change");
    SavedMode saved;Check(LoadRecovery(saved)&&saved.mode.dmPelsWidth==3840,"recovery_preserves_exact_original");
    Check(!Apply(c,false),"existing_recovery_blocks_overwrite");
    Check(Restore()&&!Exists(recoveryFile)&&fakeMode.mode.dmPelsWidth==3840,"restore_original_and_clear_record");
    ResetFake();rejectCheck=true;Check(!Apply(c,false)&&changed==0&&!Exists(recoveryFile),"unsupported_mode_cannot_switch");
    ResetFake();rejectChange=true;Check(!Apply(c,false)&&fakeMode.mode.dmPelsWidth==3840&&!Exists(recoveryFile),"switch_failure_rolls_back");
    ResetFake();rejectChange=rejectRestore=true;Check(!Apply(c,false)&&Exists(recoveryFile),"failed_rollback_keeps_recovery");
    rejectChange=rejectRestore=false;Check(Restore()&&!Exists(recoveryFile),"manual_recovery_after_failure");
    ResetFake();Config enabled=config;config.enabled=false;
    Check(Apply(c,true)&&preview,"preview_switches_temporarily");previewUntil=GetTickCount64()-1;Tick();
    Check(!preview&&fakeMode.mode.dmPelsWidth==3840&&!Exists(recoveryFile),"preview_timeout_restores_automatically");config=enabled;
    ResetFake();Apply(c,false);policy.Applied(Presence::Present);Shutdown();Check(!Exists(recoveryFile)&&!policy.active,"normal_exit_restores_active_session");
    ResetFake();FILE* bad=_wfopen(recoveryFile.c_str(),L"wb");fputs("corrupt",bad);fclose(bad);
    Check(!LoadRecovery(saved)&&!Restore()&&Exists(recoveryFile),"corrupt_recovery_is_not_applied");DeleteFileW(recoveryFile.c_str());
    resolution::display={};SavedMode current;Check(Current(current),"real_primary_display_readable");
    Fields(current.mode);LONG result=display.change(current.device,&current.mode,NULL,CDS_TEST,NULL);
    Check(result==DISP_CHANGE_SUCCESSFUL,"real_driver_accepts_current_mode_without_switch");
    c.automatic=true;bool supported=FindMode(c,before,target,code);
    fprintf(results,"INFO real_auto_4x3_supported=%d driver_code=%ld current=%lux%lu@%lu\n",supported,code,current.mode.dmPelsWidth,current.mode.dmPelsHeight,current.mode.dmDisplayFrequency);
    Config savedConfig=config,custom;custom.platform=3;custom.path=L"E:\\测试目录\\程序.exe";custom.enabled=false;custom.startup=Exists(StartupPath());custom.width=1920;custom.height=1440;custom.automatic=false;
    Check(SaveConfig(custom),"unicode_configuration_write");ReadConfig();
    Check(config.path==custom.path&&config.width==1920&&config.height==1440&&!config.enabled,"unicode_configuration_roundtrip");config=savedConfig;
    Check(SetStartup(true)&&Exists(StartupPath()),"startup_shortcut_created_in_test_directory");
    IShellLinkW* link=NULL;IPersistFile* persist=NULL;HRESULT init=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,IID_IShellLinkW,(void**)&link);
    wchar_t targetPath[MAX_PATH]={},ownPath[MAX_PATH]={};GetModuleFileNameW(NULL,ownPath,MAX_PATH);
    if(SUCCEEDED(hr))hr=link->QueryInterface(IID_IPersistFile,(void**)&persist);
    if(SUCCEEDED(hr))hr=persist->Load(StartupPath().c_str(),STGM_READ);
    if(SUCCEEDED(hr))hr=link->GetPath(targetPath,MAX_PATH,NULL,SLGP_RAWPATH);
    Check(SUCCEEDED(hr)&&!_wcsicmp(targetPath,ownPath),"startup_shortcut_targets_current_executable");
    if(persist)persist->Release();if(link)link->Release();if(SUCCEEDED(init))CoUninitialize();
    Check(SetStartup(false)&&!Exists(StartupPath()),"startup_shortcut_removed_in_test_directory");
}
static PROCESS_INFORMATION SpawnProbe(const std::wstring& path){
    std::wstring command=L"\""+path+L"\" --probe-child";STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={};
    CreateProcessW(path.c_str(),&command[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi);return pi;
}
static void EndProbe(PROCESS_INFORMATION& p){if(p.hProcess){TerminateProcess(p.hProcess,0);WaitForSingleObject(p.hProcess,3000);CloseHandle(p.hProcess);CloseHandle(p.hThread);p={};}}
static void ProcessTests(){
    using namespace resolution;wchar_t exe[MAX_PATH];GetModuleFileNameW(NULL,exe,MAX_PATH);
    std::wstring a=std::wstring(folder)+L"\\测试甲",b=std::wstring(folder)+L"\\测试乙";CreateDirectoryW(a.c_str(),NULL);CreateDirectoryW(b.c_str(),NULL);
    a+=L"\\probe.exe";b+=L"\\probe.exe";CopyFileW(exe,a.c_str(),FALSE);CopyFileW(exe,b.c_str(),FALSE);
    auto first=SpawnProbe(b);Config cfg;cfg.platform=3;cfg.path=a;Presence game,launcher;
    Check(first.hProcess&&Scan(cfg,game,launcher)&&game==Presence::Absent,"real_process_same_name_different_path");
    auto second=SpawnProbe(a);auto third=SpawnProbe(a);
    Check(second.hProcess&&third.hProcess&&Scan(cfg,game,launcher)&&game==Presence::Present,"real_two_target_instances_detected");
    Config old=config;ResetFake();config=cfg;config.enabled=true;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==2880,"real_process_drives_auto_switch_state_machine");
    config.path=b;nextPoll=0;Tick();
    Check(policy.active&&session.path==a,"active_target_change_is_deferred");
    EndProbe(second);Check(Scan(cfg,game,launcher)&&game==Presence::Present,"one_remaining_instance_keeps_target_alive");
    nextPoll=0;Tick();Check(policy.active,"one_remaining_real_instance_does_not_restore");
    EndProbe(third);Check(Scan(cfg,game,launcher)&&game==Presence::Absent,"last_target_instance_exit_detected");
    nextPoll=0;Tick();Check(!policy.active&&fakeMode.mode.dmPelsWidth==3840,"last_real_instance_exit_drives_restore");
    EndProbe(first);config=old;display={};
}
static std::wstring fakeForegroundPath;
static std::wstring FakeForeground(){return fakeForegroundPath;}
static resolution::Presence fakeDeltaGame=resolution::Presence::Absent;
static bool FakeDeltaScan(const resolution::Config&,resolution::Presence& game,resolution::Presence& launcher){
    game=fakeDeltaGame;launcher=resolution::Presence::Absent;return true;
}
static void ForegroundRuleTests(){
    using namespace resolution;
    auto savedConfig=config;
    auto savedRules=foregroundRules;
    ResetFake();
    foregroundExecutable=FakeForeground;
    Config a;a.platform=4;a.enabled=true;a.path=L"C:\\甲\\app.exe";
    Config b=a;b.path=L"C:\\乙\\app.exe";b.automatic=false;b.width=1920;b.height=1080;
    Check(SaveForegroundRules({a,b}),"foreground_rules_save");
    ReadForegroundRules();
    Check(foregroundRules.size()==2&&MatchForegroundRule(b.path)&&
        MatchForegroundRule(b.path)->height==1080,"foreground_rules_reload");
    config=a;
    fakeForegroundPath=a.path;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==2880,"foreground_first_rule_applies");
    fakeDeltaGame=Presence::Present;scanProcesses=FakeDeltaScan;
    fakeForegroundPath=b.path;nextPoll=0;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==2880,"foreground_background_process_retains_game_resolution");
    fakeDeltaGame=Presence::Absent;scanProcesses=Scan;
    fakeForegroundPath=b.path;nextPoll=0;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==1920,"foreground_second_rule_applies");
    SavedMode baseline;
    Check(changed==2&&LoadRecovery(baseline)&&baseline.mode.dmPelsWidth==3840,
        "foreground_rule_handoff_skips_desktop_and_retains_recovery");
    fakeForegroundPath=a.path;nextPoll=0;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==2880&&fakeMode.mode.dmPelsHeight==2160,
        "foreground_auto_rule_uses_original_height");
    rejectCheck=true;
    fakeForegroundPath=b.path;nextPoll=0;Tick();
    Check(!policy.active&&fakeMode.mode.dmPelsWidth==3840&&!Exists(recoveryFile),
        "foreground_unsupported_handoff_restores_desktop");
    rejectCheck=false;retryAt=0;
    fakeForegroundPath=b.path;nextPoll=0;Tick();
    fakeForegroundPath=L"C:\\other\\app.exe";nextPoll=0;Tick();
    Check(!policy.active&&fakeMode.mode.dmPelsWidth==3840&&!Exists(recoveryFile),"foreground_unmatched_full_path_restores_original");
    Config delta=a;delta.platform=2;delta.path.clear();
    Check(SaveForegroundRules({a,b,delta}),"delta_rule_save");
    ReadForegroundRules();
    Check(foregroundRules.size()==3&&foregroundRules.back().platform==2&&foregroundRules.back().path.empty(),
        "delta_rule_channel_persists_separately_from_exe");
    scanProcesses=FakeDeltaScan;
    fakeDeltaGame=Presence::Present;
    fakeForegroundPath=b.path;nextPoll=nextDeltaScan=0;Tick();
    Check(policy.active&&session.platform==2&&fakeMode.mode.dmPelsWidth==2880,"delta_rule_takes_priority");
    fakeForegroundPath.clear();nextPoll=0;Tick();
    Check(policy.active&&fakeMode.mode.dmPelsWidth==2880,"delta_rule_stays_active_when_unfocused");
    fakeDeltaGame=Presence::Absent;nextPoll=0;Tick();
    Check(!policy.active&&fakeMode.mode.dmPelsWidth==3840,"delta_rule_restores_on_game_exit");
    scanProcesses=Scan;
    std::vector<Config> largeRules;
    for(int i=0;i<200;++i){
        Config rule=b;
        rule.path=L"C:\\测试"+std::wstring(200,L'长')+std::to_wstring(i)+L".exe";
        largeRules.push_back(std::move(rule));
    }
    Check(SaveForegroundRules(largeRules),"foreground_more_than_24_rules_save");
    ReadForegroundRules();
    Check(foregroundRules.size()==200&&MatchForegroundRule(largeRules.back().path),
        "foreground_large_rule_file_round_trip");
    Check(SaveForegroundRules({}),"foreground_empty_rule_file_save");
    ReadForegroundRules();
    Check(foregroundRules.empty(),"foreground_empty_file_stays_empty");
    foregroundExecutable=ForegroundExecutable;
    config=savedConfig;
    SaveForegroundRules(savedRules);
    display={};
}
static void FrameTests(){
    fpsProcess=GetCurrentProcess();frameTimes.clear();streams.clear();activeStream={42,"chain"};fpsTarget=42;lastFrameTick=GetTickCount64();
    std::vector<double> original;for(int i=0;i<4096;++i){double ms=1.0+(i*73%509)/11.0;frameTimes.push_back(ms);original.push_back(ms);}
    std::sort(original.begin(),original.end(),std::greater<double>());
    for(DWORD id:{0x55u,0x56u}){size_t n=(size_t)ceil(original.size()*(id==0x55?0.01:0.001));double sum=0;for(size_t i=0;i<n;++i)sum+=original[i];
        Check(fabs(FrameValue(id)-1000*n/sum)<1e-9,id==0x55?"optimized_1_percent_matches_full_sort":"optimized_point1_percent_matches_full_sort");}
    frameTimes.clear();streams.clear();activeStream={};totalFrames=0;pidColumn=msColumn=appColumn=chainColumn=-1;
    ParseFrameLine("Application,ProcessID,SwapChainAddress,Runtime,msBetweenPresents");
    ParseFrameLine("game.exe,42,0,Other,2.5");configuredTarget=42;SelectFrameStream();
    Check(fabs(FrameValue(0x50)-400)<1e-9&&fabs(FrameValue(0x51)-2.5)<1e-9,"kernel_fallback_present_fps_and_frametime");
    ParseFrameLine("game.exe,42,0,Other,0");ParseFrameLine("game.exe,42,0,Other,nan");
    Check(frameTimes.size()==1,"invalid_frame_intervals_ignored");lastFrameTick=GetTickCount64()-3000;
    Check(!isfinite(FrameValue(0x50)),"stale_fps_not_reused");fpsProcess=NULL;configuredTarget=0;
    fpsProcess=GetCurrentProcess();streams.clear();frameTimes.clear();pidColumn=1;
    Check(FrameValue(0x50)==1.0&&FrameValue(0x51)==1000.0&&FrameApiLabel()==L"GDI","desktop_gdi_one_hz_refresh_values");
    auto& background=streams[{42,"background"}];background.name=L"game.exe";background.last=GetTickCount64();background.recent.push_back({5.0,background.last});
    foregroundName=L"explorer.exe";Check(DesktopIdle()&&FrameValue(0x51)==1000.0,"desktop_foreground_does_not_reuse_background_game_fps");
    pidColumn=-1;
    Check(FrameValue(0x50)==1.0&&FrameValue(0x51)==1000.0,"desktop_refresh_available_before_capture_schema");
    pidColumn=1;
    foregroundName.clear();streams.clear();
    configuredTarget=42;Check(!isfinite(FrameValue(0x50)),"pinned_target_never_uses_desktop_fallback");configuredTarget=0;
    pidColumn=-1;Check(!isfinite(FrameValue(0x50)),"capture_without_schema_never_fakes_one_fps");
    pidColumn=msColumn=appColumn=chainColumn=runtimeColumn=-1;
    ParseFrameLine("Application,ProcessID,SwapChainAddress,Runtime,msBetweenPresents");
    ParseFrameLine("game.exe,42,0,DXGI,5");configuredTarget=42;SelectFrameStream();
    Check(FrameApiLabel()==L"DXGI"&&FrameValue(0x50)==200.0,"runtime_label_and_live_fps");
    ParseFrameLine("game.exe,42,0,D3D9,5");Check(FrameApiLabel()==L"D3D9","d3d9_runtime_label");
    ParseFrameLine("game.exe,42,0,Other,5");Check(FrameApiLabel()==L"FPS","unknown_api_is_not_guessed");
    fpsProcess=NULL;configuredTarget=0;
    Check(apiidentity::StackMask({L"d3d11.dll",L"d3d10.dll"})==2,"api_d3d10_wrapper_not_mislabeled_d3d11");
    Check(apiidentity::StackMask({L"D3D11.dll"})==8,"api_case_insensitive_stack_module");
    Check(apiidentity::StackMask({L"nvoglv64.dll"})==0,"shared_driver_module_is_not_api_evidence");
    Check(apiidentity::Names(32|64)==L"OpenGL/Vulkan","mixed_api_evidence_retains_both_names");
    DWORD ownPid=GetCurrentProcessId();auto ownIdentity=apiidentity::ProcessIdentity(ownPid);
    apiEvidence[ownPid]={ownIdentity,16};Check(DetectedApi(ownPid)==L"D3D12","api_evidence_matches_process_creation_time");
    apiEvidence[ownPid]={ownIdentity+1,8};Check(DetectedApi(ownPid).empty(),"pid_reuse_cannot_reuse_old_api_evidence");
    Check(EnsureApiRuntime(),"single_exe_extracts_verified_api_detector");
    StartApi();Check(apiCollector.process&&apiCollector.pipe&&apiCollector.stop,"api_detector_starts_with_owned_pipe_and_stop_event");
    HANDLE detector=apiCollector.process;HANDLE detectorCopy=NULL;DuplicateHandle(GetCurrentProcess(),detector,GetCurrentProcess(),&detectorCopy,SYNCHRONIZE,FALSE,0);
    StopApi();Check(!apiCollector.process&&!apiCollector.pipe&&!apiCollector.stop&&detectorCopy&&WaitForSingleObject(detectorCopy,0)==WAIT_OBJECT_0,"api_detector_stops_and_releases_handles");if(detectorCopy)CloseHandle(detectorCopy);
    apiEvidence[ownPid]={ownIdentity,16};SaveApiEvidence(ownPid,apiEvidence[ownPid]);apiEvidence.clear();LoadApiEvidence();
    Check(DetectedApi(ownPid)==L"D3D12","verified_api_cache_survives_detector_restart");
    WritePrivateProfileStringW(L"APICache",std::to_wstring(ownPid).c_str(),(std::to_wstring(ownIdentity+1)+L",8").c_str(),iniPath);LoadApiEvidence();
    Check(DetectedApi(ownPid).empty(),"stale_api_cache_is_removed_on_load");
    StartApiChannel(apiBroker,true,false);ULONGLONG deadline=GetTickCount64()+3000;
    while(!ConnectedApi(apiBroker)&&GetTickCount64()<deadline)Sleep(10);
    HMODULE graphics=LoadLibraryW(L"d3d11.dll");auto address=graphics?GetProcAddress(graphics,"D3D11CreateDevice"):NULL;
    std::string query="STACK,"+std::to_string(ownPid)+","+std::to_string(ownIdentity)+","+std::to_string((ULONGLONG)(uintptr_t)address)+"\n";DWORD sent=0;
    bool sentOk=ConnectedApi(apiBroker)&&WriteFile(apiBroker.pipe,query.data(),(DWORD)query.size(),&sent,NULL)&&sent==query.size();
    deadline=GetTickCount64()+3000;while(DetectedApi(ownPid).empty()&&GetTickCount64()<deadline){ReadApiChannel(apiBroker);Sleep(10);}
    Check(sentOk&&DetectedApi(ownPid)==L"D3D11","module_mapping_broker_round_trip_and_client_pid_check");
    StopApi();if(graphics)FreeLibrary(graphics);
    Check(EnsureCaptureRuntime(),"single_exe_extracts_verified_capture_runtime");
    Check(EnsureCaptureRuntime(),"existing_capture_runtime_verified_and_reused");
    FILE* recorded=_wfopen((std::wstring(folder)+L"\\..\\tests\\game-fixture.csv").c_str(),L"rb");
    if(recorded){char line[8192];frameTimes.clear();streams.clear();activeStream={4242,"test-chain"};
        totalFrames=0;totalMs=0;pidColumn=msColumn=appColumn=chainColumn=-1;fpsProcess=GetCurrentProcess();
        while(fgets(line,sizeof(line),recorded)){std::string row=line;while(!row.empty()&&(row.back()=='\n'||row.back()=='\r'))row.pop_back();ParseFrameLine(row);}fclose(recorded);
        Check(totalFrames>2500&&totalMs>0,"replay_real_delta_force_capture_rows");
        Check(fabs(FrameValue(0x53)-379.8)<1,"real_delta_force_capture_fps_regression");fpsProcess=NULL;
    }else Check(false,"real_delta_force_capture_fixture_exists");
    std::vector<double> sample;for(int i=0;i<4096;++i)sample.push_back(1.0+(i*73%509)/11.0);
    LARGE_INTEGER frequency,start,end;QueryPerformanceFrequency(&frequency);volatile double sink=0;
    auto benchmark=[&](bool full){QueryPerformanceCounter(&start);std::vector<double> copy;copy.reserve(4096);
        for(int i=0;i<2000;++i){copy.assign(sample.begin(),sample.end());if(full)std::sort(copy.begin(),copy.end(),std::greater<double>());else std::nth_element(copy.begin(),copy.begin()+41,copy.end(),std::greater<double>());
            double sum=0;for(int j=0;j<41;++j)sum+=copy[j];sink=sum;}
        QueryPerformanceCounter(&end);return (end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart;};
    double fullMs=benchmark(true),partialMs=benchmark(false);(void)sink;
    fprintf(results,"BENCH percentile_2000_iterations full_sort_ms=%.3f partial_selection_ms=%.3f speed_ratio=%.3f\n",fullMs,partialMs,fullMs/partialMs);
}
static bool SaveSettingsPreview(HWND window){
    RECT r;GetClientRect(window,&r);BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);void* pixels=NULL;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    HGDIOBJ old=SelectObject(dc,bitmap);
    SendMessageW(sensorList,LB_SETSEL,FALSE,-1);
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    PrintWindow(window,dc,PW_CLIENTONLY);GdiFlush();
    ShowWindow(window,SW_HIDE);
    RECT list;GetWindowRect(sensorList,&list);POINT listPoint={list.right-GetSystemMetrics(SM_CXVSCROLL)-5,(list.top+list.bottom)/2};ScreenToClient(window,&listPoint);
    bool ok=GetPixel(dc,r.right-4,r.bottom-4)!=RGB(0,0,0)&&GetPixel(dc,listPoint.x,listPoint.y)==RGB(35,39,45);
    std::wstring path=std::wstring(folder)+L"\\settings-preview.bmp";FILE* file=_wfopen(path.c_str(),L"wb");
    if(file){BITMAPFILEHEADER header={};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
        DWORD bytes=r.right*r.bottom*4;header.bfSize=header.bfOffBits+bytes;
        ok=ok&&fwrite(&header,sizeof(header),1,file)==1&&fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file)==1&&fwrite(pixels,bytes,1,file)==1;fclose(file);
    }else ok=false;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
static void UiTests(){
    fontHandle=CreateFontW(-16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Microsoft YaHei UI");settingsBrush=CreateSolidBrush(RGB(35,39,45));
    WNDCLASSW wc={};wc.hInstance=appInstance;wc.lpfnWndProc=PickerProc;wc.lpszClassName=L"SuiteTestsPicker";RegisterClassW(&wc);
    ReadSensors();SetLocal(L"SilverWolfBackground",1);
    HICON icon=CreateTrayIcon();ICONINFO iconInfo={};
    Check(icon&&GetIconInfo(icon,&iconInfo),"silver_wolf_tray_icon_resource_loads");
    if(iconInfo.hbmColor)DeleteObject(iconInfo.hbmColor);if(iconInfo.hbmMask)DeleteObject(iconInfo.hbmMask);if(icon)DestroyIcon(icon);
    picker=CreateWindowExW(0,wc.lpszClassName,L"Suite test",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,750,700,NULL,NULL,appInstance,NULL);
    Check(picker&&GetDlgItem(picker,80)&&GetDlgItem(picker,81)&&GetDlgItem(picker,115),"two_sections_created_in_same_window");
    Check(settingsArtworkDC&&settingsArtworkWidth==640&&settingsArtworkHeight==480,"preblurred_background_loaded_at_fixed_size");
    BITMAP artwork={};GetObjectW(settingsArtworkBitmap,sizeof(artwork),&artwork);
    fprintf(results,"INFO background_bitmap_bytes=%ld runtime_blur_passes=0\n",artwork.bmWidthBytes*artwork.bmHeight);
    SendMessageW(GetDlgItem(picker,83),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(picker,WM_COMMAND,83,0);
    Check(!settingsArtworkDC&&!settingsArtworkBitmap&&Setting(L"SilverWolfBackground",1)==0,"background_toggle_off_releases_bitmap_and_saves_setting");
    SendMessageW(GetDlgItem(picker,83),BM_SETCHECK,BST_CHECKED,0);SendMessageW(picker,WM_COMMAND,83,0);
    Check(settingsArtworkDC&&Setting(L"SilverWolfBackground",0)==1,"background_toggle_on_reloads_bitmap");
    SelectSettingsSection(1);Check((GetWindowLongW(GetDlgItem(picker,100),GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongW(GetDlgItem(picker,10),GWL_STYLE)&WS_VISIBLE),"resolution_tab_hides_monitor_controls");
    Check(resolution::SaveForegroundRules({}),"rule_table_test_starts_empty");
    resolutionui::RefreshRules();
    SendMessageW(picker,WM_COMMAND,123,0);
    SetDlgItemInt(picker,108,1280,FALSE);SetDlgItemInt(picker,110,960,FALSE);
    SendMessageW(picker,WM_COMMAND,127,0);
    resolutionui::SelectMonitor(L"test-monitor-id");
    resolutionui::SelectRefreshRate(144);SendMessageW(picker,WM_COMMAND,127,0);
    resolution::ReadForegroundRules();
    HWND table=GetDlgItem(picker,122);
    Check(SendMessageW(table,LVM_GETITEMCOUNT,0,0)==1&&resolution::foregroundRules.size()==1&&
        resolution::foregroundRules[0].width==1280&&resolution::foregroundRules[0].height==960&&
        resolution::foregroundRules[0].platform==0&&resolution::foregroundRules[0].refreshHz==144&&resolution::foregroundRules[0].monitor==L"test-monitor-id","rule_table_create_delta_steam");
    wchar_t cell[48]={};LVITEMW item={};item.iSubItem=4;item.pszText=cell;item.cchTextMax=48;
    SendMessageW(table,LVM_GETITEMTEXTW,0,(LPARAM)&item);
    Check(!wcscmp(cell,L"Steam"),"rule_table_virtual_channel_cell");
    SetDlgItemInt(picker,108,1600,FALSE);SendMessageW(picker,WM_COMMAND,127,0);
    Check(resolution::foregroundRules.size()==1&&resolution::foregroundRules[0].width==1600,
        "rule_table_update_selected_row");
    SendMessageW(picker,WM_COMMAND,124,0);
    Check(resolution::foregroundRules.empty()&&SendMessageW(table,LVM_GETITEMCOUNT,0,0)==0,
        "rule_table_delete_selected_row");
    int small=settingsFontPixels;SetWindowPos(picker,NULL,0,0,1920,1080,SWP_NOZORDER|SWP_NOACTIVATE);
    Check(settingsFontPixels>small,"settings_fonts_scale_up_with_window");
    SelectSettingsSection(0);Check((GetWindowLongW(sensorList,GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongW(GetDlgItem(picker,100),GWL_STYLE)&WS_VISIBLE),"monitor_tab_restores_its_controls");
    SetWindowPos(picker,NULL,0,0,1000,850,SWP_NOZORDER|SWP_NOACTIVATE);
    Check(SaveSettingsPreview(picker),"settings_window_renders_with_background_and_controls");
    bool fullscreenBefore=settingsFullscreen;ToggleSettingsFullscreen();Check(settingsFullscreen!=fullscreenBefore,"fullscreen_enter");ToggleSettingsFullscreen();Check(!settingsFullscreen,"fullscreen_exit_restores_window");
    DestroyWindow(picker);Check(!settingsFont&&!settingsPreviewFont,"closing_settings_releases_fonts");
    Check(!settingsArtworkDC&&!settingsArtworkBitmap&&!settingsIconSmall&&!settingsIconLarge,"closing_settings_releases_artwork_and_icons");
    DWORD handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<10;++i){HWND h=CreateWindowExW(0,wc.lpszClassName,L"Theme resource test",WS_OVERLAPPEDWINDOW,0,0,750,700,NULL,NULL,appInstance,NULL);if(h)DestroyWindow(h);}
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handles+2,"repeated_settings_open_close_does_not_accumulate_gdi_objects");
    DeleteObject(fontHandle);fontHandle=NULL;DeleteObject(settingsBrush);settingsBrush=NULL;
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&!wcscmp(argv[1],L"--probe-child")){Sleep(30000);return 0;}
    SetProcessDPIAware();appInstance=GetModuleHandleW(NULL);GetModuleFileNameW(NULL,folder,MAX_PATH);*wcsrchr(folder,L'\\')=0;
    swprintf(iniPath,MAX_PATH,L"%ls\\test.ini",folder);swprintf(logPath,MAX_PATH,L"%ls\\test.log",folder);
    resolution::recoveryFile=std::wstring(folder)+L"\\test-recovery.bin";
    resolution::startupOverride=std::wstring(folder)+L"\\test-startup.lnk";
    if(argc>1&&!wcscmp(argv[1],L"--restore-guard")){
        Sleep(10000);if(resolution::Exists(resolution::recoveryFile))return resolution::Restore()?0:1;return 0;
    }
    if(argc>1&&!wcscmp(argv[1],L"--live-display")){
        std::wstring report=std::wstring(folder)+L"\\live-display-result.txt";FILE* live=_wfopen(report.c_str(),L"wb");if(!live)return 2;
        if(resolution::Exists(resolution::recoveryFile)){fputs("FAIL existing recovery record\n",live);fclose(live);return 3;}
        resolution::SavedMode before;DEVMODEW target;LONG code;resolution::Config cfg;cfg.automatic=true;
        if(!resolution::FindMode(cfg,before,target,code)){fprintf(live,"FAIL driver test code=%ld\n",code);fclose(live);return 4;}
        wchar_t own[MAX_PATH];GetModuleFileNameW(NULL,own,MAX_PATH);std::wstring cmd=L"\""+std::wstring(own)+L"\" --restore-guard";
        STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION guard={};
        if(!CreateProcessW(own,&cmd[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&guard)){fputs("FAIL guard start\n",live);fclose(live);return 5;}
        bool applied=resolution::Apply(cfg,true);Sleep(3000);resolution::SavedMode during;bool readDuring=resolution::Current(during);
        bool restored=applied?resolution::Restore():false;resolution::SavedMode after;bool readAfter=resolution::Current(after);
        bool changedOk=readDuring&&during.mode.dmPelsWidth==target.dmPelsWidth&&during.mode.dmPelsHeight==target.dmPelsHeight;
        bool restoredOk=restored&&readAfter&&after.mode.dmPelsWidth==before.mode.dmPelsWidth&&after.mode.dmPelsHeight==before.mode.dmPelsHeight&&after.mode.dmDisplayFrequency==before.mode.dmDisplayFrequency;
        fprintf(live,"%s real_display_switch_and_restore\nbefore=%lux%lu@%lu\nduring=%lux%lu@%lu\nafter=%lux%lu@%lu\n",applied&&changedOk&&restoredOk?"PASS":"FAIL",before.mode.dmPelsWidth,before.mode.dmPelsHeight,before.mode.dmDisplayFrequency,during.mode.dmPelsWidth,during.mode.dmPelsHeight,during.mode.dmDisplayFrequency,after.mode.dmPelsWidth,after.mode.dmPelsHeight,after.mode.dmDisplayFrequency);
        fclose(live);WaitForSingleObject(guard.hProcess,15000);CloseHandle(guard.hThread);CloseHandle(guard.hProcess);return applied&&changedOk&&restoredOk?0:1;
    }
    std::wstring report=std::wstring(folder)+L"\\test-results.txt";results=_wfopen(report.c_str(),L"wb");if(!results)return 2;
    PolicyTests();DisplayTests();ProcessTests();ForegroundRuleTests();FrameTests();UiTests();
    fprintf(results,"SUMMARY passed=%u failed=%u\n",passed,failed);fclose(results);printf("passed=%u failed=%u\n",passed,failed);return failed?1:0;
}
