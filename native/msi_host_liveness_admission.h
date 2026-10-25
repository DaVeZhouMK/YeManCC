#pragma once
// MSI-only tri-state admission guard. Never converts an unknown sample to a
// positive alive/dead verdict; it only withholds one speculative start request.
namespace ymcc::msi::liveness {
inline bool admissionAllowed(bool enabled,bool knownBoard,int liveness) {
    return !enabled || !knownBoard || liveness >= 0;
}
}
