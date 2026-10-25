#pragma once
#include "json.hpp"
#include <initializer_list>
#include <string>

// Only the ordinary log argument is projected. Full snapshots, history and
// exports retain their diagnostic objects; this policy never infers Running.
namespace ymcc::steamlink::logpolicy {
using Json = nlohmann::json;

inline Json compact(const Json& detail) {
    const auto copy = [](const Json& source, Json& target, std::initializer_list<const char*> keys) {
        if (!source.is_object()) return;
        for (const auto* key : keys) {
            const auto it = source.find(key);
            if (it == source.end()) continue;
            if (it->is_null() || it->is_boolean() || it->is_number_integer() ||
                (it->is_string() && it->get_ref<const std::string&>().size() <= 256)) target[key] = *it;
        }
    };
    const auto exactString = [](const Json& source, Json& target, const char* key) {
        if (!source.is_object()) return;
        const auto it = source.find(key);
        if (it != source.end() && it->is_string() && it->get_ref<const std::string&>().size() <= 256)
            target[key] = *it; // Never round-trip a full GameID through a number.
    };
    Json out = Json::object();
    copy(detail, out, {"schemaVersion", "implementation", "readOnly", "evidenceScope",
        "repairAuthorized", "postWakeRetargetAllowed", "gameInputVerified", "continueButtonVerified",
        "steamWrites", "gameLaunches", "focusWrites", "deviceWrites", "sleepWrites",
        "phase", "reason", "linked", "contactConnected", "transportConnected", "active", "terminal",
        "linkSerial", "configEpoch", "observationSequence", "observedAtTick", "dueTick", "readFailures",
        "appid", "originalPid", "originalCreated", "verifiedReadAvailable",
        "gameIdentitySame", "steamIdentitySame", "accountMatched", "trackerExact", "nativePrimaryExact",
        "ymccSinglePidAccepted", "nativeOverviewAssociated"});
    exactString(detail, out, "trackerGameIdExactString");
    const auto child = [&](const char* key, std::initializer_list<const char*> keys) {
        const auto it = detail.find(key);
        if (it != detail.end() && it->is_object()) copy(*it, out[key] = Json::object(), keys);
    };
    child("originalGame", {"pid", "created", "windowsSession"});
    child("steamClient", {"pid", "created"});
    child("tracker", {"complete", "status", "appid"});
    const auto tracker = detail.find("tracker");
    if (tracker != detail.end() && tracker->is_object())
        exactString(*tracker, out["tracker"], "gameIdExactString");
    // Keep the native Running witness and its unknown/failure status, but omit
    // receipts, callback inventories and nested event payloads.
    child("nativeAppLifetimeFeedback", {"implementation", "authority", "available", "reason",
        "eventRelation", "instanceRelation", "originalAppRunningKnown", "originalAppRunning",
        "latestEventSequence", "originalEventCount", "otherAppEventCount", "schemaInvalidEvents"});
    return out;
}
} // namespace ymcc::steamlink::logpolicy
