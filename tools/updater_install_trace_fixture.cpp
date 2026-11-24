#define NOMINMAX
#include "update_install_recovery.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    using namespace ymcc::updateconfig;
    if (argc != 2) return 2;
    const auto root = std::filesystem::path(argv[1]);
    auto readCase = [&](const wchar_t* name) {
        return read((root / name / L"test-journal.json.install-trace.json").wstring(), 4096);
    };
    unsigned passed = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) throw std::runtime_error(name);
        ++passed;
    };
    try {
        Json journal = {{"operationId", "private-operation"}, {"version", "0.0.41"},
                        {"sha256", std::string(64, 'A')}, {"authorizedAt", 1000}};
        auto image = readCase(L"early-exit");
        auto out = exportInstallTrace(image, journal);
        check(out.at("status") == "matched" && out.at("step") == "await-handshake" &&
              out.at("errorKind") == "startup-exited" && out.at("exitCode") == 73 &&
              out.at("rollback") == "succeeded" && out.at("relaunch") == "started", "early exit projection");
        auto launch = exportInstallTrace(readCase(L"launch-fails"), journal);
        check(launch.at("errorKind") == "startup-launch" && launch.at("win32Code") == 5, "launch error code");
        check(exportInstallTrace(readCase(L"timeout"), journal).at("errorKind") == "handshake-timeout", "timeout projection");
        check(exportInstallTrace(readCase(L"commit"), journal).at("outcome") == "committed", "commit projection");
        image.value["path"] = "C:/private/data";
        image.value["message"] = "https://invalid.example/private-token";
        image.value["repairAuthorized"] = true;
        check(exportInstallTrace(image, journal) == out && !out.contains("operationId") &&
              !out.contains("sha256") && !out.contains("authorizedAt"), "private and authority fields are not exported");
        auto newer = journal;
        newer["authorizedAt"] = 1001;
        check(exportInstallTrace(image, newer) == Json{{"status", "superseded"}}, "same version and operation retried");
        newer = journal;
        newer["sha256"] = std::string(64, 'B');
        check(exportInstallTrace(image, newer) == Json{{"status", "superseded"}}, "mismatched package");
        check(exportInstallTrace(image, Json::object()) == Json{{"status", "uncorrelated"}}, "journal absent");
        check(exportInstallTrace(readCase(L"missing"), journal) == Json{{"status", "missing"}}, "snapshot absent");
        check(exportInstallTrace(readCase(L"blocked-write"), journal) == Json{{"status", "damaged"}}, "damaged JSON");
        check(exportInstallTrace(Image{}, journal) == Json{{"status", "unavailable"}}, "unreadable snapshot");
        image.value["errorKind"] = "private arbitrary text";
        check(exportInstallTrace(image, journal) == Json{{"status", "damaged"}}, "unknown label not copied");
        std::cout << "PASS: " << passed << " native trace projection checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
