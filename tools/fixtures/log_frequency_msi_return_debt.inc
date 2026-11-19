@@BASIS_ENUM@@;
@@BASIS_NAME@@
static std::atomic<unsigned long long> g_msiDinputReturnDebtEpisode{77};
static unsigned long g_xinputConnectedMask=1,g_xinputPhysicalSourceMask=1;
static std::atomic<bool> g_externalPriorityVirtualSuspended{false},g_msiDinputRouteDinputArmed{false};
static std::atomic<unsigned long> g_externalPrioritySourceMask{0};
static bool msiClawKnownBoardAuthority(){return true;}
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static unsigned long long inputHostOwnerToken(){return 71;}
static unsigned long long inputHostEpochToken(){return 73;}
static void originalDebtReceipt(bool discharged) {
    const bool readbackVerified=false,xinputReAdmitted=false;const unsigned long admittedMask=0;const char* where="owned-original-receipt";
    const auto basis=MsiReturnDebtBasis::ReadbackVerified;
    if(discharged){@@DISCHARGED@@ return;}
    @@RETAINED@@
}
static json retainedDebtPayload(){return {{"action","retained"},{"basis","release-unproven"},{"returnDebtEpisodeId",77ULL},{"nativePid",static_cast<unsigned long long>(GetCurrentProcessId())},{"powerGeneration",1ULL},{"where","owned-original-receipt"},{"readbackVerified",false},{"xinputReAdmitted",false},{"connectedMask",1ULL},{"physicalSourceMask",1ULL},{"admittedMask",0ULL},{"externalPrioritySuspended",false},{"externalPrioritySourceMask",0ULL},{"routeDisarmed",true},{"boardAuthorized",true},{"returnDebtRetained",true},{"policy","qualified device-side fact only; retained until readback/xinput-readmission"}};}
static bool debtPayloadExact(const json& row,const json& payload){for(auto it=payload.begin();it!=payload.end();++it){const auto key=it.key()=="powerGeneration"?"producerPowerGeneration":it.key().c_str();if(!row.contains(key)||row[key]!=it.value())return false;}return true;}
static unsigned debtDetailRows(){g_nativeDetailLog.flush();std::ifstream in(fspath::path(g_testRoot)/L"native-detail.log");unsigned rows=0;std::string line;while(std::getline(in,line))if(!line.empty())++rows;return rows;}
static void debtOffer(const json& value){appendNativeLifecycleLog("msi-claw-return-debt",value);}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
#ifndef YMCC_MSI_RETURN_DEBT_CONTINUATION_ONLY
    check(nativeLifecycleEventIsBase("msi-claw-return-debt"),"existing-msi-return-debt-name-resident-registered");
#endif
    const auto retained=retainedDebtPayload();originalDebtReceipt(false);originalDebtReceipt(true);originalDebtReceipt(false);auto rows=readRecoveryRows(root);
    check(rows.size()==3&&debtPayloadExact(rows[0],retained)&&rows[1]["action"]=="discharged"&&rows[1]["basis"]=="readback-verified"&&rows[1]["ownerToken"]==71&&rows[1]["hostEpoch"]==73&&debtPayloadExact(rows[2],retained),"two-original-initializers-off-retained-discharge-and-next-retained-exact");
