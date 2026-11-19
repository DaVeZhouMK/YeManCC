static ULONGLONG readSteamBirth=100;
const auto beginRead=[](DWORD account,std::vector<int> answers){
 reset();readSteamBirth=100;mockRoot=L"S:\\Steam";mockAccount=account;accountReadable=true;offlineFound=true;
 presenceReads=liveReads=registryReads=sharedSnapshotReads=sharedRefreshRequests=0;presenceAnswers=std::move(answers);g_aiFanMockSession.isolated=false;noPublicSnapshot=false;changedEpoch=false;readSourceHook={};readPublishedPresenceOverride=-999;
 fixtureIdentityRead=[]()->std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>{
  if(noPublicSnapshot)return {};
  auto receipt=std::make_shared<ymcc::steamsession::IdentitySnapshot>();const auto answer=readPublishedPresenceOverride==-999?presenceAnswers.front():readPublishedPresenceOverride;
  receipt->registryRoot=mockRoot;receipt->rootReadError=ERROR_SUCCESS;receipt->accountReadError=accountReadable?ERROR_SUCCESS:ERROR_NOT_READY;receipt->registeredPidReadError=ERROR_SUCCESS;
  receipt->presence=answer<0?-1:answer;receipt->session={mockRoot,answer==1?1234u:0u,mockAccount,answer==1?readSteamBirth:0ull};receipt->registeredPid=1234;
  receipt->processEpoch=changedEpoch?2:1;receipt->accountEpoch=1;return receipt;
 };
};
#ifdef YMCC_SDM_READ_PROBE_OWNER_CONTROL
beginRead(42,{1});probeBeforeOwner=[] {++readSteamBirth;};auto state=steamDeckMouseGet();
std::cout<<"PROBE_OWNER_CONTROL_OBSERVATION "<<json{{"discovery",probeDiscoveryReads},{"liveReads",reconcileGetCalls},{"ownerSupplied",probeOwnerSupplied}}.dump()<<'\n';
expect(probeDiscoveryReads==0&&reconcileGetCalls==0&&probeOwnerSupplied,"retired-probe-owner-zero-discovery");
std::cout<<"DIRECTED_SCOPE read-probe-owner-control assertions=1 legacy-read-get=NOT_RUN\n";
#else
beginRead(0,{1});
auto state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1,"missing-account-shares-public-presence");
expect(!state.at("available").get<bool>()&&state.at("steamRunning").get<bool>()&&state.at("reason")=="steam-session-starting","missing-account-starting-preserved");
beginRead(0,{0});state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&state.at("available").get<bool>()&&!state.at("steamRunning").get<bool>()&&state.at("percent")==145,"offline-fallback-public-presence");
beginRead(0,{0,1});state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&state.at("available").get<bool>()&&!state.at("steamRunning").get<bool>(),"same-response-observation-consistent");
presenceAnswers={1};state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=2&&!state.at("available").get<bool>()&&state.at("steamRunning").get<bool>(),"next-get-fresh-after-steam-start");
beginRead(42,{1});state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&state.at("available").get<bool>()&&state.at("steamRunning").get<bool>()&&state.at("liveAvailable").get<bool>(),"normal-account-public-read-and-live-get");
expect(writes==0&&liveMutations==0,"get-does-not-mutate");
expect(probeOwnerSupplied&&probeDiscoveryReads==1&&reconcileGetCalls==1,"healthy-explicit-probe-forwards-pure-outer-owner");
beginRead(42,{0});state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&state.at("available").get<bool>()&&!state.at("steamRunning").get<bool>(),"normal-account-offline-public-read");
for(const int answer:{-1,-2,-3}){
 beginRead(0,{answer});state=steamDeckMouseGet();
 expect(presenceReads==0&&sharedSnapshotReads>=1&&!state.at("available").get<bool>()&&state.at("steamRunning").get<bool>()&&state.at("reason")=="steam-session-starting","unknown-presence-never-offline");
}
beginRead(0,{0});accountReadable=false;state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&state.at("available").get<bool>(),"failed-active-account-read-offline-fallback");
beginRead(0,{0});offlineFound=false;state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&!state.at("available").get<bool>()&&state.at("reason")=="steam-account-not-found","missing-offline-account-preserved");
beginRead(42,{1});mockRoot.clear();state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads>=1&&!state.at("available").get<bool>()&&state.at("reason")=="steam-not-found","missing-root-still-fresh-state");
beginRead(0,{1});g_aiFanMockSession.isolated=true;state=steamDeckMouseGet();
expect(presenceReads==0&&sharedSnapshotReads==0&&state.at("reason")=="isolated-session","isolated-get-zero-presence");g_aiFanMockSession.isolated=false;
beginRead(0,{1});readPublishedPresenceOverride=0;
std::wstring foundPath;DWORD foundAccount=0;std::string foundText,failure;
const auto offlineTarget=g_steamSettingsObserver.identitySnapshot();
expect(sdmTarget(&foundPath,&foundAccount,&foundText,&failure,*offlineTarget,0)&&foundAccount==42&&presenceReads==0&&registryReads==0,"target-uses-public-offline-identity-only");
readPublishedPresenceOverride=1;
const auto runningTarget=g_steamSettingsObserver.identitySnapshot();
expect(!sdmTarget(&foundPath,&foundAccount,&foundText,&failure,*runningTarget,0)&&presenceReads==0&&registryReads==0&&failure=="steam-session-starting","target-refuses-offline-account-on-new-public-presence");
beginRead(0,{0});auto enqueued=steamDeckMouseSet({{"percent",160}});
expect(enqueued.at("ok").get<bool>()&&receipts.value("pending",false),"set-prepared-through-original-runtime");
presenceReads=0;presenceAnswers={1};readPublishedPresenceOverride=0;sdmApplyLocked();
expect(presenceReads==1&&writes==0&&liveMutations==0&&receipts.value("pending",false)&&receipts.value("liveReason",std::string{})=="steam-session-starting","final-prewrite-check-rejects-new-steam");
expect(ymcc::steamdeck::desktopLayout(disk[path]).sensitivity==145&&disk.count(path+L".ymcc-bak")==0,"rejected-write-keeps-original-and-no-backup");
beginRead(0,{0});steamDeckMouseSet({{"percent",160}});presenceReads=0;presenceAnswers={0};sdmApplyLocked();
expect(writes==1&&liveMutations==0&&!receipts.value("pending",true)&&ymcc::steamdeck::desktopLayout(disk[path]).sensitivity==160,"offline-write-still-commits-once");

