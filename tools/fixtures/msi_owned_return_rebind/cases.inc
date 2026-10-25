static void startupSeed(){
    reset();g_msiCrashLoaded=true;g_msiCrashInheritedPending=true;g_msiCrashStartupAttempted=false;
    g_msiCrashStartupTransactionId.clear();g_msiCrashStartupHiddenPaths.clear();g_msiCrashNextTick=0;
    g_msiCrashUnhideAttempts=0;g_msiCrashSettlementChecks=0;g_msiCrashLastDecision.clear();g_msiCrashDecisionRows=0;
    g_msiCrashRecord.ownerPid=30000;g_msiCrashRecord.ownerCreationTime="133800000000000001";
    g_msiCrashRecord.modeAttemptOwner="30000:133800000000000001";
    g_msiDinputReturnDebtEpisode=0;g_msiDinputReturnOwed=false;g_msiMode1TerminalAttempted=false;
    g_msiDinputSourceValid=true;g_msiDinputRouteConfirmed=false;
    g_msiFeedbackSamplePower=g_power;g_msiFeedbackSampleMode=g_msiFeedbackModeGeneration.load();g_msiFeedbackSampleTopology=g_xinputTopologyEpoch.load();
    g_powerLifecycle=PowerLifecycle::Ready;g_hidHideVisibilityJournal={};
    g_fakeOldOwnerDead=g_fakeStoreOk=g_fakeResolveOk=g_fakeVisibilityOk=g_fakeUnhideOk=g_fakeLocalClearOk=true;
    g_fakeInitialWrites=g_fakeStoreCalls=g_fakeUnhides=g_fakeSchedules=0;g_fakeStoreHook=g_fakeIdentityHook={};
    g_fakeSource={};g_fakeSource.usbInstance=kOwnedRoot;g_fakeSource.interfacePath=W2U(kCommandPath);
    g_fakePhysical.clear();g_fakeHidden={U2W(kOwnedRoot)};g_fakeInitialOutcome=successful();
    g_fakeDurableText=msiRecovery::encodeRecord(g_msiCrashRecord);
}
static void lifecycle(ULONGLONG tick){g_tick=tick;msiCrashRecoveryMaintenance(tick);msiOwnedReturnMaintenance(tick);}
static void returned(unsigned long long sequence,ULONGLONG tick){
    physical();sample(sequence,tick);g_fakePhysical={{true,0x0db0,0x1901,kReturnedRoot}};
}

