#pragma once
// WMI action ownership != OS keyboard mirror consumption. MSI QS only.
// No platform API, process/service operation, hardware write or action dispatch.
#include <cstdint>
namespace ymcc::msi::qsMirror {
inline uint64_t ruleMask(bool knownMsi,bool armed,bool noDesktop,uint64_t required,bool supportedClick,uint64_t qsBit) noexcept {
    return knownMsi&&armed&&noDesktop&&supportedClick&&required==qsBit?qsBit:0;
}
inline uint64_t suppressionMask(uint64_t mask,bool wmi,uint64_t authorisedMirror,uint64_t frontBits) noexcept {
    return wmi?mask&~(frontBits&~authorisedMirror):mask;
}
inline uint64_t suppressionCanonical(uint64_t actionCanonical,uint64_t rawKeyboard,uint64_t authorisedMirror,
    bool wmi,bool msiProfile,uint32_t physicalKeys,uint32_t shortChord,uint32_t longChord) noexcept {
    // Only the two exact source-declared chords; never turn a composite chord
    // into a QS-only consumption receipt by discarding unrelated held keys.
    if(!wmi||!msiProfile||(physicalKeys!=shortChord&&physicalKeys!=longChord))return actionCanonical;
    return actionCanonical|(rawKeyboard&authorisedMirror);
}
}
