#include "../native/steam_native_return_evidence.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Json = ymcc::steamreturnevidence::Json;
namespace {
unsigned cases = 0, assertions = 0;
constexpr char kCanary[] = "PRIVATE-RAW-ACCOUNT-ADDRESS-CANARY";
constexpr char kAccount[] = "76561198012345678";
constexpr char kAddress[] = "0x00007fff12345678";
const std::array<const char*, 9> kFields = {"source", "kind", "observed", "code", "booleanValue",
    "authority", "nativeMeaningVerified", "gameInputVerified", "continueVerified"};
void expect(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
Json evidence(const char* kind, bool observed, Json code = nullptr, Json booleanValue = nullptr) {
    return {{"source", "SteamClient.Apps.RaiseWindowForGame"}, {"kind", kind}, {"observed", observed},
        {"code", std::move(code)}, {"booleanValue", std::move(booleanValue)},
        {"authority", "diagnostic-only-no-admission"}, {"nativeMeaningVerified", false},
        {"gameInputVerified", false}, {"continueVerified", false}};
}
Json deepPayload() {
    Json deep = {{"canary", kCanary}, {"steamid64", kAccount}, {"heapAddress", kAddress},
        {"actionAcknowledged", true}, {"accepted", true}, {"replay", true}};
    for (unsigned i = 0; i < 256; ++i) {
        Json parent = Json::object();
        parent["nested"] = std::move(deep);
        deep = std::move(parent);
    }
    return deep;
}
void checkPrivacy(const Json& output) {
    const auto text = output.dump();
    expect(text.find(kCanary) == std::string::npos, "raw canary leaked");
    expect(text.find(kAccount) == std::string::npos, "full account leaked");
    expect(text.find(kAddress) == std::string::npos, "heap address leaked");
    if (output.is_null()) return;
    expect(output.is_object() && output.size() == kFields.size(), "output is not exactly nine fields");
    for (const auto* key : kFields) expect(output.contains(key), "required output field missing");
    for (const auto* key : {"actionAcknowledged", "accepted", "runtimeAccepted", "verificationPending",
            "replay", "retry", "raw", "steamid64", "heapAddress", "reason", "ok", "mutated"})
        expect(!output.contains(key), "unknown field or action authority copied");
}
Json run(const Json& reply) {
    ++cases;
    // Input remains untouched, including its unknown/private material.
    const auto before = reply.dump();
    const auto output = ymcc::steamreturnevidence::bounded(reply);
    expect(reply.dump() == before, "bounded changed its const input");
    checkPrivacy(output);
    return output;
}
void accept(Json value, const Json& expected) {
    value["raw"] = deepPayload();
    value["steamid64"] = kAccount;
    value["heapAddress"] = kAddress;
    value["actionAcknowledged"] = true;
    value["accepted"] = true;
    value["runtimeAccepted"] = true;
    value["verificationPending"] = true;
    value["replay"] = true;
    const auto output = run({{"nativeReturn", std::move(value)}, {"raw", deepPayload()},
        {"reason", kCanary}, {"actionAcknowledged", true}, {"accepted", true}, {"replay", true}});
    expect(output == expected, "valid fixed-contract evidence changed or rejected");
    if (expected["kind"] == "uint32") expect(output["code"].is_number_unsigned(), "uint32 was not normalized");
}
void rejectReply(const Json& reply) {
    expect(run(reply).is_null(), "malformed reply accepted");
}
void reject(const Json& value) {
    rejectReply({{"nativeReturn", value}, {"raw", deepPayload()}});
}
void validCases() {
    // Actual native return codes 1/2/3, zero and both sides of the signed-int32
    // boundary must survive both JSON integer storage representations exactly.
    for (const std::uint64_t number : {0ULL, 1ULL, 2ULL, 3ULL, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL}) {
        const auto expected = evidence("uint32", true, Json(number));
        accept(expected, expected);
        accept(evidence("uint32", true, Json(static_cast<std::int64_t>(number))), expected);
    }
    for (const bool booleanValue : {false, true}) {
        const auto expected = evidence("boolean", true, nullptr, booleanValue);
        accept(expected, expected);
    }
    for (const auto* kind : {"undefined", "unsupported"}) {
        const auto expected = evidence(kind, false);
        accept(expected, expected);
    }
}
void malformedContainers() {
    for (const auto& value : std::vector<Json>{nullptr, false, true, 0, 1.0, kCanary, Json::array(),
            Json::array({deepPayload()}), Json::binary({0, 1, 2}), Json::object()}) {
        rejectReply(value);
        if (!value.is_object()) reject(value);
    }
    reject(Json::object());
    rejectReply({{"NativeReturn", evidence("undefined", false)}});
    rejectReply({{"result", {{"nativeReturn", evidence("uint32", true, 1)}}}});
    auto outer = evidence("uint32", true, 1); // A bare evidence object is not a reply.
    rejectReply(outer);
    auto missingReturn = Json::object();
    missingReturn["raw"] = deepPayload();
    rejectReply(missingReturn);
}
void fieldTypesAndMissingFields() {
    const std::array<Json, 4> contracts = {evidence("uint32", true, 1), evidence("boolean", true, nullptr, false),
        evidence("undefined", false), evidence("unsupported", false)};
    for (const auto& contract : contracts) {
        for (const auto* field : kFields) {
            auto missing = contract;
            missing.erase(field); // at() throws; bounded must catch and return null.
            reject(missing);
            auto deep = contract;
            deep[field] = deepPayload();
            reject(deep);
        }
    }
    const auto valid = evidence("uint32", true, 1);
    for (const auto* field : {"source", "kind", "authority"}) {
        for (const auto& bad : std::vector<Json>{nullptr, false, 0, 1.0, Json::array(), Json::object(),
                "", kCanary, "SteamClient.Apps.RaiseWindowForGame ", "diagnostic-only-no-admission ",
                "Uint32", "uint64", "null", "TRUE"}) {
            auto value = valid;
            value[field] = bad;
            reject(value);
        }
    }
    for (const auto* field : {"observed", "nativeMeaningVerified", "gameInputVerified", "continueVerified"}) {
        for (const auto& bad : std::vector<Json>{nullptr, 0, 1, 0.0, "false", Json::array(), Json::object()}) {
            auto value = valid;
            value[field] = bad;
            reject(value);
        }
        auto opposite = valid;
        opposite[field] = std::string(field) == "observed" ? Json(false) : Json(true);
        reject(opposite);
    }
}
void numericAndCombinationCases() {
    const auto valid = evidence("uint32", true, 1);
    for (const auto& bad : std::vector<Json>{nullptr, false, true, -1, Json(std::numeric_limits<std::int64_t>::min()),
            Json(std::uint64_t{0x100000000ULL}), Json(std::int64_t{0x100000000LL}),
            Json(std::numeric_limits<std::int64_t>::max()), Json(std::numeric_limits<std::uint64_t>::max()),
            -0.0, 0.0, 1.0, 1.5, -1.0, 4294967295.0, 4294967296.0,
            Json(std::numeric_limits<double>::quiet_NaN()), Json(std::numeric_limits<double>::infinity()),
            "1", "4294967295", kCanary, Json::array(), Json::object()}) {
        auto value = valid;
        value["code"] = bad;
        reject(value);
    }
    for (const auto& bad : std::vector<Json>{false, true, 0, "false", Json::array(), Json::object()}) {
        auto value = valid;
        value["booleanValue"] = bad;
        reject(value);
    }
    for (const auto& bad : std::vector<Json>{nullptr, 0, 1, 0.0, "false", Json::array(), Json::object()})
        reject(evidence("boolean", true, nullptr, bad));
    reject(evidence("boolean", false, nullptr, false));
    for (const auto& badCode : std::vector<Json>{0, 1, false, "1", Json::object()})
        reject(evidence("boolean", true, badCode, false));
    for (const auto* kind : {"undefined", "unsupported"}) {
        reject(evidence(kind, true));
        for (const auto& nonNull : std::vector<Json>{0, false, "", Json::array(), Json::object()}) {
            reject(evidence(kind, false, nonNull));
            reject(evidence(kind, false, nullptr, nonNull));
        }
    }
    // Neither null evidence nor a prior successful call can authorize anything.
    reject(nullptr);
    accept(evidence("uint32", true, 3), evidence("uint32", true, Json(std::uint64_t{3})));
    reject(nullptr);
    reject(evidence("unsupported", false, kCanary));
    accept(evidence("boolean", true, nullptr, false), evidence("boolean", true, nullptr, false));
}
} // namespace
int main() {
    try {
        validCases();
        malformedContainers();
        fieldTypesAndMissingFields();
        numericAndCombinationCases();
        std::cout << "PASS steam_native_return_evidence: " << cases << " cases, " << assertions
                  << " assertions; strict nine-field evidence, no authority or private payload copied\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL steam_native_return_evidence at case " << cases << ": " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL steam_native_return_evidence: unexpected exception\n";
        return 1;
    }
}