static void rebindSeed(){startupSeed();g_rebindFixtureEnabled=true;rebindRelease();
    g_msiDinput.enabled=true;g_msiDinput.openTried=false;g_msiDinput.exhausted=false;
    g_msiDinput.lastFailAt=g_msiDinput.episodeStartedAt=0;g_msiDinput.backoffMs=250;
    g_msiDinput.episodeOpens=g_msiDinput.openAttempts=g_msiDinput.bindCount=0;
    g_fakeSource.valid=true;g_fakeSource.vendorId=0x0db0;g_fakeSource.productId=0x1902;
    g_fakeSource.generation=1;g_fakeSource.usbInstance=kOwnedRoot;g_fakeSource.interfacePath=W2U(kCommandPath);
    g_msiMainSource={};g_msiFeedbackSourceRecord.reset();g_msiFeedbackSourceGeneration=0;
    g_rebindReleaseCalls=0;g_rebindReleaseAllowed=true;g_rebindReleaseHook={};
}
int main(){Checks c;(void)&returned;
 c.run("pump-rebind/same-owned-source-between-initial-and-retry",[&]{
    rebindSeed();g_fakeInitialOutcome=disconnected();lifecycle(10000);
    c.require(g_msiOwnedReturnPending && g_msiOwnedReturnRetrySafe && rebindActualDeviceTargetAbsent(),"real initial failure leaves safe retry and released target");
    g_tick=10008;rebindActualPumpOpenFragment();
    c.require(g_msiDinput.openAttempts==1 && !rebindActualDeviceTargetAbsent(),"actual enabled OFF pump open branch rebinds the physical DInput and target becomes present");
    lifecycle(12000);
    c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"owned reconciliation must still be able to quiesce its same rebound source and retry once");
    c.require(rebindActualDeviceTargetAbsent() && g_rebindReleaseCalls==2,"one Stop release and one same-source retry quiesce leave no local target");
    const auto used=g_count.writes;g_tick=12008;rebindActualPumpOpenFragment();lifecycle(14000);
    c.require(g_count.writes==used,"a later rebind cannot refund a used retry");
    c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"write receipt must not settle physical return");
 });
 c.run("source-event/actual-DInput-publish-reopens-dormant-observation",[&]{
    rebindSeed();g_commandAvailable=false;g_fakeInitialOutcome={};lifecycle(10000);
    for(ULONGLONG t=11000;t<40000;t+=1000)lifecycle(t);
    c.require(g_msiOwnedReturnFailed && !g_msiOwnedReturnWorking && !g_msiOwnedReturnRecovery.retryUsed(),"missing command has exhausted observation without spending retry");
    const auto event=g_msiOwnedReturnSourceEvent.load();g_commandAvailable=true;g_tick=40000;rebindActualPumpOpenFragment();
    c.require(g_msiOwnedReturnSourceEvent.load()!=event,"actual DInput availability publication must notify a pending owned return");
    lifecycle(41000);
    c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"late same source can reopen and consume the original retry");
 });

 const auto rebound=[](){g_tick=10008;rebindActualPumpOpenFragment();};
 const auto prepare=[&](){rebindSeed();g_fakeInitialOutcome=disconnected();lifecycle(10000);rebound();};
 c.run("quiesce/retry-delay-is-read-only-until-original-deadline",[&]{
    prepare();const auto releases=g_rebindReleaseCalls;const auto observations=g_msiOwnedReturnRecovery.observations();
    lifecycle(11000);c.require(g_rebindReleaseCalls==releases && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"before 1500ms no source release or retry claim");
    c.require(g_msiOwnedReturnRecovery.observations()>=1 && observations==1,"availability event can refresh observation while original retry clock remains unchanged");
    lifecycle(12000);c.require(g_count.writes==1,"original eligible delay later permits one retry");
 });
 std::vector<std::pair<std::string,std::function<void()>>> boundRejects={
    {"invalid",[]{g_fakeSource.valid=false;}},{"zero-generation",[]{g_fakeSource.generation=0;}},
    {"wrong-vendor",[]{g_fakeSource.vendorId=1;}},{"wrong-product",[]{g_fakeSource.productId=0x1901;}},
    {"empty-path",[]{g_fakeSource.interfacePath.clear();}},{"empty-root",[]{g_fakeSource.usbInstance.clear();}},
    {"current-root-mismatch",[]{g_fakeSource.usbInstance="USB\\VID_0DB0&PID_1902\\OTHER";}},
    {"resolved-other-device",[]{const auto path=LR"(\\?\hid#other)";g_fakeSource.interfacePath=W2U(path);g_fakeSource.usbInstance="USB\\VID_0DB0&PID_1902\\OTHER";g_parents[path]={U2W(g_fakeSource.usbInstance),{"OTHER"}};}},
    {"resolver-unavailable",[]{g_parents.erase(U2W(g_fakeSource.interfacePath));}},
 };
 for(const auto& denied:boundRejects)c.run("quiesce/bound-reject/"+denied.first,[&]{
    prepare();denied.second();const auto releases=g_rebindReleaseCalls;lifecycle(12000);
    c.require(g_rebindReleaseCalls==releases && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"unproven bound source cannot be released or consume retry");
    c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"unproven source retains duty");
 });
 std::vector<std::pair<std::string,std::function<void()>>> contextRejects={
    {"new-ON",[]{g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";}},
    {"Host",[]{g_inputHostProcess=100;}},{"prepared",[]{g_inputHostPrepared=true;}},
    {"active",[]{g_inputHostActive=true;}},{"exit",[]{g_exitFence=true;}},
    {"write-gate",[]{g_hardwareGate=false;}},{"armed-route",[]{g_msiDinputRouteDinputArmed=true;}},
    {"foreground-borrow",[]{g_foregroundIsolationBorrowBusy=true;}},
    {"foreground-cycle",[]{g_msiNativeForegroundCyclePending=true;}},
    {"owner",[]{g_msiCrashRecord.ownerPid=222;}},{"other-episode",[]{++g_msiDinputReturnDebtEpisode;}},
    {"ROG",[]{g_board="rog";}},{"GPD",[]{g_board="gpd";}},
 };
 for(const auto& denied:contextRejects)c.run("quiesce/context-reject/"+denied.first,[&]{
    prepare();denied.second();const auto releases=g_rebindReleaseCalls;lifecycle(12000);
    c.require(g_rebindReleaseCalls==releases && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"new target/owner/lease/power/family cannot release rebound source or write");
 });
 c.run("quiesce/ambiguous-initial-never-releases-bound-source",[&]{
    rebindSeed();g_fakeInitialOutcome=disconnected();g_fakeInitialOutcome.write.timedOut=true;lifecycle(10000);rebound();
    const auto releases=g_rebindReleaseCalls;lifecycle(12000);
    c.require(g_rebindReleaseCalls==releases && g_count.writes==0,"forbidden write retry also forbids preparatory source release");
 });
 c.run("quiesce/release-unproven-does-not-spend-retry",[&]{
    prepare();g_rebindReleaseAllowed=false;lifecycle(12000);
    c.require(g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed() && !rebindActualDeviceTargetAbsent(),"failure to release local target cannot authorize mode retry");
    g_rebindReleaseAllowed=true;g_fakeSource=g_msiMainSource;g_msiDinputSourceValid=true;lifecycle(13000);c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"later proven release uses the original retry once");
 });
 std::vector<std::pair<std::string,std::function<void()>>> duringRelease={
    {"power",[]{++g_power;}},{"mode",[]{++g_msiFeedbackModeGeneration;}},
    {"topology",[]{++g_xinputTopologyEpoch;}},{"event",[]{++g_msiOwnedReturnSourceEvent;}},
    {"owner",[]{g_msiCrashRecord.ownerPid=222;}},{"new-ON",[]{g_realStickTest.virtualTargetEnabled=true;}},
    {"borrow",[]{g_foregroundIsolationBorrowBusy=true;}},
 };
 for(const auto& drift:duringRelease)c.run("quiesce/recheck-after-release/"+drift.first,[&]{
    prepare();const auto releases=g_rebindReleaseCalls;g_rebindReleaseHook=drift.second;lifecycle(12000);
    c.require(g_rebindReleaseCalls==releases+1 && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"release boundary drift cannot reach claim or actual write");
    c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"released target does not pay physical duty");
 });
 c.run("availability/same-source-publish-is-not-a-periodic-event",[&]{
    prepare();const auto before=g_msiOwnedReturnSourceEvent.load();
    for(unsigned i=0;i<1000;++i)msiDinputPublishMainSourceIdentity(W2U(kCommandPath),"same-container",0x0db0,0x1902,g_msiDinput.bindCount);
    c.require(g_msiOwnedReturnSourceEvent==before,"identical bound identity never refreshes dormant observation");
 });
 for(const auto& entry:std::vector<std::pair<std::string,std::function<void()>>>{
    {"topology",[]{++g_xinputTopologyEpoch;}},{"source-event",[]{++g_msiOwnedReturnSourceEvent;}},
    {"Host-epoch",[]{++g_fakeHostEpoch;}},
    {"intent-revision",[]{++g_inputHostVirtualIntentRevision;}},
    {"source-revision",[]{++g_msiMainSourceRevision;}}
 })c.run(std::string("claimed-context/before-writer-entry/")+entry.first,[&]{
    prepare();bool injected=false;const auto stores=g_fakeStoreCalls;
    g_logHook=[&](const json& row){if(!injected && row.value("action","")=="retry-claimed"){injected=true;entry.second();}};
    lifecycle(12000);
    c.require(injected && g_msiOwnedReturnRecovery.retryUsed(),"original context changes after claim and before writer entry");
    c.require(g_count.physicalWriteAttempts==0 && g_fakeStoreCalls==stores,"original immutable context prevents stale transport and a new durable claim");
    c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"entry rejection keeps return obligation and spent retry");
 });
 const std::vector<std::pair<std::string,std::function<void()>>> durableEdges={
    {"topology",[]{++g_xinputTopologyEpoch;}},
    {"source-event",[]{++g_msiOwnedReturnSourceEvent;}},
    {"Host-epoch",[]{++g_fakeHostEpoch;}},
    {"intent-revision",[]{++g_inputHostVirtualIntentRevision;}},
    {"source-revision",[]{++g_msiMainSourceRevision;}}
 };
 for(const auto& edge:durableEdges)c.run(std::string("durable-claim/context-edge/")+edge.first,[&]{
    prepare();g_fakeStoreHook=edge.second;lifecycle(12000);
    c.require(!g_fakeStoreHook && g_msiOwnedReturnRecovery.retryUsed(),"edge executes inside actual durable claim after consuming the single retry");
    c.require(g_count.physicalWriteAttempts==0,"changed topology or source event rejects stale mode command before transport");
    c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"context rejection retains both forms of the physical return duty");
    g_tick=13000;rebindActualPumpOpenFragment();lifecycle(14000);
    c.require(g_count.physicalWriteAttempts==0 && g_msiOwnedReturnRecovery.retryUsed(),"later availability cannot refund the consumed command budget");
 });
 c.run("intent-mutation/partial-producer-cannot-be-captured-as-stable",[&]{
    prepare();const auto releases=g_rebindReleaseCalls;
    auto mutation=std::make_unique<InputHostVirtualIntentMutation>();const auto beginRevision=g_inputHostVirtualIntentRevision.load();
    lifecycle(12000);
    c.require(g_inputHostVirtualIntentWriters==1 && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"capturing a newer begin revision does not authorize a partially updated intent");
    c.require(g_rebindReleaseCalls==releases && g_msiOwnedReturnPending,"partial producer retains the source and pending return duty");
    mutation.reset();lifecycle(13000);
    c.require(g_inputHostVirtualIntentWriters==0 && g_inputHostVirtualIntentRevision!=beginRevision && g_count.writes==1,"producer completion may resume the original unused retry under coherent OFF context");
 });
 c.run("intent-mutation/overlapping-producers-stay-busy-until-both-end",[&]{
    prepare();auto first=std::make_unique<InputHostVirtualIntentMutation>();auto second=std::make_unique<InputHostVirtualIntentMutation>();
    first.reset();lifecycle(12000);
    c.require(g_inputHostVirtualIntentWriters==1 && g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"one producer finishing cannot make another active producer look stable");
    second.reset();lifecycle(13000);
    c.require(g_inputHostVirtualIntentWriters==0 && g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"all producers completing leaves only the original single retry");
 });
 c.run("intent-mutation/producer-starts-after-claim",[&]{
    prepare();std::unique_ptr<InputHostVirtualIntentMutation> mutation;
    g_logHook=[&](const json& row){if(!mutation && row.value("action","")=="retry-claimed")mutation=std::make_unique<InputHostVirtualIntentMutation>();};
    lifecycle(12000);
    c.require(mutation && g_inputHostVirtualIntentWriters==1 && g_msiOwnedReturnRecovery.retryUsed(),"producer starts inside the actual post-claim scheduling boundary");
    c.require(g_count.physicalWriteAttempts==0 && g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"active mutation rejects the original command without refunding or settling duty");
    mutation.reset();
 });
 c.run("intent-mutation/complete-producer-during-durable-claim",[&]{
    prepare();g_fakeStoreHook=[]{const InputHostVirtualIntentMutation mutation;};lifecycle(12000);
    c.require(!g_fakeStoreHook && g_inputHostVirtualIntentWriters==0 && g_msiOwnedReturnRecovery.retryUsed(),"a whole mutation may finish before final gate without remaining busy");
    c.require(g_count.physicalWriteAttempts==0 && g_msiDinputReturnOwed,"end revision still revokes a command admitted before that mutation");
 });
 struct Publication {const char* name;std::function<void()> setup;unsigned vendor,product,generation;bool edge;};
 std::vector<Publication> publications={
    {"current-valid",[]{},0x0db0,0x1902,9,true},
    {"no-pending",[]{g_msiOwnedReturnPending=false;},0x0db0,0x1902,9,false},
    {"virtual-ON",[]{g_realStickTest.virtualTargetEnabled=true;},0x0db0,0x1902,9,false},
    {"ROG",[]{g_board="rog";},0x0db0,0x1902,9,false},
    {"GPD",[]{g_board="gpd";},0x0db0,0x1902,9,false},
    {"bad-vendor",[]{},1,0x1902,9,false},
    {"XInput-product",[]{},0x0db0,0x1901,9,false},
    {"generation-unknown",[]{},0x0db0,0x1902,0,false},
    {"root-unresolved",[]{g_parents.erase(kCommandPath);},0x0db0,0x1902,9,false},
 };
 for(const auto& pub:publications)c.run(std::string("availability/publication/")+pub.name,[&]{
    rebindSeed();g_msiOwnedReturnPending=true;pub.setup();const auto event=g_msiOwnedReturnSourceEvent.load();
    msiDinputPublishMainSourceIdentity(W2U(kCommandPath),"fake",pub.vendor,pub.product,pub.generation);
    c.require((g_msiOwnedReturnSourceEvent.load()!=event)==pub.edge,"only a valid MSI OFF pending availability edge can notify");
    c.require(g_count.writes==0 && g_fakeInitialWrites==0,"source publication never writes a mode");
 });
 c.run("late-XInput/no-release-and-no-retry-required",[&]{
    prepare();const auto releases=g_rebindReleaseCalls;returned(1,12000);lifecycle(12000);returned(2,13000);lifecycle(13000);
    c.require(g_rebindReleaseCalls==releases && g_count.writes==0,"valid returned XInput settles without quiescing or mode retry");
    c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending,"two physical reads still settle both duty forms");
 });
 const json report={{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases}};
 std::cout<<report.dump(2)<<'\n';return c.failures.empty()?0:1;
}
