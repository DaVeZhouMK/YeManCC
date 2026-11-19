// Controlled source/window context around exact production tick/observe and UI delivery.
// No controller, settings file, virtual target, or output writer is touched.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using json=nlohmann::json;
static HWND g_hwnd=nullptr;
static DWORD uiThread=0;
static std::atomic<bool> g_exitRequested{false},g_inputCaptureStop{false},g_inputReady{true};
static std::atomic<std::uint64_t> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
enum class PowerLifecycle{Ready};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{1};
#if defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE)
/*PRODUCTION_CURRENT_POWER*/
#else
static std::uint64_t currentPowerGeneration(){return g_powerGeneration.load(std::memory_order_acquire);}
#endif
namespace ymcc{static constexpr std::uint64_t kVirtualOutputSourceWindowMaxAgeMs=2000;namespace touchhaptic{enum class Backend{XInput,Msi};}}
struct FeedbackSource{ymcc::touchhaptic::Backend backend=ymcc::touchhaptic::Backend::XInput;std::uint64_t generation=1;};
struct Pad{short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;unsigned char bLeftTrigger=0,bRightTrigger=0;};
struct InputCaptureGamepadSnapshot{bool live=true,readContextStable=true;std::shared_ptr<const std::string> axisCalibrationDeviceKey=std::make_shared<const std::string>("controlled-physical-source");std::uint64_t readAttemptSequence=1,readStartedTick=0,readCompletedTick=0,readPowerGeneration=1,readTopologyGeneration=1,readIdentityGeneration=1,readMsiSourceRevision=1,readMsiModeGeneration=1,readContinuityGeneration=1;FeedbackSource feedbackSource;Pad pad;};
static bool foreground=true;
static bool gamepadUiInputEligible(){return foreground;}
static std::vector<json> delivered;
static int wrongApartment=0;
#if defined(SERIAL_OWNER_FIXTURE) || defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE) || defined(SERIAL_MEDIA_FIXTURE)
static void fixtureMayThrowEmit();
#endif
static void ipc_emit(const std::string& event,const json& data={}){
#if defined(SERIAL_OWNER_FIXTURE) || defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE) || defined(SERIAL_MEDIA_FIXTURE)
fixtureMayThrowEmit();
#endif
if(GetCurrentThreadId()!=uiThread){++wrongApartment;return;}delivered.push_back({{"event",event},{"data",data}});}
static void ipc_emit_owned_gyro_telemetry(std::string event,json data){ipc_emit(event,data);}
struct UiCostSerialScope{explicit UiCostSerialScope(const char*){}};
#ifdef SERIAL_CONFIG_FIXTURE
static std::vector<json> fixtureLifecycleLogs;
static void appendNativeLifecycleLog(const char* event,const json& data){fixtureLifecycleLogs.push_back({{"event",event},{"data",data}});}
#else
static void appendNativeLifecycleLog(const char*,const json&){}
#endif

#if defined(SERIAL_OWNER_FIXTURE) || defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE) || defined(SERIAL_MEDIA_FIXTURE)
static std::mutex eventAllocationMx;
static std::vector<void*> eventAllocations;
static unsigned allocatedEvents=0,deletedEvents=0,fixtureRescueCount=0,workerThreadsJoined=0;
static bool failPost=false,exitAfterAllocate=false,throwEmit=false;
static unsigned successfulPosts=0;
static std::vector<LPARAM> postedValues;
static void* fixtureEventNew(std::size_t bytes){
 void* p=::operator new(bytes);std::lock_guard lock(eventAllocationMx);eventAllocations.push_back(p);++allocatedEvents;return p;
}
static void fixtureEventDelete(void* p) noexcept {
 {std::lock_guard lock(eventAllocationMx);const auto it=std::find(eventAllocations.begin(),eventAllocations.end(),p);if(it!=eventAllocations.end()){eventAllocations.erase(it);++deletedEvents;}}
 ::operator delete(p);
}
static std::size_t retainedEvents(){std::lock_guard lock(eventAllocationMx);return eventAllocations.size();}
static void fixtureMayThrowEmit(){if(throwEmit){throwEmit=false;throw std::runtime_error("controlled-ui-emit-failure");}}
static void fixtureSerialAfterAllocate();
static BOOL WINAPI fixturePostMessageW(HWND h,UINT m,WPARAM w,LPARAM l){
 if(failPost)return FALSE;
 const BOOL result=::PostMessageW(h,m,w,l);if(result){++successfulPosts;postedValues.push_back(l);}return result;
}
#define PostMessageW fixturePostMessageW
#endif

/*PRODUCTION_SERIAL_TYPES_AND_POST*/
/*PRODUCTION_MEDIA_CALLBACKS*/
#if defined(SERIAL_OWNER_FIXTURE) || defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE) || defined(SERIAL_MEDIA_FIXTURE)
#undef PostMessageW
static void fixtureSerialAfterAllocate(){
 if(exitAfterAllocate){exitAfterAllocate=false;g_exitRequested.store(true,std::memory_order_release);/*PRODUCTION_SERIAL_RETIRE*/}
}
static void fixtureRescueOldPayloads(){
 std::vector<void*> pending;{std::lock_guard lock(eventAllocationMx);pending=eventAllocations;}
 for(void* p:pending){++fixtureRescueCount;delete static_cast<GamepadSerialEvent*>(p);}
}
#endif

