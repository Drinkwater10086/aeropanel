#pragma once
#include <evntrace.h>
#include <evntcons.h>
#include <tlhelp32.h>
#include "ApiNames.h"
// Provider GUIDs and device event IDs verified against this Windows runtime manifests.
namespace apidetection {
static const GUID providers[]={
 {0x802ec45a,0x1e99,0x4b83,{0x99,0x20,0x87,0xc9,0x82,0x77,0xba,0x9d}},
 {0x9b7e4c0f,0x342c,0x4106,{0xa1,0x9f,0x4f,0x27,0x04,0xf6,0x89,0xf0}},
 {0x9b7e4c8f,0x342c,0x4106,{0xa1,0x9f,0x4f,0x27,0x04,0xf6,0x89,0xf0}},
 {0xdb6f6ddb,0xac77,0x4e88,{0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40}},
 {0x5d8087dd,0x3a9b,0x4f56,{0x90,0xdf,0x49,0x19,0x6c,0xdc,0x4f,0x11}}
};
struct Evidence {ULONGLONG created=0;unsigned mask=0;};
static SRWLOCK lock=SRWLOCK_INIT;
static std::map<DWORD,Evidence> evidence;
static TRACEHANDLE session=0,consumer=INVALID_PROCESSTRACE_HANDLE;
static HANDLE thread=NULL;
static wchar_t sessionName[96];
static bool reportEvents=false;
static ULONGLONG Identity(DWORD pid){return apiidentity::ProcessIdentity(pid);}
static std::wstring Names(unsigned mask){return apiidentity::Names(mask);}
static std::vector<ULONGLONG> Addresses(EVENT_RECORD* record){
 std::vector<ULONGLONG> addresses;
 for(unsigned i=0;i<record->ExtendedDataCount;++i){const auto& data=record->ExtendedData[i];if(data.DataSize<8)continue;
  if(data.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE64){auto stack=(const ULONGLONG*)(uintptr_t)data.DataPtr;
   for(unsigned n=1;n<data.DataSize/8&&addresses.size()<128;++n)addresses.push_back(stack[n]);}
  else if(data.ExtType==EVENT_HEADER_EXT_TYPE_STACK_TRACE32){auto stack=(const DWORD*)(uintptr_t)data.DataPtr;
   for(unsigned n=2;n<data.DataSize/4&&addresses.size()<128;++n)addresses.push_back(stack[n]);}
 }return addresses;
}
static unsigned ResolveStack(DWORD pid,const std::vector<ULONGLONG>& addresses,DWORD& error){
 error=0;if(addresses.empty())return 0;HANDLE snapshot=INVALID_HANDLE_VALUE;
 for(int retry=0;retry<3;++retry){snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
  if(snapshot!=INVALID_HANDLE_VALUE)break;error=GetLastError();if(error!=ERROR_BAD_LENGTH)break;Sleep(5);}
 if(snapshot==INVALID_HANDLE_VALUE)return 0;error=0;
 std::vector<std::wstring> names;MODULEENTRY32W module={};module.dwSize=sizeof(module);
 if(Module32FirstW(snapshot,&module))do{for(auto address:addresses)
  if(address>=(uintptr_t)module.modBaseAddr&&address-(uintptr_t)module.modBaseAddr<module.modBaseSize){names.push_back(module.szModule);break;}
 }while(Module32NextW(snapshot,&module));else error=GetLastError();
 CloseHandle(snapshot);return apiidentity::StackMask(names);
}
static void WINAPI Event(EVENT_RECORD* record){
 bool kernel=IsEqualGUID(record->EventHeader.ProviderId,providers[0]);
 unsigned id=record->EventHeader.EventDescriptor.Id;
 if(kernel?!(id==27||id==30):!(record->EventHeader.EventDescriptor.Task==2&&(id==3||id==5)))return;
 DWORD pid=record->EventHeader.ProcessId;auto addresses=Addresses(record);DWORD error=0;
 unsigned mask=ResolveStack(pid,addresses,error);ULONGLONG created=Identity(pid);if(!created)return;
 if(!mask){
  if(reportEvents&&error==ERROR_ACCESS_DENIED&&!addresses.empty()){
   static std::map<DWORD,ULONGLONG> requested;ULONGLONG now=GetTickCount64();
   if(requested.size()>256)requested.clear();
   if(!requested.count(pid)||now-requested[pid]>=500){requested[pid]=now;
    printf("STACK,%lu,%llu",pid,created);for(auto address:addresses)printf(",%llu",address);printf("\n");fflush(stdout);}
  }return;
 }
 AcquireSRWLockExclusive(&lock);
 if(evidence.size()>=256)for(auto it=evidence.begin();it!=evidence.end();) {if(Identity(it->first)!=it->second.created)it=evidence.erase(it);else ++it;}
 if(evidence.size()<256||evidence.count(pid)){auto& item=evidence[pid];if(item.created!=created)item={created,0};unsigned previous=item.mask;item.mask|=mask;
  if(reportEvents&&previous!=item.mask){printf("API,%lu,%llu,%u\n",pid,created,item.mask);fflush(stdout);}}

 ReleaseSRWLockExclusive(&lock);
}
static std::wstring Label(DWORD pid){
 ULONGLONG created=Identity(pid);unsigned mask=0;
 AcquireSRWLockShared(&lock);auto found=evidence.find(pid);
 if(created&&found!=evidence.end()&&found->second.created==created)mask=found->second.mask;
 ReleaseSRWLockShared(&lock);return Names(mask);
}
static DWORD WINAPI Consume(LPVOID){TRACEHANDLE handle=consumer;return ProcessTrace(&handle,1,NULL,NULL);}
static void Properties(std::vector<BYTE>& memory){
 memory.assign(sizeof(EVENT_TRACE_PROPERTIES)+sizeof(sessionName),0);auto props=(EVENT_TRACE_PROPERTIES*)memory.data();
 props->Wnode.BufferSize=(ULONG)memory.size();props->Wnode.Flags=WNODE_FLAG_TRACED_GUID;props->Wnode.ClientContext=1;
 props->BufferSize=16;props->MinimumBuffers=2;props->MaximumBuffers=16;props->FlushTimer=1;
 props->LogFileMode=EVENT_TRACE_REAL_TIME_MODE;props->LoggerNameOffset=sizeof(*props);
}
static void Stop(){
 if(session){std::vector<BYTE> memory;Properties(memory);ControlTraceW(session,sessionName,(EVENT_TRACE_PROPERTIES*)memory.data(),EVENT_TRACE_CONTROL_STOP);session=0;}
 if(consumer!=INVALID_PROCESSTRACE_HANDLE){CloseTrace(consumer);}
 if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);thread=NULL;}
 consumer=INVALID_PROCESSTRACE_HANDLE;
 AcquireSRWLockExclusive(&lock);evidence.clear();ReleaseSRWLockExclusive(&lock);
}
static bool Start(){
 if(session)return true;swprintf(sessionName,96,L"AeroPanel-API-%lu",GetCurrentProcessId());
 std::vector<BYTE> memory;Properties(memory);ULONG result=StartTraceW(&session,sessionName,(EVENT_TRACE_PROPERTIES*)memory.data());
 if(result){session=0;return false;}
 EVENT_TRACE_LOGFILEW log={};log.LoggerName=sessionName;log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD;log.EventRecordCallback=Event;
 consumer=OpenTraceW(&log);if(consumer==INVALID_PROCESSTRACE_HANDLE){Stop();return false;}
 thread=CreateThread(NULL,0,Consume,NULL,0,NULL);if(!thread){Stop();return false;}
 ENABLE_TRACE_PARAMETERS parameters={};parameters.Version=ENABLE_TRACE_PARAMETERS_VERSION_2;parameters.EnableProperty=EVENT_ENABLE_PROPERTY_STACK_TRACE;
 bool enabled=false;
 for(const auto& provider:providers){ULONG result=EnableTraceEx2(session,&provider,EVENT_CONTROL_CODE_ENABLE_PROVIDER,TRACE_LEVEL_VERBOSE,
  provider.Data1==0x802ec45a?0x40:2,0,0,&parameters);
  if(reportEvents){printf("PROVIDER,%08lX,%lu\n",provider.Data1,result);fflush(stdout);}enabled|=result==ERROR_SUCCESS;}
 if(!enabled){Stop();return false;}
 return true;
}
}
