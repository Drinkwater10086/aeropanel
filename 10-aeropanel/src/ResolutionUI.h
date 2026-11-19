#pragma once
namespace resolutionui {
static std::wstring lastStatus;
static bool visible=false;
static HWND Item(int id){return GetDlgItem(picker,id);}
static const wchar_t* ruleKinds[]={L"类型：其他应用（按前台）",L"类型：三角洲 · Steam",L"类型：三角洲 · 官方启动器",L"类型：三角洲 · WeGame"};
static void RefreshRules(){
    SendMessageW(Item(122),CB_RESETCONTENT,0,0);
    if(resolution::foregroundRules.empty()){
        SendMessageW(Item(122),CB_ADDSTRING,0,(LPARAM)L"暂无已保存规则");
        SendMessageW(Item(122),CB_SETCURSEL,0,0);
        return;
    }
    SendMessageW(Item(122),CB_ADDSTRING,0,(LPARAM)L"选择已保存的规则…");
    SendMessageW(Item(122),CB_SETCURSEL,0,0);
    for(const auto& rule:resolution::foregroundRules){
        std::wstring filename=rule.platform<3?ruleKinds[rule.platform+1]:rule.path.substr(rule.path.find_last_of(L"\\/")+1);
        auto label=filename+L" · "+(rule.automatic?L"4:3":std::to_wstring(rule.width)+L" × "+std::to_wstring(rule.height));
        SendMessageW(Item(122),CB_ADDSTRING,0,(LPARAM)label.c_str());
    }
}
static void LoadRule(){
    auto index=SendMessageW(Item(122),CB_GETCURSEL,0,0)-1;
    if(index<0||(size_t)index>=resolution::foregroundRules.size())return;
    const auto& rule=resolution::foregroundRules[(size_t)index];
    SendMessageW(Item(125),CB_SETCURSEL,rule.platform<3?rule.platform+1:0,0);
    SetWindowTextW(Item(104),rule.path.c_str());
    SendMessageW(Item(106),BM_SETCHECK,rule.automatic,0);
    SendMessageW(Item(107),BM_SETCHECK,!rule.automatic,0);
    SetDlgItemInt(picker,108,rule.width,FALSE);
    SetDlgItemInt(picker,110,rule.height,FALSE);
}
static void EnableInputs(){
    auto mode=SendMessageW(Item(102),CB_GETCURSEL,0,0);
    bool custom=mode==3||(mode==4&&SendMessageW(Item(125),CB_GETCURSEL,0,0)==0);
    EnableWindow(Item(122),mode==4&&!resolution::foregroundRules.empty());
    EnableWindow(Item(125),mode==4);
    EnableWindow(Item(124),mode==4&&SendMessageW(Item(122),CB_GETCURSEL,0,0)>0);
    SetWindowTextW(Item(123),mode==4?L"添加 / 更新规则":L"编辑多应用规则…");
    SetWindowTextW(Item(118),mode==4?L"选规则类型 → 设置分辨率 → 添加规则 → 启用并保存设置。":
        L"仅切换主显示器。试用会自动恢复。");
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
    SendMessageW(Item(111),BM_SETCHECK,resolution::Exists(resolution::StartupPath()),0);RefreshRules();EnableInputs();lastStatus.clear();
}
static void Create(HWND h){
    auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style=0){return SettingControl(h,cls,text,id,0,0,1,1,style);};
    add(L"STATIC",L"分辨率自动切换",121);
    add(L"BUTTON",L"启用自动切换",100,BS_AUTOCHECKBOX|WS_TABSTOP);
    add(L"STATIC",L"监听模式",101);
    HWND combo=add(L"COMBOBOX",L"",102,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    for(auto name:{L"三角洲 · Steam",L"三角洲 · 官方启动器",L"三角洲 · WeGame / 游戏本体",L"指定其他程序（完整路径）",L"多应用规则（按前台窗口）"})SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)name);
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
    HWND kind=add(L"COMBOBOX",L"",125,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    for(auto name:ruleKinds)SendMessageW(kind,CB_ADDSTRING,0,(LPARAM)name);
    SendMessageW(kind,CB_SETCURSEL,0,0);
    add(L"COMBOBOX",L"",122,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"BUTTON",L"添加 / 更新规则",123,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"删除所选",124,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"查看已启用规则",126,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"查看日志",119,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"关闭设置",120,BS_OWNERDRAW|WS_TABSTOP);
}
static void Show(bool show){visible=show;for(int id=100;id<=126;++id)if(Item(id))ShowWindow(Item(id),show?SW_SHOW:SW_HIDE);}
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
    place(116,22,354,-1,52);place(117,22,410,-1,26);place(118,22,444,-1,26);
    place(125,22,472,300,140);
    place(126,334,472,200,30);
    place(122,22,508,300,140);place(123,334,508,145,30);place(124,491,508,105,30);
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
    const bool haveCurrent=resolution::Current(mode);
    std::wstring text=haveCurrent?L"当前主屏："+std::to_wstring(mode.mode.dmPelsWidth)+L" × "+std::to_wstring(mode.mode.dmPelsHeight)+L" @ "+std::to_wstring(mode.mode.dmDisplayFrequency)+L" Hz":L"无法读取当前显示模式";
    SetWindowTextW(Item(117),text.c_str());
    resolution::SavedMode baseline;
    const bool haveRecovery=resolution::LoadRecovery(baseline);
    if(!haveRecovery&&haveCurrent)baseline=mode;
    int width=0,height=0;
    std::wstring automatic=L"自动计算同高度 4:3";
    if((haveRecovery||haveCurrent)&&resolution::Calculate4x3(baseline.mode.dmPelsHeight,width,height))
        automatic+=L"（"+std::to_wstring(width)+L" × "+std::to_wstring(height)+L"）";
    else automatic+=L"（无法计算）";
    SetWindowTextW(Item(106),automatic.c_str());
}
static bool Read(resolution::Config& c,bool validateProgram){
    c=resolution::config;c.enabled=SendMessageW(Item(100),BM_GETCHECK,0,0)==BST_CHECKED;
    c.platform=(int)SendMessageW(Item(102),CB_GETCURSEL,0,0);c.automatic=SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED;
    c.startup=SendMessageW(Item(111),BM_GETCHECK,0,0)==BST_CHECKED;
    BOOL okW,okH;c.width=GetDlgItemInt(picker,108,&okW,FALSE);c.height=GetDlgItemInt(picker,110,&okH,FALSE);
    if(c.platform<0||c.platform>4||!okW||!okH||c.width<320||c.width>16384||c.height<200||c.height>16384){MessageBoxW(picker,L"请选择监听模式并填写有效宽高（320～16384 / 200～16384）。",L"设置",MB_ICONWARNING);return false;}
    wchar_t path[1024];GetDlgItemTextW(picker,104,path,1024);c.path=path;
    if(validateProgram&&(c.platform==3||c.platform==4)){wchar_t normalized[1024]={};DWORD count=GetFullPathNameW(path,1024,normalized,NULL);
        DWORD attrs=GetFileAttributesW(normalized);size_t length=wcslen(normalized);
        wchar_t own[MAX_PATH];GetModuleFileNameW(NULL,own,MAX_PATH);
        if(!path[0]||!count||count>=1024||attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)||length<4||_wcsicmp(normalized+length-4,L".exe")||!_wcsicmp(normalized,own)){
            MessageBoxW(picker,L"请选择实际存在的 EXE，不能选择本程序自身。",L"设置",MB_ICONWARNING);return false;}c.path=normalized;
    }return true;
}
static void ShowEnabledRules(){
    const auto& config=resolution::config;
    if(!config.enabled){
        MessageBoxW(picker,L"自动切换未启用。勾选「启用自动切换」并保存设置后，规则才参与监听。",
            L"已启用规则",MB_OK|MB_ICONINFORMATION);
        return;
    }
    resolution::SavedMode baseline;
    const bool haveBaseline=resolution::LoadRecovery(baseline)||resolution::Current(baseline);
    auto describe=[&](const resolution::Config& rule){
        std::wstring name=rule.platform<3?ruleKinds[rule.platform+1]+3:
            rule.path.substr(rule.path.find_last_of(L"\\/")+1);
        int width=rule.width,height=rule.height;
        if(rule.automatic&&(!haveBaseline||!resolution::Calculate4x3(baseline.mode.dmPelsHeight,width,height)))
            return name+L" · 自动 4:3（当前无法计算）";
        return name+L" · "+std::to_wstring(width)+L" × "+std::to_wstring(height)+(rule.automatic?L"（自动 4:3）":L"");
    };
    std::wstring message;
    if(config.platform==4){
        for(const auto& rule:resolution::foregroundRules)message+=describe(rule)+L"\n";
        if(message.empty())message=L"多应用监听已启用，但没有添加规则。";
    }else{
        message=L"当前使用单应用监听：\n"+describe(config)+L"\n\n多应用列表尚未启用。";
    }
    MessageBoxW(picker,message.c_str(),L"已启用规则",MB_OK|MB_ICONINFORMATION);
}
static bool Command(int id){
    if(id==126){ShowEnabledRules();return true;}
    if(id==125){EnableInputs();return true;}
    if(id==122){LoadRule();EnableInputs();return true;}
    if(id==123&&SendMessageW(Item(102),CB_GETCURSEL,0,0)!=4){
        SendMessageW(Item(102),CB_SETCURSEL,4,0);
        EnableInputs();
        SetFocus(Item(125));
        return true;
    }
    if(id==123||id==124){
        int selection=-1;
        auto rules=resolution::foregroundRules;
        if(id==124){
            auto selected=SendMessageW(Item(122),CB_GETCURSEL,0,0)-1;
            if(selected<0||(size_t)selected>=rules.size())return true;
            rules.erase(rules.begin()+selected);
            if(!rules.empty())selection=std::min((int)selected,(int)rules.size()-1);
        }else{
            resolution::Config rule;
            const auto kind=SendMessageW(Item(125),CB_GETCURSEL,0,0);
            if(kind<0||kind>3||!Read(rule,kind==0)||rule.platform!=4)return true;
            if(kind>0){rule.platform=(int)kind-1;rule.path.clear();}
            auto existing=std::find_if(rules.begin(),rules.end(),[&](const auto& item){
                if(rule.platform<3)return item.platform<3;
                return item.platform==4&&!_wcsicmp(rule.path.c_str(),item.path.c_str());
            });
            if(existing!=rules.end()){selection=(int)(existing-rules.begin());*existing=rule;}
            else if(rules.size()<resolution::maxForegroundRules){selection=(int)rules.size();rules.push_back(rule);}
            else {MessageBoxW(picker,L"最多支持 24 条规则。",L"分辨率规则",MB_ICONINFORMATION);return true;}
        }
        if(resolution::SaveForegroundRules(rules)){
            RefreshRules();
            if(selection>=0){SendMessageW(Item(122),CB_SETCURSEL,selection+1,0);LoadRule();}
            EnableInputs();
        }
        else MessageBoxW(picker,L"无法保存规则。",L"分辨率规则",MB_ICONERROR);
        return true;
    }
    if(id==102||id==106||id==107){EnableInputs();return true;}
    if(id==105){wchar_t path[1024]=L"";OPENFILENAMEW dialog={};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=picker;
        dialog.lpstrFilter=L"应用程序 (*.exe)\0*.exe\0\0";dialog.lpstrFile=path;dialog.nMaxFile=1024;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&dialog))SetWindowTextW(Item(104),path);return true;}
    if(id==112||id==113||id==115){resolution::Config c;if(!Read(c,id==115&&SendMessageW(Item(102),CB_GETCURSEL,0,0)==3))return true;
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
