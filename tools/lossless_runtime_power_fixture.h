#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
#include "fixtures/lossless_uia_ports.h"
#endif
#include "lossless_scale.h"
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
#undef CoCreateInstance
#undef CoInitializeEx
#undef CoUninitialize
#undef QueryFullProcessImageNameW
#undef EnumWindows
#undef GetWindowThreadProcessId
#undef GetWindowTextW
#undef GetClassNameW
#endif
#include "sleep_power_revocation.h"
#include "sleep_wake_admission.h"
#include "json.hpp"
namespace ymcc::screenpads {template<class... Args> inline void ownerSharedPowerChanged(const Args&...){}}
namespace ymcc::http {inline ULONGLONG timeoutBudget(long long){throw std::runtime_error("unselected-http-budget");}}
#if defined(YMCC_LOSSLESS_RUNTIME_ENTRY_ONLY)
namespace ymcc::inputHost::msiPowerWork {inline void readyDelivery(const sleeppower::SharedPowerV1&){} }
#endif
namespace ymcc::losslessRuntimePowerFixture {
using json=nlohmann::json;
inline std::wstring folder;
inline std::atomic<unsigned> opened{0},closed{0},invalid{0},reads{0};
inline std::atomic<HANDLE> borrowedCancellation{nullptr};
inline std::atomic<bool> throttleEntered{false};
inline std::mutex powerMutex;
inline ymcc::sleeppower::SharedPowerV1 nativePowerFact;
inline bool known=true;
inline ymcc::sleeppower::RevocationMirrors revocations;
inline int __cdecl reader(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t bytes){
    ++reads;std::lock_guard<std::mutex> lock(powerMutex);if(!known||bytes<sizeof(nativePowerFact))return 0;*out=nativePowerFact;return 1;
}
inline HRESULT WINAPI localFolder(REFKNOWNFOLDERID,DWORD,HANDLE,PWSTR* output){
    const auto bytes=(folder.size()+1)*sizeof(wchar_t);*output=static_cast<PWSTR>(CoTaskMemAlloc(bytes));
    if(!*output)return E_OUTOFMEMORY;memcpy(*output,folder.c_str(),bytes);return S_OK;
}
inline HANDLE WINAPI firstChange(LPCWSTR name,BOOL subtree,DWORD filter){
    const auto h=::FindFirstChangeNotificationW(name,subtree,filter);if(h!=INVALID_HANDLE_VALUE)++opened;return h;
}
inline BOOL WINAPI closeChange(HANDLE h){const auto ok=::FindCloseChangeNotification(h);if(ok)++closed;else ++invalid;return ok;}
inline DWORD WINAPI waitMany(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
    if(count>=2){borrowedCancellation=handles[1];if(count==2&&timeout&&timeout<500)throttleEntered=true;}
    return ::WaitForMultipleObjects(count,handles,all,timeout);
}
}
#define SHGetKnownFolderPath ymcc::losslessRuntimePowerFixture::localFolder
#define FindFirstChangeNotificationW ymcc::losslessRuntimePowerFixture::firstChange
#define FindCloseChangeNotification ymcc::losslessRuntimePowerFixture::closeChange
#define WaitForMultipleObjects ymcc::losslessRuntimePowerFixture::waitMany
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
#define scaleMainWindow ymcc::losslessUiaFixture::window
#endif
#include "lossless_runtime.h"
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
#undef scaleMainWindow
#undef OpenProcess
#undef CloseHandle
#endif
#undef SHGetKnownFolderPath
#undef FindFirstChangeNotificationW
#undef FindCloseChangeNotification
#undef WaitForMultipleObjects
namespace ymcc::losslessRuntimePowerFixture {
using namespace ymcc::lossless;
inline unsigned assertions=0;
inline std::vector<std::string> failures;
inline void expect(bool ok,const char* label){++assertions;if(!ok)failures.emplace_back(label);}
template<class Predicate> bool until(Predicate predicate,DWORD maximum=1000){
    const auto end=GetTickCount64()+maximum;while(!predicate()&&GetTickCount64()<end)Sleep(2);return predicate();
}
struct Gate {
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    ~Gate(){SetEvent(release);CloseHandle(release);CloseHandle(entered);}
    void enter(){SetEvent(entered);WaitForSingleObject(release,5000);}
    bool seen(){return WaitForSingleObject(entered,1000)==WAIT_OBJECT_0;}
    void open(){SetEvent(release);}
};
inline void reset(){
    std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact={};nativePowerFact.generation=41;nativePowerFact.suspendSerial=1;
    nativePowerFact.observedNotificationSequence=1;nativePowerFact.publicationRevision=1;nativePowerFact.flags=3;known=true;
    opened=closed=invalid=reads=0;borrowedCancellation=nullptr;throttleEntered=false;
}
inline void transition(std::uint32_t phase,std::uint32_t flags,bool newGeneration=false){
    {std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.phase=phase;nativePowerFact.flags=flags;++nativePowerFact.observedNotificationSequence;
      ++nativePowerFact.publicationRevision;if(newGeneration){++nativePowerFact.generation;++nativePowerFact.suspendSerial;}}
    revocations.revoke();
}
inline std::unique_ptr<RuntimeMonitor> monitor(std::function<RuntimeReceipt()> discover){
#if defined(YMCC_LOSSLESS_RUNTIME_LEGACY_CONTROL)
    return std::make_unique<RuntimeMonitor>(std::move(discover));
#else
    return std::make_unique<RuntimeMonitor>(std::move(discover),RuntimePowerAccess{reader,&revocations});
#endif
}
inline void notify(RuntimeMonitor& value){
#if defined(YMCC_LOSSLESS_RUNTIME_LEGACY_CONTROL)
    (void)value;
#else
    value.powerChanged();
#endif
}
inline RuntimeReceipt absent(){return selectRuntime({},false);}
#if defined(YMCC_LOSSLESS_RUNTIME_ENTRY_ONLY)
inline std::atomic<bool> g_exitRequested{false};
inline auto& g_sharedPowerRevocations=revocations;
inline std::unique_ptr<RuntimeMonitor> endpoint;
inline std::function<json(const json&)> watchEntry;
inline std::atomic<unsigned> endpointDiscoveries{0},endpointEmits{0};
inline std::atomic<unsigned> commonEmits{0};
inline std::atomic<bool> endpointReceiptKnown{false};
inline std::atomic<std::uint64_t> endpointGeneration{0},endpointSequence{0},endpointSuspendSerial{0};
inline int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t bytes){return reader(out,bytes);}
inline std::string W2U(const std::wstring& value){
    if(value.empty())return {};if(value.size()>INT_MAX)throw std::length_error("fixture text size");
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("fixture utf8 conversion");std::string result(static_cast<std::size_t>(size),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr)!=size)throw std::runtime_error("fixture utf8 conversion");return result;
}
inline RuntimeMonitor& fixtureMonitor(){return *endpoint;}
inline void ipc_emit(const char* name,const json& value){
    if(std::string(name)=="power.lifecycleChanged"){++commonEmits;return;}
    endpointReceiptKnown=std::string(name)=="lossless.runtime"&&value.at("known").get<bool>();
    endpointGeneration=value.value("powerGeneration",std::uint64_t{0});
    endpointSequence=value.value("powerNotificationSequence",std::uint64_t{0});
    endpointSuspendSerial=value.value("powerSuspendSerial",std::uint64_t{0});++endpointEmits;
}
inline bool ipc_emit_shared_power_fact(const json& state,bool){ipc_emit("power.lifecycleChanged",state);return true;}
[[maybe_unused]] inline bool sleepPowerQueueProjection(HWND){throw std::logic_error("unselected WebView transport fallback");}
inline std::map<std::string,std::function<json(const json&)>> readEntries;
template<class F> void ipc_on(const char* name,F value){if(std::string(name)=="lossless.detect"||std::string(name)=="lossless.getState")readEntries[name]=value;else watchEntry=value;}
#if defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
#include "fixtures/lossless_runtime_read_fixture.h"
#endif
inline void installWatch(){
#include "watch-entry.inc"
}
inline std::atomic<bool> g_sharedPowerProjectionPending{false},g_sensorSuspendCallbackActive{false};
inline std::atomic<bool> g_powerResumeCommitRedelivery{false},g_powerInputRecoverRedelivery{false},g_sleepPowerRedeliveryArmed{false};
inline std::atomic<std::uint64_t> g_sharedPowerPublicationRevision{1};
inline bool commonRetryRequested=false;
inline void sleepPowerArmRedelivery(HWND){commonRetryRequested=true;}
inline void sleepPowerArmUnknownRedelivery(HWND,unsigned long long){commonRetryRequested=true;g_sleepPowerRedeliveryArmed=true;}
inline void sleepPowerRedeliverCommitted(HWND){}
inline unsigned long long currentPowerGeneration(){std::lock_guard<std::mutex> lock(powerMutex);return nativePowerFact.generation;}
inline bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out){return ymcc::sleeppower::capturePower(reader,out);}
inline bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& out){return ymcc::sleeppower::powerCurrent(reader,out);}
inline bool sharedSleepWakeClassified(unsigned long long){return true;}
inline void sharedPowerProjectionConsumerFailed(const char*,
    const ymcc::sleeppower::SharedPowerV1* = nullptr,const std::uint64_t* = nullptr){}
