#if defined(INCLUDED_STEAM2_USERID_STRUCTS) || defined(STEAM_CALLBACK_INSPECTION_ENABLED)
#error Driver cleanup fixture requires the original Win64 SDK feature macro set
#endif
#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <string>
#include "native/steam_original_recovery_engine/running-publish-trial.h"

// Test-only memory/Win32 leaves. No real debug attachment, remote thread,
// remote allocation/write, SDK load, or machine identity lookup is permitted.
namespace driver_cleanup_fixture {
enum class Boundary { FirstDiagnostic, CleanupDiagnostic, Management, Unknown,
  CleanupDisarm, DetachDiagnostic, Idle, Retained, NormalDisarm, WaitFailed, LastGuard };
// Controlled C++ leaf fault only; Win32 DebugActiveProcessStop is not
// asserted to throw this POD (or any C++ exception).
struct NonStdCleanupUnwind {std::uint32_t tag;};
inline constexpr std::uint32_t nonStdCleanupTag=0x2584a11u;
static_assert(std::is_trivial_v<NonStdCleanupUnwind> && std::is_standard_layout_v<NonStdCleanupUnwind>);
struct Fault {
  unsigned site=0;std::size_t bytes=0;DWORD tid=0;USHORT frames=0;
  void* stack[32]{};unsigned sequence=0;
};
struct Context {
  Boundary boundary;HANDLE target=nullptr,entered=nullptr,fault=nullptr,hold=nullptr,done=nullptr;
  std::atomic<HANDLE> injected{nullptr},management{nullptr};
  std::atomic<unsigned> queryStarted{0},queryStopped{0},ownerEntries{0},waitFailures{0};
  std::atomic<unsigned> detach{0},confirm{0},control{0},read{0},write{0},freed{0};
  std::atomic<unsigned> unexpected{0},targetCloses{0},managementCloses{0};
  std::atomic<unsigned> sdkModules{0},sdkUsers{0},sdkPipes{0},sdkClients{0},sdkQualifications{0};
  std::atomic<bool> driverReturned{false},escaped{false},waitFailedInjected{false};
  std::atomic<unsigned> sequence{0};
  std::atomic<unsigned> guardEntries{0},nonStdThrows{0},escapedNonStd{0},escapedNonStdTag{0};
  std::atomic<DWORD> guardThread{0},detachFaultThread{0};
  std::atomic<unsigned> guardSequence{0},detachFaultSequence{0};
  running_trial::Packet wire{};char ordinaryRetained[8]{};
  Fault faults[8]{};unsigned faultCount=0;
  std::runtime_error primary{"fixture preconstructed native operation failure"};
  explicit Context(Boundary b):boundary(b){}
};
inline std::atomic<Context*> active{nullptr};
inline thread_local unsigned failSite=0;
inline thread_local bool inOwner=false;
inline Context& state() noexcept {auto* p=active.load(std::memory_order_acquire);if(!p)std::abort();return *p;}
inline void arm(unsigned site) noexcept {failSite=site;}
inline void allocation(std::size_t bytes) {
  const unsigned site=failSite;if(!site)return;failSite=0;
  auto& c=state();if(c.faultCount>=8)std::abort();auto& f=c.faults[c.faultCount++];
  f.site=site;f.bytes=bytes;f.tid=GetCurrentThreadId();
  f.sequence=++c.sequence;f.frames=RtlCaptureStackBackTrace(0,32,f.stack,nullptr);
  SetEvent(c.fault);throw std::bad_alloc();
}
inline void diagnostic(unsigned site) noexcept {
  const auto b=state().boundary;
  if((site==1&&(b==Boundary::FirstDiagnostic||b==Boundary::Management||b==Boundary::Unknown||b==Boundary::Idle||b==Boundary::Retained||b==Boundary::WaitFailed||b==Boundary::LastGuard))||
     (site==2&&b==Boundary::CleanupDiagnostic)||(site==3&&b==Boundary::CleanupDisarm)||
     (site==4&&b==Boundary::DetachDiagnostic)||(site==5&&b==Boundary::NormalDisarm))arm(site);
}
inline void lastGuardEntry() noexcept {
  auto& c=state();++c.guardEntries;c.guardThread=GetCurrentThreadId();c.guardSequence=++c.sequence;
}
inline bool normalDisarm() noexcept {return state().boundary==Boundary::NormalDisarm;}
inline bool enabled() noexcept {return active.load(std::memory_order_acquire)!=nullptr;}
inline void ownerEntry(HANDLE handle) noexcept {
  auto& c=state();if(handle!=c.injected.load())std::abort();inOwner=true;++c.ownerEntries;SetEvent(c.hold);
}
template<class Handle,class Memory,class Packet>
void inject(Handle& process,Memory& packetMemory,Memory& pathMemory,Packet& packet,
    Handle& managementThread,DWORD& pid,bool& attached,bool& initialBreakpointSeen,
    bool& preparedTrial,bool& controlUnknown,bool& loaded) {
  auto& c=state();HANDLE duplicate=nullptr;
  if(!::DuplicateHandle(::GetCurrentProcess(),c.target,::GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS))std::abort();
  process=Handle(duplicate);c.injected.store(duplicate);pid=0x7ffffffe;
  const auto b=c.boundary;
  attached=b==Boundary::FirstDiagnostic||b==Boundary::CleanupDiagnostic||b==Boundary::DetachDiagnostic||b==Boundary::WaitFailed||b==Boundary::LastGuard;
  initialBreakpointSeen=true;
  if(b==Boundary::Management){auto h=::CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!h)std::abort();managementThread=Handle(h);c.management.store(h);}
  controlUnknown=b==Boundary::Unknown;
  preparedTrial=b==Boundary::CleanupDisarm||b==Boundary::NormalDisarm;
  if(preparedTrial){packetMemory.process=duplicate;packetMemory.address=reinterpret_cast<std::uint64_t>(&c.wire);packet.status.size=sizeof(packet.status);packet.status.abiVersion=osd::kAbiVersion;packet.status.state=osd::State::Armed;}
  if(b==Boundary::Retained){loaded=true;packet.prepared.trampoline=reinterpret_cast<osd::Getter>(static_cast<std::uintptr_t>(0x45670000));pathMemory.process=duplicate;pathMemory.address=reinterpret_cast<std::uint64_t>(c.ordinaryRetained);}
  SetEvent(c.entered);
}
[[noreturn]] inline void failNative(){throw state().primary;}
inline BOOL unexpected() noexcept {++state().unexpected;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
}

