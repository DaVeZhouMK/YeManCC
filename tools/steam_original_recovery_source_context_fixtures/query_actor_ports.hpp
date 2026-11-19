#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "json.hpp"

// The complete production H, query waits, and actor bodies are supplied by the
// original prepare-only extractor. Never substitute a lifecycle model.
using U64=std::uint64_t;
using J=nlohmann::json;
using Current=std::function<bool()>;
using WakeSignal=std::uintptr_t;
enum class RecoveryRoute { LegacyDynamic,SemiStatic };
inline void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
#include "actual-handle.inc"

// A lifetime selection must receive the parent ledger deadline through CLI.
// No phase date/deadline is embedded in the reusable production fixture source.
inline U64 fixtureDeadline=0;
inline U64 fixtureNowMs()noexcept{
 FILETIME value{};GetSystemTimeAsFileTime(&value);
 ULARGE_INTEGER ticks{};ticks.LowPart=value.dwLowDateTime;ticks.HighPart=value.dwHighDateTime;
 return (ticks.QuadPart-116444736000000000ULL)/10000ULL;
}
inline DWORD fixtureRemaining(DWORD cap=5000){
 const auto now=fixtureNowMs();if(now>=fixtureDeadline)return 0; // Expiry cannot unwind a joinable owner.
 return static_cast<DWORD>((std::min)(static_cast<U64>(cap),fixtureDeadline-now));
}

