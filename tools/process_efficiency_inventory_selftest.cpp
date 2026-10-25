#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <map>
#include <string>
#include <iostream>
#include <stdexcept>
namespace fake {
DWORD last=123,openCount=0,closeCount=0,policyWrites=0;bool snapshotFailed=false;
struct Item{DWORD pid;const wchar_t* name;const wchar_t* path;bool partial=false;DWORD priority=NORMAL_PRIORITY_CLASS;DWORD control=0,state=0;};
std::array<Item,5> items{{{100,L"YeManCC.exe",L"C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe"},{101,L"YeManFanHost.exe",L"C:\\SOFT\\YeMan\\PowerControl\\fan-host\\YeManFanHost.exe"},{102,L"YeManInputHost.exe",L"D:\\Games\\YeManInputHost.exe"},{103,L"game.exe",L"C:\\SOFT\\YeMan\\game.exe"},{104,L"YeManBatteryHost.exe",L"C:\\SOFT\\YeMan\\PowerControl\\battery-host\\YeManBatteryHost.exe",true}}};
unsigned cursor=0;DWORD getError(){return last;}void setError(DWORD value){last=value;}DWORD currentId(){return 100;}
DWORD module(HMODULE,LPWSTR output,DWORD length){const auto text=items[0].path;wcscpy_s(output,length,text);return static_cast<DWORD>(wcslen(text));}
HANDLE snapshot(DWORD,DWORD){return snapshotFailed?INVALID_HANDLE_VALUE:reinterpret_cast<HANDLE>(900);}
BOOL entry(LPPROCESSENTRY32W output){if(cursor>=items.size())return FALSE;output->th32ProcessID=items[cursor].pid;wcscpy_s(output->szExeFile,items[cursor].name);return TRUE;}
BOOL first(HANDLE,LPPROCESSENTRY32W output){cursor=0;return entry(output);}BOOL next(HANDLE,LPPROCESSENTRY32W output){++cursor;return entry(output);}
HANDLE open(DWORD,BOOL,DWORD pid){++openCount;return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(pid));}
BOOL close(HANDLE){++closeCount;last=77;return TRUE;}
Item& find(HANDLE h){const auto pid=static_cast<DWORD>(reinterpret_cast<UINT_PTR>(h));for(auto& item:items)if(item.pid==pid)return item;throw std::runtime_error("foreign handle");}
BOOL image(HANDLE h,DWORD,LPWSTR output,PDWORD length){auto& item=find(h);wcscpy_s(output,*length,item.path);*length=static_cast<DWORD>(wcslen(item.path));return TRUE;}
BOOL getInfo(HANDLE h,PROCESS_INFORMATION_CLASS,LPVOID buffer,DWORD){auto& item=find(h);auto* words=static_cast<DWORD*>(buffer);words[0]=1;words[1]=item.control;words[2]=item.state;return TRUE;}
BOOL setInfo(HANDLE h,PROCESS_INFORMATION_CLASS,LPVOID buffer,DWORD){auto& item=find(h);++policyWrites;if(item.partial){last=ERROR_ACCESS_DENIED;return FALSE;}auto* words=static_cast<DWORD*>(buffer);item.control=words[1];item.state=words[2];return TRUE;}
DWORD getPriority(HANDLE h){return find(h).priority;}BOOL setPriority(HANDLE h,DWORD p){find(h).priority=p;return TRUE;}
}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetCurrentProcessId fake::currentId
#define GetModuleFileNameW fake::module
#define CreateToolhelp32Snapshot fake::snapshot
#define Process32FirstW fake::first
#define Process32NextW fake::next
#define OpenProcess fake::open
#define CloseHandle fake::close
#define QueryFullProcessImageNameW fake::image
#define GetProcessInformation fake::getInfo
#define SetProcessInformation fake::setInfo
#define GetPriorityClass fake::getPriority
#define SetPriorityClass fake::setPriority
#include "../native/process_efficiency_inventory.h"
int main(){try{
 const auto r=ymcc::processEfficiency::applyExistingTasksOnce();
 if(r.found!=2||r.confirmed!=1||r.partial!=1||fake::openCount!=3||fake::closeCount!=4||fake::last!=123||fake::policyWrites!=2)throw std::runtime_error("inventory classification/cleanup/receipt mismatch");
 if(fake::items[0].priority!=NORMAL_PRIORITY_CLASS||fake::items[2].priority!=NORMAL_PRIORITY_CLASS||fake::items[3].priority!=NORMAL_PRIORITY_CLASS)throw std::runtime_error("self/external process changed by inventory");
 if(fake::items[1].priority!=IDLE_PRIORITY_CLASS||fake::items[4].priority!=IDLE_PRIORITY_CLASS)throw std::runtime_error("owned task not opted in");
 fake::snapshotFailed=true;const auto failed=ymcc::processEfficiency::applyExistingTasksOnce();if(failed.found||fake::last!=123)throw std::runtime_error("snapshot failure not benign");
 std::cout<<"{\"pass\":true,\"ownedFound\":2,\"fullyConfirmed\":1,\"partialVisible\":1,\"externalProcessesChanged\":false,\"allHandlesClosed\":true,\"realProcessEnumerationOrMutation\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}