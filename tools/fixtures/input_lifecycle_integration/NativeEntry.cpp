// Real private Windows pipe to the source-exact managed consumer. No SDK/devices.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <wincrypt.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "game_feedback_receipt.h"
#include "HcControllerFeedbackCapabilityCatalog.h"
#include "input_host_pipe_transport.h"
#include "input_host_transaction_mutex.h"
#include "input_host_transport_state.h"
#include "input_host_selftest_rules.h"
#include "input_frame_submission.h"
#include "controller_persona.h"
#include "canonical_frame_fingerprint_memo.h"
#include "msi_input_host_transport_evidence.h"
#include "msi_steamdeck_release_fence_1726.h"
#include "input_chain_command_evidence.h"
using json=nlohmann::json;
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;
static InputHostMutex g_inputHostMx;
static HANDLE g_inputHostProcess=nullptr,g_inputHostServerPipe=nullptr;
static HANDLE g_inputHostUnadoptedPipe=nullptr;
static bool g_inputHostReleaseQuarantined=false,g_inputHostStartBlocked=false,g_inputHostPrepareMayHaveReachedHost=false;
static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;
static DWORD g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;
// Explicit leaves: no player transaction, capture ticket or durable target
// ledger is owned by these wire scenarios. Full R3/target-return consumers are
// separate. The actual Request body, command deadlines and pipe owner execute.
struct FixtureWireTuple { std::uint64_t epoch=0,power=0,requestId=0,sequence=0,acceptedSerial=0; bool protectedCycle=false; };
static std::uint64_t inputHostStartProtectionNow() noexcept{return GetTickCount64();}
static FixtureWireTuple inputHostTupleRecoveryTokenLocked(std::uint64_t epoch,std::uint64_t sequence){return {epoch,3,0,sequence,0,false};}
static bool inputHostTupleRecoveryCurrentLocked(const FixtureWireTuple&) noexcept{return false;}
static std::string inputHostTupleRecoveryReason(const FixtureWireTuple& token){return "tuple-mismatch-reopen:"+std::to_string(token.epoch);}
static void inputHostStartProtectionFinish(std::uint64_t,const char*,const char*) noexcept{std::terminate();}
static void inputHostSwitchVerificationForgetAcceptedLocked() noexcept{}
static bool inputHostTargetReturnBeforeCreateLocked(const char*,unsigned long long){return true;}
static bool inputHostPipeRetirementProvenLocked() noexcept;
namespace ymcc::inputStartupDiagnostics { static void appendAcceptedPrepare(json&,const json&,const char*){} }

