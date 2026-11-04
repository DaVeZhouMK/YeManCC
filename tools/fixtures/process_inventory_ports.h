#pragma once
// Same TU: actual owner plus verbatim before/after consumer slices.
// Only OS facts are faked. No process/audio/configuration/filesystem discovery.
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "native/fxsound_recovery_policy.h"

namespace osfacts {
constexpr DWORD ownPid=42, ownSession=7;
inline const std::wstring expectedPath=L"Q:\\FixtureOnly\\FxSound LLC\\FxSound\\FxSound.exe";
struct Row {
    DWORD pid; std::wstring name=L"FxSound.exe"; DWORD session=ownSession;
    ULONGLONG birth=100; std::wstring path=expectedPath;
    bool sessionFail=false, openFail=false, alive=true, pathFail=false, birthFail=false;
    DWORD heldPid=0, heldSession=ownSession; bool heldSessionFail=false;
};
struct Held { bool snapshot=false; size_t index=0; Row row{0}; };
enum class QueryMode { complete, unsupported, failed, malformed, allocationFailure };
struct State {
    std::vector<Row> rows; std::map<UINT_PTR,Held> handles;
    QueryMode mode=QueryMode::complete; ULONGLONG tick=1000;
    DWORD error=ERROR_SUCCESS; bool ownSessionFail=false, snapshotFail=false, firstFail=false, partial=false;
    unsigned queryCalls=0, snapshots=0, opens=0, closes=0, sessionCalls=0, waits=0, violations=0;
    UINT_PTR nextHandle=1000;
};
inline State s;
inline DWORD WINAPI error(){return s.error;}
inline void WINAPI setError(DWORD value){s.error=value;}
inline ULONGLONG WINAPI tick(){return s.tick;}
inline DWORD WINAPI pid(){return ownPid;}
inline HANDLE hold(Held value){const auto key=++s.nextHandle;s.handles.emplace(key,std::move(value));return reinterpret_cast<HANDLE>(key);}
inline Held* lookup(HANDLE value){auto at=s.handles.find(reinterpret_cast<UINT_PTR>(value));if(at==s.handles.end()){++s.violations;return nullptr;}return &at->second;}
inline BOOL WINAPI close(HANDLE value){auto at=s.handles.find(reinterpret_cast<UINT_PTR>(value));if(at==s.handles.end()){++s.violations;return FALSE;}s.handles.erase(at);++s.closes;return TRUE;}
inline BOOL WINAPI session(DWORD value,DWORD* output){
    ++s.sessionCalls;
    if(value==ownPid){if(s.ownSessionFail){s.error=ERROR_ACCESS_DENIED;return FALSE;}*output=ownSession;return TRUE;}
    for(const auto& [key,held]:s.handles)if(!held.snapshot&&held.row.pid==value){if(held.row.heldSessionFail){s.error=ERROR_ACCESS_DENIED;return FALSE;}*output=held.row.heldSession;return TRUE;}
    for(const auto& row:s.rows)if(row.pid==value){if(row.sessionFail){s.error=ERROR_ACCESS_DENIED;return FALSE;}*output=row.session;return TRUE;}
    s.error=ERROR_INVALID_PARAMETER;return FALSE;
}
inline HANDLE WINAPI snapshot(DWORD flags,DWORD value){
    ++s.snapshots;if(flags!=TH32CS_SNAPPROCESS||value){++s.violations;return INVALID_HANDLE_VALUE;}
    if(s.snapshotFail){s.error=ERROR_ACCESS_DENIED;return INVALID_HANDLE_VALUE;}Held held;held.snapshot=true;return hold(held);
}
inline BOOL toolRow(Held* held,PROCESSENTRY32W* output){
    if(!held||!held->snapshot){++s.violations;return FALSE;}
    if(held->index>=s.rows.size()){s.error=ERROR_NO_MORE_FILES;return FALSE;}
    const auto& row=s.rows[held->index];*output={};output->dwSize=sizeof(*output);output->th32ProcessID=row.pid;
    wcsncpy_s(output->szExeFile,row.name.c_str(),_TRUNCATE);return TRUE;
}
inline BOOL WINAPI first(HANDLE handle,PROCESSENTRY32W* output){auto* held=lookup(handle);if(s.firstFail){s.error=ERROR_ACCESS_DENIED;return FALSE;}if(held)held->index=0;return toolRow(held,output);}
inline BOOL WINAPI next(HANDLE handle,PROCESSENTRY32W* output){auto* held=lookup(handle);if(s.partial){s.error=ERROR_ACCESS_DENIED;return FALSE;}if(held)++held->index;return toolRow(held,output);}
inline HANDLE WINAPI open(DWORD access,BOOL inherit,DWORD value){
    if(access!=(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)||inherit){++s.violations;return nullptr;}
    for(const auto& row:s.rows)if(row.pid==value){if(row.openFail){s.error=ERROR_ACCESS_DENIED;return nullptr;}++s.opens;Held held;held.row=row;return hold(held);}return nullptr;
}
inline DWORD WINAPI heldPid(HANDLE value){auto* h=lookup(value);return h?(h->row.heldPid?h->row.heldPid:h->row.pid):0;}
inline DWORD WINAPI wait(HANDLE value,DWORD ms){++s.waits;if(ms){++s.violations;return WAIT_FAILED;}auto* h=lookup(value);return h?(h->row.alive?WAIT_TIMEOUT:WAIT_OBJECT_0):WAIT_FAILED;}
inline BOOL WINAPI image(HANDLE value,DWORD flags,wchar_t* output,DWORD* count){auto* h=lookup(value);if(flags){++s.violations;return FALSE;}if(!h||h->row.pathFail||*count<=h->row.path.size())return FALSE;std::copy(h->row.path.begin(),h->row.path.end(),output);output[h->row.path.size()]=0;*count=static_cast<DWORD>(h->row.path.size());return TRUE;}
inline BOOL WINAPI times(HANDLE value,FILETIME* born,FILETIME* ended,FILETIME* kernel,FILETIME* user){auto* h=lookup(value);if(!h||h->row.birthFail)return FALSE;born->dwLowDateTime=static_cast<DWORD>(h->row.birth);born->dwHighDateTime=static_cast<DWORD>(h->row.birth>>32);*ended={};*kernel={};*user={};return TRUE;}
inline NTSTATUS NTAPI query(SYSTEM_INFORMATION_CLASS kind,PVOID output,ULONG capacity,PULONG returned){
    ++s.queryCalls;if(static_cast<int>(kind)!=252){++s.violations;return static_cast<NTSTATUS>(0xC0000003L);}
    if(s.mode==QueryMode::unsupported)return static_cast<NTSTATUS>(0xC0000003L);
    if(s.mode==QueryMode::failed)return static_cast<NTSTATUS>(0xC0000022L);
    if(s.mode==QueryMode::allocationFailure)throw std::bad_alloc();
    if(s.mode==QueryMode::malformed){*returned=1;return 0;}
    size_t required=0;const auto alignment=alignof(SYSTEM_BASICPROCESS_INFORMATION);
    for(const auto& row:s.rows){const auto bytes=sizeof(SYSTEM_BASICPROCESS_INFORMATION)+(row.name.size()+1)*sizeof(wchar_t);required+=(bytes+alignment-1)/alignment*alignment;}
    *returned=static_cast<ULONG>(required);if(capacity<required)return static_cast<NTSTATUS>(0xC0000004L);
    auto* base=static_cast<unsigned char*>(output);size_t offset=0;
    for(size_t i=0;i<s.rows.size();++i){const auto& row=s.rows[i];const auto bytes=(sizeof(SYSTEM_BASICPROCESS_INFORMATION)+(row.name.size()+1)*sizeof(wchar_t)+alignment-1)/alignment*alignment;
        std::memset(base+offset,0,bytes);SYSTEM_BASICPROCESS_INFORMATION record{};record.NextEntryOffset=i+1<s.rows.size()?static_cast<ULONG>(bytes):0;
        record.UniqueProcessId=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(row.pid));record.SequenceNumber=row.birth;
        record.ImageName.Length=static_cast<USHORT>(row.name.size()*sizeof(wchar_t));record.ImageName.MaximumLength=static_cast<USHORT>((row.name.size()+1)*sizeof(wchar_t));
        record.ImageName.Buffer=reinterpret_cast<wchar_t*>(base+offset+sizeof(record));std::memcpy(record.ImageName.Buffer,row.name.c_str(),record.ImageName.MaximumLength);std::memcpy(base+offset,&record,sizeof(record));offset+=bytes;
    }return 0;
}
inline ULONG NTAPI toDos(NTSTATUS){return ERROR_ACCESS_DENIED;}
inline HMODULE WINAPI module(LPCWSTR){return reinterpret_cast<HMODULE>(1);}
inline FARPROC WINAPI proc(HMODULE,LPCSTR name){if(!std::strcmp(name,"NtQuerySystemInformation"))return reinterpret_cast<FARPROC>(&query);if(!std::strcmp(name,"RtlNtStatusToDosError"))return reinterpret_cast<FARPROC>(&toDos);++s.violations;return nullptr;}
inline std::filesystem::path executable(){return std::filesystem::path(expectedPath);}
} // namespace osfacts

// SDK declarations were included before these substitutions. Production owner
// and both verbatim consumer generations below see only these fake APIs.
#define GetLastError osfacts::error
#define SetLastError osfacts::setError
#define GetTickCount64 osfacts::tick
#define GetCurrentProcessId osfacts::pid
#define ProcessIdToSessionId osfacts::session
#define CreateToolhelp32Snapshot osfacts::snapshot
#define Process32FirstW osfacts::first
#define Process32NextW osfacts::next
#define OpenProcess osfacts::open
#define CloseHandle osfacts::close
#define GetProcessId osfacts::heldPid
#define WaitForSingleObject osfacts::wait
#define QueryFullProcessImageNameW osfacts::image
#define GetProcessTimes osfacts::times
#define GetModuleHandleW osfacts::module
#define GetProcAddress osfacts::proc
#include "native/process_inventory.h"
