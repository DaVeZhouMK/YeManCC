#pragma once
// Per-request dispatch result only; no ownership or action authority.
namespace ymcc::steamcommand {
enum class Disposition : unsigned char { Unavailable, Busy, Revoked, Failed, Submitted };
struct DispatchResult {
    Disposition disposition=Disposition::Unavailable;
    constexpr bool submitted()const noexcept{return disposition==Disposition::Submitted;}
};
} // namespace ymcc::steamcommand