static std::wstring g_inputHostPipeName;
static std::string g_inputHostRunId,g_inputHostStartupNonce="fake-nonce",g_inputHostHostInstanceId;
static std::string fixturePersona="steamdeck";
static ymcc::game_feedback::Inbox g_inputHostGameMotorInbox;
static std::atomic<unsigned long long> g_inputHostGameMotorBindingGeneration{0};
// Serial motor output remains an explicit inert terminal in this wire fixture.
static void gamepadSerialRevokeGameFeedback() {}
static unsigned long long g_inputHostEpoch=17,g_inputHostSequence=0,g_inputHostConfigRevision=1;
static bool g_inputHostHello=false,g_inputHostPipeConnected=false,g_inputHostPipeRetired=false,g_inputHostTransportFault=false;
static std::atomic<bool> g_inputHostTupleHealPending{false};
static std::atomic<unsigned long long> g_inputCapturePreHostElapsedUs{0},g_inputHostLastSubmitElapsedUs{0};
static std::atomic<int> g_backendInstallPrompt{0};static unsigned g_xinputConnectedMask=0;
enum class YmccFamily{Unknown,MsiClaw};static struct{YmccFamily family=YmccFamily::Unknown;}g_machineIdentity;
static json logs=json::array();static std::vector<std::string> queued;
static void appendNativeLifecycleLog(const char* event,json row=json::object()){row["event"]=event;logs.push_back(std::move(row));}
static void writeLog(const char*,const std::string&){}
static void inputHostSwitchAuditDispatch(const std::string&,const std::string&){}
static void recoverySnapSetPersona(const std::string&){}static void recoverySnapNoteCommand(const char*){}
static bool ymStartPipeSeamServe(const char*,const char*,json*){return false;}
static bool inputHostQueueLifecycleWork(const char* s){queued.push_back(s);return true;}
static bool tickCostEnabled(){return false;}
enum{ kTickCostFrameBuild,kTickCostReceiptValidate,kTickCostPipeWrite,kTickCostPipeRead,kTickCostHostSubmit};
static unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point){return 0;}
static void tickCostAccumulate(int,unsigned long long){}
namespace ymcc::screenpads {static void observeNativeTransport(unsigned long long,bool){}}
// This integration covers wire/dispatch; no normal-frame capture ticket is armed.
struct InputFrameSubmission{ymcc::game_feedback::Sample gameMotorReply{};std::uint64_t gameMotorBinding=0;ymcc::input::frameSubmission::Ticket ticket{};bool physicalSourceAdmitted=false,sourceNeutralized=false;bool rejected=false;ymcc::input::frameSubmission::Verdict reason=ymcc::input::frameSubmission::Verdict::Allowed;};
static thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;
static thread_local bool t_inputFrameTransportGuard=false;
static bool inputHostFrameSubmissionRequestCurrent(const char*,json*,const char*){if(t_inputFrameSubmission)throw std::runtime_error("unexpected-live-capture-ticket");return true;}
static bool inputHostFrameSubmissionMayIssue(void*)noexcept{return false;}
static void inputHostFrameSubmissionTransportRejected()noexcept{std::terminate();}
@@FRAME_SCOPE@@
@@CONSTANTS@@
@@UNICODE@@
@@NEW_ID@@
static const char* inputHostBoundOrCurrentPersona(){return fixturePersona.c_str();}
static const char* inputHostBoundOrCurrentProfileIdentity(){return ymcc::controllerPersona::profileIdentity(fixturePersona);}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 3;}
static const char kC6aHexDigits[]="0123456789abcdef";
@@HASH@@
@@ENVELOPE@@
@@REQUEST_LIFECYCLE@@
@@PIPE@@
@@GAME_MOTOR@@
@@FAST_RECEIPT@@
@@SCANNER@@
@@AFTER_FRAME@@
// Optional MSI diagnostics are disabled; control and ACK validation stay exact.
static void msiNonSdReleaseReceiptLog(json&,const json&,const char*,bool)noexcept{}
@@RELEASE_TIMING_LOG@@
@@COMMAND_RESULT_LOG@@
@@REQUEST@@
static unsigned checks=0;static json cases=json::array(),commands=json::array();
static unsigned privateHostHandles=0,privateParentHandles=0,hostProcessJoins=0,hostPrimaryJoins=0,hostForcedBackstops=0,privateCloseFailures=0;
static void checkedPrivateClose(HANDLE& value,unsigned& count) noexcept{if(value){if(CloseHandle(value)){value=nullptr;--count;}else ++privateCloseFailures;}}
static void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(std::string(message)+" win32="+std::to_string(GetLastError()));}
static std::wstring quote(const std::wstring& s){require(s.find_first_of(L"\"\r\n")==std::wstring::npos,"unsafe-owned-child-path");return L"\""+s+L"\"";}
struct Session{
 HANDLE child=nullptr,primary=nullptr;std::filesystem::path receipt;bool finished=false;
 Session(const std::wstring& dotnet,const std::wstring& dll,const std::filesystem::path& root,const std::string& id,const std::string& selected,const std::string& fault,DWORD watchedParent=0){
  fixturePersona=selected;g_inputHostSequence=0;g_inputHostEpoch+=1;g_inputHostHostInstanceId.clear();g_inputHostRunId="offline-integration-"+id;
  g_inputHostHello=g_inputHostPipeConnected=g_inputHostPipeRetired=g_inputHostTransportFault=false;queued.clear();commands=json::array();
  g_inputHostPipeName=L"YMCC-private-integration-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+U2W(id);
  g_inputHostServerPipe=inputHostCreateServerPipeLocked();require(g_inputHostServerPipe!=nullptr,"actual-owned-server-factory");
  receipt=root/(id+"-managed.json");
  std::wstring command=quote(dotnet)+L" "+quote(dll)+L" --pipe "+quote(g_inputHostPipeName)+L" --parent-pid "+std::to_wstring(watchedParent?watchedParent:GetCurrentProcessId())+L" --receipt "+quote(receipt.wstring());
  if(fault!="none")command+=L" --fault "+U2W(fault);
  STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  require(CreateProcessW(dotnet.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"owned-managed-child-start");primary=pi.hThread;child=pi.hProcess;privateHostHandles+=2;g_inputHostProcess=child;
 }
 Session(const Session&)=delete;
 ~Session(){if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}if(child){if(WaitForSingleObject(child,5000)!=WAIT_OBJECT_0){++hostForcedBackstops;TerminateProcess(child,93);WaitForSingleObject(child,2000);}checkedPrivateClose(child,privateHostHandles);}if(primary){if(WaitForSingleObject(primary,0)!=WAIT_OBJECT_0)++privateCloseFailures;checkedPrivateClose(primary,privateHostHandles);}g_inputHostProcess=nullptr;}
 json finish(DWORD expectedExit=0){
  require(WaitForSingleObject(child,5000)==WAIT_OBJECT_0,"owned-child-natural-exit");DWORD exit=999;require(GetExitCodeProcess(child,&exit)!=FALSE&&exit==expectedExit,"managed-child-exit-exact");++hostProcessJoins;require(WaitForSingleObject(primary,1000)==WAIT_OBJECT_0,"managed-primary-thread-exit-before-parent");++hostPrimaryJoins;finished=true;
  std::ifstream stream(receipt);require(bool(stream),"managed-final-receipt-present");json value;stream>>value;require(value.is_object(),"managed-final-receipt-object");return value;
 }
};
static bool send(const char* command,const char* expected,json payload=json::object(),bool fast=false){
 const auto before=g_inputHostSequence;json response;InputHostFastReceipt f;inputHostSelfTestRules::ReceiptObservation observation;
 const bool ok=inputHostRequestPreparedLocked(command,payload,expected,4000,&response,nullptr,nullptr,nullptr,fast?&f:nullptr,true,&observation);
 if(std::strcmp(command,"HELLO")==0&&ok){g_inputHostHello=true;ULONG clientPid=0;require(GetNamedPipeClientProcessId(g_inputHostServerPipe,&clientPid)!=FALSE&&clientPid==GetProcessId(g_inputHostProcess)&&clientPid!=GetCurrentProcessId(),"actual-private-pipe-connected-to-owned-managed-process");}
 commands.push_back({{"command",command},{"ok",ok},{"sequenceBefore",before},{"sequenceAfter",g_inputHostSequence},{"response",response},{"fastUsed",f.used},{"tupleVerified",observation.tupleVerified},{"transportFault",g_inputHostTransportFault}});return ok;
}
static json framePayload(){return {{"buttons",0},{"lx",0.0},{"ly",0.0},{"rx",0.0},{"ry",0.0},{"lt",0.0},{"rt",0.0},{"sampleSequence",g_inputHostSequence+1},{"monotonicTick",GetTickCount64()},{"motionPairProofId","offline-pipe-contract"},{"normalizedInputHash",inputHostCanonicalFrameHash(0,0,0,0,0,0,0)}};}
static json readPrepared(){require(send("HELLO","host-hello"),"actual-HELLO-accepted");require(!g_inputHostHostInstanceId.empty(),"actual-HELLO-binds-instance");require(send("PREPARE_TARGET","target-prepared",fixturePersona=="steamdeck"?json{{"msiSteamDeckReleaseFence",true}}:json::object()),"actual-PREPARE-accepted");return commands.back().at("response");}
static void activeFrames(){require(send("SUBMIT_NEUTRAL","neutral-accepted"),"actual-NEUTRAL-accepted");require(send("SUBMIT_FRAME","frame-accepted",framePayload()),"actual-DOM-FRAME-accepted");require(send("SUBMIT_FRAME","frame-accepted",framePayload(),true),"actual-fast-FRAME-accepted");require(commands.back().at("fastUsed")==true,"actual-fast-scanner-used");}
static void closeCommands(){require(send("RELEASE_TARGET","release-local-dispose-complete"),"actual-RELEASE-accepted");require(send("SHUTDOWN","shutdown"),"actual-SHUTDOWN-accepted");}
static void runCase(const std::wstring& dotnet,const std::wstring& dll,const std::filesystem::path& root,const std::string& id,const std::string& selected,const std::string& fault){
 const unsigned start=checks;Session session(dotnet,dll,root,id,selected,fault);json prepared=readPrepared();activeFrames();
 ymcc::msi::sdRelease1726::Session fence;if(selected=="steamdeck"){fence.hello(commands[0]["response"]);require(fence.begin(true,selected,g_inputHostRunId,g_inputHostEpoch)&&fence.acceptPrepare(prepared),"actual-native-MSI-owned-target-pin");}
 if(id=="recreate-loop"){
  for(int i=0;i<8;++i){require(send("RECREATE_TARGET","target-prepared"),"actual-RECREATE-cycle-accepted");activeFrames();}
 }
 if(id=="stale-host-binding"){
  g_inputHostHostInstanceId="foreign-host-instance";const auto before=g_inputHostSequence;
  require(!send("QUIESCE","quiesced"),"stale-Host-instance-cannot-accept-ACK");
  require(!g_inputHostTransportFault&&g_inputHostSequence==before,"unbound-ACK-does-not-certify-transport-fault-or-advance-sequence");
  require(queued.empty()&&logs.back().value("stage",std::string{})=="refused"&&logs.back().value("reason",std::string{})=="identity-or-asset-unproven","actual-unbound-ACK-cannot-authorize-recovery");
 }else require(send("QUIESCE","quiesced"),"actual-QUIESCE-accepted");
 const auto beforeRejected=g_inputHostSequence;
 if(id!="stale-host-binding"){
  require(!send("SUBMIT_FRAME","frame-accepted",framePayload()),"post-QUIESCE-frame-rejected");require(!g_inputHostTransportFault&&g_inputHostSequence==beforeRejected+1,"business-refusal-sequence-synced-without-transport-fault");
 }
 if((id=="pipe-break"||id=="stale-host-binding")){
  inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;g_inputHostPipeConnected=false;
 }else{
  if(fault=="output-stop-once"){
   const auto before=g_inputHostSequence;require(!send("RELEASE_TARGET","release-local-dispose-complete"),"failed-stop-cannot-ack-release");require(g_inputHostSequence==before+1&&!g_inputHostTransportFault,"failed-release-consumes-actual-sequence");
  }
  closeCommands();
  if(selected=="steamdeck"&&id!="recreate-loop"){
   require(fence.acceptRelease(commands[commands.size()-2]["response"]),"actual-native-MSI-release-proof-consumed");require(fence.verified(g_inputHostRunId,g_inputHostEpoch),"actual-native-MSI-exact-tuple-absence");
  }
 }
 const auto child=session.finish((id=="pipe-break"||id=="stale-host-binding")?5:0);
 require(child.at("processId")==GetProcessId(session.child)&&child.at("parentPid")==GetCurrentProcessId(),"actual-managed-receipt-process-and-parent-bound");
 require(child.at("creates").get<unsigned>()>0,"actual-managed-prepare-created-fixture-target");
 require(child.at("status")=="OBSERVED" && child.at("failure").is_null(),"managed-bound-observation-complete");
 require(child.at("pipeLoopReturned")==true && child.at("pipeLoopExitCode")==((id=="pipe-break"||id=="stale-host-binding")?5:0),"actual-managed-loop-exit-code");
 require(child.at("hasController")==false && child.at("hasContext")==false && child.at("finalCleanupOk")==true,"actual-managed-resources-released");
 require(child.at("stateAfterLoop").at("lastSequence")==g_inputHostSequence+(id=="stale-host-binding"?1:0),"actual-native-managed-final-sequence-equal");
 require(child.at("faultConsumed")== (fault=="output-stop-once"),"managed-fault-triggered-only-when-requested");
 if(fault=="output-stop-once"){
  const auto& stops=child.at("outputStopResults");require(stops.size()>=2&&stops[0]["result"]==false&&stops[1]["result"]==true,"failed-stop-actually-retried-and-succeeded");
  require(stops[0]["stateBeforeCallerUpdatesPendingFlags"]["hasController"]==true,"failed-stop-retained-real-fixture-controller");
  const auto first=stops[0]["eventIndex"].get<unsigned>(),next=stops[1]["eventIndex"].get<unsigned>();
  for(const auto& event:child.at("eventTrace")){const auto index=event["index"].get<unsigned>();if(index>first&&index<next)require(event["value"]!="controller-dispose"&&event["value"]!="context-dispose","failed-stop-no-disposal-before-next-stop-barrier");}
 }

 // Managed side is authoritative for final object ownership; only fake SDK effects.
 cases.push_back({{"id",id},{"persona",selected},{"passed",true},{"checks",checks-start},{"managedReceipt",child},{"commands",commands},{"nativeTransportFault",g_inputHostTransportFault},{"queuedRecovery",queued}});
}

