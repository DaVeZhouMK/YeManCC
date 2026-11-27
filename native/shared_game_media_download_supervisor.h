#pragma once
#include "process_efficiency.h"
#include "shared_game_media_download_queue.h"
#include "shared_game_media_download_resources.h"
#include "shared_game_media_identity_worker.h"
#include "steam_source_lease.h"
#include "shared_game_media_source_job.h"
#include "shared_game_media_playback30_deadline.h"
#include <atomic>
#include <deque>

namespace ymcc::media::downloads {
class Supervisor {
  Queue queue_;
  fs::path executable_;
  Limits limits_;
  int64_t startupProtectionUntil_;
  ResourcePorts resources_;
  CpuReader cpuReader_;
  CpuGate cpuGate_;
  uint64_t sampledAt_ = 0;
  std::optional<double> sampledCpu_;
  std::string resourceReason_;
  int sampledThreshold_ = 0;
  HANDLE stop_ = nullptr, wake_ = nullptr;
  std::thread thread_;
  std::mutex lifecycleMutex_;
  bool stopping_=false;
  std::atomic<bool> armed_{false};
  uint64_t nextLaunchAt_ = 0;
  unsigned launchFailures_ = 0;
  bool identityRecheckPending_ = true; // owner thread only; notifications coalesce
  struct PlaybackSource {
    std::string key,identity;
    uint64_t bindingRevision=0;
    bool olderThan(const PlaybackSource& other) const {
      return bindingRevision&&bindingRevision<other.bindingRevision&&key==other.key&&identity!=other.identity;
    }
  };
  struct PlaybackAttempt {
    int code=24;const char* stage="exception";DWORD error=0;const char* veto=nullptr;
    sleeppower::SharedPowerV1 power{};
  };
  struct PlaybackRetryLog {
    uint64_t policy=0,binding=0;
    sleeppower::SharedPowerV1 power{};
    unsigned vetoKind=0,streak=0;
    bool observe(const PlaybackAttempt& attempt,bool requeued,uint64_t nextPolicy,uint64_t nextBinding) noexcept {
      const std::string_view veto=attempt.veto?attempt.veto:"";
      const unsigned kind=veto=="cpu-busy"?1u:veto=="cpu-recovering"?2u:0u;
      if(!requeued||attempt.code!=17||attempt.error||!attempt.stage||std::string_view(attempt.stage)!="admission-withdrawn"||
         !kind||!sleeppower::powerAdmitted(attempt.power)){
        streak=0;return false;
      }
      const bool same=streak&&vetoKind==kind&&policy==nextPolicy&&binding==nextBinding&&
        sleeppower::samePowerRevocation(power,attempt.power);
      streak=same?streak+1:1;
      policy=nextPolicy;binding=nextBinding;power=attempt.power;vetoKind=kind;
      // Keep a progress witness even if the pending task expires without another attempt.
      return same&&streak%8!=0;
    }
  };
  struct PlaybackTask {Json claim;PlaybackSource source;uint64_t due=0,expires=0;bool failureRetried=false;PlaybackRetryLog retryLog{};};
  std::mutex playbackMutex_;
  std::deque<PlaybackTask> playbackPending_;
  std::map<std::string,uint64_t> playbackDone_;
  PlaybackSource playbackActive_;
  // Low byte is the threshold (zero means unknown/blocked); high bits fence
  // a job across a busy -> idle transition even if the owner wakes late.
  // The retired Job ledger can outlive the Supervisor after a failed drain.
  std::shared_ptr<std::atomic<uint64_t>> playbackPolicy_=std::make_shared<std::atomic<uint64_t>>(0);
  CpuReader playbackCpu_;
  CpuGate playbackCpuGate_;
  uint64_t playbackCpuAt_=0;
  std::optional<double> playbackCpuSample_;
  DWORD waitOwner(DWORD count,const HANDLE* events,BOOL all,DWORD timeout) {
    const auto result=WaitForMultipleObjects(count,events,all,timeout);
    if(result<WAIT_OBJECT_0+count&&events[result-WAIT_OBJECT_0]==wake_)identityRecheckPending_=true;
    return result;
  }
  static std::wstring quote(const std::wstring &text) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : text) {
      if (c == L'\\') {
        ++slashes;
        continue;
      }
      if (c == L'\"') {
        result.append(slashes * 2 + 1, L'\\');
        result += c;
        slashes = 0;
        continue;
      }
      result.append(slashes, L'\\');
      slashes = 0;
      result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
  }
  const char* playbackResourceVeto(uint64_t tick,uint64_t policy,int64_t* retry=nullptr) {
    if(retry)*retry=20000;
    if(tick-processStartedTick<limits_.startup)return "startup-protection";
    if(!sleeppower::task::allowed())return "shared-power-blocked";
    const auto ac=resources_.ac();
    if(ac!=1)return ac==0?"on-battery":"power-unknown";
    const auto threshold=static_cast<int>(policy&0xff);
    if(!threshold||playbackPolicy_->load(std::memory_order_acquire)!=policy)return "idle-policy-changed";
    if(powerPolicy::blocked())return "native-power-policy-blocked";
    SYSTEM_POWER_STATUS status{};
    if(!ymcc::powerSource::read(status))return "power-unknown";
    if(status.ACLineStatus!=1)return status.ACLineStatus==0?"on-battery":"power-unknown";
    if(status.SystemStatusFlag==1)return "battery-saver";
    if(!playbackCpuAt_||tick-playbackCpuAt_>=limits_.monitor){
      playbackCpuAt_=tick;
      playbackCpuSample_=resources_.cpu?resources_.cpu(tick):playbackCpu_.read(tick);
    }
    const auto* reason=playbackCpuGate_.observe(playbackCpuSample_,threshold,tick);
    if(retry){
      if(std::string_view(reason)=="cpu-recovering")*retry=CpuGate::calmWindowMs;
      else if(!playbackCpuSample_)*retry=limits_.monitor;
    }
    return *reason?reason:nullptr;
  }
  PlaybackAttempt runPlayback30(const Json& claim,uint64_t policy) {
    if(SourceJob::reap())return {17,"cleanup-pending"};
    const auto manifest=read(executable_.parent_path()/L"package-manifest.json");
    if(manifest.value("backgroundPlayback30Protocol",0)!=1)return {23,"package-protocol"};
    const auto token=randomId();
    const auto stage=custom_steam_library::ioPath(Store(queue_.root()).playback30Staging(token));
    const auto permit=sleeppower::task::Scope::capture();
    auto* transport=sleeppower::task::transport();
    if(!permit||!transport)return {17,"power-not-ready"};
    SourceJob job(nullptr,stage.native());
    if(!job.transfer().prepare(token))return {24,"prepare-transfer"};
    const auto parent=GetCurrentProcessId();const auto birth=processBirthToken(parent);
    if(birth.empty())return {24,"parent-identity"};
    const auto command=quote(executable_.wstring())+L" --media-playback30 "+std::to_wstring(parent)+L" "+quote(wide(birth))+
      L" "+quote(wide(token))+L" "+quote(wide(stringField(claim,"key")))+L" "+quote(wide(stringField(claim,"id")))+
      L" "+quote(wide(stringField(claim,"revision")))+job.transfer().arguments();
    std::vector<wchar_t> line(command.begin(),command.end());line.push_back(0);
    sleeppower::task::HandleList inherited({job.transfer().handle()});
    if(!permit->current())return {17,"admission-withdrawn",0,"shared-power-permit-revoked"};
    if(const auto* veto=playbackResourceVeto(GetTickCount64(),policy))return {17,"admission-withdrawn",0,veto};
    PROCESS_INFORMATION child{};
    if(!ymcc::processEfficiency::createProcess(executable_.c_str(),line.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|IDLE_PRIORITY_CLASS|EXTENDED_STARTUPINFO_PRESENT,
        nullptr,executable_.parent_path().c_str(),inherited.startup(),&child))return {24,"create-process",GetLastError()};
    if(!job.assign(child))return {24,"assign-job",GetLastError()};
    sleeppower::remote::Key128 request;sleeppower::remote::RemoteOfferV1 offer;
    if(!sleeppower::task::requestKey(token,request)||!transport->captureOwnedTask(job.power(),child.hProcess,job.get(),request,
        [state=playbackPolicy_,permit,policy](auto purpose){return purpose==sleeppower::remote::Purpose::Observation&&
            state->load(std::memory_order_acquire)==policy&&permit->current();},offer)||
       !job.transfer().seal(offer))return {17,"grant-withdrawn",0,"worker-grant-unavailable"};
    if(!permit->current())return {17,"grant-withdrawn",0,"shared-power-permit-revoked"};
    if(const auto* veto=playbackResourceVeto(GetTickCount64(),policy))return {17,"grant-withdrawn",0,veto};
    if(ResumeThread(child.hThread)==DWORD(-1))return {24,"resume-process",GetLastError()};
    const auto started=GetTickCount64();
    playback30::Deadline budget;
    for(;;){
      const auto elapsed=GetTickCount64()-started;
      if(elapsed>=budget.expires())return {24,"process-deadline"};
      if(budget.shouldObserve(elapsed)){
        std::error_code error;const auto bytes=fs::file_size(stage,error);
        budget.observeFileSize(elapsed,bytes,error);
        if(elapsed>=budget.expires())return {24,"process-deadline"};
      }
      HANDLE events[]={stop_,child.hProcess,job.power().cancellationEvent(),wake_};
      const auto until=static_cast<DWORD>((std::min)(uint64_t{1000},budget.expires()-elapsed));
      const auto result=waitOwner(static_cast<DWORD>(std::size(events)),events,FALSE,until);
      if(result==WAIT_OBJECT_0+1){DWORD code=0;if(!GetExitCodeProcess(child.hProcess,&code))return {24,"exit-status",GetLastError()};return {static_cast<int>(code),"child-exit"};}
      if(result==WAIT_OBJECT_0)return {17,"cancelled",0,"owner-shutdown"};
      if(result==WAIT_OBJECT_0+2)return {17,"cancelled",0,"shared-power-cancel"};
      if(result==WAIT_FAILED)return {24,"wait-process",GetLastError()};
      if(!permit->current())return {17,"admission-withdrawn",0,"shared-power-permit-revoked"};
      if(!job.power().current(child.hProcess))return {17,"admission-withdrawn",0,"worker-grant-not-current"};
      if(const auto* veto=playbackResourceVeto(GetTickCount64(),policy))return {17,"admission-withdrawn",0,veto,permit->capturedPower()};
    }
  }
  int64_t servicePlayback30() {
    const auto tick=GetTickCount64();PlaybackTask task;
    const auto policy=playbackPolicy_->load(std::memory_order_acquire);
    {
      std::lock_guard lock(playbackMutex_);
      if(playbackPending_.empty())return -1;
      std::erase_if(playbackPending_,[&](const auto& pending){return tick>=pending.expires;});
      if(playbackPending_.empty())return -1;
      if(!(policy&0xff)){
        // A missing idle grant can only change on a policy wake. Keep the
        // expiry, but don't periodically probe power/CPU until granted.
        const auto expires=std::min_element(playbackPending_.begin(),playbackPending_.end(),[](const auto& a,const auto& b){return a.expires<b.expires;})->expires;
        return static_cast<int64_t>(expires-tick);
      }
      const auto due=std::min_element(playbackPending_.begin(),playbackPending_.end(),[](const auto& a,const auto& b){return a.due<b.due;})->due;
      if(tick<due)return static_cast<int64_t>(due-tick);
    }
    int64_t retry=0;
    if(playbackResourceVeto(tick,policy,&retry))return retry;
    {
      std::lock_guard lock(playbackMutex_);
      const auto next=std::find_if(playbackPending_.rbegin(),playbackPending_.rend(),[&](const auto& pending){return tick>=pending.due;});
      if(next==playbackPending_.rend())return 20000;
      task=std::move(*next);playbackPending_.erase(std::next(next).base());playbackActive_=task.source;
    }
    PlaybackAttempt attempt;
    try{attempt=runPlayback30(task.claim,policy);}catch(...){ }
    const auto completed=GetTickCount64();
    const auto code=attempt.code;
    std::string observedId;
    if(resources_.playbackAttempt)observedId=stringField(task.claim,"id");
    bool requeued=false,hasPending=false,repeatLog=false;
    unsigned retryStreak=0;
    {
      std::lock_guard lock(playbackMutex_);
      playbackActive_={};
      // Policy revocation retains its normal slow retry. A generic transient
      // failure gets one retry, without repeatedly generating a bad asset.
      // Do not requeue an old worker after a newer source has taken its place.
      const bool retryable=(code==17&&completed<task.expires)||(code==24&&!task.failureRetried&&completed+retryMs<task.expires);
      if(retryable&&playbackPending_.size()<8&&std::none_of(playbackPending_.begin(),playbackPending_.end(),
          [&](const auto& pending){return task.source.olderThan(pending.source);})){
        if(code==24)task.failureRetried=true;
        if(resources_.playbackAttempt){
          repeatLog=task.retryLog.observe(attempt,true,policy,task.source.bindingRevision);
          retryStreak=task.retryLog.streak;
        }
        task.due=completed+retryMs;playbackPending_.push_front(std::move(task));requeued=true;
      }else{
        if(playbackDone_.size()>=64){const auto earliest=std::min_element(playbackDone_.begin(),playbackDone_.end(),[](const auto& a,const auto& b){return a.second<b.second;});playbackDone_.erase(earliest);}
        playbackDone_[task.source.identity]=completed+((code>=20&&code<=22)?60000:3600000);
      }
      hasPending=!playbackPending_.empty();
    }
    if(resources_.playbackAttempt)try{resources_.playbackAttempt(observedId,code,attempt.stage,attempt.error,completed-tick,requeued,attempt.veto,retryStreak,repeatLog);}catch(...){ }
    return hasPending?1000:-1;
  }
  std::string resourceReason(const std::string &token = {}) {
    const auto state = queue_.snapshot();
    if (!downloadsEnabled(state) || policyBlocked(state) ||
        (!token.empty() && (Queue::terminalReceipt(state, token) ||
                           !Queue::policyAllowed(state, token))))
      return {};
    const auto publish = [&](const std::string &reason,
                             std::optional<double> cpu = {}, int64_t until = 0) {
      return queue_.resource(reason, cpu, until, token, &state);
    };
    const auto ac = resources_.ac();
    if (ac != 1) {
      sampledAt_ = 0;
      resourceReason_ = ac == 0 ? "on-battery" : "power-unknown";
      publish(resourceReason_);
      return resourceReason_;
    }
    const auto tick = GetTickCount64();
    if (tick - processStartedTick < limits_.startup) {
      resourceReason_ = "startup-protection";
      publish(resourceReason_);
      return resourceReason_;
    }
    if(manualRun(state)||(resources_.foreground?resources_.foreground():libraryForeground(executable_))){
      // Foreground library use bypasses only CPU admission. AC/startup and
      // queue/storage/game policy still apply. Loss of focus samples afresh.
      sampledAt_=0;resourceReason_.clear();publish("");return {};
    }
    const auto threshold = (std::clamp)(state.value("cpuThreshold", 15), 5, 90);
    if (!sampledAt_ || tick - sampledAt_ >= limits_.monitor ||
        threshold != sampledThreshold_) {
      if (!sampledAt_ || tick - sampledAt_ >= limits_.monitor) {
        sampledAt_ = tick;
        sampledCpu_ =
            resources_.cpu ? resources_.cpu(tick) : cpuReader_.read(tick);
      }
      sampledThreshold_ = threshold;
      const auto previousGate = cpuGate_;
      resourceReason_ = cpuGate_.observe(sampledCpu_, threshold, tick);
      if (!publish(resourceReason_, sampledCpu_)) {
        // A rejected observation cannot leave hysteresis for a superseded
        // threshold/request. Re-evaluate on the existing policy wake.
        cpuGate_ = previousGate;
        sampledAt_ = 0;
      }
    }
    return resourceReason_;
  }
  void runOne(const std::string &token) {
    if(SourceJob::reap())throw std::runtime_error("download-source-cleanup-incomplete");
    const auto manifest =
        read(executable_.parent_path() / L"package-manifest.json");
    if(manifest.value("backgroundSharedSleepProtocol",0)!=1)throw std::runtime_error("download-sleep-worker-upgrade-required");
    if(manifest.value("backgroundSteamSourceProtocol",0)!=1)throw std::runtime_error("download-steam-source-worker-upgrade-required");
    if (manifest.value("backgroundMediaProtocol", 0) != 1)
      throw std::runtime_error("download-worker-upgrade-required");
    if(queue_.catalogDue(token)&&manifest.value("backgroundCatalogProtocol",0)!=1) {
      queue_.catalogResult(token,false,"download-worker-upgrade-required");
      if(queue_.snapshot().value("remaining",size_t{0})==0) {queue_.end(token,"deferred","download-worker-upgrade-required");return;}
    }
    if(manifest.value("backgroundIdentityProtocol",0)!=1) {
      queue_.deferUnsupportedIdentities(token);
      const auto state=queue_.document();
      if(nextQueueKey(state,now()).empty()&&!queue_.catalogDue(token)){queue_.end(token,"deferred","download-identity-worker-upgrade-required");return;}
    }
    if(manifest.value("backgroundVideoQualityPolicy",0)<videoQualityPolicy){queue_.deferUnsupportedQuality(token);const auto state=queue_.document();if(nextQueueKey(state,now()).empty()&&!queue_.catalogDue(token)){queue_.end(token,"deferred","download-quality-worker-upgrade-required");return;}}
    if(manualRun(queue_.snapshot())&&manifest.value("backgroundManualProtocol",0)!=1){queue_.rejectManualUpgrade(token);queue_.end(token,"deferred","download-manual-worker-upgrade-required");return;}
    if(scopedManual(queue_.snapshot())&&manifest.value("backgroundVideoSelectionProtocol",0)!=1){const std::string reason="download-video-selection-worker-upgrade-required";queue_.rejectManualUpgrade(token,reason);queue_.end(token,"deferred",reason);return;}
    auto sharedPower=sleeppower::task::Scope::capture();if(!sharedPower)throw std::runtime_error(sleeppower::task::paused);
    auto* publicTransport=sleeppower::task::transport();if(!publicTransport)throw std::runtime_error(sleeppower::task::paused);
    // Capture once. Missing source affects only shortcut lanes in this child.
    auto source=ymcc::steaminfo::transfer::Export::capture(ymcc::steaminfo::acquireSourceV2());
    SourceJob closeJob(std::move(source));
    if(!closeJob.transfer().prepare(token))throw std::runtime_error("media-shared-power-unavailable");
    const HANDLE job=closeJob.get();
    std::wstring command =
        quote(executable_.wstring()) + L" --media-background " +
        std::to_wstring(GetCurrentProcessId()) + L" " +
        quote(wide(processBirthToken(GetCurrentProcessId()))) + L" " +
        quote(wide(token));
    if(closeJob.source())command+=closeJob.source()->arguments();
    command+=closeJob.transfer().arguments();
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(0);
    STARTUPINFOW startup{sizeof startup};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    std::vector<HANDLE> inheritedHandles;if(closeJob.source()){const auto handles=closeJob.source()->inheritanceHandles();inheritedHandles.insert(inheritedHandles.end(),handles.begin(),handles.end());}inheritedHandles.push_back(closeJob.transfer().handle());
    auto inherited=std::make_unique<sleeppower::task::HandleList>(std::move(inheritedHandles));
    if(!sharedPower->current())throw std::runtime_error(sleeppower::task::paused);
    if(closeJob.source()&&!closeJob.source()->current())throw std::runtime_error("media-steam-facts-unavailable");
    // This launch does not enter customSteamLibrary.setIntegrationSession.
    // No HWND/InputOwner property is created by the headless entrypoint.
    if (!ymcc::processEfficiency::createProcess(
            executable_.c_str(), line.data(), nullptr, nullptr, inherited?TRUE:FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | IDLE_PRIORITY_CLASS | (inherited?EXTENDED_STARTUPINFO_PRESENT:0), nullptr,
            executable_.parent_path().c_str(), inherited?inherited->startup():&startup, &child))
      throw std::runtime_error("download-launch-failed");
    if(closeJob.source()&&!closeJob.source()->sealInheritance()){
      closeJob.assign(child);throw std::runtime_error("download-source-inheritance-failed");
    }
    if(!closeJob.assign(child))throw std::runtime_error("download-job-assignment-failed");
    if(closeJob.source()&&!closeJob.source()->current())throw std::runtime_error("media-steam-facts-unavailable");
    sleeppower::remote::Key128 publicRequest;if(!sleeppower::task::requestKey(token,publicRequest))throw std::runtime_error("media-shared-power-unavailable");sleeppower::remote::RemoteOfferV1 offer;
    if(!publicTransport->captureOwnedTask(closeJob.power(),child.hProcess,job,publicRequest,[sharedPower](auto purpose){return purpose==sleeppower::remote::Purpose::Observation&&sharedPower->current();},offer)||!closeJob.transfer().seal(offer)||!sharedPower->current())throw std::runtime_error(sleeppower::task::paused);
    if(!queue_.beginWorkerSession(token,limits_.session))throw std::runtime_error("media-download-cancelled");
    if (ResumeThread(child.hThread) == DWORD(-1))
      throw std::runtime_error("download-resume-failed");
    const auto started = GetTickCount64();
    auto activity = started, taskStarted = uint64_t{0};
    int64_t heartbeat = 0, taskStamp = 0;
    struct LaneClock {int64_t task=0,heartbeat=0;uint64_t started=0,activity=0,bytes=0,byteProgress=0;};
    std::map<std::string,LaneClock> laneClocks;
    std::string failure,failedKey;
    Json expiredClaim;
    bool terminalAccepted = false;
    const auto childExit = [&] {
      DWORD code = 0;
      if (!GetExitCodeProcess(child.hProcess, &code))
        failure = "download-worker-exit-unavailable";
      else if(code==17)failure=sharedPower->current()?"media-shared-power-unavailable":sleeppower::task::paused;
      else if (code)
        failure = "download-worker-exited-" + std::to_string(code);
    };
    const auto drainTerminal = [&] {
      terminalAccepted = true;
      // The worker has durably handed back its lanes and result. Wait only
      // for this owned process/stop, keeping the original session/stall clocks.
      for (;;) {
        const auto tick = GetTickCount64();
        failure = expiry(tick, started, activity, 0, limits_);
        if (!failure.empty()) return;
        const auto remaining = (std::min)(limits_.session - (tick - started),
                                          limits_.stall - (tick - activity));
        std::array<HANDLE,7> events{stop_,child.hProcess};DWORD count=2;
        events[count++]=closeJob.power().cancellationEvent();
        if(auto* source=closeJob.source()){const auto handles=source->inheritanceHandles();for(unsigned i=0;i<4;++i)events[count++]=handles[i];}
        const auto result = waitOwner(count, events.data(), FALSE,
            static_cast<DWORD>((std::min)(remaining, uint64_t{INFINITE - 1})));
        if(result==WAIT_OBJECT_0){failure="owner-stopped";return;}if(result==WAIT_OBJECT_0+1){childExit();return;}
        if(!sharedPower->current()||!closeJob.power().current(child.hProcess)){failure=sleeppower::task::paused;return;}
        if(closeJob.source()&&!closeJob.source()->current()){failure="media-steam-facts-unavailable";return;}
        if (result == WAIT_OBJECT_0) { failure = "owner-stopped"; return; }
        if (result == WAIT_OBJECT_0 + 1) { childExit(); return; }
        if (result == WAIT_FAILED) { failure = "download-worker-wait-failed"; return; }
      }
    };
    for (;;) {
      std::array<HANDLE,8> events{stop_,child.hProcess,wake_};DWORD count=3;
      events[count++]=closeJob.power().cancellationEvent();
      if(auto* source=closeJob.source()){const auto handles=source->inheritanceHandles();for(unsigned i=0;i<4;++i)events[count++]=handles[i];}
      const auto result = waitOwner(count,events.data(),FALSE,limits_.monitor);
      if(result==WAIT_OBJECT_0){failure="owner-stopped";break;}if(result==WAIT_OBJECT_0+1){childExit();break;}
      if(!sharedPower->current()||!closeJob.power().current(child.hProcess)){failure=sleeppower::task::paused;break;}
      if(closeJob.source()&&!closeJob.source()->current()){failure="media-steam-facts-unavailable";break;}
      if (result == WAIT_OBJECT_0) {
        failure = "owner-stopped";
        break;
      }
      if (result == WAIT_OBJECT_0 + 1) {
        childExit();
        break;
      }
      if (result == WAIT_FAILED) {
        failure = "download-worker-wait-failed";
        break;
      }
      auto state = queue_.snapshot();
      if (Queue::terminalReceipt(state, token)) { drainTerminal(); break; }
      // Cancellation/OFF and policy pauses are authoritative before resource
      // probes. An existing resource pause may still be cleared by a fresh probe.
      if (!Queue::policyAllowed(state, token)) {
        failure = "download-paused";
        break;
      }
      resourceReason(token);
      state = queue_.snapshot();
      if (Queue::terminalReceipt(state, token)) { drainTerminal(); break; }
      if (!Queue::allowed(state, token)) {
        failure = "download-paused";
        break;
      }
      const auto stamp = state.value("heartbeatAt", int64_t{0});
      if (stamp != heartbeat) {
        heartbeat = stamp;
        activity = GetTickCount64();
      }
      const auto task = state.value("taskStartedAt", int64_t{0});
      if (task != taskStamp) {
        taskStamp = task;
        taskStarted = task ? GetTickCount64() : 0;
      }
      const auto active=state.value("active",Json::object());
      for(auto it=laneClocks.begin();it!=laneClocks.end();) {
        if(!active.contains(it->first)) it=laneClocks.erase(it);else ++it;
      }
      for(auto it=active.begin();it!=active.end();++it) {
        auto &clock=laneClocks[it.key()];const auto tick=GetTickCount64();
        const auto taskAt=it.value().value("taskStartedAt",int64_t{0});
        const auto beat=it.value().value("heartbeatAt",int64_t{0});
        if(clock.task!=taskAt){clock.task=taskAt;clock.started=tick;clock.activity=tick;clock.bytes=0;clock.byteProgress=0;}
        if(clock.heartbeat!=beat){clock.heartbeat=beat;clock.activity=tick;}
        const auto bytes=it.value().value("bytes",uint64_t{0});
        // Only received video bytes renew the task window. Repeated heartbeats,
        // metadata resolution and publishing cannot keep an unproductive task alive.
        // The original stall and whole-session deadlines still bound every lane.
        if(it.value().value("phase",std::string{})=="downloading"&&bytes>clock.bytes)clock.byteProgress=tick;
        clock.bytes=bytes;
        if(now()>=it.value().value("sessionDeadlineAt",now()+int64_t(sessionMs)))failure="media-task-timeout";
        else if(tick-clock.started>=(std::min)(limits_.attempt,it.value().value("attemptBudgetMs",uint64_t(automaticAttemptMs))))failure="media-task-time-budget";
        else failure=expiry(tick,started,clock.activity,(std::max)(clock.started,clock.byteProgress),limits_);
        if(!failure.empty()){failedKey=it.key();expiredClaim=it.value();break;}
      }
      if(failure.empty()) failure =
          expiry(GetTickCount64(), started, activity, active.empty()?taskStarted:0, limits_);
      if (!failure.empty())
        break;
    }
    if (!failure.empty()) {
      if(failure==sleeppower::task::paused)try{queue_.markSharedSleep(token);}catch(...){/* Original terminal write still owns the same queue;always stop exact Job. */}
      TerminateJobObject(job, 16);
      WaitForSingleObject(child.hProcess, 2000);
      if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0&&!powerPolicy::blocked()&&failure!=sleeppower::task::paused)
        try { queue_.cleanupTemporary(); } catch (...) {}
      // A durable end may race the last wait/probe or a nonzero process exit.
      // Recheck after teardown; accepted results/history/retry debt stay intact.
      if (!terminalAccepted && !Queue::terminalReceipt(queue_.snapshot(), token)) {
        if (failure == "download-paused" || failure == "owner-stopped"||failure==sleeppower::task::paused)
          queue_.end(token, "paused", failure);
        else
          queue_.recover(token, failure,failedKey,failedKey.empty()?nullptr:&expiredClaim);
      }
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(child.hProcess, &created, &exited, &kernel, &user)) {
      const auto ticks = [](FILETIME time) {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
      };
      const auto cpuMs = double(ticks(kernel) + ticks(user)) / 10000.0;
      const auto elapsedMs =
          (std::max)(uint64_t{1}, GetTickCount64() - started);
      const auto processors =
          (std::max)(DWORD{1}, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
      queue_.update(token, [&](Json &j) {
        j["lastWorkerMetrics"] = {
            {"cpuMs", cpuMs},
            {"elapsedMs", elapsedMs},
            {"cpuPercent", cpuMs * 100.0 / double(elapsedMs) / processors},
            {"processors", processors},
            {"priorityClass", GetPriorityClass(child.hProcess)},
            {"outcome", failure.empty() ? "exited" : failure}};
      });
    }
  }
  void loop() {
    for (;;) {
      if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
        return;
      int64_t delay = -1;
      try {
        if(armed_.load()&&!sleeppower::task::allowed()){queue_.resource("system-sleep");const auto debt=SourceJob::reap();HANDLE events[]={stop_,wake_};if(waitOwner(2,events,FALSE,debt?limits_.recheck:INFINITE)==WAIT_OBJECT_0)return;continue;}
        if(armed_.load()&&queue_.snapshot().value("resourcePauseReason",std::string{})=="system-sleep")queue_.resource("");
        if (armed_.load()) {
          const auto elapsed = GetTickCount64() - processStartedTick;
          if (elapsed < limits_.startup) {
            queue_.resource("startup-protection", {}, startupProtectionUntil_);
            HANDLE events[] = {stop_, wake_};
            if (waitOwner(
                    2, events, FALSE,
                    static_cast<DWORD>(limits_.startup - elapsed)) ==
                WAIT_OBJECT_0)
              return;
            continue;
          }
          if (queue_.snapshot().value("resourcePauseReason", std::string{}) ==
              "startup-protection")
            queue_.resource("");
        }
        if(armed_.load()&&identityRecheckPending_){
          identityRecheckPending_=false;
          const auto source=ymcc::steaminfo::acquireSourceV2();
          queue_.recoverIdentityPrerequisites(executable_.parent_path()/L"SteamArtworkLab.exe",[&](const Json& target){return readShortcutCurrentSource(target,source,nullptr,queue_.root());},true);
        }
        const auto playback=servicePlayback30();
        delay = armed_.load() ? queue_.delay() : -1;
        if (delay == 0 && GetTickCount64() < nextLaunchAt_)
          delay = static_cast<int64_t>(nextLaunchAt_ - GetTickCount64());
        if (delay == 0) {
          const auto sampledBefore=sampledAt_;
          const auto blocked = resourceReason();
          if (!blocked.empty()) {
            HANDLE events[] = {stop_, wake_};
            const auto interval =
                (blocked == "cpu-unknown" || blocked == "cpu-recovering" ||
                 (blocked == "cpu-busy"&&sampledBefore==sampledAt_)) ? limits_.monitor : limits_.recheck;
            if (waitOwner(2, events, FALSE, playback<0?interval:(std::min)(interval,static_cast<DWORD>(playback))) ==
                WAIT_OBJECT_0)
              return;
            continue;
          }
          auto token = queue_.start();
          uint64_t cooldown = 3000;
          if (!token.empty())
            try {
              runOne(token);
              launchFailures_ = 0;
            } catch (const std::exception &e) {
              if(std::string(e.what())==sleeppower::task::paused){queue_.end(token,"paused",sleeppower::task::paused);continue;}
              queue_.recover(token, e.what());
              launchFailures_ = (std::min)(8u, launchFailures_ + 1);
              // A fixed slow retry also notices a repaired/replaced helper.
              // Per-game exhaustion must not impose an hour on later arrivals.
              cooldown = retryMs;
            }
          nextLaunchAt_ = GetTickCount64() + cooldown;
          delay = queue_.delay();
          if (delay == 0)
            delay = 3000;
        }
        if(playback>=0)delay=delay<0?playback:(std::min)(delay,playback);
      } catch (...) {
        delay = retryMs;
      }
      if(armed_.load()&&identityRecheckPending_)continue;
      // Only outstanding exact cleanup debt uses the existing slow owner wait.
      if(SourceJob::reap()&&delay<0)delay=limits_.recheck;
      bool slowRecovery=false;
      try{const auto retained=armed_.load()?queue_.snapshot():Json::object();
        // Queue repair cannot lift an explicit policy pause. The policy owner
        // publishes a wake when it changes; only eligible stranded work polls.
        slowRecovery=armed_.load()&&downloadsEnabled(retained)&&!policyBlocked(retained)&&
            (retained.value("remaining",size_t{0})>0||retained.value("catalogPending",false))&&delay<0;
        if(slowRecovery)delay=limits_.recheck;
      }catch(...){delay=limits_.recheck;}
      HANDLE events[] = {stop_, wake_};
      const auto wait =
          delay < 0 ? INFINITE
                    : static_cast<DWORD>((std::min)(
                          int64_t{3600000}, (std::max)(int64_t{250}, delay)));
      const auto result=waitOwner(2, events, FALSE, wait);
      if(result==WAIT_OBJECT_0)return;
      if(result==WAIT_TIMEOUT&&slowRecovery)try{queue_.recheck();}catch(...){}
    }
  }

public:
  Supervisor(fs::path root, fs::path executable, Limits limits = {},
             ResourcePorts resources = {})
      : queue_(std::move(root)), executable_(std::move(executable)),
        limits_(limits), startupProtectionUntil_(now()+static_cast<int64_t>(limits.startup)-static_cast<int64_t>(GetTickCount64()-processStartedTick)), resources_(std::move(resources)) {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_ = CreateEventW(nullptr, FALSE, FALSE,
                         eventName(queue_.root(), L"owner").c_str());
    if (!stop_ || !wake_) {
      if (stop_)
        CloseHandle(stop_);
      if (wake_)
        CloseHandle(wake_);
      throw std::runtime_error("download-owner-unavailable");
    }
    thread_ = std::thread([this] { loop(); });
  }
private:
  bool finish(bool waitForThread) noexcept {
    try{std::unique_lock lifecycle(lifecycleMutex_,std::defer_lock);
      if(waitForThread)lifecycle.lock();else if(!lifecycle.try_lock())return false;
      stopping_=true;
      playbackPolicy_->store(0,std::memory_order_release);
      if(stop_&&!SetEvent(stop_))return false;
      if(thread_.joinable()){
        if(!waitForThread&&WaitForSingleObject(thread_.native_handle(),0)!=WAIT_OBJECT_0)return false;
        thread_.join();
      }
      const auto debt=SourceJob::reap(); // original zero-budget final pass
      const auto close=[](HANDLE& value){if(!value)return true;if(!CloseHandle(value))return false;value=nullptr;return true;};
      const bool a=close(stop_),b=close(wake_);return a&&b&&debt==0;
    }catch(...){return false;}
  }
public:
  bool finishForExit() noexcept {return finish(false);}
  ~Supervisor(){(void)finish(true);}
  Queue &queue() { return queue_; }
  void configurePlayback30(bool idle,int threshold) {
    const auto limit=idle?static_cast<uint64_t>((std::clamp)(threshold,5,90)):0;
    auto policy=playbackPolicy_->load(std::memory_order_relaxed);
    while((policy&0xff)!=limit){
      if(playbackPolicy_->compare_exchange_weak(policy,((policy&~uint64_t{0xff})+0x100)|limit,std::memory_order_release,std::memory_order_relaxed)){wake();break;}
    }
  }
  void requestPlayback30(Json claim) {
    auto key=stringField(claim,"key");
    const auto id=stringField(claim,"id"),revision=stringField(claim,"revision");
    if(!safeKey(key)||!safeKey(id)||!safeKey(revision))return;
    PlaybackSource source{std::move(key),id+":"+revision,claim.value("bindingRevision",uint64_t{0})};
    const auto tick=GetTickCount64();
    {
      std::lock_guard lock(playbackMutex_);
      const auto done=playbackDone_.find(source.identity);
      if(playbackActive_.identity==source.identity||source.olderThan(playbackActive_)||
         (done!=playbackDone_.end()&&tick<done->second))return;
      for(auto& pending:playbackPending_){
        if(source.olderThan(pending.source))return;
        if(pending.source.identity==source.identity){
          if(pending.source.key==source.key)
            pending.source.bindingRevision=(std::max)(pending.source.bindingRevision,source.bindingRevision);
          return;
        }
      }
      std::erase_if(playbackPending_,[&](const auto& pending){return pending.source.olderThan(source);});
      if(playbackPending_.size()>=8)playbackPending_.pop_front();
      playbackPending_.push_back({std::move(claim),std::move(source),tick+3000,tick+20*60000});
    }
    wake();
  }
  void wake(bool activate = false) {
    std::lock_guard lifecycle(lifecycleMutex_);if(stopping_)return;
    if (activate)
      armed_.store(true);
    SetEvent(wake_);
  }
};
// This is the production command/event boundary used by the native IPC and
// isolated chain acceptance. It never calls the visible library launcher.
class Controller {
  std::mutex commandMutex_;
  std::mutex finishMutex_;
  std::atomic<bool> stopping_{false};
  Supervisor owner_;
  Watcher watcher_;

public:
  Controller(fs::path root, fs::path executable,
             std::function<void(const Json &)> changed, Limits limits = {},
             ResourcePorts resources = {})
      : owner_(root, std::move(executable), limits, std::move(resources)),
        watcher_(root, L"main",
                 [root, changed] { changed(Queue(root).snapshot()); }) {}
  void requestPlayback30(Json claim) {if(!stopping_.load(std::memory_order_acquire))owner_.requestPlayback30(std::move(claim));}
  Json background(const Json &request) {
    if (!request.is_object())
      throw std::runtime_error("media-download-command-invalid");
    if(stopping_.load(std::memory_order_acquire))throw std::runtime_error("media-download-owner-stopped");
    if(!steamDemand::allowed(request))return steamDemand::inactive();
    std::lock_guard lock(commandMutex_);
    if(stopping_.load(std::memory_order_acquire))throw std::runtime_error("media-download-owner-stopped");
    if(!steamDemand::allowed(request))return steamDemand::inactive();
    if(request.value("recheck",false))owner_.queue().recheck();
    const bool hasPlaybackPolicy=request.contains("playback30Idle");
    const bool playbackIdle=hasPlaybackPolicy&&request.value("playback30Idle",false)&&request.contains("pauseReason")&&stringField(request,"pauseReason").empty();
    if(hasPlaybackPolicy&&!playbackIdle)owner_.configurePlayback30(false,0);
    if (request.contains("enabled"))
      owner_.queue().configure(request.value("enabled", true),
                               request.value("pauseReason", std::string{}),
                               request.value("cpuThreshold", 15));
    if(playbackIdle)owner_.configurePlayback30(true,request.value("cpuThreshold",15));
    if(request.value("catalog",false)&&request.value("enabled",false)) owner_.queue().requestCatalog();
    if(request.value("manual",false))owner_.queue().requestSupplement(true);
    if(request.value("cancelManual",false))owner_.queue().cancelManual();
    const bool wake=request.contains("enabled")||request.value("manual",false)||request.value("cancelManual",false)||request.value("recheck",false);
    const auto status=request.find("replyStatus");
    const bool admissionOnly=request.contains("targets")&&!wake&&!hasPlaybackPolicy&&
        !request.contains("catalog")&&!request.contains("inventoryRequestId");
    const bool needsStatus=!admissionOnly||status==request.end()||!status->is_boolean()||status->get<bool>();
    Json unchangedSnapshot;
    if (request.contains("targets"))
      owner_.queue().enqueue(request["targets"],{},request,{},
          needsStatus&&!wake&&!request.contains("inventoryRequestId")?&unchangedSnapshot:nullptr);
    if(request.contains("inventoryRequestId"))owner_.queue().inventoryReady(request.value("inventoryRequestId",std::string{}));
    if(wake)owner_.wake(true);
    if(!steamDemand::allowed(request))return steamDemand::inactive();
    if(!needsStatus)return nullptr;
    if(!unchangedSnapshot.is_null())return unchangedSnapshot;
    return owner_.queue().snapshot();
  }
  bool finishForExit() noexcept {
    stopping_.store(true,std::memory_order_release);
    try{std::unique_lock finish(finishMutex_,std::try_to_lock);
      if(!finish)return false;
      const bool watcher=watcher_.finishForExit();
      const bool owner=owner_.finishForExit();
      // A command admitted before stop may still be using the queue.
      std::unique_lock command(commandMutex_,std::try_to_lock);
      return watcher&&owner&&command.owns_lock();
    }catch(...){return false;}
  }
  ~Controller(){stopping_.store(true,std::memory_order_release);}
  Json snapshot() { return owner_.queue().snapshot(); }
};
} // namespace ymcc::media::downloads