#ifndef YMCC_MSI_RETURN_DEBT_OLD_ONLY
    const auto writes=g_nativeLifecycleFrameWrites;ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)debtOffer(retained);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&writes==g_nativeLifecycleFrameWrites&&!g_nativeDetailLog.is_open(),"50000-identical-retained-observations-off-allocation-free-and-silent");
    g_domainLogEnabled[1]=true;for(unsigned i=0;i<100;++i)debtOffer(retained);g_domainLogEnabled[1]=false;
    check(debtDetailRows()==100&&readRecoveryRows(root).size()==3,"one-hundred-repeat-observations-go-only-to-enabled-detail");
    const auto priorFields=readRecoveryRows(root).size();unsigned fields=0;
    for(auto it=retained.begin();it!=retained.end();++it){auto next=retained;if(it->is_boolean())next[it.key()]=!it->get<bool>();else if(it->is_number())next[it.key()]=it->get<unsigned long long>()+1;else next[it.key()]=it->get<std::string>()+"-changed";debtOffer(next);debtOffer(retained);++fields;}
    check(fields==17&&readRecoveryRows(root).size()==priorFields+34,"all-seventeen-field-changes-and-return-to-original-remain-visible");
    const auto priorBad=readRecoveryRows(root).size();unsigned malformed=0;
    for(auto it=retained.begin();it!=retained.end();++it){auto bad=retained;bad[it.key()]=json::array({1});debtOffer(bad);debtOffer(bad);bad=retained;bad.erase(it.key());debtOffer(bad);debtOffer(bad);malformed+=2;}
    auto future=retained;future["futureField"]=1;debtOffer(future);debtOffer(future);malformed+=1;
    check(malformed==35&&readRecoveryRows(root).size()==priorBad+70,"thirty-five-bad-type-missing-or-future-shapes-keep-each-unknown-receipt");
    const auto priorTerminal=readRecoveryRows(root).size();for(unsigned i=0;i<3;++i)originalDebtReceipt(true);
    check(readRecoveryRows(root).size()==priorTerminal+3,"every-identical-discharge-operation-keeps-terminal-receipt");
    const auto priorRearm=readRecoveryRows(root).size();debtOffer(retained);debtOffer(retained);
    check(readRecoveryRows(root).size()==priorRearm+1,"discharge-rearms-next-identical-retained-observation");
    const auto priorEpisode=readRecoveryRows(root).size();auto episode=retained;episode["returnDebtEpisodeId"]=78ULL;debtOffer(episode);debtOffer(episode);
    check(readRecoveryRows(root).size()==priorEpisode+1,"new-debt-episode-retained-once-without-cross-operation-suppression");
    const auto priorPower=readRecoveryRows(root).size();++g_powerGeneration;debtOffer(episode);debtOffer(episode);rows=readRecoveryRows(root);
    check(rows.size()==priorPower+1&&rows.back()["producerPowerGeneration"]==1&&rows.back()["powerGeneration"]==2,"observed-power-generation-rearms-and-keeps-producer-generation");
    auto fileState=episode;fileState["where"]="owned-file-failure";const auto failureCount=count(Failure::eventWrite),priorFile=readRecoveryRows(root).size();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);debtOffer(fileState);}debtOffer(fileState);rows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==failureCount+1&&rows.size()==priorFile+1&&debtPayloadExact(rows.back(),fileState),"actual-file-failure-reoffers-on-next-original-observation");
    const auto priorRecovery=rows.size();debtOffer(fileState);debtOffer(fileState);
    check(readRecoveryRows(root).size()==priorRecovery+1,"delivery-recovery-epoch-reoffers-once-then-resumes-repeat-suppression");
    auto allocated=fileState;allocated["where"]=std::string(180,'a');const auto prep=count(Failure::preparationException),priorAllocation=readRecoveryRows(root).size();ymcc::testing::allocationFault::failAfter(0);debtOffer(allocated);ymcc::testing::allocationFault::failAfter(-1);debtOffer(allocated);debtOffer(allocated);rows=readRecoveryRows(root);
    check(count(Failure::preparationException)==prep+1&&rows.size()==priorAllocation+1&&debtPayloadExact(rows.back(),allocated),"memo-copy-allocation-failure-does-not-poison-next-original-observation");
#ifndef YMCC_MSI_RETURN_DEBT_CONTINUATION_ONLY
    const auto priorHigh=readRecoveryRows(root).size();const json high={{"reason","buttons-change"}};ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto highArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(highArm==0&&readRecoveryRows(root).size()==priorHigh&&!nativeLifecycleEventIsBase("msi-claw-return-debt-future"),"unrelated-high-frequency-samples-and-unknown-event-suffix-stay-suppressed");
#endif
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-returned-before-exit");unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failures},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"productOrSteamActions",0},{"originalProducerStatements",2},{"businessDischargeExecuted",false}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
