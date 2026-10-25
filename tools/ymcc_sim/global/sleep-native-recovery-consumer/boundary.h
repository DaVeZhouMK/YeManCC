#pragma once
// Test compilation boundary only. Product snapshot and reviewed fixture assertions stay byte-identical within each run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <powrprof.h>
#include <winhttp.h>
#include <shellapi.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <map>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <set>
#include <limits>
#include <cassert>
#include <crtdbg.h>
namespace YmccSleepBoundary {
inline std::filesystem::path root,events;
inline unsigned dynamicUnknown=0,denied=0;
inline bool inside(const std::filesystem::path& p){const auto a=std::filesystem::absolute(p).lexically_normal().wstring(),b=root.wstring()+L"\\";return a==root.wstring()||a.rfind(b,0)==0;}
inline void event(const char* kind,const char* port){std::ofstream log(events,std::ios::app);if(!log)ExitProcess(92);log<<"{\"kind\":\""<<kind<<"\",\"port\":\""<<port<<"\"}\n";log.flush();}
[[noreturn]] inline void refuse(const char* port){++denied;event("DENIED",port);std::fprintf(stderr,"YMCC_SLEEP_EXTERNAL_PORT_DENIED:%s\n",port);ExitProcess(91);}
struct Guard {
 Guard(){wchar_t value[32768]{};if(!GetEnvironmentVariableW(L"YMCC_SLEEP_NATIVE_ROOT",value,32768))ExitProcess(92);root=std::filesystem::path(value).lexically_normal();if(!root.is_absolute()||!std::filesystem::is_directory(root))ExitProcess(92);wchar_t module[32768]{};if(!GetModuleFileNameW(nullptr,module,32768)||!inside(module))ExitProcess(92);events=root/(std::filesystem::path(module).filename().wstring()+L".boundary.jsonl");SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);event("START","owned-native-fixture");}
 ~Guard(){event("NATURAL_RETURN","owned-native-fixture");}
};
inline Guard active;
inline FARPROC WINAPI resolve(HMODULE,LPCSTR name){if(name&&reinterpret_cast<uintptr_t>(name)>65535&&std::strcmp(name,"NtQueryInformationThread")==0){++dynamicUnknown;event("UNKNOWN","NtQueryInformationThread");return nullptr;}refuse("GetProcAddress");}
inline HANDLE WINAPI snapshot(DWORD,DWORD){refuse("CreateToolhelp32Snapshot");}
inline HANDLE WINAPI openThread(DWORD,BOOL,DWORD){refuse("OpenThread");}
inline HANDLE WINAPI openProcess(DWORD,BOOL,DWORD){refuse("OpenProcess");}
inline BOOL WINAPI openToken(HANDLE,DWORD,PHANDLE){refuse("OpenProcessToken");}
inline DWORD WINAPI resumeThread(HANDLE){refuse("ResumeThread");}
inline DWORD WINAPI suspendThread(HANDLE){refuse("SuspendThread");}
inline BOOL WINAPI suspendState(BOOLEAN,BOOLEAN,BOOLEAN){refuse("SetSuspendState");}
inline HINTERNET WINAPI http(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){refuse("WinHttpOpen");}
inline BOOL WINAPI createProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){refuse("CreateProcessW");}
inline HINSTANCE WINAPI shell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){refuse("ShellExecuteW");}
inline HANDLE WINAPI createFile(LPCWSTR file,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attributes,HANDLE templ){if(!file||!inside(file))refuse("CreateFileW-outside-owned-root");const auto result=::CreateFileW(file,access,share,security,disposition,attributes,templ);const DWORD error=result==INVALID_HANDLE_VALUE?GetLastError():0;if(error){std::ofstream log(events,std::ios::app);log<<"{\"kind\":\"FILE_ERROR\",\"port\":\"CreateFileW\",\"characters\":"<<std::wcslen(file)<<",\"error\":"<<error<<"}\n";log.flush();SetLastError(error);}return result;}
inline BOOL WINAPI createDirectory(LPCWSTR file,LPSECURITY_ATTRIBUTES security){if(!file||!inside(file))refuse("CreateDirectoryW-outside-owned-root");return ::CreateDirectoryW(file,security);}
}
#define GetProcAddress YmccSleepBoundary::resolve
#define CreateToolhelp32Snapshot YmccSleepBoundary::snapshot
#define OpenThread YmccSleepBoundary::openThread
#define OpenProcess YmccSleepBoundary::openProcess
#define OpenProcessToken YmccSleepBoundary::openToken
#define ResumeThread YmccSleepBoundary::resumeThread
#define SuspendThread YmccSleepBoundary::suspendThread
#define SetSuspendState YmccSleepBoundary::suspendState
#define WinHttpOpen YmccSleepBoundary::http
#define CreateProcessW YmccSleepBoundary::createProcess
#define ShellExecuteW YmccSleepBoundary::shell
#define CreateFileW YmccSleepBoundary::createFile
#define CreateDirectoryW YmccSleepBoundary::createDirectory
