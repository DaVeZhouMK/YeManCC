
static bool g_gyroReadingChangedSubscribed=false,g_accelReadingChangedSubscribed=false;
struct SensorRunPort {bool running()const{return false;}};static SensorRunPort g_msiSensorApiProvider,g_hcWindowsSensorApiProvider;
static unsigned long long g_inputCaptureSensorGeneration=9007199254740993ULL;
static std::atomic<bool> g_inputCaptureSensorsSuspended{false};
struct PowerReceiptPort {bool ready=true;struct Value {unsigned long long generation=7,observedNotificationSequence=9007199254740995ULL,suspendSerial=3;} value;bool current()const{return ready;}const Value& power()const{return value;}};static PowerReceiptPort sourcePower;
static std::string W2U(const std::wstring& value){std::string result;for(const auto c:value)result.push_back(static_cast<char>(c));return result;}
static void originalLifecycleReceipt(unsigned index,bool healthy=false,bool revoked=false){
 g_gyroReadingChangedSubscribed=!healthy;const bool startCompleted=healthy;sourcePower.ready=healthy&&!revoked;const auto power=&sourcePower;g_inputCaptureSensorsSuspended=!sourcePower.ready;
 const struct {bool ok,copied,reused,repaired;std::wstring reason;} result{healthy,healthy,false,!healthy,L"owned-result-reason"};
 const json args={{"enabled",healthy}},recovery={{"status","repaired"},{"source",healthy?"primary":"defaults"},{"archived",healthy?2:0},{"automaticHardwareDeactivated",!healthy}};
@@PRODUCERS@@
}
static const char* lifecycleEvents[]={"gyro-power-early-pause-completed","gyro-power-start-current","fxsound-toggle","settings-upgrade-repaired"};
static bool exactLifecyclePayload(const json& row,unsigned index,bool healthy=false){
 if(row["event"]!=lifecycleEvents[index])return false;
 if(index==0)return row["source"]=="os-suspend-callback"&&row["subscriptionsStopped"]==healthy&&row["generation"]==9007199254740993ULL&&row["physicalSleepVerified"]==false;
 if(index==1)return row["commandCompleted"]==healthy&&row["publicPowerCurrent"]==healthy&&row["producerPowerGeneration"]==7&&row["observedPowerGeneration"]==row["powerGeneration"]&&row["notificationSequence"]==9007199254740995ULL&&row["suspendSerial"]==3&&row["suspendRequested"]==!healthy&&row["physicalSleepVerified"]==false;
 if(index==2)return row["ok"]==healthy&&row["enabled"]==healthy&&row["copied"]==healthy&&row["reused"]==false&&row["repaired"]==!healthy&&row["reason"]=="owned-result-reason";
 return row["status"]=="repaired"&&row["source"]==(healthy?"primary":"defaults")&&row["archived"]==(healthy?2:0)&&row["automaticHardwareDeactivated"]==!healthy;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 bool registered=true;for(const auto* event:lifecycleEvents)registered=registered&&nativeLifecycleEventIsBase(event);check(registered,"four-original-finite-receipt-names-registered");
 for(unsigned i=0;i<4;++i)originalLifecycleReceipt(i);auto rows=readRecoveryRows(root);bool exact=rows.size()==4;if(exact)for(unsigned i=0;i<4;++i)exact=exact&&exactLifecyclePayload(rows[i],i);
 check(exact&&!g_nativeDetailLog.is_open(),"four-original-producer-statements-off-preserve-negative-flags-and-exact-u64");
#ifndef YMCC_LIFECYCLE_OUTCOMES_OLD_ONLY
 for(unsigned i=0;i<4;++i)originalLifecycleReceipt(i);rows=readRecoveryRows(root);check(rows.size()==8,"independent-identical-operation-receipts-remain-distinct");
 for(unsigned i=0;i<4;++i)originalLifecycleReceipt(i,true);rows=readRecoveryRows(root);bool healthy=rows.size()==12;if(healthy)for(unsigned i=0;i<4;++i)healthy=healthy&&exactLifecyclePayload(rows[8+i],i,true);check(healthy,"original-positive-result-flags-preserved-without-hardware-inference");
 originalLifecycleReceipt(1,true,true);rows=readRecoveryRows(root);check(rows.size()==13&&rows.back()["commandCompleted"]==true&&rows.back()["publicPowerCurrent"]==false&&rows.back()["suspendRequested"]==true,"completed-command-and-revoked-public-power-remain-independent");
 g_domainLogEnabled[1]=true;for(unsigned i=0;i<4;++i)originalLifecycleReceipt(i);g_domainLogEnabled[1]=false;check(readRecoveryRows(root).size()==17&&g_nativeDetailWrites==0,"detail-on-keeps-original-operation-receipts-resident");
 for(const auto* event:lifecycleEvents){const std::string future=std::string(event)+"-future";appendNativeLifecycleLog(future.c_str(),{{"ok",false}});}check(readRecoveryRows(root).size()==17,"registry-permission-does-not-cover-unknown-suffixes");
 const json high={{"reason","buttons-change"}};const auto writes=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);check(arm==0&&writes==g_nativeLifecycleFrameWrites,"50000-original-high-frequency-off-samples-allocation-free-and-silent");
 ++g_powerGeneration;for(unsigned i=0;i<4;++i)originalLifecycleReceipt(i);rows=readRecoveryRows(root);bool power=rows.size()==21;if(power)for(size_t i=17;i<21;++i)power=power&&rows[i]["powerGeneration"]==2;check(power,"new-power-generation-keeps-original-receipts");
 const auto failureCountBefore=count(Failure::eventWrite);{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);originalLifecycleReceipt(2);}originalLifecycleReceipt(2);check(count(Failure::eventWrite)==failureCountBefore+1&&readRecoveryRows(root).size()==22,"actual-log-write-failure-is-counted-and-next-operation-retained");
 for(const auto* event:lifecycleEvents)appendNativeLifecycleLog(event,{{"ok",nullptr},{"future",json::array({true,3})}});rows=readRecoveryRows(root);bool unknown=rows.size()==26;if(unknown)for(size_t i=22;i<26;++i)unknown=unknown&&rows[i]["ok"].is_null()&&rows[i]["future"]==json::array({true,3});check(unknown,"unknown-payload-fields-are-preserved-without-health-defaults");
#endif
 closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"actualProducerStatements",4},{"businessProducerFunctionsExecuted",false},{"deviceCalls",0}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& e){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<e.what()<<'\n';return 3;}}
