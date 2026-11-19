// Explicit variant of the original actual Steam UI native consumer.
// Steam process, power and shell are inert ports. Local Windows token queries
// and token closes are real; foreign identity is injected, not cross-session coverage.
static void commandNeed(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void commandReset(){fixture::reset();g_exitRequested=false;g_aiFanMockSession.isolated=false;fixturePowerGeneration=fixturePowerPublication=1;fixturePowerSerial=fixturePowerSequence=0;fixturePowerReady=fixturePowerKnown=true;fixturePowerPending=fixturePowerThrow=false;}
static json commandArgs(bool mode=false){json args={{"pid",uint32_t(123)},{"birth","456"},{"userInitiated",true}};if(mode)args["mode"]="bigpicture";return args;}
static bool commandRecovery(const std::string& mode,const std::function<bool()>& admit,const std::function<bool()>& owner){
    const ymcc::steamsession::Session session{LR"(X:\InertSteam)",123,42,456};
#if YMCC_COMMAND_SHARED
    return ymcc::steamui::dispatchRecoveryMode(session,mode,admit,owner);
#else
    (void)owner;return ymcc::steamui::dispatchRecoveryMode(session,mode,admit);
#endif
}
// Original fixed-command consumer; actual source lifetime with controlled
// existing ShellExecute/process leaves. No Observer worker or installed Steam.
template<class Action>static void commandWithSource(bool owned,Action&& action){
 ymcc::steamsession::SourceOwner source;
 if(owned){commandNeed(source.claim(),"actual isolated command source claim");source.resumeReads();}
 fixture::commandSource=&source;
 struct Reset {ymcc::steamsession::SourceOwner& source;~Reset(){fixture::commandSource=nullptr;source.suspendReads();source.drain();source.close();}} reset{source};
 action(source);
 source.suspendReads();source.drain();commandNeed(source.close(),"actual command source close after completed original request");
}
static void commandDrainAtShell(const char* handler,bool withMode){
 commandWithSource(true,[&](auto& source){
  HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),drainEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  struct Events {HANDLE a,b,c;~Events(){if(c)CloseHandle(c);if(b)CloseHandle(b);if(a)CloseHandle(a);}} events{entered,release,drainEntered};
  commandNeed(entered&&release&&drainEntered,"actual command source test events created");
  std::thread request,drain;
  struct Join {std::thread& request;std::thread& drain;HANDLE release;~Join(){SetEvent(release);if(request.joinable())request.join();if(drain.joinable())drain.join();}} join{request,drain,release};
  std::exception_ptr error;json result;std::atomic<bool> finished=false;
  fixture::onShell=[&]{SetEvent(entered);if(WaitForSingleObject(release,3000)!=WAIT_OBJECT_0)throw std::runtime_error("controlled original shell leaf release timeout");};
  request=std::thread([&]{try{result=handlers.at(handler)(commandArgs(withMode));}catch(...){error=std::current_exception();}});
  commandNeed(WaitForSingleObject(entered,1000)==WAIT_OBJECT_0,"actual original manual request entered controlled shell");
  source.suspendReads();
  drain=std::thread([&]{SetEvent(drainEntered);source.drain();finished.store(true);});
  commandNeed(WaitForSingleObject(drainEntered,1000)==WAIT_OBJECT_0,"actual source drain thread entered");
  Sleep(50);const bool prematurelyDrained=finished.load();
  SetEvent(release);request.join();drain.join();fixture::onShell={};
  if(error)std::rethrow_exception(error);
  commandNeed(finished.load()&&fixture::shells==1&&fixture::lookups==1&&fixture::closes==1,"original one shell submission and process release complete");
  commandNeed(result.value("ok",false),"accepted original shell submission remains accepted after source revocation");
  commandNeed(!prematurelyDrained,"actual source drainage must wait for original manual shell request return");
 });
}

