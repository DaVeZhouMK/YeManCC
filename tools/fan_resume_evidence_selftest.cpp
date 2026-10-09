#include "fan_resume_evidence.h"
#include <iostream>
#include <stdexcept>
using namespace ymcc::fan_session;
static int checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
int main() {
    using nlohmann::json;
    const auto base = json{{"state", "Ready"}, {"powerState", "On"}, {"resumePhase", "Ready"},
        {"resumePhaseGeneration", 2}, {"powerOperationGeneration", 2}, {"powerOperationStatus", "completed"},
        {"openCalled", true}, {"openEventsCalled", true}, {"controlAccepting", true},
        {"unknownState", false}, {"hcCloseCleanupPending", false}, {"fanCapabilitySupported", true}};
    auto evaluate = [&](json state) { ResumeReply r; check(parseResumeReply(json{{"ok", true}, {"state", state}}.dump(), &r), "valid response parsed"); return classifyResumeReply(r, 2); };
    check(evaluate(base) == ResumeVerdict::Recovered, "current complete session accepted");
    for (const auto status : {"pending", "executing", "failed", "superseded"}) {
        auto state = base; state["powerOperationStatus"] = status;
        check(evaluate(state) != ResumeVerdict::Recovered, "Ready cannot override operation");
    }
    for (const auto phase : {"Suspending", "Suspended", "Resuming"}) {
        auto state = base; state["powerState"] = phase;
        check(evaluate(state) != ResumeVerdict::Recovered, "Ready cannot override power transition");
    }
    for (const auto field : {"controlAccepting", "openCalled", "openEventsCalled", "fanCapabilitySupported"}) {
        auto state = base; state[field] = false; check(evaluate(state) != ResumeVerdict::Recovered, "explicit false veto");
    }
    for (const auto field : {"unknownState", "hcCloseCleanupPending"}) {
        auto state = base; state[field] = true; check(evaluate(state) == ResumeVerdict::Failed, "fault/cleanup veto");
    }
    for (long long target = 1; target <= 70; ++target) {
        for (long long actual = 0; actual <= 71; ++actual) {
            auto state = base; state["resumePhaseGeneration"] = actual; state["powerOperationGeneration"] = actual;
            ResumeReply reply; check(parseResumeReply(json{{"state", state}}.dump(), &reply), "matrix response parsed");
            check((classifyResumeReply(reply, static_cast<std::uint64_t>(target)) == ResumeVerdict::Recovered) == (actual == target), "exact-generation matrix");
        }
    }
    auto conflict = base; conflict["powerOperationGeneration"] = 3;
    check(evaluate(conflict) == ResumeVerdict::Failed, "contradictory generations denied");
    auto noWork = base; noWork["state"] = "AwaitingControl"; noWork["resumePhase"] = "AwaitingControl"; noWork["powerOperationOutcome"] = "await-user";
    check(evaluate(noWork) == ResumeVerdict::NoWork, "empty restore is no-work, not recovered");
    ResumeReply r;
    check(parseResumeReply(R"({"state":{"state":"Ready","unknownState":false,"hcCloseCleanupPending":false}})", &r), "legacy parsed");
    check(classifyResumeReply(r, 2) == ResumeVerdict::Recovered, "legacy absent fields remain compatible");
    check(parseResumeReply(R"({"state":{"state":"Ready","powerOperationStatus":null}})", &r), "nullable status parsed atomically");
    for (const auto field : {"resumePhaseGeneration", "powerOperationGeneration", "controlAccepting", "openCalled", "openEventsCalled", "unknownState"}) {
        auto malformed = base; malformed[field] = "wrong-type";
        check(!parseResumeReply(json{{"resumeAccepted", true}, {"state", malformed}}.dump(), &r), "malformed parse rejected");
        check(!r.parsed && !r.accepted && !r.ready, "partial flags never escape parser");
    }
    for (const auto value : {json(-1), json(1.5), json(nullptr), json("2")}) {
        auto state = base; state["resumePhaseGeneration"] = value;
        check(!parseResumeReply(json{{"state", state}}.dump(), &r), "invalid generation rejected");
        check(!r.parsed, "invalid generation cannot become a receipt");
    }
    for (const auto code : {"HC_RESUME_RESULT_EXPIRED", "HC_RESUME_SUPERSEDED", "HC_RESUME_REBUILD_FAILED", "API_SESSION_REQUIRED", "FAN_UNSUPPORTED"}) {
        check(parseResumeReply(json{{"state", base}, {"error", {{"code", code}}}}.dump(), &r), "error parsed");
        check(classifyResumeReply(r, 2) != ResumeVerdict::Recovered, "error overrides Ready");
    }
    check(isTerminalResumeRejection(401, ResumeReply{}), "401 never waits");
    check(isTerminalResumeRejection(403, ResumeReply{}), "403 never waits");
    ResumeReply transient; transient.errorCode = "POWER_RESUMING";
    check(!isTerminalResumeRejection(409, transient), "genuine resume transient stays bounded");
    for (const auto code : {"HC_RESUME_REBUILD_FAILED", "API_SESSION_REQUIRED", "FAN_UNSUPPORTED", "FAN_ROUTE_CONFLICT"}) {
        ResumeReply rejected; rejected.errorCode = code;
        check(isTerminalResumeRejection(409, rejected), "known rejection never becomes a Ready deadline");
    }
    std::cout << "{\"ok\":true,\"checks\":" << checks << ",\"hardwareCalls\":0,\"networkCalls\":0,\"productionHeader\":\"fan_resume_evidence.h\"}\n";
}
