#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Diagnostic-only: consume ALREADY captured configuration and proven selected
// paths. No driver access, ownership claim, startup recovery, policy/gate or
// external-reader proof. An inherited entry is NOT evidence that YMCC owns it.
namespace ymcc::msi::focus_evidence_20261012_fa1322 {
enum class Knowledge { Unknown, No, Yes };
enum class SelectedConfiguration { Unknown, NoFilterForSelectedPaths, HiddenFromUnlistedReaders };
struct Facts {
    bool snapshotRead = false;
    bool selectedPathsComplete = false;
    bool cloakKnown = false, cloakOn = false;
    bool inverseKnown = false, inverseOn = false;
    bool readerRegistrationKnown = false, ymccReaderExempt = false;
    std::vector<std::wstring> selectedPaths, hiddenPaths, ownedPaths;
};
struct Evidence {
    Knowledge ownedEntriesReturned = Knowledge::Unknown;
    Knowledge ymccExemptReaderCanBypassConfiguration = Knowledge::Unknown;
    SelectedConfiguration selectedConfiguration = SelectedConfiguration::Unknown;
    unsigned selectedHiddenCount = 0, ownedStillHiddenCount = 0;
    unsigned unownedSelectedHiddenCount = 0;
    // Always false: a config/cycle receipt never proves Steam button delivery.
    bool externalInputDeliveryProven = false;
    // Always false: this decoder never authorizes unhide/claim of foreign paths.
    bool inheritedEntryOwnershipProven = false;
};
inline std::wstring key(std::wstring path) {
    // Windows instance IDs are ASCII case-insensitive; do not conflate paths,
    // trim suffixes, match names/VID substrings, or infer PnP ancestry here.
    for (auto& c : path) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    return path;
}
inline std::vector<std::wstring> uniqueKeys(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> result;
    for (const auto& path : paths) if (!path.empty()) result.push_back(key(path));
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
inline bool has(const std::vector<std::wstring>& keys, const std::wstring& path) {
    return std::binary_search(keys.begin(), keys.end(), path);
}
inline Evidence decode(const Facts& f) {
    Evidence e;
    if (!f.snapshotRead) return e;
    const auto hidden = uniqueKeys(f.hiddenPaths), owned = uniqueKeys(f.ownedPaths);
    for (const auto& path : owned) if (has(hidden, path)) ++e.ownedStillHiddenCount;
    e.ownedEntriesReturned = e.ownedStillHiddenCount ? Knowledge::No : Knowledge::Yes;
    const auto selected = uniqueKeys(f.selectedPaths);
    for (const auto& path : selected) if (has(hidden, path)) {
        ++e.selectedHiddenCount;
        if (!has(owned, path)) ++e.unownedSelectedHiddenCount;
    }
    if (!f.selectedPathsComplete || selected.empty() || !f.cloakKnown) return e;
    // An off cloak bypasses filtering regardless of the list/inverse state.
    if (!f.cloakOn || !e.selectedHiddenCount)
        e.selectedConfiguration = SelectedConfiguration::NoFilterForSelectedPaths;
    else if (f.inverseKnown && !f.inverseOn) {
        e.selectedConfiguration = SelectedConfiguration::HiddenFromUnlistedReaders;
        if (f.readerRegistrationKnown)
            e.ymccExemptReaderCanBypassConfiguration = f.ymccReaderExempt ? Knowledge::Yes : Knowledge::No;
    }
    // inverse-on/unknown is not normal allowlist semantics: leave Unknown.
    return e;
}
} // namespace ymcc::msi::focus_evidence_20261012_fa1322
