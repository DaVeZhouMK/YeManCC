static json pathIpcEvidence(){json availability;@@ACTUAL_IPC_EXPORT@@ return availability;}
static json pathRecovery(){return exportEvidence()["nativeLogFailureEvidence"]["stats"]["eventAppendRecovery"];}
static bool pathPending(const json& value){const auto it=value.find("unrecoveredResidentPathPreparationFailureObserved");return it!=value.end()&&it->is_boolean()&&it->get<bool>();}
static unsigned long long pathCount(const json& value,const char* key){const auto it=value.find("pathPreparationSources");if(it==value.end()||!it->is_object()||!it->contains(key))return ~0ULL;return (*it)[key].get<unsigned long long>();}
static unsigned pathRows(const char* event){unsigned value=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))value+=row.value("event",std::string{})==event;return value;}
static void clearPathFault(){pathAllocationFailureAfter=-1;ymcc::testing::allocationFault::failAfter(-1);}
static bool observePathAllocation(const fspath::path& root,int after){
    closeStreams();g_testRoot=root.wstring();fspath::create_directories(root);const json row={{"fixture","already-prepared-path-fault"}};
    const auto preparation=count(Failure::preparationException),file=count(Failure::eventException),paths=pathCount(pathRecovery(),"residentPath");const auto writes=g_nativeLifecycleFrameWrites;
    pathAllocationFailureAfter=after;appendNativeLifecyclePrepared("log-export-result",row,true,nullptr);const auto consumed=ymcc::testing::allocationFault::countdown.load();clearPathFault();const auto value=pathRecovery();
    return consumed==-1&&count(Failure::preparationException)==preparation+1&&count(Failure::eventException)==file&&pathCount(value,"residentPath")==paths+1&&pathPending(value)&&value["unrecoveredFailureObserved"]==false&&
        value.value("unrecoveredResidentRecordPreparationFailureObserved",true)==false&&g_nativeLifecycleFrameWrites==writes&&!g_nativeLifecycleLog.is_open()&&!fspath::exists(root/L"native-lifecycle.log");
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0;GetProcessHandleCount(GetCurrentProcess(),&before);g_domainLogEnabled[1]=false;
    check(observePathAllocation(root/L"directory-copy",0),"actual directory string allocation failure is path preparation before any file attempt rather than a file exception");
#ifndef YMCC_PATH_PREPARATION_OLD_ONLY
    check(observePathAllocation(root/L"filesystem-path",1),"actual later filesystem path allocation remains preparation without incrementing file attempts");
    const auto initialEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();appendNativeLifecycleLog("log-export-result",{{"fixture","natural-path-recovery"}});auto value=pathRecovery();
    check(!pathPending(value)&&value["epoch"]==initialEpoch+1&&pathCount(value,"residentPath")==2&&pathRows("log-export-result")==1,"original successful resident append clears path pending once while preserving historical source count");
    const char* ipcEvent="ipc-response-delivery-failed";const json ipc={{"childId",7},{"command","fixture"},{"messageKind","response"},{"requestId",1},{"requestIdPresent",true},{"eventName",nullptr},{"eventNamePresent",false},{"eventNameOmitted",false},{"hresult",-5}};
    const auto writes=g_nativeLifecycleFrameWrites;pathAllocationFailureAfter=0;appendNativeLifecycleLog(ipcEvent,ipc);clearPathFault();value=pathRecovery();
    check(pathPending(value)&&pathCount(value,"residentPath")==3&&g_nativeLifecycleFrameWrites==writes&&pathRows(ipcEvent)==0&&pathIpcEvidence()["nativeIpcFailureEvidence"]["typesRetained"]==1,"selected first IPC path fault retains original lifetime type and pending state without a file attempt");
    const auto preparation=count(Failure::preparationException);ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(ipcEvent,ipc);const auto repeatArm=ymcc::testing::allocationFault::countdown.load();clearPathFault();
    check(repeatArm==0&&count(Failure::preparationException)==preparation&&g_nativeLifecycleFrameWrites==writes&&pathRows(ipcEvent)==0,"50000 repeated IPC observations after path failure allocate nothing and do not retry a file");
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-following-append"}});appendNativeLifecycleLog(ipcEvent,ipc);value=pathRecovery();
    check(value["epoch"]==initialEpoch+2&&!pathPending(value)&&pathRows(ipcEvent)==1&&pathIpcEvidence()["nativeIpcFailureEvidence"]["typesRetained"]==1,"natural good append permits one current IPC witness without resetting type table or replaying the lost row");
    const auto detailEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto detailException=count(Failure::detailException);const json prepared={{"fixture","detail-path-only"}};
    g_domainLogEnabled[1]=true;pathAllocationFailureAfter=0;appendNativeLifecyclePrepared("screen-touchpads-state",prepared,false,nullptr);clearPathFault();g_domainLogEnabled[1]=false;value=pathRecovery();
    appendNativeLifecycleLog("log-export-result",{{"fixture","detail-does-not-rearm"}});
    check(pathCount(value,"detailPath")==1&&!pathPending(value)&&value["unrecoveredPreparationFailureObserved"]==false&&value["unrecoveredResidentRecordPreparationFailureObserved"]==false&&count(Failure::detailException)==detailException&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==detailEpoch,"detailed path preparation is counted separately and grants no resident or named screen allowance");
    const auto detailedPreparation=count(Failure::preparationException);pathAllocationFailureAfter=0;ymcc::testing::allocationFault::failAfter(0);appendNativeLifecyclePrepared("detail-disabled",prepared,false,nullptr);const auto offArm=ymcc::testing::allocationFault::countdown.load();clearPathFault();
    check(offArm==0&&count(Failure::preparationException)==detailedPreparation&&!g_nativeDetailLog.is_open(),"original detail OFF guard returns before any directory or path preparation");
    closeStreams();g_testRoot=(root/L"absent"/L"parent").wstring();const auto openBefore=count(Failure::eventOpen);appendNativeLifecyclePrepared("real-open-failure",prepared,true,nullptr);value=pathRecovery();
    check(count(Failure::eventOpen)==openBefore+1&&value["unrecoveredFailureObserved"]==true&&!pathPending(value)&&pathCount(value,"residentPath")==3&&pathCount(value,"detailPath")==1,"real missing-directory file open failure retains original file bucket rather than path preparation");
    closeStreams();g_testRoot=(root/L"file-recovery").wstring();fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","original-open-recovery"}});
    const auto writeBefore=count(Failure::eventWrite);{ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);appendNativeLifecyclePrepared("real-write-failure",prepared,true,nullptr);}value=pathRecovery();
    check(count(Failure::eventWrite)==writeBefore+1&&value["unrecoveredFailureObserved"]==true&&!pathPending(value)&&pathCount(value,"residentPath")==3&&pathCount(value,"detailPath")==1,"actual stream write failure remains file evidence and never increments path preparation counters");
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-write-recovery"}});const auto encodedBefore=count(Failure::preparationException);const std::string encoded="{\"event\":\"path-preencoded\"}";
    appendNativeLifecyclePrepared("preencoded",json{{"bad",std::string(1,char(-1))}},true,&encoded);
    check(pathRows("path-preencoded")==1&&count(Failure::preparationException)==encodedBefore,"successful original preencoded insertion preserves bytes and bypasses serialization after path preparation");
    Failure local;using Source=Failure::PreparationSource;local.pathPreparationSources[0]=~0ULL;local.record(Failure::preparationException,"screen-touchpads-state",Source::residentPath);local.record(Failure::preparationException,"screen-touchpads-state",Source::detailPath);local.eventRecoveryEpoch.store(~0ULL);local.observeEventAppendSuccess();
    check(local.pathPreparationSources[0]==~0ULL&&local.pathPreparationSources[1]==1&&local.recoveryEpoch()==~0ULL&&!local.residentPathPreparationFailurePending.load()&&!local.residentRecordPreparationFailurePending.load()&&!local.screenPreparationFailurePending.load()&&local.recordPreparationSources==std::array<unsigned long long,4>{},"typed path counters and epoch saturate without altering record or named preparation flags");
    value=pathRecovery();check(value["schemaVersion"]==7&&value["pathPreparationSources"]["countsAreUniqueRows"]==false&&value["pathPreparationSources"]["fileFailureProven"]==false&&value["oldLostRowRestored"]==false&&value["currentFileHealthProven"]==false,"schema7 path source observations remain distinct from file health unique rows and restored delivery");
#endif
    clearPathFault();closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all owned path preparation consumer handles returned before exit");
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","native-sink-path-preparation"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"failureEvidenceBytes",sizeof(Failure)},{"evidence",exportEvidence()},{"ipcEvidence",pathIpcEvidence()},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){clearPathFault();std::cerr<<error.what()<<'\n';return 3;}}
