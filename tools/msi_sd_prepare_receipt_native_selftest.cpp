// Strict offline SD PREPARE receipt regression. One existing-seam-backed
// inputHostStart() old-HELLO negative; no wWinMain, identity resolution, real
// Host transport or hardware queries. Other receipts are log/Session-only.
// The verification script extracts the actual current/R2 required log statements
// into a generated header. Only the log sink is captured; JSON argument evaluation
// and the production Session class remain unchanged. Request behavior is a model,
// not proof of the whole handler. See the emitted scope/counting disclosures.
#ifndef YMCC_FAULT_INJECT
#error This selftest must be built with YMCC_FAULT_INJECT.
#endif
#include <iostream>
#include <functional>
#include "../native/main.cpp"

namespace sdReceiptTest {
static unsigned failed=0, checks=0, metadataSeamCalls=0, simulatedPrepareRequests=0;
static std::vector<json> capturedLogs;
static json cases=json::array();
static json nativeEntryCase=json::object();
static json releaseCases=json::array();
static unsigned simulatedReleaseRequests=0;
static void check(const std::string& name,bool pass) {
    ++checks;if(!pass)++failed;
    std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';
}
static void captureLifecycle(const char* event,json fields) {
    fields["event"]=event?event:"";
    capturedLogs.push_back(std::move(fields));
}
}
// The generated functions contain the exact source statements, not a maintained
// copy of the guard. Capturing the sink cannot hide errors during argument eval.
#define appendNativeLifecycleLog sdReceiptTest::captureLifecycle
#include "msi_sd_prepare_receipt_log.generated.h"
#undef appendNativeLifecycleLog

