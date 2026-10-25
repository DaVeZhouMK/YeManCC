#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include "json.hpp"
#include "input_host_recovery_health.h"
#include "msi_virtual_report_diagnostic.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <stdexcept>
using json=nlohmann::json;
namespace health=ymcc::input::recoveryHealth;
@@SNAPSHOT@@
@@PROOF_TYPE@@
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<unsigned long long> g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static DWORD g_xinputPrimarySlot=0;
static ULONGLONG fakeNow=100;
static bool scope=true,sourceEligible=true;
static bool msiControllerRepairScope(){return scope;}
static bool msiDinputMainSourceEligible(){return sourceEligible;}
static ULONGLONG currentPowerGeneration(){return 1;}
static ULONGLONG fixtureTick(){return fakeNow;}
#define GetTickCount64 fixtureTick
@@SOURCE@@
@@APPLY_PROOF@@
static unsigned long long fixtureMsiReadSequence=0;
static void produce(const std::string& route,DWORD slot,DWORD mask,unsigned long long revision,ULONGLONG tick){
    fakeNow=tick;g_xinputPrimarySlot=slot;
    const auto diagnosticReadPower=currentPowerGeneration();
    const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto diagnosticReadIdentity=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
    const auto diagnosticReadMsiSourceRevision=revision;
    const ULONGLONG probeNow=tick;
    const bool live=true;const XINPUT_GAMEPAD pad{};const DWORD admittedMask=mask;
    const std::string inputSource=route;
@@READ_PROOF_LOCAL@@
    // Inert successful read boundary: deterministic proof input for the actual production apply/commit.
    if(route=="msi-dinput")msiCanonicalReadProof={++fixtureMsiReadSequence,tick,tick,currentPowerGeneration(),g_msiFeedbackModeGeneration.load(),revision,g_xinputTopologyEpoch.load()};
@@PRODUCER@@
}
static const health::Context healthContext{1,1,1,1,1,true};
static health::Window window;
static json rows=json::array(),checks=json::array();
static void expect(const char* label,bool actual,bool expected){checks.push_back({{"name",label},{"actual",actual},{"expected",expected},{"pass",actual==expected}});}
static void finish(const char* name){bool pass=true;for(const auto& c:checks)pass=pass&&c["pass"].get<bool>();rows.push_back({{"name",name},{"passed",pass},{"checks",checks}});checks=json::array();}
static void reset(){g_inputCaptureGamepadSnapshot={};g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_msiMainSourceRevision=1;scope=true;sourceEligible=true;window.clear();fakeNow=100;}
static health::Source latest(){return inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);}
static void receipt(){const auto source=latest();window.accepted(healthContext,source,healthContext,source,fakeNow);}
static void initial(const std::string& route,DWORD slot=0,DWORD mask=1){produce(route,slot,mask,1,100);receipt();produce(route,slot,mask,1,1100);receipt();expect("initial two reads not yet 2s",window.complete(2000),false);}
int main(){try{
 reset();g_msiMainSourceRevision=2;produce("msi-dinput",0,0,1,100);expect("old MSI snapshot revision is not live",latest().live,false);finish("retired-msi-snapshot");
 reset();g_msiMainSourceRevision=0;produce("msi-dinput",0,0,0,100);expect("zero MSI revision is not authority",latest().live,false);finish("zero-msi-snapshot");
 reset();initial("msi-dinput",0,0);const auto continuity=g_inputCaptureGamepadSnapshot.readContinuityGeneration;g_msiMainSourceRevision=2;produce("msi-dinput",0,0,2,2100);receipt();expect("new MSI source starts a new continuity",g_inputCaptureGamepadSnapshot.readContinuityGeneration!=continuity,true);expect("reopen cannot combine two source windows",window.complete(2000),false);produce("msi-dinput",0,0,2,3100);receipt();produce("msi-dinput",0,0,2,4100);receipt();expect("new source can establish its own 2s",window.complete(2000),true);finish("same-host-msi-republish");
 reset();initial("xinput");const auto oldRouteContinuity=g_inputCaptureGamepadSnapshot.readContinuityGeneration;produce("msi-dinput",0,0,1,2100);receipt();expect("new reader route breaks continuity",g_inputCaptureGamepadSnapshot.readContinuityGeneration!=oldRouteContinuity,true);expect("different reader routes cannot complete one window",window.complete(2000),false);finish("xinput-to-msi-without-missing-tick");
 reset();initial("xinput",0,3);const auto slotContinuity=g_inputCaptureGamepadSnapshot.readContinuityGeneration;produce("xinput",1,3,1,2100);receipt();expect("different XInput slot breaks continuity",g_inputCaptureGamepadSnapshot.readContinuityGeneration!=slotContinuity,true);expect("different source slots cannot complete one window",window.complete(2000),false);finish("selected-slot-change-without-topology-change");
 reset();initial("xinput");const auto aliasContinuity=g_inputCaptureGamepadSnapshot.readContinuityGeneration;produce("xinput-any-connected",0,1,1,2100);receipt();expect("same XInput slot alias keeps continuity",g_inputCaptureGamepadSnapshot.readContinuityGeneration==aliasContinuity,true);expect("alias not a source disconnect",window.complete(2000),true);finish("same-xinput-slot-alias");
 reset();initial("xinput");const auto independent=g_inputCaptureGamepadSnapshot.readContinuityGeneration;g_msiMainSourceRevision=2;produce("xinput",0,1,2,2100);receipt();expect("unrelated MSI revision cannot retire XInput",g_inputCaptureGamepadSnapshot.readContinuityGeneration==independent,true);expect("XInput health remains complete",window.complete(2000),true);finish("xinput-independent-of-msi-revision");
 reset();initial("msi-dinput",0,0);const auto oldRead=latest();g_msiMainSourceRevision=2;produce("msi-dinput",0,0,2,2100);auto replacement=latest();replacement.continuity=oldRead.continuity;window.accepted(healthContext,oldRead,healthContext,replacement,fakeNow);expect("old receipt cannot extend replacement even with same continuity token",window.since()==0,true);finish("late-old-receipt-with-colliding-continuity");
 reset();initial("xinput",0,3);const auto oldSlotRead=latest();produce("xinput",1,3,1,2100);auto newSlotRead=latest();newSlotRead.continuity=oldSlotRead.continuity;window.accepted(healthContext,oldSlotRead,healthContext,newSlotRead,fakeNow);expect("old slot receipt cannot validate another source",window.since()==0,true);finish("late-other-slot-receipt");
 reset();initial("xinput");produce("xinput",0,1,1,2100);receipt();expect("stable all-zero report remains healthy",window.complete(2000),true);expect("all-zero physical buttons preserved",g_inputCaptureGamepadSnapshot.pad.wButtons==0,true);finish("stable-zero-input");
 reset();produce("msi-dinput",0,0,1,100);receipt();sourceEligible=false;expect("revoked route is not live",latest().live,false);window.observe(healthContext,latest(),fakeNow);expect("revoked route clears health",window.since()==0,true);finish("msi-route-revoked");
 reset();produce("msi-dinput",0,0,1,100);scope=false;expect("unsupported family is not MSI authority",latest().live,false);finish("unknown-family-remains-ineligible");
 unsigned count=0,failed=0,failedCases=0;for(const auto& row:rows){if(!row["passed"].get<bool>())++failedCases;for(const auto& c:row["checks"]){++count;if(!c["pass"].get<bool>())++failed;}}
 std::cout<<json{{"passed",failed==0},{"cases",rows.size()},{"assertions",count},{"failedCases",failedCases},{"failedChecks",failed},{"realHardwareAccess",false},{"productOrHostExecuted",false},{"results",rows}}.dump(2)<<'\n';return failed?1:0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 2;}}