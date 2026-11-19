#include <memory>
#include "touch_feedback_lease.h"
@@MSI_DIAG@@;
struct HidHideMsiMainSourceRecord {unsigned long long generation=1;std::string interfacePath=std::string(160,'p');};
static auto msiFixtureSource=std::make_shared<HidHideMsiMainSourceRecord>();
static bool msiFixtureManaged=true,msiFixtureOpenSucceeds=false,msiFixtureWriteSucceeds=true,msiFixturePreparationFault=false;
static unsigned msiFixtureOpenCalls=0,msiFixtureWriteCalls=0;
static std::shared_ptr<const HidHideMsiMainSourceRecord> msiDinputFeedbackSourceSnapshot(){return msiFixtureManaged?msiFixtureSource:nullptr;}
static bool msiDinputFeedbackSourceCurrent(const std::shared_ptr<const HidHideMsiMainSourceRecord>& p){return p==msiFixtureSource;}
static std::atomic<bool> g_exitRequested{false},g_msiFeedbackLeaseInvalidated{false};
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{1},g_xinputTopologyEpoch{1};
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static bool hardwareWriteAllowed(){return true;}
namespace ymcc::game_feedback {struct SourceBoundCommand {};}
static bool nativeGameFeedbackCommandCurrent(const ymcc::game_feedback::SourceBoundCommand&){return true;}
struct MsiFeedbackLeaseOps {
    using Spec=HidHideMsiMainSourceRecord;
    struct Resource {MsiClawHidDiag diag;};
    static std::uint64_t now()noexcept{return 0;}
    static Resource* open(const Spec&,std::uint32_t& error){++msiFixtureOpenCalls;static Resource resource;
        if(!msiFixtureOpenSucceeds){error=5;return nullptr;}return &resource;}
    static bool pending(const Resource&)noexcept{return false;}
    static void cancel(Resource&)noexcept{}
    static bool reap(Resource&)noexcept{return true;}
    static void close(Resource*)noexcept{}
    static void quarantine(Resource*)noexcept{std::abort();}
    static ymcc::touchhaptic::Receipt write(Resource&,unsigned,unsigned){++msiFixtureWriteCalls;ymcc::touchhaptic::Receipt value;
        value.attempted=true;value.ok=msiFixtureWriteSucceeds;value.reason=value.ok?ymcc::touchhaptic::Reason::None:ymcc::touchhaptic::Reason::WriteFailed;value.error=value.ok?0:29;return value;}
};
static std::mutex g_msiFeedbackLeaseMx;
static ymcc::touchhaptic::FeedbackLease<MsiFeedbackLeaseOps> g_msiFeedbackLease;
static ymcc::touchhaptic::FailureLogGate g_msiFeedbackFailureLogGate;
static void msiFailurePreparationFaultPoint(){if(msiFixturePreparationFault){msiFixturePreparationFault=false;ymcc::testing::allocationFault::failAfter(0);}}
@@MSI_RESULT@@;
@@MSI_WRITE@@
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);fspath::create_directories(root);g_testRoot=root.wstring();
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
    auto verify=[](bool ok,const char* name){check(ok,name);return ok;};
    auto finish=[&]{closeStreams();if(!GetProcessHandleCount(GetCurrentProcess(),&after))return 3;
        check(before==after,"MSI diagnostic fixture returns all stream handles");unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
        std::cout<<json{{"selectedCase","native-sink-failure"},{"selectedBoundary","msi-feedback"},{"checks",checks},{"failures",failed},
            {"handlesBefore",before},{"handlesAfter",after},{"evidence",exportEvidence()},{"openEffectPortCalls",msiFixtureOpenCalls},
            {"writeEffectPortCalls",msiFixtureWriteCalls},{"hardwareWrites",0},{"productSdkDeviceSleepExecuted",false}}.dump(2)<<'\n';return failed?2:0;};
    const auto first=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    const bool resident=readRecoveryRows(root).size()==1&&readRecoveryRows(root).at(0)["event"]=="msi-dinput-feedback";
    if(!verify(resident&&first.managed&&!first.ok&&first.lastError==5,"managed first MSI failure is resident with detail OFF"))return finish();
#ifdef YMCC_MSI_OLD_PREFIX_ONLY
    return finish();