// The JSON allocator reaches these real C++ allocation functions. The hook
// never replaces a JSON expression with throw. Captured stacks are fixed POD.
void* operator new(std::size_t n){driver_cleanup_fixture::allocation(n);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t& t) noexcept{return ::operator new(n,t);}
void operator delete(void* p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t a){driver_cleanup_fixture::allocation(n);if(auto* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a)))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept{_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept{_aligned_free(p);}

inline DWORD WINAPI fixtureWait(HANDLE handle,DWORD budget) {
  auto* p=driver_cleanup_fixture::active.load();
  if(p&&handle==p->injected.load()&&driver_cleanup_fixture::inOwner&&p->boundary==driver_cleanup_fixture::Boundary::WaitFailed&&!p->waitFailedInjected.exchange(true)){++p->waitFailures;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
  return ::WaitForSingleObject(handle,budget);
}
inline BOOL WINAPI fixtureClose(HANDLE handle){
  if(auto* p=driver_cleanup_fixture::active.load()){
    if(handle==p->injected.load())++p->targetCloses;
    if(handle==p->management.load())++p->managementCloses;
  }
  return ::CloseHandle(handle);
}
inline BOOL WINAPI fixtureDetach(DWORD pid){auto& c=driver_cleanup_fixture::state();if(pid!=0x7ffffffe)return driver_cleanup_fixture::unexpected();++c.detach;if(c.boundary==driver_cleanup_fixture::Boundary::LastGuard){
  ++c.nonStdThrows;c.detachFaultThread=GetCurrentThreadId();c.detachFaultSequence=++c.sequence;
  throw driver_cleanup_fixture::NonStdCleanupUnwind{driver_cleanup_fixture::nonStdCleanupTag};
}if(c.boundary==driver_cleanup_fixture::Boundary::CleanupDiagnostic||c.boundary==driver_cleanup_fixture::Boundary::WaitFailed){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return TRUE;}
inline BOOL WINAPI fixtureDebugger(HANDLE h,PBOOL present){auto& c=driver_cleanup_fixture::state();if(h!=c.injected.load()||!present)return driver_cleanup_fixture::unexpected();++c.confirm;*present=FALSE;return TRUE;}
inline BOOL WINAPI fixtureWrite(HANDLE h,LPVOID at,LPCVOID data,SIZE_T n,SIZE_T* written){auto& c=driver_cleanup_fixture::state();if(h!=c.injected.load()||at!=&c.wire||n!=sizeof(c.wire))return driver_cleanup_fixture::unexpected();++c.write;std::memcpy(&c.wire,data,n);if(written)*written=n;return TRUE;}
inline BOOL WINAPI fixtureRead(HANDLE h,LPCVOID at,LPVOID data,SIZE_T n,SIZE_T* got){auto& c=driver_cleanup_fixture::state();if(h!=c.injected.load()||at!=&c.wire||n!=sizeof(c.wire))return driver_cleanup_fixture::unexpected();++c.read;std::memcpy(data,&c.wire,n);if(got)*got=n;return TRUE;}
inline HANDLE WINAPI fixtureRemoteThread(HANDLE h,LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID at,DWORD,LPDWORD){
  auto& c=driver_cleanup_fixture::state();if(h!=c.injected.load()||at!=&c.wire||c.wire.op!=running_trial::Op::Disarm){driver_cleanup_fixture::unexpected();return nullptr;}
  ++c.control;c.wire.status.state=osd::State::Cleaned;c.wire.returnCode=0;
  auto event=::CreateEventW(nullptr,TRUE,TRUE,nullptr);c.management.store(event);return event;
}
inline BOOL WINAPI fixtureThreadExit(HANDLE h,LPDWORD code){auto& c=driver_cleanup_fixture::state();if(h!=c.management.load()||!code)return driver_cleanup_fixture::unexpected();*code=0;return TRUE;}
inline BOOL WINAPI fixtureFree(HANDLE h,LPVOID at,SIZE_T,DWORD){auto& c=driver_cleanup_fixture::state();if(h!=c.injected.load()||(at!=&c.wire&&at!=c.ordinaryRetained))return driver_cleanup_fixture::unexpected();++c.freed;return TRUE;}
inline BOOL WINAPI fixtureAttach(DWORD){return driver_cleanup_fixture::unexpected();}
inline BOOL WINAPI fixtureBreak(HANDLE){return driver_cleanup_fixture::unexpected();}
inline BOOL WINAPI fixtureDebugEvent(LPDEBUG_EVENT,DWORD){return driver_cleanup_fixture::unexpected();}
inline BOOL WINAPI fixtureContinue(DWORD,DWORD,DWORD){return driver_cleanup_fixture::unexpected();}
inline BOOL WINAPI fixtureGetContext(HANDLE,LPCONTEXT){return driver_cleanup_fixture::unexpected();}
inline BOOL WINAPI fixtureSetContext(HANDLE,const CONTEXT*){return driver_cleanup_fixture::unexpected();}
inline LPVOID WINAPI fixtureAlloc(HANDLE,LPVOID,SIZE_T,DWORD,DWORD){driver_cleanup_fixture::unexpected();return nullptr;}
inline BOOL WINAPI fixtureProtect(HANDLE,LPVOID,SIZE_T,DWORD,PDWORD){return driver_cleanup_fixture::unexpected();}
inline HANDLE WINAPI fixtureOpenProcess(DWORD,BOOL,DWORD){driver_cleanup_fixture::unexpected();return nullptr;}
inline HMODULE WINAPI fixtureLoad(LPCWSTR,HANDLE,DWORD){driver_cleanup_fixture::unexpected();return nullptr;}

// Adapted from the original query_actor_ports.hpp memory provider. These
// structural SDK objects are bound ONLY while compiling the full QueryActor
// class. Its entire original run(), cleanup, stopAndJoin and destructor remain
// unchanged. No production ISteamClient vtable or DLL is instantiated.
namespace driver_cleanup_fixture::QueryQualification {
enum class ModuleKind{Client};
struct Qualified {std::optional<int> layout{1};struct Match{bool matched=true;}record,publisher;std::string sha256;};
inline Qualified qualifyFile(const std::wstring&,ModuleKind){++state().sdkQualifications;return {};}
}
struct FixtureQuerySteamId {std::uint64_t ConvertToUint64()const noexcept{return 1;}};
struct FixtureQueryUser {FixtureQuerySteamId GetSteamID(){return {};}};
inline FixtureQueryUser fixtureQueryUser;
struct FixtureQueryClient {
  int CreateSteamPipe(){++driver_cleanup_fixture::state().sdkPipes;return 1;}
  int ConnectToGlobalUser(int){++driver_cleanup_fixture::state().sdkUsers;return 1;}
  FixtureQueryUser* GetISteamUser(int,int,const char*){return &fixtureQueryUser;}
  void ReleaseUser(int,int){--driver_cleanup_fixture::state().sdkUsers;}
  bool BReleaseSteamPipe(int){--driver_cleanup_fixture::state().sdkPipes;return true;}
  bool BShutdownIfAllPipesClosed(){auto& c=driver_cleanup_fixture::state();if(c.sdkUsers||c.sdkPipes){++c.unexpected;return false;}c.sdkClients=0;return true;}
};
inline FixtureQueryClient fixtureQueryClient;
inline void* __cdecl fixtureQueryFactory(const char*,int*){driver_cleanup_fixture::state().sdkClients=1;return &fixtureQueryClient;}
inline HMODULE WINAPI fixtureQueryLoad(LPCWSTR,HANDLE,DWORD){auto& c=driver_cleanup_fixture::state();++c.queryStarted;++c.sdkModules;return reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1));}
inline FARPROC WINAPI fixtureQueryProc(HMODULE module,LPCSTR name){if(module!=reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1))||std::strcmp(name,"CreateInterface")){driver_cleanup_fixture::unexpected();return nullptr;}return reinterpret_cast<FARPROC>(&fixtureQueryFactory);}
inline BOOL WINAPI fixtureQueryFree(HMODULE module){auto& c=driver_cleanup_fixture::state();if(module!=reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(1))||c.sdkModules!=1||c.sdkUsers||c.sdkPipes||c.sdkClients)return driver_cleanup_fixture::unexpected();--c.sdkModules;++c.queryStopped;return TRUE;}

#define WaitForSingleObject fixtureWait
#define CloseHandle fixtureClose
#define DebugActiveProcess fixtureAttach
#define DebugActiveProcessStop fixtureDetach
#define CheckRemoteDebuggerPresent fixtureDebugger
#define DebugBreakProcess fixtureBreak
#define WaitForDebugEvent fixtureDebugEvent
#define ContinueDebugEvent fixtureContinue
#define GetThreadContext fixtureGetContext
#define SetThreadContext fixtureSetContext
#define ReadProcessMemory fixtureRead
#define WriteProcessMemory fixtureWrite
#define CreateRemoteThread fixtureRemoteThread
#define GetExitCodeThread fixtureThreadExit
#define VirtualAllocEx fixtureAlloc
#define VirtualFreeEx fixtureFree
#define VirtualProtectEx fixtureProtect
#define OpenProcess fixtureOpenProcess
#define LoadLibraryExW fixtureLoad
