#pragma once
// Pure projection of caller-owned session/mode observations. No producer,
// retained cache, IO, callback, freshness lease or action permission lives here.
#include "steam_session_observer.h"
#include "steam_ui_epoch.h"
#include "json.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace ymcc::steamaccount {
using Json = nlohmann::json;
enum class Truth { Unknown, False, True };

inline DWORD positiveAccount32(const Json& value) {
    if (!value.is_number_integer()) return 0;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        return number > 0 && number <= MAXDWORD ? static_cast<DWORD>(number) : 0;
    }
    const auto number = value.get<std::int64_t>();
    return number > 0 && number <= MAXDWORD ? static_cast<DWORD>(number) : 0;
}

struct RegistryCandidate {
    DWORD accountID32 = 0; // Candidate only, including legacy Session fallback.
    // Session alone carries no read receipt; zero is not a logout observation.
    bool readStatusAvailable = false, registryReadKnown = false;
    LSTATUS readError = ERROR_NOT_READY;
    DWORD registeredPid = 0;
    bool registeredPidReadKnown = false, rootReadKnown = false;
    LSTATUS registeredPidReadError = ERROR_NOT_READY, rootReadError = ERROR_NOT_READY;
    Truth pidMatchesSession = Truth::Unknown;
    Truth processAssociation = Truth::Unknown;
    // The associated root/PID/birth are Facts::session. This correlation says
    // nothing about a CM login or the account in a particular UI document.
};

struct UiAccount {
    bool object = false;
    Truth known = Truth::Unknown, running = Truth::Unknown;
    DWORD pid = 0, accountID32 = 0;
    std::string birth;
    bool samePidAndBirth = false;
    Truth deckyAccountWatchAvailable = Truth::Unknown;
    bool modeEpochKnown = false, documentKnown = false;
    std::string modeEpoch, document;
    std::uint64_t documentEntry = 0;
    // Current ui.mode.get and its native cache have no CM SteamID64 field.
    // Always Unknown in this adapter. Do not derive it from accountID32 or
    // accept SteamID/GameID/AppID/overlay aliases. A future adapter needs an
    // actual CM field and a checked association with the observed account32.
    std::optional<std::uint64_t> cmSteamID64;

    bool currentAccountKnown() const noexcept {
        return samePidAndBirth && known == Truth::True && running == Truth::True && accountID32 != 0;
    }
};

struct Facts {
    steamsession::Session session;
    RegistryCandidate registryCandidate;
    UiAccount ui;
    // A comparison of the supplied candidates, never a global admission gate.
    // Unknown unless a positive registry candidate and current UI account exist.
    Truth candidateUiConflict = Truth::Unknown;
    bool identitySnapshotAvailable = false;
    int presence = -1;
    std::uint64_t identityRevision = 0, processEpoch = 0, accountEpoch = 0;
    ULONGLONG observedAt = 0;
};

inline bool hasSessionIdentity(const steamsession::Session& session) noexcept {
    return session.pid && session.created && !session.root.empty();
}

namespace detail {
inline const Json* member(const Json& object, const char* name) {
    if (!object.is_object()) return nullptr;
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &*found;
}
inline Truth flag(const Json& object, const char* name) {
    const auto value = member(object, name);
    return !value || !value->is_boolean() ? Truth::Unknown : value->get<bool>() ? Truth::True : Truth::False;
}
inline UiAccount uiAccount(const steamsession::Session& session, const Json& mode) {
    UiAccount result;
    if (!mode.is_object()) return result;
    try {
        result.object = true;
        result.known = flag(mode, "known");
        result.running = flag(mode, "running");
        result.deckyAccountWatchAvailable = flag(mode, "deckyAccountWatchAvailable");
        if (const auto value = member(mode, "pid")) result.pid = positiveAccount32(*value);
        if (const auto value = member(mode, "observedAccount")) result.accountID32 = positiveAccount32(*value);
        if (const auto value = member(mode, "birth"); value && value->is_string())
            result.birth = value->get<std::string>();
        result.samePidAndBirth = hasSessionIdentity(session) && result.pid == session.pid &&
            result.birth == std::to_string(session.created);
        // Reuse the existing document-id:entry parser. Epoch absence/malformation
        // makes document evidence Unknown; legacy Decky resolve did not gate on it.
        if (const auto value = member(mode, "modeEpoch"); value && value->is_string()) {
            const auto& text = value->get_ref<const std::string&>();
            if (text.size() <= 96) {
                result.modeEpochKnown = true;
                result.modeEpoch = text;
                const auto parsed = steamui::parseModeEpoch(text);
                if (parsed.valid) {
                    result.documentKnown = true;
                    result.document.assign(parsed.document);
                    result.documentEntry = parsed.entry;
                }
            }
        }
    } catch (...) { return {}; }
    return result;
}
} // namespace detail

inline Facts project(const steamsession::Session& session, const Json& cachedMode) {
    Facts facts;
    facts.session = session;
    facts.registryCandidate.accountID32 = session.account;
    facts.ui = detail::uiAccount(session, cachedMode);
    if (session.account && facts.ui.currentAccountKnown())
        facts.candidateUiConflict = session.account == facts.ui.accountID32 ? Truth::False : Truth::True;
    return facts;
}

inline Facts project(const steamsession::IdentitySnapshot& identity, const Json& cachedMode) {
    auto facts = project(identity.session, cachedMode);
    facts.identitySnapshotAvailable = true;
    facts.presence = identity.presence;
    facts.identityRevision = identity.revision;
    facts.processEpoch = identity.processEpoch;
    facts.accountEpoch = identity.accountEpoch;
    facts.observedAt = identity.observedAt;
    auto& candidate = facts.registryCandidate;
    candidate.readStatusAvailable = true;
    candidate.readError = identity.accountReadError;
    candidate.registryReadKnown = identity.accountReadError == ERROR_SUCCESS;
    candidate.registeredPid = identity.registeredPid;
    candidate.registeredPidReadError = identity.registeredPidReadError;
    candidate.registeredPidReadKnown = identity.registeredPidReadError == ERROR_SUCCESS;
    candidate.rootReadError = identity.rootReadError;
    candidate.rootReadKnown = identity.rootReadError == ERROR_SUCCESS;
    if (candidate.registeredPidReadKnown && identity.session.pid)
        candidate.pidMatchesSession = identity.registeredPid == identity.session.pid ? Truth::True : Truth::False;
    if (candidate.registeredPidReadKnown && candidate.rootReadKnown && identity.presence >= 0)
        candidate.processAssociation = identity.registeredProcessMatches() ? Truth::True : Truth::False;
    return facts;
}

// Explicit consumer policy, separate from the evidence projection. It preserves
// Decky's existing local UI preference; it is not permission for other owners.
enum class Policy { UiLocal };
struct Selection { DWORD value = 0; bool fromUi = false; };
inline Selection select(const Facts& facts, Policy policy) {
    if (policy != Policy::UiLocal || !hasSessionIdentity(facts.session)) return {};
    const auto candidate = facts.registryCandidate.accountID32;
    if (facts.ui.currentAccountKnown() && facts.ui.deckyAccountWatchAvailable == Truth::True)
        return {facts.ui.accountID32, facts.ui.accountID32 != candidate};
    // Preserve the old registry candidate even when richer read status is
    // Unknown/failed. This legacy local fallback does not upgrade its evidence.
    return {candidate, false};
}
} // namespace ymcc::steamaccount
