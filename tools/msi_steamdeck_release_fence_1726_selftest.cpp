// Consumes real JSON serialized by extracted Host production session methods.
// Header under test is the parent's exact native parser, with no product APIs.
#include <json.hpp>
#include <msi_steamdeck_release_fence_1726.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using json = nlohmann::json;
using Session = ymcc::msi::sdRelease1726::Session;
static int checks = 0;
static void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
static json load(const char* file) { std::ifstream stream(file); if (!stream) throw std::runtime_error("missing Host wire fixture"); json result; stream >> result; return result; }
static Session prepared(const json& wire) {
    Session session; session.hello(wire.at("hello"));
    const auto& e = wire.at("prepare").at("msiSteamDeckReleaseEvidence");
    check(session.begin(true, "steamdeck", e.at("runId").get<std::string>(), e.at("epoch").get<uint64_t>()), "negotiated opt-in");
    check(session.acceptPrepare(wire.at("prepare")), "actual Host PREPARE pin parsed by native");
    return session;
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("success and failure fixture arguments required");
        auto success = load(argv[1]), failure = load(argv[2]);
        for (int i = 0; i < 100; ++i) {
            auto session = prepared(success);
            check(session.pinned() && !session.verified("exact-msi-run", 17), "pin is not release absence");
            check(session.acceptRelease(success.at("release")), "actual Host RELEASE native accepts");
            check(session.verified("exact-msi-run", 17), "exact numeric tuple verified");
            check(!session.verified("other-run", 17) && !session.verified("exact-msi-run", 18), "stale tuple rejected");
            check(success.at("prepare").at("msiSteamDeckReleaseEvidence").at("ownedTargets") == success.at("release").at("msiSteamDeckReleaseEvidence").at("ownedTargets"), "exact pin set retained");
            check(success.at("release").at("msiSteamDeckReleaseEvidence") == success.at("shutdown").at("msiSteamDeckReleaseEvidence"), "successful shutdown receipt immutable");
        }
        for (int mode = 0; mode < 13; ++mode) {
            auto session = prepared(success); auto receipt = success.at("release");
            auto& e = receipt["msiSteamDeckReleaseEvidence"];
            switch (mode) {
                case 0: e["version"] = 2; break;
                case 1: e["epoch"] = "17"; break;
                case 2: e["epoch"] = 17.0; break;
                case 3: e["runId"] = "stale-run"; break;
                case 4: e["probeSucceeded"] = false; break;
                case 5: e["absentVerified"] = false; break;
                case 6: e["remainingOwnedTargetCount"] = 1; break;
                case 7: e["required"] = "true"; break;
                case 8: e["ownedTargetCount"] = 99; break;
                case 9: e["ownedTargets"][0] = "USB\\VID_28DE&PID_1205\\FOREIGN"; break;
                case 10: e["profileIdentity"] = "wrong-profile"; break;
                case 11: e["persona"] = "dualshock4"; break;
                case 12: e["ownedTargets"][1] = e["ownedTargets"][0]; break;
            }
            check(!session.acceptRelease(receipt), "mutated foreign/stale/bad evidence rejected");
        }
        auto failed = prepared(success);
        check(failed.acceptPrepare(failure.at("prepare")), "failure wire has valid prepare ownership");
        check(!failure.at("release").at("ok").get<bool>(), "Host failed RELEASE cannot authorize unhide");
        check(!failed.acceptRelease(failure.at("release")), "owned-still-present negative accepted as failure");
        check(!failed.acceptRelease(failure.at("stillPresent")), "failed SHUTDOWN cannot replace release proof");
        check(!failed.acceptRelease(failure.at("lateEmpty")), "later empty SHUTDOWN recheck does not upgrade failed RELEASE");
        auto debt = failed;
        check(!debt.acceptOwnedPnpAbsence(false, {}), "debt needs complete PnP enumeration");
        check(!debt.acceptOwnedPnpAbsence(true, debt.ownedTargets()), "still-owned debt cannot clear");
        check(debt.acceptOwnedPnpAbsence(true, {"USB\\VID_28DE&PID_1205\\FOREIGN"}), "foreign root not owned debt");
        Session legacy; legacy.hello(json::object()); check(!legacy.begin(true, "steamdeck", "exact-msi-run", 17), "legacy Host unsupported explicit failure");
        check(legacy.begin(false, "steamdeck", "rog-gpd", 17), "nonMSI unchanged no capability requirement");
        check(legacy.acceptPrepare(json::object()) && legacy.acceptRelease(json::object()), "non-opt-in old behavior unchanged");
        std::cout << "PASS " << checks << " native/Host paired wire checks, 100 repeats; fake-only no product APIs\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << "FAIL " << ex.what() << '\n'; return 1; }
}
