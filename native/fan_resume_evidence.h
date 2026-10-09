#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <stdexcept>
#include <utility>
#include <initializer_list>
#include "json.hpp"

namespace ymcc::fan_session {
struct ResumeReply {
    bool parsed = false, noop = false, accepted = false, ready = false;
    bool unknown = false, faultLocked = false, cleanupPending = false;
    std::string stateName, phase, powerState, opStatus, opOutcome, errorCode;
    long long retryAfterMs = 0, attempt = 0;
    long long responseGeneration = -1, phaseGeneration = -1, operationGeneration = -1;
    int controlAccepting = -1, openCalled = -1, openEventsCalled = -1, fanCapabilitySupported = -1;
};
enum class ResumeVerdict { Pending, Recovered, NoWork, Failed, Superseded };
inline std::string text(const nlohmann::json& value, const char* field) {
    const auto it = value.find(field);
    if (it == value.end() || it->is_null()) return {};
    return it->get<std::string>();
}
inline long long generation(const nlohmann::json& value, const char* field) {
    const auto it = value.find(field);
    if (it == value.end()) return -1;
    if (!it->is_number_integer() || (it->is_number_unsigned()
        && it->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<long long>::max())))
        throw std::invalid_argument("invalid power generation");
    const auto result = it->get<long long>();
    if (result < 0) throw std::invalid_argument("negative power generation");
    return result;
}
inline int boolean(const nlohmann::json& value, const char* field) {
    const auto it = value.find(field);
    if (it == value.end()) return -1;
    return it->get<bool>() ? 1 : 0;
}
/** Parse into a temporary value: malformed/null fields cannot leave a partial
 * parsed=true/accepted=true object for the production classifier to consume. */
inline bool parseResumeReply(const std::string& body, ResumeReply* out) {
    if (!out) return false;
    *out = ResumeReply{};
    try {
        const auto root = nlohmann::json::parse(body);
        if (!root.is_object()) return false;
        const auto& state = root.contains("state") && root["state"].is_object() ? root["state"] : root;
        ResumeReply result;
        result.noop = boolean(root, "resumeNoop") == 1;
        result.accepted = boolean(root, "resumeAccepted") == 1;
        if (root.contains("retryAfterMs")) result.retryAfterMs = generation(root, "retryAfterMs");
        if (root.contains("attempt")) result.attempt = generation(root, "attempt");
        result.responseGeneration = generation(root, "generation");
        result.phaseGeneration = generation(state, "resumePhaseGeneration");
        result.operationGeneration = generation(state, "powerOperationGeneration");
        result.phase = text(root, "resumePhase");
        if (result.phase.empty()) result.phase = text(state, "resumePhase");
        result.stateName = text(state, "state");
        result.powerState = text(state, "powerState");
        result.opStatus = text(state, "powerOperationStatus");
        result.opOutcome = text(state, "powerOperationOutcome");
        if (root.contains("error")) {
            const auto& error = root["error"];
            result.errorCode = error.is_object() ? text(error, "code") : error.get<std::string>();
        }
        result.unknown = boolean(state, "unknownState") == 1;
        result.cleanupPending = boolean(state, "hcCloseCleanupPending") == 1;
        result.controlAccepting = boolean(state, "controlAccepting");
        result.openCalled = boolean(state, "openCalled");
        result.openEventsCalled = boolean(state, "openEventsCalled");
        result.fanCapabilitySupported = state.contains("fanCapabilitySupported") && !state["fanCapabilitySupported"].is_null()
            ? boolean(state, "fanCapabilitySupported") : -1;
        result.faultLocked = result.stateName == "FaultLocked" || result.stateName == "ListenerFaultLocked";
        result.ready = result.stateName == "Ready" && !result.unknown && !result.faultLocked && !result.cleanupPending;
        result.parsed = true;
        *out = std::move(result);
        return true;
    } catch (...) { return false; }
}
inline bool isTerminalResumeRejection(unsigned long status, const ResumeReply& r) {
    if (status == 401 || status == 403) return true;
    return r.errorCode == "HC_RESUME_RESULT_EXPIRED" || r.errorCode == "HC_RESUME_SUPERSEDED"
        || r.errorCode == "HC_RESUME_REBUILD_FAILED" || r.errorCode == "FAULT_LOCKED"
        || r.errorCode == "FAN_UNSUPPORTED" || r.errorCode == "API_SESSION_REQUIRED"
        || r.errorCode == "FAN_ROUTE_CONFLICT" || r.errorCode == "HC_OPENLIB_CONFLICT"
        || r.errorCode == "EXTERNAL_FAN_OWNER" || r.errorCode == "HC_CLOSE_PENDING"
        || r.errorCode == "HOST_CLOSING";
}
/** No route-specific writes: classify software lifecycle evidence only. Missing
 * fields retain legacy compatibility; explicit denials/transitions/generations
 * always override the old Ready label. */
inline ResumeVerdict classifyResumeReply(const ResumeReply& r, std::uint64_t expectedGeneration) {
    if (!r.parsed) return ResumeVerdict::Pending;
    if (r.errorCode == "HC_RESUME_SUPERSEDED" || r.errorCode == "HC_RESUME_RESULT_EXPIRED") return ResumeVerdict::Superseded;
    if (!r.errorCode.empty() || r.unknown || r.faultLocked || r.cleanupPending || r.fanCapabilitySupported == 0) return ResumeVerdict::Failed;
    if (r.phaseGeneration >= 0 && r.operationGeneration >= 0 && r.phaseGeneration != r.operationGeneration) return ResumeVerdict::Failed;
    const long long receipt = r.phaseGeneration >= 0 ? r.phaseGeneration : r.operationGeneration;
    for (const auto actual : {r.responseGeneration, receipt}) {
        if (actual >= 0 && static_cast<std::uint64_t>(actual) != expectedGeneration)
            return static_cast<std::uint64_t>(actual) > expectedGeneration ? ResumeVerdict::Superseded : ResumeVerdict::Pending;
    }
    if (r.opStatus == "failed") return ResumeVerdict::Failed;
    if (r.opStatus == "superseded") return ResumeVerdict::Superseded;
    if (r.phase == "Resuming" || r.powerState == "Resuming" || r.powerState == "Suspending"
        || r.powerState == "Suspended" || r.opStatus == "pending" || r.opStatus == "executing") return ResumeVerdict::Pending;
    if (r.stateName == "AwaitingControl" && (r.opOutcome == "await-user" || r.opStatus == "completed")) return ResumeVerdict::NoWork;
    if (r.ready && r.controlAccepting != 0 && r.openCalled != 0 && r.openEventsCalled != 0) return ResumeVerdict::Recovered;
    return ResumeVerdict::Pending;
}
} // namespace ymcc::fan_session