// The supervisor retains the control pipe while a separate owned process is
// watched by the production ParentProcessLifetime. Its exit cannot be confused
// with a server-side pipe close or a RELEASE_TARGET/SHUTDOWN command.
struct LifetimeParent {
 HANDLE process=nullptr,stopEvent=nullptr,primary=nullptr;DWORD pid=0;std::string creation;
 LifetimeParent(){
  const auto event=L"Local\\YMCC-private-lifetime-parent-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  stopEvent=CreateEventW(nullptr,TRUE,FALSE,event.c_str());require(stopEvent!=nullptr,"owned-parent-stop-event-created");++privateParentHandles;
  wchar_t executable[32768]{};const DWORD count=GetModuleFileNameW(nullptr,executable,32768);require(count>0&&count<32768,"owned-parent-executable-resolved");
  std::wstring command=quote(executable)+L" --lifetime-parent "+quote(event);STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"owned-lifetime-parent-started");
  primary=pi.hThread;process=pi.hProcess;privateParentHandles+=2;pid=pi.dwProcessId;
  FILETIME c{},e{},k{},u{};require(GetProcessTimes(process,&c,&e,&k,&u)!=FALSE,"owned-parent-creation-time-readable");
  ULARGE_INTEGER value{};value.LowPart=c.dwLowDateTime;value.HighPart=c.dwHighDateTime;creation=std::to_string(value.QuadPart);
  require(WaitForSingleObject(process,0)==WAIT_TIMEOUT,"watched-parent-live-before-session");
 }
 void exit(bool abrupt){
  require(GetProcessId(process)==pid,"only-original-owned-parent-can-exit");
  require(abrupt?TerminateProcess(process,73)!=FALSE:SetEvent(stopEvent)!=FALSE,"owned-parent-exit-requested");
  require(WaitForSingleObject(process,5000)==WAIT_OBJECT_0,"owned-parent-exit-observed");DWORD code=999;
  require(GetExitCodeProcess(process,&code)!=FALSE&&code==(abrupt?73ul:0ul),"owned-parent-exact-exit-code");
 }
 ~LifetimeParent(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){SetEvent(stopEvent);if(WaitForSingleObject(process,5000)!=WAIT_OBJECT_0){++hostForcedBackstops;TerminateProcess(process,94);WaitForSingleObject(process,2000);}}checkedPrivateClose(process,privateParentHandles);}if(primary){if(WaitForSingleObject(primary,0)!=WAIT_OBJECT_0)++privateCloseFailures;checkedPrivateClose(primary,privateParentHandles);}checkedPrivateClose(stopEvent,privateParentHandles);}
};
static void runParentExitCase(const std::wstring& dotnet,const std::wstring& dll,const std::filesystem::path& root,const std::string& selected,bool abrupt,bool retry){
 const unsigned start=checks;const std::string id=std::string(abrupt?"parent-kill-":"parent-exit-")+(retry?"retry-":"")+selected;
 LifetimeParent parent;Session session(dotnet,dll,root,id,selected,retry?"output-stop-once":"none",parent.pid);
 (void)readPrepared();activeFrames();
 require(commands.size()==5&&commands.back().at("command")=="SUBMIT_FRAME","parent-exit-begins-with-live-target-and-accepted-frame");
 const auto before=GetTickCount64();parent.exit(abrupt);const auto child=session.finish();const auto latency=GetTickCount64()-before;
 DWORD flags=0;require(g_inputHostServerPipe!=nullptr&&GetHandleInformation(g_inputHostServerPipe,&flags)!=FALSE,"supervisor-kept-server-open-until-host-natural-exit");
 require(!g_inputHostTransportFault&&!g_inputHostPipeRetired,"no-native-stop-or-pipe-fault-triggered-parent-cleanup");
 require(child.at("processId")==GetProcessId(session.child)&&child.at("parentPid")==parent.pid&&parent.pid!=GetCurrentProcessId(),"managed-host-watched-exact-owned-parent-not-supervisor");
 require(child.at("status")=="OBSERVED"&&child.at("failure").is_null()&&child.at("creates")==1,"parent-exit-actual-session-observation-complete");
 require(child.at("pipeLoopReturned")==true&&child.at("pipeLoopExitCode")==0,"parent-exit-actual-loop-returns-without-command");
 require(child.at("stateAfterLoop").at("lastSequence")==g_inputHostSequence,"parent-exit-no-hidden-control-command-consumed");
 require(child.at("hasController")==false&&child.at("hasContext")==false&&child.at("finalCleanupOk")==true,"parent-exit-actual-session-final-objects-released");
 require(child.at("actualProductionTerminalSettlement")==true&&child.at("terminalSettlement").at("ManagedReleaseComplete")==true&&child.at("terminalSettlement").at("ExitCode")==0,"parent-exit-unique-terminal-resource-settlement");
 const auto& attempts=child.at("terminalReleaseAttempts");
 const auto& reasons=child.at("exitReasons");require(reasons.size()==1&&reasons[0].at("reason")=="parent-exited","held-open-pipe-cleanup-came-from-bound-parent-exit");
 const auto& events=child.at("events");unsigned controllers=0,contexts=0;for(const auto& e:events){if(e=="controller-dispose")++controllers;if(e=="context-dispose")++contexts;}
 require(controllers==1&&contexts==1,"parent-exit-disposes-each-actual-session-object-once");
 const auto& stops=child.at("outputStopResults");
 require(child.at("faultConsumed")==retry,"parent-exit-only-explicit-stop-fault-consumed");
 if(retry){
  require(attempts.size()==2&&attempts[0].at("ok")==false&&attempts[1].at("ok")==true,"parent-exit-only-one-terminal-retry");
  const auto& failedStop=attempts[0].at("after");
  require(failedStop.at("hasController")==true&&failedStop.at("hasContext")==true&&failedStop.at("localReleasePending")==true,"parent-exit-failed-first-stop-retains-controller-context-and-debt");
  require(stops.size()==2&&stops[0].at("result")==false&&stops[1].at("result")==true,"parent-exit-final-cleanup-retries-real-stop-barrier");
  const auto first=stops[0].at("eventIndex").get<unsigned>(),next=stops[1].at("eventIndex").get<unsigned>();
  for(const auto& event:child.at("eventTrace")){const auto index=event.at("index").get<unsigned>();if(index>first&&index<next)require(event.at("value")!="controller-dispose"&&event.at("value")!="context-dispose","parent-exit-failed-stop-never-disposes-before-confirmed-barrier");}
 }else require(child.at("stateAfterLoop").at("hasController")==true&&child.at("stateAfterLoop").at("hasContext")==true&&attempts.size()==1&&attempts[0].at("after").at("hasController")==false&&attempts[0].at("after").at("hasContext")==false&&stops.size()==1&&stops[0].at("result")==true,"parent-exit-unique-terminal-settlement-completes-held-object-cleanup");
 cases.push_back({{"id",id},{"persona",selected},{"passed",true},{"checks",checks-start},{"managedReceipt",child},{"commands",commands},{"nativeTransportFault",g_inputHostTransportFault},{"queuedRecovery",queued},{"parentLifetime",{{"actualParentHandle",true},{"watchedPid",parent.pid},{"creationTime",parent.creation},{"supervisorPid",GetCurrentProcessId()},{"abruptExit",abrupt},{"firstStopFailure",retry},{"serverHeldOpenUntilHostExit",true},{"hostNaturalExitWithinMs",latency},{"nativeReleaseCommands",0},{"sdkBoundary","explicit-fixture-endpoint"}}}});
}

