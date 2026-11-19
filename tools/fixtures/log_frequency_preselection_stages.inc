static json preselectionReceipt(){const auto stats=ymcc::logging::nativeLogFailureEvidence().snapshot();const auto found=stats.find("preselectionExceptionStages");return found==stats.end()?json(nullptr):*found;}
static unsigned long long preselectionCount(const char* stage){const auto receipt=preselectionReceipt();if(!receipt.is_object()||!receipt.contains("stages"))return ~0ULL;for(const auto& row:receipt["stages"])if(row.value("stage",std::string{})==stage)return row["observedFailures"].get<unsigned long long>();return ~0ULL;}
static unsigned preselectionRows(const char* event){unsigned result=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))result+=row.value("event",std::string{})==event;return result;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();appendNativeLifecycleLog("gamepad-vibrate-request",{{"requestSource","fixture-simulation"}});
    DWORD before=0;GetProcessHandleCount(GetCurrentProcess(),&before);const auto genericBefore=count(Failure::preparationException),fileBefore=count(Failure::eventException);const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    const json projection={{"phase","fixture-projection"},{"reason",std::string(192,'r')}};ymcc::testing::allocationFault::failAfter(0);appendNativeLifecycleLog("steam-game-link-state",projection);const auto projectionArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(projectionArm==-1&&count(Failure::preparationException)==genericBefore+1&&count(Failure::eventException)==fileBefore&&preselectionCount("steam-log-projection")==1&&preselectionCount("native-log-classification")==0&&!g_nativeLifecycleLog.is_open()&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch,
        "actual original Steam projection allocation failure reports preselection stage without a file lane or recovery grant");
