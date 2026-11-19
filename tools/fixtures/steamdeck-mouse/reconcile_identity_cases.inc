std::shared_ptr<ymcc::steamsession::IdentitySnapshot> published;
const auto beginReconcile=[&](int presence=1){
 reset();mockRoot=L"S:\\Steam";mockAccount=42;accountReadable=true;offlineFound=true;presenceAnswers={presence};presenceReads=registryReads=0;readSourceHook={};
 reconcileGetReturned=reconcileOwnerSupplied=false;reconcileGetCalls=reconcileSettingsWrites=0;reconcileRegistryPid=1234;reconcileAfterGet={};
 published=std::make_shared<ymcc::steamsession::IdentitySnapshot>();published->registryRoot=mockRoot;published->rootReadError=published->accountReadError=published->registeredPidReadError=ERROR_SUCCESS;
 published->presence=presence;published->session={mockRoot,presence==1?1234u:0u,42,presence==1?100ull:0ull};published->registeredPid=1234;published->processEpoch=published->accountEpoch=1;
 fixtureIdentityRead=[&]()->std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>{reconcilePostGetCheckpoint();return published;};
 receipts={{"account",42u},{"path",W2U(path)},{"group","77"},{"desired",145},{"phase","live-inflight"},{"pending",true},{"error","live-transport-uncertain"}};
};
unsigned actualCases=0;std::vector<std::string> actualNames;
const auto test=[&](const char* name,const std::function<void()>& run){
#ifdef YMCC_SDM_RECONCILE_AFTER_GET_ONLY
 if(std::string(name)!="pid-changed-after-get-return")return;
#endif
#ifdef YMCC_SDM_RECONCILE_TARGET_ONLY
 if(std::string(name)!="selector-changed-after-get")return;
#endif
 ++actualCases;actualNames.push_back(name);std::cout<<"CASE "<<name<<std::endl;run();
 expect(writes==0&&liveMutations==0,"reconcile-never-writes-vdf-or-replays-set");
};
test("healthy-pins-original-public-pid",[&]{beginReconcile();expect(sdmReconcileLocked(receipts),"healthy-original-tuple-confirmed");expect(receipts.at("livePid")==1234&&receipts.at("liveAvailable")==true&&reconcileOwnerSupplied&&reconcileGetCalls==1,"healthy-held-pid-and-pure-owner-forwarded");expect(registryReads==0&&presenceReads==0,"healthy-reconcile-zero-private-registry-or-process-inventory");});
for(const auto* variant:{"pid-changed-after-get-return","birth-changed-after-get-return","account-changed-after-get-return","root-changed-after-get-return","registry-marker-unreadable-after-get","file-changed-after-get","selector-changed-after-get","library-target-ambiguous-after-get"}){
 test(variant,[&,variant]{beginReconcile();const auto before=receipts;bool post=false;
  reconcileAfterGet=[&,variant]{post=true;published=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*published);const std::string v=variant;
   if(v=="pid-changed-after-get-return"){published->session.pid=published->registeredPid=reconcileRegistryPid=9876;published->session.created=101;++published->processEpoch;}
   else if(v=="birth-changed-after-get-return"){++published->session.created;++published->processEpoch;}
   else if(v=="account-changed-after-get-return"){++published->session.account;++published->accountEpoch;}
   else if(v=="root-changed-after-get-return"){published->session.root=published->registryRoot=L"T:\\Steam";++published->processEpoch;}
   else if(v=="registry-marker-unreadable-after-get")published->registeredPidReadError=ERROR_NOT_READY;
   else if(v=="selector-changed-after-get")disk[selection]="\"controller_config\" { \"413080\" { \"autosave\" \"0\" } }";
   else if(v=="library-target-ambiguous-after-get"){
    disk[mockRoot+L"\\steamapps\\libraryfolders.vdf"]=R"VDF("libraryfolders" { "1" { "path" "T:\\Steam" } })VDF";
    disk[L"T:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\configset_controller_neptune.vdf"]=disk[selection];
    disk[L"T:\\Steam\\steamapps\\common\\Steam Controller Configs\\42\\config\\413080\\controller_neptune.vdf"]=disk[path];
   }
   else disk[path]=ymcc::steamdeck::replaceSensitivity(disk[path],160);
  };
  expect(!sdmReconcileLocked(receipts),"post-get-replacement-must-not-be-confirmed");expect(post&&reconcileGetReturned&&reconcileGetCalls==1&&reconcileSettingsWrites==0&&receipts==before,"real-post-return-window-no-confirmed-receipt");expect(registryReads==0&&presenceReads==0,"post-return-rejection-keeps-unique-public-reader");
 });
}
test("unknown-is-not-offline",[&]{for(const int p:{-1,1}){beginReconcile(p);if(p==1)published->registeredPidReadError=ERROR_NOT_READY;const auto before=receipts;expect(!sdmReconcileLocked(receipts)&&receipts==before&&reconcileSettingsWrites==0&&reconcileGetCalls==0&&registryReads==0&&presenceReads==0,"unknown-or-unqualified-source-refused-before-private-read");}});
test("offline-readback-does-not-carry-a-live-pid",[&]{beginReconcile(0);published->session.account=0;published->accountReadError=ERROR_NOT_READY;expect(sdmReconcileLocked(receipts),"stable-offline-fallback-readback-confirmed");expect(receipts.at("livePid")==0&&receipts.at("liveAvailable")==false&&reconcileGetCalls==0&&registryReads==0&&presenceReads==0,"offline-confirmation-zero-pid-without-live-query");});
std::cout<<"RECONCILE_IDENTITY_RESULT "<<json{{"cases",actualNames},{"actualCases",actualCases},{"actualProductionReconcile",true},{"controlledOsStorageTransport",true},{"defaultGroupExecuted",false},{"physicalInputProven",false}}.dump()<<std::endl;
