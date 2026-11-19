struct StartupMachinePort {std::string deviceClass="owned-legion-model";};static StartupMachinePort g_machineIdentity;
static std::atomic<unsigned long long> g_legionMotionCaptureEpoch{77};
static void originalStartupReceipt(unsigned index,bool healthy=false) {
    const struct {unsigned long long found,confirmed,partial;} efficiencyStartup{3,healthy?3ULL:2ULL,healthy?0ULL:1ULL};
    const struct {bool qos,priority;DWORD qosError,priorityError;} initialEfficiency{healthy,true,static_cast<DWORD>(healthy?ERROR_SUCCESS:ERROR_ACCESS_DENIED),ERROR_SUCCESS};
    const int recoveryResult=healthy?0:5;const unsigned long long completedGeneration=2,recoveryPowerGeneration=3,completedDemandGeneration=4,recoveryDemandGeneration=5;const bool recoveryPowerReady=false;
    const bool subscribed=healthy;const DWORD error=healthy?ERROR_SUCCESS:ERROR_ACCESS_DENIED;
    const json result={{"ok",healthy},{"reason",healthy?"":"startup-application-unconfirmed"},{"scope","client-setting"},{"gameOverlayVerified",false},{"targetPath","owned-private-path"},{"targetAccount","owned-private-account"}};
@@PRODUCERS@@
}
static const char* startupEvents[]={"steam-overlay-startup","process-efficiency-startup","input-module-replay-unverified","hwinfo-recovery-result-discarded","controller-motion-raw-registration"};
static json startupExpected(unsigned index,bool healthy=false) {
    if(index==0)return {{"ok",healthy},{"reason",healthy?"":"startup-application-unconfirmed"},{"scope","client-setting"},{"gameOverlayVerified",false}};
    if(index==1)return {{"policy","ecoqos-idle-v1"},{"ownedTasksFound",3},{"ownedTasksConfirmed",healthy?3:2},{"ownedTasksPartial",healthy?0:1},{"selfPriority",GetPriorityClass(GetCurrentProcess())},{"selfEcoQoS",healthy},{"selfPriorityConfirmed",true},{"selfEcoQoSError",healthy?0:5},{"selfPriorityError",0}};
    if(index==2)return {{"newHostCreated",false},{"automaticRetryStopped",true}};
    if(index==3)return {{"code",healthy?0:5},{"completedGeneration",2},{"currentGeneration",3},{"powerReady",false},{"completedDemandGeneration",4},{"demandGeneration",5},{"owner","ymcc-native"}};
    return {{"deviceClass","owned-legion-model"},{"usagePage",0xFFA0},{"usage",1},{"subscribed",healthy},{"error",healthy?0:5},{"captureEpoch",77},{"scope","legion-controller-vendor-motion;separate-from-generic-and-oem-shortcuts"}};
}
static bool startupPayload(const json& row,const json& payload){for(auto it=payload.begin();it!=payload.end();++it)if(!row.contains(it.key())||row[it.key()]!=it.value())return false;return true;}
static unsigned startupDetailRows(){g_nativeDetailLog.flush();std::ifstream in(fspath::path(g_testRoot)/L"native-detail.log");unsigned rows=0;std::string line;while(std::getline(in,line))if(!line.empty())++rows;return rows;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    bool registered=true;for(const auto* event:startupEvents)registered=registered&&nativeLifecycleEventIsBase(event);check(registered,"five-original-finite-startup-and-completion-names-registered");
    for(unsigned i=0;i<5;++i)originalStartupReceipt(i);auto rows=readRecoveryRows(root);bool exact=rows.size()==5;if(exact)for(unsigned i=0;i<5;++i)exact=exact&&rows[i]["event"]==startupEvents[i]&&startupPayload(rows[i],startupExpected(i))&&!rows[i].contains("targetPath")&&!rows[i].contains("targetAccount");
    check(exact&&!g_nativeDetailLog.is_open(),"five-actual-original-producers-off-preserve-fields-and-existing-steam-redaction");
#ifndef YMCC_STARTUP_FINITE_OLD_ONLY
    for(unsigned i=0;i<5;++i)originalStartupReceipt(i);rows=readRecoveryRows(root);bool every=rows.size()==10;if(every)for(unsigned i=0;i<10;++i)every=every&&startupPayload(rows[i],startupExpected(i%5));check(every,"identical-independent-operation-receipts-are-never-deduplicated");
    for(unsigned i=0;i<5;++i)originalStartupReceipt(i,true);rows=readRecoveryRows(root);bool healthy=rows.size()==15;if(healthy)for(unsigned i=0;i<5;++i)healthy=healthy&&startupPayload(rows[10+i],startupExpected(i,true));check(healthy,"original-success-and-discarded-zero-code-are-preserved-without-health-inference");
    g_domainLogEnabled[1]=true;for(unsigned i=0;i<5;++i)originalStartupReceipt(i);g_domainLogEnabled[1]=false;check(readRecoveryRows(root).size()==20&&!g_nativeDetailLog.is_open(),"detail-on-does-not-move-finite-receipts-out-of-resident-lane");
    const auto priorSuffix=readRecoveryRows(root).size();for(const auto* event:startupEvents){const auto future=std::string(event)+"-future";appendNativeLifecycleLog(future.c_str(),{{"ok",false}});}check(readRecoveryRows(root).size()==priorSuffix,"five-unknown-event-suffixes-do-not-gain-resident-permission");
    const json high={{"reason","buttons-change"}};const auto writes=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);check(arm==0&&writes==g_nativeLifecycleFrameWrites,"50000-existing-high-frequency-samples-off-allocation-free-and-silent");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog("system-xinput-readback",high);g_domainLogEnabled[1]=false;check(startupDetailRows()==1&&readRecoveryRows(root).size()==20,"high-frequency-sample-still-enters-enabled-detail");
    ++g_powerGeneration;for(unsigned i=0;i<5;++i)originalStartupReceipt(i);rows=readRecoveryRows(root);bool generation=rows.size()==25;if(generation)for(unsigned i=20;i<25;++i)generation=generation&&rows[i]["powerGeneration"]==2;check(generation,"new-power-generation-keeps-every-original-finite-receipt");
    const auto priorFile=readRecoveryRows(root).size(),failureCount=count(Failure::eventWrite);{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);originalStartupReceipt(2);}originalStartupReceipt(2);rows=readRecoveryRows(root);check(count(Failure::eventWrite)==failureCount+1&&rows.size()==priorFile+1&&startupPayload(rows.back(),startupExpected(2)),"actual-file-failure-counted-and-next-original-operation-retained");
    const auto priorUnknown=rows.size();for(const auto* event:startupEvents)appendNativeLifecycleLog(event,{{"ok",nullptr},{"futureField",json::array({1,true})}});rows=readRecoveryRows(root);bool unknown=rows.size()==priorUnknown+5;if(unknown)for(size_t i=priorUnknown;i<rows.size();++i)unknown=unknown&&rows[i]["ok"].is_null()&&rows[i]["futureField"]==json::array({1,true});check(unknown,"unknown-finite-payload-fields-remain-visible-without-type-coercion");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-returned-before-exit");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"productOrSteamActions",0},{"originalProducerStatements",5},{"originalSteamRedaction",true},{"businessProducerFunctionsExecuted",false}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
