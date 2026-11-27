#include "msi_key_event_class.h"
#include "msi_dinput_reprobe_log.h"
static constexpr unsigned long long kMsiWmiRetryBackoffMs=60000;
static unsigned fixtureEventRows(const char* event){unsigned n=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))n+=row.value("event",std::string{})==event;return n;}
static ymcc::logging::ControllerConnectionEdge g_gamepadConnectionEdge;
static unsigned long long g_gamepadConnectionEdgeSequence=0,fixtureConnectionTick=100;
static bool g_padConnected=false,g_lastGamepadFallbackConnected=false;
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
#define GetTickCount64() fixtureConnectionTick
// ACTUAL_CONTROLLER_CONNECTION_FUNCTIONS
#undef GetTickCount64
using PowerLifecycle=int;
static unsigned long g_xinputPreTargetSlots=1,g_xinputPhysicalSourceMask=0,g_xinputConnectedMask=0;
static bool g_xinputPhysicalSourceLocked=true;
static const char* inputHostPersona(){return "steamdeck";}
static std::atomic<ULONGLONG> g_wakeHotReplaceRetryAfterTick{0};
static std::atomic<bool> g_wakeRescanRequested{false};
namespace ymcc::inputHost::wakeExecutionRuntime {
// ACTUAL_WAKE_TYPES
static std::mutex mx;
static Work episode;
static const char* retiredLocked(const Work& w){return w.serial==episode.serial?nullptr:"stale";}
static void retireLocked(const Work&,const char*){}
static bool wanted(){return true;}
static bool paused(){return false;}
static void log(const Work&,const char*,const char*,unsigned=0,bool=false){}
static std::string encode(const Work& w){return "dispatch-for-host-"+std::to_string(w.epoch);}
#define GetTickCount64() fixtureConnectionTick
// ACTUAL_WAKE_QUEUE
#undef GetTickCount64
}
// ACTUAL_WAKE_LOG

static void checkWakeRescan(const fspath::path& root){
    namespace runtime=ymcc::inputHost::wakeExecutionRuntime;
    closeStreams();g_testRoot=(root/L"wake-rescan").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    runtime::Work work;work.serial=7;work.generation=g_powerGeneration;work.requestId=1;work.epoch=12;
    runtime::episode=work;
    bool coalesced=false,accepted=true;
    const auto submit=[&](const char* reason,bool* result){
        *result=coalesced;return accepted&&std::string_view(reason).starts_with("dispatch-for-host-");
    };
    runtime::QueueReceipt receipt;
    const auto event="input-host-wake-rescan";
    check(runtime::queue(work,12,receipt,submit)&&receipt.afterPhysicalReturnWaits==0,"first dispatch has no physical-return provenance");
    logInputHostWakeRescanQueued(work,12,true,false,receipt);
    runtime::retry(work,1000,"wait",true);
    check(runtime::queue(work,12,receipt,submit)&&receipt.afterPhysicalReturnWaits==1,"the next accepted dispatch consumes exact same-host wait provenance");
    ymcc::testing::allocationFault::failAfter(0);logInputHostWakeRescanQueued(work,12,true,false,receipt);
    const bool noJson=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    check(noJson&&fixtureEventRows(event)==1&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),
          "unchanged wait dispatch avoids JSON and does not enter a file with detail off");
    runtime::retry(work,1000,"wait",true);g_domainLogEnabled[1]=true;
    check(runtime::queue(work,12,receipt,submit)&&receipt.afterPhysicalReturnWaits==2,"each executed wait reports its true current ordinal");
    logInputHostWakeRescanQueued(work,12,true,false,receipt);
    g_nativeDetailLog.flush();std::ifstream stream(fspath::path(g_testRoot)/L"native-detail.log");
    std::string line;const auto sample=std::getline(stream,line)?json::parse(line):json::object();
    check(sample.value("queueOutcome",std::string{})=="enqueued"&&sample.value("repeatPhysicalReturnDispatch",false)&&
          sample.value("afterPhysicalReturnWaits",0)==2&&!std::getline(stream,line)&&fixtureEventRows(event)==1,
          "only the detail file receives the marked wait dispatch with actual queue result");
    stream.close();g_domainLogEnabled[1]=false;
    runtime::retry(work,1000,"wait",true);coalesced=true;
    check(runtime::queue(work,12,receipt,submit)&&receipt.coalesced,"queue port returns a coalesced action distinctly");
    logInputHostWakeRescanQueued(work,12,true,false,receipt);
    ++g_powerLifecycle;logInputHostWakeRescanQueued(work,12,true,false,receipt);
    g_xinputConnectedMask=1;logInputHostWakeRescanQueued(work,12,true,true,receipt);
    const auto rows=readRecoveryRows(g_testRoot);
    check(rows.size()==4&&rows[1]["queueOutcome"]=="coalesced"&&rows.back()["handshake"]==true,
          "coalescing transition, power transition, and handshake remain resident");
    runtime::retry(work,1000,"wait",true);
    check(runtime::queue(work,13,receipt,submit)&&receipt.afterPhysicalReturnWaits==0,"old Host wait does not transfer to a new epoch");
    runtime::retry(work,1000,"wait",true);accepted=false;
    const auto refused=!runtime::queue(work,12,receipt,submit);
    accepted=true;
    check(refused&&runtime::queue(work,12,receipt,submit)&&receipt.afterPhysicalReturnWaits==0,
          "queue refusal takes its own backoff; retry is no longer classified as a physical-return sample");
    closeStreams();g_testRoot=(root/L"wake-rescan-reopen").wstring();g_xinputConnectedMask=0;
    logInputHostWakeRescanQueued(work,12,true,false,receipt);
    check(ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"first resident dispatch fails with missing evidence directory");
    fspath::create_directories(g_testRoot);
    receipt.afterPhysicalReturnWaits=4;
    logInputHostWakeRescanQueued(work,12,true,false,receipt);
    check(fixtureEventRows(event)==1,"pending write failure prompts low-frequency reoffer of the current observation");
}