enum class FixtureFault { None,FactoryMissing,FactoryNull,ReleaseUserThrow,PipeFalse,PipeThrow,ShutdownFalse,FreeLibraryFalse,GetSteamIdThrow,CurrentAfterReply };
enum class FixtureBlock { None,ReleaseUser,ReleasePipe };
struct FixtureState {
 // Default mode preserves the original no-IO Current tests and their trace.
 // Lifetime mode alone bridges the parent's pinned, inert local fixture DLL.
 bool realLoader=false,wrongAccount=false;
 FixtureFault fault=FixtureFault::None;FixtureBlock block=FixtureBlock::None;
 std::wstring dllPath;std::string cancelAt;
 std::atomic<bool> live{true},portViolation{false},prematureUnload{false};
 std::atomic<int> loads{0},lookups{0},factories{0},moduleRefs{0},freeCalls{0},freeSuccess{0};
 std::atomic<int> creates{0},connects{0},userInterfaces{0},steamIds{0};
 std::atomic<int> releaseUserCalls{0},releaseUserSuccess{0},releasePipeCalls{0},releasePipeSuccess{0};
 std::atomic<int> shutdownCalls{0},shutdownSuccess{0},userOwned{0},pipeOwned{0},clientOwned{0};
 std::atomic<int> scopeCreated{0},scopeDestroyed{0},gateWaits{0};
 std::atomic<HMODULE> loadedModule{nullptr};
 std::mutex mutex;std::vector<std::string> steps;std::vector<DWORD> stepThreads;
 H releaseEntered{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
 H releasePermit{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
 H ownerDone{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
 void call(const char* name){
  std::lock_guard<std::mutex> lock(mutex);steps.emplace_back(name);
  stepThreads.push_back(GetCurrentThreadId());if(cancelAt==name)live=false;
 }
 void gate(FixtureBlock point){
  if(block!=point)return;
  ++gateWaits;need(SetEvent(releaseEntered.h)!=FALSE,"fixture-release-entered-signal");
  need(WaitForSingleObject(releasePermit.h,fixtureRemaining())==WAIT_OBJECT_0,"fixture-release-permit");
 }
 bool moduleLoaded()const noexcept{
  if(!realLoader)return moduleRefs.load()!=0;
  HMODULE observed=nullptr;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,dllPath.c_str(),&observed)!=FALSE&&observed==loadedModule.load();
 }
 J counters()const{
  return {{"loads",loads.load()},{"lookups",lookups.load()},{"factories",factories.load()},
   {"moduleReferences",moduleRefs.load()},{"freeLibraryCalls",freeCalls.load()},{"freeLibrarySuccess",freeSuccess.load()},
   {"createPipeCalls",creates.load()},{"connectUserCalls",connects.load()},{"getUserInterfaceCalls",userInterfaces.load()},
   {"getSteamIdCalls",steamIds.load()},{"releaseUserCalls",releaseUserCalls.load()},{"releaseUserSuccess",releaseUserSuccess.load()},
   {"releasePipeCalls",releasePipeCalls.load()},{"releasePipeSuccess",releasePipeSuccess.load()},
   {"shutdownCalls",shutdownCalls.load()},{"shutdownSuccess",shutdownSuccess.load()},
   {"usersStillOwned",userOwned.load()},{"pipesStillOwned",pipeOwned.load()},{"clientStillOwned",clientOwned.load()},
   {"scopeCreated",scopeCreated.load()},{"scopeDestroyed",scopeDestroyed.load()},{"releaseGateWaits",gateWaits.load()},
   {"prematureUnload",prematureUnload.load()},{"portViolation",portViolation.load()}};
 }
};
inline FixtureState* fixture=nullptr;
inline thread_local Current gCurrent;
inline thread_local WakeSignal gCancelSignal=0,gIdentityCancelSignal=0;
inline void requireCurrent(){need(!gCurrent||gCurrent(),"normal-query-current-revoked");}

// Lifetime-only ReadScope port. This does not acquire an Observer delegation or
// attest production native identity/power qualification. Its two borrowed
// cancellation events are real, sticky local Win32 events.
namespace ymcc::steamnative {
class ReadScope {
 std::shared_ptr<FixtureState> owner_;
 H power_{CreateEventW(nullptr,TRUE,FALSE,nullptr)},identity_{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
public:
 explicit ReadScope(std::shared_ptr<FixtureState> state):owner_(std::move(state)){
  need(power_.good()&&identity_.good(),"fixture-scope-events");++owner_->scopeCreated;
 }
 virtual ~ReadScope()noexcept{
  power_=H{};identity_=H{};++owner_->scopeDestroyed;
 }
 virtual bool current()noexcept{return owner_->live.load();}
 virtual HANDLE powerCancellationEvent()const noexcept{return power_.h;}
 virtual HANDLE identityCancellationEvent()const noexcept{return identity_.h;}
 virtual void cancel()noexcept{owner_->live=false;SetEvent(power_.h);SetEvent(identity_.h);}
};
}
inline thread_local std::shared_ptr<ymcc::steamnative::ReadScope> gPublicationScope;
#include "actual-query-wait.inc"

namespace rc {inline std::optional<int> profileIdForSha256(const std::string&){return 1;}}
namespace compat {
 enum class ModuleKind { Client };
 struct Qualified {std::optional<int> layout{1};};
 inline Qualified qualifyFile(const std::wstring&,ModuleKind){fixture->call("qualification");return {};}
}
inline std::string fileHash(const std::wstring&){fixture->call("qualification");return "qualified-client";}
using HSteamPipe=int;using HSteamUser=int;
inline constexpr const char* STEAMCLIENT_INTERFACE_VERSION="offline-client-interface";
inline constexpr const char* STEAMUSER_INTERFACE_VERSION="offline-user-interface";
struct FixtureSteamId {U64 ConvertToUint64()const{return fixture->wrongAccount?78:77;}};
struct ISteamUser {
 FixtureSteamId GetSteamID(){
  fixture->call("GetSteamID");++fixture->steamIds;
  if(fixture->fault==FixtureFault::GetSteamIdThrow)throw std::runtime_error("fixture-sdk-get-steamid-unknown");
  if(fixture->fault==FixtureFault::CurrentAfterReply)fixture->live=false;
  return {};
 }
};
inline ISteamUser fixtureApi;
struct ISteamClient {
 HSteamPipe CreateSteamPipe(){fixture->call("CreateSteamPipe");++fixture->creates;++fixture->pipeOwned;return 1;}
 HSteamUser ConnectToGlobalUser(HSteamPipe){fixture->call("ConnectToGlobalUser");++fixture->connects;++fixture->userOwned;return 2;}
 ISteamUser* GetISteamUser(HSteamUser,HSteamPipe,const char*){fixture->call("GetISteamUser");++fixture->userInterfaces;return &fixtureApi;}
 void ReleaseUser(HSteamPipe,HSteamUser){
  fixture->call("ReleaseUser");++fixture->releaseUserCalls;fixture->gate(FixtureBlock::ReleaseUser);
  if(fixture->fault==FixtureFault::ReleaseUserThrow)throw std::runtime_error("fixture-sdk-release-user-unknown");
  ++fixture->releaseUserSuccess;--fixture->userOwned;
 }
 bool BReleaseSteamPipe(HSteamPipe){
  fixture->call("BReleaseSteamPipe");++fixture->releasePipeCalls;fixture->gate(FixtureBlock::ReleasePipe);
  if(fixture->fault==FixtureFault::PipeFalse)return false;
  if(fixture->fault==FixtureFault::PipeThrow)throw 17; // non-std SDK exception must also stay contained
  ++fixture->releasePipeSuccess;--fixture->pipeOwned;return true;
 }
 bool BShutdownIfAllPipesClosed(){
  fixture->call("BShutdownIfAllPipesClosed");++fixture->shutdownCalls;
  if(fixture->fault==FixtureFault::ShutdownFalse)return false;
  if(fixture->pipeOwned.load()!=0||fixture->userOwned.load()!=0){fixture->portViolation=true;return false;}
  ++fixture->shutdownSuccess;fixture->clientOwned=0;return true;
 }
};
inline ISteamClient fixtureClient;
inline void* __cdecl portFactory(const char*,int*){
 fixture->call("CreateInterface");++fixture->factories;
 if(fixture->fault==FixtureFault::FactoryNull)return nullptr;
 fixture->clientOwned=1;return &fixtureClient;
}
// Defined before the actor-only macros: these call the actual local loader.
// GetProcAddress's CreateInterface result is explicitly the controlled SDK leaf.
inline HMODULE WINAPI portLoadLibrary(LPCWSTR path,HANDLE file,DWORD flags){
 fixture->call("LoadLibraryExW");++fixture->loads;
 HMODULE result=nullptr;
 if(fixture->realLoader){
  if(fixture->dllPath!=path){fixture->portViolation=true;SetLastError(ERROR_INVALID_NAME);return nullptr;}
  result=::LoadLibraryExW(path,file,flags);
 }else result=reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1));
 if(result){++fixture->moduleRefs;fixture->loadedModule=result;}
 return result;
}
inline FARPROC WINAPI portGetProcAddress(HMODULE module,LPCSTR name){
 fixture->call("GetProcAddress");++fixture->lookups;
 if(module!=fixture->loadedModule.load()||std::strcmp(name,"CreateInterface")!=0){fixture->portViolation=true;return nullptr;}
 if(fixture->realLoader&&::GetProcAddress(module,"QueryActorFixtureTag")==nullptr){fixture->portViolation=true;return nullptr;}
 if(fixture->fault==FixtureFault::FactoryMissing)return nullptr;
 return reinterpret_cast<FARPROC>(&portFactory);
}
inline BOOL WINAPI portFreeLibrary(HMODULE module){
 fixture->call("FreeLibrary");++fixture->freeCalls;
 if(fixture->userOwned.load()!=0||fixture->pipeOwned.load()!=0||fixture->clientOwned.load()!=0)fixture->prematureUnload=true;
 if(module!=fixture->loadedModule.load()||fixture->moduleRefs.load()!=1){fixture->portViolation=true;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
 if(fixture->fault==FixtureFault::FreeLibraryFalse){SetLastError(ERROR_BUSY);return FALSE;}
 const BOOL returned=fixture->realLoader?::FreeLibrary(module):TRUE;
 if(returned){++fixture->freeSuccess;--fixture->moduleRefs;}
 return returned;
}

