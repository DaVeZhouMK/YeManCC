#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <functional>
#include "fan_physical_wake.h"
#include "fan_resume_business.h"
#include "sleep_power_dispatch.h"
#include "sleep_power_remote_transport.h"
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"advapi32.lib")
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
#include "http_request_deadline.h"
#include "sleep_power_revocation.h"
#include "fan_http_body_contract.h"
#include "fan_resume_evidence.h"
#include "fan_resume_owner.h"
#include "fan_response_contract.h"
#pragma comment(lib,"winhttp.lib")
using json=nlohmann::json;
INTERNET_PORT fixturePort=0;
std::string fixtureCase;
int authRejected=0,headerFailed=0,leaseArms=0;
std::vector<json> lifecycle;
std::atomic<bool> g_exitRequested{false};
std::atomic<unsigned long long> g_fanResumeRecoveryGeneration{0};
enum class PowerLifecycle : uint8_t { Ready, Suspending, Suspended, Resuming };
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_fanResumeManualOperation{0};
static std::atomic<std::uint64_t> g_fanWritePowerRevision{1};
static std::atomic<std::uint64_t> fixtureNativeGeneration{1},fixtureFanGeneration{1};
static std::atomic<bool> fixtureIntent{true};
struct FixtureActivity{int revision=1;};
static std::mutex g_fanActivityMx;
static FixtureActivity g_fanActivity;
static unsigned long long currentPowerGeneration(){return fixtureNativeGeneration.load();}
ymcc::fan_resume::RequestOwner g_fanResumeRequestOwner;
std::string g_exitSessionId="fixture-exit";
std::wstring U2W(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring out(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;}
std::string W2U(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string out(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::string ascii_lower(std::string value){for(char& ch:value)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));return value;}
std::string fanHostReadSessionToken(){return fixtureCase=="missing-token"?std::string{}:"fixture-"+fixtureCase;}
bool fanHostExactProcessRunning(){return fixtureCase!="wrong-process";}
template<class...Args>void traceLog(Args&&...){}
bool domainLogEnabled(const char*){return false;}
template<class...Args>void appendDomainLog(Args&&...){}
template<class...Args>void fanHostNoteCredentialUnavailable(Args&&...){}
template<class...Args>void fanHostIdentityNoteUnverified(Args&&...){}
template<class...Args>void fanHostNoteAuthRejected(Args&&...){++authRejected;}
template<class...Args>void fanHostNoteRequestHeaderFailed(Args&&...){++headerFailed;}
std::string fanRequestId(const char*){return "fixture-request";}
std::string fanErrorCodeFromBody(const std::string& b){auto p=json::parse(b,nullptr,false);return p.is_object()&&p.contains("error")&&p["error"].is_object()?p["error"].value("code",std::string{}):std::string{};}
static std::vector<json> formalCaptures;
static ymcc::sleeppower::remote::NativeProvider* fixtureProvider=nullptr;
static ymcc::sleeppower::remote::NativeRemoteTransport* fixtureTransport=nullptr;
static bool fixtureClaimBusiness=false;
static std::vector<ymcc::sleeppower::remote::DescriptorV1> fixtureBusinessOffers;
static size_t fixtureBusinessClaims=0;
static bool fixtureHostCurrent=true;
static std::atomic<HANDLE> fixtureChosenHost{nullptr};
static ymcc::sleeppower::remote::NativeRemoteTransport* sharedNativeRemotePowerTransport() noexcept {return fixtureTransport;}
static bool fanHostExactProcessCurrent(HANDLE held,const ymcc::sleeppower::remote::ProcessIdentityV1& expected) noexcept {ymcc::sleeppower::remote::ProcessIdentityV1 current;ymcc::sleeppower::remote::ProcessIdentityV1 selected;const HANDLE chosen=fixtureChosenHost.load()?fixtureChosenHost.load():GetCurrentProcess();return fixtureHostCurrent&&ymcc::sleeppower::remote::processIdentity(held,current)&&ymcc::sleeppower::remote::sameProcess(current,expected)&&((fixtureChosenHost.load()?ymcc::sleeppower::remote::processIdentity(chosen,selected):ymcc::sleeppower::remote::sameProcess(current,expected)))&&(!fixtureChosenHost.load()||ymcc::sleeppower::remote::sameProcess(selected,expected));}
static bool fanHostRetainExactProcess(ymcc::sleeppower::remote::ProcessHold& held,ymcc::sleeppower::remote::ProcessIdentityV1& identity) noexcept {if(!fixtureHostCurrent||!held.bind(fixtureChosenHost.load()?fixtureChosenHost.load():GetCurrentProcess()))return false;return ymcc::sleeppower::remote::processIdentity(held.get(),identity);}
void setHttpTimeouts(HINTERNET h,int ms){WinHttpSetTimeouts(h,ms,ms,ms,ms);}
void fixtureObserveLifecycle(const char*);
static void fanHostEmergencyResume(unsigned long long,ymcc::fan_resume::RequestTicket = {});
static unsigned unexpectedDrains=0;
static void fanHostStartResumeTicket(ymcc::fan_resume::RequestTicket) noexcept;
static std::mutex fixtureLogMx;
static std::atomic<bool> fixtureThrowFailureLog{false},fixtureRevokeAtStatus{false};
static std::atomic<unsigned> fixtureStatusCalls{0};
static std::atomic<std::uint64_t> fixtureClockAdvance{0};
void appendFanLifecycleLog(const char* name,const json& detail){if(std::string(name)=="fan-host-resume-thread-failed"&&fixtureThrowFailureLog.exchange(false))throw std::runtime_error("explicit diagnostic failure");{std::lock_guard<std::mutex> lock(fixtureLogMx);lifecycle.push_back(json{{"event",name},{"details",detail}});}fixtureObserveLifecycle(name);}
unsigned long long currentFanPowerGeneration(){return fixtureFanGeneration.load();}
std::atomic<bool> sharedPowerKnown{true},sharedPowerPending{false},sharedDiagnosticPublication{false};
std::atomic<std::uint64_t> sharedPublication{1},sharedSerial{1},sharedNotification{1};
static auto& g_sharedPowerSuspendSerial=sharedSerial;
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
ymcc::sleeppower::RevocationMirrors g_sharedPowerRevocations;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t size)noexcept{
 if(g_exitRequested.load()||!sharedPowerKnown.load()||size<sizeof(*out))return 0;*out={};out->generation=currentPowerGeneration();out->phase=static_cast<std::uint32_t>(g_powerLifecycle.load());out->suspendSerial=sharedSerial.load();out->observedNotificationSequence=sharedNotification.load();
 out->publicationRevision=sharedDiagnosticPublication.load()?sharedPublication.fetch_add(1):sharedPublication.load();
 out->flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed|(sharedPowerPending.load()?ymcc::sleeppower::kNotificationPending:0u);return 1;
}
void fixtureObserveLifecycle(const char* name){
 if(std::string(name)!="fan-host-resume-attempt")return;
 if(fixtureCase=="business-awaiting-old-superseded"&&formalCaptures.size()==1){++sharedNotification;++sharedPublication;++g_fanWritePowerRevision;}
 if(fixtureCase=="formal-peer-replaced"&&formalCaptures.size()==1){++sharedNotification;++sharedPublication;}
 if(fixtureCase=="f5-ready-retains-unknown"||fixtureCase=="f5-three-shared-slots") {++sharedNotification;++sharedPublication;}
 if(fixtureCase=="shared-owner-revoke"||fixtureCase=="shared-owner-retry")g_sharedPowerRevocations.revoke();
 if(fixtureCase=="shared-owner-pending")sharedPowerPending=true;
 if(fixtureCase=="shared-owner-serial")++sharedSerial;
 if(fixtureCase=="shared-owner-publication")++sharedPublication;
}
bool fanHostPowerMayWrite(){return sharedPowerKnown.load()&&!sharedPowerPending.load();}
bool fanHostPowerMayResume(){return fanHostPowerMayWrite();}
bool fanActivityIntentPresent(){return fixtureIntent.load();}
#include "FanResumeScheduleResult.inc"
static FanResumeScheduleResult fanHostScheduleFanResume(unsigned long long,bool=false,unsigned long long=0);
void fanLeaseKeepaliveArm(const std::string&,const char*,ULONGLONG=0){++leaseArms;}
struct FixtureDisplay{ULONGLONG epoch(){return 1;}} g_monitorFanDisplay;
int availabilityQueries=0;
BOOL countedDataAvailable(HINTERNET h,LPDWORD count){++availabilityQueries;return WinHttpQueryDataAvailable(h,count);}
static BOOL fixtureQueryHeaders(HINTERNET request,DWORD info,LPCWSTR name,LPVOID buffer,LPDWORD bytes,LPDWORD index){
 const auto result=::WinHttpQueryHeaders(request,info,name,buffer,bytes,index);
 if(result&&fixtureClaimBusiness&&(info&~WINHTTP_QUERY_FLAG_NUMBER)==WINHTTP_QUERY_STATUS_CODE&&
    fixtureBusinessClaims<fixtureBusinessOffers.size()){
  // Model a loopback peer importing the delivered offer before its response is consumed.
  const auto& offer=fixtureBusinessOffers[fixtureBusinessClaims];
  if(fixtureProvider->claimRemoteImport(offer.scope,fixtureChosenHost.load(),offer.purpose))++fixtureBusinessClaims;
 }
 if((info&~WINHTTP_QUERY_FLAG_NUMBER)==WINHTTP_QUERY_STATUS_CODE)++fixtureStatusCalls;
 if((info&~WINHTTP_QUERY_FLAG_NUMBER)==WINHTTP_QUERY_STATUS_CODE&&fixtureRevokeAtStatus.exchange(false)){
  std::lock_guard<std::mutex> lock(g_fanActivityMx);
  fixtureIntent=false;++g_fanActivity.revision;fixtureIntent=true;++g_fanActivity.revision;
 }
 return result;
}
static BOOL fixtureAddHeaders(HINTERNET request,LPCWSTR text,DWORD length,DWORD modifiers){
 const auto result=::WinHttpAddRequestHeaders(request,text,length,modifiers);
 if(result){std::wstring headers(text,length==static_cast<DWORD>(-1)?std::wcslen(text):length);const std::wstring prefix=L"X-YMCC-Shared-Power: ";const auto at=headers.find(prefix);if(at!=std::wstring::npos){
  const auto end=headers.find(L"\r\n",at);const auto value=W2U(headers.substr(at+prefix.size(),end-at-prefix.size()));const auto split=value.rfind(' ');
  if(split==std::string::npos||value.size()-split-1!=256)throw std::runtime_error("formal header format");
  const auto hex=value.substr(split+1);ymcc::sleeppower::remote::DescriptorV1 descriptor;auto* bytes=reinterpret_cast<unsigned char*>(&descriptor);
  for(std::size_t i=0;i<sizeof(descriptor);++i)bytes[i]=static_cast<unsigned char>(std::stoul(hex.substr(i*2,2),nullptr,16));
  if(fixtureClaimBusiness)fixtureBusinessOffers.push_back(descriptor);
  formalCaptures.push_back({{"header",value},{"scopeLow",std::to_string(descriptor.scope.low)},{"scopeHigh",std::to_string(descriptor.scope.high)},{"requestLow",std::to_string(descriptor.request.low)},{"requestHigh",std::to_string(descriptor.request.high)},{"providerPid",descriptor.provider.pid},{"consumerPid",descriptor.consumer.pid},{"nativeGeneration",descriptor.captured.generation},{"serial",descriptor.captured.suspendSerial},{"notification",descriptor.captured.observedNotificationSequence}});
 }}return result;
}
#define WinHttpQueryDataAvailable countedDataAvailable
#define WinHttpAddRequestHeaders fixtureAddHeaders
#define WinHttpQueryHeaders fixtureQueryHeaders
#include "SchedulePorts.inc"
static bool fanHostPhysicalWakeCurrent(){return g_fanPhysicalWake.physical(currentFanPowerGeneration());}
static std::atomic<unsigned> fixtureObservationSleeps{0};
static void fixtureSleep(DWORD delay){
 if(fixtureStatusCalls.load()>=2 &&
   (fixtureCase=="business-unknown"||fixtureCase=="business-other-request"||fixtureCase=="business-running-global-ready"||fixtureCase=="business-duplicate-key")){
  ++fixtureObservationSleeps;fixtureClockAdvance=16000;return;
 }
 ::Sleep(delay);
}
#define GetTickCount64 fixtureClock
#define Sleep fixtureSleep
#include "PowerExtract.inc"
#undef Sleep
#undef GetTickCount64
#undef WinHttpQueryDataAvailable
#undef WinHttpAddRequestHeaders
#undef WinHttpQueryHeaders
int legacyMain(int argc,char** argv){
 if(argc!=4)return 9;fixturePort=static_cast<INTERNET_PORT>(std::stoi(argv[1]));const std::string mode=argv[2];fixtureCase=argv[3];
 // A Ready reply can only retain control if the peer actually claimed the offer.
 fixtureClaimBusiness=(mode=="resume"||mode=="resume-retry")&&fixtureCase!="shared-owner-resume-unclaimed";
 if(fixtureCase.rfind("shared-unknown",0)==0)sharedPowerKnown=false;
 if(fixtureCase=="shared-pending-resume")sharedPowerPending=true;
 if(fixtureCase=="shared-publication-resume")sharedDiagnosticPublication=true;
 std::thread sharedCancel;if(fixtureCase=="shared-cancel-resume")sharedCancel=std::thread([]{Sleep(80);g_sharedPowerRevocations.revoke();});
 const auto started=GetTickCount64();DWORD status=999;std::string body="must-clear";bool ok=false;
 std::vector<json> requestViews;
 bool reopened=false;
 const auto invoke=[&](unsigned long long start,unsigned long long manual=0){
  FanHttpPowerAdmission capture;capture.resume=true;
  if(!capture.captureCurrent())throw std::runtime_error("fixture admission");
  std::uint64_t intentRevision=0;if(!fanResumeActivityRevision(intentRevision))return false;
  const auto ticket=g_fanResumeRequestOwner.offer(1,fanResumeRequestPowerStamp(capture.power),start,manual,intentRevision,capture.writeRevision);
  if(!ticket)return false;
  const auto view=g_fanResumeRequestOwner.snapshot(ticket);
  const auto key=g_fanResumeRequestOwner.requestKey(ticket);
  requestViews.push_back({{"request",ticket.request},{"run",ticket.run},{"deadline",view->deadlineTick},{"started",view->startedTick},{"attempts",view->attempts},{"mayPost",view->mayPost},{"keyLow",std::to_string(key->first)},{"keyHigh",std::to_string(key->second)}});
  fanHostEmergencyResume(1,ticket);return true;
 };
 if(mode=="f5-budget-post"||mode=="f5-budget-get"){
  (void)invoke(GetTickCount64()-14900);
 }else if(mode=="f5-queue-expired"){
  FanHttpPowerAdmission capture;capture.resume=true;if(!capture.captureCurrent())return 8;
  const auto ticket=g_fanResumeRequestOwner.offer(1,fanResumeRequestPowerStamp(capture.power),GetTickCount64()-14999,0,static_cast<std::uint64_t>(g_fanActivity.revision),capture.writeRevision);
  Sleep(30);fanHostEmergencyResume(1,ticket);++sharedNotification;++sharedPublication;
  FanHttpPowerAdmission fresh;fresh.resume=true;fresh.captureCurrent();
  reopened=!!g_fanResumeRequestOwner.offer(1,fanResumeRequestPowerStamp(fresh.power),GetTickCount64(),0,static_cast<std::uint64_t>(g_fanActivity.revision),fresh.writeRevision);
 }else if(mode=="f5-ready-unknown"){
  (void)invoke(GetTickCount64());fixtureCase="f5-observe-only-successor";(void)invoke(GetTickCount64());
 }else if(mode=="f5-three-slots"){
  for(int i=0;i<3;++i)(void)invoke(GetTickCount64());fixtureCase="f5-observe-only-successor";(void)invoke(GetTickCount64());
 }else if(mode=="f5-terminal"){
  (void)invoke(GetTickCount64());reopened=invoke(GetTickCount64());
 }else if(mode=="get")ok=fanHostEmergencyGetState(&body,&status);
 else if(mode=="suspend")ok=fanHostEmergencySuspend("fixture",1);
 else if(mode=="resume"||mode=="resume-retry"){
  fanHostEmergencyResume(1);
  if(mode=="resume-retry"){
   fixtureCase="normal";
   FanHttpPowerAdmission fresh;fresh.resume=true;
   if(fresh.captureCurrent()){
    std::uint64_t intent=0;
    if(fanResumeActivityRevision(intent)){
     const auto ticket=g_fanResumeRequestOwner.offer(1,fanResumeRequestPowerStamp(fresh.power),GetTickCount64(),++g_fanResumeManualOperation,intent,fresh.writeRevision);
     if(ticket)fanHostEmergencyResume(1,ticket);
    }
   }
  }
  if(!lifecycle.empty())for(const auto& row:lifecycle)if(row["event"]=="fan-host-resume-native-trigger")ok=row["details"].value("terminal",std::string{})=="recovered";
 }
 else ok=fanHostEmergencyPost(mode=="post-safe"?L"/api/close":L"/api/resume","fixture",2,mode=="post-safe",&status,1,"native.power",&body);
 if(sharedCancel.joinable())sharedCancel.join();
 std::cout<<json{{"ok",ok},{"status",status},{"bodyBytes",body.size()},{"bodyParsed",!body.empty()&&!json::parse(body,nullptr,false).is_discarded()},{"elapsedMs",GetTickCount64()-started},{"availabilityQueries",availabilityQueries},{"authRejected",authRejected},{"headerFailed",headerFailed},{"leaseArms",leaseArms},{"fixtureClaims",fixtureBusinessClaims},{"lifecycle",lifecycle},{"requestViews",requestViews},{"reopened",reopened},{"unexpectedDrains",unexpectedDrains}}.dump()<<"\n";return 0;
}

