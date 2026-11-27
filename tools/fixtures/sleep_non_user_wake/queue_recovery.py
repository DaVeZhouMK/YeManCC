from pathlib import Path
import json,re

def pair(g):
    p=g['preamble'];f=g['functions'];extract=g['function'];header=g['wake_recovery_source'];source=g['source']
    def one(a,b):
        nonlocal p
        assert p.count(a)==1,('queue recovery adapter anchor',a);p=p.replace(a,b)
    if 'bool fallback=false,acceptedOnce=false;' not in p:
        one('std::string markerText;bool done=false;','std::string markerText;bool done=false,acceptedOnce=false;')
    one('void sgRecordFact(const char* event,const json& details=json::object()){facts.emplace_back(event,details);}',
        'static std::function<void()> queueJournalHook,queueBeforePrepareHook;\nvoid sgRecordFact(const char* event,const json& details=json::object()){facts.emplace_back(event,details);if(std::strcmp(event,"sleep-game-recovery-canceled")==0&&queueJournalHook){auto hook=std::exchange(queueJournalHook,{});hook();}}')
    old='static void sgCancelWakeGameRecoveryUnlocked(const char* reason){++recoveryCancelCalls;g_sgWakeRecoveryPending=false;g_sgWakeRecovery={};sgReconcileSleepGameFlagsAfterRecoveryCancel(reason);}'
    origin='native/wake_game_recovery.h'
    actualPrepare=extract('sgPrepareWakeRecoveryForSleep',header,origin)
    guarded='const std::function<bool()>& requestCurrent' in actualPrepare
    ports='' if 'using SgWakeRecoveryEntry=RecoveryEntry;' in p else 'using SgWakeRecoveryEntry=RecoveryEntry;\n'
    ports+='std::string sgReadFile(const std::wstring&){return markerExists?"retained-marker":std::string{};}\n'
    actual=[]
    names=['sgWakeRecoveryMarker','sgWakeRecoveryClearTarget']
    cancel=extract('sgCancelWakeGameRecoveryUnlocked',header,origin)
    if 'sgRetireAcceptedWakeMarkerUnlocked(' in cancel:
        retirement=re.findall(r'^enum class SgWakeMarkerRetirement \{[^\n]*\};$',header,re.M)
        assert len(retirement)==1
        journal=(g['args'].source.resolve().parent/'sleep_legacy_resume_journal.h').read_text(encoding='utf-8-sig')
        handle=re.findall(r'^struct Close \{[^}]*\{[^}]*\}[^}]*\};\s*using File = [^;]+;',journal,re.M)
        assert len(handle)==1
        ports+='namespace ymcc::sleepLegacyResume {\n'+handle[0]+'\n}\n'
        actual.extend(retirement)
        names.append('sgRetireAcceptedWakeMarkerUnlocked')
    one(old,ports)
    actual.extend(extract(n,header,origin) for n in names)
    actual.extend([cancel,actualPrepare])
    opening=p.rfind('static ',0,p.index(' sgPrepareWakeRecoveryForSleep('));br=p.index('{',opening);end=br+1;depth=1
    while depth:depth+=(p[end]=='{')-(p[end]=='}');end+=1
    bridge='\nstatic bool queuePrepareBridge(unsigned long long generation'+(',const std::function<bool()>& current' if guarded else '')+'){if(queueBeforePrepareHook){auto hook=std::exchange(queueBeforePrepareHook,{});hook();}'
    bridge+=('return sgPrepareWakeRecoveryForSleep(generation,current);' if guarded else 'sgPrepareWakeRecoveryForSleep(generation);return true;')+'}\n'
    p=p[:opening]+p[end:]
    # Bind cancellation after the real marker reader and retained-delivery helpers.
    marker_reader_end=f.index('static bool sgResumeMarkerDefinitelyStale(')
    f=f[:marker_reader_end]+'\n'.join(actual)+'\n'+bridge+'\n'+f[marker_reader_end:]
    worker=f.index('void runProductionSuspendPause(')
    f=f[:worker]+'#define sgPrepareWakeRecoveryForSleep queuePrepareBridge\n'+f[worker:]+'\n#undef sgPrepareWakeRecoveryForSleep\n'
    checks=g['checks'];checks=checks[:checks.index('int main(){')]
    checks+=r'''
int main(){
 json rows=json::array();
 for(const std::string mode:{"stale-ordinary-auto","stale-ordinary-user","stale-ordinary-critical","stale-ordinary-post-failed","stale-ordinary-clock-back","stale-ordinary-manual","stale-returned-auto","current-cross-generation","current-same-generation","current-no-pending","caller-auto","locked-auto","journal-auto","query-auto","current-running-retire","default-helper","default-cancel"}){
  reset();returnedPublicReset();g_sgWorkQ.clear();g_sgWorkStop=false;ordinarySteamCaptures=0;ordinaryCaptureHook={};queueJournalHook=queueBeforePrepareHook={};pauseThreadProbeHook={};
  g_powerGeneration=7;g_powerLifecycle=PowerLifecycle::Suspending;g_sgTask.generation=7;g_sgGuardPlaceholder();
  g_sgLastPowerButtonSleepIntentFileTime=200;g_sgLatestSleepEntryFileTime=200;g_sgLatestUserSleepEntryFileTime=200;g_sgOriginalPowerButtonWakeFileTime=100;
  sgQueueWork(SgWork::Suspend,7,200);assert(g_sgWorkQ.size()==1);auto item=g_sgWorkQ.front();g_sgWorkQ.pop_front();
  if(mode=="stale-returned-auto")item={SgWork::Suspend,7,200,100,100,1234};
  g_sgWakeRecoveryPending=true;g_sgWakeRecovery.generation=mode=="current-same-generation"?7:6;
  auto& retained=g_sgWakeRecovery.entries.emplace_back();
  retained.dir=SG_SLEEP_LEASE_DIR;retained.pid=100;retained.created=1234;retained.sleepOwnerGeneration=6;
  markerExists=true;markerCreated=1234;markerGeneration=6;freezeDepth=1;g_sgSleepTarget={100,1234,11,6,true,true};g_sgInSuspend=true;g_sgGameActuallySuspended=true;
  assert(sgReadResumeMarkerChecked(sgWakeRecoveryMarker(retained),retained.markerText));
  if(mode=="current-no-pending")g_sgWakeRecoveryPending=false;
  if(mode=="query-auto"||mode=="current-running-retire"){
   g_sgWakeRecovery.entries.front().acceptedOnce=true;WakeGameResume::pauseThreadState={true,3,0};freezeDepth=0;
  }
  const auto publish=[] {returnedPublicPublish(18,300,true);};
  if(mode.starts_with("stale-")){
   if(mode=="stale-ordinary-manual")g_sgOriginalPowerButtonWakeFileTime=300;
   else returnedPublicPublish(mode=="stale-ordinary-user"?7:mode=="stale-ordinary-critical"?6:18,mode=="stale-ordinary-clock-back"?50:300,mode!="stale-ordinary-post-failed");
  }
  if(mode=="caller-auto")queueBeforePrepareHook=publish;
  if(mode=="journal-auto")queueJournalHook=publish;
  if(mode=="query-auto")pauseThreadProbeHook=publish;
  if(mode=="default-helper")sgPrepareWakeRecoveryForSleep(7);
  else if(mode=="default-cancel")sgCancelWakeGameRecoveryUnlocked("default-call-control");
  else if(mode=="locked-auto"){
   // Hold the actual operation mutex until callback publication. The original
   // helper cannot enter its body before that publication, regardless of OS scheduling.
   std::unique_lock<std::mutex> held(g_sgOpMtx);std::atomic<bool> started{false};
   std::thread waiter([&]{started=true;QUEUE_DIRECT_PREPARE});while(!started.load())std::this_thread::yield();
   publish();held.unlock();waiter.join();
  }else runProductionSuspendPause(7,&item);
  const bool stale=mode.starts_with("stale-")||mode=="caller-auto"||mode=="locked-auto"||mode=="journal-auto"||mode=="query-auto";
  const auto canceled=std::count_if(facts.begin(),facts.end(),[](const auto& fact){return fact.first=="sleep-game-recovery-canceled";});
  rows.push_back({{"mode",mode},{"pending",g_sgWakeRecoveryPending.load()},{"batchGeneration",g_sgWakeRecovery.generation},{"entries",g_sgWakeRecovery.entries.size()},{"marker",markerExists},{"leaseOwner",g_sgSleepTarget.powerGeneration},{"freezeDepth",freezeDepth},{"suspendCalls",suspendCalls},{"resumeCalls",resumeCalls},{"cancelFacts",canceled},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"threadQueries",pauseThreadQueries}});
  std::cout<<"QUEUE_RECOVERY_BOUNDARY "<<rows.back().dump()<<std::endl;
  if(stale){
   assert(g_sgWakeRecoveryPending&&g_sgWakeRecovery.generation==6&&g_sgWakeRecovery.entries.size()==1&&"revoked queued pause retired active wake recovery");
   assert(markerExists&&g_sgSleepTarget.markerOwned&&g_sgSleepTarget.powerGeneration==6&&"revoked recovery cleanup retired original marker");
   assert(suspendCalls==0&&resumeCalls==0&&"revoked queued pause issued a process action");
  }else if(mode=="current-same-generation")assert(g_sgWakeRecoveryPending&&g_sgWakeRecovery.generation==7&&canceled==0);
  else if(mode=="current-no-pending")assert(!g_sgWakeRecoveryPending&&canceled==0);
  else assert(!g_sgWakeRecoveryPending&&canceled==1);
  if(mode=="current-running-retire")assert(suspendCalls==1&&freezeDepth==1&&markerGeneration==7);
  if(mode=="query-auto")assert(pauseThreadQueries==1&&"wake was not delivered at the thread query");
 }
 std::cout<<"QUEUE_RECOVERY_MATRIX "<<json({{"status","PASS_ORIGINAL_QUEUE_RECOVERY_OWNERSHIP"},{"count",rows.size()},{"cases",rows},{"physicalCalls",0}}).dump()<<std::endl;return 0;
}
'''.replace('g_sgGuardPlaceholder();','g_guardEnabled=true;g_sgRepairEligible=true;')
    direct='sgPrepareWakeRecoveryForSleep(7, [&]{return sgOrdinaryPauseCurrent(item);});' if guarded else 'sgPrepareWakeRecoveryForSleep(7);'
    checks=checks.replace('QUEUE_DIRECT_PREPARE',direct)
    task=g['out']/'QUEUE-RECOVERY-BINDING.json'
    with task.open('x',encoding='utf-8') as out:json.dump(dict(actualHeaderFunctions=names+['sgCancelWakeGameRecoveryUnlocked','sgPrepareWakeRecoveryForSleep','sgReconcileSleepGameFlagsAfterRecoveryCancel'],unrewrittenBodies=True,actualWorkerPauseFragment=True,actualPublicDispatchSecondTu=True,callerYieldMacroOnly=True,actualMutexHeldDuringIngress=True,realWindowsInputOrSleep=False,guardedSource=guarded),out,indent=2)
    return p,f,checks