// ACTUAL_DINPUT_REPROBE_LOG
static void checkDinputReprobe(const fspath::path& root){
    closeStreams();g_testRoot=(root/L"dinput-reprobe").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    ymcc::msi::DinputReprobeLogState memo;
    ymcc::msi::DinputReprobeLogState::Context context{17,3,0,0,6,2};
    const ymcc::msi::DinputReprobeObservation miss{true,true,0,0,3,0,0,0};
    constexpr auto event="msi-dinput-transition";
    msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<1000;++i)msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    const bool noJson=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    check(noJson&&fixtureEventRows(event)==1&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),"identical known DInput misses avoid JSON with detail off");
    g_domainLogEnabled[1]=true;msiDinputLogExhaustedReprobe(memo,context,miss,false,false);g_nativeDetailLog.flush();
    std::ifstream stream(fspath::path(g_testRoot)/L"native-detail.log");std::string line;
    const auto sample=std::getline(stream,line)?json::parse(line):json::object();
    check(sample.value("repeatReprobeSample",false)&&!std::getline(stream,line)&&fixtureEventRows(event)==1,"known repeated reprobe is detail only");
    stream.close();g_domainLogEnabled[1]=false;
    auto changed=miss;++changed.vendorCandidates;msiDinputLogExhaustedReprobe(memo,context,changed,false,false);
    auto unknown=miss;unknown.completeNoMatch=false;unknown.enumerated=false;
    msiDinputLogExhaustedReprobe(memo,context,unknown,false,false);
    msiDinputLogExhaustedReprobe(memo,context,unknown,false,false);
    msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    msiDinputLogExhaustedReprobe(memo,context,miss,true,false);
    msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    msiDinputLogExhaustedReprobe(memo,context,miss,false,true);
    ++context.episode;msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    const auto rows=readRecoveryRows(g_testRoot);
    check(rows.size()==9&&rows[2]["result"]=="unresolved"&&rows[2]["enumerationHresult"].is_null()&&
          rows[5]["result"]=="bound"&&rows[7]["probePowerChanged"]==true,
          "candidate changes, unknown failures, bound, power changes and a new episode stay resident");
    closeStreams();g_testRoot=(root/L"dinput-reprobe-reopen").wstring();
    appendNativeLifecycleLog("log-export-result",{{"fixture","forced-open-failure"}});
    fspath::create_directories(g_testRoot);
    msiDinputLogExhaustedReprobe(memo,context,miss,false,false);
    check(fixtureEventRows(event)==1,"pending native sink failure preserves identical current reprobe on recovery");
}

