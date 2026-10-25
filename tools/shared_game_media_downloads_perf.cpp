#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_supervisor.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static double cpuMs(HANDLE process) {
  FILETIME a{}, b{}, kernel{}, user{};
  if (!GetProcessTimes(process, &a, &b, &kernel, &user))
    throw std::runtime_error("perf-process-times-failed");
  auto ticks = [](FILETIME ft) {
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
  };
  return double(ticks(kernel) + ticks(user)) / 10000.0;
}
static Json target() {
  return {{"appId", 570},
          {"name", "Dota 2"},
          {"verified", true},
          {"lastPlayed", now()}};
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 3 && argc != 4)
      throw std::runtime_error(
          "requires fresh output directory and compiled library host");
    const auto threshold = argc == 4 ? (std::clamp)(_wtoi(argv[3]), 5, 90) : 15;
    const auto root = fs::absolute(argv[1]);
    if (fs::exists(root))
      throw std::runtime_error("perf-directory-already-exists");
    fs::create_directories(root);
    const auto host = root / L"CustomSteamLibrary.exe";
    fs::copy_file(fs::absolute(argv[2]), host);
    atomicWrite(root / L"package-manifest.json",
                {{"backgroundMediaProtocol", 1}});
    const auto data = root / L"download-data";
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT", data.c_str());
    const auto processors =
        (std::max)(DWORD{1}, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    Json report = {
        {"processors", processors},
        {"batteryWattsMeasured", false},
        {"scope",
         "isolated actual library headless worker, official Steam video, own "
         "cache only; playback and Steam processes unchanged"}};
    Queue queue(data);
    queue.enqueue(Json::array({target()}));
    const auto start = GetTickCount64();
    const auto cpuStart = cpuMs(GetCurrentProcess());
    bool completed = false;
    std::string previous;
    DWORD workerPid = 0, priority = 0;
    size_t childWindows = 0;
    Json resources = Json::array();
    {
      Supervisor owner(data, host);
      owner.queue().configure(true, "", threshold);
      owner.wake(true);
      for (;;) {
        Sleep(1000);
        auto state = queue.snapshot();
        if (state.value("workerPid", DWORD{0})) {
          workerPid = state.value("workerPid", DWORD{0});
          HANDLE child =
              OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, workerPid);
          if (child) {
            priority = GetPriorityClass(child);
            CloseHandle(child);
          }
          std::pair<DWORD, size_t> windows{workerPid, 0};
          EnumWindows(
              [](HWND window, LPARAM arg) -> BOOL {
                auto &context =
                    *reinterpret_cast<std::pair<DWORD, size_t> *>(arg);
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (pid == context.first)
                  ++context.second;
                return TRUE;
              },
              reinterpret_cast<LPARAM>(&windows));
          childWindows = (std::max)(childWindows, windows.second);
        }
        const auto phase = state.value("phase", std::string{}) + ":" +
                           state.value("resourcePauseReason", std::string{});
        if (phase != previous) {
          std::cout << Json({{"phase", phase},
                             {"elapsedMs", GetTickCount64() - start},
                             {"bytes", state.value("bytes", uint64_t{0})}})
                           .dump()
                    << std::endl;
          previous = phase;
        }
        if (state.value("remaining", size_t{1}) == 0 &&
            state.value("workerPid", DWORD{0}) == 0 &&
            state.contains("lastWorkerMetrics")) {
          completed = true;
          report["worker"] = state["lastWorkerMetrics"];
          break;
        }
        if (GetTickCount64() - start > 180000) {
          report["downloadState"] = state;
          break;
        }
      }
    }
    report["downloadCompleted"] = completed;
    report["observedWorkerPid"] = workerPid;
    report["observedPriorityClass"] = priority;
    report["childWindowCount"] = childWindows;
    report["activeSupervisorCpuMs"] = cpuMs(GetCurrentProcess()) - cpuStart;
    report["activeWallMs"] = GetTickCount64() - start;
    report["activeSupervisorCpuPercent"] =
        report["activeSupervisorCpuMs"].get<double>() * 100.0 /
        double(report["activeWallMs"].get<uint64_t>()) / processors;
    auto entries = Store(data).snapshot()["entries"];
    report["staticReady"] =
        entries.value("steam-570", Json::object()).value("staticReady", false);
    report["dynamicReady"] =
        entries.value("steam-570", Json::object()).value("dynamicReady", false);
    report["assets"]=entries.value("steam-570",Json::object()).value("assets",Json::object());
    if(completed&&priority!=IDLE_PRIORITY_CLASS)throw std::runtime_error("download-priority-is-not-idle");
    if (completed && !report["dynamicReady"].get<bool>())
      throw std::runtime_error(
          "no-video-does-not-prove-active-download-performance");
    for (const auto *label :
         {"idle-empty", "battery-backlog", "cpu-paused-backlog"}) {
      const auto lane = root / wide(label);
      Queue pending(lane);
      if (std::string(label) != "idle-empty")
        pending.enqueue(Json::array({target()}));
      std::atomic<int> reads{0};
      ResourcePorts ports{
          [label] { return std::string(label) == "battery-backlog" ? 0 : 1; },
          [&](uint64_t) {
            reads.fetch_add(1);
            return std::optional<double>{80.0};
          }};
      Supervisor owner(lane, host, Limits{90000, 240000, 900000, 5000, 0},
                       ports);
      owner.queue().configure(true, "", 15);
      owner.wake(true);
      Sleep(200);
      const auto before = cpuMs(GetCurrentProcess());
      const auto wall = GetTickCount64();
      Sleep(6000);
      const auto elapsed = GetTickCount64() - wall;
      const auto used = cpuMs(GetCurrentProcess()) - before;
      auto state = pending.snapshot();
      report["standby"][label] = {
          {"wallMs", elapsed},
          {"cpuMs", used},
          {"cpuPercent", used * 100.0 / double(elapsed) / processors},
          {"cpuReads", reads.load()},
          {"workerPid", state.value("workerPid", DWORD{0})},
          {"resourcePauseReason",
           state.value("resourcePauseReason", std::string{})}};
    }
    atomicWrite(root / L"cpu-measurements.json", report);
    std::cout << report.dump() << std::endl;
    return completed ? 0 : 2;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
