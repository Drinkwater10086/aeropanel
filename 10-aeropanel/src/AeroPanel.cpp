// Independent monitor: reads MAHMSharedMemory, never RTSS shared memory.
// Optional PresentMon CLI captures frame timing in a uniquely named session.
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <limits.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>

static HWND panel, picker, sensorList;
static int settingsSection=0;
static bool settingsFullscreen=false;
static WINDOWPLACEMENT settingsPlacement={sizeof(WINDOWPLACEMENT)};
static LONG_PTR settingsWindowStyle=0;
static HFONT fontHandle;
static HFONT settingsFont=NULL,settingsPreviewFont=NULL;
static int settingsFontPixels=0;
static HFONT osdFont;
static HBRUSH settingsBrush;
static HINSTANCE appInstance;
static wchar_t iniPath[MAX_PATH], logPath[MAX_PATH], folder[MAX_PATH];
static wchar_t afterburnerConfig[MAX_PATH],afterburnerProfile[MAX_PATH];
static int xPos=0,yPos=0,panelWidth=700,panelHeight=420,interval=1000,scrollRow=0;
static bool followAfterburner=true, drawGraphs=true;
static bool transparentBackground=true,compactStyle=true,panelVisible=true,clickThrough=true;
static int fontSize=26;
static HDC layerDC=NULL;
static HBITMAP layerBitmap=NULL,previousBitmap=NULL;
static int layerWidth=0,layerHeight=0;
static HICON trayIcon=NULL;
static NOTIFYICONDATAW trayData={};
static bool trayAdded=false;
static UINT taskbarCreated=0;
static const UINT TRAY_MESSAGE=WM_APP+10;
static const COLORREF TRANSPARENT_KEY=RGB(1,2,3);
static void RefreshPanel();
static void ShowMenu();
static ULONGLONG quitAt=0;
static std::wstring statusText;
static std::wstring hardwareCaption=L"AEROPANEL / 硬件数据：MSI Afterburner";
struct Sensor {
    std::wstring name,units;
    std::wstring key;
    DWORD source,gpu,flags;
    float value;
    bool valid,selected;
};
static std::vector<Sensor> sensors, pickerSnapshot;
static std::map<std::wstring,std::deque<float>> history;
static DWORD updateCount=0;
static int Setting(const wchar_t* name,int fallback) {
    return (int)GetPrivateProfileIntW(L"Window",name,fallback,iniPath);
}
static int Clamp(int v,int low,int high) {return v<low?low:(v>high?high:v);}
static std::wstring Wide(const char* p,size_t cap) {
    size_t n=0;while(n<cap && p[n])++n;
    int len=MultiByteToWideChar(CP_ACP,0,p,(int)n,NULL,0);
    std::wstring text(len,L' ');
    if(len)MultiByteToWideChar(CP_ACP,0,p,(int)n,&text[0],len);
    return text;
}
static DWORD Read32(const BYTE* p) {DWORD v;memcpy(&v,p,4);return v;}
static float ReadFloat(const BYTE* p) {float v;memcpy(&v,p,4);return v;}
static void Log(const wchar_t* text) {
    FILE* f=_wfopen(logPath,L"a, ccs=UTF-8");if(f){fwprintf(f,L"%ls\n",text);fclose(f);}
}
#include "CaptureRuntime.h"
#include "ApiRuntime.h"
#include "FrameCapture.h"
#include "ResolutionEngine.h"