static void checkHidHideNoJournal(const fspath::path& root){
    closeStreams();g_testRoot=(root/L"hidhide-disconnect").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    ymcc::logging::HidHideDisconnectLogMemo memo;
    constexpr auto event="hidhide.unplugged-unhide";
    const auto power=currentPowerGeneration();
    const auto emitEdge=[] { ++fixtureConnectionTick;gamepadLogControllerConnectionEdge(false,true,{{"reason","xinput-disconnect"}}); };
    const auto offer=[&](bool edge=true,bool active=false,bool base=false,bool instance=false,unsigned host=0){
        logHidHideDisconnectWithoutWrite(memo,g_gamepadConnectionEdge,edge,active,base,instance,1,power,"fixture",host);
    };
    emitEdge();offer();const auto first=g_gamepadConnectionEdge.sequence;
    emitEdge();ymcc::testing::allocationFault::failAfter(0);offer();
    const bool noJson=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    const auto rows=readRecoveryRows(g_testRoot);
    check(noJson&&rows.size()==3&&rows[0]["connectionEdgeSequence"]==first&&rows[1]["connectionEdgeSequence"]==first&&
          rows[1]["unhideAttempted"]==false&&rows[1]["unhideOutcome"]=="not-attempted"&&rows[1]["repeatNoJournalObservation"]==false,
          "initial no-journal result remains resident; the next confirmed disconnect remains resident without its repeat JSON");
    g_domainLogEnabled[1]=true;emitEdge();offer();g_nativeDetailLog.flush();
    std::ifstream stream(fspath::path(g_testRoot)/L"native-detail.log");std::string line;
    const auto sample=std::getline(stream,line)?json::parse(line):json::object();
    check(sample.value("repeatNoJournalObservation",false)&&sample["connectionEdgeSequence"]==g_gamepadConnectionEdge.sequence&&
          fixtureEventRows(event)==1&&!std::getline(stream,line),"detail repeat references the exact current disconnect including sequence");
    stream.close();g_domainLogEnabled[1]=false;
    emitEdge();offer(true,true,false,true);
    emitEdge();offer(true,false,true,false);
    emitEdge();offer();
    emitEdge();offer(true,false,false,false,12);
    emitEdge();offer(false,false,false,false,12);
    std::vector<json> after;
    for(const auto& row:readRecoveryRows(g_testRoot))if(row.value("event",std::string{})==event)after.push_back(row);
    check(after.size()==6&&after.back()["connectionEdgeSequence"].is_null()&&
          after[1]["state"]=="active-journal-missing-base-identity"&&after[2]["state"]=="inactive-journal-retains-identity"&&
          after[3]["state"]=="no-journaled-physical"&&after[4]["hostEpoch"]==12,
          "active incomplete and inactive stale journals, return to quiet, new Host and unbound edge all remain resident");
    emitEdge();offer();
    closeStreams();g_testRoot=(root/L"hidhide-reopen").wstring();
    emitEdge();const auto failed=!g_gamepadConnectionEdge.written;
    fspath::create_directories(g_testRoot);offer();
    const auto recovered=readRecoveryRows(g_testRoot);
    check(failed&&recovered.size()==1&&recovered[0]["event"]==event&&recovered[0]["connectionEdgeSequence"].is_null(),
          "a failed actual disconnect write keeps separate no-journal evidence and does not claim a binding");
}

enum class RogRemapDirty { None, Noise, Relevant };
static std::atomic<bool> g_rogRemapDevTreeDirty{false},g_rogRemapLogQuiet{false};
static std::atomic<unsigned long long> g_rogRemapBoundSig{0};
// ACTUAL_ROG_DEVICE_TREE

static void checkRogDeviceTreeNoise(const fspath::path& root){
    closeStreams();g_testRoot=(root/L"rog-noise").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    constexpr auto event="rog.controller-remap-noise-ignored";
    RogRemapThrottle throttle;
    check(rogRemapResolveDirty(throttle)==RogRemapDirty::None&&!throttle.boundKnown,"no dirty notification does not create a signature baseline");
    g_rogRemapDevTreeDirty=true;
    const bool first=!throttle.boundKnown&&rogRemapResolveDirty(throttle)==RogRemapDirty::Noise;
    rogRemapLogDeviceTreeNoise(throttle.boundSig,false,1,5,"ready",false);
    const auto rows=readRecoveryRows(g_testRoot);
    check(first&&rows.size()==1&&rows[0]["signatureObservation"]=="baseline-established"&&rows[0]["cachedIdentityPresent"]==false,
          "initial zero-signature baseline is resident without implying a bound device");
    ymcc::testing::allocationFault::failAfter(0);
    bool stayedNoise=true;
    for(unsigned i=0;i<1000;++i){
        g_rogRemapDevTreeDirty=true;
        stayedNoise&=rogRemapResolveDirty(throttle)==RogRemapDirty::Noise;
        rogRemapLogDeviceTreeNoise(throttle.boundSig,true,1,5,"ready",false);
    }
    const bool noJson=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    check(stayedNoise&&noJson&&fixtureEventRows(event)==1&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),
          "1000 same cached-signature observations with detail off avoid JSON");
    g_domainLogEnabled[1]=true;
    rogRemapLogDeviceTreeNoise(0,true,1,5,"ready",false);g_nativeDetailLog.flush();
    std::ifstream stream(fspath::path(g_testRoot)/L"native-detail.log");std::string line;
    const auto detail=std::getline(stream,line)?json::parse(line):json::object();
    check(detail.value("repeatDeviceTreeNoise",false)&&!std::getline(stream,line)&&fixtureEventRows(event)==1,
          "actual writer routes only the marked repeat to detail");
    stream.close();g_domainLogEnabled[1]=false;
    g_rogRemapBoundSig=1ull<<31;g_rogRemapDevTreeDirty=true;
    check(rogRemapResolveDirty(throttle)==RogRemapDirty::Relevant,"changed signature retains the original relevant classification");
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,5,"ready",false);
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"ready",false);
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"suspending",false);
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"suspending",true);
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"suspending",true);
    check(fixtureEventRows(event)==6,"new device or power context and all terminal observations stay resident");
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"suspending",false);
    closeStreams();g_testRoot=(root/L"rog-noise-reopen").wstring();
    appendNativeLifecycleLog("log-export-result",{{"fixture","forced-open-failure"}});fspath::create_directories(g_testRoot);
    rogRemapLogDeviceTreeNoise(1ull<<31,true,2,6,"suspending",false);
    check(fixtureEventRows(event)==1,"native writer failure forces an otherwise identical observation into low frequency");
}