#endif
    const auto firstRows=readRecoveryRows(root).size();const auto firstWrites=g_nativeLifecycleFrameWrites;const auto firstOpens=msiFixtureOpenCalls;
    ymcc::testing::allocationFault::failAfter(0);bool sameOutcome=true;
    for(unsigned n=0;n<50000;++n){const auto repeated=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);sameOutcome&=!repeated.ok&&repeated.lastError==5;}
    const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    if(!verify(sameOutcome&&arm==0&&g_nativeLifecycleFrameWrites==firstWrites&&msiFixtureOpenCalls==firstOpens&&readRecoveryRows(root).size()==firstRows,
        "50000 managed repeated failures allocate zero and add no Native file writes or output opens"))return finish();
    // Managed failure classification is strict; successful/unmanaged/badly typed receipts remain detailed.
    const auto rowsBeforeClassify=readRecoveryRows(root).size();
    appendNativeLifecycleLog("msi-dinput-feedback",{{"managed",true},{"ok",true}});
    appendNativeLifecycleLog("msi-dinput-feedback",{{"managed",false},{"ok",false}});
    appendNativeLifecycleLog("msi-dinput-feedback",{{"managed",1},{"ok",false}});
    appendNativeLifecycleLog("msi-dinput-feedback",{{"managed",true},{"ok",0}});
    if(!verify(readRecoveryRows(root).size()==rowsBeforeClassify&&!g_nativeDetailLog.is_open(),"successful unmanaged and malformed MSI receipts do not enter resident lane"))return finish();
    ++g_msiFeedbackModeGeneration;msiFixturePreparationFault=true;
    const auto prepareResult=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    auto& evidence=ymcc::logging::nativeLogFailureEvidence();const auto prepare=evidence.snapshot();
    if(!verify(!prepareResult.ok&&prepareResult.lastError==5&&count(Failure::preparationException)==1&&count(Failure::eventOpen)==0&&
        prepare["eventAppendRecovery"]["unrecoveredMsiFeedbackPreparationFailureObserved"]==true&&prepare["eventAppendRecovery"]["unrecoveredFailureObserved"]==false,
        "actual MSI diagnostic allocation failure is separate from output and Native file errors"))return finish();
    const auto prepareEpoch=evidence.recoveryEpoch();const auto prepareWrites=g_nativeLifecycleFrameWrites;
    ymcc::testing::allocationFault::failAfter(0);for(unsigned n=0;n<50000;++n)(void)msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    const auto prepareArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    if(!verify(prepareArm==0&&g_nativeLifecycleFrameWrites==prepareWrites&&evidence.recoveryEpoch()==prepareEpoch&&count(Failure::preparationException)==1,
        "50000 post-preparation-fault repeats stay quiet until an existing resident append succeeds"))return finish();
    appendNativeLifecyclePrepared("detail-off-is-no-recovery",json::object(),false,nullptr);
    if(!verify(evidence.recoveryEpoch()==prepareEpoch&&evidence.msiFeedbackPreparationFailurePending.load(),"suppressed detail is not MSI log recovery"))return finish();
    appendNativeLifecyclePrepared("natural-resident-after-preparation",json::object(),true,nullptr);
    if(!verify(evidence.recoveryEpoch()==prepareEpoch+1&&!evidence.msiFeedbackPreparationFailurePending.load(),"real original resident append clears MSI preparation pending and advances one epoch"))return finish();
    const auto afterPrepareRows=readRecoveryRows(root).size();const auto beforeRearmOpens=msiFixtureOpenCalls;
    const auto rearmed=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    if(!verify(readRecoveryRows(root).size()==afterPrepareRows+1&&!rearmed.ok&&rearmed.lastError==5&&msiFixtureOpenCalls==beforeRearmOpens,
        "same MSI failure reoffers after diagnostic recovery without granting an output retry"))return finish();
    closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();++g_msiFeedbackModeGeneration;
    const auto fileResult=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);const auto fileEpoch=evidence.recoveryEpoch();
    if(!verify(!fileResult.ok&&count(Failure::eventOpen)==1&&evidence.eventFailurePending.load()&&!evidence.msiFeedbackPreparationFailurePending.load(),
        "actual missing-directory Native open failure keeps file and MSI preparation causes distinct"))return finish();
    g_testRoot=root.wstring();appendNativeLifecyclePrepared("natural-resident-after-file-open",json::object(),true,nullptr);
    const auto afterFileRows=readRecoveryRows(root).size();const auto fileOpens=msiFixtureOpenCalls;(void)msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    if(!verify(evidence.recoveryEpoch()==fileEpoch+1&&readRecoveryRows(root).size()==afterFileRows+1&&msiFixtureOpenCalls==fileOpens,
        "same MSI failure reoffers after actual file sink recovers without a new hardware open"))return finish();
    for(const auto mode:{ControlledBuffer::Mode::writeFail,ControlledBuffer::Mode::flushFail}){
        const auto kind=mode==ControlledBuffer::Mode::writeFail?Failure::eventWrite:Failure::eventFlush;
        const auto priorCount=count(kind);++g_msiFeedbackModeGeneration;
        {ControlledBuffer buffer(mode);BufferLease lease(g_nativeLifecycleLog,buffer);(void)msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);}
        if(!verify(count(kind)==priorCount+1&&evidence.eventFailurePending.load(),mode==ControlledBuffer::Mode::writeFail?
            "controlled original Native write refusal is observed":"controlled original Native flush refusal is observed"))return finish();
        const auto priorEpoch=evidence.recoveryEpoch();appendNativeLifecyclePrepared("natural-resident-after-stream",json::object(),true,nullptr);
        const auto priorRows=readRecoveryRows(root).size();(void)msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
        if(!verify(evidence.recoveryEpoch()==priorEpoch+1&&readRecoveryRows(root).size()==priorRows+1,mode==ControlledBuffer::Mode::writeFail?
            "MSI failure reoffers after original Native write path recovers":"MSI failure reoffers after original Native flush path recovers"))return finish();
    }
    msiFixtureOpenSucceeds=true;++g_msiFeedbackModeGeneration;const auto beforeSuccessRows=readRecoveryRows(root).size();
    const auto successful=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    if(!verify(successful.ok&&successful.attempted&&readRecoveryRows(root).size()==beforeSuccessRows,"successful managed touch pulse retains original silent logging path"))return finish();
    msiFixtureWriteSucceeds=false;const auto afterSuccess=msiDinputFeedbackWrite(51,51,false,"touch",nullptr,true);
    if(!verify(!afterSuccess.ok&&afterSuccess.lastError==29&&readRecoveryRows(root).size()==beforeSuccessRows+1,
        "failure after actual lease output success remains visible with original error code"))return finish();
    const auto manualRows=readRecoveryRows(root).size();(void)msiDinputFeedbackWrite(51,51,false,"manual",nullptr,false);
    if(!verify(readRecoveryRows(root).size()==manualRows+1,"explicit manual failed feedback retains its own receipt"))return finish();
    Failure local;local.record(Failure::preparationException,"screen-touchpads-state");local.record(Failure::preparationException,"gyro-power-unsubscribe-failed");local.record(Failure::preparationException,"msi-dinput-feedback");
    const auto separate=local.snapshot()["eventAppendRecovery"];local.observeEventAppendSuccess();const auto merged=local.snapshot()["eventAppendRecovery"];
    if(!verify(separate["unrecoveredPreparationFailureObserved"]==true&&separate["unrecoveredSensorRetirementPreparationFailureObserved"]==true&&
        separate["unrecoveredMsiFeedbackPreparationFailureObserved"]==true&&separate["unrecoveredFailureObserved"]==false&&merged["epoch"]==1&&
        merged["unrecoveredPreparationFailureObserved"]==false&&merged["unrecoveredSensorRetirementPreparationFailureObserved"]==false&&merged["unrecoveredMsiFeedbackPreparationFailureObserved"]==false,
        "three named preparation causes stay independent and share one natural recovery epoch"))return finish();
    ymcc::touchhaptic::FailureLogGate legacy;const ymcc::touchhaptic::SourceToken token{ymcc::touchhaptic::Backend::Msi,1,1,1,4};
    if(!verify(legacy.publish(true,false,token,ymcc::touchhaptic::Reason::OpenFailed,5)&&!legacy.publish(true,false,token,ymcc::touchhaptic::Reason::OpenFailed,5),
        "original five-argument failure gate remains source compatible and deduplicated"))return finish();
    {std::lock_guard<std::mutex> lock(local.mutex);local.eventRecoveryEpoch.store(~0ULL);}
    local.record(Failure::preparationException,"msi-dinput-feedback");local.observeEventAppendSuccess();
    if(!verify(local.snapshot()["eventAppendRecovery"]["epochLimitReached"]==true&&local.recoveryEpoch()==~0ULL,
        "recovery epoch saturation remains explicit without wrapping"))return finish();
    return finish();
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 3;}}
