#pragma once
#include <tlhelp32.h>
#include <memory>
#include <functional>
#include <cstring>
namespace FoundationMemory {
struct Process {
    DWORD pid=42, parent=4; uint64_t created=100;
    std::wstring image=L"C:\\MSI\\MSIAPService.exe";
    bool alive=true, denied=false, timesDenied=false, pathDenied=false, waitFailed=false;
};
struct Handle { std::shared_ptr<Process> process; std::vector<PROCESSENTRY32W> rows; size_t cursor=0; bool snapshot=false; };
static std::map<DWORD,std::shared_ptr<Process>> processes;
static unsigned opens=0,closes=0,snapshots=0,firstCalls=0,nextCalls=0,processOpens=0,waitCalls=0;
static unsigned failSnapshot=0,failFirst=0,failNextAt=0;
static bool unsafeRights=false; static ULONGLONG tick=555,tickStep=0;
static std::function<void(unsigned)> onSnapshot;
static std::function<void(DWORD)> onOpen;
static std::function<void(DWORD)> onWait;
static void reset() {
    processes.clear(); opens=closes=snapshots=firstCalls=nextCalls=processOpens=waitCalls=0;
    failSnapshot=failFirst=failNextAt=0; unsafeRights=false; tick=555; tickStep=0;
    onSnapshot={}; onOpen={}; onWait={};
}
static std::shared_ptr<Process> add(DWORD pid,DWORD parent,uint64_t created) {
    auto p=std::make_shared<Process>();p->pid=pid;p->parent=parent;p->created=created;
    p->image=L"C:\\MSI\\process-"+std::to_wstring(pid)+L".exe";processes[pid]=p;return p;
}
static ULONGLONG clock() { const auto value=tick;tick+=tickStep;return value; }
static HANDLE open(DWORD access,BOOL inherit,DWORD pid) {
    ++processOpens;if(access!=(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)||inherit)unsafeRights=true;
    if(onOpen)onOpen(pid);
    auto it=processes.find(pid);
    if(it==processes.end()||it->second->denied){SetLastError(it==processes.end()?ERROR_INVALID_PARAMETER:ERROR_ACCESS_DENIED);return nullptr;}
    auto h=new Handle;h->process=it->second;++opens;return reinterpret_cast<HANDLE>(h);
}
static BOOL close(HANDLE raw) { delete reinterpret_cast<Handle*>(raw);++closes;return TRUE; }
static HANDLE snapshot(DWORD flags,DWORD pid) {
    ++snapshots;if(flags!=TH32CS_SNAPPROCESS||pid)unsafeRights=true;
    if(onSnapshot)onSnapshot(snapshots);
    if(failSnapshot==snapshots){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    auto h=new Handle;h->snapshot=true;
    for(const auto& [id,p]:processes)if(p->alive){PROCESSENTRY32W row{};row.dwSize=sizeof(row);row.th32ProcessID=id;row.th32ParentProcessID=p->parent;h->rows.push_back(row);}
    ++opens;return reinterpret_cast<HANDLE>(h);
}
static BOOL first(HANDLE raw,LPPROCESSENTRY32W row) {
    ++firstCalls;auto& h=*reinterpret_cast<Handle*>(raw);
    if(failFirst==snapshots){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    h.cursor=0;if(h.rows.empty()){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}*row=h.rows[0];return TRUE;
}
static BOOL next(HANDLE raw,LPPROCESSENTRY32W row) {
    ++nextCalls;auto& h=*reinterpret_cast<Handle*>(raw);
    if(failNextAt==nextCalls){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(++h.cursor>=h.rows.size()){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}*row=h.rows[h.cursor];return TRUE;
}
static BOOL times(HANDLE raw,LPFILETIME created,LPFILETIME exited,LPFILETIME kernel,LPFILETIME user) {
    auto& p=*reinterpret_cast<Handle*>(raw)->process;
    if(p.timesDenied){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    created->dwLowDateTime=static_cast<DWORD>(p.created);created->dwHighDateTime=static_cast<DWORD>(p.created>>32);
    *exited={};*kernel={};*user={};return TRUE;
}
static BOOL path(HANDLE raw,DWORD flags,LPWSTR buffer,PDWORD size) {
    auto& p=*reinterpret_cast<Handle*>(raw)->process;if(flags)unsafeRights=true;
    if(p.pathDenied){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(*size<=p.image.size()){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memcpy(buffer,p.image.data(),p.image.size()*sizeof(wchar_t));*size=static_cast<DWORD>(p.image.size());return TRUE;
}
static DWORD wait(HANDLE raw,DWORD millis) {
    ++waitCalls;auto& p=*reinterpret_cast<Handle*>(raw)->process;if(millis)unsafeRights=true;
    if(onWait)onWait(p.pid);
    if(p.waitFailed){SetLastError(ERROR_ACCESS_DENIED);return WAIT_FAILED;}
    return p.alive?WAIT_TIMEOUT:WAIT_OBJECT_0;
}
}
// Every process/snapshot boundary used by the production helper is redirected.
#define OpenProcess FoundationMemory::open
#define CloseHandle FoundationMemory::close
#define CreateToolhelp32Snapshot FoundationMemory::snapshot
#define Process32FirstW FoundationMemory::first
#define Process32NextW FoundationMemory::next
#define GetProcessTimes FoundationMemory::times
#define QueryFullProcessImageNameW FoundationMemory::path
#define WaitForSingleObject FoundationMemory::wait