static std::atomic<unsigned> g_xinputTopologyEpoch{1};
// ACTUAL_HIDHIDE_VISIBILITY_LOG

static void checkHidHideVisibilitySnapshot(const fspath::path& root){
    closeStreams();g_testRoot=(root/L"hidhide-restore-snapshot").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    constexpr auto event="hidhide.visibility-snapshot";
    json row={{"phase","restore"},{"readback",false},{"padPnpStatus",17},
        {"pnpDeviceStackEvidence",{{"contextStableAcrossRead",true},{"powerAfter",g_powerGeneration.load()},
            {"topologyAfter",g_xinputTopologyEpoch.load()}}}};
    auto& state=ymcc::logging::hidHideRestoreSnapshotLogState();
    const auto select=[&]{return state.select(row,"same-txn",g_powerGeneration.load(),g_xinputTopologyEpoch.load(),true,
        ymcc::logging::nativeLogFailureEvidence());};
    const auto first=select(),queued=select();
    check(first.sequence&&first.sequence==queued.sequence&&!first.repeat&&!queued.repeat,
          "matching offers remain resident before the deferred writer confirms any append");
    logHidHideVisibilitySnapshot(row,"same-txn",true);
    for(int i=0;i<1000;++i)logHidHideVisibilitySnapshot(row,"same-txn",true);
    check(fixtureEventRows(event)==1&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),
          "writer-confirmed repeats are suppressed when detail is off");
    g_domainLogEnabled[1]=true;logHidHideVisibilitySnapshot(row,"same-txn",true);g_nativeDetailLog.flush();
    std::ifstream detail(fspath::path(g_testRoot)/L"native-detail.log");std::string line;
    auto sample=std::getline(detail,line)?json::parse(line):json::object();
    check(sample.value("repeatRestoreSnapshot",false)&&sample.value("restoreSnapshotSequence",0ull)==first.sequence&&
        !std::getline(detail,line)&&fixtureEventRows(event)==1,"actual detail row cites its resident snapshot sequence");
    detail.close();g_domainLogEnabled[1]=false;
    row["padPnpStatus"]=18;logHidHideVisibilitySnapshot(row,"same-txn",true);
    ++g_xinputTopologyEpoch;row["pnpDeviceStackEvidence"]["topologyAfter"]=g_xinputTopologyEpoch.load();
    logHidHideVisibilitySnapshot(row,"same-txn",true);
    ++g_powerGeneration;row["pnpDeviceStackEvidence"]["powerAfter"]=g_powerGeneration.load();
    logHidHideVisibilitySnapshot(row,"same-txn",true);
    logHidHideVisibilitySnapshot(row,"new-txn",true);
    state.acknowledge(first.sequence);logHidHideVisibilitySnapshot(row,"new-txn",true);
    check(fixtureEventRows(event)==5,"PnP, topology, power and transaction changes stay resident; old acknowledgements do not rearm");
    row["readback"]=true;logHidHideVisibilitySnapshot(row,"new-txn",true);
    row["readback"]=false;logHidHideVisibilitySnapshot(row,"new-txn",true);
    row["pnpDeviceStackEvidence"]["contextStableAcrossRead"]=false;
    logHidHideVisibilitySnapshot(row,"new-txn",true);logHidHideVisibilitySnapshot(row,"new-txn",true);
    check(fixtureEventRows(event)==9,"success, first failure after success and unstable observations stay resident");
    row["pnpDeviceStackEvidence"]["contextStableAcrossRead"]=true;
    logHidHideVisibilitySnapshot(row,"new-txn",true);
    closeStreams();g_testRoot=(root/L"hidhide-restore-snapshot-reopen").wstring();
    appendNativeLifecycleLog("log-export-result",{{"fixture","forced-open-failure"}});
    fspath::create_directories(g_testRoot);logHidHideVisibilitySnapshot(row,"new-txn",true);
    check(fixtureEventRows(event)==1,"native open failure reoffers the known visibility snapshot");
}

