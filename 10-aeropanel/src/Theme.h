#pragma once
// Preblurred static artwork: no runtime blur, animation, decoding library or timer.
static HBITMAP settingsArtworkBitmap=NULL;
static HDC settingsArtworkDC=NULL;
static HGDIOBJ settingsArtworkPrevious=NULL;
static HICON settingsIconSmall=NULL,settingsIconLarge=NULL;
static bool settingsArtworkEnabled=true;
static int settingsArtworkWidth=0,settingsArtworkHeight=0;
static void ReleaseSettingsArtwork(){
    if(settingsArtworkDC&&settingsArtworkPrevious)SelectObject(settingsArtworkDC,settingsArtworkPrevious);
    if(settingsArtworkDC)DeleteDC(settingsArtworkDC);
    if(settingsArtworkBitmap)DeleteObject(settingsArtworkBitmap);
    settingsArtworkBitmap=NULL;settingsArtworkDC=NULL;settingsArtworkPrevious=NULL;
    settingsArtworkWidth=settingsArtworkHeight=0;
}
static bool LoadSettingsArtwork(){
    if(settingsArtworkDC)return true;
    HBITMAP bitmap=(HBITMAP)LoadImageW(appInstance,MAKEINTRESOURCEW(201),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
    if(!bitmap)return false;
    BITMAP info={};HDC dc=CreateCompatibleDC(NULL);
    if(!dc||!GetObjectW(bitmap,sizeof(info),&info)||info.bmWidth<=0||info.bmWidth>1024||info.bmHeight<=0||info.bmHeight>1024){
        if(dc)DeleteDC(dc);DeleteObject(bitmap);return false;
    }
    HGDIOBJ previous=SelectObject(dc,bitmap);
    if(!previous||previous==HGDI_ERROR){DeleteDC(dc);DeleteObject(bitmap);return false;}
    settingsArtworkBitmap=bitmap;settingsArtworkDC=dc;settingsArtworkPrevious=previous;
    settingsArtworkWidth=info.bmWidth;settingsArtworkHeight=info.bmHeight;return true;
}
static void PaintSettingsBackground(HWND h,HDC dc,HWND child=NULL){
    RECT client;GetClientRect(h,&client);POINT origin={0,0};
    int saved=SaveDC(dc);
    if(child){RECT bounds;GetClientRect(child,&bounds);IntersectClipRect(dc,0,0,bounds.right,bounds.bottom);MapWindowPoints(child,h,&origin,1);}
    if(settingsArtworkDC){
        int previous=SetStretchBltMode(dc,COLORONCOLOR);
        StretchBlt(dc,-origin.x,-origin.y,client.right,client.bottom,settingsArtworkDC,0,0,settingsArtworkWidth,settingsArtworkHeight,SRCCOPY);
        if(previous)SetStretchBltMode(dc,previous);
    }else{
        RECT area;if(child)GetClientRect(child,&area);else area=client;
        FillRect(dc,&area,settingsBrush);
    }
    if(saved)RestoreDC(dc,saved);
}
static void CreateSettingsTheme(HWND h){
    settingsArtworkEnabled=Setting(L"SilverWolfBackground",1)!=0;
    if(settingsArtworkEnabled)LoadSettingsArtwork();
    settingsIconSmall=(HICON)LoadImageW(appInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,16,16,0);
    settingsIconLarge=(HICON)LoadImageW(appInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,64,64,0);
    if(settingsIconSmall)SendMessageW(h,WM_SETICON,ICON_SMALL,(LPARAM)settingsIconSmall);
    if(settingsIconLarge)SendMessageW(h,WM_SETICON,ICON_BIG,(LPARAM)settingsIconLarge);
}
static void ReleaseSettingsTheme(){
    ReleaseSettingsArtwork();
    if(settingsIconSmall)DestroyIcon(settingsIconSmall);if(settingsIconLarge)DestroyIcon(settingsIconLarge);
    settingsIconSmall=settingsIconLarge=NULL;
}