#if defined(SERIAL_POWER_FIXTURE) || defined(SERIAL_CONFIG_FIXTURE)
/*PRODUCTION_SERIAL_JOB_KIND*/
// Explicit memory serial-job port. Runs on the fixture caller, never starts the
// product serial worker. Only enum/helper/TLS/currentPower and HWND consumer are
// production slices; actual worker ordering/lifecycle is the parent task's proof.
struct FixtureSerialJob{SerialJobKind kind;std::function<void()> call;};
static std::deque<FixtureSerialJob> fixtureSerialJobs;
static unsigned fixtureSerialJobsRun=0;
static bool gamepadSerialSubmit(std::function<void()> job,SerialJobKind kind=SerialJobKind::YmccOwn){
 if(!job||g_exitRequested.load(std::memory_order_acquire))return false;
 fixtureSerialJobs.push_back({kind,std::move(job)});return true;
}
/*PRODUCTION_CURRENT_POWER_SUBMIT*/
static bool fixtureSubmitAction(std::function<void()> job,SerialJobKind kind=SerialJobKind::YmccOwn){
#ifdef FIXTURE_SERIAL_ACTION_POWER_PRESENT
 return gamepadSerialSubmitCurrentPower(std::move(job),kind);
#else
 // Raw before counterexample: no replacement helper, TLS or corrected metadata.
 return gamepadSerialSubmit(std::move(job),kind);
#endif
}
static void fixtureRunNextSerialJob(){
 if(fixtureSerialJobs.empty())throw std::runtime_error("fixture memory port empty");
 auto job=std::move(fixtureSerialJobs.front());fixtureSerialJobs.pop_front();++fixtureSerialJobsRun;job.call();
}
static bool fixturePendingPower(unsigned long long value,std::size_t count){
 std::lock_guard lock(eventAllocationMx);if(eventAllocations.size()!=count)return false;
#ifdef FIXTURE_SERIAL_ACTION_POWER_PRESENT
 for(const void* p:eventAllocations)if(static_cast<const GamepadSerialEvent*>(p)->actionPower!=value)return false;
 return true;
#else
 (void)value;return false; // Report missing real metadata; do not add a before field.
#endif
}
#endif

#ifdef SERIAL_CONFIG_FIXTURE
#ifdef FIXTURE_SERIAL_ACTION_CONFIG_PRESENT
using FixtureConfigurationWrite=GamepadSerialEvent::ConfigurationWrite;
static auto& fixtureConfigurationRevision=GamepadSerialEvent::ConfigurationRevision;
static auto& fixtureConfigurationWriters=GamepadSerialEvent::ConfigurationWriters;
static auto& fixtureRulesRequired=GamepadSerialEvent::RulesRequired;
static auto& fixtureRulesApplied=GamepadSerialEvent::RulesApplied;
static void fixtureRulesPublished(){GamepadSerialEvent::rulesPublished();}
#else
// Raw before stimulus ONLY: none of these fixture data counters is read by the
// old actual struct/helper/Post/case. Never implement a replacement correct guard.
static std::atomic<unsigned long long> fixtureConfigurationRevision{1},fixtureConfigurationWriters{0},fixtureRulesRequired{0},fixtureRulesApplied{0};
struct FixtureConfigurationWrite{bool active;explicit FixtureConfigurationWrite(bool value):active(value){if(active){++fixtureConfigurationWriters;++fixtureConfigurationRevision;}}~FixtureConfigurationWrite(){if(active){++fixtureConfigurationRevision;--fixtureConfigurationWriters;}}};
static void fixtureRulesPublished(){++fixtureRulesRequired;++fixtureConfigurationRevision;}
#endif
static void fixtureOffOn(){ {FixtureConfigurationWrite off(true);} {FixtureConfigurationWrite on(true);} }
static void fixtureApplyRules(){fixtureRulesApplied.store(fixtureRulesRequired.load(std::memory_order_acquire),std::memory_order_release);}
static bool fixturePendingConfiguration(unsigned long long revision,std::size_t count){
 std::lock_guard lock(eventAllocationMx);if(eventAllocations.size()!=count)return false;
#ifdef FIXTURE_SERIAL_ACTION_CONFIG_PRESENT
 for(const void* p:eventAllocations)if(static_cast<const GamepadSerialEvent*>(p)->actionRevision!=revision)return false;
 return true;
#else
 (void)revision;return false;
#endif
}
static constexpr const char* fixtureExpectedUiCancelReason=/*PRODUCTION_CONFIG_UI_REASON*/;
static std::string fixtureLastUiCancelReason(std::size_t from){
 for(std::size_t i=fixtureLifecycleLogs.size();i>from;--i){const auto& data=fixtureLifecycleLogs[i-1]["data"];
  if(data.value("reason",std::string{})==fixtureExpectedUiCancelReason)return data["reason"].get<std::string>();}
 return {};
}
#endif