static std::wstring SensorKey(DWORD source,DWORD gpu,const char* name) {
    DWORD hash=2166136261u;
    for(int i=0;i<260 && name[i];++i)hash=(hash^(unsigned char)name[i])*16777619u;
    wchar_t text[80];swprintf(text,80,L"%08lX_%08lX_%08lX",(unsigned long)source,(unsigned long)gpu,(unsigned long)hash);
    return text;
}
static void PublishSensors(std::vector<Sensor>& next){
    const wchar_t* names[]={L"Framerate",L"Frametime",L"Minimum FPS",L"Average FPS",L"Maximum FPS",L"1% low FPS",L"0.1% low FPS"};
    for(DWORD id=0x50;id<=0x56;++id){
        bool found=false;for(const auto& sensor:next)if(sensor.source==id){found=true;break;}
        if(found)continue;
        Sensor sensor; sensor.source=id;sensor.gpu=0xFFFFFFFF; sensor.flags=0;
        sensor.name=names[id-0x50];sensor.units=id==0x51?L"ms":L"FPS";
        sensor.key=L"IndependentFrame_"+std::to_wstring(id);
        sensor.selected=followAfterburner?(id==0x50||id==0x51||id==0x55):GetPrivateProfileIntW(L"Sensors",sensor.key.c_str(),0,iniPath)!=0;
        sensor.value=sensor.selected?(float)FrameValue(id):NAN;sensor.valid=isfinite(sensor.value);next.push_back(sensor);
    }
    sensors.swap(next);
    for(const Sensor& sensor:sensors)if(sensor.selected){auto& values=history[sensor.key];values.push_back(sensor.valid?sensor.value:NAN);if(values.size()>60)values.pop_front();}
    ++updateCount;
}
static bool ReadSensors() {
    HANDLE map=OpenFileMappingW(FILE_MAP_READ,FALSE,L"MAHMSharedMemory");
    if(!map){statusText=L"请运行 MSI Afterburner；本程序无需 RTSS。";std::vector<Sensor> next;PublishSensors(next);return false;}
    const BYTE* view=(const BYTE*)MapViewOfFile(map,FILE_MAP_READ,0,0,0);
    if(!view){CloseHandle(map);statusText=L"无法读取 Afterburner 数据，请检查运行权限。";std::vector<Sensor> next;PublishSensors(next);return false;}
    MEMORY_BASIC_INFORMATION region={};VirtualQuery(view,&region,sizeof(region));
    bool valid=region.RegionSize>=32 && Read32(view)==0x4D41484D && Read32(view+4)==0x20000;
    DWORD header=valid?Read32(view+8):0,count=valid?Read32(view+12):0,stride=valid?Read32(view+16):0;
    valid=valid && header>=32 && stride>=1324 && count<=4096 &&
        (ULONGLONG)header+(ULONGLONG)stride*count<=region.RegionSize;
    std::vector<Sensor> next;
    DWORD stamp=valid?Read32(view+20):0;
    if(valid) {
        next.reserve(count);
        DWORD gpuCount=Read32(view+24),gpuStride=Read32(view+28);
        ULONGLONG gpuOffset=(ULONGLONG)header+(ULONGLONG)stride*count;
        std::map<DWORD,std::wstring> devices;
        if(gpuCount<=64&&gpuStride>=1304&&gpuOffset+(ULONGLONG)gpuStride*gpuCount<=region.RegionSize){
            for(DWORD i=0;i<gpuCount;++i)devices[i]=Wide((const char*)view+(size_t)gpuOffset+(size_t)i*gpuStride+520,260);
        }
        std::map<DWORD,bool> activeDevices;
        for(DWORD i=0;i<count;++i) {
            // Copy fixed fields to avoid retaining any pointers into another process's view.
            BYTE entry[1324];memcpy(entry,view+header+(size_t)i*stride,sizeof(entry));
            Sensor s;
            s.source=Read32(entry+1320);s.gpu=Read32(entry+1316);s.flags=Read32(entry+1312);
            s.key=SensorKey(s.source,s.gpu,(const char*)entry);
            s.name=Wide((const char*)entry,260);
            if(s.name.empty())s.name=Wide((const char*)entry+520,260);
            s.units=Wide((const char*)entry+260,260);
            if(entry[260]==(BYTE)0xB0 && (entry[261]=='C'||entry[261]=='F'))s.units=entry[261]=='C'?L"°C":L"°F";
            s.value=ReadFloat(entry+1300);s.valid=isfinite(s.value) && fabs(s.value)<FLT_MAX/2;
            if(IsFrameSource(s.source)){double v=FrameValue(s.source);s.value=(float)v;s.valid=isfinite(v);}
            if(s.gpu!=0xFFFFFFFFu && ((s.source<0x80 && !IsFrameSource(s.source)) ||
                (s.source>=0xB0 && s.source<=0xF0))){
                activeDevices[s.gpu]=true;
                if(s.name.find(L"GPU")!=0){wchar_t suffix[40];swprintf(suffix,40,L" [GPU %lu]",(unsigned long)s.gpu+1);s.name+=suffix;}
            }
            if(s.source==0xA0&&s.name==L"CPU clock")s.name=L"CPU报告频率（Afterburner）";
            bool publishedSelection=(s.flags&1)!=0;
            // Disabling Afterburner's RTSS link also clears its shared OSD flags.
            // Read the existing monitoring selection without enabling the server.
            if(followAfterburner&&afterburnerConfig[0]&&
                !GetPrivateProfileIntW(L"Settings",L"EnableServer",1,afterburnerConfig)){
                std::wstring section=L"Source "+Wide((const char*)entry,260);
                publishedSelection=GetPrivateProfileIntW(section.c_str(),L"ShowInOSD",publishedSelection,afterburnerProfile)!=0;
            }
            s.selected=followAfterburner ? publishedSelection :
                GetPrivateProfileIntW(L"Sensors",s.key.c_str(),0,iniPath)!=0;
            next.push_back(s);
        }
        hardwareCaption=L"AEROPANEL / ";
        for(const auto& device:activeDevices){auto name=devices.find(device.first);
            if(name!=devices.end()&&!name->second.empty())hardwareCaption+=L"GPU"+std::to_wstring(device.first+1)+L" · "+name->second+L"  ";}
        if(activeDevices.empty())hardwareCaption+=L"硬件数据：MSI Afterburner";
    }
    UnmapViewOfFile(view);CloseHandle(map);
    if(!valid){statusText=L"Afterburner 共享内存格式不匹配。";next.clear();PublishSensors(next);return false;}
    FILETIME ft;GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER now;now.LowPart=ft.dwLowDateTime;now.HighPart=ft.dwHighDateTime;
    ULONGLONG seconds=now.QuadPart/10000000ULL-11644473600ULL;
    bool stale=stamp && seconds>(ULONGLONG)stamp+10;
    if(stale)for(Sensor& s:next)if(!IsFrameSource(s.source))s.valid=false;
    statusText=stale?L"硬件数据源超过 10 秒未更新，显示 N/A。":L"";
    PublishSensors(next);return true;
}
static void RestorePosition() {
    if(!panelVisible)return;
    RECT r={xPos,yPos,xPos+panelWidth,yPos+panelHeight};
    if(!MonitorFromRect(&r,MONITOR_DEFAULTTONULL)){xPos=0;yPos=0;}
    if(IsIconic(panel))ShowWindow(panel,SW_SHOWNOACTIVATE);
    SetWindowPos(panel,HWND_TOPMOST,xPos,yPos,panelWidth,panelHeight,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
static void LoadSettings() {
    xPos=Setting(L"X",0);yPos=Setting(L"Y",0);panelWidth=Clamp(Setting(L"Width",700),320,3000);
    panelHeight=Clamp(Setting(L"Height",420),160,2000);interval=Clamp(Setting(L"RefreshMs",1000),250,5000);
    followAfterburner=GetPrivateProfileIntW(L"Selection",L"FollowAfterburner",1,iniPath)!=0;
    drawGraphs=Setting(L"Graphs",1)!=0;
    transparentBackground=Setting(L"Transparent",1)!=0;
    compactStyle=Setting(L"CompactStyle",1)!=0;
    clickThrough=Setting(L"ClickThrough",1)!=0;
    fontSize=Clamp(Setting(L"FontSize",26),12,36);
    captureEnabled=GetPrivateProfileIntW(L"Capture",L"Enabled",1,iniPath)!=0;
    apiAdmin=GetPrivateProfileIntW(L"Capture",L"APIAdmin",0,iniPath)!=0;
    configuredTarget=GetPrivateProfileIntW(L"Capture",L"TargetPID",0,iniPath);
}
static void ApplyAppearance() {
    LONG_PTR style=GetWindowLongPtrW(panel,GWL_EXSTYLE);
    style=transparentBackground?style|WS_EX_LAYERED:style&~WS_EX_LAYERED;
    style=transparentBackground&&clickThrough?style|WS_EX_TRANSPARENT:style&~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(panel,GWL_EXSTYLE,style);
    if(osdFont)DeleteObject(osdFont);
    osdFont=CreateFontW(-fontSize,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
}
static void ReleaseLayer() {
    if(layerDC&&previousBitmap)SelectObject(layerDC,previousBitmap);
    if(layerBitmap)DeleteObject(layerBitmap);if(layerDC)DeleteDC(layerDC);
    layerDC=NULL;layerBitmap=NULL;previousBitmap=NULL;layerWidth=layerHeight=0;
}
static void OutlinedText(HDC dc,int x,int y,const std::wstring& text,COLORREF color) {
    SetTextColor(dc,RGB(0,0,0));
    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)if(dx||dy)TextOutW(dc,x+dx,y+dy,text.c_str(),(int)text.size());
    SetTextColor(dc,color);TextOutW(dc,x,y,text.c_str(),(int)text.size());
}
static std::wstring ValueText(const Sensor& s) {
    if(!s.valid)return L"N/A"+(s.units.empty()?std::wstring():L" "+s.units);
    wchar_t value[90];int precision=0;
    if(s.units==L"V")precision=3;
    else if(s.units==L"W"||s.units==L"ms"||s.units==L"FPS")precision=1;
    swprintf(value,90,L"%.*f",precision,s.value);
    return std::wstring(value)+(s.units.empty()?L"":L" "+s.units);
}
static void DrawCurve(HDC dc,const Sensor& sensor,int x,int y,int width,int height,COLORREF color,int styleOverride=-1) {
    auto it=history.find(sensor.key);size_t valid=0;
    double low=HUGE_VAL,high=-HUGE_VAL;
    if(it!=history.end())for(float v:it->second)if(isfinite(v)){low=std::min(low,(double)v);high=std::max(high,(double)v);++valid;}
    if(!valid){HFONT f=(HFONT)SelectObject(dc,fontHandle);OutlinedText(dc,x+5,y+5,L"等待有效数据",RGB(165,165,165));SelectObject(dc,f);return;}
    if(high==low){double pad=std::max(1.0,fabs(low)*0.05);low-=pad;high+=pad;}
    else {double pad=(high-low)*0.08;low-=pad;high+=pad;}
    int mode=styleOverride>=0?styleOverride:Setting(L"GraphStyle",0);
    HPEN pen=CreatePen(PS_SOLID,2,color);HGDIOBJ oldPen=SelectObject(dc,pen);bool start=true;int i=0;
    int count=(int)it->second.size(),previousX=0,previousY=0;
    for(float v:it->second){int px=x+2+(60-count+i++)*(width-5)/59;
        if(!isfinite(v)){start=true;continue;}
        int py=y+height-3-(int)((height-5)*(v-low)/(high-low));
        if(mode==1){MoveToEx(dc,px,y+height-3,NULL);LineTo(dc,px,py);}
        else if(mode==2){if(!start){MoveToEx(dc,previousX,previousY,NULL);LineTo(dc,px,previousY);LineTo(dc,px,py);}}
        else {if(start)MoveToEx(dc,px,py,NULL);else LineTo(dc,px,py);}
        // A single sample must remain visible before the next poll arrives.
        SetPixelV(dc,px,py,color);previousX=px;previousY=py;start=false;
    }
    SelectObject(dc,oldPen);DeleteObject(pen);
}
static void PaintCompact(HDC dc,RECT rect) {
    HFONT old=(HFONT)SelectObject(dc,osdFont);SetBkMode(dc,TRANSPARENT);
    std::map<int,std::vector<const Sensor*>> groups;
    int graphScope=Setting(L"GraphScope",0);
    auto hasGraph=[&](const Sensor& s){return drawGraphs&&(graphScope==1||s.source==0x50||s.source==0x51);};
    int extra=11000;
    for(const Sensor& s:sensors)if(s.selected&&!hasGraph(s)){int group=extra++;
        DWORD gpu=s.gpu==0xFFFFFFFF?0:s.gpu;
        if(gpu<64){
            if(s.source==0||s.source==0x30||s.source==0x20||s.source==0x61||s.source==0x40)group=(int)gpu*2;
            else if(s.source==0x31||s.source==0x22)group=(int)gpu*2+1;
        }
        if(s.source==0x80||s.source==0x90||s.source==0xA0||s.source==0x100)group=10000;
        if(s.source==0x91||s.source==0x92)group=10001;
        if(IsFrameSource(s.source))group=10002;
        groups[group].push_back(&s);
    }
    int row=0,y=0,line=fontSize+5;
    for(const auto& group:groups){if(row++<scrollRow)continue;if(y+line>rect.bottom)break;
        std::wstring label;COLORREF color=RGB(0,180,245);
        if(group.first<128){wchar_t number[20];swprintf(number,20,L"%d",group.first/2+1);
            label=(group.first%2?L"MEM":L"GPU")+std::wstring(number);color=RGB(0,180,90);}
        else if(group.first==10000)label=L"CPU";
        else if(group.first==10001)label=L"RAM";
        else if(group.first==10002){label=FrameApiLabel();color=RGB(195,125,150);}
        else label=group.second.front()->name;
        OutlinedText(dc,0,y,label,color);SIZE size;
        GetTextExtentPoint32W(dc,label.c_str(),(int)label.size(),&size);
        int startX=std::max(78,(int)size.cx+17),x=startX;
        for(const Sensor* sensor:group.second){std::wstring text=ValueText(*sensor);
            if(sensor->source>=0x52&&sensor->source<=0x56){
                const wchar_t* names[]={L"min ",L"avg ",L"max ",L"1% ",L"0.1% "};text=names[sensor->source-0x52]+text;
            }else if(group.first==10000&&sensor->name.size()>3&&sensor->name[3]>=L'0'&&sensor->name[3]<=L'9'){
                size_t end=3;while(end<sensor->name.size()&&sensor->name[end]>=L'0'&&sensor->name[end]<=L'9')++end;
                text=L"C"+sensor->name.substr(3,end-3)+L" "+text;
            }
            GetTextExtentPoint32W(dc,text.c_str(),(int)text.size(),&size);
            if(x>startX && x+size.cx>rect.right-5){x=startX;y+=line;}
            if(y+line>rect.bottom)break;
            OutlinedText(dc,x,y,text,sensor->valid?RGB(255,145,0):RGB(165,165,165));x+=size.cx+14;
        }y+=line;
    }
    if(!statusText.empty()){OutlinedText(dc,0,y,statusText,RGB(255,145,0));y+=line;}
    if(groups.empty()&&statusText.empty()&&!drawGraphs)OutlinedText(dc,0,y,L"托盘右键 → 选择传感器",RGB(255,145,0));
    if(drawGraphs)for(const Sensor& s:sensors)if(s.selected&&hasGraph(s)&&y+line+42<rect.bottom){
        std::wstring graphTitle=s.source==0x50?L"Framerate":s.source==0x51?L"Frametime":s.name;
        if(s.source==0x50){auto api=FrameApiLabel();if(api!=L"FPS")graphTitle+=L" · "+api;}
        OutlinedText(dc,0,y,graphTitle,RGB(195,125,150));
        y+=line;
        std::wstring graphValue=ValueText(s);SIZE valueSize={};
        GetTextExtentPoint32W(dc,graphValue.c_str(),(int)graphValue.size(),&valueSize);
        int curveWidth=std::min(330,std::max(0,(int)rect.right-(int)valueSize.cx-14));
        if(curveWidth>=12)DrawCurve(dc,s,0,y,curveWidth,36,RGB(195,125,150));
        OutlinedText(dc,curveWidth+10,y+std::max(0,(36-(int)valueSize.cy)/2),graphValue,
            s.valid?RGB(255,145,0):RGB(165,165,165));
        y+=std::max(44,(int)valueSize.cy+8);}
    SelectObject(dc,old);
}
static void PaintPanel(HDC dc,RECT rect) {
    HBRUSH bg=CreateSolidBrush(transparentBackground?TRANSPARENT_KEY:RGB(18,20,24));FillRect(dc,&rect,bg);DeleteObject(bg);
    if(compactStyle){PaintCompact(dc,rect);return;}
    HFONT old=(HFONT)SelectObject(dc,fontHandle);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(210,225,230));
    TextOutW(dc,10,8,L"硬件监控",4);
    std::wstring caption=fpsStatus;if(fpsProcess && !fpsTargetName.empty())caption+=L" · "+fpsTargetName;
    TextOutW(dc,10,32,caption.c_str(),(int)caption.size());
    if(!statusText.empty()){SetTextColor(dc,RGB(255,190,100));TextOutW(dc,10,58,statusText.c_str(),(int)statusText.size());}
    int row=0,shown=0,y=statusText.empty()?58:85;
    for(const Sensor& s:sensors)if(s.selected){
        if(row++<scrollRow)continue;
        if(y+26>rect.bottom-18)break;
        SetTextColor(dc,s.valid?RGB(130,235,190):RGB(160,160,160));
        RECT nameRect={10,y,panelWidth-245,y+25};DrawTextW(dc,s.name.c_str(),(int)s.name.size(),&nameRect,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        wchar_t number[100];if(s.valid)swprintf(number,100,L"%.2f %ls",s.value,s.units.c_str());else wcscpy(number,L"N/A");
        TextOutW(dc,panelWidth-235,y,number,(int)wcslen(number));
        if(drawGraphs)DrawCurve(dc,s,panelWidth-100,y,90,23,RGB(80,180,240));y+=27;++shown;
    }
    if(!shown&&statusText.empty()){const wchar_t* msg=L"右键 → 选择传感器";TextOutW(dc,10,70,msg,(int)wcslen(msg));}
    SetTextColor(dc,RGB(140,150,160));const wchar_t* hint=L"右键设置 · 滚轮查看更多";TextOutW(dc,10,rect.bottom-19,hint,(int)wcslen(hint));
    SelectObject(dc,old);
}
static void RefreshPanel() {
    if(!panelVisible)return;
    static ULONGLONG lastDesktopPaint=0;ULONGLONG now=GetTickCount64();
    if(DesktopIdle()){if(lastDesktopPaint&&now-lastDesktopPaint<1000)return;lastDesktopPaint=now;}else lastDesktopPaint=0;
    if(!transparentBackground){InvalidateRect(panel,NULL,FALSE);return;}
    if(!layerDC||layerWidth!=panelWidth||layerHeight!=panelHeight){
        ReleaseLayer();HDC screen=GetDC(NULL);layerDC=CreateCompatibleDC(screen);
        BITMAPINFO bitmap={};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth=panelWidth;bitmap.bmiHeader.biHeight=-panelHeight;
        bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
        void* pixels=NULL;layerBitmap=CreateDIBSection(screen,&bitmap,DIB_RGB_COLORS,&pixels,NULL,0);ReleaseDC(NULL,screen);
        if(!layerDC||!layerBitmap){ReleaseLayer();Log(L"Transparent surface allocation failed.");return;}
        previousBitmap=(HBITMAP)SelectObject(layerDC,layerBitmap);layerWidth=panelWidth;layerHeight=panelHeight;
    }
    RECT rect={0,0,panelWidth,panelHeight};PaintPanel(layerDC,rect);
    POINT position={xPos,yPos},source={0,0};SIZE size={panelWidth,panelHeight};
    if(!UpdateLayeredWindow(panel,NULL,&position,&size,layerDC,&source,TRANSPARENT_KEY,NULL,ULW_COLORKEY))Log(L"UpdateLayeredWindow failed.");
}
static void SaveRenderDiagnostics(){
    if(!layerDC||!layerBitmap)return;
    RefreshPanel();
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=layerWidth;
    info.bmiHeader.biHeight=-layerHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<BYTE> bits((size_t)layerWidth*layerHeight*4);
    SelectObject(layerDC,previousBitmap);
    int copied=GetDIBits(layerDC,layerBitmap,0,layerHeight,bits.data(),&info,DIB_RGB_COLORS);
    SelectObject(layerDC,layerBitmap);
    if(copied){BITMAPFILEHEADER header={};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+(DWORD)bits.size();
        std::wstring path=std::wstring(folder)+L"\\render-check.bmp";FILE* file=_wfopen(path.c_str(),L"wb");
        if(file){fwrite(&header,sizeof(header),1,file);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,file);fwrite(bits.data(),bits.size(),1,file);fclose(file);}}
    for(const Sensor& sensor:sensors)if(sensor.selected){size_t valid=0;for(float v:history[sensor.key])if(isfinite(v))++valid;
        std::wstring report=L"GRAPH CHECK: "+sensor.name+L" samples="+std::to_wstring(history[sensor.key].size())+L" valid="+std::to_wstring(valid)+L" latest="+ValueText(sensor);Log(report.c_str());}
    Log(fpsStatus.c_str());
}
static bool VerifyCurveRendering(){
    HDC dc=CreateCompatibleDC(NULL);BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=180;info.bmiHeader.biHeight=-50;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    HGDIOBJ old=SelectObject(dc,bitmap);Sensor sensor={};sensor.key=L"__curve_validation__";
    bool ok=true;COLORREF color=RGB(210,110,155);
    for(int style=0;style<3;++style)for(int pattern=0;pattern<3;++pattern){
        auto& data=history[sensor.key];data.clear();
        for(int i=0;i<60;++i)data.push_back(pattern==0?60.f:pattern==2&&i>=20&&i<30?NAN:(float)(40+i%17));
        PatBlt(dc,0,0,180,50,BLACKNESS);DrawCurve(dc,sensor,0,0,180,50,color,style);
        unsigned count=0;int minY=50,maxY=-1;
        for(int y=0;y<50;++y)for(int x=0;x<180;++x)if(GetPixel(dc,x,y)==color){++count;minY=std::min(minY,y);maxY=std::max(maxY,y);}
        bool pass=count>30&&minY>0&&maxY<49;
        if(pattern==0&&style!=1)pass=pass&&maxY-minY<=2&&minY>15&&maxY<35;
        ok=ok&&pass;
        std::wstring report=L"CURVE RENDER "+std::wstring(pass?L"PASS":L"FAIL")+L": style="+std::to_wstring(style)+L" pattern="+std::to_wstring(pattern)+L" coloredPixels="+std::to_wstring(count);Log(report.c_str());
    }
    history.erase(sensor.key);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
#include "Theme.h"
static HICON CreateTrayIcon() {
    HICON artwork=(HICON)LoadImageW(appInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,0);
    if(artwork)return artwork;
    HDC screen=GetDC(NULL),dc=CreateCompatibleDC(screen);BITMAPINFO info={};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=32;info.bmiHeader.biHeight=-32;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=NULL;
    HBITMAP image=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,NULL,0);ReleaseDC(NULL,screen);
    if(!dc||!image){if(dc)DeleteDC(dc);if(image)DeleteObject(image);return NULL;}
    HGDIOBJ old=SelectObject(dc,image);RECT r={0,0,32,32};HBRUSH bg=CreateSolidBrush(RGB(18,30,44));FillRect(dc,&r,bg);DeleteObject(bg);
    HBRUSH bars=CreateSolidBrush(RGB(0,190,245));RECT a={5,18,10,27},b={13,11,18,27},c={21,5,26,27};
    FillRect(dc,&a,bars);FillRect(dc,&b,bars);FillRect(dc,&c,bars);DeleteObject(bars);
    GdiFlush();for(int i=0;i<32*32;++i)((DWORD*)pixels)[i]|=0xFF000000;
    SelectObject(dc,old);DeleteDC(dc);BYTE maskBits[128]={};HBITMAP mask=CreateBitmap(32,32,1,1,maskBits);
    ICONINFO icon={};icon.fIcon=TRUE;icon.hbmColor=image;icon.hbmMask=mask;HICON result=CreateIconIndirect(&icon);
    DeleteObject(image);DeleteObject(mask);return result;
}
static void AddTray() {
    if(!trayIcon)trayIcon=CreateTrayIcon();
    ZeroMemory(&trayData,sizeof(trayData));trayData.cbSize=sizeof(trayData);trayData.hWnd=panel;trayData.uID=1;
    trayData.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;trayData.uCallbackMessage=TRAY_MESSAGE;trayData.hIcon=trayIcon;
    wcscpy(trayData.szTip,L"AeroPanel · 单击打开设置，右键菜单");
    trayAdded=Shell_NotifyIconW(NIM_ADD,&trayData)!=FALSE;
    if(trayAdded){trayData.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&trayData);Log(L"Tray icon added.");}
    else Log(L"Tray icon add failed; will retry.");
}
static void DeleteTray() {if(trayAdded)Shell_NotifyIconW(NIM_DELETE,&trayData);trayAdded=false;}
static void SetLocal(const wchar_t* key,int value){wchar_t text[40];swprintf(text,40,L"%d",value);WritePrivateProfileStringW(L"Window",key,text,iniPath);}
static HWND SettingControl(HWND h,const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0){
    HWND c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,h,(HMENU)(INT_PTR)id,appInstance,NULL);
    SendMessageW(c,WM_SETFONT,(WPARAM)fontHandle,TRUE);return c;
}
#include "ResolutionUI.h"

