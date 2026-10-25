#include "steam_overlay_provider_cache_diagnostic.h"
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef YMCC_PROVIDER_HEADER_SHA256
#define YMCC_PROVIDER_HEADER_SHA256 ""
#define YMCC_PROVIDER_FIXTURE_SHA256 ""
#define YMCC_PROVIDER_RUNTIME_SHA256 ""
#define YMCC_PROVIDER_RUNTIME_BEFORE_SHA256 ""
#endif
using Json = nlohmann::json;
namespace {
constexpr std::uint32_t kApp = 4131894616U, kPid = 24452;
const std::string kGame = "17746352246272032768";
unsigned cases = 0, assertions = 0, evaluations = 0;
std::vector<std::string> caseNames;
void begin(const char* name) { ++cases; caseNames.emplace_back(name); }
void expect(bool value, const std::string& message) {
    ++assertions; if (!value) throw std::runtime_error(caseNames.back() + ": " + message);
}
Json tag(const char* type, Json value = nullptr) { return {{"rawType", type}, {"value", std::move(value)}}; }
Json row(std::string game = kGame, std::uint32_t pid = kPid, std::uint32_t app = kApp) {
    return {{"entryKey", game + ":" + std::to_string(pid)}, {"fields", {
        {"m_unPID", tag("number", pid)}, {"m_nBrowserID", tag("number", 7)},
        {"m_eBrowserType", tag("number", 2)}, {"m_unAppID", tag("number", app)},
        {"m_gameID", tag("string", game)}, {"m_eUIMode", tag("number", 1)}}}};
}
Json read(Json rows = Json::array({row()})) {
    return {{"ok", true}, {"mutated", false}, {"recoveryAdmission", false},
        {"private", "FOREIGN_PRIVATE_SENTINEL"}, {"overlayProviderCacheDiagnostic", {
        {"implementation", "steam-overlay-provider-cache-v1"}, {"readOnly", true}, {"sourceMatched", true},
        {"sourcePathQualified", true}, {"available", true}, {"reason", "source-qualified-cache-read"},
        {"mapEntryCount", rows.size()}, {"passesComplete", Json::array({true, true})}, {"rows", std::move(rows)},
        {"snapshotAtomic", false}, {"originalProviderObjectVerified", false}, {"steamContactVerified", false},
        {"actualInputVerified", false}, {"actualContinueVerified", false}, {"recoveryAdmission", false}}}};
}
Json& wire(Json& value) { return value.at("overlayProviderCacheDiagnostic"); }
Json& fields(Json& value) { return wire(value).at("rows").at(0).at("fields"); }
Json check(const Json& input, const Json& app = kApp, const Json& pid = kPid, const Json& game = kGame) {
    ++evaluations; const auto before = input;
    const auto out = ymcc::steamoverlayprovidercache::summarize(input, app, pid, game);
    expect(input == before, "original read preserved");
    const std::set<std::string> keys = {"implementation", "readOnly", "sourceMatched", "sourcePathQualified", "available", "reason",
        "appid", "originalPid", "fullGameIdExactString", "mapEntryCount", "targetMembership", "samePidOtherGameIdCount", "targetFieldsTagged",
        "sameGameIdCandidateCount", "sameGameIdCandidatesComplete", "sameGameIdCandidatesTagged",
        "snapshotAtomic", "originalProviderObjectVerified", "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission", "pidMatchIsIdentityProof"};
    expect(out.is_object() && out.size() == keys.size(), "closed output shape");
    for (const auto& item : out.items()) expect(keys.count(item.key()) == 1, "no extra output key");
    for (const auto* flag : {"snapshotAtomic", "originalProviderObjectVerified", "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission", "pidMatchIsIdentityProof"})
        expect(out.at(flag).is_boolean() && !out.at(flag).get<bool>(), "no inferred authority");
    expect(out.at("implementation") == "steam-overlay-provider-cache-v1" && out.at("readOnly") == true, "fixed implementation/readOnly");
    expect(out.dump().find("FOREIGN_PRIVATE_SENTINEL") == std::string::npos, "no foreign/private raw data in log summary");
    const auto& count = out.at("sameGameIdCandidateCount");
    const auto& complete = out.at("sameGameIdCandidatesComplete");
    const auto& candidates = out.at("sameGameIdCandidatesTagged");
    expect(complete.is_boolean(), "candidate completeness is always boolean");
    if (out.at("available") == false) {
        expect(count.is_null() && complete == false && candidates.is_null(), "unavailable never publishes partial candidate facts");
    } else {
        expect(count.is_number_unsigned() && count.get<unsigned>() <= 128, "known candidate count is bounded and exact integer");
        if (count.get<unsigned>() <= 2) {
            expect(complete == true && candidates.is_array() && candidates.size() == count.get<unsigned>(), "complete candidates respect two-row boundary");
            for (const auto& candidate : candidates) {
                expect(candidate.is_object() && candidate.size() == 6, "candidate exposes exactly six descriptor fields");
                for (const auto* field : {"m_unPID", "m_nBrowserID", "m_eBrowserType", "m_unAppID", "m_gameID", "m_eUIMode"})
                    expect(candidate.contains(field), "candidate retains existing field names only");
            }
        } else {
            expect(complete == false && candidates.is_null(), "overflow exposes count without a partial candidate list");
        }
    }
    return out;
}
Json unknown(const Json& input, const Json& app = kApp, const Json& pid = kPid, const Json& game = kGame) {
    const auto out = check(input, app, pid, game);
    expect(out.at("available") == false && out.at("targetMembership") == "unknown" && out.at("targetFieldsTagged").is_null(), "unknown stays unavailable with null target");
    return out;
}
void present(const Json& out) {
    expect(out.at("available") == true && out.at("targetMembership") == "present-in-two-cache-reads", "present requires complete validated identity");
    expect(out.at("mapEntryCount").is_number_integer() && out.at("targetFieldsTagged").is_object(), "present count/fields available");
}
void candidates(const Json& out, const Json& expected) {
    expect(out.at("sameGameIdCandidateCount") == expected.size() && out.at("sameGameIdCandidatesComplete") == true &&
        out.at("sameGameIdCandidatesTagged") == expected, "candidate count and original six raw tags preserved");
}
void overflow(const Json& out, unsigned count) {
    expect(out.at("available") == true && out.at("sameGameIdCandidateCount") == count &&
        out.at("sameGameIdCandidatesComplete") == false && out.at("sameGameIdCandidatesTagged").is_null(), "overflow retains exact count and existing availability");
}
void run() {
    begin("exact-shortcut-string-identity");
    auto input = read(); auto out = check(input); present(out);
    expect(out.at("fullGameIdExactString") == kGame && out.at("targetFieldsTagged") == fields(input), "exact full GameID and original six tags");
    candidates(out, Json::array({fields(input)}));

    begin("native-24-bit-numeric-gameid");
    input = read(Json::array({row("16777215", kPid, 16777215)})); fields(input)["m_gameID"] = tag("number", 16777215);
    out = check(input, 16777215, kPid, "16777215"); present(out); candidates(out, Json::array({fields(input)}));

    begin("bigint-gameid-string-pid-and-bigint-appid");
    input = read(); fields(input)["m_gameID"] = tag("bigint", kGame); fields(input)["m_unPID"] = tag("string", "24452");
    fields(input)["m_unAppID"] = tag("bigint", "4131894616"); out = check(input); present(out); candidates(out, Json::array({fields(input)}));

    begin("uint32-max-shortcut-retains-64-bit-precision");
    input = read(Json::array({row("18446744069448138752", 0xffffffffU, 0xffffffffU)}));
    fields(input)["m_gameID"] = tag("bigint", "18446744069448138752"); fields(input)["m_unPID"] = tag("bigint", "4294967295");
    out = check(input, 0xffffffffU, 0xffffffffU, "18446744069448138752"); present(out);
    expect(out.at("fullGameIdExactString") == "18446744069448138752", "not rounded or truncated");

    begin("signed-int32-appid-normalization-preserves-tag");
    input = read(); fields(input)["m_unAppID"] = tag("number", -163072680);
    out = check(input); present(out); expect(out.at("targetFieldsTagged").at("m_unAppID") == tag("number", -163072680), "signed raw AppID retained");
    candidates(out, Json::array({fields(input)}));

    begin("nonidentity-descriptor-types-and-number-null");
    input = read(); fields(input)["m_nBrowserID"] = tag("undefined"); fields(input)["m_eBrowserType"] = tag("object"); fields(input)["m_eUIMode"] = tag("function"); present(check(input));
    fields(input)["m_nBrowserID"] = tag("symbol"); fields(input)["m_eBrowserType"] = tag("boolean", true); fields(input)["m_eUIMode"] = tag("null"); present(check(input));
    fields(input)["m_nBrowserID"] = tag("number"); present(check(input));

    begin("pid-zero-is-only-raw-absence");
    input = read(Json::array({row(kGame, 0)})); out = check(input);
    expect(out.at("available") == true && out.at("targetMembership") == "absent-in-two-cache-reads" && out.at("targetFieldsTagged").is_null(), "PID zero cannot match pinned PID");
    candidates(out, Json::array({fields(input)}));

    begin("complete-empty-map-is-known-absence");
    out = check(read(Json::array()));
    expect(out.at("mapEntryCount") == 0 && out.at("samePidOtherGameIdCount") == 0 && out.at("available") == true &&
        out.at("targetMembership") == "absent-in-two-cache-reads" && out.at("reason") == "no-target", "empty differs from unknown");
    candidates(out, Json::array());

    begin("foreign-rows-redacted-with-only-aggregates");
    auto foreign = row("42", 17, 42); foreign["fields"]["m_nBrowserID"] = tag("string", "FOREIGN_PRIVATE_SENTINEL");
    input = read(Json::array({foreign, row()})); out = check(input); present(out);
    expect(out.at("mapEntryCount") == 2 && out.at("samePidOtherGameIdCount") == 0 && out.at("targetFieldsTagged") == row().at("fields"), "only target fields survive");
    candidates(out, Json::array({row().at("fields")}));

    begin("same-full-gameid-other-pid-visible-with-original-target-absent");
    auto other = row(kGame, kPid + 1); other["fields"]["m_gameID"] = tag("bigint", kGame);
    other["fields"]["m_unPID"] = tag("string", std::to_string(kPid + 1));
    other["fields"]["m_unAppID"] = tag("number", -163072680);
    other["fields"]["m_nBrowserID"] = tag("undefined"); other["fields"]["m_eBrowserType"] = tag("object");
    out = check(read(Json::array({other}))); candidates(out, Json::array({other.at("fields")}));
    expect(out.at("available") == true && out.at("reason") == "no-target" &&
        out.at("targetMembership") == "absent-in-two-cache-reads" && out.at("targetFieldsTagged").is_null(), "descriptor PID does not replace original target");

    begin("two-same-gameid-candidates-preserve-original-target-and-raw-tags");
    input = read(Json::array({other, row()})); out = check(input); present(out);
    candidates(out, Json::array({other.at("fields"), row().at("fields")}));
    expect(out.at("targetFieldsTagged") == row().at("fields"), "second candidate remains the exact original target");

    begin("two-same-gameid-candidates-with-original-target-absent");
    auto second = row(kGame, kPid + 2);
    out = check(read(Json::array({other, second}))); candidates(out, Json::array({other.at("fields"), second.at("fields")}));
    expect(out.at("reason") == "no-target" && out.at("targetMembership") == "absent-in-two-cache-reads" &&
        out.at("targetFieldsTagged").is_null(), "two descriptors cannot fabricate original PID membership");

    begin("three-candidate-overflow-preserves-present-original-target");
    input = read(Json::array({other, second, row()})); out = check(input); present(out); overflow(out, 3);
    expect(out.at("targetFieldsTagged") == row().at("fields") && out.at("mapEntryCount") == 3 &&
        out.at("samePidOtherGameIdCount") == 0 && out.at("reason") == "source-qualified-cache-read", "overflow leaves original target result intact");

    begin("three-candidate-overflow-preserves-absent-original-target");
    out = check(read(Json::array({other, second, row(kGame, kPid + 3)}))); overflow(out, 3);
    expect(out.at("reason") == "no-target" && out.at("targetMembership") == "absent-in-two-cache-reads" &&
        out.at("targetFieldsTagged").is_null(), "overflow does not change original absence");

    begin("candidate-count-continues-after-two-tag-storage-limit");
    Json many = Json::array(); for (std::uint32_t i = 1; i <= 8; ++i) many.push_back(row(kGame, kPid + i));
    out = check(read(many)); overflow(out, 8);
    expect(out.at("mapEntryCount") == 8 && out.at("targetMembership") == "absent-in-two-cache-reads", "count scans the complete qualified map");

    begin("same-appid-different-full-gameid-never-exports-candidate");
    auto sameAppForeign = row("17746352246272032769", kPid + 4);
    sameAppForeign["fields"]["m_nBrowserID"] = tag("string", "FOREIGN_PRIVATE_SENTINEL");
    out = check(read(Json::array({sameAppForeign}))); candidates(out, Json::array());
    expect(out.at("targetMembership") == "absent-in-two-cache-reads", "AppID does not substitute for exact GameID");
    out = check(read(Json::array({sameAppForeign, other, row()}))); present(out);
    candidates(out, Json::array({other.at("fields"), row().at("fields")}));

    begin("malformed-over64bit-or-rounded-gameid-never-becomes-candidate");
    for (const auto& bad : {tag("number"), tag("number", 17746352246272032768ULL), tag("number", 17746352246272032768.0),
        tag("bigint", "18446744073709551616"), tag("bigint", "-1"), tag("string", "017746352246272032768"),
        tag("object"), tag("boolean", true)}) {
        auto invalid = row(kGame, kPid + 1); invalid["fields"]["m_gameID"] = bad;
        unknown(read(Json::array({row(), invalid})));
    }

    begin("candidate-pid-and-appid-must-retain-qualified-identity");
    for (const auto& bad : {tag("number"), tag("string", "024453"), tag("bigint", "4294967296"), tag("boolean", true)}) {
        auto invalid = row(kGame, kPid + 1); invalid["fields"]["m_unPID"] = bad;
        unknown(read(Json::array({row(), invalid})));
    }
    for (const auto& bad : {tag("number"), tag("string", "04131894616"), tag("bigint", "4294967296"), tag("number", 42)}) {
        auto invalid = row(kGame, kPid + 1); invalid["fields"]["m_unAppID"] = bad;
        unknown(read(Json::array({row(), invalid})));
    }

    begin("candidate-source-and-original-identity-errors-remain-unknown");
    for (const auto* flag : {"sourceMatched", "sourcePathQualified"}) for (const auto& bad : {Json(false), Json(1), Json(nullptr)}) {
        input = read(Json::array({other, row()})); wire(input)[flag] = bad; unknown(input);
    }
    input = read(Json::array({other, row()}));
    unknown(input, Json("4131894616")); unknown(input, kApp, Json("24452"));
    unknown(input, kApp, kPid, Json(17746352246272032768ULL));
    unknown(input, kApp, kPid, "18446744073709551616"); unknown(input, kApp, kPid, "42");

    begin("late-invalid-row-discards-earlier-same-gameid-candidates");
    auto lateInvalid = row("42", 17, 42); lateInvalid["fields"]["m_eUIMode"] = tag("mystery");
    unknown(read(Json::array({other, row(), lateInvalid})));

    begin("candidate-overflow-cannot-bypass-whole-map-or-byte-budget");
    many = Json::array(); for (std::uint32_t i = 1; i <= 128; ++i) many.push_back(row(kGame, kPid + i));
    out = unknown(read(many)); expect(out.at("reason") == "summary-budget", "candidate count not committed past the 8KiB gate");
    many.push_back(row(kGame, kPid + 129)); out = unknown(read(many));
    expect(out.at("reason") == "map-budget", "candidate overflow still observes the 128-row gate");

    begin("same-original-pid-other-full-gameid-is-conflict");
    input = read(Json::array({row(), row("42", kPid, 42)})); out = unknown(input);
    expect(out.at("mapEntryCount") == 2 && out.at("samePidOtherGameIdCount") == 1, "conflict exposes aggregate only");

    begin("duplicate-entry-key-rejects-whole-map");
    out = unknown(read(Json::array({row(), row()}))); expect(out.at("mapEntryCount").is_null(), "duplicate gives no partial aggregate");

    begin("entry-key-cannot-supply-or-rewrite-identity");
    input = read(); wire(input)["rows"][0]["entryKey"] = kGame + ":024452"; unknown(input);

    begin("same-full-gameid-conflicting-appid-even-other-pid");
    input = read(Json::array({row(), row(kGame, 19, 42)})); unknown(input);

    begin("expected-appid-strict-nonzero-uint32");
    for (const Json& bad : {Json(0), Json(true), Json(4131894616.0), Json("4131894616"), Json(4294967296ULL)}) unknown(read(), bad);

    begin("expected-pid-strict-nonzero-uint32");
    for (const Json& bad : {Json(0), Json(false), Json(24452.0), Json("24452"), Json(-1)}) unknown(read(), kApp, bad);

    begin("native-full-gameid-strict-canonical-uint64-string");
    for (const Json& bad : {Json(17746352246272032768ULL), Json("017746352246272032768"), Json("+17746352246272032768"), Json("18446744073709551616")}) unknown(read(), kApp, kPid, bad);

    begin("expected-gameid-must-belong-to-appid");
    unknown(read(), kApp, kPid, "4131894616"); // Never use AppID as a shortcut GameID.
    unknown(read(), kApp, kPid, "17746352246238478336"); // Wrong shortcut low32.
    unknown(read(), 16777216, kPid, "16777216"); // Native GameID is only 24 bits.

    begin("read-ok-and-unmutated-strict-booleans");
    input = read(); input["ok"] = 1; unknown(input);
    input = read(); input["mutated"] = true; unknown(input);
    input = read(); input.erase("mutated"); unknown(input);

    begin("wire-closed-shape-and-required-fields");
    input = read(); wire(input)["private"] = {{"account", "FOREIGN_PRIVATE_SENTINEL"}}; unknown(input);
    input = read(); wire(input).erase("mapEntryCount"); unknown(input);

    begin("row-and-six-fields-closed-shapes");
    input = read(); wire(input)["rows"][0]["account"] = "FOREIGN_PRIVATE_SENTINEL"; unknown(input);
    input = read(); fields(input)["private"] = "FOREIGN_PRIVATE_SENTINEL"; unknown(input);
    input = read(); fields(input).erase("m_eUIMode"); unknown(input);

    begin("tag-closed-shape-and-rawtype-value-consistency");
    input = read(); fields(input)["m_eUIMode"] = {{"rawType", "object"}, {"value", {{"private", "FOREIGN_PRIVATE_SENTINEL"}}}}; unknown(input);
    input = read(); fields(input)["m_eUIMode"] = tag("mystery"); unknown(input);
    input = read(); fields(input)["m_eUIMode"]["private"] = true; unknown(input);

    begin("safe-signed-integer-boundaries-not-floating-point");
    input = read(); fields(input)["m_nBrowserID"] = tag("number", 9007199254740991LL); fields(input)["m_eUIMode"] = tag("number", -9007199254740991LL); present(check(input));
    fields(input)["m_nBrowserID"] = tag("number", 9007199254740992ULL); unknown(input);
    input = read(); fields(input)["m_nBrowserID"] = tag("number", 7.0); unknown(input);

    begin("unsafe-number-null-gameid-cannot-be-recovered-from-key");
    input = read(); fields(input)["m_gameID"] = tag("number"); unknown(input);

    begin("row-identity-requires-supported-primitive");
    input = read(); fields(input)["m_unPID"] = tag("boolean", true); unknown(input);
    input = read(); fields(input)["m_gameID"] = tag("object"); unknown(input);

    begin("pid-canonical-and-uint32-range");
    input = read(); fields(input)["m_unPID"] = tag("string", "024452"); unknown(input);
    input = read(); fields(input)["m_unPID"] = tag("bigint", "4294967296"); unknown(input);

    begin("gameid-canonical-unsigned-and-uint64-range");
    input = read(); fields(input)["m_gameID"] = tag("bigint", "18446744073709551616"); unknown(input);
    input = read(); fields(input)["m_gameID"] = tag("bigint", "-1"); unknown(input);

    begin("bigint-tag-canonical-signed-decimal-budget");
    input = read(); fields(input)["m_nBrowserID"] = tag("bigint", "-12345678901234567890"); present(check(input));
    fields(input)["m_nBrowserID"] = tag("bigint", "-0"); unknown(input);
    fields(input)["m_nBrowserID"] = tag("bigint", "1234567890123456789012"); unknown(input);

    begin("utf16-budget-and-malformed-utf8");
    std::string emoji; for (unsigned i = 0; i < 32; ++i) emoji += "\xf0\x9f\x98\x80";
    input = read(); fields(input)["m_nBrowserID"] = tag("string", emoji); present(check(input));
    fields(input)["m_nBrowserID"] = tag("string", emoji + "a"); unknown(input);
    fields(input)["m_nBrowserID"] = tag("string", std::string("\xed\xa0\x80")); unknown(input);

    begin("whole-map-budget-128-and-count-consistency");
    Json rows = Json::array({row()}); for (std::uint32_t i = 1; i < 128; ++i) rows.push_back(row(std::to_string(i), i, i));
    input = read(rows); out = check(input); expect(!out.at("available").get<bool>() && out.at("reason") == "summary-budget" && out.at("mapEntryCount").is_null(), "128 well-shaped rows still obey total 8KiB wire budget");
    rows.push_back(row("128", 128, 128)); unknown(read(rows));
    input = read(); wire(input)["mapEntryCount"] = 0; unknown(input);

    begin("count-json-type-is-never-coerced");
    input = read(); wire(input)["mapEntryCount"] = true; unknown(input);
    input = read(); wire(input)["mapEntryCount"] = 1.0; unknown(input);

    begin("two-complete-passes-strict-array");
    input = read(); wire(input)["passesComplete"] = Json::array({true, false}); unknown(input);
    input = read(); wire(input)["passesComplete"] = Json::array({true, 1}); unknown(input);
    input = read(); wire(input)["passesComplete"] = Json::array({true, true, true}); unknown(input);

    begin("wire-cannot-assert-any-authority-flag");
    for (const auto* flag : {"snapshotAtomic", "originalProviderObjectVerified", "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission"}) {
        input = read(); wire(input)[flag] = true; unknown(input);
    }

    begin("source-and-path-must-both-be-qualified");
    input = read(); wire(input)["sourceMatched"] = false; unknown(input);
    input = read(); wire(input)["sourcePathQualified"] = false; unknown(input);

    begin("unavailable-whitelisted-reasons-preserve-null-not-zero");
    for (const auto* reason : {"no-target", "provider-store-unavailable", "provider-source-unverified", "data-path-unqualified", "map-brand-unqualified",
        "map-budget", "field-unavailable", "cache-changed", "read-window-expired", "summary-budget", "cache-read-unavailable", "diagnostic-unavailable", "original-identity-bracket-unqualified"}) {
        input = read(); auto& value = wire(input); value["available"] = false; value["reason"] = reason;
        value["mapEntryCount"] = nullptr; value["rows"] = nullptr; value["passesComplete"] = nullptr;
        out = unknown(input); expect(out.at("reason") == reason && out.at("mapEntryCount").is_null() && out.at("samePidOtherGameIdCount").is_null(), "only fixed reason, no fabricated zero");
    }

    begin("unknown-reasons-and-inconsistent-availability-scrubbed");
    input = read(); wire(input)["reason"] = "FOREIGN_PRIVATE_SENTINEL"; unknown(input);
    input = read(); wire(input)["available"] = false; unknown(input);
    input = read(); wire(input)["reason"] = "cache-changed"; unknown(input);

    begin("late-foreign-row-failure-does-not-publish-partial-target");
    foreign = row("42", 17, 42); foreign["entryKey"] = "bad";
    out = unknown(read(Json::array({row(), foreign}))); expect(out.at("mapEntryCount").is_null(), "validate entire map before commit");
    input = read(); const auto original = input; Json detail = {{"steamUI", input}};
    detail["steamUI"].erase("overlayProviderCacheDiagnostic"); detail["overlayProviderCacheDiagnostic"] = check(input);
    expect(!detail.at("steamUI").contains("overlayProviderCacheDiagnostic") && input == original, "log copy strips raw map and preserves original read");
}
} // namespace
int main() {
    try {
        run();
        expect(cases == 48, "bounded semantic case count");
        for (const auto* hash : {YMCC_PROVIDER_HEADER_SHA256, YMCC_PROVIDER_FIXTURE_SHA256, YMCC_PROVIDER_RUNTIME_SHA256, YMCC_PROVIDER_RUNTIME_BEFORE_SHA256})
            expect(std::string(hash).size() == 64, "compiled output bound to actual source hashes");
        std::cout << Json{{"schema", "steam-overlay-provider-cache-native-fixture.v1"}, {"status", "PASS"}, {"cases", cases},
            {"evaluations", evaluations}, {"assertions", assertions}, {"caseNames", caseNames}, {"pureJsonOnly", true},
            {"compiledSourceSha256", {{"header", YMCC_PROVIDER_HEADER_SHA256}, {"fixture", YMCC_PROVIDER_FIXTURE_SHA256},
                {"runtime", YMCC_PROVIDER_RUNTIME_SHA256}, {"runtimeBefore", YMCC_PROVIDER_RUNTIME_BEFORE_SHA256}}},
            {"steamRpcCalls", 0}, {"targetMemoryReads", 0}, {"recoveryAdmissionChanged", false}, {"runtimeReady", false},
            {"runtimeAuthorized", false}, {"repairAccepted", false}}.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << Json{{"status", "FAIL"}, {"cases", cases}, {"evaluations", evaluations}, {"assertions", assertions}, {"failure", error.what()}}.dump() << '\n';
        return 1;
    }
}