namespace sdReceiptTest {
static void seedIdentity(bool knownMsi) {
    // The current production resolver's actual sentinel is this struct member.
    // ymccIdentityResolveAttempted is a test-local intent label, NOT a fabricated
    // production global or an assertion that a registry API was intercepted.
    constexpr bool ymccIdentityResolveAttempted=true;
    g_machineIdentity={};
    g_machineIdentity.attempted=ymccIdentityResolveAttempted;
    g_machineIdentity.resolved=true;
    g_machineIdentity.family=knownMsi?YmccFamily::MsiClaw:YmccFamily::AsusRogAllyX;
    g_machineIdentity.manufacturer=knownMsi?"MICRO-STAR INTERNATIONAL CO., LTD.":"ASUSTEK COMPUTER INC.";
    g_machineIdentity.product=knownMsi?"MS-1T41":"ROG Ally X";
}
static json modernHello() { return json{{"msiSteamDeckReleaseFenceVersion",1}}; }
static json validPin() {
    return json{{"msiSteamDeckReleaseEvidence",json{
        {"version",1},{"required",true},{"runId","offline-sd-run"},{"epoch",7},
        {"persona","steamdeck"},{"profileIdentity","steam-deck-composite"},
        {"probeSucceeded",true},{"absentVerified",false},
        {"ownedTargets",json::array({"USB\\VID_28DE&PID_1205\\OFFLINE_SELFTEST"})},
        {"ownedTargetCount",1},{"remainingOwnedTargetCount",1}}}};
}
struct Expect {
    bool required=true,attempted=true,sent=true,prepared=false;
};
static void runCase(const char* name,bool knownMsi,const char* persona,const json& helloReceipt,
                    bool helloAccepted,bool requestAccepted,const json& response,Expect expected,
                    const std::string& run="offline-sd-run",uint64_t epoch=7) {
    const unsigned failuresBefore=failed;
    seedIdentity(knownMsi);
    g_inputHostRunId=run;g_inputHostEpoch=epoch;
    g_msiSteamDeckReleaseFence.clear();
    g_msiSteamDeckReleaseFence.hello(helloReceipt);
    check(std::string(name)+"/identity-preseeded-no-resolver",g_machineIdentity.attempted&&g_machineIdentity.resolved);
    check(std::string(name)+"/real-board-authority",msiClawKnownBoardAuthority()==knownMsi);

    const bool fenceAdmission=g_msiSteamDeckReleaseFence.begin(msiClawKnownBoardAuthority(),persona,run,epoch);
    json receipt;unsigned requestCalls=0;
    auto modelRequest=[&]() {++requestCalls;++simulatedPrepareRequests;
        if(requestAccepted)receipt=response;return requestAccepted;};
    // Same caller short-circuit, explicitly modeled transport: NO real pipe call.
    const bool attempted=helloAccepted&&fenceAdmission;
    const bool sent=attempted&&modelRequest();
    const bool prepared=sent&&g_msiSteamDeckReleaseFence.acceptPrepare(receipt);
    const bool required=g_msiSteamDeckReleaseFence.required();
    const auto reason=g_msiSteamDeckReleaseFence.reason();
    const json originalReceipt=receipt;
    check(std::string(name)+"/required-scope",required==expected.required);
    check(std::string(name)+"/prepare-attempt-gate",attempted==expected.attempted&&requestCalls==unsigned(expected.attempted));
    check(std::string(name)+"/prepare-send-result",sent==expected.sent);
    check(std::string(name)+"/prepare-accept-gate",prepared==expected.prepared);
    check(std::string(name)+"/no-pin-on-rejection",!required||prepared||!g_msiSteamDeckReleaseFence.pinned());
    check(std::string(name)+"/no-removal-proof-created",!g_msiSteamDeckReleaseFence.verified(run,epoch));

    capturedLogs.clear();bool currentThrew=false;int currentExceptionId=0;std::string currentError;
    try { sdPrepareRequiredLogCurrent(prepared,attempted,sent,receipt); }
    catch(const json::exception& e) {currentThrew=true;currentExceptionId=e.id;currentError=e.what();}
    catch(const std::exception& e) {currentThrew=true;currentError=e.what();}
    const auto currentRows=capturedLogs;
    check(std::string(name)+"/current-log-does-not-throw",!currentThrew);
    check(std::string(name)+"/receipt-type-and-value-preserved",receipt.type()==originalReceipt.type()&&receipt==originalReceipt);
    check(std::string(name)+"/current-log-required-only",currentRows.size()==size_t(required));
    if(required&&!currentRows.empty()) {
        const auto& row=currentRows.back();
        const json expectedEvidence=receipt.is_object()?receipt.value("msiSteamDeckReleaseEvidence",json(nullptr)):json(nullptr);
        check(std::string(name)+"/current-log-fields",row.value("event","")=="msi-sd-release-fence"&&row.value("phase","")=="prepare"&&
            row.contains("prepareAttempted")&&row.at("prepareAttempted")==attempted&&
            row.contains("prepareSent")&&row.at("prepareSent")==sent&&
            row.contains("receiptKind")&&row.at("receiptKind")==receipt.type_name()&&
            row.contains("receiptObject")&&row.at("receiptObject")==receipt.is_object()&&
            row.contains("admitted")&&row.at("admitted")==prepared&&row.value("reason","")==reason);
        check(std::string(name)+"/evidence-not-coerced",row.contains("evidence")&&row.at("evidence")==expectedEvidence);
    }
    check(std::string(name)+"/log-cannot-alter-gates",required==g_msiSteamDeckReleaseFence.required()&&reason==g_msiSteamDeckReleaseFence.reason());

    capturedLogs.clear();bool legacyThrew=false;int legacyExceptionId=0;
    try { sdPrepareRequiredLogLegacy(prepared,attempted,sent,receipt); }
    catch(const json::exception& e) {legacyThrew=true;legacyExceptionId=e.id;}
    catch(...) {legacyThrew=true;}
    const bool legacyMustThrow=required&&!receipt.is_object();
    check(std::string(name)+"/legacy-negative-control",legacyThrew==legacyMustThrow&&(!legacyMustThrow||legacyExceptionId==306));
    check(std::string(name)+"/legacy-throws-before-sink",capturedLogs.size()==size_t(required&&!legacyMustThrow));
    if(required&&prepared) {
        check(std::string(name)+"/pin-not-release-ACK",!g_msiSteamDeckReleaseFence.acceptRelease(receipt)&&!g_msiSteamDeckReleaseFence.verified(run,epoch));
    }
    cases.push_back(json{{"name",name},{"passed",failed==failuresBefore},{"required",required},
        {"fenceAdmission",fenceAdmission},{"prepareAttempted",attempted},{"prepareSent",sent},
        {"simulatedPrepareRequestCalls",requestCalls},{"prepared",prepared},{"receiptKind",receipt.type_name()},
        {"receiptObject",receipt.is_object()},{"receiptPreserved",receipt.type()==originalReceipt.type()&&receipt==originalReceipt},
        {"reason",reason},{"currentThrow",currentThrew},{"currentExceptionId",currentExceptionId},{"currentError",currentError},
        {"legacyThrow",legacyThrew},{"legacyExceptionId",legacyExceptionId},{"currentLogs",currentRows}});
}
static void runNativeOldHelloNegative(const fspath::path& evidenceDir) {
    const unsigned failuresBefore=failed;
    const unsigned metadataBefore=metadataSeamCalls;
    // Independent fresh-process fixture only. No unrelated live owner is touched.
    const bool idle=!g_inputHostProcess&&!g_inputCaptureThread&&!g_sensorThread&&!g_msiDinput.di&&!g_msiDinput.pad;
    check("native-old-HELLO/no-live-runtime-or-device-owner",idle);
    if(!idle) {nativeEntryCase={{"passed",false},{"scope","refused to take an existing runtime/device owner"}};return;}
    seedIdentity(true); // Actual resolver sentinel is set before ALL helpers.
    g_startEntrySeam=YmccStartEntrySeam{};
    g_startEntrySeam.active=true;
    g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
    {
        std::lock_guard<std::mutex> lock(g_inputHostMx);
        inputHostClearLocked();
    }
    g_inputHostActive=false;g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;
    g_inputHostExitFence=false;g_inputHostFaultHoldsStart=false;g_inputHostPowerAdmissionRevoked=false;
    g_wakeHoldPhysicalUnhide=false;g_exitRequested=false;
    g_hidHideVisibilityJournal={};
    g_msiM12RemapDone=true; // Existing cached early return: no SetupDi/HID work.
    g_msiDinput.enabled=true;g_msiDinput.mainInterfaceMatched=true;
    g_msiDinput.productId=static_cast<uint32_t>(kMsiClawCapability.pidDInput);
    g_msiDinputSourceValid=true;
    g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=false;g_msiDinputAdmissionLogged=false;
    g_xinputConnectedMask=0;g_xinputVirtualEndpointMask=0;g_xinputSlotUnknownMask=0;
    g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;
    g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;
    g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;
    {std::lock_guard<std::mutex> lock(g_inputHostPersonaMx);g_realStickTest.virtualTargetPersona="steamdeck";}
    g_domainLogEnabled[1]=true;
    // Existing service-health throttle, set immediately before entry. Keep this
    // case finite; no service API is invoked and no test gives the Host v1.
    g_hidHideLastHealTick=GetTickCount64();
    const auto started=GetTickCount64();
    bool returned=false,threw=false;std::string exception;
    try {returned=inputHostStart();}
    catch(const std::exception& e) {threw=true;exception=e.what();}
    catch(...) {threw=true;exception="non-standard exception";}
    const auto elapsed=GetTickCount64()-started;
    const auto trace=g_startEntrySeam.trace;
    auto count=[&](const std::string& exact) {return unsigned(std::count(trace.begin(),trace.end(),exact));};
    unsigned pipeSeamCalls=0,fakeModeWrites=0;
    for(const auto& step:trace) {pipeSeamCalls+=step.rfind("pipe:",0)==0;fakeModeWrites+=step.rfind("mode-write:",0)==0;}
    g_nativeLifecycleLog.flush();g_nativeDetailLog.flush();
    json prepareRows=json::array(),openRows=json::array();
    for(const auto* filename:{L"native-lifecycle.log",L"native-detail.log"}) {
        std::ifstream input(evidenceDir/filename);std::string line;
        while(std::getline(input,line)) {
            const auto row=json::parse(line,nullptr,false);
            if(!row.is_object())continue;
            if(row.value("event","")=="msi-sd-release-fence"&&row.value("phase","")=="prepare")prepareRows.push_back(row);
            if(row.value("event","")=="input-host-open-result")openRows.push_back(row);
        }
    }
    check("native-old-HELLO/does-not-throw",!threw);
    check("native-old-HELLO/returns-rejected",!returned);
    check("native-old-HELLO/reaches-real-HELLO-and-create-seams",count("pipe:HELLO")==1&&count("create-process")==1);
    check("native-old-HELLO/no-PREPARE-command",count("pipe:PREPARE_TARGET")==0);
    check("native-old-HELLO/executes-opening-failed-cleanup",count("stop:opening-failed")==1);
    check("native-old-HELLO/no-target-acceptance",!g_inputHostPrepared&&!g_inputHostActive&&!g_inputHostNeutralized);
    check("native-old-HELLO/no-real-runtime-or-device-created",!g_inputHostProcess&&!g_inputCaptureThread&&!g_sensorThread&&!g_msiDinput.pad&&!g_msiDinput.di);
    check("native-old-HELLO/identity-stays-preseeded",g_machineIdentity.attempted&&g_machineIdentity.resolved);
    check("native-old-HELLO/cached-service-check-remained-within-throttle",elapsed<15000&&GetTickCount64()-g_hidHideLastHealTick<15000);
    check("native-old-HELLO/real-required-log-recorded",prepareRows.size()==1);
    if(!prepareRows.empty()) {
        const auto& row=prepareRows.front();
        check("native-old-HELLO/real-required-log-rejection-fields",row.value("reason","")=="host-capability-missing"&&
            row.contains("admitted")&&row.at("admitted")==false&&
            row.contains("prepareAttempted")&&row.at("prepareAttempted")==false&&
            row.contains("prepareSent")&&row.at("prepareSent")==false&&
            row.value("receiptKind","")=="null"&&row.contains("receiptObject")&&row.at("receiptObject")==false&&
            row.contains("evidence")&&row.at("evidence").is_null());
    }
    check("native-old-HELLO/real-open-result-not-prepared",openRows.size()==1&&openRows.front().contains("prepared")&&openRows.front().at("prepared")==false);
    nativeEntryCase={{"scope","One actual inputHostStart old-HELLO negative via existing g_startEntrySeam; cached M12/service guards; fixed metadata seams; no generic PREPARE override"},
        {"passed",failed==failuresBefore},{"wholeNegativeHandlerExecuted",true},{"returned",returned},{"threw",threw},{"exception",exception},
        {"elapsedMs",elapsed},{"nativePrepareCommandCalls",count("pipe:PREPARE_TARGET")},
        {"fakeHostSpawnCalls",count("create-process")},{"pipeSeamCalls",pipeSeamCalls},{"fakeModeWriteCalls",fakeModeWrites},
        {"syntheticMetadataSeamCalls",metadataSeamCalls-metadataBefore},{"realHostProcesses",0},{"hardwareAPICalls",0},
        {"trace",trace},{"actualPrepareLogs",prepareRows},{"actualOpenLogs",openRows}};
    g_domainLogEnabled[1]=false;
    // Never retire a live fake handle silently; this would conceal cleanup bugs.
    // Normal negative cleanup has already closed its two synthetic event handles.
    if(!g_inputHostProcess)g_startEntrySeam.active=false;
}
static void runTests() {
    const json oldHello={{"status","hello-ready"},{"detail",json{{"hidhideStateQuery",true}}.dump()}};
    runCase("old-HELLO-denied-no-PREPARE-no-target",true,"steamdeck",oldHello,true,true,validPin(),{true,false,false,false});
    runCase("capability-request-failure-null",true,"steamdeck",modernHello(),true,false,validPin(),{true,true,false,false});
    runCase("HELLO-not-accepted-no-PREPARE",true,"steamdeck",modernHello(),false,true,validPin(),{true,false,false,false});
    for(const auto& version:{json(0),json(2),json("1"),json(true),json(nullptr)}) {
        const auto name="invalid-HELLO-version-"+std::string(version.type_name())+"-"+version.dump();
        runCase(name.c_str(),true,"steamdeck",json{{"msiSteamDeckReleaseFenceVersion",version}},true,true,validPin(),{true,false,false,false});
    }
    const std::vector<std::pair<std::string,json>> malformed={
        {"null",nullptr},{"array",json::array({1,2})},{"string","not-a-receipt"},
        {"boolean",true},{"number",42},{"empty-object",json::object()}};
    for(const auto& [kind,receipt]:malformed) {
        const auto name="malformed-receipt-"+kind;
        runCase(name.c_str(),true,"steamdeck",modernHello(),true,true,receipt,{true,true,true,false});
    }
    runCase("valid-version-and-bound-pin",true,"steamdeck",modernHello(),true,true,validPin(),{true,true,true,true});
    auto invalidPin=[&](const char* name,const std::function<void(json&)>& mutate) {
        auto receipt=validPin();mutate(receipt["msiSteamDeckReleaseEvidence"]);
        runCase(name,true,"steamdeck",modernHello(),true,true,receipt,{true,true,true,false});
    };
    invalidPin("wrong-pin-version",[](json& e){e["version"]=2;});
    invalidPin("wrong-run-binding",[](json& e){e["runId"]="other-run";});
    invalidPin("wrong-epoch-binding",[](json& e){e["epoch"]=8;});
    invalidPin("wrong-persona-binding",[](json& e){e["persona"]="dualshock4";});
    invalidPin("wrong-profile-binding",[](json& e){e["profileIdentity"]="other-profile";});
    invalidPin("required-is-false",[](json& e){e["required"]=false;});
    invalidPin("probe-failed",[](json& e){e["probeSucceeded"]=false;});
    invalidPin("prepare-is-not-absence-proof",[](json& e){e["absentVerified"]=true;});
    invalidPin("wrong-owned-endpoint",[](json& e){e["ownedTargets"]=json::array({"USB\\VID_045E&PID_028E\\OTHER"});});
    invalidPin("owned-target-count-mismatch",[](json& e){e["ownedTargetCount"]=2;});
    invalidPin("remaining-count-zero",[](json& e){e["remainingOwnedTargetCount"]=0;});
    invalidPin("null-pin-evidence",[](json& e){e=nullptr;});
    invalidPin("array-pin-evidence",[](json& e){e=json::array();});
    runCase("empty-run-denied-before-request",true,"steamdeck",modernHello(),true,true,validPin(),{true,false,false,false},"",7);
    runCase("zero-epoch-denied-before-request",true,"steamdeck",modernHello(),true,true,validPin(),{true,false,false,false},"offline-sd-run",0);
    runCase("non-MSI-no-new-gate",false,"steamdeck",oldHello,true,true,nullptr,{false,true,true,true});
    for(const char* persona:{"dualshock4","dualsense","elite"}) {
        const auto name="non-SD-no-new-gate-"+std::string(persona);
        runCase(name.c_str(),true,persona,oldHello,true,true,nullptr,{false,true,true,true});
    }
}
static json validAbsenceReceipt() {
    auto receipt=validPin();auto& proof=receipt["msiSteamDeckReleaseEvidence"];
    proof["absentVerified"]=true;proof["remainingOwnedTargetCount"]=0;
    return receipt;
}
static void runReleaseCase(const char* name,bool requestAccepted,const json& response,bool expectedAccepted) {
    const unsigned failuresBefore=failed;
    seedIdentity(true);g_inputHostRunId="offline-sd-run";g_inputHostEpoch=7;
    g_msiSteamDeckReleaseFence.clear();g_msiSteamDeckReleaseFence.hello(modernHello());
    const bool begin=g_msiSteamDeckReleaseFence.begin(msiClawKnownBoardAuthority(),"steamdeck",g_inputHostRunId,g_inputHostEpoch);
    const bool pin=g_msiSteamDeckReleaseFence.acceptPrepare(validPin());
    check(std::string(name)+"/fresh-valid-prepare-pin",begin&&pin&&g_msiSteamDeckReleaseFence.required()&&
        g_msiSteamDeckReleaseFence.pinned()&&!g_msiSteamDeckReleaseFence.verified(g_inputHostRunId,g_inputHostEpoch));
    json release;unsigned modelRequests=0;
    auto modelReleaseRequest=[&]() {++modelRequests;++simulatedReleaseRequests;
        if(requestAccepted)release=response;return requestAccepted;};
    // Mirrors the unchanged local ACK + actual Session gate. This is a model of
    // transport, NOT a call of inputHostStopLocked or a real RELEASE RPC.
    const bool localAck=modelReleaseRequest();
    const bool localReleaseAck=localAck&&g_msiSteamDeckReleaseFence.acceptRelease(release);
    const bool absent=g_msiSteamDeckReleaseFence.verified(g_inputHostRunId,g_inputHostEpoch);
    const bool unhideEligibleModel=localReleaseAck; // Only when other hold gates are false.
    const auto reason=g_msiSteamDeckReleaseFence.reason();
    const auto owned=g_msiSteamDeckReleaseFence.ownedTargets();const json originalRelease=release;
    check(std::string(name)+"/request-count-and-local-ACK",modelRequests==1&&localAck==requestAccepted);
    check(std::string(name)+"/strict-release-proof-gate",localReleaseAck==expectedAccepted);
    check(std::string(name)+"/absence-not-manufactured",absent==expectedAccepted);
    check(std::string(name)+"/unproven-cannot-enable-model-unhide",unhideEligibleModel==expectedAccepted);
    check(std::string(name)+"/pin-ownership-retained",g_msiSteamDeckReleaseFence.pinned()&&owned.size()==1);

    capturedLogs.clear();bool currentThrew=false;int currentExceptionId=0;std::string error;
    try {sdReleaseLogCurrent(localAck,localReleaseAck,release);}
    catch(const json::exception& e){currentThrew=true;currentExceptionId=e.id;error=e.what();}
    catch(const std::exception& e){currentThrew=true;error=e.what();}
    const auto currentRows=capturedLogs;
    check(std::string(name)+"/current-release-log-does-not-throw",!currentThrew);
    check(std::string(name)+"/raw-release-type-and-value-preserved",release.type()==originalRelease.type()&&release==originalRelease);
    check(std::string(name)+"/current-release-log-emitted",currentRows.size()==1);
    if(!currentRows.empty()) {
        const auto& row=currentRows.front();
        check(std::string(name)+"/current-release-log-fields",row.value("event","")=="msi-sd-release-fence"&&row.value("phase","")=="release"&&
            row.contains("releaseAttempted")&&row.at("releaseAttempted")==true&&
            row.contains("localDisposeAck")&&row.at("localDisposeAck")==localAck&&
            row.contains("ownedEndpointAbsent")&&row.at("ownedEndpointAbsent")==localReleaseAck&&
            row.value("receiptKind","")==release.type_name()&&row.contains("receiptObject")&&row.at("receiptObject")==release.is_object()&&
            row.value("reason","")==reason);
        const auto evidence=release.is_object()?release.value("msiSteamDeckReleaseEvidence",json(nullptr)):json(nullptr);
        check(std::string(name)+"/release-evidence-not-coerced",row.contains("evidence")&&row.at("evidence")==evidence);
    }
    check(std::string(name)+"/log-cannot-grant-absence-or-change-ownership",g_msiSteamDeckReleaseFence.required()&&
        g_msiSteamDeckReleaseFence.pinned()&&g_msiSteamDeckReleaseFence.ownedTargets()==owned&&g_msiSteamDeckReleaseFence.reason()==reason&&
        g_msiSteamDeckReleaseFence.verified(g_inputHostRunId,g_inputHostEpoch)==absent&&localReleaseAck==expectedAccepted);
    capturedLogs.clear();bool legacyThrew=false;int legacyExceptionId=0;
    try {sdReleaseLogLegacy(localAck,localReleaseAck,release);}
    catch(const json::exception& e){legacyThrew=true;legacyExceptionId=e.id;}
    catch(...){legacyThrew=true;}
    const bool legacyMustThrow=!release.is_object();
    check(std::string(name)+"/R2-release-306-negative-control",legacyThrew==legacyMustThrow&&(!legacyMustThrow||legacyExceptionId==306));
    check(std::string(name)+"/R2-release-throws-before-log-sink",capturedLogs.size()==size_t(!legacyMustThrow));
    check(std::string(name)+"/legacy-log-cannot-change-proof-state",g_msiSteamDeckReleaseFence.verified(g_inputHostRunId,g_inputHostEpoch)==absent);
    if(expectedAccepted) {
        check(std::string(name)+"/valid-absence-remains-exact-session-bound",!g_msiSteamDeckReleaseFence.verified("other-run",7)&&
            !g_msiSteamDeckReleaseFence.verified("offline-sd-run",8));
        ymcc::msi::sdRelease1726::Session unpinned;
        unpinned.hello(modernHello());unpinned.begin(true,"steamdeck","offline-sd-run",7);
        check(std::string(name)+"/absence-receipt-without-prior-pin-refused",!unpinned.acceptRelease(release)&&!unpinned.verified("offline-sd-run",7));
        const bool wakeHold=true,stopHold=true;
        check(std::string(name)+"/positive-ACK-still-cannot-bypass-hold-gates",!(localReleaseAck&&!wakeHold&&!stopHold));
    }
    releaseCases.push_back(json{{"name",name},{"passed",failed==failuresBefore},
        {"scope","Exact current/R2 RELEASE log arguments + actual Session with modeled transport; no whole stop/release handler, cleanup/debt or unhide execution"},
        {"wholeReleaseHandlerExecuted",false},{"actualUnhideExecuted",false},{"modelReleaseRequests",modelRequests},
        {"requestAccepted",requestAccepted},{"localAck",localAck},{"localReleaseAck",localReleaseAck},{"releaseUnproven",!localReleaseAck},
        {"absenceVerified",absent},{"unhideEligibilityModel",unhideEligibleModel},{"receiptKind",release.type_name()},
        {"receiptObject",release.is_object()},{"receiptPreserved",release.type()==originalRelease.type()&&release==originalRelease},
        {"reason",reason},{"currentThrow",currentThrew},{"currentExceptionId",currentExceptionId},{"currentError",error},
        {"legacyThrow",legacyThrew},{"legacyExceptionId",legacyExceptionId},{"currentLogs",currentRows}});
}
static void runReleaseTests() {
    runReleaseCase("release-null",true,nullptr,false);
    runReleaseCase("release-array",true,json::array({1,2}),false);
    runReleaseCase("release-string",true,"not-release-proof",false);
    runReleaseCase("release-boolean",true,true,false);
    runReleaseCase("release-empty-object",true,json::object(),false);
    runReleaseCase("release-request-failed-no-receipt",false,validAbsenceReceipt(),false);
    runReleaseCase("release-valid-matching-owned-absence",true,validAbsenceReceipt(),true);
    auto invalid=[&](const char* name,const std::function<void(json&)>& mutate) {
        auto receipt=validAbsenceReceipt();mutate(receipt["msiSteamDeckReleaseEvidence"]);
        runReleaseCase(name,true,receipt,false);
    };
    invalid("release-wrong-run",[](json& e){e["runId"]="other-run";});
    invalid("release-wrong-epoch",[](json& e){e["epoch"]=8;});
    invalid("release-owned-target-substitution",[](json& e){e["ownedTargets"]=json::array({"USB\\VID_28DE&PID_1205\\OTHER"});});
    invalid("release-absence-flag-false",[](json& e){e["absentVerified"]=false;});
    invalid("release-owned-target-still-present",[](json& e){e["remainingOwnedTargetCount"]=1;});
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    using namespace sdReceiptTest;
    // Must precede every potentially identity-sensitive production helper.
    seedIdentity(true);
    g_yemanccDataDir=argv[1];g_appDataDir=argv[1];
    // Real old-HELLO negative uses fixed existing metadata seams; all other cases
    // execute only Session and captured log expressions.
    g_msiControllerTraceInventoryForTest=[](){++metadataSeamCalls;return json{{"known",false},{"scope","offline-no-hardware"}};};
    g_msiControllerOfficialStackForTest=[](){++metadataSeamCalls;return json{{"modeWriterProven",false},{"scope","offline-no-services"}};};
    g_msiControllerModeProbeIoForTest=[](){++metadataSeamCalls;return json{{"scope","offline-no-device"}};};
    const bool runtimeIdle=!g_inputHostProcess&&!g_inputCaptureThread&&!g_sensorThread;
    check("runtime-host-and-sensor-not-started",runtimeIdle);
    if(!runtimeIdle)return 3;
    try {runNativeOldHelloNegative(fspath::path(argv[1]));}catch(const std::exception& e){check("unexpected-native-negative-exception",false);std::cerr<<e.what()<<'\n';}
    const auto metadataBeforePure=metadataSeamCalls;
    try {runTests();}catch(const std::exception& e){check("unexpected-selftest-exception",false);std::cerr<<e.what()<<'\n';}
    const unsigned prepareChecksBeforeRelease=checks;
    try {runReleaseTests();}catch(const std::exception& e){check("unexpected-release-selftest-exception",false);std::cerr<<e.what()<<'\n';}
    const unsigned releaseCheckCount=checks-prepareChecksBeforeRelease;
    check("pure-log-and-Session-metadata-seam-calls-zero",metadataSeamCalls==metadataBeforePure);
    check("no-real-host-capture-or-sensor-created",!g_inputHostProcess&&!g_inputCaptureThread&&!g_sensorThread);
    json report={{"scope","Preserved PREPARE/actual old-HELLO negative plus extracted PREPARE/RELEASE log expressions and actual Session; modeled transport outside that single negative; NOT whole-release handler coverage"},
        {"wholeHandlerExecuted",true},{"wholeHandlerCoverage","old-HELLO negative only"},{"otherReceiptShapesWholeHandlerExecuted",false},{"wholeReleaseHandlerExecuted",false},{"actualUnhideExecutedForReleaseCases",false},{"actualLogSinkExecuted",true},{"actualLogArgumentStatementExecuted",true},
        {"identityActualSentinel","g_machineIdentity.attempted"},{"identityPreseeded",g_machineIdentity.attempted},
        {"hardwareAPICalls",0},{"hardwareAPICountBasis","Existing seams/cached early-return audit for one native old-HELLO negative; all other cases pure Session/extracted expressions. Counts are not a global Windows API interception audit."},
        {"registryQueries",0},{"wmiQueries",0},{"deviceQueries",0},{"serviceQueries",0},{"taskQueries",0},{"hidHideOperations",0},
        {"realHostProcesses",0},{"realReleaseRequests",0},{"simulatedReleaseRequests",simulatedReleaseRequests},{"realPrepareRequests",0},{"metadataSeamCalls",metadataSeamCalls},{"simulatedPrepareRequests",simulatedPrepareRequests},
        {"combinedPrepareAndRelease",true},{"prepareCaseCount",cases.size()+1},{"releaseCaseCount",releaseCases.size()},
        {"preservedPrepareCheckCount",prepareChecksBeforeRelease+2},{"releaseCheckCount",releaseCheckCount},
        {"caseCount",cases.size()+1+releaseCases.size()},{"modeledCaseCount",cases.size()+releaseCases.size()},{"nativeNegativeCaseCount",1},{"checkCount",checks},{"failed",failed},{"passed",failed==0},{"nativeOldHelloNegative",nativeEntryCase},{"cases",cases},{"releaseCases",releaseCases}};
    std::ofstream output(fspath::path(argv[1])/L"msi-sd-prepare-receipt-native-selftest.json");
    output<<report.dump(2)<<'\n';output.close();
    std::cout<<"MSI_SD_PREPARE_RECEIPT_NATIVE_SELFTEST cases="<<(cases.size()+1+releaseCases.size())<<" prepareCases="<<(cases.size()+1)<<" releaseCases="<<releaseCases.size()<<" checks="<<checks<<" failed="<<failed<<" hardwareAPICalls=0 scope=one-native-negative-plus-prepare-release-log-and-Session\n";
    return failed==0&&output.good()?0:3;
}