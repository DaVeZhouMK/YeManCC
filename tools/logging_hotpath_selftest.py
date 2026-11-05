"""Offline actual logging hot-path comparison; no YMCC/device process is launched.

Runs extracted before/after producers/classifiers with fake clocks and log sinks.
Counts allocations after warm-up and measures current-thread cycles/wall time.
These microbenchmarks are not whole-app or G3E CPU utilization measurements.
"""
from __future__ import annotations
import argparse, datetime, hashlib, json, pathlib, subprocess
from log_frequency_selftest import ROOT, extract

SIGNATURES = [
    'static void inputCaptureCommonFrameEvidence(const json& hostFrame,bool semanticNeutral) noexcept {',
    'static bool nativeLifecycleEventIsBase(const char* event) {',
    'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
]
PRELUDE = r'''
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
using json=nlohmann::json;
static bool countAllocations=false;
static unsigned long long allocations=0;
void* operator new(std::size_t n) {if(countAllocations)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static bool detailEnabled=true;
static bool domainLogEnabled(const char*){return detailEnabled;}
static ULONGLONG fixtureTick=1000;
#define GetTickCount64() fixtureTick
struct Sample {
    std::string inputSource="xinput";
    unsigned long long readTopologyGeneration=25,readPowerGeneration=1,readAttemptSequence=400,readStartedTick=990,readCompletedTick=999;
    unsigned primarySlot=0,connectedMask=1;
    bool live=true,readContextStable=true,specialXbox=false;
    unsigned physicalReaderPid=0,physicalReaderPacket=0,physicalReaderActualSlot=0;
    unsigned long long physicalReaderSequence=0,readMsiSourceRevision=0,readMsiModeGeneration=0;
    struct RawPad {unsigned short wButtons=0;unsigned char bLeftTrigger=0,bRightTrigger=0;
        short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;} physicalReaderRawPad;
};
static Sample sample;
static Sample* t_commonFrameSource=&sample;
static std::atomic<unsigned long long> g_powerGeneration{1},g_xinputTopologyEpoch{25};
static std::mutex g_hidHidePhysicalIdentityMx;
static struct Identity{bool resolved=true;} g_hidHidePhysicalIdentity;
static unsigned identityQueries=0;
static json hidHideIdentityJson(const Identity&){++identityQueries;return {{"physicalIdentity","fixture-only-long-identity"}};}
static struct Machine{int family=6;std::string product="MS-1T91";}g_machineIdentity;
static const char* ymccFamilyName(int){return "msi-claw";}
static unsigned long long g_commonFrameEvidenceRows=0,g_commonFrameEvidenceFailures=0;
static bool collectRows=true;
static std::vector<json> rows;
static void appendNativeLifecycleLog(const char* event,json detail){if(collectRows){detail["event"]=event;rows.push_back(std::move(detail));}}
static json checks=json::array(),timings=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
template<class Work> static void measure(const char* label,unsigned count,Work work) {
    for(unsigned i=0;i<100;++i)work();
    allocations=0;ULONG64 c0=0,c1=0;QueryThreadCycleTime(GetCurrentThread(),&c0);
    const auto start=std::chrono::steady_clock::now();countAllocations=true;
    for(unsigned i=0;i<count;++i)work();
    countAllocations=false;const auto end=std::chrono::steady_clock::now();QueryThreadCycleTime(GetCurrentThread(),&c1);
    timings.push_back({{"path",label},{"calls",count},{"allocations",allocations},{"threadCycles",c1-c0},
        {"wallUs",std::chrono::duration<double,std::micro>(end-start).count()}});
}
'''
TESTS = r'''
static json frame={{"hostSubmission","frame-accepted"},{"frameProof",{
    {"sampleSequence",1},{"motionPairProofId","pair-unproven-safe-zero"},{"hostReceipt",{
        {"hostInstanceId","fixture-host-instance-with-long-identity"},{"runId","fixture-run-instance-with-long-identity"},
        {"epoch",1},{"commandSequence",5},{"sampleSequence",1},{"hostAckAccepted",true}}}}}};
static volatile unsigned lookupSink=0;
template<class Emit> static std::vector<json> exercise(Emit emit,bool includeNeutralOnly=true) {
    rows.clear();sample=Sample{};t_commonFrameSource=&sample;detailEnabled=true;fixtureTick=1000;
    auto f=frame;emit(f,false);emit(f,false); // first + immediate repeat
    fixtureTick=1999;emit(f,false);fixtureTick=2000;emit(f,false); // one-second boundary
    sample.connectedMask=0;emit(f,false);sample.live=false;emit(f,false);
    sample.readContextStable=false;emit(f,false);if(includeNeutralOnly)emit(f,true);
    f["frameProof"]["hostReceipt"]["epoch"]=2;emit(f,true);
    f["frameProof"]["hostReceipt"]["runId"]="fixture-second-run";emit(f,true);
    f["frameProof"]["sampleSequence"]=900;emit(f,true); // rolling counters are not transitions
    t_commonFrameSource=nullptr;emit(f,true);fixtureTick=3000;emit(f,true);
    detailEnabled=false;fixtureTick=5000;emit(f,true);detailEnabled=true;
    t_commonFrameSource=&sample;
    return rows;
}
int main() {
    auto oldRows=exercise(before::inputCaptureCommonFrameEvidence,false);
    auto newRows=exercise(after::inputCaptureCommonFrameEvidence);
    bool baselineLabelsMatch=oldRows.size()==newRows.size();
    for(size_t i=0;i<oldRows.size()&&i<newRows.size();++i){
        // Old baselines may predate snapshotReason; current baselines carry it.
        if(oldRows[i].contains("snapshotReason"))baselineLabelsMatch &= oldRows[i]["snapshotReason"]==newRows[i]["snapshotReason"];
        oldRows[i].erase("snapshotReason");oldRows[i].erase("transitionScope");oldRows[i]["formatVersion"]=1;
    }
    unsigned periodic=0,changed=0;
    for(auto& row:newRows){if(row["snapshotReason"]=="periodic")++periodic;else if(row["snapshotReason"]=="state-change")++changed;
        row.erase("snapshotReason");row.erase("transitionScope");row["formatVersion"]=1;}
    check("producer-preserves-context-fields-except-neutral-only-activity",baselineLabelsMatch && oldRows==newRows && newRows.size()==9);
    check("producer-labels-two-periodic-and-seven-context-observations",periodic==2&&changed==7);
    check("producer-has-no-exceptions",g_commonFrameEvidenceFailures==0);
    const auto failuresBefore=g_commonFrameEvidenceFailures;const auto rowsBefore=rows.size();
    for(const json& invalid:{json(nullptr),json({{"frameProof",42}}),json({{"hostSubmission",42}})}){
        before::inputCaptureCommonFrameEvidence(invalid,false);after::inputCaptureCommonFrameEvidence(invalid,false);
    }
    check("malformed-log-input-keeps-failure-counter-and-produces-no-false-observation",g_commonFrameEvidenceFailures==failuresBefore+6 && rows.size()==rowsBefore);
    rows.clear();sample=Sample{};t_commonFrameSource=&sample;fixtureTick=4000;
    auto activityFrame=frame;activityFrame["frameProof"]["hostReceipt"]["epoch"]=700;
    after::inputCaptureCommonFrameEvidence(activityFrame,false);rows.clear();allocations=0;countAllocations=true;
    for(unsigned i=0;i<50000;++i)after::inputCaptureCommonFrameEvidence(activityFrame,(i&1)!=0);
    countAllocations=false;
    check("50000-neutral-flips-no-resident-row-or-allocation",rows.empty()&&allocations==0);
    fixtureTick=5000;after::inputCaptureCommonFrameEvidence(activityFrame,true);
    ++sample.readTopologyGeneration;after::inputCaptureCommonFrameEvidence(activityFrame,true);
    check("activity-retained-in-periodic-and-real-source-transition-immediate",rows.size()==2&&rows[0]["snapshotReason"]=="periodic"&&rows[0]["semanticNeutral"]==true&&rows[1]["snapshotReason"]=="state-change");
    // Exact snapshot semantics including unknown fields, nested dynamic keys and types.
    using Kind=ymcc::logging::SnapshotKind;
    ymcc::logging::JsonSnapshotLogState snapshot;
    json state={{"state",{{"events",1},{"enabled",true},{"futureField",{{"seq",1}}}}},{"events",1}};
    check("snapshot-first-observation",snapshot.observe(state,1,Kind::screen));
    state["state"]["events"]=42;
    check("snapshot-ignores-only-named-state-counter",!snapshot.observe(state,1,Kind::screen));
    state["events"]=2;check("root-counter-name-is-semantic",snapshot.observe(state,1,Kind::screen));
    state["state"]["futureField"]["seq"]=2;check("unknown-nested-field-is-semantic",snapshot.observe(state,1,Kind::screen));
    state["state"]["enabled"]=1.0;check("snapshot-type-change-visible",snapshot.observe(state,1,Kind::screen));
    state["state"]["enabled"]=1;check("snapshot-integer-vs-float-visible",snapshot.observe(state,1,Kind::screen));
    check("snapshot-new-power-generation-visible",snapshot.observe(state,2,Kind::screen));
    // Compare old/current classifiers for the established lane cases.
    bool laneMatch=true;
    for(const char* event:{"rog.hid-bound","rog.hid-discovery-summary","gamepad.feedbackCaps","screen-touchpads-state","desktop-hud-event"}){
        json value={{"known",true},{"path","fixture-long-device-path"},{"state",{{"events",0},{"enabled",true}}},{"hud",{{"seq",0},{"visible",true}}}};
        for(int i=0;i<6;++i){if(i==2){value["state"]["events"]=1;value["hud"]["seq"]=1;}if(i==3)value["known"]=false;if(i==4)++g_powerGeneration;
            laneMatch &= before::nativeLifecycleRecordIsStreaming(event,value)==after::nativeLifecycleRecordIsStreaming(event,value);}
    }
    check("established-snapshot-lanes-match-baseline",laneMatch);
    check("periodic-frame-is-streaming",after::nativeLifecycleRecordIsStreaming("input-host-frame-evidence",{{"snapshotReason","periodic"}}));
    check("state-frame-stays-resident",!after::nativeLifecycleRecordIsStreaming("input-host-frame-evidence",{{"snapshotReason","state-change"}}));
    check("legacy-unknown-frame-stays-resident",!after::nativeLifecycleRecordIsStreaming("input-host-frame-evidence",json::object()));
    collectRows=false;sample=Sample{};t_commonFrameSource=&sample;fixtureTick=100000;detailEnabled=true;
    before::inputCaptureCommonFrameEvidence(frame,false);after::inputCaptureCommonFrameEvidence(frame,false);
    const unsigned beforeQueries=identityQueries;
    for(int repeat=0;repeat<5;++repeat){
        if(repeat%2==0){
            measure("frame-before",100000,[]{before::inputCaptureCommonFrameEvidence(frame,false);});
            measure("frame-after",100000,[]{after::inputCaptureCommonFrameEvidence(frame,false);});
        }else{
            measure("frame-after",100000,[]{after::inputCaptureCommonFrameEvidence(frame,false);});
            measure("frame-before",100000,[]{before::inputCaptureCommonFrameEvidence(frame,false);});
        }
    }
    check("unchanged-frame-does-not-query-physical-identity",identityQueries==beforeQueries);
    detailEnabled=false;
    measure("frame-off-after",100000,[]{after::inputCaptureCommonFrameEvidence(frame,false);});
    static const json largeSnapshot={{"bound",false},{"family","fixture-family-long-name"},{"path","fixture-path-with-long-identity"},
        {"capabilities",{{"input",true},{"feature",true},{"buttons",json::array({1,2,3,4,5})}}}};
    for(int repeat=0;repeat<5;++repeat){
        measure("snapshot-before",20000,[]{lookupSink=before::nativeLifecycleRecordIsStreaming("rog.hid-bound",largeSnapshot);});
        measure("snapshot-after",20000,[]{lookupSink=after::nativeLifecycleRecordIsStreaming("rog.hid-bound",largeSnapshot);});
    }
    for(int repeat=0;repeat<5;++repeat){
        measure("registry-before",100000,[]{lookupSink=before::nativeLifecycleEventIsBase("unregistered-fixture-sample");});
        measure("registry-after",100000,[]{lookupSink=after::nativeLifecycleEventIsBase("unregistered-fixture-sample");});
    }

    // Same field sequence as the writer regression, measured without disk I/O.
    json queuedWork={{"action","consuming"},{"requestId",3},{"originalDeadline",10000},{"persona","steamdeck"}};
    json queuedDequeue={{"beat","dequeue"},{"reason","initial-target-admission"},{"requestedAtDequeue","steamdeck"},{"waitedMs",4000},{"deferCount",1}};
    json queuedSettle={{"reason","initial-target-admission"},{"policy","queue-not-reject-coalesce-on-next-round"},{"deferMs",1000000},{"settleMs",1000000}};
    json queuedMerge={{"outcome","coalesced"},{"coalesced",true},{"queueToken",4},{"runId","field-run"},{"hostGen",2},{"sourceGen",390},{"recordGen",7}};
    for(int repeat=0;repeat<3;++repeat){
        measure("queued-wait-before",20000,[&]{
            lookupSink=before::nativeLifecycleRecordIsStreaming("input-host-player-start-work",queuedWork);
            lookupSink=before::nativeLifecycleRecordIsStreaming("input-host-switch-audit",queuedDequeue);
            lookupSink=before::nativeLifecycleRecordIsStreaming("input-host-switch-settle",queuedSettle);
            lookupSink=before::nativeLifecycleRecordIsStreaming("late-hide-enqueue",queuedMerge);
        });
        measure("queued-wait-after",20000,[&]{
            queuedDequeue["waitedMs"]=queuedDequeue["waitedMs"].get<unsigned long long>()+1;
            queuedDequeue["deferCount"]=queuedDequeue["deferCount"].get<unsigned long long>()+1;
            queuedSettle["deferMs"]=queuedSettle["deferMs"].get<unsigned long long>()-1;
            lookupSink=after::nativeLifecycleRecordIsStreaming("input-host-player-start-work",queuedWork);
            lookupSink=after::nativeLifecycleRecordIsStreaming("input-host-switch-audit",queuedDequeue);
            lookupSink=after::nativeLifecycleRecordIsStreaming("input-host-switch-settle",queuedSettle);
            lookupSink=after::nativeLifecycleRecordIsStreaming("late-hide-enqueue",queuedMerge);
        });
    }

    ymcc::logging::IdentityObservationLogState identities;
    ymcc::logging::IdentityObservationView collectionA{{"collection-one","instance-one","shared-container"},{0x17ef,0x61ed,1,5,1,1}};
    auto collectionB=collectionA;collectionB.text[0]="collection-two";collectionB.text[1]="instance-two";
    check("identity-two-interfaces-first-observations",identities.observe(collectionA)&&identities.observe(collectionB));
    measure("identity-observation-after",100000,[&]{lookupSink=identities.observe(collectionA);lookupSink=identities.observe(collectionB);});
    auto identityEdge=collectionA;identityEdge.numbers[5]=2;
    check("identity-topology-generation-is-an-observation-edge",identities.observe(identityEdge));
    identityEdge.numbers[4]=2;check("identity-power-generation-is-an-observation-edge",identities.observe(identityEdge));
    identities.invalidate(identityEdge.text[0]);check("identity-invalidated-recovery-is-an-observation-edge",identities.observe(identityEdge));
    ymcc::logging::IdentityObservationLogState bounded;std::array<std::string,17> paths;
    for(size_t i=0;i<paths.size();++i){paths[i]="interface-"+std::to_string(i);auto v=collectionA;v.text[0]=paths[i];bounded.observe(v);}
    auto evicted=collectionA;evicted.text[0]=paths[0];
    check("identity-cache-bounded-to-sixteen-and-eviction-reemits-first",bounded.entries.size()==16&&bounded.observe(evicted));
    bool zero=true;for(const auto& row:timings)if(row["path"]=="frame-after"||row["path"]=="frame-off-after"||row["path"]=="snapshot-after"||row["path"]=="registry-after"||row["path"]=="queued-wait-after"||row["path"]=="identity-observation-after")zero &= row["allocations"]==0;
    check("unchanged-off-and-classification-hot-paths-zero-allocations-after-warmup",zero);
    unsigned failures=0;for(const auto& c:checks)if(!c["pass"].get<bool>())++failures;
    std::cout<<json({{"checks",checks},{"failures",failures},{"measurements",timings},
        {"scope","extracted-logging-only;fake-clock-and-sinks;no-disk-or-hardware-in-benchmark;not-field-CPU-percent"}}).dump(2)<<std::endl;
    return failures?3:0;
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',required=True,type=pathlib.Path)
    parser.add_argument('--output-dir',required=True,type=pathlib.Path)
    args=parser.parse_args();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir()
    current=(ROOT/'native/main.cpp').read_bytes();baseline=args.baseline.read_bytes()
    now=current.decode('utf-8-sig').replace('\r\n','\n');old=baseline.decode('utf-8-sig').replace('\r\n','\n')
    headers={n:(ROOT/'native'/n).read_bytes() for n in ('log_frequency_policy.h','log_snapshot_state.h')}
    (run/'bound-main.cpp').write_bytes(current);(run/'baseline-main.cpp').write_bytes(baseline)
    for n,b in headers.items():(run/n).write_bytes(b)
    current_functions=[extract(now,s) for s in SIGNATURES]
    cpp=PRELUDE+'\nnamespace before {\n'+'\n'.join(extract(old,s) for s in SIGNATURES)+'\n}\nnamespace after {\n'+'\n'.join(current_functions)+'\n}\n'+TESTS
    (run/'harness.cpp').write_text(cpp,encoding='utf-8')
    vcvars=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    exe=run/'harness.exe';batch=run/'build.cmd'
    command='call "'+str(vcvars)+'" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(run/'harness.cpp')+'" /I"'+str(run)+'" /I"'+str(ROOT/'deps/json')+'" /Fo"'+str(run/'harness.obj')+'" /Fe"'+str(exe)+'"'
    # Match the writer fixture: installed SDK files can exist without registry discovery.
    sdk_root=pathlib.Path(r"C:\Program Files (x86)\Windows Kits\10")
    installed=sorted((sdk_root/'Include').glob('10.*'),reverse=True)
    sdk=next((p for p in installed if (p/'um/Windows.h').is_file() and (p/'ucrt/stdio.h').is_file()),None)
    if sdk is not None:
        command+=''.join(' /I"'+str(sdk/name)+'"' for name in ('um','shared','ucrt'))
        command+=' /link'+''.join(' /LIBPATH:"'+str(sdk_root/'Lib'/sdk.name/name/'x64')+'"' for name in ('um','ucrt'))
    batch.write_text('@echo off\n'+command+'\n',encoding='utf-8')
    build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:print(build.stdout+build.stderr);return build.returncode
    result=subprocess.run([str(exe)],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'result.json').write_text(result.stdout,encoding='utf-8')
    if result.stderr:(run/'stderr.txt').write_text(result.stderr,encoding='utf-8')
    report=json.loads(result.stdout)
    latest=(ROOT/'native/main.cpp').read_bytes();latest_text=latest.decode('utf-8-sig').replace('\r\n','\n')
    stable=all(extract(latest_text,s)==f for s,f in zip(SIGNATURES,current_functions)) and all((ROOT/'native'/n).read_bytes()==b for n,b in headers.items())
    report.update(boundFunctionsAndHeadersStable=stable,wholeSourceStable=current==latest,sourceSha256=hashlib.sha256(current).hexdigest(),baselineSha256=hashlib.sha256(baseline).hexdigest(),headers={n:hashlib.sha256(b).hexdigest() for n,b in headers.items()},productLaunched=False,hardwareAccess=False,runDirectory=str(run))
    if not stable:report['failures']+=1
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'failures':report['failures'],'checks':len(report['checks']),'report':str(out/'report.json')}))
    return result.returncode or (3 if report['failures'] else 0)

if __name__=='__main__':raise SystemExit(main())
