from pathlib import Path
import json,runpy

def pair(g):
    p,f,checks=runpy.run_path(str(Path(__file__).with_name('queue_recovery.py')))['pair'](g)
    a='static std::function<void()> queueJournalHook,queueBeforePrepareHook;';assert p.count(a)==1
    p=p.replace(a,'static std::function<void()> pauseCompletionHook;static std::string pauseCompletionEvent;\n'+a)
    a='facts.emplace_back(event,details);';assert p.count(a)==1
    p=p.replace(a,a+'if(pauseCompletionHook&&pauseCompletionEvent==event){auto hook=std::exchange(pauseCompletionHook,{});hook();}')
    # One iteration contains the entire original Suspend branch, including its
    # original dispatch guard, accurate delivery acknowledgement and CAS tail.
    start=f.index('void runProductionSuspendPause(');br=f.index('{',start);end=br+1;depth=1
    while depth:depth+=(f[end]=='{')-(f[end]=='}');end+=1
    wrapper='void runProductionSuspendPause(ULONGLONG generation,const SgWorkItem* scope=nullptr){\nconst SgWorkItem item=scope?*scope:SgWorkItem{SgWork::Suspend,generation};\nfor(unsigned once=0;once<1;++once){\n'+g['worker_pause']+'\n}\n}\n}\n'
    f=f[:start]+wrapper+f[end:]
    checks=checks[:checks.index('int main(){')]
    checks+=r'''
int main(){
 json rows=json::array();
 const auto one=[&](unsigned ticket,unsigned stage,unsigned input){
  reset();returnedPublicReset();g_sgWorkQ.clear();g_sgWorkStop=false;ordinarySteamCaptures=0;ordinaryCaptureHook={};queueJournalHook=queueBeforePrepareHook={};pauseThreadProbeHook={};pauseCompletionHook={};
  g_sgTask.generation=7;g_guardEnabled=true;g_sgRepairEligible=true;g_powerLifecycle=PowerLifecycle::Suspending;
  g_sgLastPowerButtonSleepIntentFileTime=200;g_sgLatestSleepEntryFileTime=200;g_sgLatestUserSleepEntryFileTime=200;g_sgOriginalPowerButtonWakeFileTime=100;
  SgWorkItem item{SgWork::Suspend,7,200,100,100,1234};
  if(!ticket){sgQueueWork(SgWork::Suspend,7,200);assert(g_sgWorkQ.size()==1);item=g_sgWorkQ.front();g_sgWorkQ.pop_front();}
  if(stage==2)freeze();
  g_sgSleepCycleActive=false;g_sgPauseWorkCompletedGeneration=0;
  const auto callsBefore=suspendCalls;const auto resumedBefore=resumeCalls;unsigned inputs=0;
  pauseDetailLogging=stage==0;
  pauseCompletionEvent=stage==0?"sleep-game-pause-receipt":stage==1?"sleep-game-pause":"sleep-game-pause-skipped";
  pauseCompletionHook=[&]{
   ++inputs;
   if(input==0)return;
   if(input==6){g_sgOriginalPowerButtonWakeFileTime=300;return;}
   returnedPublicPublish(input==2?7:input==3?6:18,input==5?50:300,input!=4);
   if(input==7){
    g_sgOriginalPowerButtonWakeFileTime=300;
    sgAbortSleepIntent("completion-original-ui-cancel",300);
    assert(!markerExists&&!g_sgSleepTarget.markerOwned&&freezeDepth==0&&resumeCalls==resumedBefore+1);
   }
  };
  runProductionSuspendPause(7,&item);
  const bool revoked=input!=0;
  rows.push_back({{"ticket",ticket},{"stage",stage},{"input",input},{"phase",static_cast<unsigned>(g_powerLifecycle.load())},{"cycle",g_sgSleepCycleActive.load()},{"inSuspend",g_sgInSuspend.load()},{"actuallySuspended",g_sgGameActuallySuspended.load()},{"marker",markerExists},{"markerOwned",g_sgSleepTarget.markerOwned},{"freezeDepth",freezeDepth},{"suspendCalls",suspendCalls},{"resumeCalls",resumeCalls},{"completionGeneration",g_sgPauseWorkCompletedGeneration.load()},{"callbackCount",inputs}});
  std::cout<<"PAUSE_COMPLETION_BOUNDARY "<<rows.back().dump()<<std::endl;
  assert(inputs==1&&!pauseCompletionHook&&"completion boundary input was not consumed exactly once");
  if(revoked){
   if(input!=7)assert(g_powerLifecycle==PowerLifecycle::Suspending&&!g_sgSleepCycleActive&&"revoked pause completion published stale lifecycle");
   const bool flags=stage==2;
   assert(g_sgInSuspend==flags&&g_sgGameActuallySuspended==flags&&"revoked pause completion rewrote resource flags");
  }else assert(g_powerLifecycle==PowerLifecycle::Suspended&&g_sgSleepCycleActive&&g_sgInSuspend&&g_sgGameActuallySuspended);
  assert(g_sgPauseWorkCompletedGeneration==7&&"completion delivery acknowledgement was lost");
  assert(suspendCalls==callsBefore+(stage==2?0:1)&&"completion boundary issued another suspend");
  if(input!=7)assert(markerExists&&g_sgSleepTarget.markerOwned&&freezeDepth==1&&resumeCalls==resumedBefore&&"completion veto lost the exact already-issued lease");
  else assert(!markerExists&&!g_sgSleepTarget.markerOwned&&freezeDepth==0);
  // The existing original exact-owner resume can still return an already-issued
  // pause once; suppressing a stale completion must never consume that lease.
  g_powerLifecycle=PowerLifecycle::Resuming;const auto rr=sgResumeSleepTarget(7,true);
  assert(rr.count==(input==7?0:1)&&freezeDepth==0&&!markerExists&&!g_sgSleepTarget.markerOwned);
  const auto calls=resumeCalls;const auto second=sgResumeSleepTarget(7,true);assert(second.count==0&&resumeCalls==calls);
 };
 for(const unsigned ticket:{0U,1U})for(const unsigned stage:{0U,1U,2U})for(const unsigned input:{0U,1U,2U,3U,4U,5U,6U})one(ticket,stage,input);
 for(const unsigned ticket:{0U,1U})one(ticket,1,7);
 std::cout<<"PAUSE_COMPLETION_MATRIX "<<json({{"status","PASS_ORIGINAL_PAUSE_COMPLETION_OWNERSHIP"},{"count",rows.size()},{"cases",rows},{"physicalCalls",0}}).dump()<<std::endl;return 0;
}
'''
    with (g['out']/'PAUSE-COMPLETION-BINDING.json').open('x',encoding='utf-8') as out:json.dump(dict(entireOriginalSuspendBranch=True,wholeWorkLoopExecuted=False,originalBranchBodiesUnrewritten=True,originalFinalNtMemoryPort=True,originalActualUiAbortAndResume=True,actualPublicDispatchSecondTu=True,originalDeliveryAcknowledgement=True,powerAuthorityAdded=False),out,indent=2)
    return p,f,checks
