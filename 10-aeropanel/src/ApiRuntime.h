#pragma once
#include "ApiNames.h"
struct ApiChannel {HANDLE process=NULL,pipe=NULL,stop=NULL;DWORD childPid=0;std::string pending;};
static ApiChannel apiCollector,apiBroker;
static HANDLE &apiProcess=apiCollector.process,&apiPipe=apiCollector.pipe,&apiStop=apiCollector.stop;
static bool apiAdmin=false;
struct ApiEvidence {ULONGLONG created;unsigned mask;};
static std::map<DWORD,ApiEvidence> apiEvidence;
static void SaveApiEvidence(DWORD pid,const ApiEvidence& item){
 std::wstring value=std::to_wstring(item.created)+L","+std::to_wstring(item.mask);
 WritePrivateProfileStringW(L"APICache",std::to_wstring(pid).c_str(),value.c_str(),iniPath);
}
static void LoadApiEvidence(){
 wchar_t data[16384]={};GetPrivateProfileSectionW(L"APICache",data,16384,iniPath);
 apiEvidence.clear();
 for(const wchar_t* line=data;*line;line+=wcslen(line)+1){DWORD pid=0;unsigned long long created=0;unsigned mask=0;
  if(swscanf(line,L"%lu=%llu,%u",&pid,&created,&mask)==3&&pid&&created&&mask&&!(mask&~127u)&&
    apiidentity::ProcessIdentity(pid)==created&&apiEvidence.size()<256)apiEvidence[pid]={created,mask};
 }
 WritePrivateProfileStringW(L"APICache",NULL,NULL,iniPath);
 for(const auto& item:apiEvidence)SaveApiEvidence(item.first,item.second);
}
static bool EnsureApiRuntime(){
 HRSRC resource=FindResourceW(NULL,MAKEINTRESOURCEW(301),RT_RCDATA);if(!resource)return false;
 DWORD size=SizeofResource(NULL,resource);const BYTE* payload=(const BYTE*)LockResource(LoadResource(NULL,resource));if(!size||!payload)return false;
 std::wstring dir=std::wstring(folder)+L"\\runtime",path=dir+L"\\ApiProbe.exe";
 HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(file!=INVALID_HANDLE_VALUE){LARGE_INTEGER length;bool ok=GetFileSizeEx(file,&length)&&length.QuadPart==size;
  BYTE buffer[16384];DWORD offset=0;while(ok&&offset<size){DWORD got=0,amount=std::min<DWORD>(sizeof(buffer),size-offset);
   ok=ReadFile(file,buffer,amount,&got,NULL)&&got==amount&&!memcmp(buffer,payload+offset,amount);offset+=got;}
  CloseHandle(file);if(ok)return true;}
 if(!CreateDirectoryW(dir.c_str(),NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
 std::wstring temp=path+L".tmp";file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(file==INVALID_HANDLE_VALUE)return false;
 DWORD written=0;bool ok=WriteFile(file,payload,size,&written,NULL)&&written==size&&FlushFileBuffers(file);CloseHandle(file);
 if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;if(!ok)DeleteFileW(temp.c_str());return ok;
}
static bool ConnectedApi(const ApiChannel& channel){
 DWORD client=0;return channel.pipe&&GetNamedPipeClientProcessId(channel.pipe,&client)&&client==channel.childPid;
}
static void ReadApiChannel(ApiChannel& channel){
 if(!ConnectedApi(channel))return;DWORD available=0;char buffer[4096];
 for(int i=0;i<8&&PeekNamedPipe(channel.pipe,NULL,0,NULL,&available,NULL)&&available;++i){DWORD got=0;
  if(!ReadFile(channel.pipe,buffer,std::min<DWORD>(available,sizeof(buffer)),&got,NULL)||!got)break;
  channel.pending.append(buffer,got);size_t end;
  while((end=channel.pending.find('\n'))!=std::string::npos){std::string line=channel.pending.substr(0,end);channel.pending.erase(0,end+1);
   DWORD pid=0;unsigned long long created=0;unsigned mask=0;
   if(sscanf(line.c_str(),"API,%lu,%llu,%u",&pid,&created,&mask)==3&&pid&&created&&mask&&!(mask&~127u)){
    if(apiidentity::ProcessIdentity(pid)!=created)continue;
    if(apiEvidence.size()>=256)for(auto it=apiEvidence.begin();it!=apiEvidence.end();) {if(apiidentity::ProcessIdentity(it->first)!=it->second.created)it=apiEvidence.erase(it);else ++it;}
    if(apiEvidence.size()<256||apiEvidence.count(pid)){auto& item=apiEvidence[pid];if(item.created!=created)item={created,0};
     unsigned previous=item.mask;item.mask|=mask;if(previous!=item.mask){SaveApiEvidence(pid,item);
      std::wstring message=L"Graphics API identified: "+apiidentity::Names(item.mask)+L" PID="+std::to_wstring(pid);Log(message.c_str());}}
   }else if(&channel==&apiCollector&&line.rfind("STACK,",0)==0&&line.size()<4096&&ConnectedApi(apiBroker)){
    line+='\n';DWORD written=0;WriteFile(apiBroker.pipe,line.data(),(DWORD)line.size(),&written,NULL);
   }else if(line=="ELEVATED,1")Log(L"Graphics API broker elevation verified.");
   else if(line.rfind("PROVIDER,",0)==0){std::wstring message=L"API trace "+Wide(line.c_str(),line.size());Log(message.c_str());}
   else if(line=="READY,0")Log(L"Graphics API detection unavailable; retaining present-runtime fallback.");
  }
  if(channel.pending.size()>8192)channel.pending.clear();
 }
}
static void ReadApi(){ReadApiChannel(apiCollector);ReadApiChannel(apiBroker);}
static std::wstring DetectedApi(DWORD pid){
 auto found=apiEvidence.find(pid);if(found==apiEvidence.end())return L"";
 if(apiidentity::ProcessIdentity(pid)!=found->second.created){apiEvidence.erase(found);return L"";}
 return apiidentity::Names(found->second.mask);
}
static void StopApiChannel(ApiChannel& channel){
 if(channel.stop)SetEvent(channel.stop);
 if(channel.process){if(WaitForSingleObject(channel.process,5000)==WAIT_TIMEOUT)Log(L"API worker still completing cleanup.");CloseHandle(channel.process);}
 if(channel.stop)CloseHandle(channel.stop);if(channel.pipe)CloseHandle(channel.pipe);channel={};
}
static void StopApi(){StopApiChannel(apiCollector);StopApiChannel(apiBroker);apiEvidence.clear();}
static void StartApiChannel(ApiChannel& channel,bool broker,bool elevate=true){
 GUID guid;if(FAILED(CoCreateGuid(&guid)))return;wchar_t unique[48];StringFromGUID2(guid,unique,48);
 std::wstring pipeName=L"\\\\.\\pipe\\AeroPanel-API-"+std::wstring(unique),stopName=L"Local\\AeroPanel-API-Stop-"+std::wstring(unique);
 channel.pipe=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
  PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,16384,16384,0,NULL);
 if(channel.pipe==INVALID_HANDLE_VALUE){channel.pipe=NULL;return;}
 ConnectNamedPipe(channel.pipe,NULL);channel.stop=CreateEventW(NULL,TRUE,FALSE,stopName.c_str());if(!channel.stop){StopApiChannel(channel);return;}
 std::wstring path=std::wstring(folder)+L"\\runtime\\ApiProbe.exe";
 std::wstring parameters=(broker?L"--broker ":L"--pipe ")+std::to_wstring(GetCurrentProcessId())+L" \""+pipeName+L"\" \""+stopName+L"\"";
 if(broker&&elevate){SHELLEXECUTEINFOW info={sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";
  info.lpFile=path.c_str();info.lpParameters=parameters.c_str();info.lpDirectory=folder;info.nShow=SW_HIDE;
  if(ShellExecuteExW(&info)&&info.hProcess){channel.process=info.hProcess;channel.childPid=GetProcessId(channel.process);}
 }else{std::wstring command=L"\""+path+L"\" "+parameters;STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={};
  if(CreateProcessW(path.c_str(),&command[0],NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,folder,&si,&pi)){
   channel.process=pi.hProcess;channel.childPid=pi.dwProcessId;CloseHandle(pi.hThread);}}
 if(!channel.process)StopApiChannel(channel);
}
static void StartApi(){
 if(apiProcess||!EnsureApiRuntime())return;LoadApiEvidence();StartApiChannel(apiCollector,false);
 if(apiAdmin&&apiProcess)StartApiChannel(apiBroker,true);
 Log(L"Graphics API collector started; elevated broker performs module mapping only.");
}
