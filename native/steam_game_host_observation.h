#pragma once
#include "steam_game_process_facts.h"

// PRIVATE SOURCE-ONLY CANDIDATE. Not compiled, imported or SOURCE_READY.
// Value projection of ONE existing inspectHost result, made before that result
// is moved. No discovery, HANDLE ownership, current(), parsing, timer or action.
// The result and ProcessLease MUST belong to the same inspectHost invocation.
// Stages describe that historical invocation; they are never current proof.
namespace ymcc::steamgamefacts {

struct HostProcessObservation {
    std::optional<DWORD> pid;
    std::optional<std::uint64_t> created;
};

enum class HostInspectionStage : std::uint8_t {
    Unknown, RequestCurrent, HostPidInput, RegistrationInput, HostOpen,
    HostIdentity, HostBirth, HostImage, HostParent, HostSession,
    CommandLine, Arguments, Registration, FinalCurrent, Accepted
};
enum class HostInspectionCheck : std::uint8_t {
    NotReached, Passed, Rejected, Unavailable
};

struct HostInspectionQueryCost {
    // Counts are inferred ORIGINAL helper invocations, not measured Win32 calls
    // or elapsed time. Zero counts are meaningful only when inferred is true.
    bool inferred = false;
    unsigned hostOpenAttempts = 0;
    unsigned hostWitnessBatchCalls = 0; // Original GetProcessId/birth/image block.
    unsigned parentReadCalls = 0;
    unsigned sessionReadCalls = 0;
    unsigned commandLineReadCalls = 0;
    unsigned argumentParseCalls = 0;
    unsigned registrationFactCalls = 0;
    // This projection performs none of these. Original current()/alive() and
    // lower-level NtQuery/argv calls are deliberately NOT fabricated here.
    unsigned addedHostOpens = 0;
    unsigned addedProcessQueries = 0;
    unsigned addedCurrentCalls = 0;
    unsigned addedClockReads = 0;
    bool totalNativeCallsMeasured = false;
    bool elapsedMeasured = false;
};

struct HostInspectionObservation {
    HostInspectionStage stage = HostInspectionStage::Unknown;
    HostRejection rejection = HostRejection::RequestNotCurrent;
    RegistrationFact fact = RegistrationFact::Unknown;
    HostProcessObservation actualGame, selectedGame, steam;
    std::optional<DWORD> requestedHostPid;
    // Presence means the ORIGINAL witness block ran. Each scalar can still be
    // absent on HostIdentityMismatch. No image path or raw command is copied.
    std::optional<HostProcessObservation> hostWitness;
    std::optional<bool> hostImageObserved;
    std::optional<DWORD> parentPid, windowsSession;
    std::optional<bool> commandLineAvailable;
    std::optional<Arguments> parsedRegistration;
    std::optional<std::uint32_t> trackerAppId, reportedAppId;
    std::optional<std::uint64_t> trackerFullGameId, reportedGameId;
    std::optional<bool> registeredGamePidMatchesActual;
    std::optional<bool> registeredSteamPidMatchesSteam;
    std::optional<bool> registeredGameIdMatchesReported;
    HostInspectionCheck identityCheck = HostInspectionCheck::NotReached;
    HostInspectionCheck birthCheck = HostInspectionCheck::NotReached;
    HostInspectionCheck imageCheck = HostInspectionCheck::NotReached;
    HostInspectionCheck parentCheck = HostInspectionCheck::NotReached;
    HostInspectionCheck sessionCheck = HostInspectionCheck::NotReached;
    HostInspectionCheck registrationCheck = HostInspectionCheck::NotReached;
    bool hostLeasePresentAtReturn = false; // Pointer presence only, NOT current.
    HostInspectionQueryCost queryCost;
};

inline const char* hostInspectionStageName(HostInspectionStage stage) noexcept {
    switch (stage) {
    case HostInspectionStage::RequestCurrent: return "request-current";
    case HostInspectionStage::HostPidInput: return "host-pid-input";
    case HostInspectionStage::RegistrationInput: return "registration-input";
    case HostInspectionStage::HostOpen: return "host-open";
    case HostInspectionStage::HostIdentity: return "host-identity";
    case HostInspectionStage::HostBirth: return "host-birth";
    case HostInspectionStage::HostImage: return "host-image";
    case HostInspectionStage::HostParent: return "host-parent";
    case HostInspectionStage::HostSession: return "host-session";
    case HostInspectionStage::CommandLine: return "command-line";
    case HostInspectionStage::Arguments: return "arguments";
    case HostInspectionStage::Registration: return "registration";
    case HostInspectionStage::FinalCurrent: return "final-current";
    case HostInspectionStage::Accepted: return "accepted";
    default: return "unknown";
    }
}
inline const char* hostInspectionCheckName(HostInspectionCheck check) noexcept {
    switch (check) {
    case HostInspectionCheck::Passed: return "passed";
    case HostInspectionCheck::Rejected: return "rejected";
    case HostInspectionCheck::Unavailable: return "unavailable";
    default: return "not-reached";
    }
}

namespace host_observation_detail {
inline HostProcessObservation process(const ProcessWitness& witness) noexcept {
    HostProcessObservation out;
    if (witness.pid) out.pid = witness.pid;
    if (witness.created) out.created = witness.created;
    return out;
}
inline HostInspectionStage stage(const HostInspection& inspection) noexcept {
    switch (inspection.rejection) {
    case HostRejection::None: return HostInspectionStage::Accepted;
    case HostRejection::RequestNotCurrent:
        return inspection.parsedArguments ? HostInspectionStage::FinalCurrent :
            HostInspectionStage::RequestCurrent;
    case HostRejection::InvalidHostPid: return HostInspectionStage::HostPidInput;
    case HostRejection::RegistrationUnknown:
        return inspection.parsedArguments ? HostInspectionStage::Registration :
            HostInspectionStage::RegistrationInput;
    case HostRejection::HostUnavailable: return HostInspectionStage::HostOpen;
    case HostRejection::HostIdentityMismatch: return HostInspectionStage::HostIdentity;
    case HostRejection::HostBirthMismatch: return HostInspectionStage::HostBirth;
    case HostRejection::HostPathMismatch: return HostInspectionStage::HostImage;
    case HostRejection::HostParentMismatch: return HostInspectionStage::HostParent;
    case HostRejection::HostSessionMismatch: return HostInspectionStage::HostSession;
    case HostRejection::CommandLineUnavailable: return HostInspectionStage::CommandLine;
    case HostRejection::ArgumentsInvalid: return HostInspectionStage::Arguments;
    default: return HostInspectionStage::Unknown;
    }
}
inline bool reached(HostInspectionStage observed, HostInspectionStage checkpoint) noexcept {
    return observed != HostInspectionStage::Unknown &&
        static_cast<unsigned>(observed) >= static_cast<unsigned>(checkpoint);
}
inline HostInspectionCheck check(HostInspectionStage observed, HostInspectionStage checkpoint) noexcept {
    if (!reached(observed, checkpoint)) return HostInspectionCheck::NotReached;
    return observed == checkpoint ? HostInspectionCheck::Rejected : HostInspectionCheck::Passed;
}
}

inline HostInspectionObservation projectHostInspection(const HostInspection& inspection,
    const ProcessLease& process, DWORD requestedHostPid) noexcept {
    namespace observation = host_observation_detail;
    HostInspectionObservation out;
    out.stage = observation::stage(inspection);
    out.rejection = inspection.rejection;
    out.fact = inspection.fact; // In particular, preserve final-current Unknown.
    out.actualGame = observation::process(process.gameWitness());
    out.selectedGame = observation::process(process.selectedGameWitness());
    out.steam = observation::process(process.steamWitness());
    if (requestedHostPid) out.requestedHostPid = requestedHostPid;
    if (observation::reached(out.stage, HostInspectionStage::HostIdentity)) {
        out.hostWitness = observation::process(inspection.hostWitness);
        out.hostImageObserved = !inspection.hostWitness.image.empty();
    }
    if (observation::reached(out.stage, HostInspectionStage::HostParent) && inspection.parentPid)
        out.parentPid = inspection.parentPid;
    // Session 0 is a real known value; it is NOT an unknown sentinel.
    if (observation::reached(out.stage, HostInspectionStage::HostSession) && inspection.windowsSessionKnown)
        out.windowsSession = inspection.windowsSession;
    if (observation::reached(out.stage, HostInspectionStage::CommandLine))
        out.commandLineAvailable = inspection.rawCommandLine.has_value();
    out.parsedRegistration = inspection.parsedArguments; // POD copy, no parser.
    if (inspection.tracker.appid) out.trackerAppId = inspection.tracker.appid;
    if (inspection.tracker.fullGameId) out.trackerFullGameId = inspection.tracker.fullGameId;
    if (inspection.reportedRegistration) {
        if (inspection.reportedRegistration->appid) out.reportedAppId = inspection.reportedRegistration->appid;
        if (inspection.reportedRegistration->gameId) out.reportedGameId = inspection.reportedRegistration->gameId;
    }
    if (out.parsedRegistration) {
        if (out.actualGame.pid)
            out.registeredGamePidMatchesActual = out.parsedRegistration->gamePid == *out.actualGame.pid;
        if (out.steam.pid)
            out.registeredSteamPidMatchesSteam = out.parsedRegistration->steamPid == *out.steam.pid;
        if (out.reportedGameId)
            out.registeredGameIdMatchesReported = out.parsedRegistration->gameId == *out.reportedGameId;
    }
    out.identityCheck = observation::check(out.stage, HostInspectionStage::HostIdentity);
    out.birthCheck = observation::check(out.stage, HostInspectionStage::HostBirth);
    out.imageCheck = observation::check(out.stage, HostInspectionStage::HostImage);
    out.parentCheck = observation::check(out.stage, HostInspectionStage::HostParent);
    if (out.stage == HostInspectionStage::HostParent && !out.parentPid)
        out.parentCheck = HostInspectionCheck::Unavailable;
    out.sessionCheck = observation::check(out.stage, HostInspectionStage::HostSession);
    if (out.stage == HostInspectionStage::HostSession && !out.windowsSession)
        out.sessionCheck = HostInspectionCheck::Unavailable;
    out.registrationCheck = observation::check(out.stage, HostInspectionStage::Registration);
    out.hostLeasePresentAtReturn = static_cast<bool>(inspection.hostLease);
    out.queryCost.inferred = out.stage != HostInspectionStage::Unknown;
    out.queryCost.hostOpenAttempts = observation::reached(out.stage, HostInspectionStage::HostOpen) ? 1u : 0u;
    out.queryCost.hostWitnessBatchCalls = observation::reached(out.stage, HostInspectionStage::HostIdentity) ? 1u : 0u;
    out.queryCost.parentReadCalls = observation::reached(out.stage, HostInspectionStage::HostParent) ? 1u : 0u;
    out.queryCost.sessionReadCalls = observation::reached(out.stage, HostInspectionStage::HostSession) ? 1u : 0u;
    out.queryCost.commandLineReadCalls = observation::reached(out.stage, HostInspectionStage::CommandLine) ? 1u : 0u;
    out.queryCost.argumentParseCalls = observation::reached(out.stage, HostInspectionStage::Arguments) ? 1u : 0u;
    out.queryCost.registrationFactCalls = observation::reached(out.stage, HostInspectionStage::Registration) ? 1u : 0u;
    return out;
}
}
