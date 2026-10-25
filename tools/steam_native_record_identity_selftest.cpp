#include "../native/steam_native_record_identity.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace identity = ymcc::steamrecordidentity;
using Json = identity::Json;
namespace {
constexpr std::uint64_t kSteamId = 76561198012345678ULL;
constexpr std::uint64_t kGameId = 17746352246272032768ULL;
constexpr std::uint64_t kMaximum = 18446744073709551615ULL;
constexpr std::uint32_t kAppId = 4131894616U;
constexpr char kPrivateKey[] = "_ymccPrivateNativeRecord";
constexpr char kCanary[] = "PRIVATE-OBJECT-CANARY-REMOVE-COMPLETE-PAYLOAD";
constexpr char kDefaultReason[] = "native-record-ui-identity-unavailable";
constexpr char kObservedReason[] = "native-record-ui-identity-observed";
unsigned cases = 0, assertions = 0, consumes = 0, prefetchCases = 0, beforeCaptureCalls = 0;
const char* activeCase = "initialization";
void expect(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}
template<class Body> void test(const char* name, Body&& body) {
    activeCase = name;
    body();
    ++cases;
}
Json publicReply() {
    return {{"status", "ordinary-public-status"}, {"source", "ordinary-business-source"},
        {"reason", "ordinary-public-reason"}, {"ok", true}, {"count", 17},
        {"business", {{"name", "unchanged"}, {"enabled", false}, {"values", Json::array({1, "two", nullptr, 2.5})}}},
        {"nativeRecordDiagnostic", {{"status", "unavailable"}, {"recordState", "unknown"},
            {"authority", "diagnostic-only-no-admission"}}}};
}
Json validPrivate() {
    return {{"source", "steam-ui-exact-native-record-identity"}, {"status", "observed"},
        {"steamid64", std::to_string(kSteamId)}, {"gameid64", std::to_string(kGameId)},
        {"appid", Json(std::uint64_t(kAppId))}};
}
Json replyWith(Json privateValue) {
    // Extra private material must disappear along with the identity. It is never
    // part of expected public output, even on a malformed identity response.
    if (privateValue.is_object()) {
        privateValue["privateCanary"] = kCanary;
        privateValue["untrustedExtra"] = {{"accountShadow", kSteamId}, {"gameShadow", std::to_string(kGameId)},
            {"nested", Json::array({kCanary, {{"rawAccount", std::to_string(kSteamId)}}})}};
    }
    auto reply = publicReply();
    reply[kPrivateKey] = std::move(privateValue);
    return reply;
}
void checkPrivacy(const Json& reply, const Json& expectedPublic, const identity::Read& result) {
    expect(reply == expectedPublic, "ordinary public fields changed");
    const auto serialized = reply.dump();
    expect(serialized == expectedPublic.dump(), "public JSON representation changed");
    expect(!reply.contains(kPrivateKey) && serialized.find(kPrivateKey) == std::string::npos, "private key survived");
    expect(serialized.find(kCanary) == std::string::npos, "original private object survived");
    for (const auto secret : {kSteamId, kGameId, kMaximum})
        expect(serialized.find(std::to_string(secret)) == std::string::npos, "identity value survived in public JSON");
    const std::array<const char*, 6> allowedReasons = {kDefaultReason, kObservedReason,
        "native-record-ui-identity-schema-changed", "native-record-ui-identity-raced",
        "native-record-ui-game-unavailable", "native-record-ui-account-unavailable"};
    expect(std::find(allowedReasons.begin(), allowedReasons.end(), result.reason) != allowedReasons.end(),
        "untrusted reason escaped allowlist");
    expect(result.reason.find(kCanary) == std::string::npos, "private canary leaked through reason");
    for (const auto secret : {kSteamId, kGameId, kMaximum})
        expect(result.reason.find(std::to_string(secret)) == std::string::npos, "identity leaked through reason");
    if (!result.available) expect(result.account64 == 0 && result.gameId64 == 0 && result.appid == 0,
        "failed parse retained partial or previous identity");
}
identity::Read consumeChecked(Json& reply, const Json& expectedPublic) {
    ++consumes;
    const auto result = identity::consume(reply);
    checkPrivacy(reply, expectedPublic, result);
    return result;
}
void rejectPrivate(Json privateValue, const char* expectedReason = kDefaultReason) {
    auto reply = replyWith(std::move(privateValue));
    const auto result = consumeChecked(reply, publicReply());
    expect(!result.available && result.reason == expectedReason, "malformed private identity accepted or wrong reason");
}
void acceptPrivate(Json privateValue, std::uint64_t account = kSteamId, std::uint64_t game = kGameId,
        std::uint32_t appid = kAppId) {
    auto reply = replyWith(std::move(privateValue));
    const auto result = consumeChecked(reply, publicReply());
    expect(result.available && result.reason == kObservedReason, "valid private identity rejected");
    expect(result.account64 == account && result.gameId64 == game && result.appid == appid, "integer precision or identity mismatch");
}
void decimalTests() {
    test("full SteamID64 and non-Steam GameID64", [] { acceptPrivate(validPrivate()); });
    test("uint64 maximum preserved exactly", [] {
        auto value = validPrivate(); value["steamid64"] = std::to_string(kMaximum); value["gameid64"] = std::to_string(kMaximum);
        acceptPrivate(value, kMaximum, kMaximum);
    });
    test("smallest nonzero decimal identities", [] {
        auto value = validPrivate(); value["steamid64"] = "1"; value["gameid64"] = "1";
        acceptPrivate(value, 1, 1);
    });
    test("unsigned range above int64", [] {
        auto value = validPrivate(); value["steamid64"] = "9223372036854775808"; value["gameid64"] = "9223372036854775809";
        acceptPrivate(value, 9223372036854775808ULL, 9223372036854775809ULL);
    });
    const std::vector<Json> invalid = {
        "", "0", "00", "01", "00076561198012345678", "017746352246272032768", "18446744073709551616",
        "99999999999999999999", "100000000000000000000", "-1", "+1", "-0", " 1", "1 ", "\t1", "1\n", "\r1",
        "1\r\n", "1 2", "1.0", "76561198012345678.0", "1e3", "7.6561198012345678e16", "NaN", "Infinity",
        "76561198012345678n", "17746352246272032768n", "BigInt(\"76561198012345678\")", "0x110000100000001",
        "0o123", "0b101", "1_000", "\"76561198012345678\"", "１２３", std::string("1\0", 2), std::string(1, '\xff'),
        nullptr, true, false, Json(std::int64_t(1)), Json(std::int64_t(-1)), Json(std::uint64_t(kSteamId)),
        Json(std::uint64_t(kGameId)), Json(std::uint64_t(kMaximum)), Json(1.0), Json(76561198012345678.0),
        Json::array(), Json::array({"76561198012345678"}), Json::object(),
        Json{{"type", "bigint"}, {"value", "76561198012345678"}}, Json::binary({1, 2, 3})
    };
    for (const auto* field : {"steamid64", "gameid64"}) for (const auto& bad : invalid)
        test("strict decimal field rejection and complete erasure", [&] {
            auto value = validPrivate(); value[field] = bad; rejectPrivate(value);
        });
}
void appidTests() {
    for (const auto id : {std::uint32_t(1), std::uint32_t(2147483648U), kAppId, std::uint32_t(4294967295U)}) {
        test("positive appid in signed JSON storage", [id] {
            auto value = validPrivate(); value["appid"] = Json(std::int64_t(id));
            expect(value["appid"].is_number_integer() && !value["appid"].is_number_unsigned(), "signed appid fixture");
            acceptPrivate(value, kSteamId, kGameId, id);
        });
        test("positive appid in unsigned JSON storage", [id] {
            auto value = validPrivate(); value["appid"] = Json(std::uint64_t(id));
            expect(value["appid"].is_number_unsigned(), "unsigned appid fixture");
            acceptPrivate(value, kSteamId, kGameId, id);
        });
    }
    const std::vector<Json> invalid = {Json(std::int64_t(0)), Json(std::uint64_t(0)), Json(std::int64_t(-1)),
        Json(std::numeric_limits<std::int64_t>::min()), Json(std::int64_t(4294967296LL)),
        Json(std::uint64_t(4294967296ULL)), Json(std::numeric_limits<std::int64_t>::max()), Json(kMaximum),
        Json(0.0), Json(-1.0), Json(1.0), Json(1.25), Json(4294967295.0), Json(4294967296.0),
        "1", "4131894616", "4294967295", "1n", nullptr, true, false, Json::array(), Json::object()};
    for (const auto& bad : invalid) test("strict nonzero uint32 appid rejection", [&] {
        auto value = validPrivate(); value["appid"] = bad; rejectPrivate(value);
    });
}
void schemaAndReasonTests() {
    for (const auto* field : {"source", "status", "steamid64", "gameid64", "appid"})
        test("missing required private field", [field] { auto value = validPrivate(); value.erase(field); rejectPrivate(value); });
    const std::vector<Json> discriminators = {"", "wrong", "OBSERVED", "observed ", " steam-ui-exact-native-record-identity",
        "steam-ui-exact-native-record-identity-extra", nullptr, true, false, Json(std::int64_t(1)), Json(1.0),
        Json::array({"observed"}), Json::object()};
    for (const auto* field : {"source", "status"}) for (const auto& bad : discriminators)
        test("wrong discriminator string or type", [&] { auto value = validPrivate(); value[field] = bad; rejectPrivate(value); });
    const std::vector<Json> nonObjects = {nullptr, true, false, Json(std::int64_t(-1)), Json(kMaximum), Json(1.5),
        std::to_string(kSteamId), std::string(kCanary), Json::array(),
        Json::array({{{"steamid64", std::to_string(kSteamId)}, {"privateCanary", kCanary}}}), Json::binary({4, 5})};
    for (const auto& bad : nonObjects) test("nonobject private payload is still erased", [&] { rejectPrivate(bad); });
    for (const auto& outer : {Json(), Json(true), Json(std::int64_t(5)), Json(1.5), Json("ordinary public text"), Json::array({1, "public"})})
        test("nonobject outer reply stays unchanged", [&] {
            auto reply = outer; const auto result = consumeChecked(reply, outer);
            expect(!result.available && result.reason == kDefaultReason, "nonobject reply accepted");
        });
    const std::vector<Json> maliciousReasons = {
        std::string("native-record-ui-identity-raced ") + std::to_string(kSteamId),
        std::to_string(kSteamId) + " / " + std::to_string(kGameId) + " " + kCanary,
        std::string("native-record-ui-identity-schema-changed:") + kCanary,
        std::string("native-record-ui-identity-raced") + '\0' + std::to_string(kSteamId),
        std::string("native-record-ui-identity-raced\n") + std::to_string(kSteamId),
        "unknown-reason", Json{{"steamid64", std::to_string(kSteamId)}, {"raw", kCanary}},
        Json::array({std::to_string(kGameId), kCanary}), Json(kSteamId), Json(1.5), true, nullptr
    };
    for (const auto& reason : maliciousReasons) {
        test("unavailable reason cannot carry sensitive text", [&] {
            auto value = validPrivate(); value["status"] = "unavailable"; value["reason"] = reason; rejectPrivate(value);
        });
        test("observed status replaces hostile reason with fixed reason", [&] {
            auto value = validPrivate(); value["reason"] = reason; acceptPrivate(value);
        });
        test("wrong source plus hostile reason cannot pass", [&] {
            auto value = validPrivate(); value["source"] = "wrong-source"; value["reason"] = reason; rejectPrivate(value);
        });
    }
    for (const auto* reason : {"native-record-ui-identity-schema-changed", "native-record-ui-identity-raced",
        "native-record-ui-game-unavailable", "native-record-ui-account-unavailable"})
        test("safe reason enum is retained after erasure", [reason] {
            auto value = validPrivate(); value["status"] = "unavailable"; value["reason"] = reason; rejectPrivate(value, reason);
        });
    test("minimal valid identity has no reason requirement", [] { acceptPrivate(validPrivate()); });
    test("field spelling is exact", [] {
        auto value = validPrivate(); value["steamId64"] = value["steamid64"]; value.erase("steamid64"); rejectPrivate(value);
    });
}
void publicAndRetryTests() {
    for (const auto& publicStatus : {Json("observed"), Json("unavailable"), Json("accepted"), Json(nullptr), Json(7), Json::object()})
        for (const auto valid : {false, true}) test("public status and ordinary fields are preserved", [&] {
            auto expected = publicReply(); expected["status"] = publicStatus;
            auto reply = expected; auto payload = validPrivate(); if (!valid) payload["gameid64"] = "01";
            reply[kPrivateKey] = payload;
            const auto result = consumeChecked(reply, expected);
            expect(result.available == valid, "private parse unexpectedly depends on public status");
        });
    for (const auto& original : {publicReply(), Json::object(), Json{{"status", "unavailable"}, {"reason", "ordinary"}}})
        test("missing private field is a no-op", [&] {
            auto reply = original; const auto result = consumeChecked(reply, original);
            expect(!result.available && result.reason == kDefaultReason, "missing private field inherited identity");
        });
    test("same sanitized reply cannot reuse successful identity", [] {
        auto reply = replyWith(validPrivate()); auto result = consumeChecked(reply, publicReply());
        expect(result.available && result.account64 == kSteamId, "initial success");
        for (unsigned retry = 0; retry < 3; ++retry) {
            result = consumeChecked(reply, publicReply());
            expect(!result.available && result.reason == kDefaultReason, "sanitized retry retained old identity");
        }
    });
    test("same reply rejects new malformed identity after success", [] {
        auto reply = replyWith(validPrivate()); auto result = consumeChecked(reply, publicReply()); expect(result.available, "initial success");
        auto malformed = validPrivate(); malformed["gameid64"] = "18446744073709551616";
        reply[kPrivateKey] = malformed; result = consumeChecked(reply, publicReply());
        expect(!result.available && result.account64 == 0 && result.gameId64 == 0 && result.appid == 0, "partial parse reused old identity");
        result = consumeChecked(reply, publicReply()); expect(!result.available, "retry after malformed payload inherited identity");
    });
    test("same reply accepts fresh identity and then forgets it", [] {
        auto reply = replyWith(validPrivate()); auto result = consumeChecked(reply, publicReply()); expect(result.available, "initial success");
        auto fresh = validPrivate(); fresh["steamid64"] = std::to_string(kMaximum); fresh["gameid64"] = "1"; fresh["appid"] = Json(std::int64_t(1));
        reply[kPrivateKey] = fresh; result = consumeChecked(reply, publicReply());
        expect(result.available && result.account64 == kMaximum && result.gameId64 == 1 && result.appid == 1, "fresh identity not used");
        result = consumeChecked(reply, publicReply()); expect(!result.available, "fresh identity cached across sanitized retry");
    });
}
void prefetchWitnessTests() {
    constexpr std::uint64_t fileTime = 134365500000000000ULL;
    struct Boundary {
        const char* name;
        identity::PrefetchWitness witness;
        std::uint64_t generation, epoch, captureTick, captureFileTime;
        bool accepted;
    };
    // Expected outcomes are explicit boundary fixtures. Only the production
    // predicate is evaluated; there is no parallel freshness implementation.
    const std::vector<Boundary> boundaries = {
        {"prefetch exact capture tick and filetime", {7,11,100000,fileTime}, 7,11,100000,fileTime, true},
        {"prefetch prior observation in both clocks", {7,11,100000,fileTime}, 7,11,100001,fileTime+1, true},
        {"prefetch 59999ms old", {7,11,100000,fileTime}, 7,11,159999,fileTime+1, true},
        {"prefetch 60000ms inclusive", {7,11,100000,fileTime}, 7,11,160000,fileTime+1, true},
        {"prefetch 60001ms expired", {7,11,100000,fileTime}, 7,11,160001,fileTime+1, false},
        {"prefetch future tick with equal filetime", {7,11,100001,fileTime}, 7,11,100000,fileTime, false},
        {"prefetch future filetime with equal tick", {7,11,100000,fileTime+1}, 7,11,100000,fileTime, false},
        {"prefetch regressed tick despite advancing filetime", {7,11,100001,fileTime}, 7,11,100000,fileTime+1, false},
        {"prefetch regressed filetime despite advancing tick", {7,11,100000,fileTime+1}, 7,11,100001,fileTime, false},
        {"prefetch both clocks regressed", {7,11,100001,fileTime+1}, 7,11,100000,fileTime, false},
        {"prefetch different generation only", {7,11,100000,fileTime}, 8,11,100000,fileTime, false},
        {"prefetch different epoch only", {7,11,100000,fileTime}, 7,12,100000,fileTime, false},
        {"prefetch both generation and epoch differ", {7,11,100000,fileTime}, 8,12,100000,fileTime, false},
        {"prefetch zero witness generation", {0,11,100000,fileTime}, 7,11,100000,fileTime, false},
        {"prefetch zero capture generation", {7,11,100000,fileTime}, 0,11,100000,fileTime, false},
        {"prefetch matching zero generations forbidden", {0,11,100000,fileTime}, 0,11,100000,fileTime, false},
        {"prefetch zero witness epoch", {7,0,100000,fileTime}, 7,11,100000,fileTime, false},
        {"prefetch zero capture epoch", {7,11,100000,fileTime}, 7,0,100000,fileTime, false},
        {"prefetch matching zero epochs forbidden", {7,0,100000,fileTime}, 7,0,100000,fileTime, false},
        {"prefetch zero observed tick", {7,11,0,fileTime}, 7,11,1,fileTime, false},
        {"prefetch zero capture tick", {7,11,1,fileTime}, 7,11,0,fileTime, false},
        {"prefetch matching zero ticks forbidden", {7,11,0,fileTime}, 7,11,0,fileTime, false},
        {"prefetch zero observed filetime", {7,11,100000,0}, 7,11,100000,1, false},
        {"prefetch zero capture filetime", {7,11,100000,1}, 7,11,100000,0, false},
        {"prefetch matching zero filetimes forbidden", {7,11,100000,0}, 7,11,100000,0, false},
        {"prefetch all zero inputs forbidden", {}, 0,0,0,0, false},
        {"prefetch minimum nonzero values", {1,1,1,1}, 1,1,1,1, true},
        {"prefetch 60000ms from minimum nonzero tick", {1,1,1,1}, 1,1,60001,1, true},
        {"prefetch 60001ms from minimum nonzero tick", {1,1,1,1}, 1,1,60002,1, false},
        {"prefetch maximum values are valid when equal", {kMaximum,kMaximum,kMaximum,kMaximum},
            kMaximum,kMaximum,kMaximum,kMaximum, true},
        {"prefetch unsigned generation and epoch high bits", {0x8000000000000000ULL,0x8000000000000001ULL,1,1},
            0x8000000000000000ULL,0x8000000000000001ULL,1,1, true},
        {"prefetch generation wrap cannot rebind witness", {kMaximum,11,100000,fileTime}, 1,11,100000,fileTime, false},
        {"prefetch epoch wrap cannot rebind witness", {7,kMaximum,100000,fileTime}, 7,1,100000,fileTime, false},
        {"prefetch maximum tick with one millisecond age", {7,11,kMaximum-1,fileTime}, 7,11,kMaximum,fileTime, true},
        {"prefetch maximum tick with 59999ms age", {7,11,kMaximum-59999,fileTime}, 7,11,kMaximum,fileTime, true},
        {"prefetch maximum tick with 60000ms age", {7,11,kMaximum-60000,fileTime}, 7,11,kMaximum,fileTime, true},
        {"prefetch maximum tick with 60001ms age", {7,11,kMaximum-60001,fileTime}, 7,11,kMaximum,fileTime, false},
        {"prefetch huge genuine age cannot truncate", {7,11,1,fileTime}, 7,11,kMaximum,fileTime, false},
        {"prefetch huge age cannot narrow to signed negative", {7,11,1,fileTime}, 7,11,0x8000000000000001ULL,fileTime, false},
        {"prefetch regression from maximum cannot wrap to age two", {7,11,kMaximum,fileTime}, 7,11,1,fileTime, false},
        {"prefetch regression near maximum cannot wrap to age three", {7,11,kMaximum-1,fileTime}, 7,11,1,fileTime, false},
        {"prefetch regression wrapping to exactly 60000 is forbidden", {7,11,kMaximum-59998,fileTime}, 7,11,1,fileTime, false},
        {"prefetch future maximum tick cannot underflow", {7,11,kMaximum,fileTime}, 7,11,kMaximum-1,fileTime, false},
        {"prefetch maximum filetime with valid prior ordering", {7,11,100000,kMaximum-1}, 7,11,100001,kMaximum, true},
        {"prefetch maximum filetime regression forbidden", {7,11,100000,kMaximum}, 7,11,100001,kMaximum-1, false},
        {"prefetch filetime wrap cannot restore ordering", {7,11,100000,kMaximum}, 7,11,100001,1, false},
        {"prefetch future filetime still forbidden at freshness boundary", {7,11,100000,fileTime+1}, 7,11,160000,fileTime, false},
        {"prefetch valid filetime cannot rescue expired tick", {7,11,100000,fileTime}, 7,11,160001,kMaximum, false}
    };
    for (const auto& boundary : boundaries) test(boundary.name, [&] {
        ++prefetchCases;
        ++beforeCaptureCalls;
        expect(identity::beforeCapture(boundary.witness, boundary.generation, boundary.epoch,
            boundary.captureTick, boundary.captureFileTime) == boundary.accepted,
            "prefetch witness boundary decision");
    });
    test("prefetch witness is re-evaluated for each capture without a lease", [&] {
        ++prefetchCases;
        const identity::PrefetchWitness witness{7,11,100000,fileTime};
        beforeCaptureCalls += 5;
        expect(identity::beforeCapture(witness,7,11,160000,fileTime), "boundary witness should be fresh");
        expect(!identity::beforeCapture(witness,7,11,160001,fileTime), "previous acceptance must expire");
        expect(!identity::beforeCapture(witness,8,11,160000,fileTime), "previous acceptance must not cross generation");
        expect(!identity::beforeCapture(witness,7,12,160000,fileTime), "previous acceptance must not cross epoch");
        expect(!identity::beforeCapture(witness,7,11,99999,fileTime), "previous acceptance must not survive tick regression");
    });
    // A true predicate describes only the cached diagnostic identity's source
    // and freshness. It supplies no action authority or login-generation lease.
}
} // namespace
int main() {
    try {
        decimalTests(); appidTests(); schemaAndReasonTests(); publicAndRetryTests(); prefetchWitnessTests();
        std::cout << Json({{"status", "passed"}, {"cases", cases}, {"assertions", assertions}, {"consumeCalls", consumes},
            {"prefetchCases", prefetchCases}, {"beforeCaptureCalls", beforeCaptureCalls}, {"diagnosticBindingOnly", true},
            {"privatePayloadsPersisted", false}, {"realProcessReads", 0}, {"observeCalls", 0},
            {"sleepRecoveryVerified", false}}).dump() << '\n';
        return 0;
    } catch (...) {
        // Never print untrusted JSON, private identities, or exception contents.
        std::cerr << "identity selftest failed in case: " << activeCase << '\n';
        return 1;
    }
}
