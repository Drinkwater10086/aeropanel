#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <map>
#include "ApiDetection.h"
int wmain(int argc,wchar_t** argv){
 bool broker=argc==5&&!wcscmp(argv[1],L"--broker");
 if(argc!=5||(!broker&&wcscmp(argv[1],L"--pipe")))return 2;DWORD pid=wcstoul(argv[2],NULL,10);
 HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,argv[4]);if(!stop)return 3;
 HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!parent){CloseHandle(stop);return 3;}
 HANDLE output=CreateFileW(argv[3],broker?GENERIC_WRITE|GENERIC_READ:GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
 if(output==INVALID_HANDLE_VALUE){CloseHandle(parent);CloseHandle(stop);return 3;}
 int file=_open_osfhandle((intptr_t)output,_O_WRONLY|_O_BINARY);if(file<0){CloseHandle(output);CloseHandle(parent);CloseHandle(stop);return 3;}
 _dup2(file,1);_close(file);setvbuf(stdout,NULL,_IONBF,0);
 HANDLE token=NULL;TOKEN_ELEVATION elevation={};DWORD bytes=0;
 if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&bytes);CloseHandle(token);}
 printf("ELEVATED,%lu\n",elevation.TokenIsElevated);
 if(broker){
  HANDLE pipe=(HANDLE)_get_osfhandle(1);std::string pending;printf("READY,1\n");HANDLE handles[]={parent,stop};
  while(WaitForMultipleObjects(2,handles,FALSE,100)==WAIT_TIMEOUT){DWORD available=0;char buffer[4096];
   if(!PeekNamedPipe(pipe,NULL,0,NULL,&available,NULL))break;
   if(!available)continue;DWORD got=0;if(!ReadFile(pipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,NULL)||!got)break;pending.append(buffer,got);size_t end;
   while((end=pending.find('\n'))!=std::string::npos){std::string line=pending.substr(0,end);pending.erase(0,end+1);if(line.rfind("STACK,",0))continue;
    const char* text=line.c_str()+6;char* tail=NULL;DWORD target=strtoul(text,&tail,10);if(!tail||*tail!=',')continue;
    ULONGLONG created=strtoull(tail+1,&tail,10);if(!created||apiidentity::ProcessIdentity(target)!=created)continue;
    std::vector<ULONGLONG> addresses;while(tail&&*tail==','&&addresses.size()<128){char* next=NULL;auto address=strtoull(tail+1,&next,10);if(next==tail+1)break;addresses.push_back(address);tail=next;}
    DWORD error=0;unsigned mask=apidetection::ResolveStack(target,addresses,error);
    if(mask&&apiidentity::ProcessIdentity(target)==created)printf("API,%lu,%llu,%u\n",target,created,mask);
   }if(pending.size()>8192)pending.clear();
  }CloseHandle(parent);CloseHandle(stop);return 0;
 }
 apidetection::reportEvents=true;bool ok=apidetection::Start();printf("READY,%u\n",ok?1:0);fflush(stdout);
 if(ok){HANDLE handles[]={parent,stop};WaitForMultipleObjects(2,handles,FALSE,INFINITE);}
 apidetection::Stop();CloseHandle(parent);CloseHandle(stop);return ok?0:4;
}