template<class... Args> inline void monitorSharedPowerChanged(const Args&...){}
template<class... Args> inline bool nativeAyaLightingPublicReady(const Args&...){return true;}
template<class... Args> inline bool projectPowerSourceRecovery(const Args&...){return true;}
template<class... Args> inline bool deckySidebarRecheckPublicReady(const Args&...){return true;}
template<class... Args> inline bool steamWallpaperForegroundSharedPowerChanged(const Args&...){return true;}
inline struct { void sharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){} } g_gameOverlay;
inline void sgOfferStartupResume(const ymcc::sleeppower::SharedPowerV1&){}
inline bool sgSleepRetryActive(){return false;}
inline unsigned long long currentFanPowerGeneration(){return 0;}
enum class FanReadyDelivery { Idle, Settled, Deferred };
inline FanReadyDelivery fanHostScheduleEmergencyResume(unsigned long long){return FanReadyDelivery::Idle;}
inline bool fanHostPhysicalWakeCurrent(){return false;}
inline FanReadyDelivery fanHostPublishPhysicalWake(std::uint64_t,bool){return FanReadyDelivery::Idle;}
inline void steamAppearancePublicReady(unsigned long long){}
inline void inputCaptureResumeSensorsForPower(){}
inline void sgNotifyParkedWakeGameRecovery(){}
inline bool msiNativeForegroundSharedPowerReady(const ymcc::sleeppower::SharedPowerV1&){return true;}
inline bool msiNativeForegroundSharedPowerBoundary(const ymcc::sleeppower::SharedPowerV1&){return true;}
inline bool radialOverlaySharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){return true;}
inline bool focusIsolationSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){return true;}
inline bool cpuUvTrialPowerChanged(){return false;}
namespace SleepRecoveryAuto {inline void powerReady(unsigned long long){}}
inline void steamOriginalRecoveryPowerReady(unsigned long long){}
namespace SteamControllerCursor {template<class... Args> inline void power(const Args&...){}}

