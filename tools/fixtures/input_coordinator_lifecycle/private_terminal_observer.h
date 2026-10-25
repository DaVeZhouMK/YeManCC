#pragma once
// Private fixture observation only. Never scans, opens a foreign PID or owns/closes a process handle.
#include <windows.h>
#include <array>
#include <mutex>
#include <string>
#include <limits>
namespace ymccPrivateTerminal {
static constexpr size_t ownerCapacity=256, requestCapacity=128;
struct Owner { DWORD pid=0; std::string birth; HANDLE observation=nullptr; bool self=false, exited=false, unknown=false; };
struct Request { unsigned long long ordinal=0; const char* kind=nullptr; };
static std::mutex observerMutex;
static std::array<Owner,ownerCapacity> owners{};
static std::array<Request,requestCapacity> requests{};
static size_t ownerCount=0, requestCount=0;
static unsigned long long requestDropped=0, observationRejected=0;
static void incrementBounded(unsigned long long& n){if(n!=std::numeric_limits<unsigned long long>::max())++n;}
static std::string ownedBirth(HANDLE exactOwnedHandle){
 FILETIME c{},e{},k{},u{};if(!GetProcessTimes(exactOwnedHandle,&c,&e,&k,&u))return {};
 ULARGE_INTEGER v{};v.LowPart=c.dwLowDateTime;v.HighPart=c.dwHighDateTime;return std::to_string(v.QuadPart);
}
static void recordHidRequest(){
 std::lock_guard<std::mutex> lock(observerMutex);
 if(requestCount==requestCapacity){incrementBounded(requestDropped);return;}
 requests[requestCount]={requestCount+1,"offline-hid-diagnostic-request"};++requestCount;
}
static void observeSelf(DWORD pid,HANDLE exactSelf){
 if(exactSelf!=GetCurrentProcess()||pid!=GetCurrentProcessId()){std::lock_guard<std::mutex> lock(observerMutex);incrementBounded(observationRejected);return;}
 const auto birth=ownedBirth(exactSelf);
 std::lock_guard<std::mutex> lock(observerMutex);
 if(exactSelf!=GetCurrentProcess()||pid!=GetCurrentProcessId()||birth.empty()||ownerCount==ownerCapacity){incrementBounded(observationRejected);return;}
 for(size_t i=0;i<ownerCount;++i)if(owners[i].pid==pid&&owners[i].birth==birth)return;
 owners[ownerCount++]={pid,birth,nullptr,true,false,false};
}
// Called only after successful CreateProcessW + DuplicateHandle in the original owned-child factory.
static void observeChildCreated(HANDLE exactOwnedObservation,DWORD createdPid){
 const auto birth=ownedBirth(exactOwnedObservation);const auto actualPid=GetProcessId(exactOwnedObservation);
 std::lock_guard<std::mutex> lock(observerMutex);
 if(!exactOwnedObservation||!createdPid||actualPid!=createdPid||birth.empty()||createdPid==GetCurrentProcessId()||ownerCount==ownerCapacity){incrementBounded(observationRejected);return;}
 for(size_t i=0;i<ownerCount;++i)if(owners[i].pid==createdPid&&owners[i].birth==birth){incrementBounded(observationRejected);return;}
 owners[ownerCount++]={createdPid,birth,exactOwnedObservation,false,false,false};
}
// Original wait result is observed; a second zero-wait and exact PID/birth verify the same still-owned handle.
static void observeChildWait(HANDLE exactOwnedObservation,DWORD originalWait){
 std::lock_guard<std::mutex> lock(observerMutex);
 for(size_t i=0;i<ownerCount;++i){auto& row=owners[i];if(row.self||row.observation!=exactOwnedObservation)continue;
  DWORD code=0;
  if(originalWait==WAIT_OBJECT_0&&WaitForSingleObject(exactOwnedObservation,0)==WAIT_OBJECT_0&&GetProcessId(exactOwnedObservation)==row.pid&&ownedBirth(exactOwnedObservation)==row.birth&&GetExitCodeProcess(exactOwnedObservation,&code))row.exited=true;
  // Even on timeout/failure, retire only our observation alias before original CloseHandle. Never infer death.
  else row.unknown=true;
  row.observation=nullptr;return;
 }
 incrementBounded(observationRejected);
}
static bool proveOldOwnerDead(DWORD pid,const std::string& birth){
 std::lock_guard<std::mutex> lock(observerMutex);
 if(!pid||birth.empty())return false;
 for(size_t i=0;i<ownerCount;++i){const auto& row=owners[i];if(row.pid==pid&&row.birth==birth)return !row.self&&!row.unknown&&row.exited;}
 return false;
}
struct Snapshot {size_t owners,requests;unsigned long long dropped,rejected;};
static Snapshot snapshot(){std::lock_guard<std::mutex> lock(observerMutex);return {ownerCount,requestCount,requestDropped,observationRejected};}
} // namespace ymccPrivateTerminal
