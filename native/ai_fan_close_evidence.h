#pragma once
#include "json.hpp"
namespace ymcc::ai_fan {
// A null-backend Close is a distinct boundary. No OEM/HC fields can certify it.
inline bool mockCloseReceiptAllowed(const nlohmann::json& response) {
    const auto bit = [](const nlohmann::json& j, const char* key, bool expected) {
        auto it = j.find(key);
        return it != j.end() && it->is_boolean() && it->get<bool>() == expected;
    };
    if (!response.is_object() || !bit(response, "ok", true) || !response.contains("state") || !response["state"].is_object()) return false;
    const auto& s = response["state"];
    return s.contains("hostMode") && s["hostMode"] == "mock-handshake" && s.contains("state") && s["state"] == "Stopped" &&
        s.contains("protocolVersion") && (s["protocolVersion"] == "2" || (s["protocolVersion"].is_number_integer() && s["protocolVersion"] == 2)) &&
        bit(s, "mockZeroHardwareEvidence", true) && bit(s, "mockCloseCompleted", true) && bit(s, "mockControlEnabled", false) &&
        bit(s, "hardwareWritesEnabled", false) && bit(s, "hardwareWritesObserved", false) && bit(s, "openCalled", false) && bit(s, "openEventsCalled", false) &&
        bit(s, "unknownState", false) && (!s.contains("lease") || s["lease"].is_null());
}
}