#pragma once
#include "process_efficiency.h"
#include "shared_game_media_download_queue.h"
#include "shared_game_media_download_resources.h"
#include <atomic>

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
  std::atomic<bool> armed_{false};
  uint64_t nextLaunchAt_ = 0;
  unsigned launchFailures_ = 0;
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
  std::string resourceReason() {
    const auto ac = resources_.ac();
    if (ac != 1) {
      sampledAt_ = 0;
      resourceReason_ = ac == 0 ? "on-battery" : "power-unknown";
      queue_.resource(resourceReason_);
      return resourceReason_;
    }
    const auto tick = GetTickCount64();
    if (tick - processStartedTick < limits_.startup) {
      resourceReason_ = "startup-protection";
      queue_.resource(resourceReason_);
      return resourceReason_;
    }
    const auto state = queue_.snapshot();
    if(manualRun(state)){sampledAt_=0;resourceReason_.clear();queue_.resource("");return {};}
    const auto threshold = (std::clamp)(state.value("cpuThreshold", 15), 5, 90);
    if (!sampledAt_ || tick - sampledAt_ >= limits_.monitor ||
        threshold != sampledThreshold_) {
      if (!sampledAt_ || tick - sampledAt_ >= limits_.monitor) {
        sampledAt_ = tick;
        sampledCpu_ =
            resources_.cpu ? resources_.cpu(tick) : cpuReader_.read(tick);
      }
      sampledThreshold_ = threshold;
      resourceReason_ = cpuGate_.observe(sampledCpu_, threshold, tick);
      queue_.resource(resourceReason_, sampledCpu_);
    }
    return resourceReason_;
  }
  void runOne(const std::string &token) {
    const auto manifest =
        read(executable_.parent_path() / L"package-manifest.json");
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
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
      throw std::runtime_error("download-job-unavailable");
    struct CloseJob {
      HANDLE h;
      ~CloseJob() { CloseHandle(h); }
    } closeJob{job};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit,
                                 sizeof limit))
      throw std::runtime_error("download-job-unavailable");
    std::wstring command =
        quote(executable_.wstring()) + L" --media-background " +
        std::to_wstring(GetCurrentProcessId()) + L" " +
        quote(wide(processBirthToken(GetCurrentProcessId()))) + L" " +
        quote(wide(token));
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(0);
    STARTUPINFOW startup{sizeof startup};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    // This launch does not enter customSteamLibrary.setIntegrationSession.
    // No HWND/InputOwner property is created by the headless entrypoint.
    if (!ymcc::processEfficiency::createProcess(
            executable_.c_str(), line.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | IDLE_PRIORITY_CLASS, nullptr,
            executable_.parent_path().c_str(), &startup, &child))
      throw std::runtime_error("download-launch-failed");
    struct Child {
      PROCESS_INFORMATION p;
      ~Child() {
        if (WaitForSingleObject(p.hProcess, 0) == WAIT_TIMEOUT) {
          TerminateProcess(p.hProcess, 16);
          WaitForSingleObject(p.hProcess, 2000);
        }
        CloseHandle(p.hThread);
        CloseHandle(p.hProcess);
      }
    } owned{child};
    if (!AssignProcessToJobObject(job, child.hProcess))
      throw std::runtime_error("download-job-assignment-failed");
    if (ResumeThread(child.hThread) == DWORD(-1))
      throw std::runtime_error("download-resume-failed");
    const auto started = GetTickCount64();
    auto activity = started, taskStarted = uint64_t{0};
    int64_t heartbeat = 0, taskStamp = 0;
    struct LaneClock {int64_t task=0,heartbeat=0;uint64_t started=0,activity=0,bytes=0,byteProgress=0;};
    std::map<std::string,LaneClock> laneClocks;
    std::string failure,failedKey;
    for (;;) {
      HANDLE events[] = {stop_, child.hProcess, wake_};
      const auto result =
          WaitForMultipleObjects(3, events, FALSE, limits_.monitor);
      if (result == WAIT_OBJECT_0) {
        failure = "owner-stopped";
        break;
      }
      if (result == WAIT_OBJECT_0 + 1) {
        DWORD exit = 0;
        GetExitCodeProcess(child.hProcess, &exit);
        if (exit)
          failure = "download-worker-exited-" + std::to_string(exit);
        break;
      }
      resourceReason();
      if (!queue_.allowed(token)) {
        failure = "download-paused";
        break;
      }
      auto state = queue_.snapshot();
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
        if(tick-clock.started>=(std::min)(limits_.attempt,it.value().value("attemptBudgetMs",uint64_t(automaticAttemptMs))))failure="media-task-time-budget";
        else failure=expiry(tick,started,clock.activity,(std::max)(clock.started,clock.byteProgress),limits_);
        if(!failure.empty()){failedKey=it.key();break;}
      }
      if(failure.empty()) failure =
          expiry(GetTickCount64(), started, activity, active.empty()?taskStarted:0, limits_);
      if (!failure.empty())
        break;
    }
    if (!failure.empty()) {
      TerminateJobObject(job, 16);
      WaitForSingleObject(child.hProcess, 2000);
      if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0)
        try { queue_.cleanupTemporary(); } catch (...) {}
      if (failure == "download-paused" || failure == "owner-stopped")
        queue_.end(token, "paused", failure);
      else
        queue_.recover(token, failure,failedKey);
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
        if (armed_.load()) {
          const auto elapsed = GetTickCount64() - processStartedTick;
          if (elapsed < limits_.startup) {
            queue_.resource("startup-protection", {}, startupProtectionUntil_);
            HANDLE events[] = {stop_, wake_};
            if (WaitForMultipleObjects(
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
            if (WaitForMultipleObjects(2, events, FALSE, interval) ==
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
      } catch (...) {
        delay = retryMs;
      }
      bool slowRecovery=false;
      try{const auto retained=armed_.load()?queue_.snapshot():Json::object();
        slowRecovery=armed_.load()&&downloadsEnabled(retained)&&
            (retained.value("remaining",size_t{0})>0||retained.value("catalogPending",false))&&delay<0;
        if(slowRecovery)delay=limits_.recheck;
      }catch(...){delay=limits_.recheck;}
      HANDLE events[] = {stop_, wake_};
      const auto wait =
          delay < 0 ? INFINITE
                    : static_cast<DWORD>((std::min)(
                          int64_t{3600000}, (std::max)(int64_t{250}, delay)));
      const auto result=WaitForMultipleObjects(2, events, FALSE, wait);
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
  ~Supervisor() {
    SetEvent(stop_);
    if (thread_.joinable())
      thread_.join();
    CloseHandle(stop_);
    CloseHandle(wake_);
  }
  Queue &queue() { return queue_; }
  void wake(bool activate = false) {
    if (activate)
      armed_.store(true);
    SetEvent(wake_);
  }
};
// This is the production command/event boundary used by the native IPC and
// isolated chain acceptance. It never calls the visible library launcher.
class Controller {
  Supervisor owner_;
  Watcher watcher_;
  std::mutex commandMutex_;

public:
  Controller(fs::path root, fs::path executable,
             std::function<void(const Json &)> changed, Limits limits = {},
             ResourcePorts resources = {})
      : owner_(root, std::move(executable), limits, std::move(resources)),
        watcher_(root, L"main",
                 [root, changed] { changed(Queue(root).snapshot()); }) {}
  Json background(const Json &request) {
    if (!request.is_object())
      throw std::runtime_error("media-download-command-invalid");
    std::lock_guard lock(commandMutex_);
    if(request.value("recheck",false))owner_.queue().recheck();
    if (request.contains("enabled"))
      owner_.queue().configure(request.value("enabled", true),
                               request.value("pauseReason", std::string{}),
                               request.value("cpuThreshold", 15));
    if(request.value("catalog",false)&&request.value("enabled",false)) owner_.queue().requestCatalog();
    if(request.value("manual",false))owner_.queue().requestSupplement(true);
    if(request.value("cancelManual",false))owner_.queue().cancelManual();
    if (request.contains("targets"))
      owner_.queue().enqueue(request["targets"]);
    if(request.contains("inventoryRequestId"))owner_.queue().inventoryReady(request.value("inventoryRequestId",std::string{}));
    if (request.contains("enabled")||request.value("manual",false)||request.value("cancelManual",false)||request.value("recheck",false))
      owner_.wake(true);
    return owner_.queue().snapshot();
  }
  Json snapshot() { return owner_.queue().snapshot(); }
};
} // namespace ymcc::media::downloads
