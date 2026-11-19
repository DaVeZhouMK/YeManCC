
static HANDLE fixtureThreadGate=nullptr;
static std::atomic<unsigned> fixtureCreated{0},fixtureReturned{0},fixtureConstructed{0};
static std::atomic<bool> fixtureFailCreate{false};

static std::mutex fixtureThreadMx;
static std::vector<std::thread> fixtureThreads;
static std::vector<json> fixtureTickets;
static std::function<void()> fixtureOnFailedCreate;
static ULONGLONG fixtureClock(){return ::GetTickCount64()+fixtureClockAdvance.load();}
class FixtureThread {
 std::thread worker_;
public:
 template<class F>FixtureThread(ymcc::fan_resume::RequestTicket ticket,F body){
  ++fixtureConstructed;
  const auto state=g_fanResumeRequestOwner.snapshot(ticket);const auto key=g_fanResumeRequestOwner.requestKey(ticket);
  if(!state||!key)throw std::runtime_error("unowned actual scheduler ticket");
  {std::lock_guard<std::mutex> lock(fixtureThreadMx);fixtureTickets.push_back({{"request",ticket.request},{"run",ticket.run},{"generation",ticket.generation},{"started",state->startedTick},{"deadline",state->deadlineTick},{"manual",state->manualOperation},{"attempts",state->attempts},{"low",std::to_string(key->first)},{"high",std::to_string(key->second)}});}
  if(fixtureFailCreate.exchange(false)){if(fixtureOnFailedCreate)fixtureOnFailedCreate();throw std::runtime_error("explicit thread creation failure leaf");}
  ++fixtureCreated;
  worker_=std::thread([body=std::move(body)]()mutable{if(WaitForSingleObject(fixtureThreadGate,2000)!=WAIT_OBJECT_0)std::terminate();body();++fixtureReturned;});
 }
 void detach(){std::lock_guard<std::mutex> lock(fixtureThreadMx);fixtureThreads.push_back(std::move(worker_));}
};
static void fixtureJoin(){
 const auto deadline=::GetTickCount64()+2500;
 while(fixtureReturned.load()!=fixtureCreated.load()){if(::GetTickCount64()>=deadline)throw std::runtime_error("actual scheduled workers failed to return");Sleep(1);}
 std::vector<std::thread> joined;{std::lock_guard<std::mutex> lock(fixtureThreadMx);joined.swap(fixtureThreads);}for(auto& thread:joined)if(thread.joinable())thread.join();
}
static std::function<json(const json&)> fixtureManual;
template<class F>static void ipc_on(const char* name,F handler){if(std::string(name)!="power.fanManualWake")throw std::runtime_error("wrong original IPC leaf");fixtureManual=handler;}
static const char* powerLifecycleName(PowerLifecycle p){switch(p){case PowerLifecycle::Ready:return "ready";case PowerLifecycle::Suspended:return "suspended";case PowerLifecycle::Suspending:return "suspending";case PowerLifecycle::Resuming:return "resuming";default:return "unknown";}}
static std::atomic<unsigned long long> g_resumeReadyGeneration{0};
static ULONGLONG g_resumeReadyTick=0;
static unsigned fixtureCommitCalls=0;
static void revokeSharedPowerMirrors(){g_sharedPowerRevocations.revoke();}
static bool commitPowerResume(unsigned long long generation,const char*){
 ++fixtureCommitCalls;if(generation!=currentPowerGeneration())return false;
 g_powerLifecycle=PowerLifecycle::Ready;++sharedNotification;++sharedPublication;
 fanHostScheduleFanResume(currentFanPowerGeneration());return true;
}
static ymcc::fan_wake::Observation g_fanPhysicalWake;
static void fanHostQueueSuspend(const char*,unsigned long long){}
