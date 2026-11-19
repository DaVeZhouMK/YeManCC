static json exportPreparationIpcEvidence(){json availability;@@ACTUAL_IPC_EXPORT@@ return availability;}
static json preparationIpcRow(){return json{{"childId",7},{"command","fixture"},{"messageKind","response"},{"requestId",1},{"requestIdPresent",true},{"eventName",nullptr},{"eventNamePresent",false},{"eventNameOmitted",false},{"hresult",-5}};}
static json recoveryView(){return exportEvidence()["nativeLogFailureEvidence"]["stats"]["eventAppendRecovery"];}
static bool residentPreparationPending(const json& value){return value.contains("unrecoveredResidentRecordPreparationFailureObserved")&&value["unrecoveredResidentRecordPreparationFailureObserved"]==true;}
static unsigned long long preparationSourceCount(const json& value,const char* key){const auto found=value.find("recordPreparationSources");if(found==value.end()||!found->is_object()||!found->contains(key))return ~0ULL;return (*found)[key].get<unsigned long long>();}
static unsigned preparationRows(const char* event){unsigned n=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))n+=row.value("event",std::string{})==event;return n;}
static void runCopyPreparation(const fspath::path& root){
    closeStreams();g_testRoot=(root/L"prime").wstring();fspath::create_directories(g_testRoot);const auto row=preparationIpcRow();const char* event="ipc-response-delivery-failed";
    appendNativeLifecycleLog(event,row);closeStreams();g_testRoot=(root/L"missing").wstring();
    appendNativeLifecycleLog("log-export-result",{{"fixture","actual-file-rearm-before-copy"}});fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","original-file-recovered"}});
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto before=count(Failure::preparationException);
    ymcc::testing::allocationFault::failAfter(0);appendNativeLifecycleLog(event,row);const auto consumed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    auto value=recoveryView();
    check(consumed==-1&&count(Failure::preparationException)==before+1&&residentPreparationPending(value)&&preparationSourceCount(value,"residentRecord")==1&&value["unrecoveredFailureObserved"]==false&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch,"selected existing IPC record copy allocation failure arms resident preparation without claiming file failure");
#ifdef YMCC_RECORD_PREPARATION_OLD_ONLY
    return;
#else
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(event,row);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&count(Failure::preparationException)==before+1&&preparationRows(event)==0,"50000 repeated IPC failures while preparation pending allocate nothing and do not retry a file");
    appendNativeLifecycleLog("log-export-result",{{"fixture","natural-preparation-recovery"}});appendNativeLifecycleLog(event,row);value=recoveryView();
    check(!residentPreparationPending(value)&&value["epoch"]==epoch+1&&preparationRows(event)==1&&exportPreparationIpcEvidence()["nativeIpcFailureEvidence"]["typesRetained"]==1,"original successful append clears preparation once and retains same IPC type without resetting table");
    const json publication={{"consumer","lifecycle-publication"},{"reason","fixture-preparation"}};const auto beforeTimestamp=count(Failure::preparationException);const auto epochBeforeTimestamp=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    timestampAllocationFault=true;appendNativeLifecycleLog("power-projection-consumer-failed",publication);timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);value=recoveryView();
    check(count(Failure::preparationException)==beforeTimestamp+1&&residentPreparationPending(value)&&preparationSourceCount(value,"residentRecord")==2&&value["unrecoveredFailureObserved"]==false,"actual post-selection timestamp allocation failure is resident preparation for an independent snapshot family");
    appendNativeLifecycleLog("log-export-result",{{"fixture","snapshot-preparation-recovery"}});appendNativeLifecycleLog("power-projection-consumer-failed",publication);
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epochBeforeTimestamp+1&&preparationRows("power-projection-consumer-failed")==1,"original classifier snapshot reoffers current evidence after preparation recovery");
    appendNativeLifecycleLog(event,row);const auto detailEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto lowBefore=preparationRows(event);
    g_domainLogEnabled[1]=true;timestampAllocationFault=true;appendNativeLifecycleLog(event,row);timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);g_domainLogEnabled[1]=false;
    value=recoveryView();appendNativeLifecycleLog("log-export-result",{{"fixture","detail-record-only-failure"}});appendNativeLifecycleLog(event,row);
    check(preparationSourceCount(value,"detailRecord")==1&&!residentPreparationPending(value)&&value["unrecoveredFailureObserved"]==false&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==detailEpoch&&preparationRows(event)==lowBefore,"detail record preparation failure records its source without granting a resident reoffer");