int wmain(int argc,wchar_t**argv){
 try{
  if(argc==3&&std::wstring(argv[1])==L"--lifetime-parent"){HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!stop)return 71;const DWORD wait=WaitForSingleObject(stop,30000);CloseHandle(stop);return wait==WAIT_OBJECT_0?0:72;}
  require(argc==4,"dotnet-managedDll-evidenceRoot-required");std::filesystem::path root(argv[3]);std::filesystem::create_directories(root);
  for(const auto& entry:std::vector<std::vector<std::string>>{{"steamdeck","steamdeck","none"},{"dualsense-edge","dualsense-edge","none"},{"elite","elite","none"},{"recreate-loop","steamdeck","none"},{"release-retry","steamdeck","output-stop-once"},{"pipe-break","steamdeck","none"},{"stale-host-binding","steamdeck","none"}})runCase(argv[1],argv[2],root,entry[0],entry[1],entry[2]);
  for(const auto& persona:std::vector<std::string>{"steamdeck","dualsense-edge","elite"}){runParentExitCase(argv[1],argv[2],root,persona,false,false);runParentExitCase(argv[1],argv[2],root,persona,true,false);runParentExitCase(argv[1],argv[2],root,persona,true,true);}
  if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
  auto* owner=ymcc::inputHost::pipeTransport::existingProcessRuntime();require(owner!=nullptr,"actual-transport-owner-present");owner->maintenance();const auto s=owner->snapshot();require(s.available&&s.pending==0&&s.payloadBytes==0,"actual-transport-final-pending-zero");
  require(inputHostPipeRetirementProvenLocked()&&s.stopped&&s.ownedOriginals==0&&s.ownedDuplicates==0&&s.ownedEvents==0&&s.failedCloseDebt==0&&s.retainedOperations==0&&s.cachedOperations==0,"actual-transport-all-owned-resources-returned");
  require(privateHostHandles==0&&privateParentHandles==0&&privateCloseFailures==0&&hostForcedBackstops==0&&hostProcessJoins==16&&hostPrimaryJoins==16,"all-private-process-thread-handles-returned-before-parent-exit");
  json result={{"status","PASS"},{"passed",true},{"checks",checks},{"cases",cases},{"logs",logs},{"nativeTransport",{{"pending",s.pending},{"payloadBytes",s.payloadBytes},{"admitted",s.admitted},{"reaped",s.reaped},{"stopped",s.stopped},{"ownedOriginals",s.ownedOriginals},{"ownedDuplicates",s.ownedDuplicates},{"ownedEvents",s.ownedEvents},{"failedCloseDebt",s.failedCloseDebt},{"retainedOperations",s.retainedOperations},{"cachedOperations",s.cachedOperations}}},{"beforeParentExitResources",{{"privateHostHandles",privateHostHandles},{"privateParentHandles",privateParentHandles},{"closeFailures",privateCloseFailures},{"hostForcedBackstops",hostForcedBackstops},{"hostProcessJoins",hostProcessJoins},{"hostPrimaryJoins",hostPrimaryJoins},{"processExitUsedAsCleanup",false}}},{"productionRequestExecuted",true},{"realWindowsPipes",true},{"actualManagedSession",true},{"sdkOrHardwareIo",false},{"productMainExecuted",false}};
  std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json({{"status","PASS"},{"checks",checks},{"cases",cases.size()}}).dump()<<'\n';return 0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';std::cout<<json({{"status","FAIL"},{"error",ex.what()},{"checks",checks},{"completedCases",cases},{"currentCommands",commands},{"logs",logs}}).dump(2)<<'\n';return 1;}
}