// r258 logical Coordinator core only. Events and threads below belong solely
// to this original consumer fixture. No Native engine/Startup/Steam backend.
namespace command_intent_fixture {
using namespace ymcc::steamcommand;
static json boundaries=json::array();
static std::atomic<unsigned> opened{0},closed{0},closeFailed{0};
struct Event {
 HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Event(){commandNeed(value!=nullptr,"fixture manual event allocation");++opened;}
 ~Event(){if(value){if(CloseHandle(value))++closed;else ++closeFailed;}}
 Event(const Event&)=delete;
 void set()const noexcept{SetEvent(value);}
 void clear()const{commandNeed(ResetEvent(value)!=FALSE,"fixture clear before promote");}
 bool ready()const noexcept{return WaitForSingleObject(value,0)==WAIT_OBJECT_0;}
 bool wait()const noexcept{return WaitForSingleObject(value,2000)==WAIT_OBJECT_0;}
};
struct Signal final:AvailabilitySignal {
 Event wake;std::atomic<unsigned> calls{0};std::atomic<bool> callbackFailed{false};
 std::function<void()> callback,onDestroy;
 void notify()noexcept override {++calls;wake.set();try{if(callback)callback();}catch(...){callbackFailed=true;}}
 ~Signal(){try{if(onDestroy)onDestroy();}catch(...){}}
};
static Target target(){return {123,456,LR"(X:\InertSteam)"};}
static std::shared_ptr<Signal> signal(){return std::make_shared<Signal>();}
static void need(bool v,const char* text){commandNeed(v,text);}
template<class F>void boundary(const char* name,F&& action){action();boundaries.push_back({{"name",name},{"ok",true},{"scope","logical-core-only; fixture events; original C++ command sink where stated"}});}
static void run(){
 boundary("passive-observation-original-command",[]{
  auto s=signal();auto intent=prepareIntent(target(),s);need(bool(intent)&&intent->status()==IntentStatus::Observing&&!intent->ticket(),"passive intent has no ticket");
  commandWithSource(true,[&](auto&){auto r=handlers.at("steam.ui.stop")(commandArgs());need(r.value("ok",false)&&fixture::shells==1,"passive observe permits actual original command");});
  need(intent->status()==IntentStatus::Observing&&s->calls==0,"ordinary retirement does not register passive observer");intent->cancel();
 });
 boundary("release-before-promote-and-exact-target",[]{
  Coordinator c;auto s=signal();Target requested=target();auto i=c.prepareIntent(requested,s);auto t=c.reserve(target(),Plan::Single);need(bool(t),"initial parent");t.parentReturned();
  requested.pid++;requested.birth++;requested.root+=L"-successor";need(i->matches(target())&&!i->matches(requested),"prepared target immutable across caller mutation");
  s->wake.clear();need(i->promote()==IntentStatus::Ready&&i->ticket().target(target())&&s->calls==0,"release before registration gives immediate exact ticket");i->cancel();
 });
 boundary("release-after-promote-ready-slot-original-consumer",[]{
  auto* c=provider.load();need(c!=nullptr,"ordinary installed provider");auto s=signal();auto first=c->reserve(target(),Plan::Single);need(bool(first),"hold ordinary command slot");auto i=prepareIntent(target(),s);auto secondSignal=signal();auto other=prepareIntent(target(),secondSignal);
  s->wake.clear();need(i->promote()==IntentStatus::Waiting&&i->promote()==IntentStatus::Waiting,"one idempotent waiter");need(other->promote()==IntentStatus::Busy&&other->status()==IntentStatus::Observing&&!other->ticket(),"second Busy is not registered");
  std::weak_ptr<Intent> weak=i;std::atomic<bool> reentered{false},stolen{false};
  s->callback=[&]{auto held=weak.lock();reentered=held&&held->status()==IntentStatus::Ready;auto newcomer=c->reserve(target(),Plan::Single);stolen=bool(newcomer);if(newcomer)newcomer.parentReturned();};
  first.parentReturned();need(s->wake.ready()&&s->calls==1&&reentered&&!stolen&&!s->callbackFailed,"retirement atomically reserves before unlocked notify/status reentry");
  need(i->status()==IntentStatus::Ready&&i->ticket().held()&&!c->reserve(target(),Plan::Single),"new ordinary request cannot steal Ready");
  commandWithSource(true,[&](auto&){auto r=handlers.at("steam.ui.stop")(commandArgs());need(!r.value("ok",true)&&r.value("reason",std::string{})=="steam-command-busy"&&fixture::shells==1,"actual original independent sink rejects occupied Ready slot");});
  need(other->status()==IntentStatus::Observing&&secondSignal->calls==0,"second request received no promotion");s->callback={};i->cancel();other->cancel();
 });
 boundary("cancel-before-retirement",[]{
  Coordinator c;auto s=signal();auto t=c.reserve(target(),Plan::Single);auto i=c.prepareIntent(target(),s);need(i->promote()==IntentStatus::Waiting,"registered");i->cancel();i->cancel();need(s->calls==1&&i->status()==IntentStatus::Revoked&&!i->ticket(),"idempotent cancel wakes once");t.parentReturned();need(s->calls==1&&i->promote()==IntentStatus::Revoked,"release cannot resurrect canceled waiter");auto fresh=c.reserve(target(),Plan::Single);need(bool(fresh),"slot returned after cancel first");fresh.parentReturned();
 });
 boundary("cancel-after-retirement",[]{
  Coordinator c;auto s=signal();auto t=c.reserve(target(),Plan::Single);auto i=c.prepareIntent(target(),s);need(i->promote()==IntentStatus::Waiting,"registered");t.parentReturned();need(i->status()==IntentStatus::Ready&&s->calls==1,"promoted first");auto old=i->ticket();i->cancel();need(!i->ticket()&&!old.current()&&s->calls==2,"cancel promoted ticket");auto fresh=c.reserve(target(),Plan::Single);need(bool(fresh),"unentered promoted parent releases");fresh.parentReturned();
 });
 boundary("stop-never-revives",[]{
  Coordinator c;auto s=signal();auto t=c.reserve(target(),Plan::Single);auto i=c.prepareIntent(target(),s);need(i->promote()==IntentStatus::Waiting,"registered");std::weak_ptr<Intent> w=i;std::atomic<bool> reentered{false};s->callback=[&]{auto x=w.lock();reentered=x&&x->status()==IntentStatus::Revoked;};c.stop();need(s->calls==1&&reentered,"stop signal runs outside lock");t.parentReturned();need(i->promote()==IntentStatus::Revoked&&!i->ticket()&&!c.reserve(target(),Plan::Single)&&!c.prepareIntent(target(),s),"stopped state grants nothing");s->callback={};
 });
 for(const bool granted:{false,true})boundary(granted?"granted-debt-after-cancel":"entered-debt-after-cancel",[&]{
  Coordinator c;auto s=signal();auto a=c.prepareIntent(target(),s);need(a->promote()==IntentStatus::Ready,"active intent");auto t=a->ticket();need(granted?t.grant(Effect::Mutation,0):t.enter(Effect::Mutation,0),"entered or granted exact stage");auto next=c.prepareIntent(target(),signal());need(next->promote()==IntentStatus::Waiting,"next waits");a->cancel();need(next->status()==IntentStatus::Waiting&&!c.reserve(target(),Plan::Single),"cancel preserves delivered debt");need(!t.returned(1,true)&&next->status()==IntentStatus::Waiting,"wrong stage does not return debt");need(t.returned(0,true)&&next->status()==IntentStatus::Ready,"exact stage return promotes");next->cancel();
 });
 boundary("parent-return-required-after-stage",[]{
  Coordinator c;auto t=c.reserve(target(),Plan::Single);need(t.enter(Effect::Mutation,0),"enter");auto i=c.prepareIntent(target(),signal());need(i->promote()==IntentStatus::Waiting,"wait");need(t.returned(0,true)&&i->status()==IntentStatus::Waiting,"stage return alone retains parent");t.parentReturned();need(i->status()==IntentStatus::Ready,"exact parent return promotes");i->cancel();
 });
 boundary("child-generation-proof-after-cancel",[]{
  Coordinator c;auto a=c.prepareIntent(target(),signal());need(a->promote()==IntentStatus::Ready,"active");auto t=a->ticket();need(t.attachChild(71)&&t.grant(Effect::Mutation,0),"attach exact child plus grant");auto i=c.prepareIntent(target(),signal());need(i->promote()==IntentStatus::Waiting,"wait");a->cancel();t.childRetired(72,true);need(i->status()==IntentStatus::Waiting,"wrong child proof retains debt");t.childRetired(71,true);need(i->status()==IntentStatus::Ready,"exact destroyed child clears grant and promotes");i->cancel();
 });
 boundary("retained-debt-repair-completion",[]{
  Coordinator c;auto t=c.reserve(target(),Plan::Single);need(t.attachChild(91)&&t.grant(Effect::Mutation,0),"retained child grant");auto i=c.prepareIntent(target(),signal());need(i->promote()==IntentStatus::Waiting,"wait");bool repaired=false;unsigned repairs=0;t.childRetired(91,false,[&]{++repairs;return repaired;});t.parentReturned();need(i->status()==IntentStatus::Waiting,"retained cleanup debt blocks");
  const auto post=[](std::function<void()> run,std::function<void()> done){run();done();return true;};c.requestRetainedRepair(post);need(repairs==1&&i->status()==IntentStatus::Waiting,"failed repair does not promote");
  std::function<void()> heldRun,heldDone;repaired=true;c.requestRetainedRepair([&](auto run,auto done){heldRun=std::move(run);heldDone=std::move(done);return true;});need(i->status()==IntentStatus::Waiting,"queued repair is not completion");heldRun();need(repairs==2&&i->status()==IntentStatus::Waiting,"retained completion captures hold logical debt");heldRun={};need(i->status()==IntentStatus::Waiting,"done capture still retained");heldDone={};need(i->status()==IntentStatus::Ready,"exact final completion release promotes");i->cancel();
 });
 boundary("permanent-unproved-child-debt",[]{
  Coordinator c;auto t=c.reserve(target(),Plan::Single);need(t.attachChild(101),"child");auto i=c.prepareIntent(target(),signal());need(i->promote()==IntentStatus::Waiting,"wait");t.parentReturned();t.childRetired(101,false);c.requestRetainedRepair([](auto run,auto done){run();done();return true;});need(i->status()==IntentStatus::Waiting&&!c.reserve(target(),Plan::Single),"unproved debt cannot be timed out/refunded");t.childRetired(101,true);need(i->status()==IntentStatus::Ready,"exact later child proof permits promotion");i->cancel();
 });
 boundary("cross-thread-notify-strong-resource-final-release",[]{
  Coordinator c;Event entered,release;auto probe=c.prepareIntent(target(),signal());auto s=signal();auto i=c.prepareIntent(target(),s);auto t=c.reserve(target(),Plan::Single);need(i->promote()==IntentStatus::Waiting,"wait");std::weak_ptr<Signal> weak=s;std::atomic<bool> reentered{false},released{false},deadlineFailure{false};std::atomic<unsigned> ordinal{0};std::weak_ptr<Intent> wi=i;
  s->callback=[&]{{auto v=wi.lock();if(v)(void)v->status();}if(ordinal.fetch_add(1)==0){reentered=true;entered.set();if(!release.wait())deadlineFailure=true;}};
  s->onDestroy=[&]{released=probe->status()==IntentStatus::Observing;};std::thread worker([t]{t.parentReturned();});
  struct Join{std::thread& t;Event& e;~Join(){e.set();if(t.joinable())t.join();}}join{worker,release};
  need(entered.wait()&&reentered,"cross-thread notification reentered status");i->cancel();i.reset();s.reset();need(!weak.expired()&&!released,"notice retains signal after caller intent and signal owners gone");release.set();worker.join();need(weak.expired()&&released&&!deadlineFailure,"final signal capture/destructor released outside State lock");probe->cancel();
 });
 boundary("factory-only-ordinary-provider-no-implicit-TLS",[]{
  auto s=signal();auto* c=provider.load();need(c!=nullptr&&!active,"ordinary provider no TLS");auto i=prepareIntent(target(),s);need(bool(i)&&i->status()==IntentStatus::Observing,"free ordinary factory");auto t=c->reserve(target(),Plan::Single);need(bool(t),"active parent");{Use use(t);need(!prepareIntent(target(),s),"free factory rejects active TLS without borrowing");auto explicitIntent=c->prepareIntent(target(),s);need(bool(explicitIntent)&&explicitIntent->status()==IntentStatus::Observing,"explicit core factory remains passive");explicitIntent->cancel();}t.parentReturned();i->cancel();
  auto* removed=provider.exchange(nullptr);struct Restore{Coordinator* p;~Restore(){provider.store(p);}}restore{removed};need(!prepareIntent(target(),s),"no provider factory refuses");
 });
 need(opened==closed&&closeFailed==0,"all fixture owned event handles explicitly returned");
}
}