namespace axisCalibrationRuntime{
/*PRODUCTION_BINDING_AND_MATH*/
static std::mutex mx;
static std::atomic<long long> inputRevision{4};
static std::atomic<bool> centerActive{false},watchActive{false};
static std::atomic<std::uint64_t> ordinal{0};
static std::shared_ptr<const int> published=std::make_shared<const int>(1);
/*PRODUCTION_CENTER_TYPE*/
static Center center;
static std::string watchId;
static std::uint64_t lastWatchAt=0;
static InputCaptureGamepadSnapshot latest;
static json axes(const Pad& p){return{{"left",{{"x",axis(p.sThumbLX)},{"y",axis(p.sThumbLY)}}},{"right",{{"x",axis(p.sThumbRX)},{"y",axis(p.sThumbRY)}}},{"lt",p.bLeftTrigger/255.0},{"rt",p.bRightTrigger/255.0}};}
static json snapshot(){const auto b=binding(latest,GetTickCount64());return{{"protocol","ymcc.axis-calibration.v1"},{"inputRevision",4},{"inputConfigHash","native-delivery-controlled-4"},{"config",defaults()},{"source",bindingJson(b)},{"capabilities",{{"sticks","standard-independent"},{"lt","independent-analog"},{"rt","independent-analog"},{"virtualWrite","available"},{"localWrite","unsupported"}}},{"routes",{{"virtual",{{"state","bypass"},{"reason","master-disabled"},{"appliedInputRevision",nullptr},{"appliedConfigHash",nullptr},{"source",nullptr}}},{"local",{{"state","bypass"},{"reason","master-disabled"},{"appliedInputRevision",nullptr},{"appliedConfigHash",nullptr},{"source",nullptr}}}}},{"axes",{{"raw",axes(latest.pad)},{"virtual",nullptr},{"local",nullptr}}},{"sample",{{"source",bindingJson(b)},{"readSequence",std::to_string(latest.readAttemptSequence)},{"readStartedTick",std::to_string(latest.readStartedTick)},{"readCompletedTick",std::to_string(latest.readCompletedTick)}}},{"centerSession",{{"sessionId",center.id},{"active",center.active}}}};}
/*PRODUCTION_OBSERVE_AND_TICK*/
static void applySavedNotification(){const auto result=snapshot();/*PRODUCTION_APPLY_EMIT*/if(result.is_null())throw std::runtime_error("apply ACK moved away");}
}
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){switch(message){/*PRODUCTION_UI_CASE*/case WM_DESTROY:{/*PRODUCTION_SERIAL_RETIRE*/return 0;}default:return DefWindowProcW(window,message,w,l);}}
static void pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
static void workerTick(int lx,int ly,int lt){std::thread worker([=]{auto& sample=axisCalibrationRuntime::latest;sample.pad.sThumbLX=static_cast<short>(lx);sample.pad.sThumbLY=static_cast<short>(ly);sample.pad.bLeftTrigger=static_cast<unsigned char>(lt);sample.readStartedTick=sample.readCompletedTick=GetTickCount64();sample.readAttemptSequence++;axisCalibrationRuntime::lastWatchAt=0;axisCalibrationRuntime::tick(sample);});worker.join();}

