#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media_download_supervisor.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static Json reports = Json::array();
static int checks = 0;
static void check(bool value, const char *why) {
  ++checks;
  if (!value)
    throw std::runtime_error(why);
}
static Json target(int id, int64_t played = 0) {
  return {{"appId", id},
          {"name", "Reverse fixture"},
          {"verified", true},
          {"lastPlayed", played}};
}
static void pair(const fs::path &root, int id) {
  Store store(root);
  auto request = target(id);
  auto lease = store.begin(request);
  fs::create_directories(root);
  const auto video = root / L"sample.mp4", image = root / L"sample.jpg";
  {
    std::ofstream out(video, std::ios::binary);
    out.write("\0\0\0\x18"
              "ftypisom-synthetic",
              21);
  }
  {
    std::ofstream out(image, std::ios::binary);
    out.write("\xff\xd8\xff"
              "fixture",
              10);
  }
  lease = store.publish(lease, video, false);
  lease = store.publish(lease, image, false);
  store.finish(lease);
}
static bool until(const std::function<bool()> &fn, DWORD timeout = 1500) {
  const auto limit = GetTickCount64() + timeout;
  do {
    if (fn())
      return true;
    Sleep(10);
  } while (GetTickCount64() < limit);
  return false;
}
static double processCpu() {
  FILETIME a{}, b{}, kernel{}, user{};
  GetProcessTimes(GetCurrentProcess(), &a, &b, &kernel, &user);
  auto tick = [](FILETIME time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
  };
  return double(tick(kernel) + tick(user)) / 10000.0;
}
static int benchmark(const fs::path &root) {
  if (fs::exists(root))
    throw std::runtime_error("benchmark root exists");
  Json jobs = Json::object();
  for (int n = 1; n <= 10000; ++n)
    jobs["steam-" + std::to_string(n)] = {{"appId", n},
                                          {"name", "Synthetic inventory"},
                                          {"state", "pending"},
                                          {"retryAt", 0},
                                          {"lastPlayed", 10000 - n}};
  atomicWrite(root / L"config" / L"game-media-downloads.json",
              {{"jobs", jobs}, {"enabled", true}});
  Queue queue(root);
  queue.resource("cpu-busy");
  const auto start = GetTickCount64();
  LARGE_INTEGER begin{}, end{}, frequency{};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&begin);
  const auto cpu = processCpu();
  for (int i = 0; i < 100; ++i) {
    if (queue.delay() != 0)
      throw std::runtime_error("pending deadline wrong");
    queue.resource("cpu-busy");
  }
  QueryPerformanceCounter(&end);
  Json report = {
      {"ok", true},
      {"entries", 10000},
      {"iterations", 100},
      {"cpuMs", processCpu() - cpu},
      {"wallMs", GetTickCount64() - start},
      {"wallMicroseconds",
       double(end.QuadPart - begin.QuadPart) * 1000000.0 / frequency.QuadPart},
      {"scope", "same production queue deadline/resource checks, synthetic "
                "10000-entry inventory; not full-system CPU"}};
  atomicWrite(root / L"benchmark.json", report);
  std::cout << report.dump() << std::endl;
  return 0;
}

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--bench")
      return benchmark(fs::absolute(argv[2]));
    if (argc == 5 && std::wstring_view(argv[1]) == L"--media-background") {
      Queue queue(custom_steam_library::configuredDataRoot());
      const auto token = utf8(argv[4]);
      queue.update(token, [](Json &j) {
        j["workerPid"] = GetCurrentProcessId();
        j["workerBirth"] = processBirthToken(GetCurrentProcessId());
      });
      queue.claim(token);
      Sleep(INFINITE);
      return 9;
    }
    if (argc != 2)
      throw std::runtime_error("fresh evidence root required");
    const auto base = fs::absolute(argv[1]);
    if (fs::exists(base))
      throw std::runtime_error("evidence root exists");
    fs::create_directories(base);
    const auto run = [&](const char *name,
                         const std::function<void(const fs::path &)> &fn) {
      try {
        fn(base / wide(name));
        reports.push_back({{"name", name}, {"ok", true}});
      } catch (const std::exception &error) {
        reports.push_back(
            {{"name", name}, {"ok", false}, {"error", error.what()}});
      }
    };
    run("progress-does-not-wake-owner", [](const auto &root) {
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      auto token = q.start();
      q.claim(token);
      HANDLE event = CreateEventW(nullptr, FALSE, FALSE,
                                  eventName(root, L"owner").c_str());
      if (!event)
        throw std::runtime_error("event unavailable");
      ResetEvent(event);
      q.progress(token, "downloading", 1, 100);
      const auto result = WaitForSingleObject(event, 0);
      CloseHandle(event);
      check(result == WAIT_TIMEOUT,
            "byte progress unnecessarily wakes the download supervisor");
    });
    run("explicit-refresh-retains-queue", [](const auto &root) {
      pair(root, 1);
      Store store(root);
      auto entry = store.resolve(target(1));
      entry["action"] = "redownload";
      entry["slot"] = "dynamic";
      store.edit(entry);
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      check(q.snapshot().value("remaining", size_t{0}) == 1,
            "a complete old cache hides an explicit refresh request");
    });
    run("expired-unavailable-stays-stopped", [](const auto &root) {
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      auto token = q.start();
      q.claim(token);
      q.result(token, "steam-1", true, "no-dynamic-video");
      q.update(token, [](Json &j) { j["jobs"]["steam-1"]["retryAt"] = 0; });
      q.enqueue(Json::array({target(1)}));
      check(q.snapshot().value("remaining", size_t{0}) == 0 && q.delay()<0,
            "unavailable item returned to automatic retry after its old deadline");
    });
    run("recover-does-not-penalize-next-game", [](const auto &root) {
      Queue q(root);
      q.enqueue(Json::array({target(1, 200), target(2, 100)}));
      auto token = q.start();
      q.claim(token);
      q.result(token, "steam-1", false, "media-task-timeout");
      q.end(token, "deferred");
      q.recover(token, "download-worker-exited-14");
      check(q.document()["jobs"]["steam-2"].value("attempts", 0) == 0,
            "failure after releasing a task is incorrectly charged to the next "
            "game");
    });
    run("completed-session-is-not-retried", [](const auto &root) {
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      auto token = q.start();
      q.claim(token);
      q.result(token, "steam-1", true);
      q.end(token, "completed");
      check(q.delay() < 0, "a completed batch still has an idle wake deadline");
    });
    run("threshold-change-takes-effect-immediately", [&](const auto &root) {
      fs::create_directories(root);
      wchar_t self[32768]{};
      GetModuleFileNameW(nullptr, self, 32768);
      auto exe = root / L"CustomSteamLibrary.exe";
      fs::copy_file(self, exe);
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundMediaProtocol", 1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      Supervisor owner(
          root, exe, Limits{10000, 20000, 30000, 2000, 0},
          ResourcePorts{[] { return 1; },
                        [](uint64_t) { return std::optional<double>{20.0}; }});
      owner.queue().configure(true, "", 30);
      owner.wake(true);
      check(
          until([&] { return q.snapshot().value("workerPid", DWORD{0}) > 0; }),
          "fixture child did not start");
      const auto pid = q.snapshot().value("workerPid", DWORD{0});
      HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid);
      if (!child)
        throw std::runtime_error("child handle missing");
      owner.queue().configure(true, "", 15);
      owner.wake();
      const auto stopped = until(
          [&] { return WaitForSingleObject(child, 0) == WAIT_OBJECT_0; }, 500);
      CloseHandle(child);
      check(stopped, "lowered CPU threshold waits for the old sampling "
                     "interval before stopping a helper");
    });
    run("pause-phase-survives-progress-merge", [](const auto &root) {
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      auto token = q.start();
      q.claim(token);
      Sleep(20);
      q.progress(token, "downloading", 20, 100);
      q.resource("cpu-busy");
      check(
          q.snapshot().value("phase", std::string{}) == "paused",
          "an old byte snapshot changes an explicit pause back to downloading");
    });
    run("new-refresh-is-not-penalized-by-old-download", [](const auto &root) {
      Store store(root);
      store.registerEntry(target(1));
      Queue q(root);
      q.enqueue(Json::array({target(1)}));
      auto token = q.start();
      q.claim(token);
      auto entry = store.resolve(target(1));
      entry["action"] = "redownload";
      entry["slot"] = "dynamic";
      store.edit(entry);
      q.enqueue(Json::array({target(1)}));
      q.result(token, "steam-1", false, "media-stale-publication");
      const auto job = q.document()["jobs"]["steam-1"];
      check(job.value("attempts", 0) == 0 &&
                job.value("retryAt", int64_t{0}) == 0,
            "old task failure delays a newly requested refresh");
    });
    run("ten-lane-claims-and-selective-recovery", [](const auto &root) {
      Queue q(root);Json targets=Json::array();for(int n=1;n<=12;++n)targets.push_back(target(n,100-n));q.enqueue(targets);
      const auto token=q.start();Json claims=Json::array();for(int n=0;n<10;++n)claims.push_back(q.claim(token));
      check(q.claim(token).is_null(),"eleventh simultaneous claim was admitted");
      check(q.snapshot().value("activeCount",0)==10,"queue did not retain ten owners");
      q.result(token,claims[0]["key"],true);check(q.snapshot().value("activeCount",0)==9,"completion cleared siblings");
      check(q.progress(token,claims[1]["key"],"downloading",10,100),"sibling progress was rejected");
      q.recover(token,"worker-stalled");auto jobs=q.document()["jobs"];
      check(jobs[claims[0]["key"].get<std::string>()].value("state",std::string{})=="done","recovery penalized completed sibling");
      for(size_t n=1;n<claims.size();++n)check(jobs[claims[n]["key"].get<std::string>()].value("attempts",0)==1,"active recovery skipped a lane");
      check(jobs["steam-12"].value("attempts",0)==0,"unclaimed game was penalized");
      check(q.snapshot().value("activeCount",0)==0,"recovered lanes retained ownership");
    });
    run("previous-session-cannot-complete-new-lane", [](const auto &root) {
      Queue q(root);q.enqueue(Json::array({target(1)}));auto old=q.start();auto oldTask=q.claim(old);q.end(old,"paused");
      auto current=q.start();auto task=q.claim(current);q.result(old,oldTask["key"],true);
      check(!q.progress(old,oldTask["key"],"downloading",99,100),"old-session progress was accepted");
      check(q.document()["jobs"]["steam-1"].value("state",std::string{})=="running","old session completed a new claim");
      check(q.snapshot().value("activeCount",0)==1,"old session cleared a new lane");
      q.result(current,task["key"],true);q.end(current,"completed");
    });
    run("storage-fault-has-global-cooldown", [](const auto &root) {
      Queue q(root);q.enqueue(Json::array({target(1)}));auto token=q.start();q.claim(token);q.end(token,"deferred","media-staging-budget-full");
      check(q.start().empty(),"storage fault relaunches helper immediately");
      check(q.delay()>19000,"storage cooldown deadline was not retained");
      q.update(token,[](Json &j){j["cooldownUntil"]=now()-1;});
      check(!q.start().empty(),"storage cooldown permanently disabled recovery");
    });
    run("old-active-session-releases-running-jobs", [](const auto &root) {
      Queue q(root);q.enqueue(Json::array({target(1),target(2)}));auto old=q.start();q.claim(old);q.claim(old);
      q.update(old,[](Json &j){j["session"]="replacement-session";});
      const auto doc=q.document();check(doc["active"].empty(),"prior-session active owners survived migration");
      check(doc["jobs"]["steam-1"].value("state",std::string{})=="pending"&&doc["jobs"]["steam-2"].value("state",std::string{})=="pending","prior running jobs stayed wedged");
      const auto token=q.start();const auto state=q.snapshot();
      check(state.value("ownerProtocol",0)==2&&state.value("ownerProtocolSession",std::string{})==token,"parallel admission not bound to new session");
    });
    run("recent-only-order-and-old-backlog", [](const auto &root) {
      Queue q(root);auto old=target(1091500,999999);q.enqueue(Json::array({old}));
      auto native=target(10,1000);native["sourceTier"]=1;native["catalogSource"]="steam-native";
      auto confirmed=target(20,9999);confirmed["sourceTier"]=2;
      auto popular=target(570,9999999);popular["sourceTier"]=0;popular["catalogSource"]="popular";
      old["sourceTier"]=2;q.enqueue(Json::array({confirmed,native,popular,old}));auto token=q.start();
      for(int expected:{1091500,20,10}){auto job=q.claim(token);check(job.value("appId",0)==expected,"recent play order did not win across sources");q.result(token,job["key"],true);}
      check(!q.document()["jobs"].contains("steam-570"),"popular item entered local queue");q.end(token,"completed");
    });
    run("empty-library-never-discovers-popular", [](const auto& root){Queue q(root);q.requestCatalog();check(q.delay()<0&&q.start().empty(),"empty library started a popular worker");q.requestCatalog();check(q.document()["jobs"].empty()&&!q.catalogDue(""),"legacy request reactivated catalog");});
    run("retired-catalog-does-not-change-local-backoff", [](const auto& root){Queue q(root);q.enqueue(Json::array({target(570)}));auto token=q.start();q.claim(token);q.result(token,"steam-570",false,"media-http-503");const auto before=q.document()["jobs"];q.requestCatalog();q.catalogResult(token,false,"worker-stalled");check(q.document()["jobs"]==before,"retired catalog altered local attempts");q.end(token,"deferred");});
    run("retired-popular-feed-cannot-demote-local", [](const auto&root){Queue q(root);auto native=target(570,100);native["sourceTier"]=1;q.enqueue(Json::array({native}));auto hot=target(730,99999);hot["sourceTier"]=0;hot["catalogSource"]="popular";q.enqueueCatalog(Json::array({hot}));check(q.document()["jobs"].size()==1,"popular compatibility request added work");auto token=q.start();check(q.claim(token).value("appId",0)==570,"local game displaced by retired popular feed");q.end(token,"completed");});
    bool ok = true;
    for (const auto &report : reports)
      ok = ok && report["ok"].get<bool>();
    const Json summary = {
        {"ok", ok},
        {"checks", checks},
        {"cases", reports},
        {"scope", "production queue and supervisor reverse cases, isolated "
                  "cache/processes; no installed Steam or devices"}};
    atomicWrite(base / L"results.json", summary);
    std::cout << summary.dump() << std::endl;
    return ok ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
    return 2;
  }
}
