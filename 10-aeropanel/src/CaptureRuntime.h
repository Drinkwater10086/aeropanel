#pragma once
#include "CapturePayload.h"
static bool EnsureCaptureRuntime(){
    std::wstring dir=std::wstring(folder)+L"\\runtime",path=dir+L"\\PresentMon.exe";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file!=INVALID_HANDLE_VALUE){
        LARGE_INTEGER size;bool ok=GetFileSizeEx(file,&size)&&size.QuadPart==sizeof(CapturePayload);
        BYTE buffer[16384];size_t offset=0;
        while(ok&&offset<sizeof(CapturePayload)){DWORD got=0,amount=(DWORD)std::min<size_t>(sizeof(buffer),sizeof(CapturePayload)-offset);
            ok=ReadFile(file,buffer,amount,&got,NULL)&&got==amount&&!memcmp(buffer,CapturePayload+offset,amount);offset+=got;}
        CloseHandle(file);if(ok)return true;
    }
    if(!CreateDirectoryW(dir.c_str(),NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    std::wstring temp=path+L".tmp";
    file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,CapturePayload,sizeof(CapturePayload),&written,NULL)&&written==sizeof(CapturePayload)&&FlushFileBuffers(file);
    CloseHandle(file);if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temp.c_str());return ok;
}