static int businessMain(int argc,char** argv) try {
 if(argc!=4)return 9;fixturePort=static_cast<INTERNET_PORT>(std::stoi(argv[1]));fixtureCase=argv[3];
 fixtureClaimBusiness=true;
 using namespace ymcc::sleeppower::remote;
 fixtureThreadGate=CreateEventW(nullptr,TRUE,TRUE,nullptr);if(!fixtureThreadGate)return 8;
 ProcessHold self;if(!self.bind(GetCurrentProcess()))return 7;fixtureChosenHost=self.get();
 std::vector<json> settlements,replies;
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);NativeRemoteTransport transport(provider);fixtureTransport=&transport;fixtureProvider=&provider;
  if(fixtureCase=="business-scheduler-zero-action")fixtureFailCreate=true;
  fanHostScheduleFanResume(1);fixtureJoin();
  if(fixtureCase=="business-awaiting-old-superseded"){
   fixtureRegisterManual();g_resumeReadyGeneration=1;
   const json observer={{"generation",1u},{"observedWakeOnly",true}};
   sharedPowerPending=true;replies.push_back(fixtureManual(observer));
   sharedPowerPending=false;replies.push_back(fixtureManual(observer));fixtureJoin();
  }
  for(const auto& captured:formalCaptures){const Key128 scope{std::stoull(captured["scopeLow"].get<std::string>()),std::stoull(captured["scopeHigh"].get<std::string>())};DescriptorV1 descriptor;
   if(!provider.describe(scope,self.get(),descriptor))throw std::runtime_error("business scope retained through observation: scope="+std::to_string(scope.low)+", outstanding="+std::to_string(provider.outstanding()));
   SettlementV1 receipt;receipt.scope=descriptor.scope;receipt.request=descriptor.request;receipt.provider=descriptor.provider;receipt.consumer=descriptor.consumer;receipt.outcome=SettlementOutcome::Returned;receipt.flags=AllReturned;
   settlements.push_back({{"accepted",provider.settle(self.get(),receipt)},{"remaining",provider.outstanding()}});
  }transport.stop();fixtureTransport=nullptr;fixtureProvider=nullptr;
 }
 const bool closed=CloseHandle(fixtureThreadGate)!=FALSE;
 std::cout<<json{{"kind",fixtureCase},{"constructed",fixtureConstructed.load()},{"created",fixtureCreated.load()},{"returned",fixtureReturned.load()},{"gateClosed",closed},{"captures",formalCaptures},{"tickets",fixtureTickets},{"leaseArms",leaseArms},{"lifecycle",lifecycle},{"settlements",settlements},{"replies",replies},{"manualToken",g_fanResumeManualOperation.load()},{"fixtureClaims",fixtureBusinessClaims},{"actualHostImporter",false},{"controlledOriginalNativeClockOnly",true},{"clockAdvancedAfterObservationSleeps",fixtureObservationSleeps.load()}}.dump()<<"\n";return 0;
} catch (const std::exception& error) {
 std::cerr<<error.what()<<"\n";
 return 10;
}
int main(int argc,char** argv){
 if(argc!=4)return 9;
 if(std::string(argv[2])=="business")return businessMain(argc,argv);
 using namespace ymcc::sleeppower::remote;
 fixtureThreadGate=CreateEventW(nullptr,TRUE,TRUE,nullptr);if(!fixtureThreadGate)return 8;
 ProcessHold self;if(!self.bind(GetCurrentProcess()))return 7;fixtureChosenHost=self.get();
 int result=0;{
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);NativeRemoteTransport transport(provider);fixtureTransport=&transport;fixtureProvider=&provider;
  result=legacyMain(argc,argv);transport.stop();provider.close();fixtureTransport=nullptr;fixtureProvider=nullptr;
 }
 fixtureChosenHost=nullptr;CloseHandle(fixtureThreadGate);fixtureThreadGate=nullptr;return result;
}
