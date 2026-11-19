// INERT Win32 seam: no Windows headers, imports, file/device/process APIs.
// Synthetic HANDLE tokens are never dereferenced. All completions are scripted.
using BOOL = int;
constexpr BOOL TRUE=1, FALSE=0;
constexpr DWORD ERROR_SUCCESS=0, ERROR_IO_PENDING=997, ERROR_IO_INCOMPLETE=996;
constexpr DWORD ERROR_OPERATION_ABORTED=995, ERROR_WRITE_FAULT=29;
constexpr DWORD WAIT_OBJECT_0=0, WAIT_TIMEOUT=258;
struct OVERLAPPED { std::uintptr_t Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; };
namespace inert {
struct Plan { bool pendingStart=true, completed=false, aborted=false; DWORD firstWait=WAIT_TIMEOUT; };
static Plan plan;
static std::function<void()> resetEventHook;
static DWORD lastError=0;
static unsigned writes=0, queries=0, waits=0, cancels=0, creates=0, eventCloses=0, durableGuards=0;
static bool active=false;
static bool nextWriteImmediateSuccess=false, terminalZero=false;
static HANDLE oldHandle=nullptr, oldEvent=nullptr;
static OVERLAPPED* oldOv=nullptr;
static const BYTE* oldBuffer=nullptr;
static DWORD oldSize=0;
static std::vector<BYTE> bytes;
static std::set<HANDLE> events;
static std::uintptr_t nextEvent=900000;
static void require(bool value,const char* why) { if(!value)throw std::runtime_error(why); }
static void clear() {
 require(!active,"reset while inert I/O live");plan={};lastError=0;resetEventHook={};
 writes=queries=waits=cancels=creates=eventCloses=durableGuards=0;nextWriteImmediateSuccess=terminalZero=false;
 oldHandle=oldEvent=nullptr;oldOv=nullptr;oldBuffer=nullptr;oldSize=0;bytes.clear();
 require(events.empty(),"inert event leaked");
}
}
static bool g_executeActualWriter=false;
static MsiClawCommandOutcome g_lastActualOutcome;
static DWORD GetLastError() { return inert::lastError; }
static HANDLE CreateEventW(void*,BOOL,BOOL,void*) { ++inert::creates;const auto h=reinterpret_cast<HANDLE>(++inert::nextEvent);inert::events.insert(h);return h; }
static BOOL ResetEvent(HANDLE h) { inert::require(inert::events.count(h)==1,"ResetEvent wrong inert event");if(inert::resetEventHook)inert::resetEventHook();return TRUE; }
static BOOL CloseHandle(HANDLE h) {
 inert::require(!inert::active || (h!=inert::oldHandle && h!=inert::oldEvent),"close of live inert I/O resources");
 if(inert::events.erase(h)){++inert::eventCloses;return TRUE;}
 msiClawCloseModeCommand(h);return TRUE;
}
static BOOL WriteFile(HANDLE h,const BYTE* b,DWORD size,DWORD* synchronousCount,OVERLAPPED* ov) {
 inert::require(!inert::active,"overlapping inert WriteFile");
 inert::require(g_liveHandles.count(h)==1 && ov && inert::events.count(ov->hEvent)==1,"bad inert write owner/event");
 inert::require(synchronousCount==nullptr,"overlapped write used sync byte-count pointer");
 if(inert::nextWriteImmediateSuccess){inert::plan.pendingStart=false;inert::plan.completed=true;inert::plan.aborted=false;inert::terminalZero=false;inert::nextWriteImmediateSuccess=false;}
 ++inert::writes;inert::active=true;inert::oldHandle=h;inert::oldEvent=ov->hEvent;inert::oldOv=ov;inert::oldBuffer=b;inert::oldSize=size;inert::bytes.assign(b,b+size);
 inert::lastError=inert::plan.pendingStart?ERROR_IO_PENDING:ERROR_SUCCESS;
 return inert::plan.pendingStart?FALSE:TRUE;
}
static BOOL GetOverlappedResult(HANDLE h,OVERLAPPED* ov,DWORD* bytes,BOOL wait) {
 ++inert::queries;inert::require(wait==FALSE,"blocking completion query forbidden");
 inert::require(inert::active && h==inert::oldHandle && ov==inert::oldOv,"reap wrong inert I/O");
 inert::require(g_liveHandles.count(h)==1 && inert::events.count(inert::oldEvent)==1,"retired resource closed before completion");
 inert::require(ov->hEvent==inert::oldEvent && std::equal(inert::bytes.begin(),inert::bytes.end(),inert::oldBuffer),"live retired buffer/event changed");
 if(!inert::plan.completed && !inert::plan.aborted){*bytes=0;inert::lastError=ERROR_IO_INCOMPLETE;return FALSE;}
 inert::active=false;
 if(inert::plan.aborted){*bytes=0;inert::lastError=ERROR_OPERATION_ABORTED;return FALSE;}
 *bytes=inert::terminalZero?0:inert::oldSize;inert::lastError=ERROR_SUCCESS;return TRUE;
}
static DWORD WaitForSingleObject(HANDLE h,DWORD timeout) { inert::require(inert::events.count(h)==1 && (timeout==2000 || timeout==500),"wrong inert wait");++inert::waits;return inert::plan.firstWait; }
static BOOL CancelIoEx(HANDLE h,OVERLAPPED* ov) { inert::require(h==inert::oldHandle && ov==inert::oldOv,"cancel wrong inert owner");++inert::cancels;return TRUE; }
static bool msiCrashRecoveryBeforeModeWrite(BYTE,const MsiClawHidDiag&,bool) { ++inert::durableGuards;return true; }
constexpr DWORD WAIT_FAILED=0xffffffff;
// Legacy fake-IO suites provide one permanently healthy read-only public port.
// Sleep/revocation and real HANDLE return are tested by the Native power suite;
// this seam only preserves these older suites' original scripted IO boundaries.
#ifndef YMCC_LEGACY_MSI_INERT_PUBLIC_POWER
#define YMCC_LEGACY_MSI_INERT_PUBLIC_POWER
#include <memory>
namespace ymcc::sleeppower {inline bool powerAdmitted(bool value)noexcept{return value;}}
namespace ymcc::inputHost::sharedPower {
enum class Use{Input,Lifecycle};
class Scope final {
public:
    template<class Reader,class Mirrors>static std::shared_ptr<Scope> create(Reader,Mirrors&,bool,Use){return std::make_shared<Scope>();}
    bool current()const noexcept{return true;}
    bool power()const noexcept{return true;}
    HANDLE event()const noexcept{return reinterpret_cast<HANDLE>(std::uintptr_t(0x5f20));}
};
}
[[maybe_unused]] static int readSharedSystemPower(){return 0;}
[[maybe_unused]] static int g_sharedPowerRevocations=0;
[[maybe_unused]] static std::shared_ptr<ymcc::inputHost::sharedPower::Scope> t_inputHostRequestPower;
[[maybe_unused]] static bool t_inputHostRequestHardwareRequired=false;
struct InputHostRequestPowerScope {
    std::shared_ptr<ymcc::inputHost::sharedPower::Scope> previous;
    bool previousHardware;
    explicit InputHostRequestPowerScope(std::shared_ptr<ymcc::inputHost::sharedPower::Scope> scope,bool hardware=false)
        :previous(std::move(t_inputHostRequestPower)),previousHardware(t_inputHostRequestHardwareRequired){
        t_inputHostRequestPower=std::move(scope);t_inputHostRequestHardwareRequired=hardware;}
    ~InputHostRequestPowerScope(){t_inputHostRequestPower=std::move(previous);t_inputHostRequestHardwareRequired=previousHardware;}
};
[[maybe_unused]] static DWORD WaitForMultipleObjects(DWORD count,const HANDLE* handles,BOOL all,DWORD budget){
    if(count!=2||all||!handles||handles[1]!=reinterpret_cast<HANDLE>(std::uintptr_t(0x5f20)))throw std::logic_error("invalid-legacy-public-wait-set");
    return WaitForSingleObject(handles[0],budget);
}
#endif

