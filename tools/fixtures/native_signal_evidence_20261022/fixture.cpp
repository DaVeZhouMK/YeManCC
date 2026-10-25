#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <mutex>
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "virtual_signal_evidence.h"
using json=nlohmann::json;
static bool countAllocations=false;
static unsigned long long allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#include "actual-serializer.inc"
// Explicit in-memory inputs to the extracted production axis modifier. No settings reader.
static struct FixtureAxisSettings {
    double axisInnerDeadzone=0,axisOuterDeadzone=0,axisAntiDeadzone=0;
    std::string outputShape="default";
    std::vector<std::pair<double,double>> responseCurvePoints;
} g_realStickTest;
#include "actual-blend.inc"
#include "actual-frame-consumer.inc"
#include "touch_haptic_dispatch.h"
#include "actual-source-snapshot.inc"
using Sample=InputCaptureGamepadSnapshot;
static Sample physical=[] {Sample s;s.inputSource="msi-dinput";s.live=s.readContextStable=true;
    s.readAttemptSequence=9;s.readCompletedTick=990;s.readPowerGeneration=2;
    s.readMsiSourceRevision=3;s.readMsiModeGeneration=4;s.readTopologyGeneration=5;return s;}();
static Sample* t_commonFrameSource=&physical;
#include "actual-guide-assembly.inc"
static bool fixtureDetailEnabled=true,loggerThrows=false;
static unsigned long long fixtureTick=1001,clockCalls=0,g_commonFrameEvidenceFailures=0,logAttempts=0;
static bool domainLogEnabled(const char*){return fixtureDetailEnabled;}
static unsigned long long fixtureClock(){++clockCalls;return fixtureTick;}
#define GetTickCount64() fixtureClock()
static std::vector<json> rows;
static void appendNativeLifecycleLog(const char* event,json row){++logAttempts;if(loggerThrows)throw std::runtime_error("logger-fault");row["event"]=event;rows.push_back(std::move(row));}
static unsigned foregroundCalls=0;
static bool foregroundChanged=false,foregroundClassFail=false,foregroundNull=false;
static HWND fixtureForeground(){++foregroundCalls;return foregroundNull?nullptr:reinterpret_cast<HWND>(static_cast<std::uintptr_t>(foregroundChanged && foregroundCalls%2==0?202:101));}
static DWORD fixtureForegroundThread(HWND,DWORD* pid){*pid=77;return 88;}
static int fixtureForegroundClass(HWND,char* value,int size){
    if(foregroundClassFail){SetLastError(ERROR_ACCESS_DENIED);return 0;}
    const char* name="XamlExplorerHostIslandWindow";
    const auto length=std::strlen(name);if(size<=static_cast<int>(length))return 0;
    std::memcpy(value,name,length+1);return static_cast<int>(length);
}
#define GetForegroundWindow fixtureForeground
#define GetWindowThreadProcessId fixtureForegroundThread
#define GetClassNameA fixtureForegroundClass
#include "virtual_signal_evidence_runtime.h"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetClassNameA
static std::atomic<unsigned long long> g_powerGeneration{1};
#include "actual-classifier.inc"
static unsigned checks=0;
static void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
static json request={{"runId","fixture-run"},{"hostInstanceId","fixture-host"},{"epoch",1},
    {"commandSequence",101},{"sampleSequence",10},{"hostAckAccepted",true}};
