#define UNICODE
#define _UNICODE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <objbase.h>
#include <shlguid.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WM_TRAY (WM_APP+1)
#define WM_OPEN_SETTINGS (WM_APP+2)
#define ID_PLATFORM 101
#define ID_AUTO 102
#define ID_CUSTOM 103
#define ID_WIDTH 104
#define ID_HEIGHT 105
#define ID_SAVE 106
#define ID_CHECK 107
#define ID_AUTOSTART 108
#define ID_RESTORE 109
#define ID_LOG 110
#define ID_CLOSE 111
#define ID_TRAY_OPEN 201
#define ID_TRAY_QUIT 202
#define ID_PROGRAM_PATH 112
#define ID_BROWSE 113

typedef struct { DWORD magic; DEVMODEW mode; } SavedMode;
static WCHAR dataDir[MAX_PATH], configPath[MAX_PATH], savedPath[MAX_PATH], logPath[MAX_PATH], linkPath[MAX_PATH], exePath[MAX_PATH], targetPathFile[MAX_PATH], sessionPath[MAX_PATH];
static HWND window, platformBox, widthBox, heightBox, autoBox, customBox, startupBox, programBox, browseButton;
static HINSTANCE instance; static HFONT uiFont; static HANDLE mutex; static NOTIFYICONDATAW tray;
static UINT taskbarCreated; static int active, gameSeen, launcherConsumed, platformInUse, quitting; static ULONGLONG retryAt, nextQueryLog;
static DWORD processQueryError;
static const WCHAR className[]=L"ResolutionSwitcherV2";