#if defined(SERIAL_MEDIA_FIXTURE)
int main(int argc,char** argv){
 if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"MediaWatcherDeliverySelftest";
 if(!RegisterClassW(&wc))return 3;g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 4;
 json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 auto worker=[](auto callback){std::thread thread(callback);thread.join();++workerThreadsJoined;};
 json state={{"statusRevision",17},{"phase","downloading"},{"bytes",1024},{"current",{{"name","owned fixture"}}}};const auto original=state;
 worker([&]{fixtureMediaChanged();fixtureMediaInventory();fixtureMediaDownloads(state);state["bytes"]=2048;fixtureMediaDownloads(state);state["bytes"]=9999;});
 check("media callbacks only emit on the UI thread",wrongApartment==0);
 check("worker callbacks only enqueue before the UI pump",delivered.empty()&&retainedEvents()==4);
 bool noInputFence=false;{std::lock_guard lock(eventAllocationMx);noInputFence=eventAllocations.size()==4&&std::all_of(eventAllocations.begin(),eventAllocations.end(),[](const void* p){const auto* event=static_cast<const GamepadSerialEvent*>(p);return event->actionPower==0&&event->actionRevision==0;});}
 check("ordinary media notifications have no input action fence",noInputFence);
 ++g_powerGeneration;GamepadSerialEvent::rulesPublished();pump();
 check("ordinary notifications survive unrelated power and input config changes",delivered.size()==4&&retainedEvents()==0);
 check("change and inventory event names and empty payloads remain exact",delivered.size()>=2&&delivered[0]==json({{"event","gameMedia.changed"},{"data",json::object()}})&&delivered[1]==json({{"event","gameMedia.inventoryChanged"},{"data",json::object()}}));
 check("download snapshot copies outlive their producer mutation",delivered.size()>=4&&delivered[2]["event"]=="gameMedia.downloadsChanged"&&delivered[2]["data"]==original&&delivered[3]["data"]["bytes"]==2048);
 const auto burstStart=delivered.size();worker([&]{for(int n=0;n<20;++n)fixtureMediaDownloads({{"statusRevision",100+n},{"sequence",n},{"payload",json::array({1,2,3})}});});
 check("download bursts stay queued until UI dispatch",delivered.size()==burstStart&&retainedEvents()==20);pump();
 bool ordered=delivered.size()==burstStart+20;for(int n=0;ordered&&n<20;++n)ordered=delivered[burstStart+n]["event"]=="gameMedia.downloadsChanged"&&delivered[burstStart+n]["data"]["sequence"]==n&&delivered[burstStart+n]["data"]["payload"]==json::array({1,2,3});
 check("producer FIFO preserves every download snapshot payload",ordered&&retainedEvents()==0);
 const auto beforeAbsent=allocatedEvents;const auto window=g_hwnd;g_hwnd=nullptr;worker([&]{fixtureMediaChanged();fixtureMediaInventory();fixtureMediaDownloads(state);});g_hwnd=window;
 check("missing HWND accepts no callback payload",allocatedEvents==beforeAbsent&&retainedEvents()==0);
 failPost=true;const auto beforeFailed=deletedEvents;worker([&]{fixtureMediaDownloads(state);});failPost=false;
 check("failed asynchronous post returns its payload immediately",deletedEvents==beforeFailed+1&&retainedEvents()==0);
 const auto beforeExit=allocatedEvents;g_exitRequested.store(true);worker([&]{fixtureMediaChanged();fixtureMediaDownloads(state);});
 check("exit fence accepts no late worker notification",allocatedEvents==beforeExit&&retainedEvents()==0);
 g_exitRequested.store(false);worker([&]{fixtureMediaDownloads(state);});const auto beforeCancel=delivered.size();g_exitRequested.store(true);pump();
 check("exit before UI dispatch drops and frees the pending snapshot",delivered.size()==beforeCancel&&retainedEvents()==0);
 g_exitRequested.store(false);worker([&]{fixtureMediaChanged();fixtureMediaDownloads(state);});check("window loss test owns actual queued payloads",retainedEvents()==2);
 const auto postsBeforeDestroy=successfulPosts;DestroyWindow(g_hwnd);check("real message window destruction retires pending payloads",retainedEvents()==0);
 worker([&]{fixtureMediaChanged();fixtureMediaInventory();fixtureMediaDownloads(state);});check("late callbacks cannot reopen a retired mailbox",retainedEvents()==0&&successfulPosts==postsBeforeDestroy);
 g_hwnd=nullptr;pump();check("all callback allocations returned without fixture rescue",allocatedEvents==deletedEvents&&fixtureRescueCount==0);
 bool pass=std::all_of(checks.begin(),checks.end(),[](const json& item){return item["pass"].get<bool>();});
 const json report={{"pass",pass},{"checks",checks},{"mediaWatchers",true},{"actualWatcherLifecycle",false},{"actualCallbackBodies",true},{"actualPostAndCase",true},{"actualWindowsMessagePump",true},{"controlledCallbackWorkerThreadsJoined",workerThreadsJoined},{"allocatedEvents",allocatedEvents},{"deletedEvents",deletedEvents},{"fixtureRescueCount",fixtureRescueCount},{"processExitUsedAsCleanup",false},{"wrongApartmentCalls",wrongApartment},{"deviceIo",false},{"hardwareAccepted",false}};
 std::ofstream file(argv[1],std::ios::binary);file<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return pass?0:1;
}
#elif defined(SERIAL_CONFIG_FIXTURE)
int main(int argc,char** argv){
 if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SerialConfigurationUiSelftest";
 if(!RegisterClassW(&wc))return 3;g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 4;
 json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 auto pending=[&]{MSG message{};return PeekMessageW(&message,g_hwnd,WM_GAMEPAD_SERIAL_EVENT,WM_GAMEPAD_SERIAL_EVENT,PM_NOREMOVE)!=FALSE;};
 const auto power=currentPowerGeneration();
 check("independent config scenario starts at original power",power==1);
 auto pair=[](const char* a,const char* b){gamepadSerialPostEvent(a,{{"copy",true}});gamepadSerialPostOwnedEvent(b,{{"owned",true}});};
 fixtureSubmitAction([&]{pair("config.same.copy","config.same.owned");});fixtureRunNextSerialJob();
 check("both actual Post functions carry original action revision",fixturePendingConfiguration(fixtureConfigurationRevision.load(),2)&&pending()&&delivered.empty());pump();
 check("same revision actions deliver and return payloads",delivered.size()==2&&retainedEvents()==0);

 bool oldJobRan=false;const auto postsBefore=successfulPosts;
 fixtureSubmitAction([&]{oldJobRan=true;gamepadSerialPostEvent("config.old-job",{});});const auto beforeOffOn=fixtureConfigurationRevision.load();fixtureOffOn();
 check("OFF ON retains separate writer revisions",fixtureConfigurationRevision.load()>beforeOffOn&&fixtureConfigurationWriters.load()==0);
 fixtureRunNextSerialJob();pump();check("same power queued action canceled after OFF ON",!oldJobRan&&successfulPosts==postsBefore&&currentPowerGeneration()==power);

 auto count=delivered.size();auto deleted=deletedEvents;
 fixtureSubmitAction([&]{pair("config.stale.copy","config.stale.owned");});fixtureRunNextSerialJob();
 check("old revision metadata really posted and pending before configuration change",fixturePendingConfiguration(fixtureConfigurationRevision.load(),2)&&pending());
 const auto logStart=fixtureLifecycleLogs.size();fixtureOffOn();pump();const auto staleConfigEventsDelivered=delivered.size()-count;
 check("posted actions canceled after OFF ON by actual UI case",staleConfigEventsDelivered==0);
 check("UI cancellation returns metadata and both event allocations",retainedEvents()==0&&deletedEvents==deleted+2&&!pending());
 const auto actualUiCancelReason=fixtureLastUiCancelReason(logStart);
 check("actual UI reports exact extracted configuration cancellation reason",!actualUiCancelReason.empty()&&actualUiCancelReason==fixtureExpectedUiCancelReason);

 count=delivered.size();gamepadSerialSubmit([&]{pair("config.state","config.physical-return");});fixtureRunNextSerialJob();
 check("ordinary state and return events retain actionRevision zero",fixturePendingConfiguration(0,2)&&pending());fixtureOffOn();pump();
 check("zero revision state and return survive configuration change",delivered.size()==count+2&&delivered[count]["event"]=="config.state"&&delivered[count+1]["event"]=="config.physical-return"&&retainedEvents()==0);

 fixtureRulesPublished();check("successful rules publication leaves required ahead of applied",fixtureRulesRequired.load()!=fixtureRulesApplied.load());
 bool staleParseRan=false;const auto queuedBefore=fixtureSerialJobs.size();const bool admitted=fixtureSubmitAction([&]{staleParseRan=true;gamepadSerialPostEvent("config.stale-parse",{});});
 if(fixtureSerialJobs.size()>queuedBefore)fixtureRunNextSerialJob();pump();
 check("Required Applied mismatch refuses old parser new signature",!admitted&&!staleParseRan&&retainedEvents()==0);
 fixtureApplyRules();count=delivered.size();const bool freshAdmitted=fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("config.new-after-applied",{});});
 if(!fixtureSerialJobs.empty())fixtureRunNextSerialJob();pump();check("new action works after Applied catches Required",freshAdmitted&&delivered.size()==count+1&&delivered.back()["event"]=="config.new-after-applied"&&retainedEvents()==0);

 bool nestedRan=false;fixtureSubmitAction([&]{fixtureOffOn();fixtureSubmitAction([&]{nestedRan=true;gamepadSerialPostEvent("config.nested-old",{});});});fixtureRunNextSerialJob();
 while(!fixtureSerialJobs.empty())fixtureRunNextSerialJob();pump();check("nested action preserves original revision instead of resigning current",!nestedRan&&retainedEvents()==0);

 bool writerActionRan=false;{FixtureConfigurationWrite write(true);const auto queued=fixtureSerialJobs.size();const bool accepted=fixtureSubmitAction([&]{writerActionRan=true;});
 if(fixtureSerialJobs.size()>queued)fixtureRunNextSerialJob();check("active configuration writer refuses new action",!accepted&&!writerActionRan);}
 const auto beforeThrow=fixtureConfigurationRevision.load();bool writerCaught=false;
 try{FixtureConfigurationWrite write(true);throw std::runtime_error("fixture-config-writer-throw");}catch(const std::runtime_error&){writerCaught=true;}
 check("actual configuration writer exception returns writer ownership",writerCaught&&fixtureConfigurationWriters.load()==0&&fixtureConfigurationRevision.load()>beforeThrow);
 fixtureSubmitAction([]{throw std::runtime_error("fixture-config-job-throw");});bool jobCaught=false;
 try{fixtureRunNextSerialJob();}catch(const std::runtime_error&){jobCaught=true;}
 check("controlled job exception reaches explicit memory port caller",jobCaught);
