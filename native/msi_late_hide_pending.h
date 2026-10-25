#pragma once
namespace ymcc::msi::latehide {
// 2026-10-21 G3E 22:44 field: an empty leftover job executed after a real
// completed episode, replaced its completion key and admitted the same source
// again. Required scope remains registered, armed MSI managed input only.
// The standard build must enforce this owner check; it is not a diagnostic flag.
inline constexpr bool buildEnabled=true;
inline bool required(bool enabled,bool registeredManaged){return enabled&&registeredManaged;}
inline const char* decision(bool required,bool hasPending,const char* legacy){
    return required&&!hasPending?"refused-msi-missing-pending":legacy;
}
inline bool mayComplete(bool required,bool hasPending){return !required||hasPending;}
}