// This directed case uses the actual availability header and real Win32 calls.
// No native engine, Startup continuation or installed Steam effect is covered.
#if defined(CloseHandle) || defined(SetEvent) || defined(ResetEvent) || defined(CreateEventW) || defined(WaitForSingleObject)
#error Availability consumer must use real Win32 APIs, with no fixture substitution
#endif
#include "steam_command_availability.h"
namespace command_availability_fixture {
using namespace ymcc::steamcommand;
struct Counts {unsigned signalOpens=0,signalCloses=0,refusedCloses=0,eventOpens=0,eventCloses=0,threadOpens=0,threadJoins=0,threadCloses=0,closeFailures=0;};
static void need(bool value,const char* message){commandNeed(value,message);}
struct Event {
 Counts& counts;HANDLE handle;
 explicit Event(Counts& c):counts(c),handle(CreateEventW(nullptr,TRUE,FALSE,nullptr)){need(handle!=nullptr,"fixture gate CreateEvent");++counts.eventOpens;}
 ~Event(){if(handle)CloseHandle(handle);}
 Event(const Event&)=delete;
 void set()const{need(SetEvent(handle)!=FALSE,"fixture gate SetEvent");}
 void wait()const{need(WaitForSingleObject(handle,2000)==WAIT_OBJECT_0,"fixture bounded gate wait");}
 void close(){need(handle!=nullptr,"fixture event explicit close once");need(CloseHandle(handle)!=FALSE,"fixture gate CloseHandle");handle=nullptr;++counts.eventCloses;}
};
struct Thread {
 Counts& counts;std::function<void()> action;std::exception_ptr error;HANDLE handle=nullptr;
 static DWORD WINAPI entry(void* context)noexcept{auto& self=*static_cast<Thread*>(context);try{self.action();}catch(...){self.error=std::current_exception();}return 0;}
 Thread(Counts& c,std::function<void()> f):counts(c),action(std::move(f)){handle=CreateThread(nullptr,0,&entry,this,0,nullptr);need(handle!=nullptr,"fixture CreateThread");++counts.threadOpens;}
 Thread(const Thread&)=delete;
 ~Thread(){if(handle){WaitForSingleObject(handle,3000);CloseHandle(handle);}}
 void join(){need(handle!=nullptr,"explicit thread join once");need(WaitForSingleObject(handle,3000)==WAIT_OBJECT_0,"bounded fixture thread join");++counts.threadJoins;DWORD code=STILL_ACTIVE;need(GetExitCodeThread(handle,&code)!=FALSE&&code==0,"thread returned naturally");need(CloseHandle(handle)!=FALSE,"thread HANDLE explicit CloseHandle");handle=nullptr;++counts.threadCloses;if(error)std::rethrow_exception(error);}
};
// This is only the fixture's retained owner, not the product runtime Control.
// Its destructor is never counted as cleanup evidence.
struct Owner {
 Counts& counts;const std::shared_ptr<AvailabilityEvent> signal;
 explicit Owner(Counts& c):counts(c),signal(std::make_shared<AvailabilityEvent>()){}
 void open(){need(signal->open(),"actual AvailabilityEvent open");++counts.signalOpens;}
 void close(){need(signal->close(),"actual AvailabilityEvent explicit close or retry");++counts.signalCloses;}
};
static Target target(){return {123,456,LR"(X:\InertSteam)"};}
static void run(){
 Counts counts;json boundaries=json::array();const auto boundary=[&](const char* name,const std::function<void()>& f){f();boundaries.push_back({{"name",name},{"ok",true},{"scope","actual AvailabilityEvent + Coordinator logic; fixture owned native threads/gates; no Startup or engine"}});};
 const auto report=[&](bool ok){return json{{"ok",ok},{"selected","command-native-availability"},{"boundaries",boundaries},{"signalOpens",counts.signalOpens},{"signalExplicitCloses",counts.signalCloses},{"expectedProtectedCloseRefusals",counts.refusedCloses},{"fixtureEventsOpened",counts.eventOpens},{"fixtureEventsExplicitlyClosed",counts.eventCloses},{"threadsOpened",counts.threadOpens},{"threadsExplicitlyJoined",counts.threadJoins},{"threadHandlesExplicitlyClosed",counts.threadCloses},{"tryLockContentionProven",false},{"tryLockScope","source-only; false close is not assumed to prove mutex contention"},{"destructorIsCleanupEvidence",false},{"realWin32Apis",true},{"startupOrEngineCovered",false}};};
 try{
 boundary("new-object-before-open-no-handle",[&]{
  Owner owner(counts);auto s=owner.signal->status();need(!s.opened&&!s.closing&&owner.signal->event()==nullptr,"new allocated object owns no HANDLE");owner.signal->notify();need(!owner.signal->reset()&&owner.signal->status().signalError==ERROR_SUCCESS,"preopen notify/reset grant nothing");
  owner.open();const auto h=owner.signal->event();need(h!=nullptr&&owner.signal->open()&&owner.signal->event()==h,"open idempotent exact HANDLE");owner.close();need(owner.signal->event()==nullptr&&!owner.signal->status().opened&&owner.signal->status().closing,"explicit close retires same object");need(!owner.signal->open(),"closed object cannot reopen or replace");
 });
 boundary("manual-reset-repeated-wait-reset-before-promote",[&]{
  Coordinator coordinator;Owner owner(counts);owner.open();const auto h=owner.signal->event();need(WaitForSingleObject(h,0)==WAIT_TIMEOUT,"new manual event initially reset");owner.signal->notify();for(unsigned n=0;n<3;++n)need(WaitForSingleObject(h,0)==WAIT_OBJECT_0,"manual reset preserves repeated wait");
  auto intent=coordinator.prepareIntent(target(),owner.signal);auto heldParent=coordinator.reserve(target(),Plan::Single);need(bool(intent)&&bool(heldParent),"actual coordinator passive intent and held parent");need(owner.signal->reset()&&WaitForSingleObject(h,0)==WAIT_TIMEOUT,"worker resets before promotion");need(intent->promote()==IntentStatus::Waiting,"actual registration after reset");heldParent.parentReturned();need(WaitForSingleObject(h,0)==WAIT_OBJECT_0&&intent->status()==IntentStatus::Ready&&bool(intent->ticket()),"real Coordinator retirement SetEvent cannot lose wake");intent->cancel();owner.close();
 });
 boundary("cross-thread-promotion-real-event-waiter-joined-first",[&]{
  Coordinator coordinator;Owner owner(counts);owner.open();auto intent=coordinator.prepareIntent(target(),owner.signal);auto heldParent=coordinator.reserve(target(),Plan::Single);const auto h=owner.signal->event();need(owner.signal->reset()&&intent->promote()==IntentStatus::Waiting,"clear before registration");Event waiting(counts);std::atomic<DWORD> result{WAIT_FAILED};std::atomic<DWORD> callbackThread{0};
  Thread waiter(counts,[&]{waiting.set();result=WaitForSingleObject(h,2000);});waiting.wait();Thread releaser(counts,[&]{callbackThread=GetCurrentThreadId();heldParent.parentReturned();});releaser.join();waiter.join();
  need(result==WAIT_OBJECT_0&&callbackThread!=GetCurrentThreadId()&&intent->status()==IntentStatus::Ready&&intent->ticket().target(target()),"other thread actual retire notified borrowed manual event");need(WaitForSingleObject(h,0)==WAIT_OBJECT_0,"waiter did not auto reset event");intent->cancel();owner.close();waiting.close();
 });
 boundary("stop-wakes-hint-without-ticket",[&]{
  Coordinator coordinator;Owner owner(counts);owner.open();auto heldParent=coordinator.reserve(target(),Plan::Single);auto intent=coordinator.prepareIntent(target(),owner.signal);need(owner.signal->reset()&&intent->promote()==IntentStatus::Waiting,"stop waiter registration");const auto h=owner.signal->event();std::atomic<DWORD> result{WAIT_FAILED};Event waiting(counts);Thread waiter(counts,[&]{waiting.set();result=WaitForSingleObject(h,2000);});waiting.wait();coordinator.stop();waiter.join();
  need(result==WAIT_OBJECT_0&&intent->status()==IntentStatus::Revoked&&!intent->ticket()&&intent->promote()==IntentStatus::Revoked,"stop notification conveys no ticket");heldParent.parentReturned();need(!coordinator.reserve(target(),Plan::Single),"late parent release cannot revive stopped state");intent->cancel();owner.close();waiting.close();
 });
 boundary("protect-close-error6-retains-exact-owner-handle-retry",[&]{
  Owner owner(counts);owner.open();auto* exactObject=owner.signal.get();const HANDLE exactHandle=owner.signal->event();need(exactHandle!=nullptr,"saved borrowed exact handle before closing");
  Thread waiter(counts,[&]{need(WaitForSingleObject(exactHandle,0)==WAIT_TIMEOUT,"borrower returns before close");});waiter.join();
  need(SetHandleInformation(exactHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"real protect from CloseHandle");
  struct Unprotect {HANDLE handle;~Unprotect(){if(handle)SetHandleInformation(handle,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);}}unprotect{exactHandle};
  const bool refused=owner.signal->close();const auto state=owner.signal->status();DWORD flags=0;need(!refused&&state.closeError==ERROR_INVALID_HANDLE&&state.opened&&state.closing,"real protected CloseHandle refuses with error6 and retains opened/closing debt");++counts.refusedCloses;
  need(owner.signal.get()==exactObject&&GetHandleInformation(exactHandle,&flags)!=FALSE&&(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE)!=0,"same retained owner still holds accurate protected HANDLE");need(owner.signal->event()==nullptr&&!owner.signal->open(),"closing accessor hides borrow and open cannot replace");
  owner.signal->notify();need(!owner.signal->reset()&&WaitForSingleObject(exactHandle,0)==WAIT_TIMEOUT,"closing notify/reset does not touch retained event");
  need(SetHandleInformation(exactHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,0)!=FALSE,"fixture removes protection from same handle");unprotect.handle=nullptr;owner.close();need(owner.signal.get()==exactObject&&!owner.signal->status().opened&&owner.signal->status().closeError==ERROR_SUCCESS,"same object explicit retry closes exact resource");
  DWORD afterFlags=0;SetLastError(ERROR_SUCCESS);need(GetHandleInformation(exactHandle,&afterFlags)==FALSE&&GetLastError()==ERROR_INVALID_HANDLE,"real handle invalid after successful retry");owner.signal->notify();need(owner.signal->event()==nullptr&&owner.signal->status().signalError==ERROR_SUCCESS&&!owner.signal->open(),"late notify has no stale handle route");
 });
 boundary("bounded-close-notify-serialization",[&]{
  Owner owner(counts);owner.open();const auto h=owner.signal->event();Event start(counts);std::atomic<unsigned> attempts{0},refused{0},notifications{0};std::atomic<bool> closed{false};
  // There are deliberately no borrowed waiters in this close/notify race.
  Thread notifier1(counts,[&]{start.wait();for(unsigned n=0;n<1024;++n){owner.signal->notify();++notifications;SwitchToThread();}});
  Thread notifier2(counts,[&]{start.wait();for(unsigned n=0;n<1024;++n){owner.signal->notify();++notifications;SwitchToThread();}});
  Thread closer(counts,[&]{start.wait();for(unsigned n=0;n<1024&&!closed;++n){++attempts;if(owner.signal->close()){closed=true;break;}++refused;SwitchToThread();}});
  start.set();closer.join();notifier1.join();notifier2.join();if(!closed){++attempts;need(owner.signal->close(),"owner retry after all notifier threads joined");closed=true;}++counts.signalCloses;
  const auto state=owner.signal->status();need(closed&&notifications==2048&&state.closing&&!state.opened&&state.signalError==ERROR_SUCCESS&&state.closeError==ERROR_SUCCESS&&owner.signal->event()==nullptr,"actual API notification/close serialized without stale resource");DWORD flags=0;need(GetHandleInformation(h,&flags)==FALSE&&GetLastError()==ERROR_INVALID_HANDLE,"closed original HANDLE is not live after bounded race");owner.signal->notify();start.close();
  boundaries.push_back({{"name","serialization-observation"},{"ok",true},{"closeAttempts",attempts.load()},{"falseCloseResults",refused.load()},{"notifications",notifications.load()},{"borrowedWaitersAtClose",0},{"tryLockFailureCauseProven",false}});
 });
 need(counts.signalOpens==counts.signalCloses&&counts.refusedCloses==1,"all actual product signal handles explicitly returned including protected retry");need(counts.eventOpens==counts.eventCloses&&counts.threadOpens==counts.threadJoins&&counts.threadOpens==counts.threadCloses,"all fixture gate and thread handles explicitly returned");std::cerr<<report(true).dump()<<'\n';
 }catch(...){std::cerr<<report(false).dump()<<'\n';throw;}
}
}

static int runCommandOutlet(int argc,char** argv){
    using Case=std::pair<std::string,std::function<void()>>;
    const std::vector<Case> cases={
        {"command-native-availability",[]{command_availability_fixture::run();}},
        {"command-native-intent",[]{command_intent_fixture::run();}},
        {"command-reentrant-shared-outlet",[]{commandWithSource(true,[](auto& source){
            bool inside=false;json nested;
            fixture::onShell=[&]{if(inside)return;inside=true;nested=handlers.at("steam.ui.stop")(commandArgs());};
            const auto outer=handlers.at("steam.uiMode.set")(commandArgs(true));
            fixture::onShell={};
            commandNeed(outer.value("ok",false),"outer original explicit command submits");
            const auto stillCounted=source.read();commandNeed(bool(stillCounted)&&stillCounted.current(),"same original source remains active after both original calls");
            commandNeed(fixture::lookups==fixture::closes,"all original process leases returned");
            commandNeed(fixture::shells==1&&!nested.value("ok",false),"unified-command-egress-rejects-reentrant-second-submit");
            commandNeed(nested.value("reason",std::string{})=="steam-command-busy"&&!nested.value("commandSubmitted",true)&&fixture::lookups==1&&fixture::closes==1,"busy-is-typed-zero-second-process-verification");
        });}},

        {"command-source-set-drain",[]{commandDrainAtShell("steam.uiMode.set",true);}},
        {"command-source-stop-drain",[]{commandDrainAtShell("steam.ui.stop",false);}},
        {"command-source-show-drain",[]{commandDrainAtShell("steam.ui.showBigPicture",false);}},
        {"command-source-missing",[]{commandWithSource(false,[](auto&){
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"missing actual source rejects cached identity before process or shell");
        });}},
        {"command-source-lookup-revoke",[]{commandWithSource(true,[](auto& source){
            fixture::onLookup=[&]{source.suspendReads();};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));fixture::onLookup={};
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==0,"source revocation during original process lookup cannot dispatch");
        });}},
        {"command-source-unwind",[]{commandWithSource(true,[](auto&){
            fixture::onShell=[]{throw std::runtime_error("original controlled shell exception");};
            bool threw=false;try{handlers.at("steam.ui.stop")(commandArgs());}catch(const std::runtime_error& error){threw=std::string(error.what())=="original controlled shell exception";}fixture::onShell={};
            commandNeed(threw&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==1,"original exception preserves throw and returns process and source read");
            const auto later=handlers.at("steam.ui.stop")(commandArgs());commandNeed(later.value("ok",false)&&fixture::shells==2,"same-coordinator-next-explicit-command-after-exception");
        });}},
        {"command-source-healthy",[]{commandWithSource(true,[](auto&){
            const auto set=handlers.at("steam.uiMode.set")(commandArgs(true));
            const auto stop=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(set.value("ok",false)&&stop.value("ok",false)&&fixture::lookups==2&&fixture::closes==2&&fixture::shells==2,"actual source retains two distinct explicit original commands without replay");
        });}},


        {"set-identity-process-epoch",[]{
            fixture::onLookup=[]{++fixture::processEpoch;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==0,"same-valued process successor must revoke a held manual switch");
        }},
        {"stop-identity-account-epoch",[]{
            fixture::onLookup=[]{++fixture::accountEpoch;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::closes==1&&fixture::shells==0,"same-valued account successor must revoke held manual stop");
        }},
        {"set-identity-presence-revoked",[]{
            fixture::onLookup=[]{fixture::presence=-1;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::closes==1&&fixture::shells==0,"unpublished presence cannot retain a manual switch");
        }},
        {"show-identity-process-read",[]{
            fixture::onRead=[]{++fixture::processEpoch;};
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::reads==1&&fixture::lookups==0&&fixture::shells==0,"display cannot adopt same-valued successor after original mode read");
        }},
        {"show-identity-account-aba",[]{
            fixture::onRead=[]{fixture::accountEpoch+=2;};
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"restored account scalars cannot revive old display intent");
        }},
        {"set-identity-unknown",[]{
            fixture::identityKnown=false;
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"Unknown identity must not use separately cached Session values");
        }},
        {"stop-identity-stopped",[]{
            fixture::presence=0;
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"stopped immutable source refuses manual stop");
        }},
        {"manual-identity-account-zero",[]{
            fixture::account=0;
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(result.value("ok",false)&&fixture::shells==1&&fixture::closes==1,"manual mode retains account-neutral offline client semantics");
        }},
        {"manual-identity-equivalent-publication",[]{
            fixture::onLookup=[]{++fixture::revision;};
            const auto result=ymcc::steamui::set(g_steamSettingsObserver,commandArgs(true),[]{return true;});
            commandNeed(result.value("ok",false)&&fixture::shells==1&&fixture::closes==1&&fixture::identitySnapshots>0&&fixture::legacySnapshots==0&&fixture::presenceReads==0,"equivalent immutable pointer/revision retains one command without split source reads");
        }},
        {"set-identity-post-submit",[]{
            fixture::onShell=[]{++fixture::processEpoch;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&result.value("commandSubmitted",false)&&fixture::shells==1&&fixture::closes==1,"retired identity after shell submission preserves uncertainty and cannot grant successor admission");
        }},
        {"stop-identity-post-submit",[]{
            fixture::onShell=[]{++fixture::accountEpoch;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(result.value("ok",false)&&fixture::shells==1&&fixture::closes==1,"late stop epoch does not erase the original submission-only receipt or replay");
        }},
        {"stop-identity-fast-exit",[]{
            fixture::onShell=[]{fixture::presence=0;++fixture::processEpoch;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(result.value("ok",false)&&result.value("requestedState",std::string{})=="stopped"&&fixture::shells==1&&fixture::closes==1,"normal fast exit preserves one successful shell submission and no replay");
        }},
        {"stop-identity-post-submit-owner",[]{
            fixture::onShell=[]{++fixturePowerGeneration;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(result.value("ok",false)&&fixture::shells==1&&fixture::closes==1,"late power retirement does not erase an already accepted stop submission");
        }},
        {"show-late-exit",[]{
            fixture::onRead=[]{g_exitRequested=true;};
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::reads==1&&fixture::lookups==0&&fixture::shells==0,"late exit during mode read must not dispatch");
        }},
        {"show-late-mock",[]{
            fixture::onRead=[]{g_aiFanMockSession.isolated=true;};
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"late isolated owner during mode read must not dispatch");
        }},
        {"show-power-aba",[]{
            fixture::onRead=[]{fixturePowerReady=false;++fixturePowerGeneration;fixturePowerReady=true;};
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"new Ready cannot revive an old manual command");
        }},
        {"set-session-wait-exit",[]{
            fixture::onSnapshot=[]{if(fixture::lookups==1)g_exitRequested=true;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==0,"exit during final session wait must not dispatch");
        }},
        {"set-session-wait-power",[]{
            fixture::onSnapshot=[]{if(fixture::lookups==1)++fixturePowerGeneration;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::closes==1&&fixture::shells==0,"power change during final session wait must not dispatch");
        }},
        {"stop-session-wait-mock",[]{
            fixture::onSnapshot=[]{if(fixture::lookups==1)g_aiFanMockSession.isolated=true;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::closes==1&&fixture::shells==0,"isolated owner during final session wait must not dispatch");
        }},
        {"set-late-exit",[]{
            fixture::onLookup=[]{g_exitRequested=true;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==0,"late exit during process verification closes held handle without dispatch");
        }},
        {"stop-late-mock",[]{
            fixture::onLookup=[]{g_aiFanMockSession.isolated=true;};
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==0,"late isolated stop must release handle without dispatch");
        }},
        {"set-power-change-in-wait",[]{
            fixture::onWait=[]{++fixturePowerGeneration;};
            const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::closes==1&&fixture::shells==0,"power change in final process wait must be rechecked");
        }},
        {"stop-power-not-ready",[]{
            fixturePowerReady=false;const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&result.value("reason",std::string{})=="power-not-ready"&&fixture::lookups==0&&fixture::shells==0,"unknown or not Ready power cannot dispatch");
        }},
        {"show-publication-only-keeps-owner",[]{
            fixture::onRead=[]{++fixturePowerPublication;};const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(result.value("ok",false)&&fixture::shells==1&&fixture::closes==1,"diagnostic publication alone does not revoke the original shared power owner");
        }},
        {"show-pending-notification-revokes",[]{
            fixture::onRead=[]{fixturePowerPending=true;};const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::shells==0,"same-generation pending notification revokes manual command");
        }},
        {"show-sequence-revokes",[]{
            fixture::onRead=[]{++fixturePowerSequence;};const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::shells==0,"same-generation notification sequence revokes old command");
        }},
        {"show-suspend-serial-revokes",[]{
            fixture::onRead=[]{++fixturePowerSerial;};const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::shells==0,"same-generation suspend serial revokes old command");
        }},
        {"show-power-unknown-revokes",[]{
            fixture::onRead=[]{fixturePowerKnown=false;};const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::shells==0,"unknown shared power cannot authorize old command");
        }},
        {"show-current",[]{
            const auto result=handlers.at("steam.ui.showBigPicture")(commandArgs());
            commandNeed(result.value("ok",false)&&fixture::reads==1&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==1&&fixture::shown==SW_SHOWNORMAL,"current explicit show uses one verified displayed target");
        }},
        {"manual-desktop-display-intent",[]{
            auto args=commandArgs(true);args["mode"]="desktop";const auto result=handlers.at("steam.uiMode.set")(args);
            commandNeed(result.value("ok",false)&&result.value("requestedMode",std::string{})=="desktop"&&fixture::shells==1&&fixture::shown==SW_SHOWNORMAL&&fixture::closes==1,"manual desktop keeps original display intent and receipt");
        }},
        {"manual-stop-fixed-command",[]{
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(result.value("ok",false)&&result.value("requestedState",std::string{})=="stopped"&&fixture::shells==1&&fixture::shown==SW_HIDE&&fixture::closes==1,"manual stop uses the only fixed shutdown command");
        }},
        {"manual-shell-failure-no-replay",[]{
            fixture::accepted=false;const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&result.value("reason",std::string{})=="steam-exit-command-failed"&&fixture::shells==1&&fixture::closes==1,"shell failure has one submission and original failure reason");
        }},
        {"manual-session-changed",[]{
            fixture::onLookup=[]{fixture::current=false;};const auto result=handlers.at("steam.uiMode.set")(commandArgs(true));
            commandNeed(!result.value("ok",true)&&result.value("reason",std::string{})=="steam-session-changed"&&fixture::closes==1&&fixture::shells==0,"same PID with changed Steam birth cannot dispatch");
        }},
        {"recovery-empty-admit-rejected",[]{
            const std::function<bool()> missing;
            const bool result=commandRecovery("bigpicture",missing,[]{return true;});
            commandNeed(!result&&fixture::lookups==0&&fixture::shells==0,"empty recovery admission is never a manual command");
        }},
        {"recovery-owner-rejected-before-lookup",[]{
            fixture::owner=false;const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return fixture::owner;});
            commandNeed(!result&&fixture::admissions==0&&fixture::lookups==0&&fixture::shells==0,"pure owner rejection must not reserve recovery permission");
        }},
        {"recovery-missing-process",[]{
            fixture::found=false;const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==0&&fixture::admissions==0&&fixture::shells==0,"missing process cannot reserve or dispatch");
        }},
        {"recovery-wrong-birth",[]{
            fixture::birth=false;const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0,"wrong birth closes handle before admission");
        }},
        {"recovery-windows-user-mismatch",[]{
            fixture::tokenFault=fixture::TokenFault::DifferentUser;
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetTokenQueries>0&&fixture::sidComparisons>0,"different Windows user must close exact process without spending admission or invoking shell");
        }},
        {"recovery-windows-session-mismatch",[]{
            fixture::tokenFault=fixture::TokenFault::DifferentSession;
            const bool result=commandRecovery("desktop",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetTokenQueries>0,"different Windows session must reject desktop before spending admission or invoking shell");
        }},
        {"recovery-windows-token-unavailable",[]{
            fixture::tokenFault=fixture::TokenFault::Unavailable;
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetTokenOpenAttempts>0&&fixture::tokenOpens>0,"unknown target token must return local token and process without spending admission");
        }},
        {"recovery-windows-token-query-fails",[]{
            fixture::tokenFault=fixture::TokenFault::QueryFailure;
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetUserProbes>0&&fixture::targetUserFills>0,"failed target TokenUser fill must reject before admission and return both real tokens");
        }},
        {"recovery-windows-token-query-throws",[]{
            fixture::tokenFault=fixture::TokenFault::QueryThrow;
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetUserProbes>0&&fixture::targetUserFills>0,"target TokenUser fill exception must fail closed inside real owner helper and return tokens");
        }},
        {"recovery-admit-windows-user-revoked",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;fixture::tokenFault=fixture::TokenFault::DifferentUser;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==1&&fixture::shells==0&&fixture::targetTokenOpenAttempts>=2&&fixture::sidComparisons>=2,"Windows user change in admission must be rechecked without refund or shell submission");
        }},
        {"recovery-admit-windows-session-revoked",[]{
            const bool result=commandRecovery("desktop",[]{++fixture::admissions;fixture::tokenFault=fixture::TokenFault::DifferentSession;return true;},[]{return true;});
            commandNeed(!result&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==1&&fixture::shells==0&&fixture::targetTokenOpenAttempts>=2,"Windows session change in admission must reject final desktop dispatch with one admission");
        }},
        {"manual-stop-windows-user-mismatch",[]{
            fixture::tokenFault=fixture::TokenFault::DifferentUser;
            const auto result=handlers.at("steam.ui.stop")(commandArgs());
            commandNeed(!result.value("ok",true)&&fixture::lookups==1&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0&&fixture::targetTokenQueries>0&&fixture::sidComparisons>0,"manual fixed shutdown must reject foreign Windows user and return real tokens");
        }},
        {"recovery-dead-before-admit",[]{
            fixture::live=false;const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(!result&&fixture::closes==1&&fixture::admissions==0&&fixture::shells==0,"dead process must not spend final admission");
        }},
        {"recovery-admit-once-final",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;commandNeed(fixture::lookups==1&&fixture::closes==0&&fixture::shells==0,"admission only after holding exact process and before shell");return true;},[]{return fixture::owner;});
            commandNeed(result&&fixture::admissions==1&&fixture::lookups==1&&fixture::closes==1&&fixture::shells==1&&fixture::shown==SW_SHOWNORMAL,"one final recovery admission and displayed Big Picture dispatch");
        }},
        {"recovery-desktop-hidden",[]{
            const bool result=commandRecovery("desktop",[]{++fixture::admissions;return true;},[]{return true;});
            commandNeed(result&&fixture::admissions==1&&fixture::shells==1&&fixture::shown==SW_HIDE&&fixture::closes==1,"recovery desktop remains hidden");
        }},
        {"recovery-admit-denied",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return false;},[]{return true;});
            commandNeed(!result&&fixture::admissions==1&&fixture::shells==0&&fixture::closes==1,"denied final admission does not replay or leak");
        }},
        {"recovery-admit-revokes-owner",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;fixture::owner=false;return true;},[]{return fixture::owner;});
            commandNeed(!result&&fixture::admissions==1&&fixture::shells==0&&fixture::closes==1,"post-admit owner change blocks dispatch without refund or replay");
        }},
        {"recovery-admit-process-exits",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;fixture::live=false;return true;},[]{return true;});
            commandNeed(!result&&fixture::admissions==1&&fixture::shells==0&&fixture::closes==1,"post-admit process death blocks shell");
        }},
        {"recovery-admit-throws-closes-handle",[]{
            bool threw=false;try{commandRecovery("bigpicture",[]{++fixture::admissions;throw std::runtime_error("admission failure");return true;},[]{return true;});}catch(const std::runtime_error&){threw=true;}
            commandNeed(threw&&fixture::admissions==1&&fixture::shells==0&&fixture::closes==1,"admission exception retains original throw and releases process handle");
        }},
        {"recovery-pure-owner-throws",[]{
            const bool result=commandRecovery("bigpicture",[]{++fixture::admissions;return true;},[]{if(fixture::lookups)throw std::runtime_error("owner failure");return true;});
            commandNeed(!result&&fixture::admissions==0&&fixture::shells==0&&fixture::closes==1,"pure owner exception fails closed and releases handle");
        }},
        {"recovery-invalid-mode-before-lookup",[]{
            bool threw=false;try{commandRecovery("shutdown",[]{++fixture::admissions;return true;},[]{return true;});}catch(const std::runtime_error&){threw=true;}
            commandNeed(threw&&fixture::lookups==0&&fixture::shells==0&&fixture::admissions==0,"invalid recovery mode never reaches process or admission");
        }},
