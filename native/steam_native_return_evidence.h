#pragma once
#include "json.hpp"
#include <cstdint>

namespace ymcc::steamreturnevidence {
using Json = nlohmann::json;

// Fixed steam_live.js contract, diagnostic evidence only. Rebuild the public
// fields rather than forwarding any part of an untrusted nativeReturn object.
inline Json bounded(const Json& reply) {
    try {
        if (!reply.is_object()) return nullptr;
        const auto found = reply.find("nativeReturn");
        if (found == reply.end() || !found->is_object()) return nullptr;
        const auto& value = *found;
        const auto& source = value.at("source");
        const auto& kind = value.at("kind");
        const auto& observed = value.at("observed");
        const auto& code = value.at("code");
        const auto& booleanValue = value.at("booleanValue");
        const auto& authority = value.at("authority");
        const auto strictFalse = [](const Json& field) {
            return field.is_boolean() && !field.get<bool>();
        };
        if (!source.is_string() || source != "SteamClient.Apps.RaiseWindowForGame" ||
            !kind.is_string() || !observed.is_boolean() ||
            !authority.is_string() || authority != "diagnostic-only-no-admission" ||
            !strictFalse(value.at("nativeMeaningVerified")) ||
            !strictFalse(value.at("gameInputVerified")) ||
            !strictFalse(value.at("continueVerified"))) return nullptr;

        const char* outputKind = nullptr;
        Json outputCode = nullptr, outputBoolean = nullptr;
        if (kind == "uint32") {
            if (!observed.get<bool>() || !booleanValue.is_null()) return nullptr;
            std::uint64_t number = 0;
            if (code.is_number_unsigned()) number = code.get<std::uint64_t>();
            else if (code.is_number_integer()) {
                const auto signedNumber = code.get<std::int64_t>();
                if (signedNumber < 0) return nullptr;
                number = static_cast<std::uint64_t>(signedNumber);
            } else return nullptr; // Integral-looking floats and bools also fail.
            if (number > 0xffffffffULL) return nullptr;
            outputKind = "uint32";
            outputCode = number;
        } else if (kind == "boolean") {
            if (!observed.get<bool>() || !code.is_null() || !booleanValue.is_boolean()) return nullptr;
            outputKind = "boolean";
            outputBoolean = booleanValue.get<bool>();
        } else if (kind == "undefined" || kind == "unsupported") {
            if (observed.get<bool>() || !code.is_null() || !booleanValue.is_null()) return nullptr;
            outputKind = kind == "undefined" ? "undefined" : "unsupported";
        } else return nullptr;

        return {{"source", "SteamClient.Apps.RaiseWindowForGame"}, {"kind", outputKind},
            {"observed", observed.get<bool>()}, {"code", outputCode}, {"booleanValue", outputBoolean},
            {"authority", "diagnostic-only-no-admission"}, {"nativeMeaningVerified", false},
            {"gameInputVerified", false}, {"continueVerified", false}};
    } catch (...) {
        return nullptr;
    }
}
} // namespace ymcc::steamreturnevidence