#ifndef YMCC_PRESELECTION_OLD_ONLY
    appendNativeLifecycleLog("steam-game-link-state",{{"phase",std::string(1,char(-1))}});
    check(preselectionCount("steam-log-projection")==2&&preselectionCount("native-log-classification")==0&&count(Failure::preparationException)==genericBefore+2&&count(Failure::eventException)==fileBefore,
        "actual projection invalid UTF8 serialization exception shares caught projection stage without claiming allocation failure");
    const json hud={{"hudEvent","fixture-status"},{"width",800},{"payload",std::string(192,'h')}};ymcc::testing::allocationFault::failAfter(0);appendNativeLifecycleLog("desktop-hud-event",hud);const auto classifierArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(classifierArm==-1&&preselectionCount("native-log-classification")==1&&count(Failure::preparationException)==genericBefore+3&&!ymcc::logging::nativeLogFailureEvidence().residentRecordPreparationFailurePending.load()&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch,
        "actual snapshot classifier JSON copy allocation failure is unassigned preselection rather than selected record failure");
    appendNativeLifecycleLog("desktop-hud-event",hud);
    check(preselectionRows("desktop-hud-event")==1&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch,
        "next current snapshot remains eligible after failed classifier copy without replay or rearm permission");
    const auto afterHud=count(Failure::preparationException);ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("desktop-hud-event",hud);const auto repeatArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(repeatArm==0&&count(Failure::preparationException)==afterHud&&preselectionRows("desktop-hud-event")==1&&preselectionCount("native-log-classification")==1,
        "50000 unchanged snapshot calls remain silent and allocation free after actual classifier fault");
    appendNativeLifecycleLog("gamepad-vibrate-request",{{"requestSource",false}});
    check(preselectionCount("native-log-classification")==2&&count(Failure::preparationException)==afterHud+1&&count(Failure::eventException)==fileBefore,
        "actual malformed text classifier type exception records same call stage without masquerading as allocation or file failure");
    const json ipc={{"childId",7},{"command","fixture"},{"messageKind","response"},{"requestId",1},{"requestIdPresent",true},{"eventName",nullptr},{"eventNamePresent",false},{"eventNameOmitted",false},{"hresult",-5}};
    const auto beforeIpc=count(Failure::preparationException);ymcc::testing::allocationFault::failAfter(0);appendNativeLifecycleLog("ipc-response-delivery-failed",ipc);const auto ipcArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    {auto& state=ymcc::logging::ipcFailureLogState();std::lock_guard<std::mutex> lock(state.mutex);check(ipcArm==-1&&preselectionCount("native-log-classification")==3&&count(Failure::preparationException)==beforeIpc+1&&state.value.types==0,
        "actual IPC type witness copy failure records classifier stage before type selection is committed");}
    appendNativeLifecycleLog("ipc-response-delivery-failed",ipc);{auto& state=ymcc::logging::ipcFailureLogState();std::lock_guard<std::mutex> lock(state.mutex);check(state.value.types==1&&preselectionRows("ipc-response-delivery-failed")==1&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch,
        "next current IPC failure retains original first type after interrupted copy without resetting lifetime table");}
    timestampAllocationFault=true;appendNativeLifecycleLog("steam-game-link-state",projection);timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);auto recovery=ymcc::logging::nativeLogFailureEvidence().snapshot()["eventAppendRecovery"];
    check(preselectionCount("steam-log-projection")==2&&preselectionCount("native-log-classification")==3&&recovery["recordPreparationSources"]["residentRecord"]==1&&recovery["unrecoveredResidentRecordPreparationFailureObserved"]==true,
        "stage resets after both calls so later actual timestamp allocation remains selected record preparation");
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-selected-record-recovery"}});const auto beforeMirror=count(Failure::preparationException);mirrorThrows=true;appendNativeLifecycleLog("gamepad-vibrate-request",{{"requestSource","fixture-simulation"}});mirrorThrows=false;
    check(count(Failure::preparationException)==beforeMirror+1&&preselectionCount("steam-log-projection")==2&&preselectionCount("native-log-classification")==3&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch+1,
        "suppressed mirror exception remains outside both preselection stages and adds no rearm permission");
    Failure local;using Stage=Failure::PreselectionStage;std::string name(120,'e');name[17]=char(-1);ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<10000;++i)local.record(Failure::preparationException,name.c_str(),Failure::PreparationSource::none,Stage::nativeClassifier);const auto localArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);const auto localView=local.snapshot()["preselectionExceptionStages"]["stages"][1];
    check(localArm==0&&localView["observedFailures"]==10000&&localView["firstEvent"].get_ref<const std::string&>().size()==95&&localView["firstEventTruncated"]==true&&localView["firstEventAsciiSanitized"]==true,
        "two fixed stage buckets capture bounded first event and 10000 repeats with no C++ allocation");
    local.preselectionStages[0].count=~0ULL;local.record(Failure::preparationException,"saturated",Failure::PreparationSource::none,Stage::steamProjection);local.record(Failure::eventWrite,"wrong-kind",Failure::PreparationSource::none,Stage::nativeClassifier);local.record(Failure::preparationException,"unknown-stage",Failure::PreparationSource::none,static_cast<Stage>(99));
    check(local.preselectionStages[0].count==~0ULL&&local.preselectionStages[1].count==10000&&local.recoveryEpoch()==0&&!local.residentRecordPreparationFailurePending.load()&&!local.residentPathPreparationFailurePending.load(),
        "stage counters saturate ignore nonpreparation and unknown stage without changing original recovery state");
    Failure legacy;legacy.record(Failure::preparationException,"screen-touchpads-state");legacy.record(Failure::preparationException,"already-selected",Failure::PreparationSource::residentRecord);legacy.observeEventAppendSuccess();recovery=legacy.snapshot()["eventAppendRecovery"];
    check(legacy.preselectionStages[0].count==0&&legacy.preselectionStages[1].count==0&&recovery["schemaVersion"]==7&&legacy.recoveryEpoch()==1&&recovery["oldLostRowRestored"]==false,
        "legacy two and three argument record calls retain exact schema7 named and selected recovery behavior");
#endif
    ymcc::testing::allocationFault::failAfter(-1);timestampAllocationFault=false;mirrorThrows=false;closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all owned preselection consumer handles returned before exit");
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","native-sink-preselection-stages"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"failureEvidenceBytes",sizeof(Failure)},{"evidence",exportEvidence()},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
