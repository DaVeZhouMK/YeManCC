// Run the actual production process matcher/stop loop with memory-only Win32 APIs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include "json.hpp"
using json = nlohmann::json;
struct Process { std::wstring name, image; bool deny=false, exited=false, terminateFail=false, waitTimeout=false; };
static std::vector<Process> processes;
static std::vector<DWORD> killed;
static std::vector<json> logs;
static size_t cursor=0;
static DWORD enumError=ERROR_NO_MORE_FILES;
static bool snapshotFail=false, current=true;
static ULONGLONG tick=1;
static int openHandles=0, closedHandles=0;
static std::string W2U(const std::wstring& s) { return {s.begin(),s.end()}; }
static void appendNativeLifecycleLog(const char*,json j){logs.push_back(std::move(j));}
static bool oemVendorStackIntentCurrent(unsigned long long){return current;}
static HANDLE fakeSnapshot(DWORD,DWORD){if(snapshotFail){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}++openHandles;return reinterpret_cast<HANDLE>(1);}
static BOOL fakeEntry(PROCESSENTRY32W* e){if(cursor>=processes.size()){SetLastError(enumError);return FALSE;}e->th32ProcessID=static_cast<DWORD>(cursor+10);wcscpy_s(e->szExeFile,processes[cursor].name.c_str());return TRUE;}
static BOOL fakeFirst(HANDLE,PROCESSENTRY32W* e){cursor=0;return fakeEntry(e);}
static BOOL fakeNext(HANDLE,PROCESSENTRY32W* e){++cursor;return fakeEntry(e);}
static HANDLE fakeOpen(DWORD,BOOL,DWORD id){auto& p=processes.at(id-10);if(p.deny){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}++openHandles;return reinterpret_cast<HANDLE>(&p);}
static BOOL fakeCode(HANDLE h,DWORD* code){*code=reinterpret_cast<Process*>(h)->exited?0:STILL_ACTIVE;return TRUE;}
static BOOL fakeImage(HANDLE h,DWORD,wchar_t* data,DWORD* count){const auto& p=*reinterpret_cast<Process*>(h);wcscpy_s(data,*count,p.image.c_str());*count=static_cast<DWORD>(p.image.size());return TRUE;}
static BOOL fakeTerminate(HANDLE h,UINT){auto& p=*reinterpret_cast<Process*>(h);if(p.terminateFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}killed.push_back(static_cast<DWORD>(&p-processes.data()+10));return TRUE;}
static DWORD fakeWait(HANDLE h,DWORD){return reinterpret_cast<Process*>(h)->waitTimeout?WAIT_TIMEOUT:WAIT_OBJECT_0;}
static BOOL fakeClose(HANDLE){++closedHandles;return TRUE;}
#define CreateToolhelp32Snapshot fakeSnapshot
#define Process32FirstW fakeFirst
#define Process32NextW fakeNext
#define OpenProcess fakeOpen
#define GetExitCodeProcess fakeCode
#define QueryFullProcessImageNameW fakeImage
#define TerminateProcess fakeTerminate
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
#define GetTickCount64() tick
/* PRODUCTION_PROCESS_METHODS */
static int cases=0,failed=0;
static void check(bool ok,const char* text){++cases;std::cout<<(ok?"PASS ":"FAIL ")<<text<<"\n";if(!ok)++failed;}
static void reset(){processes.clear();killed.clear();logs.clear();cursor=0;enumError=ERROR_NO_MORE_FILES;snapshotFail=false;current=true;tick=1;openHandles=closedHandles=0;SetLastError(ERROR_SUCCESS);}
static Process process(std::wstring name){return {name,L"C:\\Vendor\\"+name};}
int main(){
 const std::vector<std::wstring> names{L"MSI Center M"};
 check(oemVendorProcessMatches(L"C:\\Vendor\\mSi cEnTeR m.ExE",names),"allowlist accepts exact basename case-insensitively");
 check(!oemVendorProcessMatches(L"MSI Center M Helper.exe",names)&&!oemVendorProcessMatches(L"MSI Center M.exe.bak",names),"allowlist does not terminate prefix/suffix impostors");
 reset();check(oemVendorProcessesStop({},1)&&openHandles==0,"empty HC process list makes no process API calls");
 reset();check(oemVendorProcessesStop(names,1)&&openHandles==closedHandles,"normal empty enumeration is verified and closes snapshot");
 reset();snapshotFail=true;check(!oemVendorProcessesStop(names,1)&&killed.empty(),"unreadable process snapshot cannot claim suppression");
 reset();enumError=ERROR_ACCESS_DENIED;check(!oemVendorProcessesStop(names,1),"initial enumeration failure is not absent capability");
 reset();processes={process(L"Other.exe")};enumError=ERROR_ACCESS_DENIED;check(!oemVendorProcessesStop(names,1)&&openHandles==closedHandles,"mid-enumeration failure blocks success after a valid entry");
 reset();processes={process(L"MSI Center M.exe"),process(L"Other.exe")};check(oemVendorProcessesStop(names,1)&&killed.size()==1&&openHandles==closedHandles,"actual stop loop kills only the HC-listed process and closes every handle");
 reset();processes={process(L"MSI Center M.exe")};processes[0].image=L"C:\\Vendor\\ReusedPid.exe";check(!oemVendorProcessesStop(names,1)&&killed.empty(),"PID identity is rechecked using full image path before terminating");
 reset();processes={process(L"MSI Center M.exe")};processes[0].deny=true;check(!oemVendorProcessesStop(names,1)&&killed.empty(),"process access denial blocks success");
 reset();processes={process(L"MSI Center M.exe")};processes[0].exited=true;check(oemVendorProcessesStop(names,1)&&killed.empty(),"already exited process is not killed again");
 reset();processes={process(L"MSI Center M.exe")};processes[0].terminateFail=true;check(!oemVendorProcessesStop(names,1),"failed terminate does not claim process stopped");
 reset();processes={process(L"MSI Center M.exe")};processes[0].waitTimeout=true;check(!oemVendorProcessesStop(names,1),"timeout after terminate does not claim terminal process state");
 reset();processes={process(L"MSI Center M.exe")};current=false;check(!oemVendorProcessesStop(names,1)&&killed.empty()&&openHandles==closedHandles,"revoked ownership refuses process termination");
 reset();for(int i=0;i<33;++i)processes.push_back(process(L"MSI Center M.exe"));check(!oemVendorProcessesStop(names,1)&&killed.size()==32&&openHandles==closedHandles,"process stop work has a bounded instance budget");
 std::cout<<"cases="<<cases<<" failures="<<failed<<" hardwareCalls=0 RESULT="<<(failed?"FAIL":"PASS")<<"\n";return failed?1:0;
}
