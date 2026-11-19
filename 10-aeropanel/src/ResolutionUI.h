#pragma once
namespace resolutionui {
static std::wstring lastStatus;
static bool visible=false;
static HWND Item(int id){return GetDlgItem(picker,id);}
static void EnableInputs(){
    bool custom=SendMessageW(Item(102),CB_GETCURSEL,0,0)==3;
    EnableWindow(Item(104),custom);EnableWindow(Item(105),custom);
    bool automatic=SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED;
    EnableWindow(Item(108),!automatic);EnableWindow(Item(110),!automatic);
}
static void Refresh(){
    const auto& c=resolution::config;
    SendMessageW(Item(100),BM_SETCHECK,c.enabled,0);SendMessageW(Item(102),CB_SETCURSEL,c.platform,0);
    SetWindowTextW(Item(104),c.path.c_str());
    SendMessageW(Item(106),BM_SETCHECK,c.automatic,0);SendMessageW(Item(107),BM_SETCHECK,!c.automatic,0);
    SetDlgItemInt(picker,108,c.width,FALSE);SetDlgItemInt(picker,110,c.height,FALSE);
    SendMessageW(Item(111),BM_SETCHECK,resolution::Exists(resolution::StartupPath()),0);EnableInputs();lastStatus.clear();
}
static void Create(HWND h){
    auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style=0){return SettingControl(h,cls,text,id,0,0,1,1,style);};
    add(L"STATIC",L"分辨率自动切换",121);
    add(L"BUTTON",L"启用自动切换（目标退出后恢复）",100,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"STATIC",L"监听模式",101);
    HWND combo=add(L"COMBOBOX",L"",102,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    for(auto name:{L"三角洲 · Steam",L"三角洲 · 官方启动器",L"三角洲 · WeGame / 游戏本体",L"指定其他程序（完整路径）"})SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)name);
    add(L"STATIC",L"目标 EXE",103);add(L"EDIT",L"",104,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
    SendMessageW(GetDlgItem(h,104),EM_SETLIMITTEXT,1023,0);add(L"BUTTON",L"浏览…",105,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"自动计算同高度 4:3",106,BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP);
    add(L"BUTTON",L"自定义分辨率",107,BS_AUTORADIOBUTTON|WS_TABSTOP);
    add(L"EDIT",L"2880",108,WS_BORDER|ES_NUMBER|WS_TABSTOP);add(L"STATIC",L"×",109);
    add(L"EDIT",L"2160",110,WS_BORDER|ES_NUMBER|WS_TABSTOP);
    add(L"BUTTON",L"登录 Windows 时启动合并程序",111,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"BUTTON",L"检查模式",112,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"试用 15 秒",113,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"恢复并暂停",114,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"保存设置",115,BS_OWNERDRAW|WS_TABSTOP);
    add(L"STATIC",L"",116);add(L"STATIC",L"",117);
    add(L"STATIC",L"仅切换主显示器。先检查模式，再启用；试用会自动恢复。\n修改监听目标会在本次目标退出后生效。\n无法确认进程完整路径时保持当前模式，避免提前恢复。",118);
    add(L"BUTTON",L"查看日志",119,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"关闭设置",120,BS_OWNERDRAW|WS_TABSTOP);
}
static void Show(bool show){visible=show;for(int id=100;id<=121;++id)if(Item(id))ShowWindow(Item(id),show?SW_SHOW:SW_HIDE);}
static void Layout(HWND h,double scale,int width,int height){
    auto s=[&](int v){return (int)lround(v*scale);};
    auto place=[&](int id,int x,int y,int w,int ht){MoveWindow(GetDlgItem(h,id),s(x),s(y+40),w<0?width-s(x)-s(22):s(w),s(ht),FALSE);};
    place(121,22,10,-1,25);place(100,22,48,-1,28);
    place(101,22,90,100,24);place(102,145,86,-1,180);
    place(103,22,133,100,24);place(104,145,129,-1,28);
    MoveWindow(GetDlgItem(h,104),s(145),s(169),width-s(145+22+100),s(28),FALSE);
    MoveWindow(GetDlgItem(h,105),width-s(112),s(169),s(90),s(28),FALSE);
    place(106,22,174,-1,28);place(107,22,212,155,28);
    place(108,194,212,110,28);place(109,314,214,22,26);place(110,346,212,110,28);
    place(111,22,254,-1,28);
    place(112,22,304,130,32);place(113,165,304,140,32);place(114,318,304,150,32);
    place(116,22,354,-1,52);place(117,22,410,-1,26);place(118,22,450,-1,85);
    MoveWindow(GetDlgItem(h,119),s(22),height-s(44),s(115),s(30),FALSE);
    MoveWindow(GetDlgItem(h,115),width-s(258),height-s(44),s(110),s(30),FALSE);
    MoveWindow(GetDlgItem(h,120),width-s(136),height-s(44),s(114),s(30),FALSE);
}
static void Status(){
    if(!visible||!picker)return;
    if(lastStatus!=resolution::status){lastStatus=resolution::status;SetWindowTextW(Item(116),lastStatus.c_str());}
}
static void CurrentMode(){
    if(!picker)return;resolution::SavedMode mode;
    std::wstring text=resolution::Current(mode)?L"当前主屏："+std::to_wstring(mode.mode.dmPelsWidth)+L" × "+std::to_wstring(mode.mode.dmPelsHeight)+L" @ "+std::to_wstring(mode.mode.dmDisplayFrequency)+L" Hz":L"无法读取当前显示模式";
    SetWindowTextW(Item(117),text.c_str());
}
static bool Read(resolution::Config& c,bool validateProgram){
    c=resolution::config;c.enabled=SendMessageW(Item(100),BM_GETCHECK,0,0)==BST_CHECKED;
    c.platform=(int)SendMessageW(Item(102),CB_GETCURSEL,0,0);c.automatic=SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED;
    c.startup=SendMessageW(Item(111),BM_GETCHECK,0,0)==BST_CHECKED;
    BOOL okW,okH;c.width=GetDlgItemInt(picker,108,&okW,FALSE);c.height=GetDlgItemInt(picker,110,&okH,FALSE);
    if(c.platform<0||c.platform>3||!okW||!okH||c.width<320||c.width>16384||c.height<200||c.height>16384){MessageBoxW(picker,L"请选择监听模式并填写有效宽高（320～16384 / 200～16384）。",L"设置",MB_ICONWARNING);return false;}
    wchar_t path[1024];GetDlgItemTextW(picker,104,path,1024);c.path=path;
    if(validateProgram&&c.platform==3){wchar_t normalized[1024]={};DWORD count=GetFullPathNameW(path,1024,normalized,NULL);
        DWORD attrs=GetFileAttributesW(normalized);size_t length=wcslen(normalized);
        wchar_t own[MAX_PATH];GetModuleFileNameW(NULL,own,MAX_PATH);
        if(!path[0]||!count||count>=1024||attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)||length<4||_wcsicmp(normalized+length-4,L".exe")||!_wcsicmp(normalized,own)){
            MessageBoxW(picker,L"请选择实际存在的 EXE，不能选择本程序自身。",L"设置",MB_ICONWARNING);return false;}c.path=normalized;
    }return true;
}
static bool Command(int id){
    if(id==102||id==106||id==107){EnableInputs();return true;}
    if(id==105){wchar_t path[1024]=L"";OPENFILENAMEW dialog={};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=picker;
        dialog.lpstrFilter=L"应用程序 (*.exe)\0*.exe\0\0";dialog.lpstrFile=path;dialog.nMaxFile=1024;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&dialog))SetWindowTextW(Item(104),path);return true;}
    if(id==112||id==113||id==115){resolution::Config c;if(!Read(c,id==115))return true;
        if(id==115){if(!resolution::SaveConfig(c))MessageBoxW(picker,L"保存失败，请查看日志。",L"设置",MB_ICONERROR);Status();return true;}
        if(id==113){if(resolution::policy.active||resolution::preview||resolution::pendingRecovery){MessageBoxW(picker,L"请先恢复当前模式，再进行试用。",L"设置",MB_ICONWARNING);return true;}
            resolution::Apply(c,true);Status();return true;}
        resolution::SavedMode before;DEVMODEW target;LONG code;
        bool ok=resolution::FindMode(c,before,target,code);
        std::wstring result=ok?L"驱动验证通过："+std::to_wstring(target.dmPelsWidth)+L" × "+std::to_wstring(target.dmPelsHeight)+L" @ "+std::to_wstring(target.dmDisplayFrequency)+L" Hz；尚未切换。":L"模式未通过驱动验证，代码 "+std::to_wstring(code)+L"；没有切换。";
        MessageBoxW(picker,result.c_str(),L"检查显示模式",ok?MB_ICONINFORMATION:MB_ICONWARNING);return true;
    }
    if(id==114){resolution::config.enabled=false;WritePrivateProfileStringW(L"Resolution",L"Enabled",L"0",iniPath);SendMessageW(Item(100),BM_SETCHECK,BST_UNCHECKED,0);
        if(resolution::Exists(resolution::recoveryFile))resolution::Restore();else resolution::Note(L"没有恢复记录，已暂停自动切换");Status();return true;}
    if(id==119){ShellExecuteW(picker,L"open",L"notepad.exe",logPath,NULL,SW_SHOWNORMAL);return true;}
    if(id==120){DestroyWindow(picker);return true;}
    return false;
}
}