static void path_join(WCHAR *out,const WCHAR *dir,const WCHAR *name) { _snwprintf(out,MAX_PATH,L"%ls\\%ls",dir,name);out[MAX_PATH-1]=0; }
static int exists(const WCHAR *p) { return GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES; }
static void log_error(const WCHAR *stage, LONG code, const WCHAR *detail) {
    FILE *f=_wfopen(logPath,L"ab");SYSTEMTIME t;WCHAR line[2048];char utf8[8192];int n;
    if(!f)return;
    GetLocalTime(&t);
    _snwprintf(line,2048,L"[%04u-%02u-%02u %02u:%02u:%02u] %ls | code=%ld | %ls\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,stage,code,detail?detail:L"");line[2047]=0;
    n=WideCharToMultiByte(CP_UTF8,0,line,-1,utf8,sizeof(utf8),NULL,NULL);if(n>1)fwrite(utf8,1,(size_t)n-1,f);fclose(f);
}
static void message(const WCHAR *text,UINT flags){MessageBoxW(window,text,L"分辨率自动切换",flags);}
static void set_font(HWND c){SendMessageW(c,WM_SETFONT,(WPARAM)uiFont,TRUE);}
static HWND control(const WCHAR *cls,const WCHAR *text,DWORD style,int x,int y,int w,int h,int id) {
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,(HMENU)(INT_PTR)id,instance,NULL);set_font(c);return c;
}
static int current_mode(DEVMODEW *m){ZeroMemory(m,sizeof(*m));m->dmSize=sizeof(*m);return EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,m)!=0;}
static void display_fields(DEVMODEW *m){m->dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFLAGS|DM_DISPLAYFREQUENCY;}
static int find_mode(DWORD width,DWORD height,const DEVMODEW *current,DEVMODEW *best,LONG *err) {
    DWORD i;int found=0;long bestDiff=0x7fffffff;DEVMODEW m;
    for(i=0;;i++) {ZeroMemory(&m,sizeof(m));m.dmSize=sizeof(m);if(!EnumDisplaySettingsW(NULL,i,&m))break;
        if(m.dmPelsWidth!=width||m.dmPelsHeight!=height||m.dmBitsPerPel!=current->dmBitsPerPel)continue;
        display_fields(&m);if(ChangeDisplaySettingsW(&m,CDS_TEST)!=DISP_CHANGE_SUCCESSFUL)continue;
        {long diff=labs((long)m.dmDisplayFrequency-(long)current->dmDisplayFrequency);if(!found||diff<bestDiff){*best=m;bestDiff=diff;found=1;}}
    }
    if(!found){m=*current;m.dmPelsWidth=width;m.dmPelsHeight=height;display_fields(&m);*err=ChangeDisplaySettingsW(&m,CDS_TEST);if(*err==DISP_CHANGE_SUCCESSFUL){*best=m;found=1;}}
    return found;
}
static int save_original(const DEVMODEW *m) {SavedMode s;FILE *f; s.magic=0x44525331;s.mode=*m;f=_wfopen(savedPath,L"wb");if(!f)return 0;{int ok=fwrite(&s,1,sizeof(s),f)==sizeof(s);if(fclose(f)!=0)ok=0;return ok;}}
static int load_original(DEVMODEW *m){SavedMode s;FILE *f=_wfopen(savedPath,L"rb");int ok;if(!f)return 0;ok=fread(&s,1,sizeof(s),f)==sizeof(s)&&s.magic==0x44525331;fclose(f);if(ok)*m=s.mode;return ok;}
static int restore_mode(int show) {
    DEVMODEW m;LONG result;if(!load_original(&m)){if(show)message(L"没有可读取的原始分辨率记录。",MB_ICONWARNING);return 0;}
    display_fields(&m);result=ChangeDisplaySettingsW(&m,0);
    if(result!=DISP_CHANGE_SUCCESSFUL){log_error(L"恢复原模式",result,L"ChangeDisplaySettingsW 失败；记录已保留。");if(show)message(L"恢复失败，原模式记录已保留。请查看错误日志。",MB_ICONERROR);return 0;}
    if(!DeleteFileW(savedPath)){log_error(L"删除恢复记录",GetLastError(),L"显示模式已恢复，但无法清除恢复记录。");if(show)message(L"分辨率已恢复，但清除恢复记录失败。",MB_ICONWARNING);return 0;}
    active=0;if(show)message(L"已恢复切换前的显示模式。",MB_ICONINFORMATION);return 1;
}
static int find_target(DEVMODEW *original,DEVMODEW *target,int autoMode,int width,int height,LONG *err) {
    if(!current_mode(original)){*err=GetLastError();return 0;}
    if(autoMode){if(original->dmPelsHeight%3!=0){*err=-100;return 0;}width=(int)(original->dmPelsHeight/3*4);height=(int)original->dmPelsHeight;}
    if(width<320||height<200||width>16384||height>16384){*err=-101;return 0;}
    return find_mode((DWORD)width,(DWORD)height,original,target,err);
}
static int apply_configured_mode(void) {
    DEVMODEW original,target;LONG err=DISP_CHANGE_BADMODE;int autoMode,width,height;
    if(exists(savedPath)){log_error(L"切换前检查",-1,L"存在尚未恢复的显示模式记录；请打开设置并恢复。");return 0;}
    autoMode=GetPrivateProfileIntW(L"resolution",L"auto",1,configPath);width=GetPrivateProfileIntW(L"resolution",L"width",2880,configPath);height=GetPrivateProfileIntW(L"resolution",L"height",2160,configPath);
    if(!find_target(&original,&target,autoMode,width,height,&err)){WCHAR desc[256];_snwprintf(desc,256,L"无法验证目标分辨率，模式=%ls，输入=%d×%d；请检查驱动支持和刷新率。",autoMode?L"自动4:3":L"自定义",width,height);log_error(L"验证分辨率",err,desc);return 0;}
    if(!save_original(&original)){log_error(L"保存原模式",GetLastError(),L"无法写入恢复记录，已取消切换。");return 0;}
    err=ChangeDisplaySettingsW(&target,0);
    if(err!=DISP_CHANGE_SUCCESSFUL){log_error(L"切换分辨率",err,L"ChangeDisplaySettingsW 失败，尝试恢复原模式。");display_fields(&original);if(ChangeDisplaySettingsW(&original,0)==DISP_CHANGE_SUCCESSFUL)DeleteFileW(savedPath);else log_error(L"切换失败后的恢复",GetLastError(),L"恢复失败，记录已保留。");return 0;}
    active=1;return 1;
}
static int process_exists(const WCHAR *a,const WCHAR *b) {
    HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W p;int found=0;
    if(h==INVALID_HANDLE_VALUE){log_error(L"枚举进程",GetLastError(),L"CreateToolhelp32Snapshot 失败。");return -1;}
    ZeroMemory(&p,sizeof(p));p.dwSize=sizeof(p);
    if(!Process32FirstW(h,&p)){CloseHandle(h);return -1;}
    do{if(_wcsicmp(p.szExeFile,a)==0||(b&&_wcsicmp(p.szExeFile,b)==0)){found=1;break;}}while(Process32NextW(h,&p));CloseHandle(h);return found;
}
static int read_target_path(WCHAR *out) {
    char bytes[MAX_PATH*4]; FILE *f=_wfopen(targetPathFile,L"rb"); size_t n;int count;
    if(!f)return 0;
    n=fread(bytes,1,sizeof(bytes),f);
    if(ferror(f)||n==0||n==sizeof(bytes)){fclose(f);return 0;}fclose(f);
    count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,(int)n,out,MAX_PATH-1);
    if(count<=0)return 0;
    out[count]=0;return 1;
}
static int write_target_path(const WCHAR *path) {
    char bytes[MAX_PATH*4];WCHAR tmp[MAX_PATH];int n;FILE *f;size_t written;
    if(_snwprintf(tmp,MAX_PATH,L"%ls.tmp",targetPathFile)<0)return 0;
    n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,bytes,sizeof(bytes),NULL,NULL);
    if(n<=1)return 0;
    f=_wfopen(tmp,L"wb");if(!f)return 0;
    written=fwrite(bytes,1,(size_t)n-1,f);
    if(fclose(f)!=0||written!=(size_t)n-1){DeleteFileW(tmp);return 0;}
    if(!MoveFileExW(tmp,targetPathFile,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(tmp);return 0;}return 1;
}
/* Return -1 if a same-name process cannot be identified, so an active mode is not restored prematurely. */
static int target_process_running(const WCHAR *path) {
    const WCHAR *base=wcsrchr(path,L'\\');HANDLE snap,process;PROCESSENTRY32W entry;WCHAR image[MAX_PATH];DWORD size,ownSession,processSession;int unknown=0;
    processQueryError=0;
    base=base?base+1:path;if(!*base){processQueryError=ERROR_INVALID_NAME;return -1;}
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)){processQueryError=GetLastError();return -1;}
    snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE){processQueryError=GetLastError();return -1;}
    ZeroMemory(&entry,sizeof(entry));entry.dwSize=sizeof(entry);
    if(!Process32FirstW(snap,&entry)){processQueryError=GetLastError();CloseHandle(snap);return -1;}
    do {
        if(_wcsicmp(entry.szExeFile,base)!=0)continue;
        if(!ProcessIdToSessionId(entry.th32ProcessID,&processSession)){processQueryError=GetLastError();unknown=1;continue;}
        if(processSession!=ownSession)continue;
        process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process){DWORD err=GetLastError();if(err!=ERROR_INVALID_PARAMETER){processQueryError=err;unknown=1;}continue;}
        size=MAX_PATH;
        if(QueryFullProcessImageNameW(process,0,image,&size)) {
            CloseHandle(process);
            if(_wcsicmp(image,path)==0){CloseHandle(snap);return 1;}
        } else {processQueryError=GetLastError();unknown=1;CloseHandle(process);}
    } while(Process32NextW(snap,&entry));
    CloseHandle(snap);return unknown?-1:0;
}
static void poll(void) {
    int p,game,launcher=0;WCHAR selected[MAX_PATH];if(quitting)return;
    if(!exists(configPath))return;
    p=active?platformInUse:(int)GetPrivateProfileIntW(L"game",L"platform",0,configPath);
    if(p<0||p>3){if(GetTickCount64()>=nextQueryLog){log_error(L"读取监听模式",p,L"配置中的监听模式无效；请重新保存设置。");nextQueryLog=GetTickCount64()+30000;}return;}
    if(p==3){
        if(active)wcscpy(selected,sessionPath);
        else if(!read_target_path(selected)){if(GetTickCount64()>=nextQueryLog){log_error(L"读取监听程序",-1,L"未找到有效的程序路径；请在设置中重新选择 EXE。");nextQueryLog=GetTickCount64()+30000;}return;}
        game=target_process_running(selected);
        if(game<0){if(GetTickCount64()>=nextQueryLog){log_error(L"检测监听程序",processQueryError,L"无法确认同名进程的完整路径；本次保持显示模式。请检查进程权限。");nextQueryLog=GetTickCount64()+30000;}return;}
        if(GetTickCount64()<retryAt)return;
        if(!active&&game){platformInUse=p;wcscpy(sessionPath,selected);if(!apply_configured_mode())retryAt=GetTickCount64()+30000;}
        else if(active&&!game){if(!restore_mode(0))retryAt=GetTickCount64()+30000;}
        return;
    }
    game=process_exists(L"DeltaForceClient-Win64-Shipping.exe",NULL);
    if(p==0)launcher=process_exists(L"df_launcher.exe",NULL);
    if(p==1)launcher=process_exists(L"delta_force_launcher.exe",L"df_launcher.exe");
    if(game<0||launcher<0)return;
    if(!launcher)launcherConsumed=0;
    if(active&&game)gameSeen=1;
    if(GetTickCount64()<retryAt)return;
    if(!active&&(game||(launcher&&!launcherConsumed))){platformInUse=p;gameSeen=game;if(!apply_configured_mode())retryAt=GetTickCount64()+30000;}
    else if(active&&!game&&(gameSeen||!launcher)){if(restore_mode(0)){gameSeen=0;if(launcher)launcherConsumed=1;}else retryAt=GetTickCount64()+30000;}
}
static HRESULT set_startup(int enable) {
    IShellLinkW *link=NULL;IPersistFile *persist=NULL;HRESULT hr;
    if(!enable){if(DeleteFileW(linkPath)||GetLastError()==ERROR_FILE_NOT_FOUND)return S_OK;return HRESULT_FROM_WIN32(GetLastError());}
    hr=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link);
    if(SUCCEEDED(hr))hr=IShellLinkW_SetPath(link,exePath);
    if(SUCCEEDED(hr))hr=IShellLinkW_SetArguments(link,L"--background");
    if(SUCCEEDED(hr))hr=IShellLinkW_SetDescription(link,L"Delta Resolution Watcher");
    if(SUCCEEDED(hr))hr=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&persist);
    if(SUCCEEDED(hr))hr=IPersistFile_Save(persist,linkPath,TRUE);
    if(persist)IPersistFile_Release(persist);
    if(link)IShellLinkW_Release(link);
    return hr;
}
static void refresh_controls(void) {
    int p=(int)GetPrivateProfileIntW(L"game",L"platform",0,configPath),a=(int)GetPrivateProfileIntW(L"resolution",L"auto",1,configPath);
    WCHAR target[MAX_PATH];
    if(p<0||p>3)p=0;
    SendMessageW(platformBox,CB_SETCURSEL,p,0);
    if(read_target_path(target))SetWindowTextW(programBox,target);else SetWindowTextW(programBox,L"");
    EnableWindow(programBox,p==3);EnableWindow(browseButton,p==3);
    SendMessageW(autoBox,BM_SETCHECK,a?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(customBox,BM_SETCHECK,a?BST_UNCHECKED:BST_CHECKED,0);
    SetDlgItemInt(window,ID_WIDTH,GetPrivateProfileIntW(L"resolution",L"width",2880,configPath),FALSE);
    SetDlgItemInt(window,ID_HEIGHT,GetPrivateProfileIntW(L"resolution",L"height",2160,configPath),FALSE);
    SendMessageW(startupBox,BM_SETCHECK,exists(linkPath)?BST_CHECKED:BST_UNCHECKED,0);
}
static int read_controls(int *p,int *a,int *w,int *h) {
    BOOL ok1,ok2;*p=(int)SendMessageW(platformBox,CB_GETCURSEL,0,0);*a=SendMessageW(autoBox,BM_GETCHECK,0,0)==BST_CHECKED;
    *w=(int)GetDlgItemInt(window,ID_WIDTH,&ok1,FALSE);*h=(int)GetDlgItemInt(window,ID_HEIGHT,&ok2,FALSE);
    if(*p<0||*p>3||(!*a&&(!ok1||!ok2||*w<320||*h<200||*w>16384||*h>16384))){message(L"请选择监听模式，并输入有效的宽度和高度。",MB_ICONWARNING);return 0;}return 1;
}
static void save_controls(void) {
    int p,a,w,h;WCHAR number[32],input[MAX_PATH],resolved[MAX_PATH];DWORD attrs,n;HRESULT hr;if(!read_controls(&p,&a,&w,&h))return;
    if(p==3){
        if(GetWindowTextLengthW(programBox)>=MAX_PATH){message(L"程序路径过长，请选择短于 260 个字符的路径。",MB_ICONWARNING);return;}
        GetWindowTextW(programBox,input,MAX_PATH);n=GetFullPathNameW(input,MAX_PATH,resolved,NULL);
        if(!*input||n==0||n>=MAX_PATH){message(L"请选择有效的 EXE 程序路径。",MB_ICONWARNING);return;}
        attrs=GetFileAttributesW(resolved);
        if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)||!wcsrchr(resolved,L'.')||_wcsicmp(wcsrchr(resolved,L'.'),L".exe")!=0){message(L"请选择存在的 EXE 文件。",MB_ICONWARNING);return;}
        if(_wcsicmp(resolved,exePath)==0){message(L"不能监听本程序自身。",MB_ICONWARNING);return;}
        if(!write_target_path(resolved)){log_error(L"保存监听路径",GetLastError(),L"无法写入程序路径文件。");message(L"保存程序路径失败，请查看错误日志。",MB_ICONERROR);return;}
        SetWindowTextW(programBox,resolved);
    }
    _snwprintf(number,32,L"%d",p);if(!WritePrivateProfileStringW(L"game",L"platform",number,configPath))goto fail;
    _snwprintf(number,32,L"%d",a);if(!WritePrivateProfileStringW(L"resolution",L"auto",number,configPath))goto fail;
    _snwprintf(number,32,L"%d",w);if(!WritePrivateProfileStringW(L"resolution",L"width",number,configPath))goto fail;
    _snwprintf(number,32,L"%d",h);if(!WritePrivateProfileStringW(L"resolution",L"height",number,configPath))goto fail;
    hr=set_startup(SendMessageW(startupBox,BM_GETCHECK,0,0)==BST_CHECKED);
    if(FAILED(hr)){log_error(L"更新登录自启",hr,L"无法创建或删除启动快捷方式。");message(L"设置已保存，但登录自启更新失败。详情见错误日志。",MB_ICONERROR);return;}
    message(L"设置已保存，将在下次切换时生效。",MB_ICONINFORMATION);return;
