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
  bool traceRunning=false;
  HANDLE voltageProcess=NULL,voltagePipe=NULL;
  std::string voltagePending;
  auto stopVoltage=[&](){
   if(voltageProcess){TerminateProcess(voltageProcess,0);CloseHandle(voltageProcess);}
   if(voltagePipe)CloseHandle(voltagePipe);
   voltageProcess=NULL;voltagePipe=NULL;voltagePending.clear();
  };
  while(WaitForMultipleObjects(2,handles,FALSE,100)==WAIT_TIMEOUT){
   if(voltageProcess){
    DWORD ready=0,got=0;char readings[256];
    if(PeekNamedPipe(voltagePipe,NULL,0,NULL,&ready,NULL)&&ready&&
       ReadFile(voltagePipe,readings,std::min<DWORD>(ready,sizeof(readings)),&got,NULL)){
     voltagePending.append(readings,got);
     size_t end;
     while((end=voltagePending.find('\n'))!=std::string::npos){
      auto line=voltagePending.substr(0,end);voltagePending.erase(0,end+1);
      if(!line.empty()&&line.back()=='\r')line.pop_back();
      if(line.rfind("HW,",0)==0&&line.size()<256)printf("%s\n",line.c_str());
     }
     if(voltagePending.size()>256)voltagePending.clear();
    }
    if(WaitForSingleObject(voltageProcess,0)==WAIT_OBJECT_0){stopVoltage();printf("HWERROR\n");}
   }
   DWORD available=0;char buffer[4096];
   if(!PeekNamedPipe(pipe,NULL,0,NULL,&available,NULL))break;
   if(!available)continue;DWORD got=0;if(!ReadFile(pipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,NULL)||!got)break;pending.append(buffer,got);size_t end;
   while((end=pending.find('\n'))!=std::string::npos){std::string line=pending.substr(0,end);pending.erase(0,end+1);
    if(line=="TRACE,1"){
     if(!traceRunning){apidetection::reportEvents=true;traceRunning=apidetection::Start();}
     printf("TRACE_READY,%u\n",traceRunning?1:0);
     continue;
    }
    if(line=="TRACE,0"){
     if(traceRunning){apidetection::Stop();traceRunning=false;}
     continue;
    }
    if(line=="HARDWARE,0"){stopVoltage();continue;}
    if(line=="HARDWARE,1"||line=="HARDWARE,2"){
     if(!elevation.TokenIsElevated)continue;
     stopVoltage();
     wchar_t module[MAX_PATH]={};
     const DWORD moduleLength=GetModuleFileNameW(NULL,module,MAX_PATH);
     if(!moduleLength||moduleLength>=MAX_PATH){printf("HWERROR\n");continue;}
     std::wstring worker=module;worker.resize(worker.find_last_of(L'\\')+1);
     worker+=L"board-voltage\\BoardVoltage";
     std::wstring host=worker+L".exe";
     std::wstring command=L"\""+host+L"\" ";
     const DWORD attributes=GetFileAttributesW(host.c_str());
     if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)){
      wchar_t programFiles[MAX_PATH]={};
      const DWORD rootLength=GetEnvironmentVariableW(L"ProgramFiles",programFiles,MAX_PATH);
      if(!rootLength||rootLength>=MAX_PATH){printf("HWERROR\n");continue;}
      host=std::wstring(programFiles)+L"\\dotnet\\dotnet.exe";
      command=L"\""+host+L"\" \""+worker+L".dll\" ";
     }
     command+=std::to_wstring(GetCurrentProcessId());
     if(line=="HARDWARE,2")command+=L" voltage-only";
     SECURITY_ATTRIBUTES attrs={sizeof(attrs),NULL,TRUE};
     HANDLE write=NULL;
     if(CreatePipe(&voltagePipe,&write,&attrs,0)){
      STARTUPINFOW startup={sizeof(startup)};startup.dwFlags=STARTF_USESTDHANDLES;
      startup.hStdOutput=write;startup.hStdError=write;
      PROCESS_INFORMATION process={};
      if(SetHandleInformation(voltagePipe,HANDLE_FLAG_INHERIT,0)&&
         CreateProcessW(host.c_str(),&command[0],NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process)){
       voltageProcess=process.hProcess;CloseHandle(process.hThread);
      }
      CloseHandle(write);
     }
     if(!voltageProcess){stopVoltage();printf("HWERROR\n");}
     continue;
    }
    if(line.rfind("STACK,",0))continue;
    const char* text=line.c_str()+6;char* tail=NULL;DWORD target=strtoul(text,&tail,10);if(!tail||*tail!=',')continue;
    ULONGLONG created=strtoull(tail+1,&tail,10);if(!created||apiidentity::ProcessIdentity(target)!=created)continue;
    std::vector<ULONGLONG> addresses;while(tail&&*tail==','&&addresses.size()<128){char* next=NULL;auto address=strtoull(tail+1,&next,10);if(next==tail+1)break;addresses.push_back(address);tail=next;}
    DWORD error=0;unsigned mask=apidetection::ResolveStack(target,addresses,error);
    if(mask&&apiidentity::ProcessIdentity(target)==created)printf("API,%lu,%llu,%u\n",target,created,mask);
   }if(pending.size()>8192)pending.clear();
  }if(traceRunning)apidetection::Stop();stopVoltage();CloseHandle(parent);CloseHandle(stop);return 0;
 }
 apidetection::reportEvents=true;bool ok=apidetection::Start();printf("READY,%u\n",ok?1:0);fflush(stdout);
 if(ok){HANDLE handles[]={parent,stop};WaitForMultipleObjects(2,handles,FALSE,INFINITE);}
 apidetection::Stop();CloseHandle(parent);CloseHandle(stop);return ok?0:4;
}