#endif
}
static void runSerializationPreparation(const fspath::path& root){
    closeStreams();g_testRoot=root.wstring();fspath::create_directories(root);const char* event="ipc-response-serialization-failed";
    const json invalid={{"fixtureUnknown",std::string(1,char(-1))}};const auto before=count(Failure::preparationException);const auto fileBefore=count(Failure::eventException);const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog(event,invalid);auto value=recoveryView();
    check(count(Failure::preparationException)==before+1&&count(Failure::eventException)==fileBefore&&residentPreparationPending(value)&&preparationSourceCount(value,"residentSerialization")==1&&value["unrecoveredFailureObserved"]==false&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&preparationRows(event)==0,"real invalid UTF8 serialization failure is preparation rather than a Native file exception");
#ifdef YMCC_RECORD_PREPARATION_OLD_ONLY
    return;
#else
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(event,invalid);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&count(Failure::preparationException)==before+1&&count(Failure::eventException)==fileBefore,"50000 same invalid serialization observations stay suppressed until original append recovery");
    const json valid={{"fixtureUnknown",true}};appendNativeLifecycleLog(event,valid);appendNativeLifecycleLog("log-export-result",{{"fixture","natural-serialization-recovery"}});appendNativeLifecycleLog(event,valid);value=recoveryView();
    check(value["epoch"]==epoch+1&&!residentPreparationPending(value)&&preparationRows(event)==1,"serialization recovery retains next current valid observation without replaying invalid bytes");
    const auto detailEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto detailBefore=count(Failure::detailException);g_domainLogEnabled[1]=true;
    appendNativeLifecyclePrepared("screen-touchpads-state",invalid,false,nullptr);g_domainLogEnabled[1]=false;value=recoveryView();appendNativeLifecycleLog("log-export-result",{{"fixture","detail-serialization-only-failure"}});
    check(preparationSourceCount(value,"detailSerialization")==1&&!residentPreparationPending(value)&&value["unrecoveredPreparationFailureObserved"]==false&&value["unrecoveredFailureObserved"]==false&&count(Failure::detailException)==detailBefore&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==detailEpoch,"detail serialization failure cannot arm resident or named screen preparation allowances");
    const std::string encoded="{\"event\":\"fixture-encoded\"}";const auto preparationBefore=count(Failure::preparationException);
    appendNativeLifecyclePrepared("encoded-serialization-bypass",invalid,true,&encoded);
    check(preparationRows("fixture-encoded")==1&&count(Failure::preparationException)==preparationBefore,"original preencoded path bypasses JSON serialization and writes original bytes");
    const auto writeBefore=count(Failure::eventWrite);{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);appendNativeLifecyclePrepared("actual-file-write-failure",valid,true,nullptr);}
    value=recoveryView();check(count(Failure::eventWrite)==writeBefore+1&&value["unrecoveredFailureObserved"]==true&&!residentPreparationPending(value)&&count(Failure::preparationException)==preparationBefore,"real stream write failure keeps its original file bucket and never masquerades as preparation");
    appendNativeLifecycleLog("log-export-result",{{"fixture","natural-write-recovery"}});
    bool legacy=true;for(const char* name:{"screen-touchpads-state","gyro-power-unsubscribe-failed","msi-dinput-feedback"}){Failure local;local.record(Failure::preparationException,name);local.observeEventAppendSuccess();legacy&=local.recoveryEpoch()==1&&!local.residentRecordPreparationFailurePending.load()&&local.recordPreparationSources==std::array<unsigned long long,4>{};}
    check(legacy,"legacy two argument named preparation sources retain original one epoch recovery behavior");
    Failure local;using Source=Failure::PreparationSource;local.recordPreparationSources[0]=~0ULL;local.record(Failure::preparationException,"fixture-selected",Source::residentRecord);local.record(Failure::preparationException,"fixture-serialization",Source::residentSerialization);local.eventRecoveryEpoch.store(~0ULL);local.observeEventAppendSuccess();
    check(local.recordPreparationSources[0]==~0ULL&&local.recordPreparationSources[2]==1&&local.recoveryEpoch()==~0ULL&&!local.residentRecordPreparationFailurePending.load(),"preparation source counters and shared recovery epoch saturate without wrapping");
    value=recoveryView();check(value["schemaVersion"]==7&&value["recordPreparationSources"]["countsAreUniqueRows"]==false&&value["recordPreparationSources"]["fileFailureProven"]==false&&value["oldLostRowRestored"]==false&&value["currentFileHealthProven"]==false,"schema7 preserves source counters as overlapping observations without file or delivery proof");
#endif
}
int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>3)return 2;const std::wstring_view phase=argc==3?argv[2]:L"all";if(phase!=L"all"&&phase!=L"copy"&&phase!=L"serialization")return 2;
    const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    g_domainLogEnabled[1]=false;
    if(phase!=L"serialization")runCopyPreparation(root/L"copy");
    if(phase!=L"copy")runSerializationPreparation(root/L"serialization");
    closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
    std::cout<<json{{"selectedCase","native-sink-record-preparation"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},
        {"evidence",exportEvidence()},{"ipcEvidence",exportPreparationIpcEvidence()},{"retainedRows",readRecoveryRows(fspath::path(g_testRoot))},
        {"failureEvidenceBytes",sizeof(Failure)},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;
}
