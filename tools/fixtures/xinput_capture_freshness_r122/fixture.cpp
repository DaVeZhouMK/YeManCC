// Offline only. @@ markers receive byte-exact production slices, never rewritten.
// XInput returns, time, lifecycle facts and external I/O are controlled stand-ins.
#include "json.hpp"
#include "gamepad_backend_cost.h"
#include "xinput_identity_refresh_policy.h"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
#include <array>
#include <atomic>
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{1};
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using json=nlohmann::json;
using DWORD=std::uint32_t; using ULONGLONG=std::uint64_t; using SHORT=std::int16_t;
struct XINPUT_GAMEPAD {std::uint16_t wButtons=0;std::uint8_t bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
struct XINPUT_STATE {DWORD dwPacketNumber=0;XINPUT_GAMEPAD Gamepad{};};
struct YmccXInputStateSecret {std::uint16_t wButtons=0;};
constexpr DWORD ERROR_SUCCESS=0,ERROR_DEVICE_NOT_CONNECTED=1167;
constexpr std::uint16_t YMCC_XINPUT_SPECIAL_XBOX=0x0400;
constexpr int XINPUT_GAMEPAD_TRIGGER_THRESHOLD=30;
#define ZeroMemory(p,n) std::memset(p,0,n)
namespace ymcc::msi::virtualReportDiag {struct SourceStamp {};}
@@TYPES@@
@@CONSTANTS@@
static ULONGLONG fakeTick=10000;
static ULONGLONG GetTickCount64(){return fakeTick;}
static std::atomic<unsigned long long> g_powerGeneration{10},g_msiMainSourceRevision{90};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static bool fakeKeepDuringStandby=false;
static bool sgShouldKeepInputDuringStandby(){return fakeKeepDuringStandby;}
static bool tickCostEnabled(){return false;}
struct GpReadScope {~GpReadScope() noexcept {}};
static int t_gpReadSource=0;
constexpr int kGpProbeRealtime=0,kGpProbeDiscovery=1,kGpProbeGuard=2;
static ymcc::input::backendCost::Counters g_gamepadBackendCost;
static std::atomic<unsigned long long> g_gpSlotProbeCanonical[4][3]{},g_gpSlotProbeSecret[4][3]{},g_gpSlotProbeBySlot[4]{},g_gpSlotProbeErrors[4]{};
static std::atomic<unsigned long long> g_gpBoundaryFullScans{0},g_gpDiscoveryPasses[4]{},g_gpDiscoveryDirtyPasses[4]{};
static std::atomic<unsigned long long> g_xinputDiscoveryDirtyTick{0},g_xinputMaxDiscoveryGapMs{0},g_xinputMaxDirtyToProbeMs{0},g_xinputSlotDiscoveryMaxGapMs[4]{};
static ULONGLONG g_xinputSlotDiscoveryLastTick[4]{},g_xinputDiscoveryLastTick=10000;
static std::atomic<unsigned> g_xinputTopologyEpoch{20},g_xinputSlotIdentityEpoch{30},g_xinputSlotFullScanEpochStart{20},g_xinputBoundaryScanEpoch{20};
static std::atomic<bool> g_xinputSlotFullScanOnce{false},g_xinputDiscoveryDirty{false},g_xinputPreTargetBoundaryScanned{false};
static DWORD g_xinputKnownSlotMask=1,g_xinputConnectedMask=0,g_xinputVirtualEndpointMask=0,g_xinputVirtualEndpointProbed=15,g_xinputSlotUnknownMask=0,g_xinputAdmittedMask=0;
static std::atomic<DWORD> g_xinputSlotReprobeMask{0};
static ULONGLONG g_xinputSlotReprobeAt[4]{};
static unsigned g_xinputSlotIdentityResolvedEpoch[4]{};
static unsigned long long g_feedbackSlotGeneration[4]{};
static std::array<XINPUT_GAMEPAD,4> g_xinputPads{};
static std::array<bool,4> g_xinputSpecialButtons{};
static bool g_xinputSpecialButton=false,g_xinputPhysicalSourceLocked=false;
static DWORD g_xinputPhysicalSourceMask=15,g_xinputPrimarySlot=0,g_xinputSemanticSlot=0;
static std::atomic<bool> g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned> g_externalPrioritySourceTopology{20};
static std::atomic<unsigned long long> g_externalPrioritySourcePower{10},g_externalPrioritySourceDeadline{999999};
static std::atomic<DWORD> g_externalPrioritySourceMask{15};
static std::array<ymcc::input::identityRefresh::Slot,4> g_xinputFeedbackIdentity;
static ymcc::input::identityRefresh::DeferredStop g_xinputDeferredFeedbackStop;
static std::array<std::wstring,4> g_msiOwnedReturnSlotPaths;
static std::atomic<unsigned long long> g_msiOwnedReturnSourceEvent{0};
static bool msiClawKnownBoardAuthority(){return false;}
static std::atomic<bool> g_msiDinputRouteDinputArmed{false};
static bool msiDinputMainSourceEligible(){throw std::logic_error("MSI branch out of scope");}
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int){throw std::logic_error("Feedback I/O out of scope");}
static void appendNativeLifecycleLog(const char*,const json&){}
static std::string W2U(const std::wstring& s){std::string out;for(wchar_t c:s)out.push_back(static_cast<char>(c));return out;}
static bool fakeOpenAvailable=true,fakePathReadable=true,fakeVirtualPath=false;
static bool ymccOpenXInputAvailable(){return fakeOpenAvailable;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD){return !fakePathReadable?L"":fakeVirtualPath?L"HIDMAESTRO":L"physical";}
struct ApiResult {DWORD rc=ERROR_DEVICE_NOT_CONNECTED,packet=7,secretRc=ERROR_SUCCESS;XINPUT_GAMEPAD pad{};std::uint16_t secret=0;};
static std::array<ApiResult,4> fakeApi;
static std::array<int,4> canonicalCalls{},ordinalCalls{};
static std::function<void(DWORD)> duringCanonical;
static unsigned totalCanonical=0,totalOrdinal=0,totalReads=0;
static DWORD ymccXInputGetState14(DWORD i,XINPUT_STATE* s){++canonicalCalls[i];++totalCanonical;s->dwPacketNumber=fakeApi[i].packet;s->Gamepad=fakeApi[i].pad;if(duringCanonical)duringCanonical(i);return fakeApi[i].rc;}
static DWORD ymccXInputGetStateSecret14(DWORD i,YmccXInputStateSecret* s){++ordinalCalls[i];++totalOrdinal;s->wButtons=fakeApi[i].secret;return fakeApi[i].secretRc;}
static XINPUT_GAMEPAD xinputMergeConnectedPads(DWORD,DWORD&,bool&){throw std::logic_error("Unexpected permissive merge");}
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false,fakeExitFence=false;
static unsigned long long g_inputHostEpoch=40;
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{50};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static bool inputHostExitFenceRaised(){return fakeExitFence;}
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
// Feedback transport is outside this XInput read fixture; do not bind a writer.
static void inputCaptureStampFeedbackSourceLocked(InputCaptureGamepadSnapshot& sample){sample.feedbackSource={};}
@@METHODS@@
static void readXInputSlices(){
    ++totalReads;
@@READ_PREFIX@@
@@ADMISSION@@
@@SELECTION@@
    // The production snapshot mutex is unnecessary in this single-thread fixture.
    // Sequence storage is local harness plumbing for the unchanged assignment.
    static unsigned long long diagnosticReadSequence=0;
@@STAMP@@
}
using namespace ymcc::input::frameSubmission;
static Ticket ticket(bool neutral=false){return {g_inputHostEpoch,currentPowerGeneration(),g_inputHostVirtualIntentRevision.load(),neutral};}
static Verdict verdict(const Ticket& t){return decide(t,inputHostFrameSubmissionSample(&g_inputCaptureGamepadSnapshot),inputHostFrameSubmissionCurrent(true),fakeTick);}
static unsigned checks=0;
static void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static bool zero(const XINPUT_GAMEPAD& p){return p.wButtons==0&&p.bLeftTrigger==0&&p.bRightTrigger==0&&p.sThumbLX==0&&p.sThumbLY==0&&p.sThumbRX==0&&p.sThumbRY==0;}
static void reset(){
 fakeTick=10000;g_powerGeneration=10;g_xinputTopologyEpoch=20;g_xinputSlotIdentityEpoch=30;g_msiMainSourceRevision=90;
 g_powerLifecycle=PowerLifecycle::Ready;fakeKeepDuringStandby=false;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;
 g_inputHostProcess=true;g_inputHostPrepared=true;g_inputHostNeutralized=true;g_inputHostTransportFault=false;fakeExitFence=false;g_inputHostVirtualIntentWriters=0;
 g_xinputKnownSlotMask=1;g_xinputConnectedMask=0;g_xinputVirtualEndpointMask=0;g_xinputVirtualEndpointProbed=15;g_xinputSlotUnknownMask=0;g_xinputAdmittedMask=0;
 g_xinputSlotFullScanOnce=false;g_xinputDiscoveryDirty=false;g_xinputDiscoveryLastTick=10000;g_xinputSlotReprobeMask=0;
 g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=15;g_xinputPrimarySlot=0;g_xinputSemanticSlot=0;
 g_xinputPads.fill({});g_xinputSpecialButtons.fill(false);g_xinputSpecialButton=false;g_xinputFeedbackIdentity={};
 std::fill_n(g_xinputSlotReprobeAt,4,0);std::fill_n(g_xinputSlotIdentityResolvedEpoch,4,30);std::fill_n(g_feedbackSlotGeneration,4,0);
 fakeOpenAvailable=true;fakePathReadable=true;fakeVirtualPath=false;fakeApi={};fakeApi[0].rc=ERROR_SUCCESS;fakeApi[0].pad={0x1000,120,130,16000,-17000,18000,-19000};
 canonicalCalls.fill(0);ordinalCalls.fill(0);duringCanonical={};g_inputCaptureGamepadSnapshot={};
}
static json rows=json::array();
static void run(const char* name,const std::function<void()>& body){
 reset();const auto before=checks,reads=totalReads,calls=totalCanonical,ordinal=totalOrdinal;
 try{body();rows.push_back({{"name",name},{"passed",true},{"checks",checks-before},{"reads",totalReads-reads},{"canonicalCalls",totalCanonical-calls},{"ordinalCalls",totalOrdinal-ordinal}});}
 catch(const std::exception& e){rows.push_back({{"name",name},{"passed",false},{"error",e.what()},{"checks",checks-before}});}
}
int main(){
 run("same-packet-success-is-read-every-known-tick",[]{
  readXInputSlices();const auto first=g_inputCaptureGamepadSnapshot;check(first.live&&first.pad.wButtons==0x1000,"first live pad");
  fakeTick+=8;fakeApi[0].pad.wButtons=0x2000;readXInputSlices();
  check(canonicalCalls[0]==2&&ordinalCalls[0]==2,"each known tick must read both APIs");check(g_inputCaptureGamepadSnapshot.pad.wButtons==0x2000,"same packet must use actual second read");
  check(g_inputCaptureGamepadSnapshot.readAttemptSequence>first.readAttemptSequence,"new read attempt");check(verdict(ticket())==Verdict::Allowed,"current source allowed");
 });
 run("same-packet-all-zero-success-is-valid",[]{
  readXInputSlices();fakeTick+=8;fakeApi[0].pad={};readXInputSlices();
  check(canonicalCalls[0]==2,"zero sample must be read");check(g_inputCaptureGamepadSnapshot.live&&g_inputCaptureGamepadSnapshot.connectedMask==1,"zero does not disconnect");
  check(zero(g_inputCaptureGamepadSnapshot.pad)&&zero(g_xinputPads[0]),"zero state replaces buttons and axes");check(verdict(ticket())==Verdict::Allowed,"zero live source allowed");
 });
 run("connected-then-getstate-failure-does-not-reuse-buttons",[]{
  fakeApi[0].secret=YMCC_XINPUT_SPECIAL_XBOX;readXInputSlices();check(g_inputCaptureGamepadSnapshot.pad.wButtons!=0,"seed live buttons");check(g_inputCaptureGamepadSnapshot.specialXbox,"Guide is captured with the successful GetState snapshot");
  fakeTick+=8;fakeApi[0].rc=ERROR_DEVICE_NOT_CONNECTED;readXInputSlices();
  check(canonicalCalls[0]==2&&ordinalCalls[0]==1,"failed canonical read cannot use ordinal");check(g_xinputKnownSlotMask==0&&g_xinputConnectedMask==0,"failure clears slot masks");
  check(zero(g_xinputPads[0])&&zero(g_inputCaptureGamepadSnapshot.pad),"failed API even writing old bytes must not publish them");
  check(!g_xinputSpecialButton&&!g_xinputSpecialButtons[0],"clear old special button");check(!g_inputCaptureGamepadSnapshot.specialXbox,"failed GetState also clears the captured Guide");check(!g_inputCaptureGamepadSnapshot.live&&g_inputCaptureGamepadSnapshot.inputSource=="none","no old source");
  check(!g_inputCaptureGamepadSnapshot.readContextStable,"disconnect dirties topology during read");check(verdict(ticket())==Verdict::SourceUnavailable,"failed source rejected");
 });
 run("undiscovered-slot-does-not-inherit-g-xinputPads",[]{
  g_xinputKnownSlotMask=0;g_xinputConnectedMask=15;g_xinputPads.fill(fakeApi[0].pad);g_xinputSpecialButtons.fill(true);g_xinputSpecialButton=true;
  readXInputSlices();check(canonicalCalls==std::array<int,4>{},"unknown skipped outside discovery");check(g_xinputConnectedMask==0,"old connected mask cleared");
  check(std::all_of(g_xinputPads.begin(),g_xinputPads.end(),zero),"all cached pads cleared");check(zero(g_inputCaptureGamepadSnapshot.pad)&&!g_inputCaptureGamepadSnapshot.live,"no inherited buttons");
  check(g_inputCaptureGamepadSnapshot.readContextStable,"stable context alone does not imply live");check(verdict(ticket())==Verdict::SourceUnavailable,"unknown source rejected");
 });
 run("unknown-slot-discovery-at-100ms-control",[]{
  g_xinputKnownSlotMask=0;fakeTick+=99;readXInputSlices();check(canonicalCalls[0]==0,"99ms does not probe unknown");
  ++fakeTick;readXInputSlices();check(canonicalCalls==std::array<int,4>{1,1,1,1},"100ms probes all unknown slots");
  check(g_xinputKnownSlotMask==1&&g_inputCaptureGamepadSnapshot.live,"discovered success becomes live");check(verdict(ticket())==Verdict::Allowed,"discovered current source allowed");
 });
 for(bool boundary:{false,true})run(boundary?"boundary-discovery-control":"dirty-discovery-control",[=]{
  g_xinputKnownSlotMask=0;g_xinputVirtualEndpointProbed=0;if(boundary)g_xinputSlotFullScanOnce=true;else xinputSlotsMarkDiscoveryDirty();
  const auto topology=g_xinputTopologyEpoch.load();readXInputSlices();check(canonicalCalls==std::array<int,4>{1,1,1,1},"discovery probes all slots");
  check(g_inputCaptureGamepadSnapshot.readTopologyGeneration==topology&&g_inputCaptureGamepadSnapshot.readContextStable,"generation sampled after prior event");
  check(verdict(ticket())==Verdict::Allowed,"new discovery read allowed");
 });
 run("getstate-success-with-unresolved-identity-is-not-authorized",[]{
  g_xinputKnownSlotMask=0;g_xinputVirtualEndpointProbed=0;g_xinputDiscoveryDirty=true;fakePathReadable=false;readXInputSlices();
  check(g_xinputConnectedMask==1&&!zero(g_xinputPads[0]),"transport read can succeed");check(g_xinputSlotUnknownMask==1,"actual identity branch records Unknown");
  check(!g_inputCaptureGamepadSnapshot.live&&zero(g_inputCaptureGamepadSnapshot.pad),"unknown identity excluded from physical source");check(verdict(ticket())==Verdict::SourceUnavailable,"success alone grants no source");
 });
 run("ordinal-failure-clears-old-special-with-canonical-live",[]{
  fakeApi[0].secret=YMCC_XINPUT_SPECIAL_XBOX;readXInputSlices();check(g_xinputSpecialButton,"seed special");
  fakeTick+=8;fakeApi[0].secretRc=ERROR_DEVICE_NOT_CONNECTED;readXInputSlices();check(!g_xinputSpecialButton&&g_inputCaptureGamepadSnapshot.live,"ordinal failure cannot retain special or disconnect canonical");
  check(g_inputCaptureGamepadSnapshot.pad.wButtons==0x1000,"ordinal must not overwrite canonical buttons");
 });
 for(int generation=0;generation<3;++generation){
  const char* duringNames[]={"power-changes-during-getstate","topology-changes-during-getstate","identity-changes-during-getstate"};
  run(duringNames[generation],[=]{
   const auto t=ticket();duringCanonical=[=](DWORD i){if(i==0){if(generation==0)++g_powerGeneration;else if(generation==1)++g_xinputTopologyEpoch;else ++g_xinputSlotIdentityEpoch;}};
   readXInputSlices();check(g_inputCaptureGamepadSnapshot.live,"API result may be live despite context change");check(!g_inputCaptureGamepadSnapshot.readContextStable,"read-after check must close window");
   check(g_inputCaptureGamepadSnapshot.readPowerGeneration==10&&g_inputCaptureGamepadSnapshot.readTopologyGeneration==20&&g_inputCaptureGamepadSnapshot.readIdentityGeneration==30,"read-start generations retained");
   check(verdict(t)==(generation==0?Verdict::PowerChanged:Verdict::SourceRetired),"preexisting ticket rejected");check(verdict(ticket())==Verdict::SourceRetired,"fresh ticket cannot relabel old read");
  });
  const char* afterNames[]={"power-changes-after-read-before-submit","topology-changes-after-read-before-submit","identity-changes-after-read-before-submit"};
  run(afterNames[generation],[=]{
   readXInputSlices();const auto t=ticket();if(generation==0)++g_powerGeneration;else if(generation==1)++g_xinputTopologyEpoch;else ++g_xinputSlotIdentityEpoch;
   check(g_inputCaptureGamepadSnapshot.readContextStable,"completed flag is historical");check(verdict(t)==(generation==0?Verdict::PowerChanged:Verdict::SourceRetired),"submission rechecks generations");
   check(verdict(ticket())==Verdict::SourceRetired,"new ticket cannot reuse old read in new context");
  });
 }
 run("wake-new-generation-requires-and-allows-new-read",[]{
  readXInputSlices();++g_powerGeneration;++g_xinputSlotIdentityEpoch;xinputSlotsMarkDiscoveryDirty();g_powerLifecycle=PowerLifecycle::Resuming;fakeKeepDuringStandby=true;
  check(verdict(ticket())==Verdict::SourceRetired,"old ready read retired on wake");fakeTick+=8;fakeApi[0].pad.wButtons=0x2000;readXInputSlices();
  check(canonicalCalls[0]==2&&g_inputCaptureGamepadSnapshot.pad.wButtons==0x2000,"wake requires actual new read");check(g_inputCaptureGamepadSnapshot.readPowerGeneration==11&&g_inputCaptureGamepadSnapshot.readIdentityGeneration==31,"wake read records new generations");
  check(g_inputCaptureGamepadSnapshot.readContextStable&&verdict(ticket())==Verdict::Allowed,"fresh authorized wake read allowed");
 });
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){
  const char* label=phase==PowerLifecycle::Suspending?"suspending-stable-generation-is-power-blocked":phase==PowerLifecycle::Suspended?"suspended-stable-generation-is-power-blocked":"resuming-without-standby-policy-is-power-blocked";
  run(label,[=]{g_powerLifecycle=phase;readXInputSlices();check(g_inputCaptureGamepadSnapshot.readContextStable&&g_inputCaptureGamepadSnapshot.live,"read state and authorization are separate");
   check(!sgVirtualWriterAllowedNow(),"phase policy closes writer");check(verdict(ticket())==Verdict::PowerUnavailable,"closed phase rejects live frame");check(verdict(ticket(true))==Verdict::PowerUnavailable,"closed phase also rejects neutral ticket");});
 }
 run("resuming-standby-policy-current-read-control",[]{g_powerLifecycle=PowerLifecycle::Resuming;fakeKeepDuringStandby=true;readXInputSlices();check(sgVirtualWriterAllowedNow(),"actual resume policy permits writer");check(verdict(ticket())==Verdict::Allowed,"authorized current resume read allowed");});
 run("resuming-standby-policy-with-revoked-admission",[]{g_powerLifecycle=PowerLifecycle::Resuming;fakeKeepDuringStandby=true;g_inputHostPowerAdmissionRevoked=true;readXInputSlices();check(g_inputCaptureGamepadSnapshot.readContextStable,"unchanged generations");check(verdict(ticket())==Verdict::PowerUnavailable,"revoked admission closes ticket despite allowed phase");});
 run("ready-input-not-ready-is-power-blocked",[]{g_inputReady=false;readXInputSlices();check(g_inputCaptureGamepadSnapshot.readContextStable,"stable read");check(verdict(ticket())==Verdict::PowerUnavailable,"input ready independently required");});
 run("source-window-age-boundary-is-1500ms-inclusive",[]{readXInputSlices();const auto t=ticket();fakeTick+=1500;check(verdict(t)==Verdict::Allowed,"1500ms boundary");++fakeTick;check(verdict(t)==Verdict::SourceRetired,"1501ms expires old read");});
 run("neutral-cleanup-does-not-authorize-retired-live-source",[]{readXInputSlices();++g_xinputSlotIdentityEpoch;check(verdict(ticket())==Verdict::SourceRetired,"live retired");check(verdict(ticket(true))==Verdict::Allowed,"same-target authorized neutral cleanup is intentional");g_inputHostPowerAdmissionRevoked=true;check(verdict(ticket(true))==Verdict::PowerUnavailable,"neutral never bypasses revoked power admission");});
 unsigned failed=0;for(const auto& row:rows)if(!row.at("passed").get<bool>())++failed;
 std::cout<<json({{"passed",failed==0},{"caseCount",rows.size()},{"checks",checks},{"failedCases",failed},{"readCount",totalReads},{"canonicalCalls",totalCanonical},{"ordinalCalls",totalOrdinal},{"cases",rows}}).dump(2)<<'\n';
 return failed?1:0;
}