static void recordActual(const FocusFrameContent& effectiveContent,const XINPUT_GAMEPAD& pad,
    const XINPUT_GAMEPAD& outputPad,const FocusStickBlend& blend,bool analogAdmitted,
    unsigned long long nativeSignalWireTick,bool diagSubmitAttempted=true,bool accepted=true,
    bool semanticNeutral=false,unsigned long long nativeSignalWireSample=11) {
    const auto& commonFrameReceipt=request;
    const unsigned long long nativeSignalRequestPower=1,barrierMask=4;
#include "actual-observer-call.inc"
}
static void record(const FocusFrameContent& c,unsigned long long tick,bool attempted=true,
                   bool accepted=true,bool neutral=false,unsigned long long sample=11){
    const XINPUT_GAMEPAD pad{};
    static const std::string target="left"; // Pre-existing caller input, including Checked STL proxy.
    const auto blend=focusIsolationBlendStick(pad,true,MotionContribution{},target,1.0);
    recordActual(c,pad,pad,blend,true,tick,attempted,accepted,neutral,sample);
}
template<class T> static auto objectBytes(const T& object) {
    std::array<unsigned char,sizeof(T)> bytes{};
    std::memcpy(bytes.data(),&object,sizeof(T));return bytes;
}
static json compositionCase(const char* name,const XINPUT_GAMEPAD& sourcePad,
    const XINPUT_GAMEPAD& pad,const XINPUT_GAMEPAD& outputPad,const MotionContribution& motion,
    bool analogAdmitted,const std::string& target,double weight,bool semanticNeutral,unsigned long long tick) {
    physical.pad=sourcePad;
    const auto blend=focusIsolationBlendStick(outputPad,analogAdmitted,motion,target,weight);
    const auto content=fixtureComposedFrame(outputPad,blend,semanticNeutral);
    const auto sourceBefore=objectBytes(physical);
    const auto padBefore=objectBytes(pad),outputBefore=objectBytes(outputPad);
    const auto motionBefore=objectBytes(motion);
    const auto blendBefore=objectBytes(blend);
    const auto contentBefore=objectBytes(content);
    const auto frameBefore=focusIsolationAssembleFrame(content),requestBefore=request;
    const auto retained=rows.size();fixtureTick=tick+1;
    recordActual(content,pad,outputPad,blend,analogAdmitted,tick,true,true,semanticNeutral);
    check(rows.size()==retained+1,"composition case produces selected actual observer row");
    check(sourceBefore==objectBytes(physical)&&padBefore==objectBytes(pad)&&outputBefore==objectBytes(outputPad)&&
        motionBefore==objectBytes(motion)&&blendBefore==objectBytes(blend)&&contentBefore==objectBytes(content)&&
        requestBefore==request&&frameBefore==focusIsolationAssembleFrame(content),
        "observer leaves source, input/output pad, motion, blend, frame and receipt unchanged");
    auto witness=rows.back();witness["fixtureCase"]=name;return witness;
}
int main(){try{
    check(nativeLifecycleRecordIsStreaming("input-native-signal-window",json::object()),"native sustained rows stay in gated detailed lane");
    check(nativeLifecycleRecordIsStreaming("msi-dinput-trigger-evidence",{{"legacyRecordSelected",false}}),"post-cap source witnesses cannot bypass detailed gate");
    check(!nativeLifecycleRecordIsStreaming("msi-dinput-trigger-evidence",{{"legacyRecordSelected",true}}),"original first64 resident evidence remains available");
    check(!nativeLifecycleRecordIsStreaming("msi-dinput-trigger-evidence",json::object()),"old rows without new fields preserve legacy classification");
    check(ymcc::logging::nativeLane(true,true,false)==ymcc::logging::NativeLane::suppressed,"base-listed sustained events remain default-off");
    check(ymcc::logging::nativeLane(true,true,true)==ymcc::logging::NativeLane::detail,"enabled sustained event selects detailed lane");
    FocusFrameContent neutral;physical.pad.wButtons=0x1000;physical.pad.bLeftTrigger=255;physical.pad.sThumbRX=12345;
    const auto physicalBefore=physical.pad;const auto frameBefore=focusIsolationAssembleFrame(neutral);
    record(neutral,1000,true,true,true);
    check(rows.size()==1,"first actual runtime witness");const auto& first=rows.back();
    check(first["source"]["pad"]["buttons"]==4096,"physical source pad retained");
    check(first["nativeFinalFrame"]["raw"]["buttons"]==0,"expected focus neutral is actual final frame");
    check(first["semanticNeutral"]==true&&first["focusReleaseBarrierMask"]==4,"neutral and release barrier explicit");
    check(first["request"]["sampleSequence"]==11&&first["request"]["receiptCapturedSampleSequence"]==10,"legacy actual serialized sample wins only in witness");
    check(request["sampleSequence"]==10,"existing receipt unchanged");
    check(first["protocolPowerGeneration"]==1&&first["source"]["powerGeneration"]==2,"protocol power is separate from source power");
    check(first["wireMonotonicTick"]==1000&&first["sampleBucket"]==1,"wire bucket selected from actual serialized clock");
    check(first["physicalSuppressionProven"]==false&&first["driverDeliveryProven"]==false&&first["gameConsumptionProven"]==false,"local call never proves external delivery");
    const auto allocBefore=allocations;countAllocations=true;
    for(unsigned i=0;i<10000;++i)record(neutral,1001,true,true,true);
    countAllocations=false;check(allocations==allocBefore,"suppressed runtime witnesses allocate no heap");
    check(rows.size()==1,"high frequency same bucket bounded");
    fixtureTick=2001;record(neutral,2000,true,true,true);
    check(rows.size()==2&&rows.back()["window"]["observations"]==10001,"window counts actual invocations");
    check(rows.back()["window"]["repeatedKnownSourceReads"]==10001,"same read repeated is visible");
    check(focusIsolationAssembleFrame(neutral)==frameBefore&&!std::memcmp(&physical.pad,&physicalBefore,sizeof(physicalBefore)),"observer cannot alter final frame or physical pad");
    fixtureDetailEnabled=false;auto cc=clockCalls;auto lc=logAttempts;fixtureTick=3001;
    const auto closedAllocBefore=allocations;countAllocations=true;
    for(unsigned i=0;i<100;++i)record(neutral,3000);
    countAllocations=false;const auto closedGateAllocations=allocations-closedAllocBefore;
    check(closedGateAllocations==0,"log gate off allocates no heap");
    check(clockCalls==cc&&logAttempts==lc,"log gate off performs no clock or log work");fixtureDetailEnabled=true;
    loggerThrows=true;record(neutral,3000);check(g_commonFrameEvidenceFailures==1,"logger exception contained");
    auto failedAttempts=logAttempts;record(neutral,3000);check(logAttempts==failedAttempts,"logger fault cannot cause per-frame log retry storm");loggerThrows=false;
    fixtureTick=4001;record(neutral,4000,false,false);check(rows.back()["wireClockKnown"]==false,"no attempt never pretends actual request");
    record(neutral,4000,true,true);check(rows.back()["wireClockKnown"]==true,"unattempted bucket cannot starve live SDK correlation");
    auto retained=rows.size();for(unsigned i=0;i<1000;++i)record(neutral,500);check(rows.size()==retained,"regressed wire clock cannot storm");
    fixtureTick=5001;record(neutral,5000);check(rows.back()["window"]["clockRegressions"]==1000,"clock regression count retained");
    t_commonFrameSource=nullptr;fixtureTick=6001;record(neutral,6000);check(rows.back()["source"]["snapshotKnown"]==false,"missing source explicit");t_commonFrameSource=&physical;
    ymcc::virtualSignal::Sampler longRun;unsigned longRunRows=0;
    for(unsigned i=0;i<75000;++i){auto t=10000ULL+i*8ULL;longRunRows+=longRun.observe(t,t+1,true,true,false,i+1,2,3,4).record;}
    check(longRunRows==600,"ten minutes of 125Hz retains sustained witnesses without 64-cap");
    ymcc::virtualSignal::Sampler storm;unsigned stormRows=0;
    for(unsigned i=0;i<50000;++i)stormRows+=storm.observe(20000,20001,true,true,false,7,2,3,4).record;
    auto afterStorm=storm.observe(21000,21001,true,true,false,8,2,3,4);
    check(stormRows==1&&afterStorm.window.observations==50000,"50000 repeated calls keep finite log volume and exact count");
    ymcc::virtualSignal::Sampler generation;generation.observe(1000,1001,true,true,false,9,1,1,1);
    auto changed=generation.observe(2000,2001,true,true,false,9,2,1,1);
    check(changed.window.repeatedKnownSourceReads==0,"equal source read number in new power is not a repeated old read");
    check(!ymcc::virtualSignal::excessiveCallbackRate(125,1000,125),"125Hz normal");
    check(!ymcc::virtualSignal::excessiveCallbackRate(250,1000,125)&&ymcc::virtualSignal::excessiveCallbackRate(251,1000,125),"strict over-two-times high-rate threshold");
    check(ymcc::virtualSignal::excessiveCallbackRate(58096,1000,125),"observed wake burst flagged");
    check(!ymcc::virtualSignal::excessiveCallbackRate(500,2000,125),"rate normalized to actual window");
    check(!ymcc::virtualSignal::excessiveCallbackRate(1,0,125)&&!ymcc::virtualSignal::excessiveCallbackRate(1,1000,0),"unknown cadence not classified");
    check(ymcc::virtualSignal::excessiveCallbackRate(std::numeric_limits<unsigned long long>::max(),1,125),"high count arithmetic does not wrap");
    // A short press can lie between retained point samples. The passive window
    // must retain its presence without turning it into hardware freshness proof.
    ymcc::virtualSignal::Sampler activity;
    activity.observe(1000,1001,true,true,false,1,2,3,4,true,true,false);
    activity.observe(1008,1009,true,true,false,2,2,3,4,true,true,true);
    activity.observe(1016,1017,true,true,false,3,2,3,4,true,false,true);
    activity.observe(1024,1025,true,true,false,4,2,3,4,false,true,true);
    const auto activityWindow=activity.observe(2000,2001,true,true,false,5,2,3,4,true,true,false);
    check(activityWindow.record && activityWindow.window.selectedSourceKnownReads==3 &&
        activityWindow.window.selectedSourceLiveReads==2 && activityWindow.window.selectedSourceNonzeroReads==1,
        "short nonzero source read survives point-sampling; unknown/dead source not claimed live");
    foregroundCalls=0;foregroundChanged=false;foregroundClassFail=false;foregroundNull=false;
    const auto stableForeground=inputHostSignalForegroundObservation();
    check(stableForeground["windowStableDuringRead"]==true && stableForeground["pid"]==77 &&
        stableForeground["className"]=="XamlExplorerHostIslandWindow","bounded actual foreground adapter uses explicit memory ports");
    foregroundCalls=0;foregroundChanged=true;
    check(inputHostSignalForegroundObservation()["windowStableDuringRead"]==false,"foreground change is not a stable session");
    foregroundChanged=false;foregroundClassFail=true;
    const auto classFailed=inputHostSignalForegroundObservation();
    check(classFailed["classReadSucceeded"]==false && classFailed["classReadError"]==ERROR_ACCESS_DENIED,
        "class read failure remains explicit unknown");
    foregroundClassFail=false;foregroundNull=true;
    const auto missingForeground=inputHostSignalForegroundObservation();
    check(missingForeground["windowBefore"]==0 && missingForeground["classReadSucceeded"]==false,
        "no foreground is not a confirmed Xbox class");
    foregroundNull=false;
    json cases=json::array();
    XINPUT_GAMEPAD zero{};
    MotionContribution motion;motion.shortX=12000;motion.shortY=-6000;motion.mapped=true;
    auto motionRow=compositionCase("physical-neutral-motion-nonzero",zero,zero,zero,motion,true,"left",0.5,false,7000);
    const auto& mc=motionRow["composition"];
    check(motionRow["formatVersion"]==1&&mc["version"]==1,"composition has its own additive schema version");
    auto legacyRow=motionRow;legacyRow.erase("composition");legacyRow["virtualSignalEvidenceVersion"]=1;
    check(!legacyRow.contains("composition"),"base evidence version 1 alone cannot imply composition coverage");
    check(motionRow["source"]["pad"]["lxShort"]==0&&mc["inputPad"]["lxShort"]==0&&mc["outputPad"]["lxShort"]==0,
        "all physical stages remain zero while motion contributes");
    check(mc["analogAdmitted"]==true&&mc["blend"]["useMotion"]==true&&mc["blend"]["targetStick"]=="left"&&
        mc["blend"]["contributionX"]==12000&&mc["blend"]["weightFactor"]==0.5&&mc["blend"]["outX"]==6000&&
        mc["blend"]["lxShort"]==6000&&motionRow["nativeFinalFrame"]["raw"]["sThumbLX"]==6000,
        "actual blend explains nonzero final LX from neutral physical pad");
    check(mc["finalOverride"]=="none"&&mc["finalUseMotion"]==true,"live final motion use is explicit");
    cases.push_back(motionRow);
    const auto highBlend=focusIsolationBlendStick(zero,true,motion,"left",0.5);
    const auto highFrame=fixtureComposedFrame(zero,highBlend,false);
    const auto highAllocBefore=allocations,highClockBefore=clockCalls;
    const auto highForegroundBefore=foregroundCalls;
    const auto highRowsBefore=rows.size();countAllocations=true;
    for(unsigned i=0;i<10000;++i)recordActual(highFrame,zero,zero,highBlend,true,7001);
    countAllocations=false;
    const auto nonselectedAllocations=allocations-highAllocBefore;
    check(nonselectedAllocations==0&&rows.size()==highRowsBefore,"high-frequency real motion composition adds no nonselected heap or rows");
    check(clockCalls-highClockBefore==10000,"nonselected composition adds no extra clock beyond existing sampler clock");
    check(foregroundCalls==highForegroundBefore,"nonselected frames perform zero foreground API calls");

    XINPUT_GAMEPAD source=zero;source.wButtons=0x1004;source.sThumbLX=12000;source.bLeftTrigger=127;
    auto input=source;input.wButtons=0x1000;
    auto output=input;output.wButtons=0x1400;
    MotionContribution unmapped=motion;unmapped.mapped=false;
    auto physicalRow=compositionCase("physical-nonzero-motion-unused",source,input,output,unmapped,true,"left",0.5,false,8000);
    const auto& pc=physicalRow["composition"];
    check(physicalRow["source"]["pad"]["buttons"]==0x1004&&pc["inputPad"]["buttons"]==0x1000&&
        pc["outputPad"]["buttons"]==0x1400,"source, post-barrier input and actual pre-blend output remain distinct");
    check(pc["blend"]["useMotion"]==false&&pc["blend"]["contributionX"]==0&&pc["blend"]["outX"]==0&&
        pc["blend"]["baseX"]==12000&&physicalRow["nativeFinalFrame"]["raw"]["sThumbLX"]==12000,
        "nonzero motion candidate without actual use never becomes executed contribution");cases.push_back(physicalRow);

    auto neutralRow=compositionCase("focus-semantic-neutral-overrides-motion",zero,zero,zero,motion,true,"left",0.5,true,9000);
    const auto& nc=neutralRow["composition"];
    check(nc["blend"]["useMotion"]==true&&nc["blend"]["lxShort"]==6000&&nc["blend"]["outY"]==-3000&&
        nc["finalOverride"]=="semantic-neutral"&&nc["finalUseMotion"]==false,
        "pre-neutral blend facts survive while final semantic override is explicit");
    check(neutralRow["nativeFinalFrame"]==focusIsolationAssembleFrame(focusIsolationNeutralContent()),
        "semantic neutral covers the actual serialized final frame");cases.push_back(neutralRow);

    auto deniedRow=compositionCase("analog-not-admitted",source,input,output,motion,false,"left",0.5,false,10000);
    check(deniedRow["composition"]["analogAdmitted"]==false&&deniedRow["composition"]["blend"]["useMotion"]==false&&
        deniedRow["composition"]["blend"]["contributionX"]==0&&deniedRow["nativeFinalFrame"]["raw"]["sThumbLX"]==12000,
        "mapped motion without analog admission is not claimed as used");cases.push_back(deniedRow);

    auto rightPad=zero;rightPad.sThumbLX=321;
    auto rightRow=compositionCase("right-target-retains-left-physical",rightPad,rightPad,rightPad,motion,true,"right",0.5,false,11000);
    check(rightRow["composition"]["blend"]["targetLeft"]==false&&rightRow["composition"]["blend"]["targetStick"]=="right"&&
        rightRow["composition"]["blend"]["rxShort"]==6000&&rightRow["nativeFinalFrame"]["raw"]["sThumbLX"]==321&&
        rightRow["nativeFinalFrame"]["raw"]["sThumbRY"]==-3000,"actual target and both stick axes are preserved");cases.push_back(rightRow);

    auto saturationPad=zero;saturationPad.sThumbLX=30000;
    MotionContribution saturationMotion=motion;saturationMotion.shortX=20000;
    auto saturationRow=compositionCase("actual-weight-and-short-clamp",saturationPad,saturationPad,saturationPad,saturationMotion,true,"left",1.5,false,12000);
    const auto& sc=saturationRow["composition"]["blend"];
    check(sc["stickNorm"].get<double>()>0.9&&sc["weightFactor"].get<double>()<0.6&&sc["outX"].get<double>()>10000&&
        sc["lxShort"]==32767&&saturationRow["nativeFinalFrame"]["raw"]["sThumbLX"]==32767,
        "reported weight is actual norm-adjusted factor and axes retain production clamp");cases.push_back(saturationRow);

    g_realStickTest.axisInnerDeadzone=25;
    MotionContribution deadzoneMotion=motion;deadzoneMotion.shortX=1000;deadzoneMotion.shortY=0;
    auto modifierRow=compositionCase("actual-modifier-zeroes-contribution",zero,zero,zero,deadzoneMotion,true,"left",0.5,false,13000);
    check(modifierRow["composition"]["blend"]["useMotion"]==true&&modifierRow["composition"]["blend"]["contributionX"]==0&&
        modifierRow["nativeFinalFrame"]["raw"]["sThumbLX"]==0,"logged contribution is after actual production modifiers, not mapped intent");
    cases.push_back(modifierRow);g_realStickTest.axisInnerDeadzone=0;
    // Pure Guide is a complete same-snapshot source even when the public XInput pad is zero.
    physical.specialXbox=true;physical.inputSource="xinput";
    const auto guideOutput=fixtureGuidePad(zero,false);
    check(guideOutput.wButtons==0x0400,"actual Guide assembly consumes the captured snapshot field");
    const auto guideRow=compositionCase("pure-Guide-source-nonzero",zero,zero,guideOutput,unmapped,true,"left",0.0,false,14000);
    check(guideRow["source"]["pad"]["buttons"]==0&&guideRow["nativeFinalFrame"]["raw"]["buttons"]==0x0400,
        "pure Guide keeps public source pad zero and the actual final report Guide pressed");
    check(guideRow["window"]["selectedSourceNonzeroReads"]==1,
        "pure Guide counts as one selected nonzero source read in the actual passive observer");
    cases.push_back(guideRow);
    physical.specialXbox=false;
    const auto guideReleased=fixtureGuidePad(zero,false);
    const auto releaseRow=compositionCase("pure-Guide-release-is-zero",zero,zero,guideReleased,unmapped,true,"left",0.0,false,15000);
    check(releaseRow["window"]["selectedSourceNonzeroReads"]==0&&releaseRow["nativeFinalFrame"]["raw"]["buttons"]==0,
        "Guide release clears both actual composition and source-nonzero count");cases.push_back(releaseRow);
    physical.specialXbox=true;
    const auto guideNeutral=fixtureGuidePad(zero,true);
    const auto neutralGuideRow=compositionCase("pure-Guide-semantic-neutral-keeps-source-fact",zero,zero,guideNeutral,unmapped,true,"left",0.0,true,16000);
    check(neutralGuideRow["window"]["selectedSourceNonzeroReads"]==1&&
        neutralGuideRow["nativeFinalFrame"]==focusIsolationAssembleFrame(focusIsolationNeutralContent()),
        "source activity remains true while actual semantic-neutral output stays completely neutral");cases.push_back(neutralGuideRow);
    physical.specialXbox=false;
    std::cout<<json({{"pass",true},{"checks",checks},{"witnessRows",rows.size()},{"longRunRows",longRunRows},{"realDevicesUsed",false},{"driverOrGameConsumptionProven",false},{"compositionWitnesses",std::move(cases)},
        {"compositionNonselectedCalls",10000},{"compositionNonselectedAllocations",nonselectedAllocations},
        {"closedGateAllocations",closedGateAllocations},
        {"actualProductionBlend",true},{"actualProductionAxisModifiers",true},
        {"unknowns",{"hardware-packet-freshness","physical-suppression","driver-delivery","game-consumption","parent-persistent-lifecycle"}}}).dump()<<'\n';return 0;
}catch(const std::exception& e){std::cout<<json({{"pass",false},{"checks",checks},{"error",e.what()},{"lastWitness",rows.empty()?json(nullptr):rows.back()}}).dump()<<'\n';return 1;}}



