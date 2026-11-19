std::shared_ptr<ymcc::steamsession::IdentitySnapshot> published;
const auto beginApply=[&](int presence=1){
 reset();mockRoot=L"S:\\Steam";mockAccount=42;accountReadable=true;offlineFound=true;presenceAnswers={presence};presenceReads=registryReads=0;readSourceHook={};
 reconcileGetReturned=reconcileOwnerSupplied=applySetReturned=applySetOwner=false;reconcileGetCalls=reconcileSettingsWrites=applyBackups=0;reconcileRegistryPid=1234;reconcileAfterGet={};applyAfterSet={};applySleepHook={};applyBackupHook={};applyFileReadHook={};applyLivePersist=true;applyRejectPhaseWrite=false;
 published=std::make_shared<ymcc::steamsession::IdentitySnapshot>();published->registryRoot=mockRoot;published->rootReadError=published->accountReadError=published->registeredPidReadError=ERROR_SUCCESS;published->presence=presence;published->session={mockRoot,presence==1?1234u:0u,42,presence==1?100ull:0ull};published->registeredPid=1234;published->processEpoch=published->accountEpoch=1;
 fixtureIdentityRead=[&]()->std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>{reconcilePostGetCheckpoint();applyPostSetCheckpoint();return published;};
 receipts={{"account",42u},{"path",W2U(path)},{"group","77"},{"baseline",145},{"desired",160},{"phase","queued"},{"pending",true},{"error",""}};
};
const auto change=[&](const std::string& field){published=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*published);if(field=="pid"){published->session.pid=published->registeredPid=reconcileRegistryPid=9876;published->session.created=101;++published->processEpoch;}else if(field=="birth"){++published->session.created;++published->processEpoch;}else if(field=="account"){++published->session.account;++published->accountEpoch;}else{published->registryRoot=published->session.root=L"T:\\Steam";++published->processEpoch;}};
std::vector<std::string> names;
const auto test=[&](const char* name,const std::function<void()>& run){
#ifdef YMCC_SDM_APPLY_POST_SET_ONLY
 if(std::string(name)!="post-set-pid-change")return;
#endif
 
#ifdef YMCC_SDM_APPLY_TARGET_ONLY
 if(std::string(name)!="post-get-selection-change")return;
#endif

#ifdef YMCC_SDM_APPLY_FINAL_VALUE_ONLY
 if(std::string(name)!="final-selected-read-overrides-previous-persisted-value")return;
#endif
 names.push_back(name);std::cout<<"CASE "<<name<<std::endl;run();
};
test("healthy-single-live-write-pins-original-identity",[&]{beginApply();sdmApplyLocked();expect(liveMutations==1&&applyBackups==1&&receipts.at("phase")=="finished"&&receipts.at("error")==""&&receipts.at("livePid")==1234,"one-live-write-one-backup-original-pid");expect(reconcileOwnerSupplied&&applySetOwner&&registryReads==0&&presenceReads==0&&writes==0,"read-and-write-use-public-identity-without-private-source");});
test("already-desired-does-not-write-again",[&]{beginApply();disk[path]=ymcc::steamdeck::replaceSensitivity(disk[path],160);sdmApplyLocked();expect(liveMutations==0&&applyBackups==0&&receipts.at("livePid")==1234&&!receipts.value("pending",true),"existing-live-value-is-not-replayed");});
for(const auto* field:{"pid","birth","account","root"})test((std::string("post-get-")+field+"-change").c_str(),[&,field]{beginApply();bool changed=false;reconcileAfterGet=[&,field]{changed=true;change(field);};sdmApplyLocked();expect(changed&&liveMutations==0&&applyBackups==0&&receipts.value("pending",false)&&receipts.at("liveReason")=="steam-session-changed","post-get-owner-change-refuses-new-write");expect(registryReads==0&&presenceReads==0,"post-get-refusal-keeps-unique-reader");});
for(const auto* field:{"pid","birth"})test((std::string("post-set-")+field+"-change").c_str(),[&,field]{beginApply();bool changed=false;applyAfterSet=[&,field]{changed=true;change(field);};sdmApplyLocked();expect(changed&&liveMutations==1&&receipts.at("error")=="live-transport-uncertain"&&!receipts.value("pending",true),"post-set-owner-change-must-stay-uncertain");expect(!receipts.contains("livePid")&&!receipts.value("liveAvailable",false)&&registryReads==0&&presenceReads==0,"late-set-cannot-claim-new-pid-or-replay");});
test("readback-sleep-revocation-stops-confirmation",[&]{beginApply();applyLivePersist=false;unsigned sleeps=0;applySleepHook=[&]{++sleeps;change("pid");};sdmApplyLocked();expect(sleeps==1&&liveMutations==1&&receipts.at("error")=="live-transport-uncertain"&&!receipts.contains("livePid"),"original-readback-wait-keeps-original-owner");});
test("phase-save-failure-prevents-live-write",[&]{beginApply();applyRejectPhaseWrite=true;sdmApplyLocked();expect(liveMutations==0&&receipts.at("error")=="settings-write-failed","durable-inflight-failure-cannot-send-set");});
test("unknown-public-source-never-falls-back-to-file",[&]{for(const int p:{-1,1}){beginApply(p);if(p==1)published->registeredPidReadError=ERROR_NOT_READY;sdmApplyLocked();expect(liveMutations==0&&applyBackups==0&&writes==0&&receipts.value("pending",false)&&reconcileGetCalls==0&&registryReads==0&&presenceReads==0,"unknown-source-remains-queued-without-write");}});
test("offline-source-retired-after-backup-blocks-write",[&]{beginApply(0);applyBackupHook=[&]{published=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*published);++published->accountEpoch;};sdmApplyLocked();expect(applyBackups==1&&writes==0&&liveMutations==0&&receipts.value("pending",false),"offline-final-current-check-after-backup");});
for(const auto* phase:{"post-get","after-backup","post-set"})test((std::string(phase)+"-selection-change").c_str(),[&,phase]{beginApply();bool changed=false;
 const auto revoke=[&]{changed=true;disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"0\" } }";};
 if(std::string(phase)=="post-get")reconcileAfterGet=revoke;else if(std::string(phase)=="after-backup")applyBackupHook=revoke;else applyAfterSet=revoke;
 sdmApplyLocked();expect(changed,"selection-interleave-executed");
 if(std::string(phase)=="post-set")expect(liveMutations==1&&receipts.at("error")=="live-transport-uncertain"&&!receipts.contains("livePid"),"post-set-unselected-target-cannot-be-live-confirmed");
 else expect(liveMutations==0&&receipts.at("error")=="desktop-binding-changed","unselected-target-cannot-send-live-write");
});
test("final-selected-read-overrides-previous-persisted-value",[&]{beginApply();unsigned afterSetReads=0;
 applyFileReadHook=[&](const std::wstring& file){if(applySetReturned&&file==path&&++afterSetReads==2)disk[path]=ymcc::steamdeck::replaceSensitivity(disk[path],170);};
 sdmApplyLocked();expect(afterSetReads>=2&&ymcc::steamdeck::desktopLayout(disk[path]).sensitivity==170&&liveMutations==1,"final-actual-selected-file-read-saw-new-scalar");
 expect(receipts.at("error")=="live-file-not-confirmed"&&!receipts.value("pending",true),"stale-persisted-boolean-cannot-clear-final-read-failure");
});
std::cout<<"APPLY_IDENTITY_RESULT "<<json{{"cases",names},{"actualCases",names.size()},{"actualProductionApply",true},{"actualSharedTransport",false},{"defaultGroupExecuted",false}}.dump()<<std::endl;