#ifdef FIXTURE_SERIAL_ACTION_CONFIG_PRESENT
 check("actual helper restores BoundRevision on exception",GamepadSerialEvent::BoundRevision==0);
 bool observed=false;const auto captured=fixtureConfigurationRevision.load();fixtureSubmitAction([&]{observed=GamepadSerialEvent::BoundRevision==captured;});
 GamepadSerialEvent::BoundRevision=77;fixtureRunNextSerialJob();check("actual helper restores previous nonzero BoundRevision",observed&&GamepadSerialEvent::BoundRevision==77);GamepadSerialEvent::BoundRevision=0;
#endif
 count=delivered.size();gamepadSerialSubmit([]{gamepadSerialPostOwnedEvent("config.return-after-throw",{});});fixtureRunNextSerialJob();
 check("scope restoration leaves ordinary return notification untagged",fixturePendingConfiguration(0,1));fixtureOffOn();pump();check("return after exception remains deliverable",delivered.size()==count+1&&retainedEvents()==0);

 deleted=deletedEvents;failPost=true;fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("config.failed-post",{});});fixtureRunNextSerialJob();failPost=false;
 check("failed Post releases action metadata allocation",retainedEvents()==0&&deletedEvents==deleted+1);
 fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("config.emit-throws",{});});fixtureRunNextSerialJob();MSG message{};
 const bool found=PeekMessageW(&message,g_hwnd,WM_GAMEPAD_SERIAL_EVENT,WM_GAMEPAD_SERIAL_EVENT,PM_REMOVE)!=FALSE;bool emitCaught=false;throwEmit=found;
 // Existing owner seam: private direct catch around actual case, not an exception escaping WndProc.
 if(found)try{windowProc(message.hwnd,message.message,message.wParam,message.lParam);}catch(const std::runtime_error&){emitCaught=true;}
 check("actual UI emit exception unwinds metadata ownership",found&&emitCaught&&retainedEvents()==0);fixtureRescueOldPayloads();
 fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("config.window-loss",{});});fixtureRunNextSerialJob();check("window loss begins with owned unconsumed action metadata",retainedEvents()==1&&pending());
 const bool destroyed=DestroyWindow(g_hwnd)!=FALSE;pump();check("window retirement returns unconsumed action metadata",destroyed&&!IsWindow(g_hwnd)&&retainedEvents()==0);fixtureRescueOldPayloads();
#ifdef FIXTURE_SERIAL_MAILBOX_PRESENT
 delete g_gamepadSerialEventMailbox;
