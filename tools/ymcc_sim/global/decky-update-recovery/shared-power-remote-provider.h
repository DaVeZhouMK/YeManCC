#include "sleep_power_remote_provider.h"
static ymcc::sleeppower::remote::SettlementV1 returnedReceipt(const ymcc::sleeppower::remote::DescriptorV1& binding){
 using namespace ymcc::sleeppower::remote;SettlementV1 value;
 value.scope=binding.scope;value.request=binding.request;value.provider=binding.provider;value.consumer=binding.consumer;
 value.outcome=SettlementOutcome::Returned;value.flags=AllReturned;return value;
}
static void remoteProviderRequestControl(){
 using namespace ymcc::sleeppower::remote;
 ProcessHold peerHold;require(peerHold.bind(GetCurrentProcess()),"hold-actual-authenticated-self-peer");const HANDLE authenticatedPeer=peerHold.get();
 NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);
 auto scope=provider.capture(authenticatedPeer,{19,1},Purpose::FanResume,[](Purpose){return true;});
 require(bool(scope),"request-control-captures-original-native-scope");if(!scope)return;
 auto forged=returnedReceipt(scope->binding());forged.request.low++;forged.outcome=SettlementOutcome::Unknown;forged.flags=0;
 require(!provider.settle(authenticatedPeer,forged),"foreign-request-settlement-must-not-change-ledger");
 require(provider.outstanding()==1&&scope->current(authenticatedPeer,Purpose::FanResume),"foreign-request-cannot-retire-or-cancel-live-owner");
 require(provider.settle(authenticatedPeer,returnedReceipt(scope->binding()))&&provider.outstanding()==0,"control-returns-exact-original-owner");
}
static void remoteProviderLifecycle(){
 using namespace ymcc::sleeppower;using namespace ymcc::sleeppower::remote;
 ProcessHold peerHold;require(peerHold.bind(GetCurrentProcess()),"hold-actual-authenticated-self-peer");const HANDLE authenticatedPeer=peerHold.get();
 DWORD handlesBefore=0,handlesAfter=0;require(GetProcessHandleCount(authenticatedPeer,&handlesBefore)!=FALSE,"read-private-handle-baseline");
 require(sizeof(DescriptorV1)==128&&sizeof(SettlementV1)==80&&sizeof(SharedPowerV1)==48,"native-remote-candidate-layout-is-128-80-48");
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);bool intent=true;
  auto scope=provider.capture(authenticatedPeer,{1,9},Purpose::FanResume,[&](Purpose p){return intent&&p==Purpose::FanResume;});
  require(bool(scope),"native-provider-captures-exact-owned-consumer");if(!scope)return;
  auto binding=scope->binding();ProcessIdentityV1 identity;require(processIdentity(authenticatedPeer,identity)&&sameProcess(binding.consumer,identity)&&sameProcess(binding.provider,identity),"descriptor-uses-kernel-held-process-identities");
  require(provider.outstanding()==1&&provider.current(binding.scope,authenticatedPeer,Purpose::FanResume),"accepted-scope-remains-current-in-native-ledger");
  require(!provider.current(binding.scope,nullptr,Purpose::FanResume)&&provider.current(binding.scope,authenticatedPeer,Purpose::FanResume),"bad-peer-cannot-consume-or-cancel-another-owner");
  require(!scope->current(authenticatedPeer,Purpose::FanWrite)&&scope->current(authenticatedPeer,Purpose::FanResume),"host-cannot-upgrade-exact-purpose-by-changing-enum");
  g_sharedPowerPublicationRevision.fetch_add(1);SharedPowerV1 current;
  require(provider.read(binding.scope,authenticatedPeer,current)&&current.publicationRevision>binding.captured.publicationRevision&&provider.current(binding.scope,authenticatedPeer,Purpose::FanResume),"publication-only-uses-fresh-native-reader-without-revocation");
  auto forged=returnedReceipt(binding);forged.request.low++;
  require(!provider.settle(authenticatedPeer,forged)&&provider.outstanding()==1,"foreign-request-receipt-rejected");
  forged=returnedReceipt(binding);forged.consumer.birth100ns++;
  require(!provider.settle(authenticatedPeer,forged),"foreign-consumer-birth-receipt-rejected");
  forged=returnedReceipt(binding);forged.provider.birth100ns++;
  require(!provider.settle(authenticatedPeer,forged),"foreign-provider-birth-receipt-rejected");
  forged=returnedReceipt(binding);forged.flags=WorkerStopped;
  require(!provider.settle(authenticatedPeer,forged),"partial-return-cannot-retire-source-registration");
  forged=returnedReceipt(binding);forged.outcome=SettlementOutcome::RetainedResources;forged.flags=WorkerStopped;
  require(provider.settle(authenticatedPeer,forged)&&provider.outstanding()==1&&scope->lastSettlement()==SettlementOutcome::RetainedResources,"honest-retained-sdk-debt-stays-in-original-ledger");
  SyncDuplicate borrower(scope->cancellationEvent());intent=false;
  require(!scope->current(authenticatedPeer,Purpose::FanResume)&&WaitForSingleObject(borrower.handle,0)==WAIT_OBJECT_0,"owner-withdrawal-signals-but-keeps-accepted-borrower");
  intent=true;require(!scope->current(authenticatedPeer,Purpose::FanResume)&&provider.outstanding()==1,"new-valid-intent-never-revives-revoked-scope");
  scope.reset();require(provider.outstanding()==1,"request-object-return-does-not-forget-accepted-work");
  g_exitRequested=true;require(provider.settle(authenticatedPeer,returnedReceipt(binding))&&provider.outstanding()==0,"actual-return-ack-is-allowed-after-power-becomes-unknown");g_exitRequested=false;
  require(WaitForSingleObject(borrower.handle,0)==WAIT_OBJECT_0,"old-duplicate-stays-signaled-after-registration-return");
 }
 reset();
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);const auto eventsBefore=nativeEventsCreated.load();
  const auto pending=g_sleepPowerDispatch.window(PBT_APMSUSPEND,++serial);
  require(!provider.capture(authenticatedPeer,{2,9},Purpose::FanResume,[](Purpose){return true;})&&nativeEventsCreated==eventsBefore,"pending-never-issues-new-source-event-or-scope");
  g_sleepPowerDispatch.complete(pending,++serial);
 }
 reset();
 {
  NativeProvider provider(&unknownReader,g_sharedPowerRevocations);
  require(!provider.capture(authenticatedPeer,{3,9},Purpose::FanResume,[](Purpose){return true;}),"unknown-reader-never-issues-a-fan-permit");
 }
 reset();
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
  SharedPowerV1 strict;require(!captureSharedPowerLifecycle(strict),"fixture-has-real-nonready-public-reader-state");
  auto root=provider.capture(authenticatedPeer,{4,9},Purpose::FanResume,[](Purpose p){return p==Purpose::FanResume;});
  require(bool(root)&&root->current(authenticatedPeer,Purpose::FanResume),"native-issued-f5-purpose-retains-valid-resuming-admission");if(!root)return;
  require(!provider.handoffToFanWrite(root->binding().scope,authenticatedPeer,[](Purpose){return false;})&&root->current(authenticatedPeer,Purpose::FanResume),"write-purpose-must-be-separately-admitted-by-native-owner");
  auto child=provider.handoffToFanWrite(root->binding().scope,authenticatedPeer,[](Purpose p){return p==Purpose::FanWrite;});
  require(bool(child),"native-owner-issues-fanwrite-child-before-any-upgrade");if(!child)return;
  require(sameKey(root->binding().request,child->binding().request)&&!sameKey(root->binding().scope,child->binding().scope)&&samePowerObservation(root->binding().captured,child->binding().captured),"child-keeps-request-and-exact-public-cohort-with-new-identity");
  require(child->current(authenticatedPeer,Purpose::FanWrite)&&!child->current(authenticatedPeer,Purpose::FanResume),"write-child-has-exact-purpose-and-original-source");
  require(!provider.handoffToFanWrite(root->binding().scope,authenticatedPeer,[](Purpose){return true;})&&provider.outstanding()==2,"same-parent-cannot-mint-a-second-control-child");
  SyncDuplicate rootWait(root->cancellationEvent()),childWait(child->cancellationEvent());root->cancel();
  require(WaitForSingleObject(rootWait.handle,0)==WAIT_OBJECT_0&&WaitForSingleObject(childWait.handle,0)==WAIT_OBJECT_0&&!child->current(authenticatedPeer,Purpose::FanWrite),"parent-cancel-synchronously-signals-both-borrowed-events");
  require(!provider.settle(authenticatedPeer,returnedReceipt(root->binding()))&&provider.outstanding()==2,"parent-return-cannot-hide-unreturned-control-child");
  require(provider.settle(authenticatedPeer,returnedReceipt(child->binding()))&&provider.outstanding()==1,"actual-child-return-retires-only-child-ledger-entry");
  require(provider.settle(authenticatedPeer,returnedReceipt(root->binding()))&&provider.outstanding()==0,"parent-returns-only-after-its-control-borrower");
 }
 reset();
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);g_powerLifecycle=PowerLifecycle::Resuming;g_hardwareWriteGate=false;
  auto old=provider.capture(authenticatedPeer,{5,9},Purpose::FanResume,[](Purpose){return true;});require(bool(old),"prepare-old-precommit-scope");if(!old)return;
  g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
  require(!provider.handoffToFanWrite(old->binding().scope,authenticatedPeer,[](Purpose){return true;})&&provider.outstanding()==1,"later-ready-cannot-upgrade-a-revoked-precommit-scope");
  require(WaitForSingleObject(old->cancellationEvent(),0)==WAIT_OBJECT_0,"actual-cohort-change-sticks-local-cancellation");
  require(provider.settle(authenticatedPeer,returnedReceipt(old->binding())),"old-operation-returns-without-replaying-new-ready");
 }
 for(const std::string change:{"generation","serial","gate","unknown","publication-regressed","native-notification"}){
  reset();NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations);
  auto old=provider.capture(authenticatedPeer,{6,9},Purpose::FanResume,[](Purpose){return true;});require(bool(old),"capture-scope-for-exact-source-boundary");if(!old)return;
  if(change=="generation")g_powerGeneration.store(8);if(change=="serial")g_sharedPowerSuspendSerial.store(3);if(change=="gate")g_hardwareWriteGate=false;
  if(change=="unknown")g_exitRequested=true;if(change=="publication-regressed")g_sharedPowerPublicationRevision.store(1);
  if(change=="native-notification"){const auto receipt=g_sleepPowerDispatch.window(PBT_APMRESUMEAUTOMATIC,++serial);g_sleepPowerDispatch.complete(receipt,++serial);}
  require(!provider.current(old->binding().scope,authenticatedPeer,Purpose::FanResume)&&WaitForSingleObject(old->cancellationEvent(),0)==WAIT_OBJECT_0,"source-change-cancels-exact-old-accepted-scope");
  g_powerGeneration.store(7);g_sharedPowerSuspendSerial.store(2);g_hardwareWriteGate=true;g_exitRequested=false;g_sharedPowerPublicationRevision.store(3);
  require(!old->current(authenticatedPeer,Purpose::FanResume),"restored-facts-never-resurrect-previously-canceled-scope");
  require(provider.settle(authenticatedPeer,returnedReceipt(old->binding())),"changed-source-still-allows-owned-resource-settlement");
 }
 reset();
 {
  NativeProvider provider(&readSharedSystemPower,g_sharedPowerRevocations,1);
  auto old=provider.capture(authenticatedPeer,{7,9},Purpose::FanResume,[](Purpose){return true;});require(bool(old),"capture-bounded-ledger-entry");if(!old)return;
  require(!provider.capture(authenticatedPeer,{8,9},Purpose::FanResume,[](Purpose){return true;}),"outstanding-resource-limit-does-not-drop-or-refund-old-work");
  const auto oldKey=old->binding().scope;provider.close();
  require(!old->current(authenticatedPeer,Purpose::FanResume)&&WaitForSingleObject(old->cancellationEvent(),0)==WAIT_OBJECT_0&&provider.outstanding()==1,"provider-close-cancels-and-retains-unreturned-ledger");
  require(!provider.capture(authenticatedPeer,{8,9},Purpose::FanResume,[](Purpose){return true;}),"closed-provider-does-not-create-a-new-scope");
  require(provider.settle(authenticatedPeer,returnedReceipt(old->binding())),"closed-provider-can-receive-real-owned-settlement");
  NativeProvider fresh(&readSharedSystemPower,g_sharedPowerRevocations);
  auto next=fresh.capture(authenticatedPeer,{9,9},Purpose::FanResume,[](Purpose){return true;});require(bool(next)&&!sameKey(oldKey,next->binding().scope),"provider-recreation-cannot-reuse-previous-scope-identity");if(next)require(fresh.settle(authenticatedPeer,returnedReceipt(next->binding())),"fresh-original-owner-returns-its-own-new-scope");
 }
 reset();
 require(GetProcessHandleCount(authenticatedPeer,&handlesAfter)!=FALSE&&handlesAfter==handlesBefore,"all-source-events-held-processes-and-private-duplicates-returned");
}
static void remoteProviderNativeAccessor(){
 using namespace ymcc::sleeppower::remote;
 ProcessHold peer;require(peer.bind(GetCurrentProcess()),"native-accessor-holds-real-authenticated-peer");
 DWORD handlesBefore=0,handlesAfter=0;require(GetProcessHandleCount(peer.get(),&handlesBefore)!=FALSE,"native-accessor-private-handle-baseline");
 auto& provider=sharedNativeRemotePowerProvider();
 require(&provider==&sharedNativeRemotePowerProvider(),"actual-native-entry-keeps-one-provider-bound-to-original-registry");
 auto scope=provider.capture(peer.get(),{31,1},Purpose::FanResume,[](Purpose p){return p==Purpose::FanResume;});
 require(bool(scope),"actual-native-accessor-captures-using-original-reader");if(!scope)return;
 ymcc::sleeppower::SharedPowerV1 value;
 require(provider.read(scope->binding().scope,peer.get(),value)&&ymcc::sleeppower::samePowerObservation(scope->binding().captured,value),"actual-native-entry-re-reads-one-coherent-public-tuple");
 const auto key=scope->binding().scope;auto receipt=returnedReceipt(scope->binding());scope.reset();
 require(provider.outstanding()==1&&provider.current(key,peer.get(),Purpose::FanResume),"native-entry-keeps-accepted-scope-after-local-request-object-return");
 require(provider.settle(peer.get(),receipt)&&provider.outstanding()==0,"actual-native-entry-retires-only-authenticated-resource-return");
 require(GetProcessHandleCount(peer.get(),&handlesAfter)!=FALSE&&handlesAfter==handlesBefore,"native-entry-returns-all-owned-kernel-handles");
}
