#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "model_adaptation_log.h"
#include "virtual_adaptation_evidence.h"
#include "log_ai_index.h"
using json=nlohmann::json;
using ymcc::virtualAdaptation::FrameSelector;
using ymcc::virtualAdaptation::FrameStamp;
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static thread_local bool tracking=false;
static thread_local unsigned long long allocations=0;
void* operator new(size_t n){if(tracking)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
static json modelAdaptationMachine(){return {{"class","logging-fixture"}};}
static std::atomic<unsigned long long> g_virtualAdaptationBuildFailures{0},g_virtualAdaptationStepBuildFailures{0};
// Existing frame-source leaf only. No provider is constructed or queried here.
static struct Source {
    std::string inputSource;
    unsigned long long readAttemptSequence=0,readStartedTick=0,readCompletedTick=0,readPowerGeneration=0,
        readTopologyGeneration=0,readContinuityGeneration=0;
    unsigned primarySlot=0;bool live=false,readContextStable=false;
    struct {unsigned short wButtons=0;unsigned char bLeftTrigger=0,bRightTrigger=0;short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;}pad;
}*t_commonFrameSource=nullptr;

// ORIGINAL_SELECTOR_CONTROL
// ACTUAL_ADAPTATION_FUNCTIONS

template<class Selector>static void fill(Selector& selector){for(unsigned i=0;i<64;++i){FrameStamp s;s.epoch=1;s.power=7;s.sourceGeneration=i;s.topology=9;selector.select(s);}}
static FrameStamp stamp(unsigned shape,unsigned long long source){FrameStamp s;s.epoch=1;s.power=7;s.sourceGeneration=source;s.topology=9;
    s.neutral=bool(shape&1);s.accepted=bool(shape&2);s.sourceAdmitted=bool(shape&4);s.commandSequence=100+shape;s.wireSample=200+shape;s.wireTick=300+shape;
    s.captureBinding("run-before-submit","host-before-submit");return s;}
template<class Selector>static unsigned long long cycles(Selector& selector,unsigned iterations,unsigned mode){
    ULONG64 begin=0,end=0;if(!QueryThreadCycleTime(GetCurrentThread(),&begin))return 0;
    FrameStamp s=stamp(6,1000);
    for(unsigned i=0;i<iterations;++i){s.sourceGeneration=mode==0?1000:mode==1?1000+i:i%64;(void)selector.select(s);}
    if(!QueryThreadCycleTime(GetCurrentThread(),&end))return 0;return end-begin;
}
int main(int argc,char**argv){try{
    if(argc>1){
        if(argc!=2||std::string(argv[1])!="--guide-only")return 2;
        const auto guide=ymcc::logexport::aiReadingGuide();
        check("context-evidence-actual-ai-guide-keeps-bounded-first-rejection-interpretation",guide["virtualAdaptation"].contains("frameContextEvidence")&&guide["virtualAdaptation"]["frameContextEvidence"].get<std::string>().find("not first per rejected context")!=std::string::npos&&guide["virtualAdaptation"]["frameCapacityHighestPowerEvidence"].get<std::string>().find("not necessarily current power")!=std::string::npos);
        const bool pass=checks[0]["pass"].get<bool>();
        std::cout<<json{{"selectedCase","adaptation-facts"},{"boundarySelection","selector-context-guide"},{"checks",checks},{"failures",pass?0:1},{"exportGuide",guide},{"productOrHardwareExecuted",false}}.dump(2)<<'\n';return pass?0:3;
    }

    FrameSelector candidate;ymcc::virtualAdaptation::FrameSelectorBefore control;bool equal=true;
    for(unsigned i=0;i<200000;++i){FrameStamp s=stamp(i%8,(i*17u)%151);s.epoch=i%31?1:0;s.power=i%11?7:8;s.topology=i%7?9:10;
        equal&=candidate.select(s)==control.select(s);}
    check("context-evidence-selection-return-and-original-counters-match-uninstrumented-source",equal&&candidate.selected()==control.selected()&&candidate.capacityDrops()==control.capacityDrops()&&candidate.unknownDrops()==control.unknownDrops()&&candidate.knownContexts()==control.knownContexts());
    FrameSelector first;fill(first);auto initial=first.snapshotForExport();
    check("context-evidence-initial64-tuples-retain-exact-fields-and-no-rejection-witness",initial.available&&initial.knownContexts==64&&initial.capacityShapeMask==0&&initial.contexts[63].source==63&&initial.contexts[63].selectedOutcomeMask==1);
    auto rejected=stamp(6,1000);const bool admitted=first.select(rejected);auto rejectedSnapshot=first.snapshotForExport();
    check("context-evidence-first-capacity-rejection-keeps-exact-request-tuple-and-outcome",!admitted&&first.knownContexts()==64&&rejectedSnapshot.capacityShapeMask==(1u<<6)&&rejectedSnapshot.capacityFirst[6].sourceGeneration==1000&&rejectedSnapshot.capacityFirst[6].commandSequence==106&&std::string(rejectedSnapshot.capacityFirst[6].runId.data())=="run-before-submit");
    allocations=0;tracking=true;for(unsigned i=0;i<50000;++i){rejected.commandSequence=10000+i;rejected.wireSample=i;rejected.sourceGeneration=2000+i;first.select(rejected);}tracking=false;
    auto repeat=first.snapshotForExport();
    check("context-evidence-50000-rejections-no-heap-no-overwrite-no-new-contexts",allocations==0&&first.capacityDrops()==50001&&repeat.knownContexts==64&&repeat.capacityShapeMask==(1u<<6)&&repeat.capacityFirst[6].sourceGeneration==1000&&repeat.capacityFirst[6].commandSequence==106);
    for(unsigned shape=0;shape<8;++shape){auto s=stamp(shape,3000+shape);if(shape==7)s.captureBinding(std::string(128,'X'),"short-host");first.select(s);}
    const auto all=first.snapshotForExport();
    check("context-evidence-eight-shapes-bounded-across-arbitrary-unretained-tuples",all.capacityShapeMask==255&&all.knownContexts==64&&all.capacityFirst[0].sourceGeneration==3000&&all.capacityFirst[6].sourceGeneration==1000&&all.capacityFirst[7].bindingTruncated&&all.capacityFirst[7].runId[0]==0);
    auto higher=stamp(6,4000);higher.power=8;
    const bool selectedHigher=first.select(higher);
    auto older=stamp(7,4001);first.select(older);
    auto noPower=stamp(7,4002);noPower.power=0;first.select(noPower);
    allocations=0;tracking=true;
    for(unsigned i=0;i<50000;++i){auto repeatHigher=higher;repeatHigher.sourceGeneration=5000+i;first.select(repeatHigher);}
    tracking=false;
    const auto newer=first.snapshotForExport();
    check("highest-power-first-not-replaced-by-late-older-unknown-or-50000-repeats",
        !selectedHigher&&allocations==0&&newer.capacityHighestPower==8&&newer.capacityHighestPowerMask==(1u<<6)&&
        newer.capacityHighestPowerFirst[6].sourceGeneration==4000&&newer.capacityShapeMask==255&&
        newer.capacityFirst[6].sourceGeneration==1000&&newer.knownContexts==64&&newer.contexts[0].power==7);
    higher=stamp(7,4003);higher.power=8;first.select(higher);
    auto known=stamp(7,0);const auto selectedBefore=first.selected(),droppedBefore=first.capacityDrops();const bool newKnown=first.select(known);
    const auto after=first.snapshotForExport();
    check("context-evidence-known-context-new-outcome-still-selected-after-saturation",newKnown&&first.selected()==selectedBefore+1&&first.capacityDrops()==droppedBefore&&after.contexts[0].selectedOutcomeMask==(1|128)&&all.contexts[0].selectedOutcomeMask==1);
    allocations=0;tracking=true;const auto noHeapSnapshot=first.snapshotForExport();tracking=false;
    check("context-evidence-export-snapshot-is-fixed-storage-without-json-under-lock",noHeapSnapshot.available&&allocations==0);
    const auto projected=virtualAdaptationContextEvidence(noHeapSnapshot);
    const auto& highest=projected["capacityHighestPowerEvidence"];
    check("highest-power-export-only-has-shapes-from-that-generation",highest["schemaVersion"]==1&&highest["power"]==8&&
        highest["firstOutcomes"].size()==2&&highest["firstOutcomes"][0]["sourceContinuityGeneration"]==4000&&
        highest["firstOutcomes"][1]["sourceContinuityGeneration"]==4003&&
        highest["firstOutcomes"][1]["power"]==8&&projected["capacityFirstOutcomes"][7]["power"]==7);
    check("context-evidence-actual-projection-contains64-retained-and8-rejected-metadata-only",projected["retainedContexts"].size()==64&&projected["capacityFirstOutcomes"].size()==8&&projected["selectionOnly"]==true&&projected["snapshotAtomicWithSiblingStats"]==false&&projected.dump().size()<=32768);
    const auto& witness=projected["capacityFirstOutcomes"][6];
    check("context-evidence-projection-preserves-shape-encoding-and-native-not-game-scope",witness["shape"]==6&&witness["nativeAccepted"]==true&&witness["sourceAdmitted"]==true&&witness["semanticNeutral"]==false&&witness["sourceContinuityGeneration"]==1000&&witness["selectionDecision"]=="capacity-rejected"&&!witness.contains("gameConsumptionProven")&&!witness.contains("raw"));
    const FrameSelector::ExportSnapshot unavailable;const auto unknown=virtualAdaptationContextEvidence(unavailable);
    check("context-evidence-unavailable-snapshot-does-not-pretend-zero-contexts-or-success",unknown["snapshotAvailable"]==false&&!unknown.contains("knownContextsAtSnapshot")&&!unknown.contains("retainedContexts")&&!unknown.contains("capacityFirstOutcomes")&&!unknown.contains("capacityHighestPowerEvidence"));
    FrameSelector noEpoch;FrameStamp zero;noEpoch.select(zero);const auto noEpochSnapshot=noEpoch.snapshotForExport();
    check("context-evidence-unknown-epoch-is-separate-from-capacity-witness",noEpoch.unknownDrops()==1&&noEpoch.capacityDrops()==0&&noEpochSnapshot.capacityShapeMask==0&&noEpochSnapshot.knownContexts==0);
    const auto noRejection=virtualAdaptationContextEvidence(noEpochSnapshot)["capacityHighestPowerEvidence"];
    check("highest-power-empty-readable-snapshot-reports-null-never-zero-power",noRejection["power"].is_null()&&noRejection["firstOutcomes"].empty());
    // Exercise the original complete receipt bridge, including the independent
    // SDK-witness bypass. No source or device callbacks are installed.
    fill(virtualAdaptationSelector());const auto factsBefore=virtualAdaptationFacts().status();auto saturated=stamp(6,99999);
    const json frame={{"buttons",1}},receipt=json::object();const std::string wire="{\"command\":\"SUBMIT_FRAME\",\"buttons\":1,\"startupNonce\":\"not-retained\"}",request="request-first-rejected";
    allocations=0;tracking=true;virtualAdaptationFrameReceipt(saturated,frame,wire,request,receipt);tracking=false;
    check("context-evidence-rejected-receipt-still-skips-json-and-fact-build",allocations==0&&virtualAdaptationFacts().status()==factsBefore&&virtualAdaptationSelector().capacityDrops()==1);
    const json sdk={{"sdkFirstWitnesses",json::array({{{"outcome","returned"},{"sample",{{"ordinal",1},{"selection","live"}}}}})}};
    virtualAdaptationFrameReceipt(saturated,frame,wire,request,sdk);size_t index=0;ymcc::adaptlog::Fact stored;
    const bool taken=virtualAdaptationFacts().take(index,stored);
    check("context-evidence-sdk-witness-still-bypasses-selector-capacity-rejection",taken&&stored.category=="virtual-sdk-first-witness"&&stored.sample["requestId"]==request&&stored.sample["replayRequest"]["buttons"]==1&&!stored.sample["replayRequest"].contains("startupNonce")&&virtualAdaptationSelector().capacityDrops()==2&&g_virtualAdaptationBuildFailures==0);
    const auto status=virtualAdaptationStatus();
    check("context-evidence-actual-export-status-links-selector-evidence-and-existing-drop-counts",status["frameContextEvidence"]["knownContextsAtSnapshot"]==64&&status["frameContextEvidence"]["capacityFirstOutcomes"].size()==1&&status["frameCapacityDrops"]==2&&status["frameContextLimitReached"]==true);
    json performance=json::array();bool cost=true;
    for(unsigned mode=0;mode<3;++mode){std::vector<double> ratios;json samples=json::array();
        for(unsigned pair=0;pair<7;++pair){FrameSelector tested;ymcc::virtualAdaptation::FrameSelectorBefore before;fill(tested);fill(before);
            auto warm=stamp(6,1000);tested.select(warm);before.select(warm);
            if(mode==2)for(unsigned i=0;i<64;++i){auto s=stamp(6,i);tested.select(s);before.select(s);}
            unsigned long long a=0,b=0;if(pair%2){a=cycles(tested,300000,mode);b=cycles(before,300000,mode);}else{b=cycles(before,300000,mode);a=cycles(tested,300000,mode);}
            if(a&&b)ratios.push_back(double(a)/double(b));samples.push_back({{"candidateCycles",a},{"controlCycles",b}});
        }
        std::sort(ratios.begin(),ratios.end());const double median=ratios.empty()?0:ratios[ratios.size()/2];cost&=ratios.size()==7&&median<=1.10;
        performance.push_back({{"mode",mode==0?"same-rejected-tuple":mode==1?"new-rejected-tuples":"known-context-duplicate"},{"iterationsPerSide",300000},{"pairs",samples},{"medianCandidateOverControlCycles",median},{"maximumMedianRatio",1.10}});
    }
    check("context-evidence-original-selection-hotpaths-stay-within-declared10percent-cycle-bound",cost);
    unsigned failed=0;for(const auto& c:checks)failed+=!c["pass"].get<bool>();
    std::cout<<json{{"selectedCase","adaptation-facts"},{"boundarySelection","selector-context-evidence"},{"checks",checks},{"failures",failed},
        {"performance",performance},{"performanceScope","isolated same-thread selector/control cycles;not whole-app or logger CPU;two 8-shape banks"},
        {"exportProjection",projected},{"virtualExportStatus",status},{"productOrHardwareExecuted",false}}.dump(2)<<'\n';return failed?3:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