#endif
 const bool returned=UnregisterClassW(wc.lpszClassName,wc.hInstance)!=FALSE;
 check("same power private window queue and all allocations returned",returned&&fixtureSerialJobs.empty()&&allocatedEvents==deletedEvents&&retainedEvents()==0&&currentPowerGeneration()==power&&wrongApartment==0);
 bool pass=true;for(const auto& item:checks)pass=pass&&item["pass"].get<bool>();
 json report={{"pass",pass},{"checks",checks},{"serialConfig",true},{"serialJobPort","fixture-memory-queue"},{"actualSerialWorker",false},{"actualPostAndCase",true},{"actualWindowsMessagePump",true},
#ifdef FIXTURE_SERIAL_ACTION_CONFIG_PRESENT
 {"rawBeforeWithoutConfigurationFence",false},
#else
 {"rawBeforeWithoutConfigurationFence",true},
#endif
 {"staleConfigEventsDelivered",staleConfigEventsDelivered},{"actualUiCancelReason",actualUiCancelReason},{"allocatedEvents",allocatedEvents},{"deletedEvents",deletedEvents},{"fixtureRescueCount",fixtureRescueCount},
 {"allEvents",delivered},{"lifecycleLogs",fixtureLifecycleLogs},{"samePowerGeneration",power},{"processExitUsedAsCleanup",false},{"deviceIo",false},{"settingsIo",false},{"hardwareAccepted",false}};
 std::ofstream(argv[1])<<report.dump(2);std::cout<<report.dump()<<'\n';return pass?0:1;
}
#elif defined(SERIAL_POWER_FIXTURE)
int main(int argc,char** argv){
 if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SerialPowerActionUiSelftest";
 if(!RegisterClassW(&wc))return 3;
 g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 4;
 json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 auto pending=[&]{MSG message{};return PeekMessageW(&message,g_hwnd,WM_GAMEPAD_SERIAL_EVENT,WM_GAMEPAD_SERIAL_EVENT,PM_NOREMOVE)!=FALSE;};
 auto pair=[](const char* copied,const char* owned){json data={{"value",1}};gamepadSerialPostEvent(copied,data);data["value"]=99;gamepadSerialPostOwnedEvent(owned,{{"value",2}});};
 check("fixture power starts at original generation one",currentPowerGeneration()==1);
 const bool admitted=fixtureSubmitAction([&]{pair("power.same.copy","power.same.owned");},SerialJobKind::ExternalInjection);
 check("explicit memory job port queues action and preserves actual enum",admitted&&fixtureSerialJobs.size()==1&&fixtureSerialJobs.front().kind==SerialJobKind::ExternalInjection&&successfulPosts==0&&delivered.empty());
 fixtureRunNextSerialJob();
 check("both actual Post functions attach same-generation action metadata",fixturePendingPower(1,2)&&successfulPosts==2&&pending()&&delivered.empty());
 pump();check("same-power actions cross actual window UI case",delivered.size()==2&&delivered[0]["data"]["value"]==1&&delivered[1]["data"]["value"]==2&&retainedEvents()==0&&wrongApartment==0);

 g_powerGeneration.store(2,std::memory_order_release);auto count=delivered.size();auto deleted=deletedEvents;
 fixtureSubmitAction([&]{pair("power.stale.copy","power.stale.owned");});fixtureRunNextSerialJob();
 check("old-power actions are already successfully posted and still pending",fixturePendingPower(2,2)&&pending()&&delivered.size()==count);
 g_powerGeneration.store(3,std::memory_order_release);pump();
 const auto staleActionEventsDelivered=delivered.size()-count;
 check("posted old-power actions are dropped by actual UI case",staleActionEventsDelivered==0);
 check("stale action metadata and payloads are returned by UI take",retainedEvents()==0&&deletedEvents==deleted+2&&!pending());

 count=delivered.size();gamepadSerialSubmit([&]{pair("power.state","power.physical-return");});fixtureRunNextSerialJob();
 check("generic state and physical-return posts carry actionPower zero",fixturePendingPower(0,2)&&pending());
 g_powerGeneration.store(4,std::memory_order_release);pump();
 check("zero-power state and return notifications survive power change",delivered.size()==count+2&&delivered[count]["event"]=="power.state"&&delivered[count+1]["event"]=="power.physical-return"&&retainedEvents()==0);
 count=delivered.size();fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("power.new",{{"generation",4}});});fixtureRunNextSerialJob();
 check("new-power action carries current generation",fixturePendingPower(4,1));pump();
 check("new-power action is delivered after old actions are discarded",delivered.size()==count+1&&delivered.back()["event"]=="power.new"&&retainedEvents()==0);

 g_powerGeneration.store(5,std::memory_order_release);bool oldJobRan=false;const auto beforeCanceledPosts=successfulPosts;
 fixtureSubmitAction([&]{oldJobRan=true;gamepadSerialPostEvent("power.queued-old",{});});
 g_powerGeneration.store(6,std::memory_order_release);fixtureRunNextSerialJob();pump();
 check("actual helper cancels different-power action at memory job consumption",!oldJobRan&&successfulPosts==beforeCanceledPosts&&retainedEvents()==0);

 g_powerGeneration.store(7,std::memory_order_release);bool nestedRan=false;
 fixtureSubmitAction([&]{g_powerGeneration.store(8,std::memory_order_release);fixtureSubmitAction([&]{nestedRan=true;gamepadSerialPostEvent("power.nested-old",{});});});
 fixtureRunNextSerialJob();check("nested action remains queued at explicit memory seam",fixtureSerialJobs.size()==1);
 fixtureRunNextSerialJob();pump();check("nested helper retains original action generation across power change",!nestedRan&&retainedEvents()==0);

 g_powerGeneration.store(9,std::memory_order_release);fixtureSubmitAction([]{throw std::runtime_error("controlled-action-throw");});bool actionCaught=false;
 try{fixtureRunNextSerialJob();}catch(const std::runtime_error&){actionCaught=true;}
 check("controlled job exception reaches memory port caller",actionCaught);
#ifdef FIXTURE_SERIAL_ACTION_POWER_PRESENT
 check("actual helper RAII restores zero TLS after action exception",g_gamepadSerialActionPower==0);
 g_powerGeneration.store(10,std::memory_order_release);bool sawActionPower=false;
 fixtureSubmitAction([&]{sawActionPower=g_gamepadSerialActionPower==10;});
 g_gamepadSerialActionPower=77;fixtureRunNextSerialJob();
 check("actual helper RAII restores prior nonzero TLS",sawActionPower&&g_gamepadSerialActionPower==77);g_gamepadSerialActionPower=0;
 const auto emptyQueue=fixtureSerialJobs.size();
 check("actual helper rejects empty action without queueing",!gamepadSerialSubmitCurrentPower({})&&fixtureSerialJobs.size()==emptyQueue);
