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
#include "game_feedback_dispatch.h"
#include <condition_variable>
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
static std::wstring g_inputHostPipeName;
static std::string g_inputHostRunId,g_inputHostStartupNonce="fake-nonce",g_inputHostHostInstanceId;
static std::string fixturePersona="steamdeck";
static ymcc::game_feedback::Inbox g_inputHostGameMotorInbox;
// Real revoke consumer with an owned empty serial mailbox; no physical writer in this request/receipt fixture.
static std::atomic<unsigned long long> g_inputHostGameMotorBindingGeneration{0};
static std::mutex g_gamepadSerialMx;
static std::condition_variable g_gamepadSerialCv;
static ymcc::game_feedback::Dispatch g_gamepadSerialGameFeedback;
static std::atomic<bool> g_gamepadSerialGameInterested{false};
@@GAME_REVOKE@@
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
// Environmental source snapshot only; admission decisions below are current production functions.
namespace ymcc::legion_motion {struct Sample{};}
namespace ymcc::windows_motion {struct Stamp{};}
// Explicit fixture intent: existing original cases exercise an enabled virtual target.
static struct { bool enabled=true,virtualTargetEnabled=true; } g_realStickTest;
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_inputHostVirtualIntentWriters{0};
static bool g_inputHostPrepared=false,g_inputHostNeutralized=false;
static auto currentPowerGeneration(){return 3ULL;}
static bool sgVirtualWriterAllowedNow(){return true;}
static bool inputHostExitFenceRaised(){return false;}
static bool inputHostPhysicalPublicationSourceAdmitted(bool admitted,bool neutral){return admitted&&!neutral;}
@@CAPTURE_TYPE@@
static thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;
static thread_local bool t_inputFrameTransportGuard=false;
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG){return false;}
static bool inputHostWindowsMotionSubmissionCurrent(InputFrameSubmission& f){return f.windowsMotion==nullptr;}
static void inputHostFrameSubmissionNote(ymcc::input::frameSubmission::Verdict,const char*,bool){}
@@FRAME_ADMISSION@@
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
static void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(std::string(message)+" win32="+std::to_string(GetLastError()));}
static std::wstring quote(const std::wstring& s){require(s.find_first_of(L"\"\r\n")==std::wstring::npos,"unsafe-owned-child-path");return L"\""+s+L"\"";}
struct Session{
 HANDLE child=nullptr;std::filesystem::path receipt;bool finished=false;
 Session(const std::wstring& dotnet,const std::wstring& dll,const std::filesystem::path& root,const std::string& id,const std::string& selected,const std::string& fault){
  fixtureBeginTargetReturnCase(root);
  g_inputHostPrepared=g_inputHostNeutralized=false;g_inputHostGameMotorInbox.revoke();fixturePersona=selected;g_inputHostSequence=0;g_inputHostEpoch+=1;g_inputHostHostInstanceId.clear();g_inputHostRunId="offline-integration-"+id;
  g_inputHostHello=g_inputHostPipeConnected=g_inputHostPipeRetired=g_inputHostTransportFault=false;queued.clear();commands=json::array();
  g_inputHostPipeName=L"YMCC-private-integration-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+U2W(id);
  g_inputHostServerPipe=inputHostCreateServerPipeLocked();require(g_inputHostServerPipe!=nullptr,"actual-owned-server-factory");
  receipt=root/(id+"-managed.json");
  std::wstring command=quote(dotnet)+L" "+quote(dll)+L" --pipe "+quote(g_inputHostPipeName)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --receipt "+quote(receipt.wstring());
  if(fault!="none")command+=L" --fault "+U2W(fault);
  STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  require(CreateProcessW(dotnet.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"owned-managed-child-start");CloseHandle(pi.hThread);child=pi.hProcess;g_inputHostProcess=child;
 }
 Session(const Session&)=delete;
 ~Session(){
  if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
  if(child){
   const auto pid=GetProcessId(child);const auto wait=WaitForSingleObject(child,5000);const bool natural=wait==WAIT_OBJECT_0;
   if(!natural){TerminateProcess(child,93);WaitForSingleObject(child,2000);}
   DWORD exitCode=999;const bool known=GetExitCodeProcess(child,&exitCode)!=FALSE;
   logs.push_back({{"event","fixture-owned-child-exit"},{"pid",pid},{"natural",natural},{"forcedTermination",!natural},{"exitCodeKnown",known},{"exitCode",exitCode},{"finishObserved",finished},{"receipt",W2U(receipt.wstring())}});
   CloseHandle(child);
  }
  g_inputHostProcess=nullptr;
 }
 json finish(DWORD expectedExit=0){
  require(WaitForSingleObject(child,5000)==WAIT_OBJECT_0,"owned-child-natural-exit");DWORD exit=999;require(GetExitCodeProcess(child,&exit)!=FALSE&&exit==expectedExit,"managed-child-exit-exact");finished=true;
  std::ifstream stream(receipt);require(bool(stream),"managed-final-receipt-present");json value;stream>>value;require(value.is_object(),"managed-final-receipt-object");return value;
 }
};
static bool send(const char* command,const char* expected,json payload=json::object(),bool fast=false){
 const auto before=g_inputHostSequence;json response;InputHostFastReceipt f;inputHostSelfTestRules::ReceiptObservation observation;
 const bool ok=inputHostRequestPreparedLocked(command,payload,expected,4000,&response,nullptr,nullptr,nullptr,fast?&f:nullptr,true,&observation);
 const auto targetDuty=fixtureObserveTargetReturnReservation(command,ok);
 if(std::strcmp(command,"HELLO")==0&&ok){g_inputHostHello=true;ULONG clientPid=0;require(GetNamedPipeClientProcessId(g_inputHostServerPipe,&clientPid)!=FALSE&&clientPid==GetProcessId(g_inputHostProcess)&&clientPid!=GetCurrentProcessId(),"actual-private-pipe-connected-to-owned-managed-process");}
 if(ok&&std::strcmp(command,"PREPARE_TARGET")==0)g_inputHostPrepared=true;
 if(ok&&std::strcmp(command,"SUBMIT_NEUTRAL")==0)g_inputHostNeutralized=true;
 commands.push_back({{"historicalTargetReservation",targetDuty},{"gameReceipt",{{"received",g_inputHostGameMotorInbox.snapshot().received},{"rejected",g_inputHostGameMotorInbox.snapshot().rejected}}},{"motorFast",{{"present",f.gameMotor.present},{"valid",f.gameMotor.valid},{"left",f.gameMotor.left},{"right",f.gameMotor.right}}},{"command",command},{"ok",ok},{"sequenceBefore",before},{"sequenceAfter",g_inputHostSequence},{"response",response},{"fastUsed",f.used},{"tupleVerified",observation.tupleVerified},{"transportFault",g_inputHostTransportFault}});return ok;
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
  require(!queued.empty()&&queued.back()=="tuple-mismatch-reopen:"+std::to_string(g_inputHostEpoch),"actual-unbound-ACK-recovery-intent-recorded");
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
int OriginalLifecycleMain(int argc,wchar_t**argv){
 try{
  require(argc==4,"dotnet-managedDll-evidenceRoot-required");std::filesystem::path root(argv[3]);std::filesystem::create_directories(root);
  for(const auto& entry:std::vector<std::vector<std::string>>{{"steamdeck","steamdeck","none"},{"dualsense-edge","dualsense-edge","none"},{"elite","elite","none"},{"recreate-loop","steamdeck","none"},{"release-retry","steamdeck","output-stop-once"},{"pipe-break","steamdeck","none"},{"stale-host-binding","steamdeck","none"}})runCase(argv[1],argv[2],root,entry[0],entry[1],entry[2]);
  if(g_inputHostServerPipe){inputHostPipeCloseOwned(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
  auto* owner=ymcc::inputHost::pipeTransport::existingProcessRuntime();require(owner!=nullptr,"actual-transport-owner-present");owner->maintenance();const auto s=owner->snapshot();require(s.available&&s.pending==0&&s.payloadBytes==0,"actual-transport-final-pending-zero");
  json result={{"status","PASS"},{"passed",true},{"checks",checks},{"cases",cases},{"logs",logs},{"nativeTransport",{{"pending",s.pending},{"payloadBytes",s.payloadBytes},{"admitted",s.admitted},{"reaped",s.reaped}}},{"productionRequestExecuted",true},{"realWindowsPipes",true},{"actualManagedSession",true},{"sdkOrHardwareIo",false},{"productMainExecuted",false}};
  std::ofstream(root/"result.json")<<result.dump(2);std::cout<<json({{"status","PASS"},{"checks",checks},{"cases",cases.size()}}).dump()<<'\n';return 0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';std::cout<<json({{"status","FAIL"},{"error",ex.what()},{"checks",checks},{"completedCases",cases},{"currentCommands",commands},{"logs",logs}}).dump(2)<<'\n';return 1;}
}

#include "NativeMotorPipeTests.cpp"
