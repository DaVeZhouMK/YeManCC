const auto beginRead=[](DWORD account,std::vector<int> answers){
 reset();mockRoot=L"S:\\Steam";mockAccount=account;accountReadable=true;offlineFound=true;
 presenceReads=liveReads=registryReads=sharedSnapshotReads=sharedRefreshRequests=0;presenceAnswers=std::move(answers);g_aiFanMockSession.isolated=false;noPublicSnapshot=false;changedEpoch=false;readSourceHook={};readPublishedPresenceOverride=-999;
 fixtureIdentityRead=[]()->std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>{
  if(noPublicSnapshot)return {};
  auto receipt=std::make_shared<ymcc::steamsession::IdentitySnapshot>();const auto answer=readPublishedPresenceOverride==-999?presenceAnswers.front():readPublishedPresenceOverride;
  receipt->registryRoot=mockRoot;receipt->rootReadError=ERROR_SUCCESS;receipt->accountReadError=accountReadable?ERROR_SUCCESS:ERROR_NOT_READY;receipt->registeredPidReadError=ERROR_SUCCESS;
  receipt->presence=answer<0?-1:answer;receipt->session={mockRoot,answer==1?1234u:0u,mockAccount,answer==1?100ull:0ull};receipt->registeredPid=1234;
  receipt->processEpoch=changedEpoch?2:1;receipt->accountEpoch=1;return receipt;
 };
};
beginRead(0,{0});auto enqueued=steamDeckMouseSet({{"percent",160}});
expect(enqueued.at("ok").get<bool>()&&receipts.value("pending",false),"set-prepared-through-original-runtime");
presenceReads=0;presenceAnswers={1};readPublishedPresenceOverride=0;sdmApplyLocked();
expect(presenceReads==1&&writes==0&&liveMutations==0&&receipts.value("pending",false)&&receipts.value("liveReason",std::string{})=="steam-session-starting","final-prewrite-check-rejects-new-steam");
expect(ymcc::steamdeck::desktopLayout(disk[path]).sensitivity==145&&disk.count(path+L".ymcc-bak")==0,"rejected-write-keeps-original-and-no-backup");
beginRead(0,{0});steamDeckMouseSet({{"percent",160}});presenceReads=0;presenceAnswers={0};sdmApplyLocked();
expect(writes==1&&liveMutations==0&&!receipts.value("pending",true)&&ymcc::steamdeck::desktopLayout(disk[path]).sensitivity==160,"offline-write-still-commits-once");


std::cout<<"DIRECTED_SCOPE apply-offline-guards assertions=4 legacy-read-get=NOT_RUN\n";
