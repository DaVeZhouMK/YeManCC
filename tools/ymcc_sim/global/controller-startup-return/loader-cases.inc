static void coldStartupFromBytes(){
 startupSeed();startupFileBytes=g_fakeDurableText;
 // Model only lost volatile state after forced termination. No decoded record
 // is pre-seeded when the real loader runs.
 g_msiCrashRecord={};g_msiCrashLoaded=false;g_msiCrashStoreInvalid=false;g_msiCrashInheritedPending=false;
 startupFileAttributes=0;startupFileError=0;startupReadOk=true;startupReadError=ERROR_ACCESS_DENIED;
 g_msiCrashReadRetryPending=false;g_msiCrashReadAttempts=0;g_msiCrashReadRetryAt=g_msiCrashReadStartedAt=0;
 startupStatCalls=startupReadCalls=startupLegacyCalls=0;
}
static void appendColdStartupCases(Checks& c){
 c.run("startup-loader/cold-OFF-bytes-through-two-fresh-signals",[&]{
  coldStartupFromBytes();msiCrashRecoveryInitialize();
  c.require(startupReadCalls==1&&g_msiCrashLoaded&&!g_msiCrashStoreInvalid,"actual initializer loads bytes with actual strict journal decoder");
  c.require(g_msiCrashInheritedPending&&g_msiCrashRecord.ownerPid==30000,"loaded old-owner duty is published while virtual target remains OFF");
  lifecycle(10000);
  c.require(!g_realStickTest.virtualTargetEnabled&&g_persona=="disabled"&&!g_inputHostProcess,"ordinary OFF startup never creates or needs InputHost");
  c.require(g_msiDinputReturnDebtEpisode==1&&g_fakeInitialWrites==1&&g_msiOwnedReturnPending,"actual loader responsibility reaches actual owner adoption and initial return");
  returned(1,11000);lifecycle(11000);
  c.require(g_msiDinputReturnOwed&&g_msiCrashRecord.modeReturnPending,"first current matching signal does not settle loaded obligation");
  returned(2,12000);lifecycle(12000);lifecycle(13000);
  c.require(!g_msiDinputReturnOwed&&!g_msiCrashRecord.modeReturnPending&&!g_msiCrashInheritedPending,"second fresh signal and durable commit retire loaded startup duty");
 });
 c.run("startup-loader/already-XInput-loads-and-observes-without-mode-write",[&]{
  coldStartupFromBytes();g_msiDinputSourceValid=false;g_fakeSource.valid=false;returned(1,10000);
  msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(g_msiOwnedReturnPending&&g_msiDinputReturnOwed,"loaded already-XInput duty still starts the owned signal observer");
  c.require(g_fakeInitialWrites==0&&g_count.writes==0,"already-XInput startup issues no mode write");
  returned(2,11000);lifecycle(11000);lifecycle(12000);
  c.require(!g_msiCrashRecord.modeReturnPending&&!g_msiCrashInheritedPending,"current matching read windows settle the loaded already-XInput duty");
 });
 c.run("startup-loader/missing-file-does-not-claim-hidden-list",[&]{
  coldStartupFromBytes();startupFileAttributes=INVALID_FILE_ATTRIBUTES;startupFileError=ERROR_FILE_NOT_FOUND;
  msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(startupLegacyCalls==1&&startupReadCalls==0&&!g_msiCrashInheritedPending,"missing file plus absent legacy proof creates no responsibility");
  c.require(g_fakeUnhides==0&&g_fakeInitialWrites==0,"existing hidden paths cannot manufacture ownership");
 });
 for(const std::string kind:{"malformed","duplicate-key","foreign-board"})c.run("startup-loader/reject/"+kind,[&]{
  coldStartupFromBytes();
  if(kind=="malformed")startupFileBytes="{not json";
  if(kind=="duplicate-key")startupFileBytes="{\"schema\":1,"+startupFileBytes.substr(1);
  if(kind=="foreign-board"){auto record=msiRecovery::decodeRecord(startupFileBytes).record;record.boardKey="OTHER|BOARD|SYSTEM|CLASS";startupFileBytes=msiRecovery::encodeRecord(record);}
  const auto original=startupFileBytes;msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(g_msiCrashLoaded&&g_msiCrashStoreInvalid&&!g_msiCrashInheritedPending,"actual loader rejects malformed, duplicate or wrong-board persisted record");
  c.require(startupFileBytes==original&&g_fakeStoreCalls==0,"invalid ownership bytes are neither rewritten nor cleared");
  c.require(g_fakeInitialWrites==0&&g_fakeUnhides==0,"invalid load cannot mutate mode or visibility");
 });
 c.run("startup-loader/read-error-remains-invalid-after-IO-recovers",[&]{
  coldStartupFromBytes();startupReadOk=false;msiCrashRecoveryInitialize();
  c.require(g_msiCrashLoaded&&g_msiCrashStoreInvalid&&startupReadCalls==1,"read failure sets loaded and permanently invalid in this process");
  startupReadOk=true;msiCrashRecoveryInitialize();for(ULONGLONG t=10000;t<=15000;t+=1000)lifecycle(t);
  c.require(startupReadCalls==1&&g_msiCrashStoreInvalid&&!g_msiCrashInheritedPending,"actual initializer does not retry the recovered file because loaded was already set");
  c.require(g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"read failure retains bytes and grants no unproven restore authority");
 });
 c.run("startup-loader/access-denied-retains-file-without-ownership",[&]{
  coldStartupFromBytes();startupFileAttributes=INVALID_FILE_ATTRIBUTES;startupFileError=ERROR_ACCESS_DENIED;
  msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(g_msiCrashStoreInvalid&&startupReadCalls==0&&startupLegacyCalls==0,"access denied cannot masquerade as a missing file or permit legacy takeover");
  c.require(g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"permission failure does not erase responsibility bytes or execute restore");
 });
 c.run("startup-loader/owner-alive-or-unknown-retains-loaded-duty",[&]{
  coldStartupFromBytes();g_fakeOldOwnerDead=false;msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(g_msiCrashInheritedPending&&g_msiCrashRecord.ownerPid==30000,"unknown or living owner keeps the loaded obligation with its old identity");
  c.require(g_msiDinputReturnDebtEpisode==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"owner liveness must be positively qualified before adoption");
 });
 c.run("startup-loader/failed-owner-adoption-retains-original-bytes",[&]{
  coldStartupFromBytes();const auto old=startupFileBytes;msiCrashRecoveryInitialize();g_fakeStoreOk=false;lifecycle(10000);
  c.require(g_msiCrashInheritedPending&&g_msiCrashRecord.ownerPid==30000&&g_fakeDurableText==old,"failed owner persistence retains the exact original durable owner and pending responsibility");
  c.require(g_msiDinputReturnDebtEpisode==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"failed write-ahead adoption cannot allocate authority or mutate the device");
 });
 c.run("startup-loader/power-gate-loads-duty-then-defers-consumption",[&]{
  coldStartupFromBytes();g_hardwareGate=false;msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(g_msiCrashInheritedPending&&startupReadCalls==1,"load records the obligation even when hardware-write permission is closed");
  c.require(g_fakeInitialWrites==0&&g_fakeUnhides==0,"closed permission gates block only action, not retained responsibility");
  g_hardwareGate=true;lifecycle(11000);
  c.require(g_msiOwnedReturnPending&&g_fakeInitialWrites==1,"existing serial maintenance consumes the same loaded duty after permission returns");
 });
 c.run("startup-loader/transient-sharing-failure-retries-on-existing-maintenance",[&]{
  coldStartupFromBytes();startupFileAttributes=INVALID_FILE_ATTRIBUTES;startupFileError=32;
  msiCrashRecoveryInitialize();
  c.require(!g_msiCrashLoaded&&g_msiCrashStoreInvalid&&!g_msiCrashInheritedPending,"sharing failure retains unknown authority without permanently marking loaded");
  startupFileAttributes=0;startupFileError=0;
  for(ULONGLONG t=10000;t<=15000;t+=1000)lifecycle(t);
  c.require(startupStatCalls>=2&&startupReadCalls==1&&g_msiCrashLoaded&&!g_msiCrashStoreInvalid,"existing serial maintenance reloads the same record after transient IO recovers");
  c.require(g_msiDinputReturnOwed&&g_msiOwnedReturnPending&&g_fakeInitialWrites==1,"recovered exact old-owner record reaches actual OFF return adoption without user toggling");
 });
 for(const DWORD error:{ERROR_SHARING_VIOLATION,ERROR_LOCK_VIOLATION,ERROR_BUSY,ERROR_RETRY,ERROR_NOT_READY,ERROR_IO_DEVICE,ERROR_DEVICE_NOT_CONNECTED})c.run("startup-loader/transient-read/"+std::to_string(error),[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=error;const auto bytes=startupFileBytes;msiCrashRecoveryInitialize();
  c.require(!g_msiCrashLoaded&&g_msiCrashStoreInvalid&&g_msiCrashReadRetryPending&&startupReadCalls==1,"retryable read retains unknown responsibility and blocks mutation");
  for(unsigned i=0;i<100;++i)msiCrashRecoveryInitialize();lifecycle(10999);
  c.require(startupReadCalls==1&&g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"repeated callers before due neither spin-read nor mutate hardware or record");
  startupReadOk=true;lifecycle(11000);
  c.require(startupReadCalls==2&&g_msiCrashLoaded&&!g_msiCrashStoreInvalid&&!g_msiCrashReadRetryPending,"single due retry validates actual parser and terminates retry episode");
  c.require(g_msiOwnedReturnPending&&g_msiDinputReturnOwed&&g_fakeInitialWrites==1,"reloaded owned record enters existing actual return adoption");
  c.require(startupFileBytes==bytes,"retry never rewrites original read bytes");
 });
 c.run("startup-loader/transient-read-budget-stops-at-five",[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=ERROR_SHARING_VIOLATION;const auto bytes=startupFileBytes;msiCrashRecoveryInitialize();
  for(const ULONGLONG t:{11000ULL,13000ULL,17000ULL,25000ULL})lifecycle(t);
  c.require(g_msiCrashReadAttempts==5&&startupReadCalls==5&&!g_msiCrashReadRetryPending&&g_msiCrashLoaded&&g_msiCrashStoreInvalid,"persistent IO stops after five bounded attempts retaining blocked authority");
  startupReadOk=true;for(ULONGLONG t=26000;t<=120000;t+=1000)lifecycle(t);
  c.require(startupReadCalls==5&&g_msiCrashStoreInvalid,"exhausted episode never retries forever after IO becomes readable");
  c.require(startupFileBytes==bytes&&g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"exhausted read preserves bytes and never invents return ownership");
 });
 c.run("startup-loader/transient-read-time-budget-stops-at-thirty-seconds",[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=ERROR_RETRY;msiCrashRecoveryInitialize();startupReadOk=true;lifecycle(40000);
  c.require(startupReadCalls==1&&g_msiCrashLoaded&&g_msiCrashStoreInvalid&&!g_msiCrashReadRetryPending,"delayed maintenance at30second ceiling does not silently renew read budget");
  c.require(g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"read time ceiling retains unknown responsibility without hardware work");
 });
 for(const std::string gate:{"foreign-family","exit-fence","suspend"})c.run("startup-loader/retry-gate/"+gate,[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=ERROR_LOCK_VIOLATION;msiCrashRecoveryInitialize();startupReadOk=true;
  if(gate=="foreign-family")g_board="rog";else if(gate=="exit-fence")g_exitFence=true;else g_powerLifecycle=PowerLifecycle::Suspended;
  lifecycle(11000);c.require(startupReadCalls==1&&g_msiCrashReadRetryPending,"existing readiness/exit/family gate defers retry without losing responsibility");
  g_board="msi";g_exitFence=false;g_powerLifecycle=PowerLifecycle::Ready;lifecycle(12000);
  c.require(startupReadCalls==2&&g_msiCrashLoaded&&!g_msiCrashStoreInvalid,"original retry episode resumes once the owning lifecycle is ready");
 });
 c.run("startup-loader/read-can-recover-while-hardware-write-gate-remains-closed",[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=ERROR_RETRY;msiCrashRecoveryInitialize();startupReadOk=true;g_hardwareGate=false;lifecycle(11000);
  c.require(g_msiCrashLoaded&&!g_msiCrashStoreInvalid&&g_msiCrashInheritedPending,"read recovery establishes proven pending ownership while write gate closed");
  c.require(g_fakeInitialWrites==0&&g_fakeUnhides==0,"read success does not bypass separate hardware-write permission");
  g_hardwareGate=true;lifecycle(12000);c.require(g_msiOwnedReturnPending&&g_fakeInitialWrites==1,"same duty reaches existing writer only after write gate opens");
 });
 c.run("startup-loader/missing-during-retry-never-enters-legacy-migration",[&]{
  coldStartupFromBytes();startupFileAttributes=INVALID_FILE_ATTRIBUTES;startupFileError=ERROR_SHARING_VIOLATION;msiCrashRecoveryInitialize();startupFileError=ERROR_FILE_NOT_FOUND;lifecycle(11000);
  c.require(!g_msiCrashLoaded&&g_msiCrashStoreInvalid&&g_msiCrashReadRetryPending&&startupLegacyCalls==0,"missing after a failed owned read is unknown debt, never an empty/legacy ownership claim");
  startupFileAttributes=0;startupFileError=0;lifecycle(13000);c.require(startupReadCalls==1&&g_msiOwnedReturnPending&&g_fakeInitialWrites==1,"returning same record is revalidated before adoption");
 });
 c.run("startup-loader/transient-then-foreign-record-is-terminal",[&]{
  coldStartupFromBytes();startupReadOk=false;startupReadError=ERROR_RETRY;msiCrashRecoveryInitialize();startupReadOk=true;auto record=msiRecovery::decodeRecord(startupFileBytes).record;record.boardKey="OTHER|BOARD|SYSTEM|CLASS";startupFileBytes=msiRecovery::encodeRecord(record);const auto bytes=startupFileBytes;lifecycle(11000);
  c.require(g_msiCrashLoaded&&g_msiCrashStoreInvalid&&!g_msiCrashReadRetryPending&&!g_msiCrashInheritedPending,"a transient IO failure cannot weaken board validation on retry");
  c.require(g_msiCrashRecord.boardKey.empty()&&startupFileBytes==bytes&&g_fakeStoreCalls==0&&g_fakeInitialWrites==0&&g_fakeUnhides==0,"foreign candidate never overwrites in-memory authority or original file");
 });
 c.run("startup-loader/non-MSI-initializer-does-not-read-or-claim",[&]{
  coldStartupFromBytes();g_board="other";msiCrashRecoveryInitialize();
  c.require(startupStatCalls==0&&startupReadCalls==0&&!g_msiCrashLoaded&&!g_msiCrashInheritedPending,"actual initializer does not enter MSI journal handling on foreign board");
 });
 c.run("startup-loader/clean-record-has-no-pending-action",[&]{
  coldStartupFromBytes();auto record=msiRecovery::decodeRecord(startupFileBytes).record;record.modeReturnPending=false;record.hiddenPaths.clear();startupFileBytes=msiRecovery::encodeRecord(record);
  msiCrashRecoveryInitialize();lifecycle(10000);
  c.require(startupReadCalls==1&&!g_msiCrashStoreInvalid&&!g_msiCrashInheritedPending,"valid fully settled record loads without inventing a new return duty");
  c.require(g_fakeInitialWrites==0&&g_fakeUnhides==0,"clean OFF startup performs no return action");
 });
}
