#pragma once
#include <cmath>
#include "../deps/json/json.hpp"
namespace ymcc::fan_replay {
// The current Host's replay curve contract; not an extra hardware-write gate.
inline bool curveValid(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 4) return false;
    double previousTemperature = 0, previousDuty = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto& node = value[i];
        if (!node.is_object()) return false;
        const auto temperature = node.find("tempC"), duty = node.find("dutyPercent");
        if (temperature == node.end() || duty == node.end() || !temperature->is_number() || !duty->is_number()) return false;
        const auto t = temperature->get<double>(), d = duty->get<double>();
        if (!std::isfinite(t) || !std::isfinite(d) || t < 0 || t > 100 || d < 0 || d > 100 ||
            (i == 0 && t != 0) || t < previousTemperature || d < previousDuty) return false;
        previousTemperature = t; previousDuty = d;
    }
    return previousDuty >= 50;
}
inline const nlohmann::json* nodesForCommittedControl(const nlohmann::json& request, const nlohmann::json& reply) {
    // Retain the original explicit-node behavior after Host accepted the write.
    if (request.is_object()) {
        const auto nodes = request.find("nodes");
        if (nodes != request.end() && nodes->is_array()) return &*nodes;
    }
    if (!reply.is_object()) return nullptr;
    const auto state = reply.find("state");
    if (state == reply.end() || !state->is_object()) return nullptr;
    const auto curve = state->find("activeCurve");
    return curve != state->end() && curveValid(*curve) ? &*curve : nullptr;
}
}
