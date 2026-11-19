#pragma once
// Shared scalar/native-receipt validation for the one running-build recovery
// path. No installer-event replay model or alternative grant API is shipped.
#include "json.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <initializer_list>
namespace ymcc::deckyupgrade {
using Json=nlohmann::json;
namespace detail {
inline bool text(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (unsigned char c : value) if (c < 32 || c == 127) return false;
    return true;
}
inline bool hash(const std::string& value) {
    return value.size() == 64 &&
        value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
inline bool keys(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size()) return false;
    for (const char* name : names) if (!value.contains(name)) return false;
    return true;
}
inline bool decimal(const std::string& value, std::uint64_t maximum) {
    if (value.empty() || value.front() == '0') return false;
    std::uint64_t result = 0;
    for (unsigned char c : value) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > (maximum - digit) / 10) return false;
        result = result * 10 + digit;
    }
    return result != 0;
}
inline bool ticket(const std::string& value) {
    const auto colon = value.find(':');
    return value.size() <= 31 && colon != std::string::npos &&
        value.find(':', colon + 1) == std::string::npos &&
        decimal(value.substr(0, colon), (std::numeric_limits<std::uint32_t>::max)()) &&
        decimal(value.substr(colon + 1), (std::numeric_limits<std::uint64_t>::max)());
}
using Version = std::array<std::uint32_t, 3>;
inline bool version(const std::string& value, Version& parsed, bool loader = false) {
    if (value.empty() || value.size() > 32) return false;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        const auto end = i == 2 ? value.size() : value.find('.', begin);
        if (end == std::string::npos || end == begin || end - begin > 10 ||
            (end - begin > 1 && value[begin] == '0')) return false;
        std::uint64_t part = 0;
        for (std::size_t j = begin; j < end; ++j) {
            const auto c = value[j];
            if (c < '0' || c > '9') return false;
            part = part * 10 + static_cast<unsigned>(c - '0');
            if (part > 2147483647ULL) return false;
        }
        parsed[i] = static_cast<std::uint32_t>(part);
        begin = end + 1;
    }
    return !loader || parsed[0] != 0;
}
inline bool loader(const std::string& value) {
    Version parsed{};
    return value.size() >= 6 && value.front() == 'v' &&
        version(value.substr(1), parsed, true);
}
// Validate archived native receipts without changing the original v1/v2 shape.
// Only the exact migration sentinel can be archived as an unknown predecessor
// behind the separately verified running-build grant. This is NOT ordinary
// cleanup availability: the protected build store still consumes each page/BPM
// slot once before settings dispatch. Null/absent/malformed remain refused.
inline bool unknownLedger(const Json& value) {
    return keys(value,{"schemaVersion","history"})&&value.at("schemaVersion").is_number_integer()&&
        value.at("schemaVersion")==1&&value.at("history")=="unconfirmed";
}
inline bool legacyLedger(const Json& value, const std::string& steam) {
    return keys(value, {"schemaVersion", "steamSession"}) &&
        value.at("schemaVersion").is_number_integer() && value.at("schemaVersion") == 1 &&
        value.at("steamSession").is_string() && value.at("steamSession") == steam &&
        ticket(steam);
}
inline bool managedLedger(const Json& value, const std::string& steam) {
    return keys(value, {"schemaVersion", "steamSession", "host", "document",
                         "frontendBuildId", "loaderVersion", "status"}) &&
        value.at("schemaVersion").is_number_integer() && value.at("schemaVersion") == 2 &&
        value.at("steamSession").is_string() && value.at("steamSession") == steam &&
        ticket(steam) && value.at("host").is_string() &&
        ticket(value.at("host").get<std::string>()) &&
        value.at("document").is_string() &&
        text(value.at("document").get<std::string>(), 75) &&
        value.at("document").get_ref<const std::string&>().find(':') == std::string::npos &&
        value.at("frontendBuildId").is_string() &&
        hash(value.at("frontendBuildId").get<std::string>()) &&
        value.at("loaderVersion").is_string() &&
        loader(value.at("loaderVersion").get<std::string>()) &&
        (value.at("status") == "reserved" || value.at("status") == "ready");
}
inline bool ledgers(const Json& prior, const Json& reserved,
                    bool reopen, const std::string& steam) {
    if (reopen) return (legacyLedger(prior, steam) || unknownLedger(prior)) && legacyLedger(reserved, steam);
    return (legacyLedger(prior, steam) || managedLedger(prior, steam) || unknownLedger(prior)) &&
        managedLedger(reserved, steam) && reserved.at("status") == "reserved";
}
inline bool reservation(const Json& value, bool reopen, const std::string& steam) {
    return keys(value, {"steamTicket", "priorLedger", "reservedLedger"}) &&
        value.at("steamTicket").is_string() && value.at("steamTicket") == steam &&
        ledgers(value.at("priorLedger"), value.at("reservedLedger"), reopen, steam);
}

}
}