int wmain(int argc,wchar_t** argv){
    const bool controllerOnly=argc==3&&std::wstring_view(argv[2])==L"--controller-fallback-edges";
    if(argc!=2&&!controllerOnly)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    (void)ymcc::sleepfactlog::writerIdentity();
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto finish=[&]{closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
        json receipt={{"selectedCase","native-sink-snapshots"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",after},{"hardwareActions",0}};
        if(controllerOnly)receipt["selectedPhase"]="controller-fallback-edges";
        std::cout<<receipt.dump(2)<<'\n';return failures?2:0;};
    const auto controllerFallbackLanes=[&]{
        g_testRoot=(root/L"controller-fallback-lanes").wstring();fspath::create_directories(g_testRoot);
        g_domainLogEnabled[1]=false;
        gamepadLogControllerConnectionEdge(true,true,{{"admittedMask",1}});g_padConnected=true;++fixtureConnectionTick;
        ymcc::testing::allocationFault::failAfter(0);gamepadLogFallbackConnectionLocked(23093);
        const auto noJson=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
        auto rows=readRecoveryRows(g_testRoot);
        check(g_gamepadConnectionEdge.written&&noJson&&rows.size()==1&&rows[0]["connectionEdgeSequence"]==1&&
            rows[0]["event"]=="gamepad-controller-wake"&&fixtureEventRows("gamepad-fallback-tick")==0,
            "durable same-edge witness stays resident and detail-off timer skips JSON");
        g_domainLogEnabled[1]=true;
        gamepadLogControllerConnectionEdge(false,true,{{"reason","xinput-disconnect"}});g_padConnected=false;fixtureConnectionTick+=50;
        gamepadLogFallbackConnectionLocked(23093);gamepadLogFallbackConnectionLocked(23093);g_nativeDetailLog.flush();
        std::ifstream timerDetail(fspath::path(g_testRoot)/L"native-detail.log");json timerRow;std::string timerLine;
        if(std::getline(timerDetail,timerLine))timerRow=json::parse(timerLine);
        check(timerRow.is_object()&&timerRow["duplicateConnectionEdge"]==true&&timerRow["connectionEdgeSequence"]==2&&
            timerRow["connectionEdgeAgeMs"]==50&&timerRow["connected"]==false&&!std::getline(timerDetail,timerLine)&&
            fixtureEventRows("gamepad-controller-sleep")==1,"timer detail pairs to the exact resident disconnect once");
        timerDetail.close();
        g_domainLogEnabled[1]=false;
        const auto oldPower=currentPowerGeneration();
        gamepadLogControllerConnectionEdge(true,true,{{"admittedMask",1}});g_padConnected=true;fixtureConnectionTick+=151;
        gamepadLogFallbackConnectionLocked(23093);
        gamepadLogControllerConnectionEdge(false,true,{{"reason","xinput-disconnect"}});g_padConnected=false;++g_powerGeneration;
        gamepadLogFallbackConnectionLocked(23093);
        g_gamepadConnectionEdge={};g_padConnected=true;gamepadLogFallbackConnectionLocked(23093);
        rows=readRecoveryRows(g_testRoot);
        const auto size=rows.size();
        const auto containsFields=[](const json& row,const json& fields){
            if(!row.is_object())return false;
            for(auto i=fields.begin();i!=fields.end();++i){
                const auto found=row.find(i.key());
                if(found==row.end()||*found!=i.value())return false;
            }
            return true;
        };
        check(size==7&&rows[size-4]["connectionObservation"]=="edge-outside-recent-window"&&
            rows[size-2]["connectionObservation"]=="edge-context-changed"&&rows.back()["connectionObservation"]=="no-matching-edge",
            "late, retired and missing edges keep independent resident timer evidence");
        if(controllerOnly){
            const json missing=nullptr;
            const auto late=size==7?rows[size-4].value("connectionEdgeReference",missing):missing;
            const auto retired=size==7?rows[size-2].value("connectionEdgeReference",missing):missing;
            check(containsFields(late,{{"schema",1},{"sequence",3},{"ageMs",151},{"appendConfirmed",true},
                {"powerGeneration",oldPower},{"observedPowerGeneration",oldPower}})&&late.contains("powerLifecycle")&&
                late.contains("observedPowerLifecycle")&&late.at("powerLifecycle").is_string()&&late.at("powerLifecycle")==late.at("observedPowerLifecycle"),
                "late timer attaches exact historical edge without marking it fresh");
            check(containsFields(retired,{{"sequence",4},{"appendConfirmed",true},{"powerGeneration",oldPower},
                {"observedPowerGeneration",currentPowerGeneration()}})&&!rows.back().contains("connectionEdgeReference"),
                "retired timer preserves both power generations and missing edge has no reference");
        }
        {ControlledBuffer buffer(ControlledBuffer::Mode::flushFail);BufferLease lease(g_nativeLifecycleLog,buffer);
            gamepadLogControllerConnectionEdge(false,true,{{"reason","xinput-disconnect"}});}
        g_nativeLifecycleLog.clear();g_padConnected=false;gamepadLogFallbackConnectionLocked(23093);
        rows=readRecoveryRows(g_testRoot);
        check(!g_gamepadConnectionEdge.written&&rows.size()==size+1&&rows.back()["connectionObservation"]=="edge-write-unconfirmed"&&
            ymcc::logging::ControllerConnectionAppendScope::pending==nullptr,"actual failed flush cannot remove the timer's fallback evidence");
        if(controllerOnly){
            const auto unconfirmed=rows.empty()?json{}:rows.back().value("connectionEdgeReference",json{});
            check(containsFields(unconfirmed,{{"sequence",g_gamepadConnectionEdge.sequence},{"appendConfirmed",false}}),
                "timer reference cannot turn an unconfirmed append into confirmed evidence");
        }
    };
    if(controllerOnly){controllerFallbackLanes();return finish();}
    g_testRoot=(root/L"initial-missing").wstring();g_domainLogEnabled[1]=false;++g_powerGeneration;
    const json feedback={{"ok",true},{"fixture",1}};const auto initialEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog("gamepad.feedbackCaps",feedback);fspath::create_directories(g_testRoot);
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-append-recovery-only"}});
    appendNativeLifecycleLog("gamepad.feedbackCaps",feedback);
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>initialEpoch&&fixtureEventRows("gamepad.feedbackCaps")==1,"same feedback snapshot is resident after actual failed-open and original successful append");
#ifdef YMCC_SNAPSHOT_REARM_OLD_ONLY
    return finish();
#else
    struct Sample{const char* event;json value;};
    std::vector<Sample> samples={
        {"rog.hid-bound",{{"fixture",true}}},{"rog.hid-discovery-summary",{{"fixture",true}}},
        {"gamepad.feedbackCaps",feedback},{"desktop-hud-event",{{"hudEvent","desktopHud.ready"},{"fixture",true}}},
        {"oem-vendor-stack",{{"state","wake-guard-pass"},{"ok",true}}},
        {"gamepad-openxinput-slot-guard",{{"slot",0}}},{"gamepad-openxinput-assign-slot",{{"slot",0}}},
        {"oem-key-usage-query",{{"ok",true}}},{"input-host-player-start-work",{{"action","consuming"}}},
        {"input-host-switch-audit",{{"beat","throttled"},{"request",1}}},
        {"input-host-switch-audit",{{"beat","enqueue"},{"request",1}}},
        {"input-host-switch-audit",{{"beat","dequeue"},{"request",1},{"waitedMs",1},{"deferCount",1}}},
        {"input-host-switch-settle",{{"deferMs",1},{"settleMs",2}}},
        {"late-hide-enqueue",{{"outcome","coalesced"},{"coalesced",true}}},
        {"screen-touchpads-state",{{"callSite","screenTouchpads.get"},{"requestedPersona","steamdeck"},{"state",{{"enabled",true}}}}}};
    bool firstWitnesses=true,repeatedSuppressed=true,failuresDoNotRearm=true,allRearmed=true,onlyOnce=true;
    unsigned index=0;
    for(const auto& sample:samples){
        closeStreams();g_testRoot=(root/(L"case-"+std::to_wstring(index++))).wstring();fspath::create_directories(g_testRoot);++g_powerGeneration;
        firstWitnesses&=nativeLifecycleEventIsBase(sample.event)&&!nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        repeatedSuppressed&=nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
        // The original stream opening failure is genuine; only its path is a private port.
        g_testRoot+=(L"\\not-yet-created");appendNativeLifecycleLog("log-export-result",{{"fixture","forced-open-failure"}});
        failuresDoNotRearm&=nativeLifecycleRecordIsStreaming(sample.event,sample.value)&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch;
        fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","recovered-file"}});
        appendNativeLifecycleLog(sample.event,sample.value);allRearmed&=fixtureEventRows(sample.event)==1;
        appendNativeLifecycleLog(sample.event,sample.value);onlyOnce&=fixtureEventRows(sample.event)==1;
    }
    check(firstWitnesses&&samples.size()==15,"all15 bounded snapshot slots retain their original first resident selection");
    check(repeatedSuppressed,"all15 unchanged snapshot slots keep their existing detailed-or-suppressed route");
    check(failuresDoNotRearm,"an unresolved file failure cannot rearm snapshots before original successful append");
    check(allRearmed,"original successful append rearms all15 snapshot slots exactly within their existing classifier");
    check(onlyOnce,"each same snapshot stays suppressed again after its one recovery witness");
    const auto& stable=samples[2];bool unchanged=true;(void)nativeLifecycleRecordIsStreaming(stable.event,stable.value);
    ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<50000;++i)unchanged&=nativeLifecycleRecordIsStreaming(stable.event,stable.value);
    const bool unconsumed=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    check(unchanged&&unconsumed,"50000 stable classifier calls perform no C++ allocation after recovery");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(stable.event,stable.value);g_nativeDetailLog.flush();
    std::ifstream detailed(fspath::path(g_testRoot)/L"native-detail.log");std::string detailLine;std::getline(detailed,detailLine);detailed.close();
    check(!detailLine.empty()&&json::parse(detailLine)["event"]==stable.event,"detail ON still retains repeated snapshot in the detailed sink");
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail");
    appendNativeLifecycleLog("log-export-result",{{"fixture","detail-only-fault"}});
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&nativeLifecycleRecordIsStreaming(stable.event,stable.value),"detail-only failure does not grant another resident snapshot");
    ++g_powerGeneration;check(!nativeLifecycleRecordIsStreaming(stable.event,stable.value),"original new-power first witness remains resident");
    const json write={{"callSite","screenTouchpads.set"},{"requestedPersona","steamdeck"},{"state",{{"enabled",true}}}};
    check(!nativeLifecycleRecordIsStreaming("screen-touchpads-state",write)&&!nativeLifecycleRecordIsStreaming("screen-touchpads-state",write),"explicit settings-write receipts still remain resident on every call");
    closeStreams();g_testRoot=(root/L"oem-guard-lanes").wstring();fspath::create_directories(g_testRoot);
    g_domainLogEnabled[1]=false;
    const json oemRepeat={{"state","task-absent"},{"task","MSI_Center_M_Server"},{"wakeGuardQuietRepeat",true}};
    appendNativeLifecycleLog("oem-vendor-stack",oemRepeat);
    check(!fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log")&&
          !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),"marked OEM repeat opens neither native stream with detail off");
    g_domainLogEnabled[1]=true;
    for(const auto* state:{"task-absent","process-stop-observation","applied"}){
        json row=oemRepeat;row["state"]=state;appendNativeLifecycleLog("oem-vendor-stack",row);
    }
    auto failure=oemRepeat;failure["state"]="task-unavailable";appendNativeLifecycleLog("oem-vendor-stack",failure);
    auto legacy=oemRepeat;legacy.erase("wakeGuardQuietRepeat");appendNativeLifecycleLog("oem-vendor-stack",legacy);
    auto invalid=oemRepeat;invalid["wakeGuardQuietRepeat"]=1;appendNativeLifecycleLog("oem-vendor-stack",invalid);
    closeStreams();
    std::ifstream oemDetails(fspath::path(g_testRoot)/L"native-detail.log");unsigned detailCount=0;std::string oemLine;
    while(std::getline(oemDetails,oemLine))if(json::parse(oemLine).value("wakeGuardQuietRepeat",false))++detailCount;
    oemDetails.close();
    check(detailCount==3&&fixtureEventRows("oem-vendor-stack")==3,"actual native files separate three explicit quiet receipts from failure, legacy and invalid marker");
    controllerFallbackLanes();
    closeStreams();g_testRoot=(root/L"msi-wmi-retry-lanes").wstring();fspath::create_directories(g_testRoot);
    appendNativeLifecycleLog("log-export-result",{{"fixture","recover-controller-test-stream"}});
    using WmiState=ymcc::msi::EventClassState;
    constexpr char wmiEvent[]="msi-wmi-source-capability";
    const ymcc::msi::EventClassProbe absent{WmiState::Absent,0,-2147217406,true,false};
    auto source=1ull;auto power=currentPowerGeneration();auto nextWmi=[&](const auto& probe){
        const auto failedAt=fixtureConnectionTick+1;fixtureConnectionTick=failedAt+60001;
        msiWmiLogSourceCapability(probe,++source,power,failedAt);
    };
    msiWmiLogSourceCapability(absent,source,power,0);
    for(unsigned i=0;i<9;++i)nextWmi(absent);
    check(fixtureEventRows(wmiEvent)==1&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),
        "first unavailable MSI WMI result stays resident; nine stable retries are quiet with detail off");
    nextWmi(absent);auto rows=readRecoveryRows(g_testRoot);
    check(fixtureEventRows(wmiEvent)==2&&rows.back()["identicalAbsentRetriesSinceResident"]==10&&
        rows.back()["sourceGeneration"]==11&&!rows.back().contains("repeatRetrySample"),
        "tenth unchanged retry writes a resident summary without detail marker");
    g_domainLogEnabled[1]=true;nextWmi(absent);g_nativeDetailLog.flush();
    std::ifstream wmiDetails(fspath::path(g_testRoot)/L"native-detail.log");std::string wmiLine;json wmiDetail;
    if(std::getline(wmiDetails,wmiLine))wmiDetail=json::parse(wmiLine);
    check(fixtureEventRows(wmiEvent)==2&&wmiDetail.is_object()&&wmiDetail["repeatRetrySample"]==true&&
        wmiDetail["sourceGeneration"]==12&&!std::getline(wmiDetails,wmiLine),"stable WMI retry goes to actual detail stream when enabled");
    wmiDetails.close();g_domainLogEnabled[1]=false;
    nextWmi(ymcc::msi::EventClassProbe{WmiState::Unknown,0,262148,true,false});nextWmi(absent);
    nextWmi(ymcc::msi::EventClassProbe{WmiState::Present,0,0,true,true});nextWmi(absent);
    auto changed=absent;changed.waitStatus=-2147217392;nextWmi(changed);
    ++g_powerGeneration;power=currentPowerGeneration();nextWmi(changed);
    ++source;nextWmi(changed);
    fixtureConnectionTick+=60001;msiWmiLogSourceCapability(changed,++source,power,0);
    const auto delayed=fixtureConnectionTick+1;fixtureConnectionTick=delayed+120001;
    msiWmiLogSourceCapability(changed,++source,power,delayed);
    check(fixtureEventRows(wmiEvent)==11,"unknown, present, first absent, changed HRESULT, new power, generation gap and unbound or long retry stay resident");
    {ControlledBuffer buffer(ControlledBuffer::Mode::flushFail);BufferLease lease(g_nativeLifecycleLog,buffer);
        appendNativeLifecycleLog("log-export-result",{{"fixture","flush-failure-rearms-WMI"}});}
    g_nativeLifecycleLog.clear();nextWmi(changed);
    check(fixtureEventRows(wmiEvent)==12,"failed resident flush rearms the next WMI record before any successful append");
    const auto readiness=fixtureConnectionTick+1;fixtureConnectionTick=readiness+60001;
    msiWmiLogSourceCapability(changed,++source,0,readiness);nextWmi(changed);
    check(fixtureEventRows(wmiEvent)==14,"non-ready power clears the WMI comparison state");
    ymcc::logging::BusRateLogState busRate;
    busRate.beginWindow(8330);
    const auto enter=busRate.closeWindow(1000,8330,2);
    const auto suspend=busRate.closeWindow(21640747,0,0);
    const auto waking=busRate.closeWindow(1000,8332,4);
    const auto newAbnormal=busRate.closeWindow(1000,8332,4);
    const auto gap=busRate.closeWindow(742663,8332,0);
    const auto firstNormal=busRate.closeWindow(1000,8332,0);
    const auto secondEnter=busRate.closeWindow(1000,8332,4);
    const auto longGap=busRate.closeWindow(11989641,8332,0);
    check(enter.transition&&suspend.firstInterrupted&&!suspend.transition&&suspend.startReadyPower==8330&&
        !waking.firstInterrupted&&!waking.transition&&newAbnormal.transition,
        "suspend-long and adjacent wake windows interrupt once, then live abnormal flags start anew");
    check(gap.firstInterrupted&&!gap.transition&&!firstNormal.transition&&secondEnter.transition&&longGap.firstInterrupted&&
        !longGap.transition&&std::string_view(gap.interruption)=="window-too-long",
        "recorded ready long windows cannot emit recovery and first normal only establishes a baseline");
    const auto newGeneration=busRate.closeWindow(1000,8333,2);
    const auto nextAbnormal=busRate.closeWindow(1000,8333,2);
    const auto realRecovery=busRate.closeWindow(2000,8333,0);
    check(!newGeneration.transition&&newGeneration.interruption&&nextAbnormal.transition&&realRecovery.transition&&
        !realRecovery.interruption,"context changes break comparability; continuous ready abnormal and normal still report recovery");
    closeStreams();g_testRoot=(root/L"bus-window-lanes").wstring();fspath::create_directories(g_testRoot);
    appendNativeLifecycleLog("input-capture-bus-rate-boundary",{{"windowMs",742663},{"windowInterruption","window-too-long"}});
    appendNativeLifecycleLog("input-capture-bus-rate-sample",{{"windowMs",742663},{"windowInterruption","window-too-long"}});
    check(fixtureEventRows("input-capture-bus-rate-boundary")==1&&fixtureEventRows("input-capture-bus-rate-sample")==0&&
        !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"),"invalid-window first boundary is resident; detailed sample stays gated");
    const auto evidence=exportEvidence()["nativeLogFailureEvidence"]["stats"]["eventAppendRecovery"];
    check(evidence["schemaVersion"]==7&&evidence["oldLostRowRestored"]==false&&evidence["currentFileHealthProven"]==false,"export describes schema7 rearm scope without replay or file-health proof");
    checkWakeRescan(root);
    checkDinputReprobe(root);
    checkHidHideNoJournal(root);
    checkRogDeviceTreeNoise(root);
    checkHidHideVisibilitySnapshot(root);
    return finish();
#endif
}
