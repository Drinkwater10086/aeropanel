#pragma once
namespace resolutionui {
static std::wstring lastStatus;
static bool visible=false;
static HWND Item(int id){return GetDlgItem(picker,id);}
static const wchar_t* channels[]={L"Steam",L"官方启动器",L"WeGame"};
static int editedRule=-1, tableAutoWidth=0, tableAutoHeight=0;
static bool updatingTable=false;
static std::vector<resolution::MonitorChoice> displayChoices;
static std::vector<int> cachedRates;
static std::wstring ratesDevice;
static int ratesWidth=0,ratesHeight=0;
static DWORD ratesDepth=0;
static void ReleaseRuleUI(){
    std::vector<resolution::MonitorChoice>().swap(displayChoices);
    std::vector<int>().swap(cachedRates);
    std::wstring().swap(ratesDevice);
    editedRule=-1;
}
static std::wstring SelectedMonitor(){
    const auto i=SendMessageW(Item(130),CB_GETCURSEL,0,0);
    return i>0&&(size_t)i<=displayChoices.size()?displayChoices[i-1].id:L"";
}
static void SelectMonitor(const std::wstring& id){
    int index=0;
    for(size_t i=0;i<displayChoices.size();++i)if(!_wcsicmp(id.c_str(),displayChoices[i].id.c_str())){index=(int)i+1;break;}
    if(!index&&!id.empty()){
        displayChoices.push_back({id,L"",L"已保存的显示器（未连接）"});
        index=(int)SendMessageW(Item(130),CB_ADDSTRING,0,(LPARAM)displayChoices.back().label.c_str());
    }
    SendMessageW(Item(130),CB_SETCURSEL,index,0);
}
static void PopulateMonitors(const std::wstring& selected){
    ratesDevice.clear();
    displayChoices=resolution::Monitors();
    SendMessageW(Item(130),CB_RESETCONTENT,0,0);
    SendMessageW(Item(130),CB_ADDSTRING,0,(LPARAM)L"默认主显示器");
    for(const auto& choice:displayChoices)SendMessageW(Item(130),CB_ADDSTRING,0,(LPARAM)choice.label.c_str());
    SelectMonitor(selected);
}
static int ReadRefreshRate(){
    HWND combo=Item(129);
    const auto index=SendMessageW(combo,CB_GETCURSEL,0,0);
    if(index<0)return 0;
    const auto data=SendMessageW(combo,CB_GETITEMDATA,index,0);
    if(data!=-1)return (int)data;
    BOOL ok=FALSE;
    const UINT hz=GetDlgItemInt(picker,131,&ok,FALSE);
    return ok&&hz>=20&&hz<=1000?(int)hz:-1;
}
static void SelectRefreshRate(int hz){
    HWND combo=Item(129);
    const auto count=SendMessageW(combo,CB_GETCOUNT,0,0);
    for(int i=0;i<count;++i)if(SendMessageW(combo,CB_GETITEMDATA,i,0)==hz){
        SendMessageW(combo,CB_SETCURSEL,i,0);
        ShowWindow(Item(131),SW_HIDE);return;
    }
    SendMessageW(combo,CB_SETCURSEL,count-1,0);
    if(hz>0)SetDlgItemInt(picker,131,hz,FALSE);
    ShowWindow(Item(131),visible?SW_SHOW:SW_HIDE);
}
static void RefreshRates(int retain){
    HWND combo=Item(129);
    SendMessageW(combo,CB_RESETCONTENT,0,0);
    auto add=[&](const wchar_t* label,int hz){
        const auto index=SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)label);
        if(index>=0)SendMessageW(combo,CB_SETITEMDATA,index,hz);
    };
    add(L"保持刷新率",0);
    resolution::Config cfg;cfg.monitor=SelectedMonitor();
    resolution::SavedMode selected,baseline;
    bool haveRates=false;
    if(resolution::CurrentFor(cfg,selected)){
        if(resolution::LoadRecovery(baseline)&&!_wcsicmp(selected.device,baseline.device))selected=baseline;
        BOOL okW=FALSE,okH=FALSE;
        int width=GetDlgItemInt(picker,108,&okW,FALSE),height=GetDlgItemInt(picker,110,&okH,FALSE);
        if(SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED)
            okW=okH=resolution::Calculate4x3(selected.mode.dmPelsHeight,width,height);
        if(okW&&okH){
            haveRates=true;
            if(ratesDevice!=selected.device||ratesWidth!=width||ratesHeight!=height||ratesDepth!=selected.mode.dmBitsPerPel){
                ratesDevice=selected.device;ratesWidth=width;ratesHeight=height;ratesDepth=selected.mode.dmBitsPerPel;
                cachedRates.clear();
                for(DWORD i=0;;++i){
            DEVMODEW mode={};mode.dmSize=sizeof(mode);
            if(!resolution::display.enumerate(selected.device,i,&mode,0))break;
            if(mode.dmPelsWidth==(DWORD)width&&mode.dmPelsHeight==(DWORD)height&&
               mode.dmBitsPerPel==selected.mode.dmBitsPerPel&&mode.dmDisplayFrequency>=20&&mode.dmDisplayFrequency<=1000)
                        cachedRates.push_back((int)mode.dmDisplayFrequency);
                }
                std::sort(cachedRates.begin(),cachedRates.end());
                cachedRates.erase(std::unique(cachedRates.begin(),cachedRates.end()),cachedRates.end());
            }
        }
    }
    if(haveRates)for(int hz:cachedRates)add((std::to_wstring(hz)+L" Hz").c_str(),hz);
    add(L"自定义…（双模显示器）",-1);
    SelectRefreshRate(std::max(0,retain));
}
static int SelectedRule(){
    return (int)SendMessageW(Item(122),LVM_GETNEXTITEM,(WPARAM)-1,LVNI_SELECTED);
}
static void RefreshRules(){
    updatingTable=true;
    SendMessageW(Item(122),LVM_SETITEMCOUNT,resolution::foregroundRules.size(),0);
    InvalidateRect(Item(122),NULL,FALSE);
    updatingTable=false;
}
static void LoadRule(){
    editedRule=SelectedRule();
    if(editedRule<0||(size_t)editedRule>=resolution::foregroundRules.size())return;
    const auto& rule=resolution::foregroundRules[editedRule];
    SendMessageW(Item(125),CB_SETCURSEL,rule.platform<3?0:1,0);
    SendMessageW(Item(128),CB_SETCURSEL,rule.platform<3?rule.platform:0,0);
    SetWindowTextW(Item(104),rule.path.c_str());
    SendMessageW(Item(106),BM_SETCHECK,rule.automatic,0);
    SendMessageW(Item(107),BM_SETCHECK,!rule.automatic,0);
    SetDlgItemInt(picker,108,rule.width,FALSE);
    SetDlgItemInt(picker,110,rule.height,FALSE);SelectMonitor(rule.monitor);RefreshRates(rule.refreshHz);
}
static void EnableInputs();
static bool Notify(NMHDR* header){
    if(header->idFrom!=122)return false;
    if(header->code==LVN_GETDISPINFOW){
        auto& item=reinterpret_cast<NMLVDISPINFOW*>(header)->item;
        if(!(item.mask&LVIF_TEXT)||item.cchTextMax<=0||item.iItem<0||(size_t)item.iItem>=resolution::foregroundRules.size())return true;
        const auto& rule=resolution::foregroundRules[item.iItem];
        const bool delta=rule.platform<3;
        std::wstring text;
        switch(item.iSubItem){
        case 0:text=delta?L"三角洲":L"其他程序";break;
        case 1:case 2:{
            int width=rule.width,height=rule.height;
            if(rule.automatic){
                const int value=item.iSubItem==1?tableAutoWidth:tableAutoHeight;
                text=value>0?std::to_wstring(value)+L"（自动）":L"自动";
            }else text=std::to_wstring(item.iSubItem==1?width:height);
            break;}
        case 3:text=rule.refreshHz?std::to_wstring(rule.refreshHz)+L" Hz":L"保持";break;
        case 4:if(delta)text=channels[rule.platform];break;
        case 5:if(!delta)text=rule.path;break;
        case 6:
            text=rule.monitor.empty()?L"默认主显示器":L"已保存的显示器（未连接）";
            for(const auto& monitor:displayChoices)if(monitor.id==rule.monitor){text=monitor.label;break;}
            break;
        }
        lstrcpynW(item.pszText,text.c_str(),item.cchTextMax);
        return true;
    }
    if(header->code==LVN_ITEMCHANGED&&!updatingTable){
        const auto& change=*reinterpret_cast<NMLISTVIEW*>(header);
        if((change.uChanged&LVIF_STATE)&&((change.uNewState^change.uOldState)&LVIS_SELECTED)){
            if(change.uNewState&LVIS_SELECTED)LoadRule();
            EnableInputs();
        }
    }
    return false;
}
static void EnableInputs(){
    const bool custom=SendMessageW(Item(125),CB_GETCURSEL,0,0)==1;
    EnableWindow(Item(122),!resolution::foregroundRules.empty());
    EnableWindow(Item(124),SelectedRule()>=0);
    EnableWindow(Item(128),!custom);
    SetWindowTextW(Item(127),editedRule>=0?L"保存修改":L"保存新规则");
    SetWindowTextW(Item(118),L"选中表格行进行修改，或点「新建规则」。");
    EnableWindow(Item(104),custom);EnableWindow(Item(105),custom);
    bool automatic=SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED;
    EnableWindow(Item(108),!automatic);EnableWindow(Item(110),!automatic);
}
static void Refresh(){
    editedRule=-1;
    const auto& c=resolution::config;
    SendMessageW(Item(100),BM_SETCHECK,c.enabled,0);
    SetWindowTextW(Item(104),c.path.c_str());
    SendMessageW(Item(106),BM_SETCHECK,c.automatic,0);SendMessageW(Item(107),BM_SETCHECK,!c.automatic,0);
    SetDlgItemInt(picker,108,c.width,FALSE);SetDlgItemInt(picker,110,c.height,FALSE);PopulateMonitors(c.monitor);RefreshRates(c.refreshHz);
    SendMessageW(Item(111),BM_SETCHECK,resolution::Exists(resolution::StartupPath()),0);RefreshRules();EnableInputs();lastStatus.clear();
}
static void Create(HWND h){
    auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style=0){return SettingControl(h,cls,text,id,0,0,1,1,style);};
    add(L"STATIC",L"分辨率自动切换",121);
    add(L"BUTTON",L"启用自动切换",100,BS_AUTOCHECKBOX|WS_TABSTOP);
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
    for(auto name:{L"三角洲",L"其他程序"})SendMessageW(kind,CB_ADDSTRING,0,(LPARAM)name);
    SendMessageW(kind,CB_SETCURSEL,0,0);
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    HWND table=add(WC_LISTVIEWW,L"",122,LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER|WS_TABSTOP);
    SendMessageW(table,CCM_SETUNICODEFORMAT,TRUE,0);
    SendMessageW(table,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    int column=0;
    for(auto title:{L"类型",L"宽",L"高",L"刷新率",L"启动渠道",L"目标 EXE 路径",L"显示器"}){
        LVCOLUMNW info={};info.mask=LVCF_TEXT|LVCF_WIDTH;info.pszText=const_cast<wchar_t*>(title);info.cx=100;
        SendMessageW(table,LVM_INSERTCOLUMNW,column++,(LPARAM)&info);
    }
    add(L"COMBOBOX",L"",129,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"COMBOBOX",L"",130,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    add(L"EDIT",L"",131,WS_BORDER|ES_NUMBER|WS_TABSTOP);
    SendMessageW(Item(131),EM_SETLIMITTEXT,4,0);
    HWND channel=add(L"COMBOBOX",L"",128,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(auto name:channels)SendMessageW(channel,CB_ADDSTRING,0,(LPARAM)name);
    SendMessageW(channel,CB_SETCURSEL,0,0);
    add(L"BUTTON",L"新建规则",123,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"保存新规则",127,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"删除所选",124,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"查看已启用规则",126,BS_OWNERDRAW|WS_TABSTOP);
    add(L"BUTTON",L"查看日志",119,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"关闭设置",120,BS_OWNERDRAW|WS_TABSTOP);
}
static void Show(bool show){visible=show;for(int id=100;id<=131;++id)if(Item(id))ShowWindow(Item(id),show?SW_SHOW:SW_HIDE);if(show){const auto i=SendMessageW(Item(129),CB_GETCURSEL,0,0);ShowWindow(Item(131),i>=0&&SendMessageW(Item(129),CB_GETITEMDATA,i,0)==-1?SW_SHOW:SW_HIDE);}}
static void Layout(HWND h,double scale,int width,int height){
    auto s=[&](int v){return (int)lround(v*scale);};
    auto place=[&](int id,int x,int y,int w,int ht){MoveWindow(GetDlgItem(h,id),s(x),s(y+40),w<0?width-s(x)-s(22):s(w),s(ht),FALSE);};
    place(121,22,10,-1,25);place(100,22,48,-1,28);
    place(125,22,86,150,130);place(128,182,86,165,130);
    place(103,22,133,100,24);place(104,145,129,-1,28);
    MoveWindow(GetDlgItem(h,104),s(145),s(169),width-s(145+22+100),s(28),FALSE);
    MoveWindow(GetDlgItem(h,105),width-s(112),s(169),s(90),s(28),FALSE);
    place(106,22,174,315,28);place(130,350,174,-1,165);place(107,22,212,155,28);
    place(108,194,212,110,28);place(109,314,214,22,26);place(110,346,212,110,28);place(129,464,212,154,165);place(131,624,212,65,28);
    place(111,22,251,-1,28);
    place(122,22,291,-1,115);
    const int widths[]={92,60,60,84,115};
    int used=0;
    for(int i=0;i<5;++i){const int w=s(widths[i]);used+=w;SendMessageW(Item(122),LVM_SETCOLUMNWIDTH,i,w);}
    SendMessageW(Item(122),LVM_SETCOLUMNWIDTH,5,std::max(s(140),width-s(213)-used));SendMessageW(Item(122),LVM_SETCOLUMNWIDTH,6,s(150));
    place(123,22,413,105,28);place(127,137,413,112,28);place(124,259,413,105,28);place(126,374,413,152,28);
    place(112,22,449,110,28);place(113,142,449,116,28);place(114,268,449,126,28);
    place(117,402,449,-1,27);place(116,22,482,-1,26);place(118,22,510,-1,24);
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
    resolution::Config selected;selected.monitor=SelectedMonitor();
    const bool haveCurrent=resolution::CurrentFor(selected,mode);
    std::wstring text=haveCurrent?L"所选显示器："+std::to_wstring(mode.mode.dmPelsWidth)+L" × "+std::to_wstring(mode.mode.dmPelsHeight)+L" @ "+std::to_wstring(mode.mode.dmDisplayFrequency)+L" Hz":L"无法读取当前显示模式";
    SetWindowTextW(Item(117),text.c_str());
    resolution::SavedMode baseline;
    const bool haveRecovery=haveCurrent&&resolution::LoadRecovery(baseline)&&!_wcsicmp(baseline.device,mode.device);
    if(!haveRecovery&&haveCurrent)baseline=mode;
    int width=0,height=0;
    std::wstring automatic=L"自动计算同高度 4:3";
    if((haveRecovery||haveCurrent)&&resolution::Calculate4x3(baseline.mode.dmPelsHeight,width,height))
        automatic+=L"（"+std::to_wstring(width)+L" × "+std::to_wstring(height)+L"）";
    else automatic+=L"（无法计算）";
    SetWindowTextW(Item(106),automatic.c_str());
    tableAutoWidth=width;tableAutoHeight=height;InvalidateRect(Item(122),NULL,FALSE);
}
static void DisplayChanged(){
    if(!picker)return;
    const auto monitor=SelectedMonitor();
    const int rate=ReadRefreshRate();
    PopulateMonitors(monitor);
    RefreshRates(rate);
    CurrentMode();
}
static bool Read(resolution::Config& c,bool validateProgram){
    c=resolution::config;c.enabled=SendMessageW(Item(100),BM_GETCHECK,0,0)==BST_CHECKED;
    c.platform=4;c.automatic=SendMessageW(Item(106),BM_GETCHECK,0,0)==BST_CHECKED;
    c.startup=SendMessageW(Item(111),BM_GETCHECK,0,0)==BST_CHECKED;
    BOOL okW,okH;c.width=GetDlgItemInt(picker,108,&okW,FALSE);c.height=GetDlgItemInt(picker,110,&okH,FALSE);
    if(c.platform<0||c.platform>4||!okW||!okH||c.width<320||c.width>16384||c.height<200||c.height>16384){MessageBoxW(picker,L"请选择监听模式并填写有效宽高（320～16384 / 200～16384）。",L"设置",MB_ICONWARNING);return false;}
    c.monitor=SelectedMonitor();c.refreshHz=ReadRefreshRate();
    if(c.refreshHz<0){MessageBoxW(picker,L"请输入 20～1000 Hz 的自定义刷新率。",L"刷新率",MB_ICONWARNING);return false;}
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
        std::wstring name=rule.platform<3?std::wstring(L"三角洲 · ")+channels[rule.platform]:
            rule.path.substr(rule.path.find_last_of(L"\\/")+1);
        int width=rule.width,height=rule.height;
        if(rule.automatic&&(!haveBaseline||!resolution::Calculate4x3(baseline.mode.dmPelsHeight,width,height)))
            return name+L" · 自动 4:3（当前无法计算）";
        return name+L" · "+std::to_wstring(width)+L" × "+std::to_wstring(height)+(rule.automatic?L"（自动 4:3）":L"")+(rule.refreshHz?L" @ "+std::to_wstring(rule.refreshHz)+L" Hz":L" · 保持刷新率");
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
    if(id==129){
        auto i=SendMessageW(Item(129),CB_GETCURSEL,0,0);
        const bool custom=i>=0&&SendMessageW(Item(129),CB_GETITEMDATA,i,0)==-1;
        ShowWindow(Item(131),custom?SW_SHOW:SW_HIDE);
        if(custom){
            MessageBoxW(picker,L"自定义刷新率供双模显示器使用。请先在显示器菜单里切到对应模式，再填写刷新率。建议先点「检查模式」或「试用 15 秒」。不支持的模式会被驱动拒绝。",L"自定义刷新率",MB_OK|MB_ICONWARNING);
            SetFocus(Item(131));
        }
        return true;
    }
    if(id==130||id==108||id==110){RefreshRates(ReadRefreshRate());CurrentMode();return true;}
    if(id==125||id==128){EnableInputs();return true;}
    if(id==123){
        editedRule=-1;
        LVITEMW state={};state.stateMask=LVIS_SELECTED|LVIS_FOCUSED;
        SendMessageW(Item(122),LVM_SETITEMSTATE,(WPARAM)-1,(LPARAM)&state);
        SendMessageW(Item(125),CB_SETCURSEL,0,0);
        SendMessageW(Item(128),CB_SETCURSEL,0,0);
        SetWindowTextW(Item(104),L"");SelectMonitor(L"");RefreshRates(0);
        SendMessageW(Item(106),BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(Item(107),BM_SETCHECK,BST_CHECKED,0);
        EnableInputs();SetFocus(Item(125));return true;
    }
    if(id==124||id==127){
        auto rules=resolution::foregroundRules;
        int selection=-1;
        if(id==124){
            const int selected=SelectedRule();
            if(selected<0||(size_t)selected>=rules.size())return true;
            rules.erase(rules.begin()+selected);
            if(!rules.empty())selection=std::min(selected,(int)rules.size()-1);
        }else{
            resolution::Config rule;
            const bool delta=SendMessageW(Item(125),CB_GETCURSEL,0,0)==0;
            if(!Read(rule,!delta)||rule.platform!=4)return true;
            if(delta){rule.platform=(int)SendMessageW(Item(128),CB_GETCURSEL,0,0);rule.path.clear();}
            for(size_t i=0;i<rules.size();++i){
                if((int)i==editedRule)continue;
                const auto& item=rules[i];
                if((delta&&item.platform<3)||(!delta&&item.platform==4&&!_wcsicmp(rule.path.c_str(),item.path.c_str()))){
                    MessageBoxW(picker,delta?L"已有三角洲规则，请选中该行修改。":L"这个 EXE 已有规则，请选中该行修改。",L"分辨率规则",MB_ICONWARNING);return true;
                }
            }
            if(editedRule>=0&&(size_t)editedRule<rules.size()){selection=editedRule;rules[selection]=rule;}
            else {selection=(int)rules.size();rules.push_back(std::move(rule));}
        }
        if(resolution::SaveForegroundRules(rules)){
            editedRule=-1;RefreshRules();
            if(selection>=0){
                LVITEMW state={};state.stateMask=state.state=LVIS_SELECTED|LVIS_FOCUSED;
                SendMessageW(Item(122),LVM_SETITEMSTATE,selection,(LPARAM)&state);
                SendMessageW(Item(122),LVM_ENSUREVISIBLE,selection,FALSE);
                LoadRule();
            }
            EnableInputs();
        }else MessageBoxW(picker,L"无法保存规则。",L"分辨率规则",MB_ICONERROR);
        return true;
    }
    if(id==106||id==107){EnableInputs();RefreshRates(ReadRefreshRate());return true;}
    if(id==105){wchar_t path[1024]=L"";OPENFILENAMEW dialog={};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=picker;
        dialog.lpstrFilter=L"应用程序 (*.exe)\0*.exe\0\0";dialog.lpstrFile=path;dialog.nMaxFile=1024;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(GetOpenFileNameW(&dialog))SetWindowTextW(Item(104),path);return true;}
    if(id==115){
        auto c=resolution::config;c.platform=4;
        c.enabled=SendMessageW(Item(100),BM_GETCHECK,0,0)==BST_CHECKED;
        c.startup=SendMessageW(Item(111),BM_GETCHECK,0,0)==BST_CHECKED;
        if(!resolution::SaveForegroundRules(resolution::foregroundRules)||!resolution::SaveConfig(c))
            MessageBoxW(picker,L"保存失败，请查看日志。",L"设置",MB_ICONERROR);
        Status();return true;
    }
    if(id==112||id==113){resolution::Config c;if(!Read(c,false))return true;
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
