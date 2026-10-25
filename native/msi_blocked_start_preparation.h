#pragma once
namespace ymcc::msi::preparation {
#ifdef YMCC_MSI_BLOCKED_START_PREPARATION_FIX
inline constexpr bool buildEnabled=true;
#else
inline constexpr bool buildEnabled=false;
#endif
inline bool allowed(bool enabled,bool registeredBoard,bool quarantined){return !(enabled&&registeredBoard&&quarantined);}
}
