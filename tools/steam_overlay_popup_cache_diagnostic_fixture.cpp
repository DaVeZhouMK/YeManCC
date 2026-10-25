#include "steam_overlay_popup_cache_diagnostic.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Json = nlohmann::json;
namespace {
constexpr std::uint32_t kApp = 4131894616U, kOriginal = 24452;
unsigned cases = 0, assertions = 0;
void expect(bool value, const std::string& name) {
    ++assertions; if (!value) throw std::runtime_error(name);
}
Json successful(Json pids = Json::array({kOriginal}), int status = 4, bool listed = true) {
    return {{"ok", true}, {"mutated", false}, {"recoveryAdmission", false},
        {"overviews", Json::array({{{"appid", kApp}, {"localDisplayStatus", status}, {"inRunningList", listed}}})},
        {"overlayPopupCacheDiagnostic", {{"implementation", "steam-overlay-popup-cache-v1"}, {"readOnly", true}, {"sourceMatched", true},
            {"rows", Json::array({{{"appid", kApp}, {"available", true}, {"reason", "source-qualified-cache-read"},
                {"popupCount", pids.size()}, {"pidCandidates", pids}}})},
            {"steamContactVerified", false}, {"actualInputVerified", false}, {"actualContinueVerified", false}, {"recoveryAdmission", false}}}};
}
Json& diagnostic(Json& input) { return input["overlayPopupCacheDiagnostic"]; }
Json& row(Json& input) { return diagnostic(input)["rows"][0]; }
Json check(const std::string& name, const Json& input, const Json& app = kApp, const Json& original = kOriginal) {
    ++cases; const auto before = input.dump();
    const auto out = ymcc::steamoverlaycache::summarize(input, app, original);
    expect(input.dump() == before, name + ": existing read/admission unchanged");
    const std::set<std::string> keys = {"implementation", "readOnly", "appid", "sourceMatched", "available", "reason", "popupCount", "pidCandidates",
        "expectedPidCandidateMatch", "normalRaisePrerequisite", "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission", "pidMatchIsIdentityProof"};
    expect(out.is_object() && out.size() == keys.size(), name + ": exact output shape");
    for (const auto& item : out.items()) expect(keys.count(item.key()) == 1, name + ": no extra key");
    for (const char* flag : {"steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission", "pidMatchIsIdentityProof"})
        expect(out.at(flag).is_boolean() && !out.at(flag).get<bool>(), name + ": no authority");
    expect(out.at("readOnly") == true && out.at("implementation") == "steam-overlay-popup-cache-v1", name + ": fixed implementation/readOnly");
    expect(out.dump().find("PRIVATE_SENTINEL") == std::string::npos, name + ": no private input propagated");
    return out;
}
void unknown(const std::string& name, const Json& input, const Json& app = kApp, const Json& original = kOriginal) {
    const auto out = check(name, input, app, original);
    expect(out.at("available") == false && out.at("popupCount").is_null() && out.at("pidCandidates").is_null(), name + ": unavailable stays null");
    expect(out.at("expectedPidCandidateMatch").is_null() && out.at("normalRaisePrerequisite") == "unknown", name + ": unknown prerequisite/match");
}
void run() {
    auto input = successful(Json::array());
    auto out = check("known-empty", input);
    expect(out.at("available") == true && out.at("popupCount") == 0 && out.at("pidCandidates").empty(), "known empty, not unknown");
    expect(out.at("normalRaisePrerequisite") == "empty" && out.at("expectedPidCandidateMatch") == false, "empty prerequisite only");
    out = check("healthy-exact-candidate", successful());
    expect(out.at("popupCount") == 1 && out.at("expectedPidCandidateMatch") == true && out.at("normalRaisePrerequisite") == "nonempty", "healthy candidate is diagnostic only");
    out = check("ui11-unlisted-exact-candidate", successful(Json::array({kOriginal}), 11, false));
    expect(out.at("expectedPidCandidateMatch") == true && out.at("available") == true, "UI lost link cannot invalidate cache diagnostic or grant admission");
    out = check("healthy-other-pid", successful(Json::array({0, 9876})));
    expect(out.at("expectedPidCandidateMatch") == false && out.at("pidCandidates") == Json::array({0, 9876}), "PID zero preserved as raw candidate");
    out = check("disconnected-empty", successful(Json::array(), 11, false));
    expect(out.at("popupCount") == 0 && out.at("normalRaisePrerequisite") == "empty", "UI11 empty cache remains known empty");
    out = check("zero-is-not-original-pid", successful(Json::array({0})), kApp, 0);
    expect(out.at("expectedPidCandidateMatch").is_null() && out.at("pidCandidates") == Json::array({0}), "zero never becomes an expected game PID");
    const std::vector<Json> invalidIds = {nullptr, true, false, -1, -1.0, 1.0, 0.5, 4294967296ULL, "24452", Json::array(), Json::object()};
    for (std::size_t i = 0; i < invalidIds.size(); ++i) {
        out = check("invalid-original-" + std::to_string(i), successful(), kApp, invalidIds[i]);
        expect(out.at("expectedPidCandidateMatch").is_null() && out.at("available") == true, "invalid original affects candidate comparison only");
        unknown("invalid-target-" + std::to_string(i), successful(), invalidIds[i]);
        input = successful(); row(input)["appid"] = invalidIds[i]; unknown("invalid-row-appid-" + std::to_string(i), input);
        input = successful(); row(input)["pidCandidates"][0] = invalidIds[i]; unknown("invalid-candidate-pid-" + std::to_string(i), input);
    }
    out = ymcc::steamoverlaycache::summarize(successful(), kApp);
    expect(out.at("expectedPidCandidateMatch").is_null(), "omitted original PID stays null");
    out = check("max-uint32-pid", successful(Json::array({4294967295ULL})), kApp, 4294967295ULL);
    expect(out.at("expectedPidCandidateMatch") == true, "unsigned upper bound stays candidate only");
    input = successful(); row(input)["appid"] = 4294967295ULL;
    out = check("max-uint32-appid", input, 4294967295ULL);
    expect(out.at("available") == true && out.at("appid") == 4294967295ULL, "AppID upper bound accepted without narrowing");
    input = successful(); row(input)["appid"] = 0;
    out = check("zero-target-forged-row-zero", input, 0);
    expect(out.at("available") == false && out.at("appid").is_null() && out.at("popupCount").is_null() && out.at("pidCandidates").is_null(), "zero target cannot become available even with a matching forged row");
    expect(out.at("expectedPidCandidateMatch").is_null() && out.at("normalRaisePrerequisite") == "unknown", "zero target keeps candidate comparison and prerequisite unknown");
    unknown("unsigned-zero-target-forged-row-zero", input, std::uint32_t{0});
    unknown("missing-diagnostic", {{"ok", true}, {"mutated", false}});
    for (const Json& primitive : {Json(nullptr), Json(true), Json(1), Json("PRIVATE_SENTINEL"), Json::array()}) unknown("non-object-read", primitive);
    for (const char* key : {"ok", "mutated"}) {
        input = successful(); input.erase(key); unknown(std::string("missing-read-") + key, input);
        for (const Json& value : {Json(nullptr), Json(0), Json("true")}) { input = successful(); input[key] = value; unknown(std::string("wrong-read-type-") + key, input); }
    }
    input = successful(); input["ok"] = false; unknown("failed-read-not-zero", input);
    input = successful(); input["mutated"] = true; unknown("mutating-read-not-diagnostic", input);
    for (const char* key : {"implementation", "readOnly", "sourceMatched", "rows"}) {
        input = successful(); diagnostic(input).erase(key); unknown(std::string("missing-") + key, input);
    }
    for (const Json& value : {Json(nullptr), Json(1), Json(false), Json("PRIVATE_SENTINEL")}) {
        input = successful(); diagnostic(input)["implementation"] = value; unknown("wrong-implementation", input);
    }
    for (const char* key : {"readOnly", "sourceMatched"}) for (const Json& value : {Json(nullptr), Json(1), Json("true")}) {
        input = successful(); diagnostic(input)[key] = value; unknown(std::string("wrong-bool-") + key, input);
    }
    input = successful(); diagnostic(input)["readOnly"] = false; unknown("not-readonly", input);
    input = successful(); diagnostic(input)["sourceMatched"] = false; unknown("unverified-success-claim", input);
    for (const char* key : {"appid", "available", "reason", "popupCount", "pidCandidates"}) {
        input = successful(); row(input).erase(key); unknown(std::string("missing-row-") + key, input);
    }
    for (const Json& value : {Json(nullptr), Json(1), Json("true")}) {
        input = successful(); row(input)["available"] = value; unknown("wrong-available-type", input);
    }
    for (const Json& count : {Json(-1), Json(-0.5), Json(1.0), Json(true), Json(false), Json(65), Json(4294967296ULL), Json(nullptr), Json("1")}) {
        input = successful(); row(input)["popupCount"] = count; unknown("invalid-count", input);
    }
    input = successful(); row(input)["popupCount"] = 0; unknown("count-list-mismatch", input);
    input = successful(); row(input)["pidCandidates"] = nullptr; unknown("missing-pids-not-empty", input);
    input = successful(); row(input)["pidCandidates"] = {{"pid", kOriginal}}; unknown("pid-list-not-array", input);
    input = successful(Json::array({kOriginal, kOriginal})); unknown("duplicate-pid", input);
    input = successful(Json::array({0, 0})); unknown("duplicate-zero-pid", input);
    Json sixtyFour = Json::array(); for (unsigned i = 0; i < 64; ++i) sixtyFour.push_back(i);
    out = check("64-candidate-boundary", successful(sixtyFour)); expect(out.at("popupCount") == 64 && out.at("pidCandidates").size() == 64, "64 candidate budget accepted");
    sixtyFour.push_back(64); unknown("65-candidate-budget", successful(sixtyFour));
    input = successful(sixtyFour); row(input)["popupCount"] = 64; unknown("65-list-with-valid-count", input);
    input = successful(); row(input)["appid"] = kApp - 1; unknown("other-appid", input);
    input = successful(); diagnostic(input)["rows"].push_back(row(input)); unknown("duplicate-target-even-identical", input);
    input = successful(); diagnostic(input)["rows"].push_back({{"appid", kApp}, {"available", false}}); unknown("duplicate-target-unavailable", input);
    input = successful(); diagnostic(input)["rows"] = Json::array(); unknown("no-target", input);
    input = successful(); diagnostic(input)["rows"] = Json::object(); unknown("rows-not-array", input);
    input = successful(); diagnostic(input)["rows"].push_back(nullptr); unknown("invalid-row-shape", input);
    input = successful(); for (unsigned i = 0; i < 15; ++i) diagnostic(input)["rows"].push_back({{"appid", i}, {"private", "PRIVATE_SENTINEL"}});
    out = check("16-rows-select-only-target", input); expect(out.at("available") == true && out.at("appid") == kApp, "foreign rows do not escape");
    diagnostic(input)["rows"].push_back({{"appid", 16}}); unknown("17-row-budget", input);
    for (const char* reason : {"no-target", "window-store-unavailable", "map-unavailable", "map-budget", "getter-source-unverified", "target-entry-unknown", "cache-changed", "cache-read-unavailable"}) {
        input = successful(); row(input)["available"] = false; row(input)["reason"] = reason; row(input)["popupCount"] = nullptr; row(input)["pidCandidates"] = nullptr;
        if (std::string(reason) == "getter-source-unverified") diagnostic(input)["sourceMatched"] = false;
        unknown(std::string("unavailable-") + reason, input);
        out = ymcc::steamoverlaycache::summarize(input, kApp, kOriginal); expect(out.at("reason") == reason, "fixed reason survives unknown diagnostic");
        row(input)["popupCount"] = 0; row(input)["pidCandidates"] = Json::array(); unknown(std::string("unavailable-forged-zero-") + reason, input);
        row(input)["available"] = true; unknown(std::string("failure-reason-forged-success-") + reason, input);
    }
    input = successful(); row(input)["available"] = false; row(input)["popupCount"] = nullptr; row(input)["pidCandidates"] = nullptr; unknown("success-reason-on-unavailable", input);
    for (const Json& reason : {Json("PRIVATE_SENTINEL-account=123-address=0x123"), Json(7), Json(nullptr), Json(true)}) {
        input = successful(); row(input)["reason"] = reason; unknown("unknown-reason-scrubbed", input);
        expect(ymcc::steamoverlaycache::summarize(input, kApp, kOriginal).at("reason") == "diagnostic-unavailable", "unknown reason fixed fallback");
    }
    input = successful();
    for (auto* layer : {&input, &diagnostic(input), &row(input)}) {
        (*layer)["account"] = "PRIVATE_SENTINEL"; (*layer)["address"] = "PRIVATE_SENTINEL"; (*layer)["raw"] = {{"secret", "PRIVATE_SENTINEL"}};
        for (const char* flag : {"steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission", "pidMatchIsIdentityProof"}) (*layer)[flag] = true;
    }
    out = check("injected-private-and-authority-fields", input);
    expect(out.at("available") == true && out.at("expectedPidCandidateMatch") == true, "valid cache values retained without authority/private extras");
    input = successful(); row(input)["pidCandidates"][0] = {{"pid", kOriginal}, {"account", "PRIVATE_SENTINEL"}}; unknown("nested-private-candidate", input);
}
} // namespace
int main() {
    try { run(); std::cout << Json{{"schema", "steam-overlay-popup-cache-native-fixture.v1"}, {"status", "PASS"}, {"cases", cases}, {"assertions", assertions},
        {"runtimeReady", false}, {"runtimeAuthorized", false}, {"repairAccepted", false}, {"pureJsonOnly", true}, {"steamRpcCalls", 0}, {"targetMemoryReads", 0}, {"recoveryAdmissionChanged", false}}.dump(2) << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << Json{{"status", "FAIL"}, {"cases", cases}, {"assertions", assertions}, {"failure", error.what()}}.dump() << '\n'; return 1;
    }
}