#if !defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
inline json sharedPowerLifecycleSnapshot(){ymcc::sleeppower::SharedPowerV1 value;const auto ok=reader(&value,sizeof(value))==1;return {{"known",ok},{"generation",value.generation},{"notificationPending",(value.flags&4)!=0},{"hardwareWritesAllowed",ymcc::sleeppower::powerAdmitted(value)}};}
#include "common-projection.inc"
#if defined(YMCC_LOSSLESS_RUNTIME_WATCH_ORDER)
#include "fixtures/lossless_runtime_watch_dispatch.inc"
#endif
inline void deliverCommon(){
    {std::lock_guard<std::mutex> lock(powerMutex);g_sharedPowerPublicationRevision=++nativePowerFact.publicationRevision;}
    g_sharedPowerProjectionPending=true;projectSharedPowerLifecycle(nullptr);
}
#endif
#endif
inline std::vector<std::pair<std::string,std::function<void()>>> cases(){
    return {
      {"pending-power-never-discovers",[]{
        {std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
        std::atomic<unsigned> discoveries{0},receipts{0};
        auto owner=monitor([&]{++discoveries;return absent();});owner->configure(true,false,[&](const auto&){++receipts;});
        until([&]{return reads.load()!=0||discoveries.load()!=0;});Sleep(30);
        expect(discoveries==0&&receipts==0&&opened==0,"pending-public-fact-must-not-discover-publish-or-watch");
        transition(0,3);notify(*owner);expect(until([&]{return receipts.load()!=0;}),"public-ready-resumes-without-new-configure");
        owner->stop();expect(discoveries==1&&receipts==1,"ready-performs-one-fresh-read");
      }},
      {"unknown-power-then-ready",[]{
        {std::lock_guard<std::mutex> lock(powerMutex);known=false;}
        std::atomic<unsigned> discoveries{0},receipts{0};auto owner=monitor([&]{++discoveries;return absent();});
        owner->configure(true,false,[&](const auto&){++receipts;});expect(until([&]{return reads.load()!=0;}),"unknown-reader-observed");
        expect(discoveries==0&&opened==0,"unknown-is-not-ready");
        {std::lock_guard<std::mutex> lock(powerMutex);known=true;}
        notify(*owner);expect(until([&]{return receipts.load()==1;}),"known-ready-resumes-unknown-demand");owner->stop();
      }},
      {"revoke-during-discovery",[]{
        Gate gate;std::atomic<unsigned> discoveries{0},receipts{0};
        auto owner=monitor([&]{if(++discoveries==1)gate.enter();return absent();});
        owner->configure(true,false,[&](const auto&){++receipts;});const bool entered=gate.seen();expect(entered,"actual-worker-entered-discovery");
        transition(1,5);const auto before=reads.load();gate.open();expect(until([&]{return reads.load()>before;}),"worker-checks-revocation-after-read");
        expect(receipts==0&&opened==0,"retired-discovery-cannot-publish-or-open-watch");
        transition(0,3,true);notify(*owner);expect(until([&]{return receipts.load()==1;}),"new-native-cycle-gets-fresh-read");
        owner->stop();expect(discoveries==2,"old-discovery-not-reused-after-ready");
      }},
      {"off-on-aba-retires-old-discovery",[]{
        Gate gate;std::atomic<unsigned> discoveries{0},receipts{0},retired{0};
        auto owner=monitor([&]{const auto n=++discoveries;if(n==1)gate.enter();auto r=absent();r.reason=n==1?"retired":"current";return r;});
        const auto emit=[&](const RuntimeReceipt& r){++receipts;if(r.reason=="retired")++retired;};
        owner->configure(true,false,emit);expect(gate.seen(),"aba-worker-entered-old-read");
        owner->configure(false,false,emit);owner->configure(true,false,emit);gate.open();
        expect(until([&]{return receipts.load()!=0;}),"aba-new-request-completes");owner->stop();
        expect(retired==0&&receipts==1&&discoveries==2,"same-bool-aba-does-not-revive-old-read");
      }},
      {"off-during-throttle",[]{
        std::atomic<unsigned> discoveries{0},receipts{0};auto owner=monitor([&]{++discoveries;return absent();});
        const auto emit=[&](const auto&){++receipts;};owner->configure(true,false,emit);
        expect(until([&]{return receipts.load()==1&&borrowedCancellation.load()!=nullptr;}),"first-read-enters-real-wait");
        owner->configure(true,false,emit);expect(until([&]{return throttleEntered.load();}),"same-request-wakeup-enters-original-throttle");
        owner->configure(false,false,emit);owner->stop();
        expect(discoveries==1&&receipts==1,"off-throttle-does-not-perform-second-read");
      }},
      {"power-cancel-returns-watch-and-fresh-ready",[]{
        std::atomic<unsigned> discoveries{0},receipts{0};auto owner=monitor([&]{++discoveries;return absent();});
        owner->configure(true,false,[&](const auto&){++receipts;});
        expect(until([&]{return receipts.load()==1&&opened==1&&borrowedCancellation.load()!=nullptr;}),"active-read-owns-real-directory-watch");
        HANDLE old=nullptr;expect(DuplicateHandle(GetCurrentProcess(),borrowedCancellation.load(),GetCurrentProcess(),&old,SYNCHRONIZE,FALSE,0)!=FALSE,"borrow-cancellation-by-synchronize-duplicate");
        const auto cancelAt=GetTickCount64();transition(1,5);
        expect(until([&]{return closed.load()==1;},700),"native-revocation-wakes-original-wait-and-closes-watch");
        expect(GetTickCount64()-cancelAt<700,"return-does-not-wait-for-original-2000ms-timeout");
        transition(0,3,true);notify(*owner);expect(until([&]{return receipts.load()==2;}),"public-ready-opens-fresh-observer-scope");
        expect(old&&WaitForSingleObject(old,0)==WAIT_OBJECT_0,"old-cancellation-event-remains-signaled-after-ready");
        if(old)CloseHandle(old);
        const auto count=discoveries.load();{std::lock_guard<std::mutex> lock(powerMutex);++nativePowerFact.publicationRevision;}
        notify(*owner);Sleep(40);expect(discoveries==count,"diagnostic-publication-does-not-refresh-healthy-scope");
        owner->stop();owner->configure(true,true,{});notify(*owner);
        expect(discoveries==count,"terminal-stop-never-restarts-on-late-configure-or-ready");
      }},
      {"discovery-exception-self-heals",[]{
        std::atomic<unsigned> discoveries{0},receipts{0};auto owner=monitor([&]{if(++discoveries==1)throw std::runtime_error("injected-read-error");return absent();});
        owner->configure(true,false,[&](const auto&){++receipts;});expect(until([&]{return receipts.load()==1;},3500),"same-worker-recovers-after-read-exception");
        owner->stop();expect(discoveries==2,"read-error-retains-one-original-delayed-retry");
      }},
      {"subscriber-exception-self-heals",[]{
        std::atomic<unsigned> discoveries{0},calls{0};auto owner=monitor([&]{++discoveries;return absent();});
        owner->configure(true,false,[&](const auto&){if(++calls==1)throw std::runtime_error("injected-subscriber-error");});
        expect(until([&]{return calls.load()==2;},3500),"subscriber-failure-does-not-terminate-worker");
        owner->stop();expect(discoveries==2&&opened==2&&closed==2,"failed-publication-returns-old-watch-before-fresh-read");
      }},
      {"consecutive-failures-retain-original-delay",[]{
        std::atomic<unsigned> discoveries{0},receipts{0};const auto start=GetTickCount64();
        auto owner=monitor([&]{if(++discoveries<=2)throw std::runtime_error("repeated-read-error");return absent();});
        owner->configure(true,false,[&](const auto&){++receipts;});
        expect(until([&]{return receipts.load()==1;},3500),"repeated-error-eventually-recovers-same-demand");
        const auto elapsed=GetTickCount64()-start;owner->stop();
        expect(discoveries==3&&elapsed>=1900&&elapsed<3500,"pending-initial-wakeup-does-not-create-a-retry-busy-loop");
      }}
#if defined(YMCC_LOSSLESS_RUNTIME_ENTRY_ONLY)
      ,{"native-watch-entry-public-source-and-off",[]{
        endpointDiscoveries=endpointEmits=0;endpointReceiptKnown=false;g_exitRequested=false;
        endpoint=std::make_unique<RuntimeMonitor>([]{++endpointDiscoveries;return absent();});installWatch();
        bool invalidRejected=false;try{watchEntry(json{{"active",true}});}catch(const std::runtime_error& e){invalidRejected=std::string(e.what())=="LOSSLESS_INVALID_WATCH_REQUEST";}
        expect(invalidRejected&&endpointDiscoveries==0,"actual-watch-registration-preserves-schema-rejection");
        {std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
        expect(watchEntry(json{{"active",true},{"detailed",false}})==true,"actual-watch-accepts-pending-desired-observer");
        expect(until([]{return reads.load()!=0;}),"entry-wired-original-public-reader");
        expect(endpointDiscoveries==0&&endpointEmits==0,"entry-cannot-bypass-pending-common-source");
        transition(0,3);endpoint->powerChanged();
        expect(until([]{return endpointEmits.load()==1;}),"actual-watch-public-ready-publishes-without-another-ipc");
        expect(endpointReceiptKnown&&endpointDiscoveries==1,"actual-watch-sends-fresh-original-runtime-receipt");
        expect(endpointGeneration==41&&endpointSequence==2&&endpointSuspendSerial==1,
            "actual-watch-receipt-carries-ready-observation-scope");
        expect(watchEntry(json{{"active",false},{"detailed",false}})==true,"actual-watch-retains-normal-off");endpoint->stop();
        bool stoppedRejected=false;try{watchEntry(json{{"active",true},{"detailed",false}});}catch(const std::runtime_error& e){stoppedRejected=std::string(e.what())=="LOSSLESS_WATCH_UNAVAILABLE";}
        expect(stoppedRejected&&endpointDiscoveries==1,"unavailable-native-watch-propagates-error-and-does-not-restart");
        g_exitRequested=true;expect(watchEntry(json{{"active",true},{"detailed",false}})==false,"actual-watch-exit-check-kept");
        endpoint.reset();watchEntry={};g_exitRequested=false;
      }}
#if !defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
      ,{"actual-common-projection-resumes-native-monitor",[]{
        endpointDiscoveries=endpointEmits=commonEmits=0;endpointReceiptKnown=false;g_exitRequested=false;commonRetryRequested=false;
        endpoint=std::make_unique<RuntimeMonitor>([]{++endpointDiscoveries;return absent();});installWatch();
        {std::lock_guard<std::mutex> lock(powerMutex);nativePowerFact.flags=7;}
        watchEntry(json{{"active",true},{"detailed",false}});deliverCommon();
        expect(until([]{return reads.load()!=0;})&&endpointDiscoveries==0&&commonEmits==1,"actual-common-pending-projection-keeps-observer-unadmitted");
        transition(0,3);deliverCommon();
        expect(until([]{return endpointEmits.load()==1;}),"actual-common-ready-projection-resumes-existing-native-thread-without-ipc");
        expect(endpointDiscoveries==1&&opened==1,"common-ready-observer-opens-only-fresh-read-scope");
        transition(1,5);deliverCommon();
        expect(until([]{return closed.load()==1;},700),"nonready-common-fact-and-native-cancel-return-current-watch");
        transition(0,3,true);deliverCommon();
        expect(until([]{return endpointEmits.load()==2;}),"next-common-ready-renews-scope-in-same-desired-monitor");
        expect(endpointGeneration==42&&endpointSequence==4&&endpointSuspendSerial==2,
            "actual-watch-receipt-renews-all-scope-fields-after-suspend");
        const auto count=endpointDiscoveries.load();deliverCommon();Sleep(40);
        expect(endpointDiscoveries==count&&!commonRetryRequested,"diagnostic-common-publication-keeps-healthy-observer-and-no-retry-timer");
        g_exitRequested=true;const auto emitted=commonEmits.load();deliverCommon();
        expect(commonEmits==emitted,"actual-common-exit-refuses-late-projection");
        endpoint->stop();endpoint.reset();watchEntry={};g_exitRequested=false;g_sharedPowerProjectionPending=false;
      }}
#endif
#endif
      ,{"concurrent-stop-waits-for-actual-worker-return",[]{
        Gate gate;std::atomic<unsigned> discoveries{0},returned{0};std::atomic<bool> secondEntered{false};
        auto owner=monitor([&]{++discoveries;gate.enter();return absent();});
        owner->configure(true,false,{});expect(gate.seen(),"stop-worker-holds-real-inflight-discovery");
        std::thread first([&]{owner->stop();++returned;});
        expect(until([&]{return !owner->configure(true,false,{});}),"stop-retires-desire-before-joining");
        std::thread second([&]{secondEntered=true;owner->stop();++returned;});
        expect(until([&]{return secondEntered.load();}),"second-stop-caller-entered");Sleep(30);
        expect(returned==0,"no-stop-caller-claims-return-before-owned-worker-releases");
        gate.open();first.join();second.join();
        expect(returned==2&&discoveries==1&&opened==0,"both-stop-callers-return-after-only-worker-joins-without-late-watch");
        notify(*owner);expect(!owner->configure(true,false,{}),"late-ready-and-configure-cannot-recreate-retired-thread");
      }}
#if defined(YMCC_LOSSLESS_RUNTIME_WATCH_ORDER)
#include "fixtures/lossless_runtime_watch_cases.inc"
#endif
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
#include "fixtures/lossless_uia_cases.inc"
#endif
#if defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
#include "fixtures/lossless_runtime_read_cases.inc"
#endif
    };
}
inline int main(int argc,wchar_t** argv){
    if(argc<2||argc>3){std::cerr<<"private directory and optional exact original case required";return 2;}
    folder=argv[1];std::filesystem::create_directories(std::filesystem::path(folder)/L"Lossless Scaling");
    const std::string selected=argc==3?std::filesystem::path(argv[2]).string():"";
    auto definitions=cases();const bool suffix=selected.rfind("from:",0)==0;const auto exact=suffix?selected.substr(5):selected;
    if(argc==3&&(exact.empty()||std::none_of(definitions.begin(),definitions.end(),[&](const auto& c){return c.first==exact;}))){std::cerr<<"unknown or empty case";return 2;}
#if defined(YMCC_LOSSLESS_RUNTIME_UIA_ONLY)
    // Prime process-local COM/OLE infrastructure before the unchanged per-case
    // handle baseline; warm resources still return on their creating thread.
    std::thread warm([]{NoFocusAutomation automation;auto signal=std::make_shared<RuntimeSignal>();auto handler=Microsoft::WRL::Make<RuntimePropertyHandler>(signal);if(!automation.begin())throw std::runtime_error("uia-fixture-warm-failed");automation.listen(ymcc::losslessUiaFixture::window(GetCurrentProcessId()),handler.Get());(void)automation.observe(ymcc::losslessUiaFixture::window(GetCurrentProcessId()),L"");});
    warm.join();
#if defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
    {NoFocusAutomation automation;auto signal=std::make_shared<RuntimeSignal>();auto handler=Microsoft::WRL::Make<RuntimePropertyHandler>(signal);if(!automation.begin())throw std::runtime_error("uia-read-fixture-warm-failed");automation.listen(ymcc::losslessUiaFixture::window(GetCurrentProcessId()),handler.Get());(void)automation.observe(ymcc::losslessUiaFixture::window(GetCurrentProcessId()),L"");}
#endif
#endif
    json rows=json::array();
    bool reached=false;for(const auto& entry:definitions){if(entry.first==exact)reached=true;if(!selected.empty()&&(suffix?!reached:entry.first!=exact))continue;
        reset();const auto beforeAssertions=assertions;const auto beforeFailures=failures.size();DWORD handlesBefore=0,handlesAfter=0;
        GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);const auto start=GetTickCount64();entry.second();
        GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
        expect(opened==closed&&invalid==0,"actual-directory-notifications-returned-without-invalid-close");
        expect(handlesAfter==handlesBefore,"all-private-worker-events-registrations-and-watches-returned");
        rows.push_back({{"name",entry.first},{"assertions",assertions-beforeAssertions},{"failures",failures.size()-beforeFailures},{"elapsedMs",GetTickCount64()-start},{"opened",opened.load()},{"closed",closed.load()},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter}});
    }
    std::cout<<json{{"canonicalId","controller.lossless-toggle-owner"},{"selection","native-runtime-power"},{"cases",rows},{"assertions",assertions},{"failures",failures},{"pass",failures.empty()},
      {"actualMonitorThread",true},{"actualNativeReaderAndRegistration",true},{"actualDirectoryNotificationHandles",true},{"uiaInstantiated",false},{"liveLosslessDiscovered",false},{"osInputActions",0}}.dump()<<'\n';
    return failures.empty()?0:1;
}
}
