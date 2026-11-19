#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
static std::atomic<unsigned long long> intentAllocations{0};
static std::atomic<bool> intentFailAllocation{false};
void* operator new(std::size_t n){if(intentFailAllocation.load(std::memory_order_relaxed))throw std::bad_alloc();intentAllocations.fetch_add(1,std::memory_order_relaxed);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using json=nlohmann::json;
static bool fixtureDetailed=true,fixtureMsi=false;
static struct {int family=1;std::string product="fixture-board";}g_machineIdentity;
static bool msiTargetIntentEvidenceEnabled() noexcept{return fixtureDetailed;}
static bool msiClawKnownBoardAuthority() noexcept{return fixtureMsi;}
static const char* ymccFamilyName(int){return fixtureMsi?"msi":"gpd";}
static unsigned long long inputHostEpochToken(){return 7;}
static unsigned long long currentPowerGeneration(){return 9;}
static ULONGLONG intentTick()noexcept{return 111;}
static DWORD intentPid()noexcept{return 77;}
static BoundedObserver g_msiTargetIntentObservation,g_msiTargetIntentWrites;
static std::atomic<unsigned long long> g_msiTargetIntentEvidenceFailures{0};
static json intentRows=json::array();
static void appendNativeLifecycleLog(const char* event,const json& row){intentRows.push_back({{"event",event},{"detail",row}});}
#define GetTickCount64 intentTick
#define GetCurrentProcessId intentPid
// ACTUAL_NATIVE_INTENT_FUNCTIONS
#undef GetCurrentProcessId
#undef GetTickCount64
static void resetIntent() {
    g_msiTargetIntentObservation.~BoundedObserver();new(&g_msiTargetIntentObservation)BoundedObserver;
    g_msiTargetIntentWrites.~BoundedObserver();new(&g_msiTargetIntentWrites)BoundedObserver;
    intentRows=json::array();g_msiTargetIntentEvidenceFailures.store(0);fixtureDetailed=true;fixtureMsi=false;
}
static int runIntentObservationPreflight(){
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    json checks=json::array(),phases=json::array();
    auto check=[&](bool ok,const char* name){checks.push_back({{"name",name},{"pass",ok}});if(!ok)throw std::runtime_error(name);};
    auto phase=[&](const char* name){phases.push_back({{"name",name},{"rows",intentRows},{"observation",g_msiTargetIntentObservation.statistics()},{"writes",g_msiTargetIntentWrites.statistics()},{"failures",g_msiTargetIntentEvidenceFailures.load()}});};
    const json saved={{"revision",20},{"outputTarget",{{"persona","steamdeck"},{"buttonMappingEnabled",true},{"gyroEnabled",true}}}};
    try {
        resetIntent();msiTargetIntentObserveInput(saved,saved,false,false);const auto allocationsBefore=intentAllocations.load();
        for(unsigned i=0;i<1000;++i)msiTargetIntentObserveInput(saved,saved,false,false);
        const auto normalRepeatAllocations=intentAllocations.load()-allocationsBefore;
        check(intentRows.size()==1&&g_msiTargetIntentEvidenceFailures.load()==0,"normal1000-identical-source-calls-keep-one-original-row");phase("normal-repeat");
        bool zeroRepeat=normalRepeatAllocations==0;

        resetIntent();msiTargetIntentObserveInput(saved,saved,false,false);const auto allocationSeed=intentAllocations.load();intentFailAllocation.store(true);
        for(unsigned i=0;i<50000;++i)msiTargetIntentObserveInput(saved,saved,false,false);
        intentFailAllocation.store(false);const auto failedRepeatAllocations=intentAllocations.load()-allocationSeed;const auto repeatFailures=g_msiTargetIntentEvidenceFailures.load();
        check(intentRows.size()==1,"50000-identical-source-calls-never-add-repeat-rows");
        const bool noRepeatedPreparation=repeatFailures==0&&failedRepeatAllocations==0;

        resetIntent();fixtureDetailed=false;intentFailAllocation.store(true);for(unsigned i=0;i<1000;++i)msiTargetIntentObserveInput(saved,saved,false,false);intentFailAllocation.store(false);
        check(intentRows.empty()&&g_msiTargetIntentEvidenceFailures.load()==0,"closed-original-detail-gate-stays-zero-work");phase("detail-off");

        resetIntent();std::vector<json> shapes={nullptr,false,json::array(),json::object(),{{"outputTarget",nullptr}},{{"outputTarget",json::array()}},{{"outputTarget",json::object()}}};
        for(const json& persona:std::vector<json>{nullptr,17,false,"","steamdeck",std::string(32,'A'),std::string(33,'B'),"\xe4\xb8\xad"}){
            auto value=saved;value["outputTarget"]["persona"]=persona;shapes.push_back(std::move(value));}
        for(const auto* key:{"buttonMappingEnabled","gyroEnabled"})for(const json& flag:std::vector<json>{nullptr,0,1,1.0,"true",false,true}){auto value=saved;value["outputTarget"][key]=flag;shapes.push_back(std::move(value));}
        const auto originalSaved=saved;for(unsigned i=0;i<shapes.size();++i)for(unsigned mode=0;mode<4;++mode){msiTargetIntentObserveInput(shapes[i],shapes[(i+mode)%shapes.size()],(mode&1)!=0,(mode&2)!=0);msiTargetIntentObserveInput(shapes[i],shapes[(i+mode)%shapes.size()],(mode&1)!=0,(mode&2)!=0);}
        check(saved==originalSaved&&intentRows.size()<=128,"normalization-shapes-and-shortcut-overlay-flags-remain-bounded-and-nonmutating");phase("normalization-matrix");

        resetIntent();msiTargetIntentObserveInput(saved,saved,false,false);json revisionOnly=saved;revisionOnly["revision"]=21;revisionOnly["unrelated"]="ignored";
        msiTargetIntentObserveInput(revisionOnly,revisionOnly,false,false);auto longer=saved;longer["revision"]=9007199254740992ULL;msiTargetIntentObserveInput(longer,longer,false,false);
        check(intentRows.size()==1,"revision-only-and-unrelated-input-fields-still-do-not-reoffer");phase("revision-only");

        resetIntent();json effective=saved;effective["outputTarget"]["gyroEnabled"]=false;
        for(unsigned i=0;i<200;++i)msiTargetIntentObserveInput(saved,(i&1)?saved:effective,false,false);
        const auto cap=g_msiTargetIntentObservation.statistics();check(intentRows.size()==128&&cap["droppedForCap"]==72&&cap["reservedRows"]==128,"original128-cap-and72-changed-drops-unchanged");phase("cap");

        resetIntent();json requested=saved;requested["outputTarget"]["persona"]="disabled";
        const json args={{"content",json{{"input",requested}}.dump()},{"baseline",{{"input",saved}}}};const json committed={{"input",requested}};
        for(unsigned i=0;i<4;++i)msiTargetIntentDocumentWrite(args,committed,true,false,-1,20);
        const json unrelated={{"content",json{{"input",saved}}.dump()},{"baseline",{{"input",saved}}}};msiTargetIntentDocumentWrite(unrelated,{{"input",saved}},true,false,-1,20);
        check(intentRows.size()==4&&g_msiTargetIntentWrites.statistics()["reservedRows"]==4,"each-original-document-write-receipt-kept-and-unrelated-write-still-skipped");phase("writes");

        resetIntent();fixtureMsi=true;msiTargetIntentObserveInput(saved,saved,false,false);check(intentRows.size()==1&&intentRows[0]["event"]=="msi-target-intent-evidence","original-msi-event-and-family-metadata-unchanged");phase("msi");

        resetIntent();intentFailAllocation.store(true);msiTargetIntentObserveInput(saved,saved,false,false);intentFailAllocation.store(false);const auto firstFailures=g_msiTargetIntentEvidenceFailures.load();msiTargetIntentObserveInput(saved,saved,false,false);
        check(firstFailures==1&&intentRows.size()==1&&g_msiTargetIntentObservation.statistics()["reservedRows"]==1,"first-preparation-failure-does-not-consume-signature-or-first-row");phase("prepare-recovery");

        resetIntent();msiTargetIntentObserveInput(saved,saved,false,false);std::vector<std::thread> threads;
        for(unsigned i=0;i<4;++i)threads.emplace_back([&]{for(unsigned j=0;j<1000;++j)msiTargetIntentObserveInput(saved,saved,false,false);});for(auto& thread:threads)thread.join();
        check(intentRows.size()==1&&g_msiTargetIntentEvidenceFailures.load()==0,"concurrent-identical-source-offers-use-existing-owner-lock-and-cap");phase("concurrent-repeat");
        GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check(handlesBefore==handlesAfter,"all-own-threads-and-handles-returned");
        checks.push_back({{"name","repeated-observation-preflight-must-avoid-json-allocation"},{"pass",zeroRepeat&&noRepeatedPreparation}});
        unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        std::cout<<json{{"checks",checks},{"phases",phases},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"normal1000RepeatAllocations",normalRepeatAllocations},{"failed50000RepeatAllocations",failedRepeatAllocations},{"failed50000RepeatPreparationFailures",repeatFailures},{"hardwareActions",0},{"settingsWrites",0},{"ports","metadata and final append values;actual full original2 native producer functions and BoundedObserver"}}.dump()<<std::endl;return failed?2:0;
    }catch(const std::exception& error){intentFailAllocation.store(false);std::cerr<<error.what()<<std::endl;return 3;}
}