// Read-only public transport ports for this legacy inert business fixture.
// SharedPowerV1 is the real Native48 declaration; this transport is controlled,
// and cannot prove real notification, cancellation or device-return behavior.
#ifndef YMCC_LEGACY_MSI_INERT_PUBLIC_POWER
#define YMCC_LEGACY_MSI_INERT_PUBLIC_POWER
#include <memory>
namespace ymcc::sleeppower {inline bool powerAdmitted(bool value)noexcept{return value;}}
namespace ymcc::inputHost::sharedPower {
class Scope final {public:bool current()const noexcept{return true;}bool power()const noexcept{return true;}};
}
[[maybe_unused]] static std::shared_ptr<ymcc::inputHost::sharedPower::Scope> t_inputHostRequestPower;
#endif
#ifndef YMCC_LEGACY_RETURN_OBSERVATION_CAPTURE
#define YMCC_LEGACY_RETURN_OBSERVATION_CAPTURE
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& output)noexcept{
    output={};output.generation=currentPowerGeneration();output.flags=ymcc::sleeppower::kKnown;
    if(hardwareWriteAllowed())output.flags|=ymcc::sleeppower::kHardwareAllowed;
    return ymcc::sleeppower::powerAdmitted(output);
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value)noexcept{
    return ymcc::sleeppower::powerAdmitted(value)&&value.generation==currentPowerGeneration()&&hardwareWriteAllowed();
}
#endif
