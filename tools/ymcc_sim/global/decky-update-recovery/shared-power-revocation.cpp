// Original shared Native owner fragments, with only OS/device/module leaves inert.
// Cancellation uses real owned manual-reset events and real duplicate/close/wait APIs.
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
static std::atomic<unsigned> nativeEventsCreated{0},nativeEventsClosed{0},nativeCloseFailed{0},duplicatesCreated{0},duplicatesClosed{0};
static bool failEventCreate=false;
static HANDLE fixtureCreateEvent(LPSECURITY_ATTRIBUTES attributes,BOOL manualReset,BOOL initial,LPCWSTR name){
 if(!manualReset||initial||name)throw std::runtime_error("mirror event shape changed");
 if(failEventCreate){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
 const auto event=CreateEventW(attributes,manualReset,initial,name);if(event)++nativeEventsCreated;return event;
}
static BOOL fixtureCloseEvent(HANDLE event){const BOOL ok=CloseHandle(event);if(ok)++nativeEventsClosed;else ++nativeCloseFailed;return ok;}
// Wrap only the actual header's SetEvent terminal. Gate synchronization uses
// the real API declared above, so it never re-enters this wrapper.
static HANDLE signalEnteredGate=nullptr,signalReleaseGate=nullptr;
static std::atomic<bool> holdFirstNativeSignal{false};
static std::atomic<unsigned> nativeSignalCalls{0},signalGateTimeouts{0};
static BOOL fixtureSetEvent(HANDLE event){
 ++nativeSignalCalls;
 if(holdFirstNativeSignal.exchange(false,std::memory_order_acq_rel)){
  ::SetEvent(signalEnteredGate);
  if(WaitForSingleObject(signalReleaseGate,5000)!=WAIT_OBJECT_0)++signalGateTimeouts;
 }
 return ::SetEvent(event);
}
#define SetEvent fixtureSetEvent
#define CreateEventW fixtureCreateEvent
#define CloseHandle fixtureCloseEvent
#include "sleep_power_revocation.h"
#undef CreateEventW
#undef CloseHandle
#undef SetEvent
#include "sleep_power_dispatch.h"
#include "recovery_power_boundary.h"
#include "sleep_cancel_ready.h"
#include "sleep_power_remote_transport.h"
using json=nlohmann::json;
using ymcc::sleeppower::SharedPowerV1;
using ymcc::sleeppower::RevocationMirrors;
using ymcc::sleeppower::Dispatch;
static std::function<void(const char*,unsigned)> atomicReadHook;
static unsigned atomicReads=0;
template<class T>struct MemoryAtomic {
 std::atomic<T> value;
 const char* name;
 MemoryAtomic(T initial,const char* label):value(initial),name(label){}
 T load(std::memory_order order=std::memory_order_seq_cst)const{const auto out=value.load(order);if(atomicReadHook)atomicReadHook(name,++atomicReads);return out;}
 void store(T next,std::memory_order order=std::memory_order_seq_cst){value.store(next,order);}
 T fetch_add(T next,std::memory_order order=std::memory_order_seq_cst){return value.fetch_add(next,order);}
};
@@POWER_ENUM@@
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_exitRequested{false},g_hardwareWriteGate{true};
static std::atomic<ULONGLONG> g_focusLightSuspendProofAfter{0};
static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
static MemoryAtomic<unsigned long long> g_powerGeneration{7,"generation"};
static MemoryAtomic<std::uint64_t> g_sharedPowerPublicationRevision{3,"publication"},g_sharedPowerSuspendSerial{2,"suspend"};
static struct {bool isolated=false;}g_aiFanMockSession;
static std::unique_ptr<RevocationMirrors> mirrorStorage;
static std::unique_ptr<Dispatch> dispatchStorage;
static std::unique_ptr<ymcc::sleeppower::CancellationReady> cancellationReadyStorage;
static std::unique_ptr<ymcc::sleeppower::remote::NativeRemoteRuntimeGate> remoteGateStorage;
#define g_sharedPowerRevocations (*mirrorStorage)
#define g_sleepPowerDispatch (*dispatchStorage)
#define g_sgCancellationReady (*cancellationReadyStorage)
#define g_sharedNativeRemotePowerRuntimeGate (*remoteGateStorage)
@@DISPATCH_FACTORY@@
@@READER@@
@@CURRENT_ACCESSORS@@
static RevocationMirrors::Registration* beforeModule=nullptr;
static unsigned orderedModuleCalls=0;
static bool moduleSawCancellation=false;
static ULONGLONG moduleSawFocusCutoff=0;
namespace ymcc::batterycharge{static void notifySystemPower(){++orderedModuleCalls;moduleSawCancellation=beforeModule&&beforeModule->cancelled()&&WaitForSingleObject(beforeModule->event(),0)==WAIT_OBJECT_0;moduleSawFocusCutoff=g_focusLightSuspendProofAfter.load();}}
static void sgStaticScanCancel(const char*){}
static void msiDinputInvalidateFeedbackLease(){}
template<class... Args>static void traceLog(const char*,Args...){}
static ULONGLONG fixtureFocusProofTick(){return 500;}
#define GetTickCount64 fixtureFocusProofTick
@@CLOSE_GATE@@
#undef GetTickCount64
@@PHASE_HOOKS@@
@@EXIT_HOOKS@@
struct SyncDuplicate {
 HANDLE handle=nullptr;
 explicit SyncDuplicate(HANDLE original){if(!DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),&handle,SYNCHRONIZE,FALSE,0))throw std::runtime_error("private event duplicate failed");++duplicatesCreated;}
 ~SyncDuplicate(){if(handle){if(!CloseHandle(handle))std::terminate();++duplicatesClosed;}}
 SyncDuplicate(const SyncDuplicate&)=delete;
};
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static unsigned long long serial=1;
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static bool signaled(const RevocationMirrors::Registration& value){return value.event()&&WaitForSingleObject(value.event(),0)==WAIT_OBJECT_0;}
static void reset(){atomicReadHook={};atomicReads=0;beforeModule=nullptr;orderedModuleCalls=0;moduleSawCancellation=false;moduleSawFocusCutoff=0;failEventCreate=false;dispatchStorage.reset();mirrorStorage.reset();mirrorStorage=std::make_unique<RevocationMirrors>();dispatchStorage=configuredDispatch();cancellationReadyStorage=std::make_unique<ymcc::sleeppower::CancellationReady>();remoteGateStorage=std::make_unique<ymcc::sleeppower::remote::NativeRemoteRuntimeGate>();g_exitRequested=false;g_hardwareWriteGate=true;g_focusLightSuspendProofAfter=0;g_sgLatestSleepEntryFileTime=0;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration.store(7);g_sharedPowerPublicationRevision.store(3);g_sharedPowerSuspendSerial.store(2);g_aiFanMockSession.isolated=false;}
static SharedPowerV1 capture(){SharedPowerV1 value;require(captureSharedPowerLifecycle(value),"formal Native capture returns admitted current facts");return value;}
static RevocationMirrors::Registration watch(const SharedPowerV1& value){auto result=observeSharedPowerRevocation(value);require(bool(result)&&!result.cancelled()&&!signaled(result),"formal Native watch begins with a fresh unsignaled cancellation event");return result;}
static int __cdecl unknownReader(SharedPowerV1*,std::uint32_t){return 0;}
static int __cdecl throwsReader(SharedPowerV1*,std::uint32_t){throw std::runtime_error("controlled borrowed-reader failure");}
@@REMOTE_PROVIDER_INCLUDE@@
int main(int argc,char** argv){
 const std::string selected=argc==2?argv[1]:"";
 const auto run=[&](const std::string& name,const std::function<void()>& body){if(!selected.empty()&&selected!=name)return;reset();cases.push_back(name);body();};
@@REMOTE_PROVIDER_CASES@@
 run("capture-shape-and-denial",[]{
  using namespace ymcc::sleeppower;const auto valid=capture();require(sizeof(valid)==48&&valid.version==1,"SharedPowerV1 ABI remains48 bytes/version1");
  for(const std::string fault:{"size","version","generation","phase","known","hardware","pending"}){auto bad=valid;if(fault=="size")bad.size=0;if(fault=="version")bad.version=2;if(fault=="generation")bad.generation=0;if(fault=="phase")bad.phase=3;if(fault=="known")bad.flags&=~kKnown;if(fault=="hardware")bad.flags&=~kHardwareAllowed;if(fault=="pending")bad.flags|=kNotificationPending;require(!powerAdmitted(bad),"incomplete or revoked snapshot never grants shared admission");require(!powerCurrent(&readSharedSystemPower,bad),"bad captured ticket cannot become current through a later Ready");}
  SharedPowerV1 output=valid;require(!capturePower(nullptr,output)&&output.generation==0&&output.flags==0,"null Reader clears old output to UNKNOWN");require(!capturePower(&unknownReader,output)&&output.flags==0,"UNKNOWN Reader clears stale admission");require(!capturePower(&throwsReader,output)&&output.flags==0,"Reader exception remains UNKNOWN");
 });
 run("capture-watermarks",[]{const auto old=capture();g_sharedPowerPublicationRevision.fetch_add(1);require(sharedPowerLifecycleCurrent(old),"benign later publication retains the same revocation lineage");require(!ymcc::sleeppower::powerPublicationCurrent(&readSharedSystemPower,old),"exact-publication helper rejects changed metadata");g_sharedPowerPublicationRevision.store(old.publicationRevision-1);require(!sharedPowerLifecycleCurrent(old),"publication watermark cannot move backwards");g_sharedPowerPublicationRevision.store(old.publicationRevision);g_sharedPowerSuspendSerial.fetch_add(1);require(!sharedPowerLifecycleCurrent(old),"same-generation Ready cannot hide a revocation serial change");g_sharedPowerSuspendSerial.store(old.suspendSerial);g_powerGeneration.fetch_add(1);require(!sharedPowerLifecycleCurrent(old),"captured ticket never crosses native generation");});
 run("moving-reader-is-unknown",[]{SharedPowerV1 output;atomicReadHook=[](const char* name,unsigned){if(std::string(name)=="publication")g_sharedPowerPublicationRevision.fetch_add(1);};require(readSharedSystemPower(&output,sizeof(output))==0,"original Reader refuses three moving captures");atomicReadHook={};require(output.flags==0&&output.generation==0,"moving reader never returns the initial Ready-shaped default as known");});
 run("ingress-during-capture",[]{bool injected=false;atomicReadHook=[&](const char* name,unsigned){if(!injected&&std::string(name)=="publication"){injected=true;g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[](const auto&){return false;});}};SharedPowerV1 output;require(!captureSharedPowerLifecycle(output),"actual receipt interleaving cannot turn an old Ready read into admission");atomicReadHook={};require(injected&&g_sleepPowerDispatch.pendingSuspend()&&output.flags==0,"capture retry observes pending and leaves UNKNOWN output");});
 run("native-callback-before-forward",[]{const auto stamp=capture();auto registration=watch(stamp);bool forwarded=false;const auto power=g_powerGeneration.load();g_sleepPowerDispatch.callback(PBT_APMRESUMEAUTOMATIC,++serial,[&](const auto&){forwarded=true;require(registration.cancelled()&&signaled(registration),"callback ingress revokes before the nonblocking post");return true;});require(forwarded&&!sharedPowerLifecycleCurrent(stamp)&&g_powerGeneration.load()==power,"callback changes source watermark without inventing power generation");});
 run("native-window-before-consume",[]{auto registration=watch(capture());const auto receipt=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial);require(bool(receipt)&&registration.cancelled()&&signaled(registration),"direct window ingress revokes before its private consumers");g_sleepPowerDispatch.complete(receipt,++serial);});
 run("native-failed-post",[]{auto registration=watch(capture());require(!g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[](const auto&){return false;}),"original transport failure is retained");require(registration.cancelled()&&signaled(registration)&&g_sleepPowerDispatch.hasPending(),"failed post cannot lose synchronous revocation");});
 run("native-reentrant-window",[]{Dispatch::Receipt queued;g_sleepPowerDispatch.callback(PBT_APMRESUMEAUTOMATIC,++serial,[&](auto value){queued=value;return true;});auto active=g_sleepPowerDispatch.forwarded(queued.sequence,queued.type);auto registration=watch(capture());auto nested=g_sleepPowerDispatch.window(PBT_APMRESUMECRITICAL,++serial);require(!nested&&registration.cancelled()&&signaled(registration),"reentrant window revokes even when no consumer receipt is returned");g_sleepPowerDispatch.complete(active,++serial);auto pending=g_sleepPowerDispatch.pendingReplay();require(bool(pending),"original reentrant delivery debt stays available");});
 run("late-forwarded-ready",[]{Dispatch::Receipt receipt;g_sleepPowerDispatch.callback(PBT_APMRESUMEAUTOMATIC,++serial,[&](auto value){receipt=value;return true;});auto applied=g_sleepPowerDispatch.forwarded(receipt.sequence,receipt.type);g_sleepPowerDispatch.complete(applied,++serial);auto fresh=watch(capture());auto stale=g_sleepPowerDispatch.forwarded(receipt.sequence,receipt.type);require(!stale&&!fresh.cancelled()&&!signaled(fresh),"late forwarded Ready cannot revoke the fresh current registration");g_sleepPowerDispatch.complete(applied,++serial);require(!fresh.cancelled(),"duplicate old completion cannot revoke current registration");});
 run("query-cancel-same-generation",[]{const auto generation=g_powerGeneration.load();auto old=watch(capture());g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](auto){return false;});SharedPowerV1 rejected;require(!captureSharedPowerLifecycle(rejected)&&signaled(old),"query pending synchronously closes old lease admission");auto cancel=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(cancel,++serial);auto fresh=watch(capture());require(g_powerGeneration.load()==generation&&old.cancelled()&&signaled(old)&&!fresh.cancelled(),"same-generation Ready creates a fresh event and never revives old cancellation");});
 run("close-gate-order",[]{
  const auto old=capture();auto registration=watch(old);beforeModule=&registration;
  closeHardwareWriteGate("fixture");
  require(registration.cancelled()&&signaled(registration)&&orderedModuleCalls==1&&moduleSawCancellation,"close gate cancels before the first dependent module callback");
  require(!g_hardwareWriteGate,"original hardware gate remains closed");
  require(moduleSawFocusCutoff==fixtureFocusProofTick(),"gate close invalidates light proof before the first dependent module callback");
  g_hardwareWriteGate=true;
  const auto current=capture();
  require(current.generation==old.generation&&current.suspendSerial>old.suspendSerial&&!sharedPowerLifecycleCurrent(old)&&signaled(registration),"current Ready in the same generation does not revive the old ticket");
 });
 for(unsigned index=0;index<phaseHookFunctions.size();++index)run("nonready-site-"+std::to_string(index),[&]{auto old=watch(capture());phaseHookFunctions[index]();require(old.cancelled()&&signaled(old),"each original production NonReady store synchronously revokes old events");g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;auto fresh=watch(capture());require(old.cancelled()&&signaled(old)&&!fresh.cancelled(),"Ready never resets a cancelled prior phase event");});
 run("normal-exit-fence",[]{auto old=watch(capture());actualNormalExitFence();require(g_exitRequested&&old.cancelled()&&signaled(old),"successful normal exit CAS cancels before serial cleanup");SharedPowerV1 state;require(!captureSharedPowerLifecycle(state)&&!observeSharedPowerRevocation(state),"closed Native owner rejects new captures and registrations");});
 run("fallback-exit-fence",[]{auto old=watch(capture());actualFallbackExitFence();require(g_exitRequested&&old.cancelled()&&signaled(old),"successful fallback exit CAS cancels before module shutdown");});
 run("registration-gap-first-reader",[]{auto stamp=capture();unsigned reads=0;atomicReadHook=[&](const char* name,unsigned){if(std::string(name)=="publication"&&++reads==1)g_sharedPowerRevocations.revoke();};auto denied=observeSharedPowerRevocation(stamp);atomicReadHook={};require(!denied,"registration closes a revocation racing the first read even if Native facts return to the same values");});
 run("registration-gap-second-reader",[]{auto stamp=capture();unsigned reads=0;atomicReadHook=[&](const char* name,unsigned){if(std::string(name)=="publication"&&++reads==3)g_sharedPowerRevocations.revoke();};auto denied=observeSharedPowerRevocation(stamp);atomicReadHook={};require(!denied,"post-registration read runs outside registry lock and rejects a concurrent revocation");});
 run("event-create-failure",[]{const auto stamp=capture();failEventCreate=true;auto denied=observeSharedPowerRevocation(stamp);failEventCreate=false;require(!denied,"event allocation failure cannot grant a source lease");auto fresh=watch(stamp);require(!fresh.cancelled(),"later explicit registration can recover without reopening old cancellation");});
 run("reset-signals-exported-duplicate",[]{auto registration=watch(capture());SyncDuplicate child(registration.event());require(!SetEvent(child.handle)&&GetLastError()==ERROR_ACCESS_DENIED,"exported duplicate has synchronization permission only");registration.reset();require(WaitForSingleObject(child.handle,0)==WAIT_OBJECT_0,"registration signals exported waiters before closing its original event");});
 run("local-cancel-keeps-borrowers",[]{const auto stamp=capture();auto cancelled=watch(stamp),unrelated=watch(stamp);SyncDuplicate child(cancelled.event());const auto original=cancelled.event();const auto closed=nativeEventsClosed.load();cancelled.cancel();cancelled.cancel();require(cancelled&&cancelled.event()==original&&nativeEventsClosed==closed,"local cancel keeps registration and native handle alive through child cleanup");require(cancelled.cancelled()&&signaled(cancelled)&&WaitForSingleObject(child.handle,0)==WAIT_OBJECT_0,"local cancel sticks and wakes the actual synchronization duplicate");require(!unrelated.cancelled()&&!signaled(unrelated)&&sharedPowerLifecycleCurrent(stamp),"local cancellation cannot alter another registration or native power facts");auto fresh=watch(stamp);require(!fresh.cancelled()&&cancelled.cancelled(),"an explicit fresh registration never revives a locally cancelled event");});
 run("cancel-completes-native-signal-race",[]{
  auto registration=watch(capture());SyncDuplicate child(registration.event());
  const auto original=registration.event();const auto closedBefore=nativeEventsClosed.load();
  signalEnteredGate=::CreateEventW(nullptr,TRUE,FALSE,nullptr);signalReleaseGate=::CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!signalEnteredGate||!signalReleaseGate){if(signalEnteredGate)::CloseHandle(signalEnteredGate);if(signalReleaseGate)::CloseHandle(signalReleaseGate);signalEnteredGate=signalReleaseGate=nullptr;require(false,"private race gates were not created");return;}
  const auto signalsBefore=nativeSignalCalls.load();holdFirstNativeSignal.store(true,std::memory_order_release);
  std::thread nativeRevoker([]{g_sharedPowerRevocations.revoke();});
  const auto reached=WaitForSingleObject(signalEnteredGate,5000);
  const bool initiallyUnsignaled=WaitForSingleObject(child.handle,0)==WAIT_TIMEOUT;
  const bool cancellationMarked=registration.cancelled();
  registration.cancel();
  const bool signaledAtCancelReturn=WaitForSingleObject(child.handle,0)==WAIT_OBJECT_0;
  const bool handleRetained=registration&&registration.event()==original&&nativeEventsClosed.load()==closedBefore;
  const auto callsAtCancelReturn=nativeSignalCalls.load()-signalsBefore;
  // Always unblock and join before reporting any failure or closing gates.
  const BOOL released=::SetEvent(signalReleaseGate);nativeRevoker.join();holdFirstNativeSignal=false;
  const BOOL enteredClosed=::CloseHandle(signalEnteredGate),releaseClosed=::CloseHandle(signalReleaseGate);signalEnteredGate=signalReleaseGate=nullptr;
  require(reached==WAIT_OBJECT_0&&released&&enteredClosed&&releaseClosed&&signalGateTimeouts.load()==0,"owned Native thread reaches terminal gate and returns all synchronization gates");
  require(initiallyUnsignaled&&cancellationMarked,"actual Native revoker is paused after sticky flag but before the real event signal");
  require(signaledAtCancelReturn&&callsAtCancelReturn==2,"cancel return synchronously signals its borrowed child event despite an earlier paused Native revoker");
  require(handleRetained&&signaled(registration),"cancel keeps its registration and HANDLE alive until the original child returns");
 });
 run("move-and-middle-unregister",[]{auto stamp=capture();auto first=watch(stamp),middle=watch(stamp),last=watch(stamp);SyncDuplicate oldMiddle(middle.event());middle.reset();require(WaitForSingleObject(oldMiddle.handle,0)==WAIT_OBJECT_0&&!first.cancelled()&&!last.cancelled(),"middle unlink cancels only its own event");RevocationMirrors::Registration moved=std::move(first);require(!first&&moved&&!moved.cancelled(),"move keeps the same event without signalling it");SyncDuplicate previousLast(last.event());last=std::move(moved);require(!moved&&last&&!last.cancelled()&&WaitForSingleObject(previousLast.handle,0)==WAIT_OBJECT_0,"move assignment retires the old destination before taking the new registration");g_sharedPowerRevocations.revoke();require(last.cancelled()&&signaled(last),"moved registration remains linked to Native revocation");});
 run("registration-outlives-owner",[]{auto stamp=capture();auto owner=std::make_unique<RevocationMirrors>();auto registration=owner->observe(&readSharedSystemPower,stamp);require(bool(registration),"private owner admits a same-process captured Native ticket");SyncDuplicate duplicate(registration.event());owner.reset();require(registration.cancelled()&&WaitForSingleObject(duplicate.handle,0)==WAIT_OBJECT_0,"owner destruction cancels outstanding registration while its state stays alive");registration.reset();});
 run("concurrent-register-revoke",[]{const auto stamp=capture();std::atomic<unsigned> accepted{0};std::thread creator([&]{for(unsigned i=0;i<200;++i){auto value=observeSharedPowerRevocation(stamp);if(value)++accepted;}});std::thread revoker([&]{for(unsigned i=0;i<200;++i)g_sharedPowerRevocations.revoke();});creator.join();revoker.join();auto last=watch(capture());g_sharedPowerRevocations.revoke();require(last.cancelled()&&signaled(last),"concurrent registration/unlink/revocation leaves a usable owner and no lost final cancellation");});
 run("twenty-original-dispatch-cycles",[]{const auto generation=g_powerGeneration.load();for(unsigned i=0;i<20;++i){auto old=watch(capture());auto receipt=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,++serial);require(signaled(old),"every actual received cycle cancels its old event");g_sleepPowerDispatch.complete(receipt,++serial);auto fresh=watch(capture());require(!fresh.cancelled()&&old.cancelled(),"new explicit registration remains usable after old cancellation");}require(g_powerGeneration.load()==generation,"mirror cycles never own or advance Native power generation");});
 if(cases.empty()){std::cerr<<"UNKNOWN_SHARED_POWER_REVOCATION_CASE\n";return 2;}
 dispatchStorage.reset();mirrorStorage.reset();require(nativeEventsCreated==nativeEventsClosed&&nativeCloseFailed==0,"every owned Native event is closed exactly once");require(duplicatesCreated==duplicatesClosed,"every exported synchronization duplicate is returned");
 std::cout<<json{{"cases",cases},{"assertions",assertions},{"failures",failures},{"nativeEventsCreated",nativeEventsCreated.load()},{"nativeEventsClosed",nativeEventsClosed.load()},{"duplicateHandlesCreated",duplicatesCreated.load()},{"duplicateHandlesClosed",duplicatesClosed.load()},{"actualNativeReaderAndCurrent",true},{"actualDispatchIngress",true},{"actualNativeWriteHookPairs",true},{"windowsPrivateEventsReal",true},{"osSystemSleepOrDeviceActions",false}}.dump(2)<<'\n';return failures.empty()?0:1;
}
