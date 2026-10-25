#pragma once
#include <cmath>
#include <string>
#include <string_view>
#include "../deps/json/json.hpp"

// Fan wire receipts only. No HC/device calls, persisted cache, or hardware claim.
namespace ymcc::fan_response {
using Json = nlohmann::json;
inline bool leaseIdValid(const std::string& id) noexcept {
    if (id.size() < 8 || id.size() > 80) return false;
    for (unsigned char c : id) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}
inline bool positiveNumber(const Json& value, bool integer) {
    if (!value.is_number()) return false;
    const double number = value.get<double>();
    return std::isfinite(number) && number > 0 && (!integer ||
        (number <= 9007199254740991.0 && std::floor(number) == number));
}
inline bool leaseValid(const Json& value) {
    if (!value.is_object()) return false;
    const auto id = value.find("leaseId");
    if (id == value.end() || !id->is_string() || !leaseIdValid(id->get_ref<const std::string&>())) return false;
    const auto generation = value.find("generation"), expiry = value.find("expiresAtMonoMs"), owner = value.find("owner");
    return (generation == value.end() || positiveNumber(*generation, true)) &&
        (expiry == value.end() || positiveNumber(*expiry, false)) && (owner == value.end() || owner->is_string());
}
inline const Json* stateFromReply(const std::string& path, const Json& response) {
    if (!response.is_object()) return nullptr;
    const auto state = response.find("state");
    if (state != response.end() && state->is_object()) return &*state;
    const bool legacy = path == "/api/open" || path == "/api/open-events" || path == "/api/release-control" ||
        path == "/api/restore" || path == "/api/suspend" || path == "/api/resume" || path == "/api/close";
    return legacy && state != response.end() && state->is_string() ? &response : nullptr;
}
inline bool stateValid(const Json& state) {
    if (!state.is_object()) return false;
    const auto name = state.find("state");
    if (name == state.end() || !name->is_string() || name->get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos) return false;
    for (const char* field : {"hardwareWrites", "hardwareWritesEnabled", "hardwareWritesObserved", "unknownState", "openCalled", "openEventsCalled",
        "oemRestoreConfirmed", "hcVirtualCloseReturned", "hcDeviceManagerStopCompleted", "oemPhysicalOwnershipConfirmed", "hcCloseCleanupPending", "controlAccepting"}) {
        const auto value = state.find(field);
        if (value != state.end() && !value->is_boolean()) return false;
    }
    const auto lease = state.find("lease"), generation = state.find("leaseGeneration");
    return (lease == state.end() || lease->is_null() || leaseValid(*lease)) &&
        (generation == state.end() || generation->is_null() || positiveNumber(*generation, true));
}
inline const Json* leaseFromReply(const Json& response) {
    if (!response.is_object()) return nullptr;
    const auto direct = response.find("lease");
    if (direct != response.end() && !direct->is_null()) return &*direct;
    const auto result = response.find("result");
    if (result != response.end() && result->is_object()) {
        const auto nested = result->find("lease");
        if (nested != result->end() && !nested->is_null()) return &*nested;
    }
    const auto state = response.find("state");
    if (state != response.end() && state->is_object()) {
        const auto nested = state->find("lease");
        if (nested != state->end()) return &*nested;
    }
    return nullptr;
}
// The returned string is owned by this response, never a cross-response cache.
inline const std::string* keepaliveLeaseReference(const std::string& path, const Json& response, const std::string& requestLease) {
    const auto lease = leaseFromReply(response);
    if (!lease || !leaseValid(*lease)) return nullptr;
    const auto& id = (*lease)["leaseId"].get_ref<const std::string&>();
    if (path == "/api/heartbeat" && id != requestLease) return nullptr;
    if ((path == "/api/enable" || path == "/api/preset") && !requestLease.empty() && id != requestLease) return nullptr;
    return &id;
}
// Compatibility callers receive an independent string.
inline std::string keepaliveLease(const std::string& path, const Json& response, const std::string& requestLease) {
    const auto* id = keepaliveLeaseReference(path, response, requestLease);
    return id ? *id : std::string{};
}
inline bool accepted(const std::string& path, unsigned status, const Json& response, const std::string& requestLease = {}) {
    if (status < 200 || status >= 300 || !response.is_object()) return false;
    const auto ok = response.find("ok");
    if (ok != response.end() && (!ok->is_boolean() || !ok->get<bool>())) return false;
    if (path == "/api/acquire-control" || path == "/api/heartbeat") return keepaliveLeaseReference(path, response, requestLease) != nullptr;
    if (path == "/api/shutdown" || path == "/api/parent-exit") return true;
    const auto state = stateFromReply(path, response);
    if (!state || !stateValid(*state)) return false;
    if (path == "/api/cancel-control-intent") {
        const auto cancelled = response.find("controlIntentCancelled");
        if (cancelled == response.end() || !cancelled->is_boolean() || !cancelled->get<bool>()) return false;
    }
    return true;
}
// Arm/replace keeps its existing start schedule. A retained lease advances only
// on acquire/heartbeat; Enable/Preset validate a lease without extending its TTL.
inline bool leaseClockMayAdvance(bool first, bool changed, const char* source) noexcept {
    if (first || changed) return true;
    const std::string_view endpoint = source ? source : "";
    return endpoint == "/api/acquire-control" || endpoint == "/api/heartbeat";
}
inline bool heartbeatStillCurrent(bool armed, const std::string& current, const std::string& requested) noexcept {
    return armed && current == requested;
}
} // namespace ymcc::fan_response
