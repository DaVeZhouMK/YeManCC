#pragma once

namespace ymcc::commonFrameLogScope {
inline constexpr char markerField[]="frameScopeSchema";
inline constexpr unsigned version=1;
inline constexpr char transition[]="source-host-admission-outcome;control-neutrality-is-sampled";
inline constexpr char identityBinding[]="selected-read-source-and-slot;device-root-join-via-existing-visibility-events";
inline constexpr char readFreshness[]="read-call-window;not-backend-cached-sample-age";
inline constexpr char msiFreshness[]="validated-dinput-producer-call-window;not-device-internal-packet-age";
}
