// Fixture ports only. No windows.h, Steam SDK, process access, debugger, or DLL loading.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "json.hpp"
using J=nlohmann::json;
using U64=std::uint64_t;
using DWORD=std::uint32_t;
using BOOL=int;
using PWSTR=wchar_t*;
using Current=std::function<bool()>;
constexpr BOOL FALSE=0,TRUE=1;
constexpr DWORD WAIT_OBJECT_0=0,WAIT_TIMEOUT=258,WAIT_FAILED=0xffffffff;
constexpr DWORD ERROR_NO_MORE_FILES=18,ERROR_ACCESS_DENIED=5;
constexpr DWORD TH32CS_SNAPTHREAD=4,THREAD_QUERY_LIMITED_INFORMATION=0x0800,SYNCHRONIZE=0x100000;
struct FILETIME {DWORD dwLowDateTime=0,dwHighDateTime=0;};
struct LARGE_INTEGER {long long QuadPart=0;};
struct THREADENTRY32 {DWORD dwSize=0,th32ThreadID=0,th32OwnerProcessID=0;};
struct FakeHandle {std::string kind;DWORD tid=0;std::function<BOOL()> signal;};
using HANDLE=FakeHandle*;
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(std::intptr_t(-1))
struct ThreadSpec {
  DWORD tid=41,pid=11;U64 birth=0x100000037ULL;
  std::wstring description=L"IPC:CSteamEngine";
  bool open=true,times=true,nullDescription=false;int hr=0;
  DWORD wait=WAIT_TIMEOUT;int timeCalls=0,descriptionCalls=0;
};
struct FixturePorts {
  std::vector<ThreadSpec> threads{ThreadSpec{}};
  std::vector<THREADENTRY32> rows{{sizeof(THREADENTRY32),41,11}};
  std::set<HANDLE> handles;std::set<PWSTR> strings;
  J events=J::array();std::size_t cursor=0;
  DWORD lastError=0,terminalError=ERROR_NO_MORE_FILES;
  bool snapshotOk=true,firstOk=true,policyOk=true,live=true,throwCurrent=false,exact=true;
  bool queryDispatch=true,queryComplete=true;U64 queryResult=77;std::string queryError;
  std::string captureFail;bool captureRevoke=false;
  int captures=0,queryFires=0,policies=0,censuses=0,opened=0,currentCalls=0,exactCalls=0,prepares=0;
  int lastErrorReads=0,localFrees=0;bool witnessEmptyAtCapture=true;
  std::vector<DWORD> openedTids;
  std::function<void()> afterCensus,afterQuery,afterCapture;
  void note(const char* s){events.push_back(s);}
  ThreadSpec& thread(DWORD tid){for(auto& t:threads)if(t.tid==tid)return t;throw std::runtime_error("fixture-unknown-thread");}
};
thread_local FixturePorts f;
HANDLE makeHandle(std::string kind,DWORD tid=0){auto* h=new FakeHandle{std::move(kind),tid,{}};f.handles.insert(h);return h;}
BOOL CloseHandle(HANDLE h){if(!h||h==INVALID_HANDLE_VALUE||!f.handles.erase(h))throw std::runtime_error("fixture-invalid-close");delete h;f.lastError=ERROR_ACCESS_DENIED;return TRUE;}
DWORD GetCurrentProcessId(){return 9001;}
BOOL QueryPerformanceCounter(LARGE_INTEGER* p){p->QuadPart=1;return TRUE;}
DWORD GetLastError(){++f.lastErrorReads;f.note("last-error:read");return f.lastError;}
BOOL DebugSetProcessKillOnExit(BOOL value){if(value!=FALSE)throw std::runtime_error("fixture-policy-wrong-argument");++f.policies;f.note("policy:false");return f.policyOk;}
HANDLE CreateToolhelp32Snapshot(DWORD flags,DWORD pid){
  if(flags!=TH32CS_SNAPTHREAD||pid)throw std::runtime_error("fixture-census-wrong-arguments");
  ++f.censuses;f.note("census:begin");return f.snapshotOk?makeHandle("census"):INVALID_HANDLE_VALUE;
}
BOOL Thread32First(HANDLE h,THREADENTRY32* row){
  if(!h||h->kind!="census"||row->dwSize!=sizeof *row)throw std::runtime_error("fixture-census-first-arguments");
  f.cursor=0;if(!f.firstOk||f.rows.empty()){f.lastError=ERROR_NO_MORE_FILES;return FALSE;}
  *row=f.rows[0];return TRUE;
}
BOOL Thread32Next(HANDLE h,THREADENTRY32* row){
  if(!h||h->kind!="census")throw std::runtime_error("fixture-census-next-arguments");
  if(++f.cursor<f.rows.size()){*row=f.rows[f.cursor];return TRUE;}
  // A completed enumeration must read this value before another API can clobber it.
  f.lastError=f.terminalError;f.note("census:end");if(f.afterCensus)f.afterCensus();return FALSE;
}
HANDLE OpenThread(DWORD access,BOOL inherit,DWORD tid){
  if(access!=(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)||inherit)throw std::runtime_error("fixture-thread-rights");
  ++f.opened;f.openedTids.push_back(tid);f.lastError=ERROR_ACCESS_DENIED;
  return f.thread(tid).open?makeHandle("thread",tid):nullptr;
}
DWORD WaitForSingleObject(HANDLE h,DWORD budget){
  f.lastError=ERROR_ACCESS_DENIED;
  if(!h||h==INVALID_HANDLE_VALUE)return WAIT_FAILED;
  if(h->kind=="query-done"){f.note("query:wait");return f.queryComplete?WAIT_OBJECT_0:WAIT_TIMEOUT;}
  if(h->kind!="thread"||budget)throw std::runtime_error("fixture-unexpected-wait");
  return f.thread(h->tid).wait;
}
DWORD GetProcessIdOfThread(HANDLE h){f.lastError=ERROR_ACCESS_DENIED;return f.thread(h->tid).pid;}
BOOL GetThreadTimes(HANDLE h,FILETIME* born,FILETIME*,FILETIME*,FILETIME*){
  auto& t=f.thread(h->tid);++t.timeCalls;f.lastError=ERROR_ACCESS_DENIED;
  born->dwLowDateTime=DWORD(t.birth);born->dwHighDateTime=DWORD(t.birth>>32);return t.times;
}
int GetThreadDescription(HANDLE h,PWSTR* raw){
  auto& t=f.thread(h->tid);++t.descriptionCalls;f.lastError=ERROR_ACCESS_DENIED;
  *raw=nullptr;if(!t.nullDescription){*raw=new wchar_t[t.description.size()+1];std::copy(t.description.c_str(),t.description.c_str()+t.description.size()+1,*raw);f.strings.insert(*raw);}
  return t.hr;
}
void* LocalFree(PWSTR p){if(!f.strings.erase(p))throw std::runtime_error("fixture-invalid-local-free");delete[] p;++f.localFrees;f.lastError=ERROR_ACCESS_DENIED;return nullptr;}
BOOL ResetEvent(HANDLE h){if(!h||h->kind!="query-done")throw std::runtime_error("fixture-reset-event");return TRUE;}
BOOL SetEvent(HANDLE h){if(!h||!h->signal)throw std::runtime_error("fixture-set-event");return h->signal();}
namespace rw {
  bool exactProcess(HANDLE,DWORD,U64,const std::wstring&,DWORD){++f.exactCalls;return f.exact;}
}
thread_local Current gCurrent;
struct Snapshot {}; // Selection never reads Snapshot; full capture is an opaque fixture port.
