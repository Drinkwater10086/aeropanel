#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

#define WM_TRAY (WM_APP+1)
#define ID_OPEN 1001
#define ID_QUIT 1002
static WCHAR dir[MAX_PATH], settings[MAX_PATH], stopfile[MAX_PATH], saved[MAX_PATH], logfile[MAX_PATH], helper[MAX_PATH], gui[MAX_PATH];
static NOTIFYICONDATAW icon;
static HANDLE mutex, child;
static int childKind, active, gameSeen, launcherConsumed, stopRequested, sessionPlatform;
static ULONGLONG retryAt;

static void makepath(WCHAR *out, const WCHAR *name) { _snwprintf(out, MAX_PATH, L"%ls\\%ls",dir,name); out[MAX_PATH-1]=0; }
static int exists(const WCHAR *path) { return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES; }
static void log_error(const char *msg) {
    FILE *f=_wfopen(logfile,L"ab"); SYSTEMTIME t;
    if(!f)return;
    GetLocalTime(&t);
    fprintf(f,"\r\n[%04u-%02u-%02u %02u:%02u:%02u] Native watcher: %s (Windows error %lu)\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,msg,(unsigned long)GetLastError());
    fclose(f);
}
static int names_running(const WCHAR *a, const WCHAR *b) {
    HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); PROCESSENTRY32W p; int found=0;
    if(h==INVALID_HANDLE_VALUE){log_error("CreateToolhelp32Snapshot failed"); return 0;}
    ZeroMemory(&p,sizeof(p)); p.dwSize=sizeof(p);
    if(Process32FirstW(h,&p)) do { if(_wcsicmp(p.szExeFile,a)==0 || (b && _wcsicmp(p.szExeFile,b)==0)){found=1;break;} } while(Process32NextW(h,&p));
    CloseHandle(h);return found;
}
static int platform(void) {
    char buf[4096]; FILE *f=_wfopen(settings,L"rb"); size_t n;
    if(!f){log_error("Cannot read settings.json");return 0;}
    n=fread(buf,1,sizeof(buf)-1,f);fclose(f);buf[n]=0;
    if(strstr(buf,"\"Steam\""))return 1;
    if(strstr(buf,"\"Official\""))return 2;
    if(strstr(buf,"\"WeGame\""))return 3;
    log_error("Invalid platform in settings.json");return 0;
}
static int launch_helper(const WCHAR *action) {
    WCHAR powershell[MAX_PATH],cmd[3*MAX_PATH]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    if(!GetSystemDirectoryW(powershell,MAX_PATH))return 0;
    wcscat(powershell,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    _snwprintf(cmd,3*MAX_PATH,L"\"%ls\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%ls\" -Action %ls",powershell,helper,action);
    cmd[3*MAX_PATH-1]=0; ZeroMemory(&si,sizeof(si));si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    ZeroMemory(&pi,sizeof(pi));
    if(!CreateProcessW(powershell,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,dir,&si,&pi)){log_error("Cannot start display helper");return 0;}
    CloseHandle(pi.hThread); child=pi.hProcess; return 1;
}
static void open_settings(void) {
    WCHAR exe[MAX_PATH],params[2*MAX_PATH]; GetSystemDirectoryW(exe,MAX_PATH);wcscat(exe,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    _snwprintf(params,2*MAX_PATH,L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -STA -File \"%ls\"",gui);params[2*MAX_PATH-1]=0;
    if((INT_PTR)ShellExecuteW(NULL,L"open",exe,params,dir,SW_HIDE)<=32)log_error("Cannot open settings");
}
static void finish(HWND hwnd) { Shell_NotifyIconW(NIM_DELETE,&icon); DestroyWindow(hwnd); }
static void poll(HWND hwnd) {
    int game, launch=0, p; DWORD code;
    if(exists(stopfile)){DeleteFileW(stopfile);stopRequested=1;}
    if(child){
        if(WaitForSingleObject(child,0)==WAIT_TIMEOUT)return;
        code=1;GetExitCodeProcess(child,&code);CloseHandle(child);child=NULL;
        if(childKind==1){if(code==0){active=1;}else{retryAt=GetTickCount64()+30000;}}
        else if(childKind==2){if(code==0){active=0;gameSeen=0;launcherConsumed=1;}else{retryAt=GetTickCount64()+30000;}}
        childKind=0;
    }
    if(stopRequested){if(active){if(launch_helper(L"restore")){childKind=2;active=0;return;}} finish(hwnd);return;}
    p=active ? sessionPlatform : platform(); if(!p)return;
    game=names_running(L"DeltaForceClient-Win64-Shipping.exe",NULL);
    if(p==1)launch=names_running(L"df_launcher.exe",NULL);
    if(p==2)launch=names_running(L"delta_force_launcher.exe",L"df_launcher.exe");
    if(!launch)launcherConsumed=0;
    if(active && game)gameSeen=1;
    if(GetTickCount64()<retryAt)return;
    if(!active && (game || (launch && !launcherConsumed))){
        if(exists(saved)){log_error("Saved mode exists; restore it in settings first");retryAt=GetTickCount64()+30000;return;}
        sessionPlatform=p;
        gameSeen=game;
        if(launch_helper(L"switch"))childKind=1; else retryAt=GetTickCount64()+30000;
    } else if(active && !game && (gameSeen || !launch)) {
        if(launch_helper(L"restore"))childKind=2; else retryAt=GetTickCount64()+30000;
    }
}
static LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m){
    case WM_TIMER:poll(h);return 0;
    case WM_COMMAND:if(LOWORD(w)==ID_OPEN)open_settings();if(LOWORD(w)==ID_QUIT){stopRequested=1;poll(h);}return 0;
    case WM_TRAY:
        if(l==WM_LBUTTONDBLCLK)open_settings();
        if(l==WM_RBUTTONUP){POINT pt;HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,ID_OPEN,L"打开设置");AppendMenuW(menu,MF_STRING,ID_QUIT,L"退出监测");GetCursorPos(&pt);SetForegroundWindow(h);TrackPopupMenu(menu,TPM_RIGHTBUTTON,pt.x,pt.y,0,h,NULL);DestroyMenu(menu);}return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE prev,LPSTR cmd,int show) {
    WCHAR *slash;WNDCLASSW wc;HWND hwnd;MSG msg;
    (void)prev;(void)cmd;(void)show;
    GetModuleFileNameW(NULL,dir,MAX_PATH);slash=wcsrchr(dir,L'\\');if(!slash)return 1;*slash=0;
    makepath(settings,L"settings.json");makepath(stopfile,L"watcher-stop.flag");makepath(saved,L"original-mode.xml");makepath(logfile,L"error.log");makepath(helper,L"Switch.ps1");makepath(gui,L"Settings.ps1");
    mutex=CreateMutexW(NULL,TRUE,L"Local\\DeltaResolutionWatcher");if(!mutex)return 1;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return 0;}
    DeleteFileW(stopfile);ZeroMemory(&wc,sizeof(wc));wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=L"DeltaResolutionNativeWatcher";
    if(!RegisterClassW(&wc)){log_error("RegisterClass failed");return 1;}
    hwnd=CreateWindowW(wc.lpszClassName,L"DeltaResolution",0,0,0,0,0,NULL,NULL,instance,NULL);if(!hwnd){log_error("CreateWindow failed");return 1;}
    ZeroMemory(&icon,sizeof(icon));icon.cbSize=sizeof(icon);icon.hWnd=hwnd;icon.uID=1;icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;icon.uCallbackMessage=WM_TRAY;icon.hIcon=LoadIconW(NULL,IDI_APPLICATION);wcscpy(icon.szTip,L"三角洲分辨率监测");
    if(!Shell_NotifyIconW(NIM_ADD,&icon)){log_error("Cannot add tray icon");DestroyWindow(hwnd);return 1;}
    SetTimer(hwnd,1,2000,NULL);
    while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    KillTimer(hwnd,1);if(child)CloseHandle(child);ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}
