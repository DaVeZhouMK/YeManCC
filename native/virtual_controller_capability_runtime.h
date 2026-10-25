#pragma once
#include "virtual_controller_runtime_contract.h"

// VC-LIFECYCLE-20261020-R3 — production definition module.
// Caller-side target admission. Machine identity and existing capability catalogs remain authoritative; optional source capabilities do not reject an entire virtual target.
// Shared enable/UI-output timeline: virtual_controller_runtime_contract.h.
// Navigation and capability boundaries: virtual_controller_runtime.md.
// This is compiled production code; the reference comments do not execute.

static bool inputHostSteamDeckSelfOutputBlocked() noexcept {
    return g_machineIdentity.resolved && g_machineIdentity.family == YmccFamily::ValveSteamDeck;
}
static const char* inputHostMachineTargetRejectionReason(const std::string& persona) noexcept {
    return ymcc::input::virtualTargetPolicy::rejectionReason(inputHostSteamDeckSelfOutputBlocked(), persona);
}
// Bound output classification reuses the existing MSI authority and persona catalog.
static bool msiUiBoundTargetAdmitted(const std::string& boundPersona) {
    return msiClawKnownBoardAuthority() &&
        ymcc::controllerPersona::currentOutput(boundPersona);
}
static const char* virtualUiBoundPersona(const std::string& boundPersona) {
    return ymcc::controllerPersona::currentOutput(boundPersona) ? ymcc::controllerPersona::runtime(boundPersona) : "";
}
