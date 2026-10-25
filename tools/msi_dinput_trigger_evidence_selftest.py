#!/usr/bin/env python3
"""Offline actual-source mapper and diagnostic adapter replay. Never runs YMCC/Host."""
from pathlib import Path
import argparse, datetime, hashlib, json, re, subprocess, sys
sys.dont_write_bytecode = True
from virtual_sample_tree_perf_selftest import match_brace
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / "Build/Validation/VC-SIGNAL-EVIDENCE-20261022/source"
FIXTURES = REPO / "tools/fixtures/msi_signal_sustained_20261022"
VC = Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
def function(text, name):
    at = text.index("static ", text.index(name) - 20)
    opening = text.index("{", text.index(name, at))
    return text[at:match_brace(text, opening)+1]
def sha(b): return hashlib.sha256(b).hexdigest()
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT)
    parser.add_argument("--negative-control-dir", type=Path,
                        help="Saved original observer, serializer and actual.inc; require the real post-cap replay to fail")
    args = parser.parse_args()
    out = args.out.resolve()
    run = out / ("run-"+datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    run.mkdir(parents=True)
    result = {"pass": False, "run": str(run), "actualInput": False, "profiles": [], "negativeControls": [], "mainWiringApplied": False, "mainEditedByThisTask": False,
              "lifecycleScope": "Persistent pump-owned value observer; owns no thread, device, timer, callback or heap resource"}
    try:
        source = (REPO/"native/main.cpp").read_text(encoding="utf-8-sig")
        names = ["msiAxisToStick(", "msiAxisToStickInverted(", "msiAxisToTrigger(", "msiPovToButtons(",
                 "msiDinputFrameCorrupt(", "msiDinputCanonicalPad(", "msiDinputStartupNeutral(", "msiDinputSignalEvidenceSnapshot(", "msiDinputRecordTriggerEvidence("]
        blocks = {name: function(source, name) for name in names}
        assert source.count("msiDinputRecordTriggerEvidence(js,") == 6
        assert '"msi-dinput-trigger-evidence", // first samples' in source
        assert "msiDinputTriggerEvidenceFile=" in source
        guardStart=source.index("    // New binding/power generation: release-before-admission, MSI only.")
        guardEnd=source.index("    if(g_msiDinput.inputLostRecovery.active){",guardStart)
        guard=source[guardStart:guardEnd]
        pumpStart=source.rfind("static void msiDinputPump",0,guardStart)
        assert pumpStart>=0 and guardEnd<source.index("    msiDinputPublishCanonicalFrame(canonical",guardEnd)
        (run/"actual-admission.inc").write_text(guard,encoding="utf-8")
        pump = function(source, "msiDinputPump()")
        disabledGuard = re.search(
            r"    if \(!g_msiDinput\.enabled\.load\(std::memory_order_acquire\)\) return;", pump).group(0)
        assert "std::atomic<bool> enabled{false};" in source
        (run/"actual-disabled-pump.inc").write_text(disabledGuard+"\n", encoding="utf-8")
        adapter = blocks[names[-1]]
        result["mainWiringApplied"] = "decisionJson(selection)" in adapter
        result["virtualGateApplied"] = 'domainLogEnabled("virtual")' in adapter and "selection.legacyRecordSelected" in adapter
        assert not re.search(r"\b(?:Poll|Acquire|GetDeviceState|SendInput|ClearOwnedState|ReleasePad)\s*\(", adapter)
        header = (REPO/"native/msi_dinput_trigger_evidence.h").read_bytes()
        (run/"msi_dinput_trigger_evidence.h").write_bytes(header)
        serializer=(REPO/"native/msi_dinput_signal_evidence_json.h").read_bytes()
        (run/"msi_dinput_signal_evidence_json.h").write_bytes(serializer)
        admissionHeader=(REPO/"native/msi_dinput_input_admission.h").read_bytes()
        (run/"msi_dinput_input_admission.h").write_bytes(admissionHeader)
        (run/"actual.inc").write_text("\n".join(blocks.values()),encoding="utf-8")
        fixture = r'''#define DIRECTINPUT_VERSION 0x0800
#define NOMINMAX
#include <windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <future>
#include <thread>
#include <cstdlib>
#include <new>
#include <memory>
#include <fstream>
#include <limits>
#include <type_traits>
#include "json.hpp"
#include "msi_dinput_trigger_evidence.h"
#include "msi_dinput_input_admission.h"
#include "msi_dinput_signal_evidence_json.h"
using json=nlohmann::json;
struct State { std::atomic<bool> enabled{false}; ymcc::msi::inputAdmission::Gate inputAdmission; ymcc::msi::triggerEvidence::Observer triggerEvidence; unsigned bindCount=1;
    long axisActualMin[6]={0,0,0,0,0,0},axisActualMax[6]={65535,65535,65535,65535,65535,65535};
    unsigned dwButtons=17; bool axisRangeNormalize=false; unsigned axisRangeVerdict=0; std::atomic<uint64_t> canonicalReadSequence{1}; };
static State g_msiDinput;
static std::mutex g_msiPersonaPumpMx;
static bool knownMsi=true;
static bool msiClawKnownBoardAuthority(){return knownMsi;}
static std::atomic<unsigned long long> allocationRequests{0};
void* operator new(size_t size){++allocationRequests;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
static std::atomic<uint64_t> g_msiMainSourceRevision{5};
static std::atomic<bool> g_msiDinputSourceValid{false},g_msiDinputRouteConfirmed{false};
static const char* msiAxisRangeVerdictName(unsigned) { return "verified-declared"; }
static std::atomic<unsigned long long> hostEpoch{7};
static unsigned long long inputHostEpochToken() { return hostEpoch.load(); }
static std::vector<json> rows;
static bool virtualDetailedLogs=true;
bool domainLogEnabled(const char* domain) {
    if(std::strcmp(domain,"virtual"))throw std::runtime_error("unexpected log domain");
    return virtualDetailedLogs;
}
static bool loggerThrows=false;
static unsigned long long loggerAttempts=0;
static unsigned long long loggerPowerStamp=0;
static void appendNativeLifecycleLog(const char* event,json row) {
    ++loggerAttempts;
    if(loggerThrows)throw std::runtime_error("synthetic logger failure");
    if(std::strcmp(event,"msi-dinput-trigger-evidence"))throw std::runtime_error("wrong event");
    if(loggerPowerStamp)row["powerGeneration"]=loggerPowerStamp;
    rows.push_back(std::move(row));
}
#include "actual.inc"
static unsigned published=0,cleared=0;
static void msiDinputClearOwnedState(const char*,bool resetInputAdmission=true) {
    ++cleared;if(resetInputAdmission)g_msiDinput.inputAdmission.reset();
    g_msiDinputSourceValid=false;g_msiDinputRouteConfirmed=false;
}
static void actualAdmission(const DIJOYSTATE2& js,const XINPUT_GAMEPAD& canonical,
    ULONGLONG completed,unsigned long long readPower=1,unsigned long long readMode=0) {
    (void)readMode;
    const DIJOYSTATE2& frame=js;
    const ULONGLONG canonicalReadStartedTick=completed,canonicalReadCompletedTick=completed;
    const HRESULT pollHr=DI_OK,stateHr=DI_OK;
#include "actual-admission.inc"
    ++published;
}
static unsigned checks=0;
static void check(bool ok,const char* reason) {++checks;if(!ok)throw std::runtime_error(reason);}
static void actualDisabledPump() {
#include "actual-disabled-pump.inc"
    throw std::runtime_error("disabled pump continued into device path");
}
// SUSTAINED_CASES_INCLUDE
int main(){try{
    DIJOYSTATE2 raw{};raw.lX=raw.lY=raw.lZ=raw.lRx=raw.lRy=raw.lRz=32768;raw.rgdwPOV[0]=0xffffffffu;
    XINPUT_GAMEPAD pad{};
    check(msiDinputCanonicalPad(raw,pad),"current mapper accepts synthetic all-32768 tuple");
    check(pad.bLeftTrigger==127&&pad.bRightTrigger==127&&pad.sThumbLX==0&&pad.sThumbLY==-1,
        "synthetic tuple exactly reproduces observed canonical numbers, not proof of observed raw");
    auto rawBefore=raw;auto padBefore=pad;
    msiDinputRecordTriggerEvidence(raw,raw,&pad,1,"canonical-published",101,102,DI_OK,DI_OK);
    check(!std::memcmp(&raw,&rawBefore,sizeof(raw))&&!std::memcmp(&pad,&padBefore,sizeof(pad)),"observer cannot alter source or output");
    check(rows.size()==1&&rows.back()["rawAxes"][3]==32768&&rows.back()["canonical"]["leftTrigger"]==127,"same-sample raw and converted evidence");
    check(rows.back()["canonicalPublished"]==true&&rows.back()["hostSubmissionProven"]==false&&rows.back()["steamConsumptionProven"]==false,"publication is not Host or Steam consumption");
    check(rows.back()["canonicalReadSequence"]==1&&rows.back()["bindCount"]==1&&rows.back()["hostEpoch"]==7,"identity fields retained");
    for(int i=0;i<4;++i)msiDinputRecordTriggerEvidence(raw,raw,&pad,1,"canonical-published",103,104,0,0);
    check(rows.size()==4,"first four only for a steady held half press");
    auto neutralRaw=raw;neutralRaw.lRx=neutralRaw.lRy=0;XINPUT_GAMEPAD neutral{};
    check(msiDinputCanonicalPad(neutralRaw,neutral),"zero-trigger sample valid");
    msiDinputRecordTriggerEvidence(neutralRaw,neutralRaw,&neutral,1,"canonical-published",113,114,0,0);
    check(rows.size()==5&&rows.back()["canonical"]["leftTrigger"]==0&&rows.back()["triggerEdge"]==true,"short pulse release survives without periodic sample");
    const auto allocationsBefore=allocationRequests.load();
    for(int i=0;i<100000;++i)msiDinputRecordTriggerEvidence(neutralRaw,neutralRaw,&neutral,1,"canonical-published",115,116,0,0);
    check(allocationRequests.load()==allocationsBefore,"native steady diagnostics allocate no heap");
    check(rows.size()==5,"no per-poll JSON/log churn for idle");
    // A real simultaneous half press with button/stick activity must still pass.
    raw.lX=42000;raw.rgbButtons[1]=0x80;check(msiDinputCanonicalPad(raw,pad),"legal dual half remains valid");
    check(pad.bLeftTrigger==127&&pad.bRightTrigger==127&&(pad.wButtons&XINPUT_GAMEPAD_A),"no position-based filtering");
    auto bad=raw;bad.lRx=bad.lRy=bad.lRz=32767;auto previous=pad;
    check(!msiDinputCanonicalPad(bad,pad)&&!std::memcmp(&previous,&pad,sizeof(pad)),"existing corrupt-frame rule unchanged");
    msiDinputRecordTriggerEvidence(bad,bad,nullptr,3,"canonical-rejected",117,118,0,0);
    check(rows.back()["canonical"].is_null()&&rows.back()["canonicalPublished"]==false&&rows.back()["rawAxes"][3]==32767,"rejected raw sample is not promoted to valid");
    ++g_msiDinput.bindCount;msiDinputRecordTriggerEvidence(raw,raw,&pad,1,"canonical-published",201,202,0,0);
    check(rows.back()["sampleWithinBinding"]==1&&rows.back()["firstSamples"]==true,"binding resets first-sample evidence");
    loggerThrows=true;msiDinputRecordTriggerEvidence(raw,raw,&pad,1,"canonical-published",203,204,0,0);loggerThrows=false;
    check(true,"logger failure did not leave noexcept adapter");
    auto observerStorage=std::make_unique<ymcc::msi::triggerEvidence::Observer>();auto& observer=*observerStorage;
    unsigned records=0;
    for(unsigned i=0;i<1000;++i)if(observer.observe(1,(i&1)?127:0,(i&1)?127:0,1).record)++records;
    check(records==65,"64 edges plus single explicit exhaustion receipt");
    auto next=observer.observe(2,127,127,1);check(next.record&&next.sample==1&&!next.capacityReached,"new generation renews bounded capture");
    auto gradualStorage=std::make_unique<ymcc::msi::triggerEvidence::Observer>();auto& gradual=*gradualStorage;
    for(unsigned i=0;i<4;++i)gradual.observe(1,40,40,1);
    unsigned gradualRows=0;
    for(unsigned i=0;i<100000;++i)if(gradual.observe(1,40+i%70,40+i%70,1).record)++gradualRows;
    check(gradualRows==0,"continuous analog motion within class does not crowd ring");
    check(gradual.observe(1,127,127,1).record&&gradual.observe(1,128,128,1).record,"exact pair entry and departure retained");
    check(gradual.observe(1,128,128,4).outcomeEdge,"generation rejection retained at same trigger values");
    // Real production gate block, existing mapper and real neutral predicate.
    g_msiDinput.inputAdmission.reset();++g_msiDinput.bindCount;published=cleared=0;
    auto heldRaw=neutralRaw;heldRaw.lRx=heldRaw.lRy=32768;XINPUT_GAMEPAD held{};
    check(msiDinputCanonicalPad(heldRaw,held),"held dual half mapped");
    actualAdmission(heldRaw,held,1001);actualAdmission(heldRaw,held,1009);
    check(published==0&&cleared==2&&!g_msiDinputSourceValid,"startup half samples cannot establish source or publish");
    actualAdmission(neutralRaw,neutral,1017);actualAdmission(neutralRaw,neutral,1025);
    check(published==0,"two neutral reads insufficient");
    actualAdmission(neutralRaw,neutral,1025);
    check(published==0,"same-millisecond repeated read cannot open gate");
    actualAdmission(neutralRaw,neutral,1033);
    check(published==1&&g_msiDinput.inputAdmission.ready(),"third later neutral read admits source");
    actualAdmission(heldRaw,held,1041);
    check(published==2&&held.bLeftTrigger==127&&held.bRightTrigger==127,"real held dual half passes after admission unchanged");
    actualAdmission(heldRaw,held,1049,2,0);
    check(published==2&&!g_msiDinput.inputAdmission.ready(),"power generation cannot inherit ready state");
    actualAdmission(neutralRaw,neutral,1057,2,0);actualAdmission(neutralRaw,neutral,1065,2,0);
    actualAdmission(heldRaw,held,1073,2,0);actualAdmission(neutralRaw,neutral,1081,2,0);
    check(published==2&&g_msiDinput.inputAdmission.neutralReads()==1,"pressed frame resets consecutive neutral evidence");
    actualAdmission(neutralRaw,neutral,1089,2,0);actualAdmission(neutralRaw,neutral,1097,2,0);
    check(published==3,"release recovers without recreate or mode write");
    actualAdmission(heldRaw,held,1105,2,1);
    check(published==4&&g_msiDinput.inputAdmission.ready(),"software mode intent alone cannot reset an established binding");
    auto rear=neutralRaw;rear.rgbButtons[15]=0x80;
    check(!msiDinputStartupNeutral(rear,neutral),"rear button held prevents admission despite canonical zero buttons");
    auto stick=neutral;stick.sThumbLX=20000;
    check(!msiDinputStartupNeutral(neutralRaw,stick),"held stick prevents startup admission");
    auto noise=neutral;noise.sThumbLX=100;noise.sThumbRY=-100;
    check(msiDinputStartupNeutral(neutralRaw,noise),"ordinary center noise uses existing XInput deadzones");
    ymcc::msi::inputAdmission::Gate rollback;
    rollback.observe({1,1},true,30);rollback.observe({1,1},true,31);
    check(!rollback.observe({1,1},true,29).admitted&&rollback.neutralReads()==0,"clock regression cannot complete admission");
    check(!rollback.observe({0,1},true,40).admitted,"missing binding never admitted");
    g_msiDinput.inputAdmission.reset();++g_msiDinput.bindCount;published=0;
    const unsigned windows[]={13,35,12,0,14,16};
    for(unsigned i=0;i<6;++i){
        ++g_msiDinput.bindCount;const auto tick=ULONGLONG(2000+i*100);
        const auto before=published;actualAdmission(heldRaw,held,tick);
        check(published==before,"observed-shaped transient blocked in new binding");
        for(unsigned j=0;j<3;++j)actualAdmission(neutralRaw,neutral,tick+windows[i]+1+j*8);
        check(published==before+1,"six recorded time shapes recover on neutral reads");
    }
    using Obs=ymcc::msi::triggerEvidence::Observer;
    using Signal=ymcc::msi::triggerEvidence::Sample;
    auto allStorage=std::make_unique<Obs>();auto& all=*allStorage;
    Signal signal{};signal.canonicalKnown=true;signal.power=1;signal.hostEpoch=2;
    for(unsigned i=0;i<4;++i)all.observe(1,0,0,1,signal);
    signal.buttons=XINPUT_GAMEPAD_A;auto down=all.observe(1,0,0,1,signal);
    signal.buttons=0;auto up=all.observe(1,0,0,1,signal);
    check(down.buttonEdge&&up.buttonEdge,"single canonical press and release retained without trigger changes");
    signal.rawButtons[3]=0x80000000u;down=all.observe(1,0,0,1,signal);
    signal.rawButtons[3]=0;up=all.observe(1,0,0,1,signal);
    check(down.rawButtonEdge&&up.rawButtonEdge,"raw button127 beyond legacy32-bit mask observed");
    signal.rawButtonBytes[15]=1;down=all.observe(1,0,0,1,signal);
    signal.rawButtonBytes[15]=0;up=all.observe(1,0,0,1,signal);
    check(down.rawButtonEdge&&up.rawButtonEdge,"raw low-bit-only changes are meaningful even with unchanged high-bit masks");
    signal.pov=4500;down=all.observe(1,0,0,1,signal);signal.pov=0xffffffffu;up=all.observe(1,0,0,1,signal);
    check(down.povEdge&&up.povEdge,"diagonal POV and center release observed");
    signal.sticks[0]=20000;down=all.observe(1,0,0,1,signal);signal.sticks[0]=0;up=all.observe(1,0,0,1,signal);
    check(down.stickEdge&&up.stickEdge,"one-read stick displacement and release observed");
    signal.sticks[2]=-32768;down=all.observe(1,0,0,1,signal);signal.sticks[2]=32767;up=all.observe(1,0,0,1,signal);
    check(down.stickEdge&&up.stickEdge,"negative and positive full-scale classes differ");
    signal.sticks[2]=0;all.observe(1,0,0,1,signal);
    const auto beforeNoise=all.snapshot().meaningful;
    for(unsigned i=0;i<10000;++i){signal.sticks={static_cast<short>(i%200),-100,100,-100};all.observe(1,0,0,1,signal);}
    check(all.snapshot().meaningful==beforeNoise,"center noise does not churn evidence history");
    signal.sticks={10000,10000,10000,10000};all.observe(1,0,0,1,signal);
    const auto beforeGradient=all.snapshot().meaningful;
    for(unsigned i=0;i<10000;++i){signal.sticks[0]=static_cast<short>(10000+i%1000);all.observe(1,0,0,1,signal);}
    check(all.snapshot().meaningful==beforeGradient,"continuous movement inside class does not churn history");
    auto leftHalf=all.observe(1,127,0,1,signal);auto leftAway=all.observe(1,128,0,1,signal);
    check(leftHalf.triggerEdge&&leftAway.triggerEdge,"single-side half-range signature enters and leaves");
    signal.rawRangeKnownMask=63;signal.rawRangeViolationMask=8;down=all.observe(1,0,0,1,signal);
    signal.rawRangeViolationMask=0;up=all.observe(1,0,0,1,signal);
    check(down.rangeEdge&&up.rangeEdge,"raw range excursion and recovery observed without filtering");
    signal.mode++;check(all.observe(1,0,0,1,signal).contextEdge,"mode metadata remains observable without rearming input");
    signal.canonicalKnown=false;check(all.observe(1,0,0,3,signal).outcomeEdge,"raw failure classified without claiming canonical zero");
    unsigned cappedRows=0;auto ringStorage=std::make_unique<Obs>();auto& ring=*ringStorage;signal={};signal.canonicalKnown=true;signal.power=2;signal.hostEpoch=99;
    for(unsigned i=0;i<4096;++i){signal.buttons=(i&1)?XINPUT_GAMEPAD_B:0;signal.completedTick=i+1;if(ring.observe(4,0,0,1,signal).record)++cappedRows;}
    check(cappedRows==65,"all-signal logging remains capped");
    auto recent=ring.snapshot();
    check(recent.meaningful==4096&&recent.buttonEdges==4095&&recent.recent[63].sample==4096,"recent edges survive after log cap");
    check(recent.recent[0].sample==4033&&recent.recent[63].signal.buttons==XINPUT_GAMEPAD_B,"history is chronological latest64");
    signal.buttons=0;ring.observe(4,0,0,1,signal);auto exported=ymcc::msi::triggerEvidence::snapshotJson(ring.snapshot());
    check(exported["recent"].size()==64&&exported["recent"].back()["buttonMask"]==0,"post-cap release reaches exported history");
    check(exported["recent"].back()["hostEpoch"]==99&&exported["evictedRecentSamples"]==4033,"source context and explicit omissions retained");
    check(exported["steamConsumptionProven"]==false&&exported["formatVersion"]==2,"export never claims consumer proof");
    ring.observe(5,0,0,1,Signal{});exported=ymcc::msi::triggerEvidence::snapshotJson(ring.snapshot());
    check(exported["recent"].size()==1&&exported["recent"][0]["hostEpoch"]==0,"rebinding cannot expose stale ring rows");
    ++g_msiDinput.bindCount;
    auto extraRaw=neutralRaw;extraRaw.rgbButtons[127]=0x80;extraRaw.lRx=70000;
    msiDinputRecordTriggerEvidence(extraRaw,extraRaw,&neutral,6,"startup-neutral-pending",20001,20002,0,0,3,4);
    check(rows.back()["rawButtonMasks"][3]==0x80000000u&&rows.back()["rawRangeViolationMask"]==8,"production adapter records high raw button and out-of-range axis");
    check(rows.back()["powerGeneration"]==3&&rows.back()["modeGeneration"]==4,"production read context captured");
    extraRaw.rgbButtons[15]=1;
    msiDinputRecordTriggerEvidence(extraRaw,extraRaw,&neutral,6,"startup-neutral-pending",20003,20004,0,0,3,4);
    check(rows.back()["rawButtonMasks"][0]==0&&rows.back()["rawButtonBytes"][15]==1&&rows.back()["rawLowBitOnlyButtonMasks"][0]==32768,"low-bit-only raw button is preserved even when old high-bit mask is zero");
    check(rows.back()["reportedButtonCount"]==17&&rows.back()["rawLowerBitMasks"][0]==32768,"reported capabilities stay distinct from full buffer observations");
    loggerPowerStamp=999;
    msiDinputRecordTriggerEvidence(extraRaw,extraRaw,&neutral,6,"startup-neutral-pending",20005,20006,0,0,3,4);
    check(rows.back()["samplePowerGeneration"]==3&&rows.back()["powerGeneration"]==999&&rows.back()["sampleModeGeneration"]==4,"read-time power is not overwritten by lifecycle writer observation-time stamp");
    loggerPowerStamp=0;
    const auto snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["status"]=="captured"&&snapshot["recent"].size()==3&&snapshot["retainedRowsAreHistorical"]==true,"actual export adapter snapshots current generation");
    knownMsi=false;check(msiDinputSignalEvidenceSnapshot()["status"]=="not-msi","export scoped to MSI");knownMsi=true;
    std::promise<void> locked,releaseLock;auto resume=releaseLock.get_future();
    std::thread owner([&]{std::lock_guard<std::mutex> hold(g_msiPersonaPumpMx);locked.set_value();resume.wait();});
    locked.get_future().wait();const auto busy=msiDinputSignalEvidenceSnapshot();releaseLock.set_value();owner.join();
    check(busy["status"]=="pump-busy"&&busy["captureComplete"]==false,"busy export reports incomplete without waiting or stale read");
    // SUSTAINED_CASES_CALL
    std::cout<<json{{"pass",true},{"checks",checks},{"syntheticRawIsFieldEvidence",false},{"actualInput",false},{"observerBytes",sizeof(ymcc::msi::triggerEvidence::Observer)}}.dump()<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";std::cout<<json{{"pass",false},{"checks",checks},{"error",e.what()}}.dump()<<"\n";return 1;}}
'''
        baseFixture = fixture
        caseBytes = (FIXTURES/"sustained_cases.inc").read_bytes()
        legacyBytes = (FIXTURES/"legacy_failure.inc").read_bytes()
        (run/"sustained_cases.inc").write_bytes(caseBytes)
        fixture = fixture.replace("// SUSTAINED_CASES_INCLUDE", '#include "sustained_cases.inc"')
        fixture = fixture.replace("// SUSTAINED_CASES_CALL", "sustainedCases();")
        result["sourceHashes"] = {"observer":sha(header), "serializer":sha(serializer),
            "admissionHeader":sha(admissionHeader), "actualAdmissionBlock":sha(guard.encode()),
            "actualDisabledPumpGuard":sha(disabledGuard.encode()), "sustainedCases":sha(caseBytes),
            "legacyFailureCase":sha(legacyBytes), "selftest":sha(Path(__file__).read_bytes()),
            "actualBlocks":{k:sha(v.encode()) for k,v in blocks.items()}}
        def execute(directory, profile, code, expectedFailure=None):
            directory.mkdir(parents=True, exist_ok=True)
            (directory/"test.cpp").write_text(code,encoding="utf-8")
            exe=directory/(profile+".exe");cmd=directory/(profile+".cmd")
            flags = ["/O2","/DNDEBUG"] if profile=="Release" else ["/Od","/RTC1","/D_ITERATOR_DEBUG_LEVEL=0"]
            sdk=Path("C:/Program Files (x86)/Windows Kits/10")
            compiler=["cl","/nologo","/std:c++20","/utf-8","/EHsc","/MT","/W4","/WX",*flags,
                str(directory/"test.cpp"),"/I"+str(run),
                *["/I"+str(sdk/"Include/10.0.26100.0"/part) for part in ("um","shared","ucrt")],
                "/I"+str(REPO/"deps/json"),"/Fo"+str(directory/(profile+".obj")),"/Fe"+str(exe),"/link",
                *["/LIBPATH:"+str(sdk/"Lib/10.0.26100.0"/part/"x64") for part in ("um","ucrt")]]
            cmd.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+
                subprocess.list2cmdline(compiler)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(exe)+
                '" > "'+str(directory/(profile+"-imports.txt"))+'"\nexit /b %errorlevel%\n',encoding="utf-8")
            built=subprocess.run(["cmd.exe","/d","/c",str(cmd)],capture_output=True,encoding="utf-8",errors="replace",timeout=60,cwd=directory)
            (directory/(profile+"-compile.log")).write_text(built.stdout+built.stderr,encoding="utf-8")
            if built.returncode: raise RuntimeError(profile+" compile failed: "+built.stdout+built.stderr)
            imports=(directory/(profile+"-imports.txt")).read_text(encoding="utf-8",errors="replace").lower()
            assert not any(x in imports for x in ["user32.dll","dinput8.dll","xinput","setupapi.dll","sendinput","hid.dll","advapi32.dll"])
            executed=subprocess.run([str(exe)],capture_output=True,encoding="utf-8",errors="replace",timeout=60,cwd=directory)
            (directory/(profile+"-stdout.log")).write_text(executed.stdout,encoding="utf-8")
            (directory/(profile+"-stderr.log")).write_text(executed.stderr,encoding="utf-8")
            try:
                details=json.loads(executed.stdout)
            except json.JSONDecodeError:
                details={"pass":False,"error":f"No JSON receipt; process exit 0x{executed.returncode & 0xffffffff:08x}"}
            receipt={"name":profile,"returnCode":executed.returncode,**details}
            if expectedFailure is not None:
                receipt["expectedFailure"]=expectedFailure
                receipt["negativeControlPassed"]=executed.returncode!=0 and receipt.get("error")==expectedFailure
            (directory/(profile+"-result.json")).write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
            return receipt
        failures=[]
        for profile in ("Release","Checked"):
            receipt=execute(run,profile,fixture)
            result["profiles"].append(receipt)
            if receipt["returnCode"] or not receipt["pass"]:failures.append(profile+": "+receipt.get("error","execution failed"))
            evidence=run/"sustained-evidence.json"
            if evidence.exists(): evidence.replace(run/(profile+"-sustained-evidence.json"))
        if args.negative_control_dir:
            negative=run/"legacy-negative-control"
            negative.mkdir()
            oldHeader=(args.negative_control_dir/"msi_dinput_trigger_evidence.h").read_bytes()
            oldSerializer=(args.negative_control_dir/"msi_dinput_signal_evidence_json.h").read_bytes()
            (negative/"msi_dinput_trigger_evidence.h").write_bytes(oldHeader)
            (negative/"msi_dinput_signal_evidence_json.h").write_bytes(oldSerializer)
            oldAdapter=(args.negative_control_dir/"actual.inc").read_bytes()
            (negative/"actual.inc").write_bytes(oldAdapter)
            code=baseFixture.replace("// SUSTAINED_CASES_CALL",legacyBytes.decode("utf-8"))
            result["negativeControlSourceHashes"]={"observer":sha(oldHeader),"serializer":sha(oldSerializer),"actualAdapterBundle":sha(oldAdapter)}
            for profile in ("Release","Checked"):
                receipt=execute(negative,profile,code,"sustained witness missing after actual legacy64 cap")
                result["negativeControls"].append(receipt)
                if not receipt["negativeControlPassed"]:failures.append(profile+" legacy negative control did not fail at sustained witness")
        if failures:raise RuntimeError("; ".join(failures))
        current=(REPO/"native/main.cpp").read_text(encoding="utf-8-sig")
        assert blocks=={name:function(current,name) for name in names},"tested source changed during validation"
        assert header==(REPO/"native/msi_dinput_trigger_evidence.h").read_bytes()
        assert admissionHeader==(REPO/"native/msi_dinput_input_admission.h").read_bytes()
        assert serializer==(REPO/"native/msi_dinput_signal_evidence_json.h").read_bytes()
        assert guard in current,"actual admission block changed during validation"
        assert disabledGuard in function(current,"msiDinputPump()"),"default pump guard changed during validation"
        assert caseBytes==(FIXTURES/"sustained_cases.inc").read_bytes(),"sustained fixture changed during validation"
        assert legacyBytes==(FIXTURES/"legacy_failure.inc").read_bytes(),"negative fixture changed during validation"
        result["sourceHashes"]["native/main.cpp"]=sha((REPO/"native/main.cpp").read_bytes())
        result["pass"]=True
    except Exception as error:
        result["error"]=str(error);print(str(error),file=sys.stderr)
    (run/"result.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    (out/"latest.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result));return 0 if result["pass"] else 1
if __name__=="__main__":sys.exit(main())