#if YMCC_COMMAND_SHARED
        {"unknown-typed-command-before-owner",[]{
            const ymcc::steamsession::Session session{LR"(X:\InertSteam)",123,42,456};int ownerCalls=0;
            const auto result=ymcc::steamui::dispatchClientCommand(session,static_cast<ymcc::steamui::ClientCommand>(99),false,[&]{++ownerCalls;return true;});
            commandNeed(!result.accepted&&!result.current&&ownerCalls==0&&fixture::lookups==0&&fixture::shells==0,"unknown command cannot obtain owner permission or execute");
        }},
        {"manual-missing-owner-fails-closed",[]{
            const auto result=ymcc::steamui::set(g_steamSettingsObserver,commandArgs(true));
            commandNeed(!result.value("ok",true)&&fixture::lookups==0&&fixture::shells==0,"omitted manual owner cannot grant a command");
        }},
#endif
        {"manual-malformed-before-lookup",[]{
            auto args=commandArgs(true);args["mode"]="shutdown";bool threw=false;try{handlers.at("steam.uiMode.set")(args);}catch(const std::runtime_error&){threw=true;}
            commandNeed(threw&&fixture::lookups==0&&fixture::shells==0,"manual command allowlist remains before process lookup");
        }}
    };
    std::string selected;
    if(argc!=1){if(argc!=3||std::string(argv[1])!="--case"||!argv[2][0])return 2;selected=argv[2];if(selected!="manual-identity"&&std::none_of(cases.begin(),cases.end(),[&](const auto& value){return value.first==selected;}))return 2;}
    installCommandHandlers();json results=json::array();bool failed=false;
    for(const auto& [name,run]:cases){if(!selected.empty()&&(selected=="manual-identity"?!(name.starts_with("manual-")||name.starts_with("set-")||name.starts_with("stop-")||name.starts_with("show-")):selected!=name))continue;commandReset();std::string error;
        try{run();}catch(const std::exception& value){error=value.what();failed=true;}
        // Check even after an assertion/port exception and on the final case;
        // reset also rejects debt rather than hiding it in the next case.
        try{fixture::tokensReturned();}catch(const std::exception& value){if(!error.empty())error+="; ";error+=value.what();failed=true;}
        results.push_back({{"name",name},{"ok",error.empty()},{"error",error},{"lookups",fixture::lookups},{"closes",fixture::closes},{"shells",fixture::shells},{"admissions",fixture::admissions},{"tokenOpens",fixture::tokenOpens},{"tokenCloses",fixture::tokenCloses},{"tokensOutstanding",fixture::tokens.size()},{"tokenCloseFailures",fixture::tokenCloseFailures},{"targetTokenOpenAttempts",fixture::targetTokenOpenAttempts},{"targetTokenQueries",fixture::targetTokenQueries},{"targetUserProbes",fixture::targetUserProbes},{"targetUserFills",fixture::targetUserFills},{"sidComparisons",fixture::sidComparisons}});if(failed)break;
    }
    std::cout<<json{{"ok",!failed},{"selected",selected},{"caseCount",results.size()},{"cases",results},{"actualProductionDefinitions",true},{"defaultGroupExecuted",false},{"sharedOutlet",YMCC_COMMAND_SHARED==1},{"realSteamActions",0},{"nativeIntentCore",command_intent_fixture::boundaries},{"fixtureEventsOpened",command_intent_fixture::opened.load()},{"fixtureEventsClosed",command_intent_fixture::closed.load()},{"fixtureEventCloseFailures",command_intent_fixture::closeFailed.load()}}.dump();
    return failed?23:0;
}
