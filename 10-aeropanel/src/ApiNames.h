#pragma once
namespace apiidentity {
static unsigned StackMask(const std::vector<std::wstring>& names){
 unsigned mask=0;
 for(const auto& name:names){
  if(!_wcsicmp(name.c_str(),L"d3d9.dll"))mask|=1;
  if(!_wcsicmp(name.c_str(),L"d3d10.dll"))mask|=2;
  if(!_wcsicmp(name.c_str(),L"d3d10_1.dll"))mask|=4;
  if(!_wcsicmp(name.c_str(),L"d3d11.dll"))mask|=8;
  if(!_wcsicmp(name.c_str(),L"d3d12.dll"))mask|=16;
  if(!_wcsicmp(name.c_str(),L"opengl32.dll"))mask|=32;
  if(!_wcsicmp(name.c_str(),L"vulkan-1.dll"))mask|=64;
 }
 // Modern Windows implements the D3D10 wrapper through the D3D11 runtime.
 if(mask&(2|4))mask&=~8u;
 if(mask&4)mask&=~2u;
 return mask;
}
static std::wstring Names(unsigned mask){
 const wchar_t* names[]={L"D3D9",L"D3D10",L"D3D10.1",L"D3D11",L"D3D12",L"OpenGL",L"Vulkan"};
 std::wstring result;for(unsigned i=0;i<7;++i)if(mask&(1u<<i)){if(!result.empty())result+=L"/";result+=names[i];}return result;
}
static ULONGLONG ProcessIdentity(DWORD pid){
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return 0;
 FILETIME created,exited,kernel,user;BOOL ok=GetProcessTimes(process,&created,&exited,&kernel,&user);CloseHandle(process);
 return ok?((ULONGLONG)created.dwHighDateTime<<32)|created.dwLowDateTime:0;
}
}