#endif
 count=delivered.size();gamepadSerialSubmit([]{gamepadSerialPostOwnedEvent("power.return-after-throw",{});});fixtureRunNextSerialJob();
 check("generic return after action exception has zero action metadata",fixturePendingPower(0,1));
 g_powerGeneration.store(11,std::memory_order_release);pump();
 check("generic return after action exception still delivers across power",delivered.size()==count+1&&delivered.back()["event"]=="power.return-after-throw"&&retainedEvents()==0);

 deleted=deletedEvents;count=delivered.size();failPost=true;
 fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("power.failed-post",{});});fixtureRunNextSerialJob();failPost=false;
 check("action failed-post leaf returns real event allocation",retainedEvents()==0&&deletedEvents==deleted+1&&delivered.size()==count);
 g_powerGeneration.store(12,std::memory_order_release);fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("power.emit-throws",{});});fixtureRunNextSerialJob();MSG message{};
 const bool found=PeekMessageW(&message,g_hwnd,WM_GAMEPAD_SERIAL_EVENT,WM_GAMEPAD_SERIAL_EVENT,PM_REMOVE)!=FALSE;
 check("emit fault removes exact own queued action message",found);bool emitCaught=false;throwEmit=true;
 // As in owner mode, private direct case catch proves C++ unwind only, never
 // exceptions escaping a Windows WndProc callback.
 if(found)try{windowProc(message.hwnd,message.message,message.wParam,message.lParam);}catch(const std::runtime_error&){emitCaught=true;}
 check("action emit exception returns payload through actual UI case",emitCaught&&retainedEvents()==0);fixtureRescueOldPayloads();

 g_powerGeneration.store(13,std::memory_order_release);fixtureSubmitAction([]{gamepadSerialPostOwnedEvent("power.window-loss",{});});fixtureRunNextSerialJob();
 check("window loss retains an unconsumed action with real metadata",fixturePendingPower(13,1)&&pending());
 g_powerGeneration.store(14,std::memory_order_release);const auto beforeDestroy=delivered.size();const bool destroyed=DestroyWindow(g_hwnd)!=FALSE;pump();
 const auto retainedAtWindowLoss=retainedEvents();
 check("actual window retirement returns unconsumed action metadata",destroyed&&!IsWindow(g_hwnd)&&retainedAtWindowLoss==0&&delivered.size()==beforeDestroy);
 fixtureRescueOldPayloads();
#ifdef FIXTURE_SERIAL_MAILBOX_PRESENT
 delete g_gamepadSerialEventMailbox; // private root; no worker exists and all memory jobs drained
#endif
 const bool classReturned=UnregisterClassW(wc.lpszClassName,wc.hInstance)!=FALSE;
 check("private window class jobs and tracked event allocations returned",classReturned&&fixtureSerialJobs.empty()&&retainedEvents()==0&&allocatedEvents==deletedEvents&&wrongApartment==0);
 bool pass=true;for(const auto& c:checks)pass=pass&&c["pass"].get<bool>();
 json report={{"pass",pass},{"checks",checks},{"serialPower",true},{"serialJobPort","fixture-memory-queue"},{"actualSerialWorker",false},
 {"actualProductionActionHelper",
#ifdef FIXTURE_SERIAL_ACTION_POWER_PRESENT
 true},{"rawBeforeWithoutActionHelper",false},
#else
 false},{"rawBeforeWithoutActionHelper",true},
