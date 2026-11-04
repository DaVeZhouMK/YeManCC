// AI-NAV[oem-input]: Follow native/main.cpp oemUnifiedInputBindingFor; AI-INDEX.json separates OEM and external-controller routes.
// Bind the existing model/profile/catalog; key-up, hook disable and raw-source release stay with the native owner.
// A chassis model binding does not grant vendor extras to an external physical controller.
#pragma once
#include "hc-device-catalog.generated.h"
#include "HcOneXVendorInputContract.h"
#include <string>
#include <string_view>
#include <utility>

namespace ymcc::capabilities::oem {
// Exact ONEX identity input to the existing shared OEM/motion/lighting consumers.
// Fixed HC f2e58468 IDevice factory: two manufacturers, 21 product labels.
// Source SHA256 f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47.
// Inputs already use the native DMI trim/uppercase normalization. No substring,
// marketing-name, CPU, or systemProduct fallback grants a device class.
inline constexpr bool oneXManufacturerMatches(std::string_view manufacturer) noexcept {
    return manufacturer == "ONE-NETBOOK" || manufacturer == "ONE-NETBOOK TECHNOLOGY CO., LTD.";
}
inline constexpr std::string_view resolveOneXDeviceClass(std::string_view manufacturer,
        std::string_view product, std::string_view version) noexcept {
    if (!oneXManufacturerMatches(manufacturer)) return {};
    if (product == "ONE XPLAYER" || product == "ONEXPLAYER MINI PRO") {
        if (version == "1002-C") return "HandheldCompanion.Devices.OneXPlayerMiniIntel";
        if (version == "V03") return "HandheldCompanion.Devices.OneXPlayerMiniPro";
        return "HandheldCompanion.Devices.OneXPlayerMiniAMD";
    }
    struct Rule { std::string_view product, deviceClass; };
    constexpr Rule rules[] = {
        {"ONEXPLAYER X1 I", "HandheldCompanion.Devices.OneXPlayerX1Intel"},
        {"ONEXPLAYER X1 A", "HandheldCompanion.Devices.OneXPlayerX1AMD"},
        {"ONEXPLAYER X1Z", "HandheldCompanion.Devices.OneXPlayerX1AMD"},
        {"ONEXPLAYER X1 MINI", "HandheldCompanion.Devices.OneXPlayerX1Mini"},
        {"ONEXPLAYER X1PRO", "HandheldCompanion.Devices.OneXPlayerX1Pro"},
        {"ONEXPLAYER APEX", "HandheldCompanion.Devices.OneXPlayerApex"},
        {"ONEXPLAYERAPEX", "HandheldCompanion.Devices.OneXPlayerApex"},
        {"ONEXPLAYER X2", "HandheldCompanion.Devices.OneXPlayerX2"},
        {"ONEXPLAYER X2MINI PRO", "HandheldCompanion.Devices.OneXPlayerX2MiniPro"},
        {"ONEXPLAYER G1 I", "HandheldCompanion.Devices.OneXPlayerG1Intel"},
        {"ONEXPLAYER G1 A", "HandheldCompanion.Devices.OneXPlayerG1AMD"},
        {"ONEXPLAYER F1", "HandheldCompanion.Devices.OneXPlayerOneXFly"},
        {"ONEXPLAYER F1PRO", "HandheldCompanion.Devices.OneXPlayerOneXFlyF1Pro"},
        {"ONEXPLAYER MINI A07", "HandheldCompanion.Devices.OneXPlayerMiniAMD"},
        {"ONEXPLAYER 2 ARP23", "HandheldCompanion.Devices.OneXPlayer2"},
        {"ONEXPLAYER 2 PRO ARP23P", "HandheldCompanion.Devices.OneXPlayer2Pro"},
        {"ONEXPLAYER 2 PRO ARP23P EVA-01", "HandheldCompanion.Devices.OneXPlayer2Pro"},
        {"ONEXPLAYER 3", "HandheldCompanion.Devices.OneXPlayer3"},
        {"ONEXPLAYER-3", "HandheldCompanion.Devices.OneXPlayer3"},
    };
    for (const auto& rule : rules) if (product == rule.product) return rule.deviceClass;
    return {};
}

// Only immutable model/profile/catalog selection is retained here. Live hook,
// ownership, interface, generation, key state and release guards stay with the
// existing input lifecycle owner and must be evaluated for every consumption.
template<class Catalog>
struct InputBinding {
    const Device* model = nullptr;
    int profileId = 0;
    Catalog catalog{};
};

// This cache belongs to the existing serialized identity/configuration owner.
// It introduces no process-wide state, hardware operation or authorization.
// Selectors must return the existing immutable profile id and key-table view;
// they must not retain a reference to the temporary canonical identity string.
template<class Family, class Catalog>
class InputBindingCache {
public:
    template<class ProfileSelector, class CatalogSelector>
    const InputBinding<Catalog>& bind(Family family, const std::string& deviceClass,
                                     ProfileSelector&& selectProfile,
                                     CatalogSelector&& selectCatalog) {
        if (initialized_ && family_ == family && deviceClass_ == deviceClass)
            return binding_;
        std::string nextClass(deviceClass);
        const auto* model = ymcc::capabilities::find(deviceClass);
        InputBinding<Catalog> next;
        next.model = model;
        const auto consume = [&](const std::string& canonical) {
            next.profileId = static_cast<int>(selectProfile(family, canonical));
            next.catalog = selectCatalog(family, canonical);
        };
        // Preserve the original resolver and its explicitly reviewed family or
        // extension fallback. A catalog declaration alone admits no new writer.
        if (model && model->consumerClass != deviceClass)
            consume(std::string(model->consumerClass));
        else
            consume(deviceClass);
        deviceClass_.swap(nextClass);
        family_ = family;
        binding_ = next;
        initialized_ = true;
        return binding_;
    }

    void resetIdentity() noexcept {
        initialized_ = false;
        deviceClass_.clear();
        binding_ = {};
    }

private:
    bool initialized_ = false;
    Family family_{};
    std::string deviceClass_;
    InputBinding<Catalog> binding_{};
};
} // namespace ymcc::capabilities::oem
