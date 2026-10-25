#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <json.hpp>
// Diagnostics only: these receipts never authorize a hide, restart, mode write,
// or target admission. Exact textual membership is not a resolved app exemption.
namespace ymcc::msi::visibility {
#if defined(YMCC_MSI_VISIBILITY_DIAGNOSTIC) && defined(YMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC)
inline constexpr bool rawReceiptBuildEnabled=true;
#else
inline constexpr bool rawReceiptBuildEnabled=false;
#endif
inline std::wstring observerRawReceiptSuffix(){return rawReceiptBuildEnabled?L" --raw-msi-receipts":L"";}

inline bool observerEligible(bool build, bool board, bool detailed) { return build && board && detailed; }
inline const char* inverseState(const std::string& raw) {
    const auto begin=raw.find_first_not_of(" \t\r\n\v\f");
    if(begin==std::string::npos)return "unknown";
    const auto token=raw.substr(begin,raw.find_last_not_of(" \t\r\n\v\f")-begin+1);
    if (token == "off" || token == "--inv-off") return "off";
    if (token == "on" || token == "--inv-on") return "on";
    return "unknown";
}
inline nlohmann::json applications(bool known, const std::vector<std::string>& paths,
                                    const std::string& inverse) {
    return {{"readbackKnown", known}, {"paths", known ? nlohmann::json(paths) : nlohmann::json(nullptr)},
        {"inverseRaw", inverse}, {"inverseState", known ? inverseState(inverse) : "unknown"},
        {"resolvedSteamExemption", "unknown"}, {"resolvedObserverExemption", "unknown"},
        {"externalSuppression", "unverified"},
        {"meaning", "configuration snapshot only; correlate independent observer exact executable and path mapping"}};
}
inline nlohmann::json controllerState(const long* axes, const unsigned long* povs,
                                      const unsigned char* buttons) {
    nlohmann::json down=nlohmann::json::array();
    for(unsigned i=0;i<128;++i)if(buttons[i]&0x80)down.push_back(i);
    return {{"axisValues", std::vector<long>(axes,axes+8)},
        {"povValues",std::vector<unsigned long>(povs,povs+4)}, {"buttonsDown",std::move(down)},
        {"meaning","game-controller state values only; correlate a held button/axis action; neutral/cached alone is inconclusive"}};
}
}
