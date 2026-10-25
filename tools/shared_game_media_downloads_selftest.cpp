#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_supervisor.h"
#include "../native/shared_game_media_download_worker.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static int checks = 0;
static void check(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
static Json target(int id, int64_t played = 0) {
  return {{"appId", id},
          {"name", "Fixture " + std::to_string(id)},
          {"verified", true},
          {"lastPlayed", played}};
}
static bool waitUntil(const std::function<bool()> &fn, DWORD timeout = 8000) {
  const auto end = GetTickCount64() + timeout;
  do {
    if (fn())
      return true;
    Sleep(20);
  } while (GetTickCount64() < end);
  return false;
}
static int fakeChild(int argc, wchar_t **argv) {
  const auto root = custom_steam_library::configuredDataRoot();
  Queue queue(root);
  const auto token = utf8(argv[4]);
  auto mode = read(root / L"fixture.json").value("mode", std::string{});
  if (mode == "production") {
    return backgroundEntrypoint(root).value_or(99);
  }
  queue.update(token, [](Json &j) {
    j["workerPid"] = GetCurrentProcessId();
    j["workerBirth"] = processBirthToken(GetCurrentProcessId());
  });
  atomicWrite(root / L"started.json",
              {{"pid", GetCurrentProcessId()},
               {"birth", processBirthToken(GetCurrentProcessId())}});
  if (mode == "preclaim-crash")
    return 78;
  auto claimed = queue.claim(token);
  if (mode == "stall")
    Sleep(INFINITE);
  if (mode == "crash")
    return 77;
  if (mode == "complete") {
    queue.result(token, claimed["key"], true);
    queue.end(token, "completed");
    return 0;
  }
  return 98;
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc > 1 && std::wstring_view(argv[1]) == L"--media-background")
      return fakeChild(argc, argv);
    const auto base = fs::temp_directory_path() /
                      wide("YMCC-MediaDownloads-Selftest-" + randomId());
    fs::create_directories(base);
    struct Cleanup {
      fs::path root;
      ~Cleanup() {
        std::error_code ec;
        if (fs::absolute(root).parent_path() ==
                fs::absolute(fs::temp_directory_path()) &&
            root.filename().wstring().starts_with(
                L"YMCC-MediaDownloads-Selftest-"))
          fs::remove_all(root, ec);
      }
    } cleanup{base};
    Queue q(base / L"queue");
    check(q.snapshot().value("enabled", false),
          "background download defaults on");
    q.enqueue(Json::array(
        {target(1, 100), target(2, 500), target(3, 200), target(4)}));
    check(q.snapshot()["total"] == 4, "queue persists inventory");
    q.enqueue(Json::array({target(2, 700), target(1, 50)}));
    check(q.snapshot()["total"] == 4, "dedup keeps one job per game");
    auto token = q.start();
    auto first = q.claim(token);
    check(first["appId"] == 2, "newest played missing game is first");
    const auto diskStamp = fs::last_write_time(q.root() / L"config" /
                                               L"game-media-downloads.json");
    check(q.progress(token, "downloading", 100, 200),
          "active owner reports bytes");
    check(fs::last_write_time(q.root() / L"config" /
                              L"game-media-downloads.json") == diskStamp,
          "byte progress does not rewrite durable queue");
    check(q.snapshot()["bytes"] == 100 && q.snapshot()["totalBytes"] == 200,
          "small status visible to both UIs");
    check(!q.progress("old-owner", "downloading", 999, 999),
          "stale owner cannot write progress");
    q.configure(false, "");
    check(!q.allowed(token) && !q.progress(token, "downloading", 150, 200),
          "switch off cancels live work");
    check(q.delay() < 0, "disabled owner has no wake timer");
    q.configure(true, "");
    q.result(token, first["key"], true);
    auto second = q.claim(token);
    check(second["appId"] == 3, "next recent game before unplayed");
    q.recover(token, "worker-stalled");
    auto document = q.document();
    check(document["jobs"]["steam-3"]["attempts"] == 1,
          "crash recovery records attempt");
    check(document["jobs"]["steam-3"]["retryAt"].get<int64_t>() > now() + 19000,
          "crash uses a backoff not immediate respawn");
    q.result(token, "steam-3", false, "again");
    q.result(token, "steam-3", false, "again");
    check(q.document()["jobs"]["steam-3"].value("autoStopped",false),
          "repeated failure stops that item after its finite budget");
    q.configure(true, "game-running");
    check(q.start().empty() && q.delay() < 0,
          "game policy pause retains jobs and exits");
    q.configure(true, "");
    check(Queue(q.root()).snapshot()["total"] == 4,
          "new process sees same durable queue");
    check(std::string(expiry(89999, 0, 0, 0)).empty(), "stall not early");
    check(std::string(expiry(90000, 0, 0, 0)) == "worker-stalled",
          "90 second actual stall boundary");
    check(std::string(expiry(240001, 0, 239999, 1)) == "task-timeout",
          "4 minute task boundary despite progress");
    check(std::string(expiry(900000, 0, 899999, 899999)) == "session-timeout",
          "15 minute session boundary");
    check(sourceGameMatches(L"/store_trailers/730/master.m3u8","steam-730"),"matching modern trailer owner accepted");
    check(!sourceGameMatches(L"/store_trailers/570/master.m3u8","steam-730"),"different game trailer owner rejected");
    check(!sourceGameMatches(L"/%73tore_trailers/570/master.m3u8","steam-730"),"encoded path cannot hide another game owner");
    check(!sourceGameMatches(L"/store_trailers/%35%37%30/master.m3u8","steam-730"),"encoded AppID cannot hide another game owner");
    check(!sourceGameMatches(L"/store_trailers/730/../../store_trailers/570/master.m3u8","steam-730"),"parent segments cannot escape to another game");
    check(sourceGameMatches(L"/store_trailers/730/480/../init.mp4","steam-730"),"same-game relative path remains valid");
    check(sourceGameMatches(L"/steam/apps/256972298/movie480.webm","steam-730"),"legacy movie ID path keeps metadata-based ownership");
    check(sourceGameMatches(L"/api/appdetails","steam-730"),"metadata endpoint remains valid");
    check(!sourceGameMatches(L"/store_trailers/570/master.m3u8","steam-578080"),"similar numeric game IDs never match by prefix");
    auto plan = hlsPlan("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=800000,"
                        "RESOLUTION=1280x720\n720.m3u8\n#EXT-X-STREAM-INF:"
                        "BANDWIDTH=400000,RESOLUTION=854x480\n480.m3u8\n",
                        "https://video.steamstatic.com/app/master.m3u8");
    check(plan.variant.ends_with("720.m3u8"), "HLS selects the highest available variant up to 1080p");
    plan = hlsPlan("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:2,\npart."
                   "m4s\n#EXT-X-ENDLIST\n",
                   "https://video.steamstatic.com/app/index.m3u8");
    check(plan.segments.size() == 2 && plan.segments[0].ends_with("init.mp4"),
          "HLS stores init and media bytes offline");
    for (const auto &text :
         {std::string{"#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\nseg.m4s\n"},
          std::string{
              "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n#EXT-X-ENDLIST\n"}}) {
      bool denied = false;
      try {
        hlsPlan(text, "https://video.steamstatic.com/a.m3u8");
      } catch (...) {
        denied = true;
      }
      check(denied, "live or encrypted stream rejected");
    }
    for (const auto *url : {"https://evil.invalid/a.mp4",
                            "https://steamstatic.com.evil.invalid/a.mp4",
                            "https://user:pass@video.steamstatic.com/a.mp4",
                            "https://video.steamstatic.com:444/a.mp4",
                            "http://video.steamstatic.com/a.mp4"}) {
      bool denied = false;
      try {
        sourceUrl(url);
      } catch (...) {
        denied = true;
      }
      check(denied, "untrusted media source denied");
    }
    CpuGate gate;
    check(std::string(gate.observe(std::nullopt, 15, 1000)) == "cpu-unknown",
          "unknown CPU never allows a download");
    check(std::string(gate.observe(5.0, 15, 2000)).empty(),
          "normal load permits downloads");
    check(std::string(gate.observe(16.0, 15, 3000)) == "cpu-busy",
          "default 15 percent boundary pauses");
    check(std::string(gate.observe(14.5, 15, 4000)) == "cpu-busy",
          "hysteresis prevents threshold flapping");
    check(std::string(gate.observe(13.0, 15, 5000)) == "cpu-recovering",
          "calm period begins below the proportional recovery margin");
    check(std::string(gate.observe(13.0, 15, 14999)) == "cpu-recovering",
          "cannot resume before ten calm seconds");
    check(std::string(gate.observe(13.0, 15, 15000)).empty(),
          "resumes after ten calm seconds");
    check(std::string(gate.observe(25.0, 30, 21000)).empty(),
          "user threshold is configurable");
    wchar_t self[32768]{};
    GetModuleFileNameW(nullptr, self, 32768);
    const auto original = custom_steam_library::configuredDataRoot();
    for (const auto *mode : {"stall", "crash", "preclaim-crash", "unsupported",
                             "complete", "production"}) {
      const auto root = base / wide(mode);
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      atomicWrite(root / L"fixture.json", {{"mode", mode}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      fs::copy_file(self, root / L"CustomSteamLibrary.exe");
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundMediaProtocol",
                    std::string(mode) == "unsupported" ? 0 : 1}});
      if (std::string(mode) == "production") {
        Store store(root);
        auto lease = store.begin(target(570));
        auto video = root / L"fixture.mp4", poster = root / L"fixture.jpg";
        {
          std::ofstream out(video, std::ios::binary);
          out.write("\0\0\0\x18"
                    "ftypisom-fixture",
                    20);
        }
        {
          std::ofstream out(poster, std::ios::binary);
          out.write("\xff\xd8\xff"
                    "fixture",
                    11);
        }
        // This case tests an already-current complete cache; legacy quality
        // migration is covered by shared_game_media_quality_selftest.cpp.
        lease["videoQualityPolicy"] = videoQualityPolicy;
        lease = store.publish(lease, video, false);
        lease = store.publish(lease, poster, false);
        store.finish(lease);
      }
      {
        Supervisor owner(
            root, root / L"CustomSteamLibrary.exe",
            Limits{250, 1200, 2000, 50, 0},
            ResourcePorts{[] { return 1; },
                          [](uint64_t) { return std::optional<double>{0.0}; }});
        owner.wake(false);
        Sleep(50);
        check(!fs::exists(root / L"started.json"),
              "unarmed inventory event cannot start a helper before settings "
              "load");
        owner.queue().configure(true, "");
        owner.wake(true);
        if (std::string(mode) == "production" ||
            std::string(mode) == "complete") {
          check(waitUntil([&] {
                  return queue.snapshot().value("phase", std::string{}) ==
                         "completed";
                }),
                "completed worker self exits");
          check(queue.snapshot()["workerPid"] == 0 &&
                    queue.snapshot()["remaining"] == 0,
                "no process parked after completion");
        } else {
          check(waitUntil([&] {
                  return queue.document()["jobs"]["steam-570"].value("attempts",0)>0 || queue.snapshot().value("reason",std::string{})=="download-worker-upgrade-required";
                }),
                "crashed or stalled process is recovered");
          if (std::string(mode) == "unsupported") {
            check(!fs::exists(root / L"started.json"),
                  "old library cannot open a window for an unknown flag");
            continue;
          }
          auto started = read(root / L"started.json");
          const auto pid = started.value("pid", DWORD{0});
          HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
          check(!process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0,
                "only owned stalled child terminated");
          if (process)
            CloseHandle(process);
          check(queue.delay() > 19000,
                "recovery leaves process exited during backoff");
        }
      }
    }
    // The startup gate is independent of user settings and prevents even
    // CPU reads or process startup during its mandatory interval.
    {
      const auto root = base / L"startup";
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      std::atomic<int> reads{0};
      Supervisor owner(root, root / L"missing.exe",
                       Limits{5000, 10000, 20000, 50, 600000},
                       ResourcePorts{[] { return 1; },
                                     [&](uint64_t) {
                                       ++reads;
                                       return std::optional<double>{0.0};
                                     }});
      owner.queue().configure(true, "");
      owner.wake(true);
      check(waitUntil([&] {
              return queue.snapshot().value("resourcePauseReason",
                                            std::string{}) ==
                     "startup-protection";
            }),
            "mandatory startup protection precedes work");
      check(reads.load() == 0 &&
                queue.snapshot().value("workerPid", DWORD{0}) == 0,
            "boot protection does not sample CPU or launch a helper");
    }
    // Expiry must publish independently of queued jobs or a second command.
    // Accelerate only the injected test limit; production remains 30000 ms.
    {
      const auto root=base/L"startup-expiry-empty";
      std::atomic<int> reads{0};std::atomic<bool> protectedEvent{false},releasedEvent{false};
      const auto startup=GetTickCount64()-processStartedTick+350;
      Controller controller(root,root/L"missing.exe",[&](const Json& state){
        if(state.value("resourcePauseReason",std::string{})=="startup-protection")protectedEvent=true;
        else if(protectedEvent.load())releasedEvent=true;
      },Limits{5000,10000,20000,50,startup},ResourcePorts{[]{return 1;},[&](uint64_t){++reads;return std::optional<double>{0.0};}});
      controller.background({{"enabled",true},{"pauseReason","game-running"}});
      check(waitUntil([&]{return protectedEvent.load();}),"empty queue publishes startup protection");
      auto state=controller.snapshot();
      check(state.value("startupProtectionUntil",int64_t{0})>now(),"native startup deadline is exported");
      check(waitUntil([&]{return releasedEvent.load();}),"deadline publishes release without queued work or a command");
      state=controller.snapshot();
      check(state.value("resourcePauseReason",std::string{}).empty()&&state.value("startupProtectionUntil",int64_t{-1})==0,"deadline clears native protection and its metadata");
      check(state.value("pauseReason",std::string{})=="game-running"&&reads.load()==0&&state.value("workerPid",DWORD{0})==0,"empty startup recovery preserves game admission and performs no CPU sampling or launch");
      check(state["admissionHistory"].size()>=2,"export retains startup transition evidence");
      const auto revision=state.value("statusRevision",uint64_t{0});Sleep(80);
      check(controller.snapshot().value("statusRevision",uint64_t{0})==revision,"idle protection release does not rewrite snapshots repeatedly");
      Queue queue(root);
      for(int n=0;n<30;++n)queue.configure(true,n%2?"game-running":"");
      check(queue.snapshot()["admissionHistory"].size()==12,"admission evidence stays bounded");
      const auto before=queue.snapshot().value("statusRevision",uint64_t{0});queue.configure(true,"game-running");queue.resource("");
      check(queue.snapshot().value("statusRevision",uint64_t{0})==before,"duplicate policy and resource checks remain no-ops");
    }
    for(const bool disabled:{false,true}) {
      const auto root=base/(disabled?L"startup-expiry-disabled":L"startup-expiry-queued");
      Queue queue(root);queue.enqueue(Json::array({target(570)}));
      atomicWrite(root/L"fixture.json",{{"mode","complete"}});
      fs::copy_file(self,root/L"CustomSteamLibrary.exe");
      atomicWrite(root/L"package-manifest.json",{{"backgroundMediaProtocol",1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
      std::atomic<int> reads{0};const auto startup=GetTickCount64()-processStartedTick+350;
      Controller controller(root,root/L"CustomSteamLibrary.exe",[](const Json&){},
        Limits{5000,10000,20000,50,startup},ResourcePorts{[]{return 1;},[&](uint64_t){++reads;return std::optional<double>{0.0};}});
      controller.background({{"enabled",true},{"pauseReason",""}});
      check(waitUntil([&]{return controller.snapshot().value("resourcePauseReason",std::string{})=="startup-protection";}),"retained queue publishes protection");
      check(!fs::exists(root/L"started.json")&&reads.load()==0,"retained jobs cannot bypass protection");
      if(disabled){
        controller.background({{"enabled",false},{"pauseReason",""}});
        check(waitUntil([&]{return controller.snapshot().value("resourcePauseReason",std::string{})!="startup-protection";}),"disabled status also releases expired startup label");
        check(!fs::exists(root/L"started.json")&&reads.load()==0&&!controller.snapshot().value("enabled",true),"deadline never overrides saved OFF or launches a worker");
      }else{
        check(waitUntil([&]{return controller.snapshot().value("phase",std::string{})=="completed";}),"retained queue starts automatically after deadline without another UI command");
        check(controller.snapshot().value("remaining",size_t{1})==0&&controller.snapshot().value("workerPid",DWORD{0})==0,"deadline-triggered worker completes and exits");
      }
    }
    // Independent process handles verify a CPU/battery pause actually ends
    // the running helper and leaves no helper while blocked.
    for (const auto *mode : {"battery-pause", "cpu-pause"}) {
      const auto root = base / wide(mode);
      Queue queue(root);
      queue.enqueue(Json::array({target(570)}));
      atomicWrite(root / L"fixture.json", {{"mode", "stall"}});
      fs::copy_file(self, root / L"CustomSteamLibrary.exe");
      atomicWrite(root / L"package-manifest.json",
                  {{"backgroundMediaProtocol", 1}});
      SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                              root.c_str());
      std::atomic<int> ac{1};
      std::atomic<double> cpu{0};
      Supervisor owner(root, root / L"CustomSteamLibrary.exe",
                       Limits{5000, 10000, 20000, 50, 0},
                       ResourcePorts{[&] { return ac.load(); },
                                     [&](uint64_t) {
                                       return std::optional<double>{cpu.load()};
                                     }});
      owner.queue().configure(true, "");
      owner.wake(true);
      check(waitUntil([&] { return fs::exists(root / L"started.json"); }),
            "resource fixture starts an owned helper");
      auto info = read(root / L"started.json");
      HANDLE process =
          OpenProcess(SYNCHRONIZE, FALSE, info.value("pid", DWORD{0}));
      check(process != nullptr, "retain exact running helper identity");
      if (std::string(mode) == "battery-pause")
        ac.store(0);
      else
        cpu.store(25);
      owner.wake();
      check(waitUntil([&] {
              return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
            }),
            "power or CPU pressure ends running helper");
      CloseHandle(process);
      check(queue.snapshot().value("resourcePauseReason", std::string{}) ==
                (std::string(mode) == "battery-pause" ? "on-battery"
                                                      : "cpu-busy"),
            "shared progress explains resource pause");
      check(queue.snapshot().value("workerPid", DWORD{0}) == 0,
            "no worker parked during resource pause");
    }
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",
                            original.empty() ? nullptr : original.c_str());
    std::cout << Json({{"ok", true},
                       {"checks", checks},
                       {"scope",
                        "production durable queue/HLS/source rules, real "
                        "isolated child completion/crash/stall processes; no "
                        "Steam, devices, input or external network"}})
                     .dump()
              << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