#endif
 {"memoryJobsConsumed",fixtureSerialJobsRun},{"staleActionEventsDelivered",staleActionEventsDelivered},{"retainedAtWindowLossBeforeFixtureRescue",retainedAtWindowLoss},
 {"allocatedEvents",allocatedEvents},{"deletedEvents",deletedEvents},{"fixtureRescueCount",fixtureRescueCount},{"allEvents",delivered},
 {"actualWindowsMessagePump",true},{"actualPostAndCase",true},{"classAllocationObserverOnly",true},{"processExitUsedAsCleanup",false},{"wrongApartmentCalls",wrongApartment},{"deviceIo",false},{"hardwareAccepted",false}};
 std::ofstream(argv[1])<<report.dump(2);std::cout<<report.dump()<<'\n';return pass?0:1;
}
#elif defined(SERIAL_OWNER_FIXTURE)
int main(int argc,char** argv){
 if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SerialEventOwnershipSelftest";
 if(!RegisterClassW(&wc))return 3;
 g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 4;
 json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 auto worker=[&](auto call){std::thread thread(call);thread.join();++workerThreadsJoined;};
 worker([]{gamepadSerialPostOwnedEvent("owner.happy",{{"value",1}});});
 check("accepted post retains one payload before UI takes it",retainedEvents()==1&&delivered.empty());pump();
 check("actual UI case takes and destroys delivered payload",retainedEvents()==0&&delivered.size()==1&&wrongApartment==0);
 json borrowed={{"value",2}};gamepadSerialPostEvent("owner.copy",borrowed);borrowed["value"]=99;pump();
 check("borrowed producer keeps original copy semantics",delivered.size()==2&&delivered.back()["data"]["value"]==2&&retainedEvents()==0);
 failPost=true;worker([]{gamepadSerialPostOwnedEvent("owner.failed-post",{{"value",3}});});failPost=false;
 check("failed Windows post releases its payload",retainedEvents()==0&&delivered.size()==2);
 gamepadSerialPostOwnedEvent("owner.emit-throws",{{"value",4}});MSG pending{};
 const bool found=PeekMessageW(&pending,g_hwnd,WM_GAMEPAD_SERIAL_EVENT,WM_GAMEPAD_SERIAL_EVENT,PM_REMOVE)!=FALSE;
 check("throw scenario removes exact own queued message",found);
 bool caught=false;throwEmit=true;
 // Directly call the actual case inside a private catch. This proves C++ payload
 // unwinding only; it does not claim Windows supports exceptions escaping WndProc.
 if(found)try{windowProc(pending.hwnd,pending.message,pending.wParam,pending.lParam);}catch(const std::runtime_error&){caught=true;}
 check("controlled emit exception crosses the actual selected case",caught);
 const auto leakedAfterEmit=retainedEvents();check("emit exception returns the actual payload before returning to caller",leakedAfterEmit==0);
 fixtureRescueOldPayloads();
 gamepadSerialPostOwnedEvent("owner.window-loss",{{"value",5}});
 check("window-loss setup owns one successfully posted payload",retainedEvents()==1);
 const auto beforeRacePosts=successfulPosts;exitAfterAllocate=true;
 worker([]{gamepadSerialPostOwnedEvent("owner.late-race",{{"value",6}});});
 check("allocation preemption really publishes terminal exit",g_exitRequested&&!exitAfterAllocate);
 check("terminal retirement rejects post admitted only by stale early exit read",successfulPosts==beforeRacePosts);
 const auto retainedAtExit=retainedEvents();check("terminal retire returns both pending and racing payloads",retainedAtExit==0);
 const bool destroyed=DestroyWindow(g_hwnd)!=FALSE;pump();
 check("actual window destruction leaves no event payload owner",destroyed&&!IsWindow(g_hwnd)&&retainedEvents()==0);
#ifdef FIXTURE_SERIAL_MAILBOX_PRESENT
 const auto prior=delivered.size();if(!postedValues.empty())windowProc(g_hwnd,WM_GAMEPAD_SERIAL_EVENT,0,postedValues.front());
 windowProc(g_hwnd,WM_GAMEPAD_SERIAL_EVENT,0,static_cast<LPARAM>(0x7fffff));
 check("retired and unknown wakeup tokens cannot take or replay a payload",delivered.size()==prior&&retainedEvents()==0);
#endif
 const auto count=allocatedEvents;for(int i=0;i<1000;++i)gamepadSerialPostOwnedEvent("owner.after-exit",{{"ordinal",i}});
 check("terminal rejected callbacks create no persistent payload backlog",allocatedEvents==count&&retainedEvents()==0);
 fixtureRescueOldPayloads();
#ifdef FIXTURE_SERIAL_MAILBOX_PRESENT
 delete g_gamepadSerialEventMailbox; // private fixture only, after every producer joined and HWND is gone
#endif
 const bool classReturned=UnregisterClassW(wc.lpszClassName,wc.hInstance)!=FALSE;
 check("private window class and all tracked payload allocations returned",classReturned&&retainedEvents()==0&&allocatedEvents==deletedEvents);
 bool pass=true;for(const auto& c:checks)pass=pass&&c["pass"].get<bool>();
 json report={{"pass",pass},{"checks",checks},{"leakedAfterEmit",leakedAfterEmit},{"retainedAtExitBeforeFixtureRescue",retainedAtExit},
 {"fixtureRescueCount",fixtureRescueCount},{"allocatedEvents",allocatedEvents},{"deletedEvents",deletedEvents},{"actualWorkerThreadsJoined",workerThreadsJoined},
 {"actualWindowsMessagePump",true},{"actualPostAndCase",true},{"classAllocationObserverOnly",true},{"processExitUsedAsCleanup",false},{"deviceIo",false},{"hardwareAccepted",false}};
 std::ofstream(argv[1])<<report.dump(2);std::cout<<report.dump()<<'\n';return pass?0:1;
}
#else
int main(int argc,char** argv){if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AxisCalibrationDeliverySelftest";RegisterClassW(&wc);g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 3;
 using namespace axisCalibrationRuntime;
 std::vector<json> checks;auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 observe({{"active",true},{"watchId","watch-A"}});workerTick(16384,-8192,128);check("worker queues without calling WebView",delivered.empty()&&wrongApartment==0);pump();check("first raw axes reach UI thread",delivered.size()==1&&delivered.back()["data"]["axes"]["raw"]["left"]["x"]>0.49);
 workerTick(-16384,8192,220);pump();check("moving stick updates next frame",delivered.size()==2&&delivered.back()["data"]["axes"]["raw"]["left"]["x"]<0);
 const auto originalEvents=delivered;
 auto count=delivered.size();foreground=false;workerTick(20000,0,0);pump();check("background stays gated",delivered.size()==count);foreground=true;
 workerTick(2000,1000,0);observe({{"active",false},{"watchId","watch-A"}});observe({{"active",true},{"watchId","watch-B"}});workerTick(3000,0,0);pump();check("queued page identity stays exact",delivered.size()==count+2&&delivered[count]["data"]["observerId"]=="watch-A"&&delivered[count+1]["data"]["observerId"]=="watch-B");
 count=delivered.size();observe({{"active",false},{"watchId","watch-B"}});workerTick(4000,0,0);pump();check("stopped observer sends no frames",delivered.size()==count);
 std::thread centerWorker([&]{auto& s=latest;s.pad={};s.readStartedTick=s.readCompletedTick=GetTickCount64();center={};center.active=true;center.id="center-test";center.source=binding(s,GetTickCount64());center.revision=4;center.started=GetTickCount64();centerActive.store(true);for(int i=0;i<32;++i){Sleep(6);s.readAttemptSequence++;s.readStartedTick=s.readCompletedTick=GetTickCount64();tick(s);}center.started=GetTickCount64()-2201;s.readAttemptSequence++;s.readStartedTick=s.readCompletedTick=GetTickCount64();tick(s);});centerWorker.join();pump();check("center terminal crosses UI thread",delivered.size()==count+1&&delivered.back()["data"].value("ok",false)&&delivered.back()["data"]["sampleCount"]>=30&&!centerActive.load());
 count=delivered.size();std::thread savedWorker([]{applySavedNotification();});savedWorker.join();pump();check("Apply saved notification uses same UI lane",delivered.size()==count+1);
 bool pass=wrongApartment==0;for(const auto& c:checks)pass=pass&&c["pass"].get<bool>();json report={{"pass",pass},{"wrongApartmentCalls",wrongApartment},{"uiThread",uiThread},{"checks",checks},{"frames",originalEvents},{"allEvents",delivered},{"hardwareIo",false},{"settingsWrite",false},{"actualWindowsMessagePump",true}};std::ofstream(argv[1])<<report.dump(2);DestroyWindow(g_hwnd);std::cout<<report.dump()<<std::endl;return pass?0:1;}

#endif
