#pragma once
#include "json.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

// Evidence from the already-subscribed Kernel-Power 507, after standby exit.
// This is OS accounting, not a live hardware sensor or an automatic repair gate.
namespace ymcc::sleep_residency {
using json = nlohmann::json;
struct EventData {
    std::map<std::string, std::string> values;
    std::set<std::string> duplicates;
};
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline std::string_view trim(std::string_view v) {
    while (!v.empty() && space(v.front())) v.remove_prefix(1);
    while (!v.empty() && space(v.back())) v.remove_suffix(1);
    return v;
}
// EvtRenderEventXml emits unprefixed EventData/Data and a Name attribute.
// Keep the reader bounded to EventData; malformed/duplicate values fail closed.
inline EventData readEventData(const std::string& xml) {
    EventData result;
    const auto section = xml.find("<EventData");
    if (section == std::string::npos) return result;
    const auto begin = xml.find('>', section);
    const auto end = xml.find("</EventData>", begin);
    if (begin == std::string::npos || end == std::string::npos) return result;
    std::size_t pos = begin + 1;
    while ((pos = xml.find("<Data", pos)) != std::string::npos && pos < end) {
        const auto tagEnd = xml.find('>', pos + 5);
        const auto valueEnd = tagEnd == std::string::npos ? std::string::npos : xml.find("</Data>", tagEnd + 1);
        if (tagEnd == std::string::npos || valueEnd == std::string::npos || valueEnd >= end) break;
        const auto namePos = xml.find("Name", pos + 5);
        if (space(xml[pos + 5]) && namePos != std::string::npos && namePos < tagEnd && space(xml[namePos - 1])) {
            std::size_t cursor = namePos + 4;
            while (cursor < tagEnd && space(xml[cursor])) ++cursor;
            if (cursor < tagEnd && xml[cursor++] == '=') {
                while (cursor < tagEnd && space(xml[cursor])) ++cursor;
                if (cursor < tagEnd && (xml[cursor] == '\'' || xml[cursor] == '"')) {
                    const char quote = xml[cursor++];
                    const auto nameEnd = xml.find(quote, cursor);
                    if (nameEnd != std::string::npos && nameEnd < tagEnd) {
                        const auto name = xml.substr(cursor, nameEnd - cursor);
                        if (!result.values.emplace(name, xml.substr(tagEnd + 1, valueEnd - tagEnd - 1)).second)
                            result.duplicates.insert(name);
                    }
                }
            }
        }
        pos = valueEnd + 7;
    }
    return result;
}
inline std::optional<std::uint64_t> unsignedValue(const std::string& text) {
    const auto v = trim(text);
    if (v.empty() || v.front() < '0' || v.front() > '9') return std::nullopt;
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(v.data(), v.data() + v.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != v.data() + v.size() || value == (std::numeric_limits<std::uint64_t>::max)())
        return std::nullopt;
    return value;
}
inline json makeEvidence(const std::string& xml) {
    const auto data = readEventData(xml);
    json raw = json::object(), values = json::object(), invalid = json::array();
    constexpr std::array fields{
        "DurationInUs", "DripsResidencyInUs", "HwDripsResidencyInUs", "DripsTransitions", "FirstDripsEntryInUs",
        "ActiveResidencyInUs", "NonDripsTimeActivatedInUs", "NonActivatedCpuInUs", "NonAttributedCpuInUs",
        "ScenarioInstanceId", "ScenarioInstanceIdV2", "BootId",
        "EnergyDrain", "NonResiliencyTimeInUs", "ResiliencyDripsTimeInUs", "ResiliencyHwDripsTimeInUs",
        "ScreenOffDurationInUs", "SleepDurationInUs", "ScreenOffSwDripsResidencyInUs", "ScreenOffHwDripsResidencyInUs",
        "SleepSwDripsResidencyInUs", "SleepHwDripsResidencyInUs", "GdiOnTime", "DwmSyncFlushTime", "MonitorPowerOnTime"
    };
    for (const auto field : fields) {
        const auto it = data.values.find(field);
        if (it == data.values.end()) continue; // Missing must never become zero.
        raw[field] = it->second;
        const auto parsed = unsignedValue(it->second);
        if (!parsed || data.duplicates.count(field)) invalid.push_back(field);
        else values[field] = *parsed;
    }
    std::optional<bool> entered;
    for (const auto field : {"SleepEntered", "IsCsSessionInProgressOnExit", "PowerStateAc"}) {
        const auto it = data.values.find(field);
        if (it == data.values.end()) continue;
        raw[field] = it->second;
        const auto text = trim(it->second);
        std::optional<bool> flag;
        if (!data.duplicates.count(field)) {
            if (text == "true" || text == "1") flag = true;
            else if (text == "false" || text == "0") flag = false;
        }
        if (flag.has_value()) values[field] = *flag;
        else invalid.push_back(field);
        if (std::string_view(field) == "SleepEntered") entered = flag;
    }
    const auto number = [&](const char* key) -> std::optional<std::uint64_t> {
        if (!values.contains(key)) return std::nullopt;
        return values[key].get<std::uint64_t>();
    };
    const auto duration = number("DurationInUs"), sw = number("DripsResidencyInUs"), hw = number("HwDripsResidencyInUs");
    const auto transitions = number("DripsTransitions");
    constexpr std::uint64_t minimumObservationUs = 120000000ULL;
    json result = {
        {"schemaVersion", 1}, {"source", "Microsoft-Windows-Kernel-Power/507"},
        {"timing", "after-standby-exit"}, {"units", "residency-and-duration-InUs-fields:microseconds;other-fields:Windows-schema"},
        {"raw", std::move(raw)}, {"values", std::move(values)}, {"invalidFields", std::move(invalid)},
        {"windowsSleepStage", entered.has_value() ? (*entered ? "entered" : "not-entered") : "unavailable"},
        {"minimumObservationUs", minimumObservationUs}, {"qualityHintThresholdPercent", 80},
        {"policy", "diagnostic-only;120s-is-not-a-Windows-entry-deadline"},
        {"hardwareZeroIsConclusive", false}, {"automaticRepairAuthorized", false}
    };
    const bool validDuration = duration && *duration != 0;
    const bool consistent = validDuration && (!sw || *sw <= *duration) && (!hw || *hw <= *duration);
    if (consistent && sw) result["softwareDripsPercent"] = 100.0 * static_cast<double>(*sw) / static_cast<double>(*duration);
    if (consistent && hw) result["hardwareDripsPercent"] = 100.0 * static_cast<double>(*hw) / static_cast<double>(*duration);
    std::string assessment = "insufficient-evidence";
    if (validDuration && !consistent) assessment = "inconsistent-counters";
    else if (validDuration && *duration < minimumObservationUs) assessment = "short-session";
    else if (consistent && sw) {
        if (*sw == 0 && ((transitions && *transitions > 0) || (hw && *hw > 0))) assessment = "inconsistent-counters";
        else if (*sw == 0) assessment = "no-software-drips-recorded";
        else if (hw && *hw == 0) assessment = "hardware-zero-validity-unverified";
        else if (hw) assessment = static_cast<double>(*hw) / static_cast<double>(*duration) < 0.8
            ? "low-recorded-hardware-residency" : "hardware-residency-recorded";
        else assessment = "software-residency-only";
    }
    result["assessment"] = assessment;
    return result;
}
} // namespace ymcc::sleep_residency