static void SelectSettingsSection(int section){
    settingsSection=section;
    for(int id=10;id<=47;++id){HWND item=GetDlgItem(picker,id);if(item)ShowWindow(item,section==0?SW_SHOW:SW_HIDE);}
    resolutionui::Show(section==1);
    resolutionui::Status();
    RedrawWindow(picker,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
static BOOL CALLBACK ApplySettingsFont(HWND control,LPARAM){
    HFONT font=GetDlgCtrlID(control)==45?settingsPreviewFont:settingsFont;
    if(font)SendMessageW(control,WM_SETFONT,(WPARAM)font,FALSE);
    return TRUE;
}
static void ScaleSettingsFonts(HWND h,double scale){
    int pixels=std::max(12,(int)lround(16*scale));
    HFONT oldFont=NULL,oldPreview=NULL;
    if(pixels!=settingsFontPixels||!settingsFont||!settingsPreviewFont){
        HFONT font=CreateFontW(-pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        HFONT preview=CreateFontW(-MulDiv(fontSize,pixels,16),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
        if(!font||!preview){if(font)DeleteObject(font);if(preview)DeleteObject(preview);return;}
        oldFont=settingsFont;oldPreview=settingsPreviewFont;
        settingsFont=font;settingsPreviewFont=preview;settingsFontPixels=pixels;
    }
    EnumChildWindows(h,ApplySettingsFont,0);
    if(oldFont)DeleteObject(oldFont);if(oldPreview)DeleteObject(oldPreview);
}
static void LayoutPicker(HWND h){
    RECT r;GetClientRect(h,&r);int width=r.right,height=r.bottom;
    if(width<=0||height<=0)return;
    double scale=std::max(0.85,std::min(3.0,std::min(width/720.0,height/640.0)));
    auto px=[&](int value){return (int)lround(value*scale);};
    ScaleSettingsFonts(h,scale);
    int tabHeight=px(40);
    int buttons=height-tabHeight-px(44),left=Clamp(width*43/100,px(300),std::max(px(300),width-px(390)));
    int right=left+px(34),rightWidth=std::max(px(240),width-right-px(12)),field=right+px(124);
    HDWP batch=BeginDeferWindowPos(30);
    auto place=[&](int id,int x,int y,int w,int ht){HWND control=GetDlgItem(h,id);if(!control)return;
        y+=tabHeight;
        if(batch)batch=DeferWindowPos(batch,control,NULL,x,y,w,ht,SWP_NOZORDER|SWP_NOACTIVATE);
        else MoveWindow(control,x,y,w,ht,TRUE);
    };
    place(15,px(12),px(10),width-px(24),px(24));place(16,px(12),px(44),left,px(24));place(17,right,px(44),rightWidth,px(24));
    place(10,px(12),px(72),left,std::max(px(80),buttons-px(84)));
    place(11,px(12),buttons,px(90),px(30));place(12,px(112),buttons,px(90),px(30));
    place(13,width-px(202),buttons,px(90),px(30));place(14,width-px(102),buttons,px(90),px(30));
    place(36,right,px(78),rightWidth,px(25));
    place(40,right,px(112),px(110),px(24));place(30,field,px(108),rightWidth-px(124),px(180));
    place(41,right,px(148),px(110),px(24));place(31,field,px(144),rightWidth-px(124),px(180));
    place(42,right,px(186),px(110),px(24));place(32,field,px(182),px(85),px(26));
    int inputWidth=std::min(px(160),std::max(px(85),(rightWidth-px(136))/2));
    place(43,right,px(224),px(110),px(24));place(33,field,px(220),inputWidth,px(26));place(34,field+inputWidth+px(12),px(220),inputWidth,px(26));
    place(37,right,px(262),rightWidth/2,px(25));place(38,right+rightWidth/2,px(262),rightWidth/2,px(25));
    place(35,right,px(304),rightWidth,px(25));
    place(44,right,px(346),px(180),px(24));place(39,right+px(196),px(342),rightWidth-px(196),px(26));
    place(46,right,px(388),rightWidth,px(60));place(45,right,px(462),rightWidth,px(50));place(47,right,px(514),rightWidth,px(24));
    if(batch)EndDeferWindowPos(batch);
    MoveWindow(GetDlgItem(h,80),px(12),px(5),px(150),px(30),FALSE);
    MoveWindow(GetDlgItem(h,81),px(174),px(5),px(170),px(30),FALSE);
    MoveWindow(GetDlgItem(h,82),width-px(124),px(5),px(112),px(30),FALSE);
    MoveWindow(GetDlgItem(h,83),px(356),px(5),px(160),px(30),FALSE);
    resolutionui::Layout(h,scale,width,height);
    RedrawWindow(h,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
static void ToggleSettingsFullscreen(){
    if(!picker)return;
    if(!settingsFullscreen){
        MONITORINFO monitor={sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(picker,MONITOR_DEFAULTTONEAREST),&monitor))return;
        settingsPlacement.length=sizeof(settingsPlacement);
        if(!GetWindowPlacement(picker,&settingsPlacement))return;
        settingsWindowStyle=GetWindowLongPtrW(picker,GWL_STYLE);
        if(IsZoomed(picker))ShowWindow(picker,SW_RESTORE);
        settingsFullscreen=true;
        SetWindowLongPtrW(picker,GWL_STYLE,settingsWindowStyle&~(WS_OVERLAPPEDWINDOW|WS_MAXIMIZE|WS_MINIMIZE));
        SetWindowPos(picker,HWND_TOPMOST,monitor.rcMonitor.left,monitor.rcMonitor.top,
            monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED);
    }else{
        settingsFullscreen=false;
        SetWindowLongPtrW(picker,GWL_STYLE,settingsWindowStyle);
        SetWindowPlacement(picker,&settingsPlacement);
        SetWindowPos(picker,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    }
}
static LRESULT CALLBACK PickerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CREATE){
        picker=h;
        CreateSettingsTheme(h);
        BOOL dark=TRUE;DwmSetWindowAttribute(h,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        HWND label=CreateWindowW(L"STATIC",hardwareCaption.c_str(),WS_CHILD|WS_VISIBLE,12,10,600,24,h,(HMENU)15,appInstance,NULL);
        SendMessageW(label,WM_SETFONT,(WPARAM)fontHandle,TRUE);
        SettingControl(h,L"STATIC",L"监控项目 · 点击切换显示",16,12,44,310,24);
        SettingControl(h,L"STATIC",L"显示设置",17,344,44,400,24);
        sensorList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_MULTIPLESEL|LBS_NOINTEGRALHEIGHT,12,40,610,380,h,(HMENU)10,appInstance,NULL);
        SendMessageW(sensorList,WM_SETFONT,(WPARAM)fontHandle,TRUE);
        pickerSnapshot=sensors;
        for(size_t i=0;i<pickerSnapshot.size();++i){std::wstring label=pickerSnapshot[i].name+L"  ("+pickerSnapshot[i].units+L")";
            SendMessageW(sensorList,LB_ADDSTRING,0,(LPARAM)label.c_str());SendMessageW(sensorList,LB_SETSEL,pickerSnapshot[i].selected,(LPARAM)i);}
        HWND c=SettingControl(h,L"BUTTON",L"显示监控图表",36,344,78,230,25,BS_AUTOCHECKBOX);
        SendMessageW(c,BM_SETCHECK,drawGraphs?BST_CHECKED:BST_UNCHECKED,0);
        SettingControl(h,L"STATIC",L"图表形式",40,344,112,110,24);
        c=SettingControl(h,L"COMBOBOX",L"",30,468,108,238,180,CBS_DROPDOWNLIST|WS_VSCROLL);
        for(auto text:{L"折线",L"柱状图",L"阶梯线"})SendMessageW(c,CB_ADDSTRING,0,(LPARAM)text);
        SendMessageW(c,CB_SETCURSEL,Clamp(Setting(L"GraphStyle",0),0,2),0);
        SettingControl(h,L"STATIC",L"图表项目",41,344,148,110,24);
        c=SettingControl(h,L"COMBOBOX",L"",31,468,144,238,180,CBS_DROPDOWNLIST|WS_VSCROLL);
        SendMessageW(c,CB_ADDSTRING,0,(LPARAM)L"帧率和帧时间");SendMessageW(c,CB_ADDSTRING,0,(LPARAM)L"全部已选传感器");
        SendMessageW(c,CB_SETCURSEL,Clamp(Setting(L"GraphScope",0),0,1),0);
        SettingControl(h,L"STATIC",L"OSD 字号",42,344,186,110,24);
        c=SettingControl(h,L"EDIT",std::to_wstring(fontSize).c_str(),32,468,182,85,26,WS_BORDER|ES_NUMBER);
        SettingControl(h,L"STATIC",L"位置 X / Y",43,344,224,110,24);
        SettingControl(h,L"EDIT",std::to_wstring(xPos).c_str(),33,468,220,85,26,WS_BORDER);
        SettingControl(h,L"EDIT",std::to_wstring(yPos).c_str(),34,568,220,85,26,WS_BORDER);
        c=SettingControl(h,L"BUTTON",L"透明背景",37,344,262,150,25,BS_AUTOCHECKBOX);SendMessageW(c,BM_SETCHECK,transparentBackground,0);
        c=SettingControl(h,L"BUTTON",L"鼠标穿透",38,520,262,150,25,BS_AUTOCHECKBOX);SendMessageW(c,BM_SETCHECK,clickThrough,0);
        c=SettingControl(h,L"BUTTON",L"自动启动独立 FPS 采集",35,344,304,350,25,BS_AUTOCHECKBOX);SendMessageW(c,BM_SETCHECK,captureEnabled,0);
        SettingControl(h,L"STATIC",L"目标 PID (0 自动)",44,344,346,180,24);
        SettingControl(h,L"EDIT",std::to_wstring(configuredTarget).c_str(),39,540,342,166,26,WS_BORDER|ES_NUMBER);
        SettingControl(h,L"STATIC",fpsStatus.c_str(),46,344,388,366,60);
        SettingControl(h,L"STATIC",L"60",45,344,462,230,50);
        SendMessageW(GetDlgItem(h,45),WM_SETFONT,(WPARAM)osdFont,TRUE);
        SettingControl(h,L"STATIC",L"F11 全屏 / Esc 返回 · 坐标原点：左上角",47,344,514,366,24);
        SetTimer(h,1,1000,NULL);
        CreateWindowW(L"BUTTON",L"全选",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,432,90,30,h,(HMENU)11,appInstance,NULL);
        CreateWindowW(L"BUTTON",L"清空",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,112,432,90,30,h,(HMENU)12,appInstance,NULL);
        CreateWindowW(L"BUTTON",L"保存",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,432,432,90,30,h,(HMENU)13,appInstance,NULL);
        CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,532,432,90,30,h,(HMENU)14,appInstance,NULL);
        SettingControl(h,L"BUTTON",L"硬件监控",80,0,0,1,1,BS_OWNERDRAW|WS_TABSTOP);
        SettingControl(h,L"BUTTON",L"分辨率切换",81,0,0,1,1,BS_OWNERDRAW|WS_TABSTOP);
        SettingControl(h,L"BUTTON",L"关于 / 许可",82,0,0,1,1,BS_OWNERDRAW|WS_TABSTOP);
        c=SettingControl(h,L"BUTTON",L"银狼背景",83,0,0,1,1,BS_AUTOCHECKBOX|WS_TABSTOP);
        SendMessageW(c,BM_SETCHECK,settingsArtworkEnabled?BST_CHECKED:BST_UNCHECKED,0);
        resolutionui::Create(h);resolutionui::Refresh();resolutionui::CurrentMode();
        LayoutPicker(h);SelectSettingsSection(settingsSection);return 0;
    }
    if(m==WM_TIMER){if(settingsSection==0)SetWindowTextW(GetDlgItem(h,46),fpsStatus.c_str());else resolutionui::Status();return 0;}
    if(m==WM_GETMINMAXINFO&&!settingsFullscreen){
        RECT minimum={0,0,720,640};
        AdjustWindowRectEx(&minimum,(DWORD)GetWindowLongPtrW(h,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtrW(h,GWL_EXSTYLE));
        MINMAXINFO* limits=(MINMAXINFO*)l;
        limits->ptMinTrackSize.x=minimum.right-minimum.left;
        limits->ptMinTrackSize.y=minimum.bottom-minimum.top;
        return 0;
    }
    if(m==WM_SIZE){if(w!=SIZE_MINIMIZED)LayoutPicker(h);return 0;}
    if(m==WM_COMMAND){int id=LOWORD(w);
        if(id==83){
            settingsArtworkEnabled=SendMessageW(GetDlgItem(h,83),BM_GETCHECK,0,0)==BST_CHECKED;
            SetLocal(L"SilverWolfBackground",settingsArtworkEnabled?1:0);
            if(settingsArtworkEnabled)LoadSettingsArtwork();else ReleaseSettingsArtwork();
            RedrawWindow(h,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);return 0;
        }
        if(id==80||id==81){SelectSettingsSection(id-80);return 0;}
        if(id==82){std::wstring notice=L"AeroPanel v3.0.1\n硬件监控 + 分辨率切换\n功能参考：github.com/Drinkwater10086/aeropanel\n分辨率模块独立原生实现。\n\n内嵌 PresentMon 2.3.1 许可证：\n"+Wide(CaptureLicense,sizeof(CaptureLicense)-1);MessageBoxW(h,notice.c_str(),L"关于与第三方许可",MB_ICONINFORMATION);return 0;}
        if(id>=100&&id<=121){resolutionui::Command(id);return 0;}
        if(id==11||id==12){SendMessageW(sensorList,LB_SETSEL,id==11,-1);return 0;}
        if(id==13){WritePrivateProfileStringW(L"Selection",L"FollowAfterburner",L"0",iniPath);
            for(size_t i=0;i<pickerSnapshot.size();++i)WritePrivateProfileStringW(L"Sensors",pickerSnapshot[i].key.c_str(),SendMessageW(sensorList,LB_GETSEL,i,0)>0?L"1":L"0",iniPath);
            SetLocal(L"GraphStyle",(int)SendMessageW(GetDlgItem(h,30),CB_GETCURSEL,0,0));
            SetLocal(L"GraphScope",(int)SendMessageW(GetDlgItem(h,31),CB_GETCURSEL,0,0));
            BOOL ok=FALSE;int size=GetDlgItemInt(h,32,&ok,FALSE);if(ok)SetLocal(L"FontSize",Clamp(size,12,36));
            int pos=(int)GetDlgItemInt(h,33,&ok,TRUE);if(ok)SetLocal(L"X",pos);
            pos=(int)GetDlgItemInt(h,34,&ok,TRUE);if(ok)SetLocal(L"Y",pos);
            SetLocal(L"Graphs",SendMessageW(GetDlgItem(h,36),BM_GETCHECK,0,0)==BST_CHECKED);
            SetLocal(L"Transparent",SendMessageW(GetDlgItem(h,37),BM_GETCHECK,0,0)==BST_CHECKED);
            SetLocal(L"ClickThrough",SendMessageW(GetDlgItem(h,38),BM_GETCHECK,0,0)==BST_CHECKED);
            bool enabled=SendMessageW(GetDlgItem(h,35),BM_GETCHECK,0,0)==BST_CHECKED;
            WritePrivateProfileStringW(L"Capture",L"Enabled",enabled?L"1":L"0",iniPath);
            wchar_t pid[40];GetDlgItemTextW(h,39,pid,40);WritePrivateProfileStringW(L"Capture",L"TargetPID",pid,iniPath);
            LoadSettings();if(captureEnabled)StartFrames();else StopFrames();
            scrollRow=0;ApplyAppearance();RestorePosition();ReadSensors();RefreshPanel();DestroyWindow(h);return 0;}
        if(id==14){DestroyWindow(h);return 0;}
    }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){HDC dc=(HDC)w;
        PaintSettingsBackground(h,dc,(HWND)l);SetTextColor(dc,RGB(225,230,240));SetBkMode(dc,TRANSPARENT);
        return (LRESULT)GetStockObject(HOLLOW_BRUSH);}
    if(m==WM_CTLCOLORLISTBOX||m==WM_CTLCOLOREDIT){HDC dc=(HDC)w;
        SetTextColor(dc,RGB(210,225,235));SetBkColor(dc,RGB(35,39,45));return (LRESULT)settingsBrush;}
    if(m==WM_ERASEBKGND){PaintSettingsBackground(h,(HDC)w);return 1;}
    if(m==WM_DRAWITEM){DRAWITEMSTRUCT* item=(DRAWITEMSTRUCT*)l;if(item->CtlType==ODT_BUTTON){
        HBRUSH brush=CreateSolidBrush((item->itemState&ODS_SELECTED)||(item->CtlID==80+settingsSection)?RGB(42,78,100):RGB(49,56,65));
        FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
        brush=CreateSolidBrush(RGB(0,150,210));FrameRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
        wchar_t text[80];GetWindowTextW(item->hwndItem,text,80);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(225,235,240));
        HFONT old=(HFONT)SelectObject(item->hDC,settingsFont?settingsFont:fontHandle);DrawTextW(item->hDC,text,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);
        if(item->itemState&ODS_FOCUS){RECT r=item->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(item->hDC,&r);}return TRUE;}}
    if(m==WM_DESTROY){settingsFullscreen=false;picker=NULL;return 0;}
    if(m==WM_NCDESTROY){
        ReleaseSettingsTheme();
        if(settingsFont)DeleteObject(settingsFont);if(settingsPreviewFont)DeleteObject(settingsPreviewFont);
        settingsFont=settingsPreviewFont=NULL;settingsFontPixels=0;
    }
    return DefWindowProcW(h,m,w,l);
}
static void OpenPicker(bool activate=true){
    if(picker){if(IsIconic(picker))ShowWindow(picker,SW_RESTORE);SetForegroundWindow(picker);return;}
    picker=CreateWindowExW(WS_EX_TOPMOST|WS_EX_APPWINDOW,L"AeroPanelPicker",L"AeroPanel · 设置",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,200,100,750,700,panel,NULL,appInstance,NULL);
    ShowWindow(picker,activate?SW_SHOW:SW_SHOWNOACTIVATE);
    if(activate)SetForegroundWindow(picker);
}
static void SetApiAdmin(bool enabled){
    StopApi();apiAdmin=enabled;if(captureEnabled)StartApi();
    if(enabled&&!apiBroker.process){apiAdmin=false;if(captureEnabled)StartApi();}
    WritePrivateProfileStringW(L"Capture",L"APIAdmin",apiAdmin?L"1":L"0",iniPath);
}
static void ShowMenu(){
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,6,panelVisible?L"隐藏监控显示":L"显示监控");
    AppendMenuW(menu,MF_STRING,1,L"监控设置 / 传感器…");
    AppendMenuW(menu,MF_STRING,2,L"使用 Afterburner 当前 OSD 选择");
    AppendMenuW(menu,MF_STRING,3,fpsProcess?L"停止独立 FPS 采集":L"启用独立 FPS 采集");
    AppendMenuW(menu,MF_STRING|(apiAdmin?MF_CHECKED:0),14,L"增强 API 识别（管理员）");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING|(transparentBackground?MF_CHECKED:0),7,L"透明背景");
    AppendMenuW(menu,MF_STRING|(clickThrough?MF_CHECKED:0),8,L"鼠标穿透（透明模式）");
    AppendMenuW(menu,MF_STRING|(compactStyle?MF_CHECKED:0),9,L"RTSS 风格紧凑显示");
    AppendMenuW(menu,MF_STRING|(drawGraphs?MF_CHECKED:0),12,L"显示曲线");
    AppendMenuW(menu,MF_STRING,10,L"放大监控字体");AppendMenuW(menu,MF_STRING,11,L"缩小监控字体");
    HMENU positions=CreatePopupMenu();
    AppendMenuW(positions,MF_STRING,20,L"左上角");AppendMenuW(positions,MF_STRING,21,L"右上角");
    AppendMenuW(positions,MF_STRING,22,L"左下角");AppendMenuW(positions,MF_STRING,23,L"右下角");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)positions,L"移动到当前显示器");
    AppendMenuW(menu,MF_STRING,4,L"重新读取本地设置");AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,5,L"退出");
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(panel);
    UINT id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,panel,NULL);DestroyMenu(menu);PostMessageW(panel,WM_NULL,0,0);
    if(id==1)OpenPicker();
    if(id==2){followAfterburner=true;WritePrivateProfileStringW(L"Selection",L"FollowAfterburner",L"1",iniPath);scrollRow=0;ReadSensors();}
    if(id==3){captureEnabled=!captureEnabled;WritePrivateProfileStringW(L"Capture",L"Enabled",captureEnabled?L"1":L"0",iniPath);if(captureEnabled)StartFrames();else StopFrames();}
    if(id==14)SetApiAdmin(!apiAdmin);
    if(id==4){LoadSettings();if(captureEnabled)StartFrames();else StopFrames();ApplyAppearance();SetTimer(panel,1,interval,NULL);RestorePosition();ReadSensors();}
    if(id==6){panelVisible=!panelVisible;if(panelVisible)RestorePosition();else ShowWindow(panel,SW_HIDE);}
    if(id==7){transparentBackground=!transparentBackground;SetLocal(L"Transparent",transparentBackground);ApplyAppearance();}
    if(id==8){clickThrough=!clickThrough;SetLocal(L"ClickThrough",clickThrough);ApplyAppearance();}
    if(id==9){compactStyle=!compactStyle;scrollRow=0;SetLocal(L"CompactStyle",compactStyle);}
    if(id==12){drawGraphs=!drawGraphs;SetLocal(L"Graphs",drawGraphs);}
    if(id==10||id==11){fontSize=Clamp(fontSize+(id==10?2:-2),12,36);SetLocal(L"FontSize",fontSize);ApplyAppearance();}
    if(id>=20&&id<=23){MONITORINFO info={sizeof(info)};if(GetMonitorInfoW(MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST),&info)){
        xPos=id%2?std::max(info.rcWork.left,info.rcWork.right-panelWidth):info.rcWork.left;
        yPos=id>=22?std::max(info.rcWork.top,info.rcWork.bottom-panelHeight):info.rcWork.top;
        SetLocal(L"X",xPos);SetLocal(L"Y",yPos);RestorePosition();}}
    if(id==5){DestroyWindow(panel);return;}
    RefreshPanel();if(trayAdded&&!id)Shell_NotifyIconW(NIM_SETFOCUS,&trayData);
}
static LRESULT CALLBACK PanelProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_APP+17){SetApiAdmin(true);return 0;}
    if(m==taskbarCreated&&taskbarCreated){trayAdded=false;AddTray();return 0;}
    if(m==TRAY_MESSAGE){UINT event=LOWORD(l);
        if(event==WM_CONTEXTMENU||event==WM_RBUTTONUP)ShowMenu();
        else if(event==NIN_SELECT||event==NIN_KEYSELECT||event==WM_LBUTTONDBLCLK)OpenPicker();
        return 0;}
    switch(m){
    case WM_TIMER:
        if(quitAt&&GetTickCount64()>=quitAt){DestroyWindow(h);return 0;}
        if(w==1){ReadSensors();RestorePosition();RefreshPanel();if(!trayAdded)AddTray();}
        if(w==2){TrackForeground();ReadFrames();resolution::Tick();}return 0;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_MOUSEWHEEL:{int delta=(short)HIWORD(w);int count=0;for(const Sensor& s:sensors)if(s.selected)++count;
        scrollRow=Clamp(scrollRow+(delta>0?-3:3),0,std::max(0,count-1));RefreshPanel();return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);if(!transparentBackground){RECT r;GetClientRect(h,&r);PaintPanel(dc,r);}EndPaint(h,&ps);if(transparentBackground)RefreshPanel();return 0;}
    case WM_DISPLAYCHANGE:RestorePosition();RefreshPanel();resolutionui::CurrentMode();return 0;
    case WM_CONTEXTMENU:ShowMenu();return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:resolution::Shutdown();DeleteTray();KillTimer(h,1);KillTimer(h,2);if(picker)DestroyWindow(picker);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int){
    appInstance=instance;
    int checkExit=0;
    wchar_t path[MAX_PATH];if(!GetModuleFileNameW(NULL,path,MAX_PATH)||wcslen(path)>=MAX_PATH-5)return 1;
    wcscpy(folder,path);wchar_t* slash=wcsrchr(folder,L'\\');if(!slash)return 1;*slash=0;
    wcscpy(iniPath,path);wchar_t* dot=wcsrchr(iniPath,L'.');if(!dot)return 1;wcscpy(dot,L".ini");
    wcscpy(logPath,path);wcscpy(wcsrchr(logPath,L'.'),L".log");
    wchar_t programFiles[MAX_PATH];DWORD n=GetEnvironmentVariableW(L"ProgramFiles(x86)",programFiles,MAX_PATH);
    if(n&&n<MAX_PATH-64){
        swprintf(afterburnerConfig,MAX_PATH,L"%ls\\MSI Afterburner\\MSIAfterburner.cfg",programFiles);
        swprintf(afterburnerProfile,MAX_PATH,L"%ls\\MSI Afterburner\\Profiles\\MSIAfterburner.cfg",programFiles);
    }
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\AeroPanel.Standalone.v1");
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS){
        HWND existing=FindWindowW(L"AeroPanelStandalone",NULL);
        if(existing){if(wcsstr(command,L"--quit"))PostMessageW(existing,WM_CLOSE,0,0);else if(wcsstr(command,L"--api-admin"))PostMessageW(existing,WM_APP+17,0,0);else PostMessageW(existing,TRAY_MESSAGE,0,NIN_SELECT);}
        if(mutex)CloseHandle(mutex);return 0;}
    if(wcsstr(command,L"--quit")){CloseHandle(mutex);return 0;}
    if(wcsstr(command,L"--smoke"))quitAt=GetTickCount64()+30000;
    const wchar_t* targetArg=wcsstr(command,L"--fps-pid=");
    if(targetArg){fpsTarget=wcstoul(targetArg+10,NULL,10);fixedFpsTarget=fpsTarget!=0;fpsTargetName=L"指定测试进程";}
    SetProcessDPIAware();LoadSettings();resolution::Init();
    if(wcsstr(command,L"--api-admin")){apiAdmin=true;WritePrivateProfileStringW(L"Capture",L"APIAdmin",L"1",iniPath);}
    if(wcsstr(command,L"--enable-startup")){bool ok=resolution::SetStartup(true);CloseHandle(mutex);return ok?0:6;}
    fontHandle=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    settingsBrush=CreateSolidBrush(RGB(35,39,45));trayIcon=CreateTrayIcon();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc={};wc.hInstance=instance;wc.lpfnWndProc=PanelProc;wc.lpszClassName=L"AeroPanelStandalone";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hIcon=trayIcon;RegisterClassW(&wc);
    wc.lpfnWndProc=PickerProc;wc.lpszClassName=L"AeroPanelPicker";RegisterClassW(&wc);
    panel=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"AeroPanelStandalone",L"硬件监控 · AeroPanel",WS_POPUP,xPos,yPos,panelWidth,panelHeight,NULL,NULL,instance,NULL);
    if(!panel){DeleteObject(fontHandle);DeleteObject(settingsBrush);if(trayIcon)DestroyIcon(trayIcon);CloseHandle(mutex);return 2;}
    ApplyAppearance();
    ReadSensors();
    if(wcsstr(command,L"--verify-picker")){
        if(sensors.empty()){Log(L"PICKER CHECK FAIL: no sensor data.");checkExit=4;}
        else {
            picker=CreateWindowExW(0,L"AeroPanelPicker",L"Picker validation",WS_OVERLAPPED,0,0,650,510,panel,NULL,instance,NULL);
            if(!picker){Log(L"PICKER CHECK FAIL: window creation failed.");checkExit=4;}
            else {
                SendMessageW(sensorList,LB_SETSEL,FALSE,-1);SendMessageW(sensorList,LB_SETSEL,TRUE,0);
                bool listOk=SendMessageW(sensorList,LB_GETSELCOUNT,0,0)==1;
                SendMessageW(picker,WM_COMMAND,13,0);
                unsigned selected=0;for(const Sensor& s:sensors)if(s.selected)++selected;
                bool saved=listOk && !followAfterburner && selected==1 && sensors[0].selected;
                Log(saved?L"PICKER CHECK PASS: real listbox selection saved to local INI and re-read as exactly one sensor.":L"PICKER CHECK FAIL: selection persistence mismatch.");
                if(!saved)checkExit=4;
            }
        }
        DestroyWindow(panel);
    }else{
        AddTray();RestorePosition();RefreshPanel();SetTimer(panel,1,interval,NULL);SetTimer(panel,2,250,NULL);
        if(wcsstr(command,L"--verify-tray")){
            bool first=trayAdded;DeleteTray();SendMessageW(panel,taskbarCreated,0,0);
            Log(first&&trayAdded?L"TRAY CHECK PASS: icon added, removed and restored through TaskbarCreated handler.":L"TRAY CHECK FAIL.");
            if(!first||!trayAdded)checkExit=4;
        }
        if(wcsstr(command,L"--ui-smoke")||!wcsstr(command,L"--background"))OpenPicker();
        if(captureEnabled||wcsstr(command,L"--fps-smoke"))StartFrames();
    }
    Log(L"Started: direct MAHMSharedMemory reader. No RTSS data or rendering dependency.");
    MSG msg;int result;while((result=GetMessageW(&msg,NULL,0,0))>0){
        if(picker&&msg.message==WM_KEYDOWN&&(msg.hwnd==picker||IsChild(picker,msg.hwnd))&&
            (msg.wParam==VK_F11||(msg.wParam==VK_ESCAPE&&settingsFullscreen))){
            if(!(msg.lParam&(1L<<30)))ToggleSettingsFullscreen();continue;
        }
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    if(wcsstr(command,L"--render-check")){SaveRenderDiagnostics();if(!VerifyCurveRendering())checkExit=5;}
    StopFrames();
    PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof(memory);wchar_t stats[260];
    if(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory))){
        swprintf(stats,260,L"Stopped: updates=%lu exportedSensors=%u fpsFrames=%llu workingSet=%.2fMiB privateCommit=%.2fMiB",
            (unsigned long)updateCount,(unsigned)sensors.size(),totalFrames,memory.WorkingSetSize/1048576.0,memory.PrivateUsage/1048576.0);Log(stats);}
    ReleaseLayer();if(osdFont)DeleteObject(osdFont);DeleteObject(fontHandle);DeleteObject(settingsBrush);if(trayIcon)DestroyIcon(trayIcon);CloseHandle(mutex);return result==-1?3:checkExit;
}
