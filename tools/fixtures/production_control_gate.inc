
static void pcPublishGate(const json& value)noexcept {
 if(!pcActive.load(std::memory_order_acquire))return;
 try{
  const auto phase=value.value("phase",std::string());
  if(phase=="stopped"){++pcStoppedPublishes;return;}
  if(phase!="waiting-events")return;
  pcWorkerTid.store(GetCurrentThreadId(),std::memory_order_release);++pcWaitingPublishes;
  if(!pcEntered||!SetEvent(pcEntered)||!pcRelease||WaitForSingleObject(pcRelease,5000)!=WAIT_OBJECT_0)pcGateTimedOut=true;
 }catch(...){pcGateTimedOut=true;}
}
