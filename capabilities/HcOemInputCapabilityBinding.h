// AI-NAV[oem-input]: Follow native/main.cpp oemUnifiedInputBindingFor; AI-INDEX.json separates OEM and external-controller routes.
// Bind the existing model/profile/catalog; key-up, hook disable and raw-source release stay with the native owner.
// A chassis model binding does not grant vendor extras to an external physical controller.
#pragma once
#include "hc-device-catalog.generated.h"
#include <string>
#include <utility>

namespace ymcc::capabilities::oem {
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