beginRead(42,{1});noPublicSnapshot=true;state=steamDeckMouseGet();
expect(!state.at("available").get<bool>()&&state.at("reason")=="steam-session-starting"&&presenceReads==0&&registryReads==0&&sharedRefreshRequests==1,"missing-publication-is-unknown-zero-private-read");
beginRead(42,{1});readSourceHook=[] {changedEpoch=true;};state=steamDeckMouseGet();
expect(!state.at("available").get<bool>()&&state.at("reason")=="steam-session-changed"&&presenceReads==0&&registryReads==0&&liveMutations==0&&writes==0,"publication-retired-during-file-read-cannot-return-old-percent");
expect(probeDiscoveryReads==0&&reconcileGetCalls==0,"file-read-retired-owner-zero-controlled-discovery");
beginRead(42,{1});state=sdmStateLocked(false);
expect(state.at("available").get<bool>()&&presenceReads==0&&registryReads==0&&sharedSnapshotReads==2&&probeDiscoveryReads==0&&reconcileGetCalls==0,"healthy-display-two-memory-loads-zero-private-sources");
beginRead(42,{1});probeBeforeOwner=[] {++readSteamBirth;};state=steamDeckMouseGet();
expect(!state.at("available").get<bool>()&&state.at("reason")=="steam-session-changed"&&probeDiscoveryReads==0&&reconcileGetCalls==0&&probeOwnerSupplied,"birth-replaced-before-probe-zero-discovery");
beginRead(42,{1});probeBeforeOwner=[] {mockAccount=43;};state=steamDeckMouseGet();
expect(!state.at("available").get<bool>()&&state.at("reason")=="steam-session-changed"&&probeDiscoveryReads==0&&reconcileGetCalls==0&&probeOwnerSupplied,"account-replaced-before-probe-zero-discovery");
beginRead(42,{1});probeBeforeOwner=[] {mockAccount=43;changedEpoch=true;mockAccount=42;};state=steamDeckMouseGet();
expect(!state.at("available").get<bool>()&&state.at("reason")=="steam-session-changed"&&probeDiscoveryReads==0&&reconcileGetCalls==0&&probeOwnerSupplied,"account-a-b-a-before-probe-zero-discovery");
std::cout<<"DIRECTED_SCOPE read-observation assertions=29 legacy-receipt-storm=NOT_RUN\n";
#endif // original public-read scope or one necessary probe control
