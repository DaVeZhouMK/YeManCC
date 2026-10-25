#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../native/shared_game_media_download_supervisor.h"
#include "../native/shared_game_media_server.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;
static std::mutex outputMutex;
static void output(const Json &message) {
  std::lock_guard lock(outputMutex);
  std::cout << message.dump() << std::endl;
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("isolated root required");
    const auto root = fs::absolute(argv[1]), data = root / L"cache";
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT", data.c_str());
    Server server;
    Store store(data);
    std::atomic<int> ac{0};
    std::atomic<double> cpu{0};
    Controller controller(
        data, root / L"CustomSteamLibrary.exe",
        [](const Json &state) {
          output({{"event", "gameMedia.downloadsChanged"}, {"data", state}});
        },
        Limits{90000, 240000, 900000, 250, 250},
        ResourcePorts{
            [&] { return ac.load(); },
            [&](uint64_t) { return std::optional<double>{cpu.load()}; }});
    ymcc::media::Watcher assets(
        data,
        [] {
          output({{"event", "gameMedia.changed"}, {"data", Json::object()}});
        },
        L"main");
    const auto describe = [&](Json entry) {
      if (entry.contains("asset") && !entry.value("deleted", false))
        entry["url"] = server.expose(store, entry);
      for (const auto *slot : {"static", "dynamic"})
        if (entry.value(std::string(slot) + "Ready", false)) {
          auto variant = entry;
          variant["asset"] = entry["assets"][slot];
          entry[std::string(slot) + "Url"] = server.expose(store, variant);
        }
      return entry;
    };
    output(
        {{"event", "chain.ready"},
         {"data", {{"isolated", true}, {"acceleratedResourceClock", true}}}});
    for (std::string line; std::getline(std::cin, line);) {
      Json request;
      try {
        request = Json::parse(line);
        const auto command = request.value("command", std::string{});
        const auto args = request.value("args", Json::object());
        Json result;
        if (command == "gameMedia.background")
          result = controller.background(args);
        else if (command == "gameMedia.downloads")
          result = controller.snapshot();
        else if (command == "gameMedia.resolve")
          result = describe(store.resolve(args));
        else if (command == "gameMedia.snapshot")
          result = store.snapshot();
        else if (command == "test.controls") {
          ac.store(args.value("ac", ac.load()));
          cpu.store(args.value("cpu", cpu.load()));
          const auto state = controller.snapshot();
          result = controller.background(
              {{"enabled", state.value("enabled", true)},
               {"pauseReason", state.value("pauseReason", std::string{})},
               {"cpuThreshold", state.value("cpuThreshold", 15)}});
        } else if (command == "test.libraryState") {
          result = {{"config", {{"roots", Json::array()}}},
                    {"manualOverrides", {{"items", Json::object()}}},
                    {"library", {{"games", Json::array()}}},
                    {"steamPlan",
                     {{"items", Json::array()}, {"summary", Json::object()}}},
                    {"gameMedia", store.snapshot()},
                    {"mediaDownloads", controller.snapshot()}};
          for (auto &entry : result["gameMedia"]["entries"]) {
            entry = describe(entry);
            entry["previewUrl"] = entry.value("url", std::string{});
            for (const auto *slot : {"static", "dynamic"})
              if (entry.contains(std::string(slot) + "Url"))
                entry[std::string(slot) + "PreviewUrl"] =
                    entry[std::string(slot) + "Url"];
          }
        } else if (command == "test.quit") {
          output({{"id", request["id"]}, {"result", true}});
          break;
        } else
          throw std::runtime_error(
              "command not allowed by isolated chain host");
        output({{"id", request["id"]}, {"result", result}});
      } catch (const std::exception &error) {
        output({{"id", request.value("id", 0)}, {"error", error.what()}});
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