fail: log_error(L"保存设置",GetLastError(),L"写入配置文件失败。");message(L"保存设置失败，请查看错误日志。",MB_ICONERROR);
}
static void browse_program(void) {
    OPENFILENAMEW dialog;WCHAR selected[MAX_PATH]=L"";
    ZeroMemory(&dialog,sizeof(dialog));dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
    dialog.lpstrFilter=L"可执行文件 (*.exe)\0*.exe\0\0";dialog.lpstrFile=selected;dialog.nMaxFile=MAX_PATH;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog))SetWindowTextW(programBox,selected);
    else if(CommDlgExtendedError()!=0){log_error(L"选择程序",(LONG)CommDlgExtendedError(),L"无法打开文件选择窗口。");message(L"打开文件选择窗口失败，请查看错误日志。",MB_ICONERROR);}
}
static void check_controls(void) {
    int p,a,w,h;DEVMODEW old,target;LONG err=DISP_CHANGE_BADMODE;WCHAR msg[256];
    if(!read_controls(&p,&a,&w,&h))return;
    if(!find_target(&old,&target,a,w,h,&err)){_snwprintf(msg,256,L"驱动未验证通过，代码：%ld。请检查分辨率和刷新率；没有进行切换。",err);log_error(L"检查目标模式",err,msg);message(msg,MB_ICONWARNING);return;}
    _snwprintf(msg,256,L"驱动验证通过：%lu × %lu，%lu Hz。没有进行切换。",(unsigned long)target.dmPelsWidth,(unsigned long)target.dmPelsHeight,(unsigned long)target.dmDisplayFrequency);message(msg,MB_ICONINFORMATION);
}
static void show_settings(void){refresh_controls();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
static void add_tray(void){Shell_NotifyIconW(NIM_ADD,&tray);}
static void quit_app(void){quitting=1;if(active&&!restore_mode(0)){message(L"退出时未能恢复原分辨率；恢复记录已保留，请重新打开程序后手动恢复。",MB_ICONWARNING);}Shell_NotifyIconW(NIM_DELETE,&tray);DestroyWindow(window);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==taskbarCreated){add_tray();return 0;}
    switch(m){
    case WM_CREATE:
        window=h;
        uiFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        control(L"STATIC",L"监听模式",0,22,20,100,24,0);
        platformBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,135,16,300,180,ID_PLATFORM);
        SendMessageW(platformBox,CB_ADDSTRING,0,(LPARAM)L"三角洲预设：Steam");SendMessageW(platformBox,CB_ADDSTRING,0,(LPARAM)L"三角洲预设：官方启动器");SendMessageW(platformBox,CB_ADDSTRING,0,(LPARAM)L"三角洲预设：WeGame");SendMessageW(platformBox,CB_ADDSTRING,0,(LPARAM)L"指定其他程序");
        control(L"STATIC",L"程序路径",0,22,62,100,24,0);
        programBox=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,120,59,300,26,ID_PROGRAM_PATH);
        browseButton=control(L"BUTTON",L"浏览…",BS_PUSHBUTTON|WS_TABSTOP,428,58,72,28,ID_BROWSE);
        autoBox=control(L"BUTTON",L"自动计算同高度 4:3",BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,22,104,210,28,ID_AUTO);
        customBox=control(L"BUTTON",L"自定义分辨率",BS_AUTORADIOBUTTON|WS_TABSTOP,22,142,145,28,ID_CUSTOM);
        widthBox=control(L"EDIT",L"2880",WS_BORDER|ES_NUMBER|WS_TABSTOP,170,142,90,25,ID_WIDTH);control(L"STATIC",L"×",0,270,144,20,25,0);
        heightBox=control(L"EDIT",L"2160",WS_BORDER|ES_NUMBER|WS_TABSTOP,300,142,90,25,ID_HEIGHT);
        startupBox=control(L"BUTTON",L"登录 Windows 后自动启动",BS_AUTOCHECKBOX|WS_TABSTOP,22,185,260,28,ID_AUTOSTART);
        control(L"BUTTON",L"保存设置",BS_PUSHBUTTON|WS_TABSTOP,22,232,110,32,ID_SAVE);control(L"BUTTON",L"检查分辨率",BS_PUSHBUTTON|WS_TABSTOP,145,232,110,32,ID_CHECK);
        control(L"BUTTON",L"恢复原分辨率",BS_PUSHBUTTON|WS_TABSTOP,268,232,125,32,ID_RESTORE);
        control(L"BUTTON",L"查看错误日志",BS_PUSHBUTTON|WS_TABSTOP,22,278,130,32,ID_LOG);control(L"BUTTON",L"关闭设置",BS_PUSHBUTTON|WS_TABSTOP,165,278,110,32,ID_CLOSE);
        control(L"STATIC",L"普通模式按所选 EXE 完整路径监听。关闭窗口后后台继续运行。\n三角洲 WeGame 预设在游戏进程出现后切换。",0,22,325,480,48,0);return 0;
    case WM_COMMAND:
        if(LOWORD(w)==ID_PLATFORM && HIWORD(w)==CBN_SELCHANGE){int custom=(SendMessageW(platformBox,CB_GETCURSEL,0,0)==3);EnableWindow(programBox,custom);EnableWindow(browseButton,custom);return 0;}
        switch(LOWORD(w)){
        case ID_SAVE:save_controls();return 0;case ID_CHECK:check_controls();return 0;
        case ID_BROWSE:browse_program();return 0;
        case ID_RESTORE:if(active){message(L"监听目标正在使用切换后的模式，请先退出目标程序或从托盘退出本程序。",MB_ICONWARNING);}else restore_mode(1);return 0;
        case ID_LOG:if(exists(logPath))ShellExecuteW(h,L"open",L"notepad.exe",logPath,NULL,SW_SHOWNORMAL);else message(L"目前没有错误日志。",MB_ICONINFORMATION);return 0;
        case ID_CLOSE:ShowWindow(h,SW_HIDE);return 0;case ID_TRAY_OPEN:show_settings();return 0;case ID_TRAY_QUIT:quit_app();return 0;
        }break;
    case WM_TIMER:poll();return 0;
    case WM_TRAY:
        if(l==WM_LBUTTONDBLCLK)show_settings();
        if(l==WM_RBUTTONUP){POINT p;HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,ID_TRAY_OPEN,L"打开设置");AppendMenuW(menu,MF_STRING,ID_TRAY_QUIT,L"退出监测");GetCursorPos(&p);SetForegroundWindow(h);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,h,NULL);DestroyMenu(menu);}return 0;
    case WM_OPEN_SETTINGS:show_settings();return 0;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);return 0;
    case WM_DESTROY:if(uiFont)DeleteObject(uiFont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
int WINAPI WinMain(HINSTANCE inst,HINSTANCE prev,LPSTR cmd,int show) {
    WCHAR local[MAX_PATH],startup[MAX_PATH];WNDCLASSW wc;MSG msg;int first;
    (void)prev;(void)show;instance=inst;GetModuleFileNameW(NULL,exePath,MAX_PATH);
    mutex=CreateMutexW(NULL,TRUE,L"Local\\DeltaResolutionWatcher");if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(className,NULL);if(existing)PostMessageW(existing,WM_OPEN_SETTINGS,0,0);else MessageBoxW(NULL,L"另一个版本正在运行，请先停止旧版后台。",L"分辨率自动切换",MB_ICONWARNING);CloseHandle(mutex);return 0;}
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,local))||FAILED(SHGetFolderPathW(NULL,CSIDL_STARTUP,NULL,SHGFP_TYPE_CURRENT,startup))){ReleaseMutex(mutex);CloseHandle(mutex);return 1;}
    path_join(dataDir,local,L"DeltaResolution");if(!CreateDirectoryW(dataDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){ReleaseMutex(mutex);CloseHandle(mutex);return 1;}
    path_join(configPath,dataDir,L"settings.ini");path_join(savedPath,dataDir,L"original-mode.bin");path_join(logPath,dataDir,L"error.log");path_join(targetPathFile,dataDir,L"program-path.txt");path_join(linkPath,startup,L"Delta Resolution Watcher.lnk");
    first=!exists(configPath);CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    ZeroMemory(&wc,sizeof(wc));wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName=className;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    if(!RegisterClassW(&wc)){log_error(L"注册窗口",GetLastError(),L"RegisterClassW 失败。");goto cleanup;}
    window=CreateWindowW(className,L"分辨率自动切换",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,550,440,NULL,NULL,inst,NULL);
    if(!window){log_error(L"创建窗口",GetLastError(),L"CreateWindowW 失败。");goto cleanup;}
    ZeroMemory(&tray,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=LoadIconW(NULL,IDI_APPLICATION);wcscpy(tray.szTip,L"分辨率自动切换");
    if(!Shell_NotifyIconW(NIM_ADD,&tray)){log_error(L"托盘图标",GetLastError(),L"Shell_NotifyIconW 失败。");message(L"无法创建托盘图标，程序将退出。",MB_ICONERROR);DestroyWindow(window);goto cleanup;}
    SetTimer(window,1,2000,NULL);if(first||exists(savedPath)||!strstr(cmd,"--background"))show_settings();
    while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
cleanup: CoUninitialize();ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}
