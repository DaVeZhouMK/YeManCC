static json sensorRefusal(const char* request="suspend",const char* reason="sta-owner-or-wake-unavailable"){
    return {{"request",request},{"phase","admission"},{"reason",reason},{"submissionAccepted",false},{"commandCompleted",false}};
}
static unsigned boundaryRows(const char* event){unsigned count=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))count+=row.value("event",std::string{})==event;return count;}
static unsigned detailedBoundaryRows(){std::ifstream input(fspath::path(g_testRoot)/L"native-detail.log");unsigned count=0;std::string line;while(std::getline(input,line))if(!line.empty())++count;return count;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    const std::vector<const char*> events={"exit-cleanup-worker-unavailable","exit-progress-timer-unavailable","gyro-pnp-rearm-skip","gyro-pnp-rearm-start","gyro-pnp-rearm-failed","gyro-pnp-rearm-done","hidhide.cycle-pipe-failed","hidhide.p-hid-apply-failed","webview-crash-module","gyro-power-command-queued","gyro-power-command-completed","gyro-power-command-rejected","app-exit-request","battery-in-process-exit","battery-in-process-final-exit","exit-steam-original-recovery-stopped","steam-document-cleanup-incomplete"};
    bool allBase=true;for(const auto* event:events)allBase&=nativeLifecycleEventIsBase(event);
    check(allBase&&events.size()==17,"seventeen original finite boundary events survive the Native default OFF registry");
#ifndef YMCC_BOUNDARY_FAILURES_OLD_ONLY
    const auto rejected="gyro-power-command-rejected";const json suspend=sensorRefusal(),resume=sensorRefusal("resume");
    for(const auto* event:events)appendNativeLifecycleLog(event,std::strcmp(event,rejected)==0?suspend:json{{"fixture","original-boundary"}});
    bool everyOnce=true;for(const auto* event:events)everyOnce&=boundaryRows(event)==1;
    check(everyOnce&&readRecoveryRows(root).size()==17&&!g_nativeDetailLog.is_open(),"actual original append retains all seventeen first boundary receipts with detail OFF");
    for(const auto* event:events)appendNativeLifecycleLog(event,std::strcmp(event,rejected)==0?suspend:json{{"fixture","second-original-operation"}});
    bool everyTwice=true;for(const auto* event:events)everyTwice&=boundaryRows(event)==2;
    check(everyTwice&&readRecoveryRows(root).size()==34,"each new explicit operation boundary keeps its own receipt including gyro queued and completed");
    const auto writes=g_nativeLifecycleFrameWrites;const auto prep=count(Failure::preparationException);ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(rejected,suspend);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&g_nativeLifecycleFrameWrites==writes&&count(Failure::preparationException)==prep&&boundaryRows(rejected)==2,"50000 same known gyro command refusals are silent and allocate no diagnostic payload with detail OFF");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(rejected,suspend);g_nativeDetailLog.flush();g_domainLogEnabled[1]=false;
    check(detailedBoundaryRows()==1&&boundaryRows(rejected)==2,"same refusal reaches original detailed lane when enabled without adding a resident copy");
    appendNativeLifecycleLog(rejected,resume);const auto both=boundaryRows(rejected);const auto bothWrites=g_nativeLifecycleFrameWrites;
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(rejected,(i&1)?suspend:resume);const auto alternateArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(both==3&&alternateArm==0&&g_nativeLifecycleFrameWrites==bothWrites&&boundaryRows(rejected)==both,"two independent request slots preserve alternating suspend and resume refusal memory for 50000 calls");
    const json changed=sensorRefusal("resume","mailbox-admission-refused");appendNativeLifecycleLog(rejected,changed);appendNativeLifecycleLog(rejected,changed);
    check(boundaryRows(rejected)==both+1,"changed known refusal reason retains one new current witness");
    ++g_powerGeneration;const auto beforePower=boundaryRows(rejected);appendNativeLifecycleLog(rejected,suspend);appendNativeLifecycleLog(rejected,changed);appendNativeLifecycleLog(rejected,suspend);appendNativeLifecycleLog(rejected,changed);
    check(boundaryRows(rejected)==beforePower+2,"new power generation retains one witness independently for each request slot");
    const auto beforeAction=boundaryRows(rejected);
    appendNativeLifecycleLog("gyro-power-command-queued",{{"ticket",7},{"request",4},{"completed",false}});appendNativeLifecycleLog(rejected,changed);
    appendNativeLifecycleLog("gyro-power-command-completed",{{"ticket",7},{"request",4},{"commandSucceeded",false}});appendNativeLifecycleLog(rejected,changed);
    check(boundaryRows(rejected)==beforeAction+2&&boundaryRows("gyro-power-command-queued")==3&&boundaryRows("gyro-power-command-completed")==3,"original queued and failed completed receipts end only the log refusal episode without claiming sensor recovery");
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();const auto beforeRecovery=boundaryRows(rejected);
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);appendNativeLifecyclePrepared("original-file-failure",json{{"fixture",true}},true,nullptr);}
    appendNativeLifecycleLog(rejected,changed);const auto notRetried=boundaryRows(rejected);appendNativeLifecycleLog("log-export-result",{{"fixture","original-file-recovered"}});appendNativeLifecycleLog(rejected,changed);appendNativeLifecycleLog(rejected,changed);
    check(notRetried==beforeRecovery&&boundaryRows(rejected)==beforeRecovery+1&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch+1,"real file failure does not trigger refusal retry until original successful append grants one current witness");
    std::vector<json> invalid;for(const auto* key:{"request","phase","reason","submissionAccepted","commandCompleted"}){auto row=changed;row[key]=nullptr;invalid.push_back(std::move(row));}
    {auto row=changed;row["future"]=true;invalid.push_back(std::move(row));}{auto row=changed;row["reason"]="future";invalid.push_back(std::move(row));}{auto row=changed;row["request"]="future";invalid.push_back(std::move(row));}{auto row=changed;row["submissionAccepted"]=true;invalid.push_back(std::move(row));}{auto row=changed;row["commandCompleted"]=true;invalid.push_back(std::move(row));}
    const auto beforeUnknown=boundaryRows(rejected);for(const auto& row:invalid)appendNativeLifecycleLog(rejected,row);appendNativeLifecycleLog(rejected,changed);appendNativeLifecycleLog(rejected,changed);
    check(invalid.size()==10&&boundaryRows(rejected)==beforeUnknown+11,"ten unsupported or contradictory refusal shapes remain resident and rearm next known observation rather than being silently deduplicated");
    const json preparation=sensorRefusal("resume","resume-scope-not-current");const auto beforePreparation=boundaryRows(rejected);const auto epochBeforePreparation=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();timestampAllocationFault=true;appendNativeLifecycleLog(rejected,preparation);timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);
    const auto failedPreparation=count(Failure::preparationException);for(unsigned i=0;i<1000;++i)appendNativeLifecycleLog(rejected,preparation);
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-preparation-recovered"}});appendNativeLifecycleLog(rejected,preparation);
    check(count(Failure::preparationException)==failedPreparation&&boundaryRows(rejected)==beforePreparation+1&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epochBeforePreparation+1,"selected refusal preparation failure stays quiet until original append recovery permits one current witness");
    const json firstBorrowed=sensorRefusal("resume","unspecified");ymcc::testing::allocationFault::failAfter(0);const bool first=nativeLifecycleRecordIsStreaming(rejected,firstBorrowed),repeat=nativeLifecycleRecordIsStreaming(rejected,firstBorrowed);const auto borrowedArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(!first&&repeat&&borrowedArm==0,"first and repeat refusal classification use bounded POD slots without copying JSON or allocating text");
    const auto beforeOther=g_nativeLifecycleFrameWrites;appendNativeLifecycleLog("not-a-base-event-194-probe",{{"fixture",true}});appendNativeLifecycleLog("input-native-signal-window",{{"fixture",true}});appendNativeLifecycleLog("system-xinput-readback",{{"reason","buttons-change"}});
    check(g_nativeLifecycleFrameWrites==beforeOther,"unregistered probes and existing high frequency input trajectories remain suppressed with detail OFF");
#endif
    timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all owned boundary and refusal log handles returned before exit");
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"selectedCase","native-sink-boundary-failures"},{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"registeredEvents",events},{"evidence",exportEvidence()},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
