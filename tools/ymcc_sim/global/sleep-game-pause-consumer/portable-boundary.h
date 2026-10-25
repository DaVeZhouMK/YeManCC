#pragma once
// Portable fixture guard: do not include windows.h or replace original memory fakes.
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <crtdbg.h>
namespace YmccPauseBoundary {
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t*);
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void*,wchar_t*,unsigned long);
extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);
inline std::filesystem::path root,eventPath;
inline void event(const char* kind,const std::string& value){std::ofstream f(eventPath,std::ios::app);if(!f)ExitProcess(92);f<<"{\"kind\":\""<<kind<<"\",\"value\":\""<<value<<"\"}\n";f.flush();}
[[noreturn]] inline void refuse(const char* port){event("DENIED",port);std::fprintf(stderr,"YMCC_PAUSE_EXTERNAL_PORT_DENIED:%s\n",port);ExitProcess(91);}
inline bool inside(const std::filesystem::path& value){const auto a=std::filesystem::absolute(value).lexically_normal().wstring(),b=root.wstring()+L"\\";return a==root.wstring()||a.rfind(b,0)==0;}
inline std::uint32_t u32(const unsigned char* p){std::uint32_t value;std::memcpy(&value,p,4);return value;}
inline std::uint64_t u64(const unsigned char* p){std::uint64_t value;std::memcpy(&value,p,8);return value;}
inline bool forbidden(const std::string& value){
 static const std::vector<std::string> names={"OpenProcess","OpenThread","OpenProcessToken","SuspendThread","ResumeThread","NtSuspendProcess","NtResumeProcess","SetSuspendState","SetSystemPowerState","CreateToolhelp32Snapshot","Process32FirstW","Process32NextW","CreateProcessW","CreateProcessA","ShellExecuteW","ShellExecuteA","WinHttpOpen","InternetOpenW","RegOpenKeyExW","RegSetValueExW"};
 return std::find(names.begin(),names.end(),value)!=names.end();
}
inline void verifyImports(){
 const auto base=static_cast<const unsigned char*>(GetModuleHandleW(nullptr));if(!base||base[0]!='M'||base[1]!='Z')ExitProcess(93);
 const auto pe=base+u32(base+0x3c);if(u32(pe)!=0x4550)ExitProcess(93);const auto opt=pe+24;const bool x64=opt[0]==0x0b&&opt[1]==0x02;if(!x64)ExitProcess(93);
 const auto imageSize=u32(opt+56);const auto importRva=u32(opt+112+8);if(!importRva||importRva>=imageSize)ExitProcess(93);auto descriptor=base+importRva;
 unsigned modules=0,imports=0;
 for(;u32(descriptor+12);descriptor+=20){if(++modules>128||descriptor-base+20>imageSize)ExitProcess(93);const auto thunkRva=u32(descriptor);if(!thunkRva||thunkRva>=imageSize)ExitProcess(93);auto thunk=base+thunkRva;
  for(;u64(thunk);thunk+=8){if(++imports>4096||thunk-base+8>imageSize)ExitProcess(93);const auto nameRva=u64(thunk);if(nameRva&(1ull<<63))refuse("ordinal-import");if(nameRva+2>=imageSize)ExitProcess(93);const char* name=reinterpret_cast<const char*>(base+nameRva+2);if(std::strlen(name)>256)ExitProcess(93);if(forbidden(name))refuse(name);event("IMPORT_ALLOWED",name);}
 }
 event("IMPORTS_VERIFIED",std::to_string(imports));
}
struct Guard {
 Guard(){const auto value=std::getenv("YMCC_SLEEP_NATIVE_ROOT");if(!value)ExitProcess(92);root=std::filesystem::path(value).lexically_normal();if(!root.is_absolute()||!std::filesystem::is_directory(root))ExitProcess(92);wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768)||!inside(exe))ExitProcess(92);eventPath=root/(std::filesystem::path(exe).filename().wstring()+L".boundary.jsonl");_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);event("START","owned-pause-fixture");verifyImports();}
 ~Guard(){event("NATURAL_RETURN","owned-pause-fixture");}
};
inline Guard guard;
}
// These names have no original memory declarations. Unexpected use refuses.
#define SetSuspendState(...) (::YmccPauseBoundary::refuse("SetSuspendState"),0)
#define NtSuspendProcess(...) (::YmccPauseBoundary::refuse("NtSuspendProcess"),0)
#define NtResumeProcess(...) (::YmccPauseBoundary::refuse("NtResumeProcess"),0)
#define SuspendThread(...) (::YmccPauseBoundary::refuse("SuspendThread"),0)
#define ResumeThread(...) (::YmccPauseBoundary::refuse("ResumeThread"),0)
#define OpenThread(...) (::YmccPauseBoundary::refuse("OpenThread"),0)
#define CreateToolhelp32Snapshot(...) (::YmccPauseBoundary::refuse("CreateToolhelp32Snapshot"),0)
#define CreateProcessW(...) (::YmccPauseBoundary::refuse("CreateProcessW"),0)
#define WinHttpOpen(...) (::YmccPauseBoundary::refuse("WinHttpOpen"),0)
#define GetProcAddress(...) (::YmccPauseBoundary::refuse("GetProcAddress"),nullptr)
#ifdef YMCC_PAUSE_BOUNDARY_PROBE
#define OpenProcess(...) (::YmccPauseBoundary::refuse("OpenProcess"),0)
#endif
