std::shared_ptr<ymcc::steamsession::IdentitySnapshot> published;
const auto beginEnqueue=[&](int presence=1){
 reset();mockRoot=L"S:\\Steam";mockAccount=42;accountReadable=true;offlineFound=true;presenceAnswers={presence};presenceReads=registryReads=0;readSourceHook={};
 reconcileGetReturned=reconcileOwnerSupplied=applySetReturned=applySetOwner=false;reconcileGetCalls=reconcileSettingsWrites=applyBackups=0;reconcileRegistryPid=1234;reconcileAfterGet={};applyAfterSet={};applySleepHook={};applyBackupHook={};applyFileReadHook={};applyLivePersist=true;applyRejectPhaseWrite=false;
 mouseEnqueueQueueCalls=mouseEnqueueBeginWindows=0;mouseEnqueueSaveRejected=false;g_sdmApplyRequested=false;g_sdmEventPending=false;
 published=std::make_shared<ymcc::steamsession::IdentitySnapshot>();published->registryRoot=mockRoot;published->rootReadError=published->accountReadError=published->registeredPidReadError=ERROR_SUCCESS;
 published->presence=presence;published->session={mockRoot,presence==1?1234u:0u,42,presence==1?100ull:0ull};published->registeredPid=1234;published->processEpoch=published->accountEpoch=1;
 fixtureIdentityRead=[&]()->std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>{return published;};
 receipts={{"untouched",true}};
};
std::vector<std::string> names;
const auto test=[&](const char* name,const std::function<void()>& run){
#ifdef YMCC_SDM_ENQUEUE_STALE_ONLY
 if(std::string(name)!="public-source-replaced-during-target-read")return;
#endif
 names.push_back(name);std::cout<<"CASE "<<name<<std::endl;run();
 expect(liveMutations==0&&writes==0&&applyBackups==0,"enqueue-never-performs-live-or-file-write");
};
const auto noQueue=[&]{expect(mouseEnqueueQueueCalls==0&&mouseEnqueueBeginWindows==0&&!g_sdmApplyRequested.load()&&!g_sdmEventPending.load(),"refused-request-does-not-arm-original-queue");};
test("healthy-enqueue-consumes-one-public-source",[&]{beginEnqueue();const auto r=steamDeckMouseSet({{"percent",160}});
 expect(r.value("ok",false)&&receipts.value("pending",false)&&receipts.at("account")==42&&receipts.at("path")==W2U(path)&&receipts.at("baseline")==145&&receipts.at("desired")==160,"original-target-intent-is-persisted");
 expect(reconcileSettingsWrites==1&&mouseEnqueueQueueCalls==1&&mouseEnqueueBeginWindows==1&&g_sdmApplyRequested.load()&&g_sdmEventPending.load(),"intent-save-before-one-original-queue-demand");
 expect(registryReads==0&&presenceReads==0&&sharedSnapshotReads>=2,"enqueue-zero-private-identity-source-read");
});
test("offline-fallback-keeps-original-account",[&]{beginEnqueue(0);published->session.account=0;published->accountReadError=ERROR_NOT_READY;const auto r=steamDeckMouseSet({{"percent",170}});expect(r.value("ok",false)&&receipts.at("account")==42&&mouseEnqueueQueueCalls==1&&registryReads==0&&presenceReads==0,"offline-candidate-fallback-uses-shared-root-only");});
test("unknown-source-cannot-create-intent",[&]{for(const auto* kind:{"no-snapshot","presence","root","marker","account"}){
 beginEnqueue();const auto before=receipts;const std::string k=kind;
 if(k=="no-snapshot")published.reset();else if(k=="presence")published->presence=-1;else if(k=="root")published->rootReadError=ERROR_NOT_READY;else if(k=="marker")published->registeredPidReadError=ERROR_NOT_READY;else published->accountReadError=ERROR_NOT_READY;
 const auto r=steamDeckMouseSet({{"percent",160}});expect(!r.value("ok",true)&&r.at("reason")=="steam-session-starting"&&receipts==before&&reconcileSettingsWrites==0&&registryReads==0&&presenceReads==0,"unknown-is-not-an-offline-fallback-or-intent");noQueue();
}});
test("public-source-replaced-during-target-read",[&]{for(const auto* field:{"pid","birth","account","root"}){
 beginEnqueue();const auto before=receipts;bool changed=false;
 readSourceHook=[&,field]{if(changed)return;changed=true;published=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*published);const std::string v=field;
 if(v=="pid"){published->session.pid=published->registeredPid=9876;published->session.created=101;++published->processEpoch;}
 else if(v=="birth"){++published->session.created;++published->processEpoch;}
 else if(v=="account"){++published->session.account;++published->accountEpoch;}
 else{published->registryRoot=published->session.root=L"T:\\Steam";++published->processEpoch;}};
 const auto r=steamDeckMouseSet({{"percent",160}});expect(changed&&!r.value("ok",true)&&r.at("reason")=="steam-session-changed"&&receipts==before&&reconcileSettingsWrites==0,"stale-target-source-cannot-be-enqueued");noQueue();expect(registryReads==0&&presenceReads==0,"stale-rejection-does-not-start-private-reader");
}});
test("save-failure-does-not-queue",[&]{beginEnqueue();mouseEnqueueSaveRejected=true;const auto before=receipts;const auto r=steamDeckMouseSet({{"percent",160}});expect(!r.value("ok",true)&&r.at("reason")=="settings-write-failed"&&receipts==before&&reconcileSettingsWrites==1,"original-persistence-failure-is-preserved");noQueue();});
test("invalid-request-and-persona-stay-before-source",[&]{for(const auto value:{0,301}){beginEnqueue();const auto r=steamDeckMouseSet({{"percent",value}});expect(!r.value("ok",true)&&r.at("reason")=="invalid-sensitivity"&&sharedSnapshotReads==0&&registryReads==0&&reconcileSettingsWrites==0,"invalid-range-before-source");noQueue();}beginEnqueue();inputConfig["outputTarget"]["persona"]="xbox";const auto r=steamDeckMouseSet({{"percent",160}});expect(!r.value("ok",true)&&r.at("reason")=="steamdeck-not-enabled"&&sharedSnapshotReads==0&&reconcileSettingsWrites==0,"persona-gate-before-source");noQueue();});
std::cout<<"ENQUEUE_IDENTITY_RESULT "<<json{{"cases",names},{"actualCases",names.size()},{"actualProductionEnqueue",true},{"actualSharedTransport",false},{"defaultGroupExecuted",false}}.dump()<<std::endl;
