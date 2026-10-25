#pragma once
#include "ps_endpoint_ownership.h"
#include <cstring>

// Diagnostic projection only. These fields are never an admission/return input.
// Callers hold the existing Host lock when projecting State/Snapshot/Retirement.
namespace ymcc::inputStartupDiagnostics {
using Json = nlohmann::json;
inline Json crossProcessPsOwner() {
    return {{"scope", "common-ps-startup;msi-and-external-priority-own-separate-duties"},
        {"durableOwnerLoaded", false}, {"previousHostTupleAvailable", false},
        {"previousSdkRootAvailable", false},
        {"source", "no-durable-common-ps-owner-producer-consumer"},
        {"localDebtAbsenceProvesPreviousReturn", false}};
}
inline Json pin(const psEndpointOwnership::Snapshot& value) {
    return {{"capable", value.capable}, {"complete", value.valid},
        {"bindingGeneration", value.generation}, {"reason", value.reason},
        {"sdkRootInstanceId", value.rootInstanceId}};
}
inline Json retirement(const psEndpointOwnership::Retirement& value) {
    if (!value.required) return {{"required", false}, {"owner", nullptr}, {"pin", nullptr}};
    return {{"required", true},
        {"owner", {{"hostInstanceId", value.owner.hostInstanceId}, {"runId", value.owner.runId},
            {"epoch", value.owner.epoch}, {"targetId", value.owner.targetId}, {"persona", value.owner.persona}}},
        {"pin", pin(value.target)}};
}
inline Json startup(Json facts, Json local, const char* snapshotStatus) {
    for (const auto* key : {"desiredPersona", "startupSettingsApplied", "previousInputStartupSession"})
        if (!facts.contains(key)) facts[key] = nullptr;
    facts["localSnapshot"] = snapshotStatus;
    facts["local"] = std::move(local); // null means unavailable, never no-debt.
    facts["crossProcessPsOwner"] = crossProcessPsOwner();
    facts["scope"] = "diagnostic-only;no-recovery-or-device-return-proof";
    return facts;
}
inline Json receiptTuple(const Json& response) {
    Json result = Json::object();
    for (const auto* key : {"command", "requestId", "hostInstanceId", "runId", "targetId", "persona", "profileIdentity"}) {
        const auto found = response.find(key);
        result[key] = found != response.end() && found->is_string() ? *found : Json(nullptr);
    }
    for (const auto* key : {"epoch", "powerGeneration", "sequence"}) {
        const auto found = response.find(key);
        result[key] = found != response.end() && found->is_number_unsigned() ? *found :
            found != response.end() && found->is_number_integer() && *found >= 0 ? *found : Json(nullptr);
    }
    return result;
}
inline void appendAcceptedPrepare(Json& row, const Json& response, const char* command) noexcept {
    try {
        if (!command || std::strcmp(command, "PREPARE_TARGET") != 0) return;
        const auto detail = response.find("detail");
        const bool present = detail != response.end() && detail->is_string() &&
            detail->get_ref<const std::string&>().find("switch-reconcile:") != std::string::npos;
        row["prepareDiagnostic"] = {{"tuple", receiptTuple(response)},
            {"switchReconcileDetailPresent", present}, {"switchReconcileDetailField", "detail"},
            {"reconcileScope", "existing-host-family-observation;not-previous-ps-owner"},
            {"psPinAcceptance", "later-ps-endpoint-ownership-event-with-same-tuple"}};
    } catch (...) { /* Optional evidence must not alter command acceptance. */ }
}
inline Json acceptedPsPrepare(const Json& response, const psEndpointOwnership::Snapshot& acceptedPin) {
    auto result = pin(acceptedPin); // Read the real consumer result, never receipt.complete.
    result["phase"] = "prepare";
    result["tuple"] = receiptTuple(response);
    result["scope"] = "current-process-accepted-pin;not-durable-cross-process-owner";
    return result;
}
} // namespace ymcc::inputStartupDiagnostics
