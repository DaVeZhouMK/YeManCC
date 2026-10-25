#pragma once
#include <array>
#include <cstdint>

namespace ymcc::sleeprequest {
enum class Mode : unsigned char { None, ModernStandby, Sleep, Hibernate };
enum class Outcome : unsigned char { Accepted, Unsupported, Failed };
struct Capabilities { bool known=false, modern=false, sleep=false, hibernate=false; std::uint32_t error=0; };
struct Reply { Outcome outcome=Outcome::Failed; std::uint32_t error=0; };
struct Attempt { Mode mode=Mode::None; bool invoked=false; Reply reply{}; };
struct Result { Mode target=Mode::None; Reply reply{}; std::array<Attempt,3> attempts{}; unsigned count=0; };
inline const char* name(Mode mode) {
    switch (mode) { case Mode::ModernStandby:return "modern-standby";case Mode::Sleep:return "sleep";
        case Mode::Hibernate:return "hibernate";default:return "none"; }
}
inline const char* api(Mode mode) {
    return mode==Mode::ModernStandby ? "SC_MONITORPOWER" : mode==Mode::None ? "none" : "SetSuspendState";
}
inline const char* name(Outcome result) {
    return result==Outcome::Accepted ? "accepted-pending-confirmation" : result==Outcome::Unsupported ? "unsupported" : "failed";
}
// Only operation-level Win32 unsupported errors authorize trying another mode.
// Permission/token errors and invalid arguments must be returned as Failed by
// their adapter, even if a prerequisite happened to use an unsupported code.
inline bool unsupportedError(std::uint32_t code) { return code==1 || code==50 || code==120; }
template<class Request> Result request(const Capabilities& caps, Request&& invoke, Mode first=Mode::ModernStandby, Mode last=Mode::Hibernate) {
    Result result;
    if (!caps.known) { result.reply={Outcome::Failed,caps.error ? caps.error : 31U}; return result; }
    constexpr std::array modes{Mode::ModernStandby,Mode::Sleep,Mode::Hibernate};
    const std::array support{caps.modern,caps.sleep,caps.hibernate};
    for (unsigned i=0;i<modes.size();++i) {
        if(modes[i]<first||modes[i]>last)continue;
        auto& attempt=result.attempts[result.count++];attempt.mode=modes[i];
        attempt.invoked=support[i];
        attempt.reply=attempt.invoked ? invoke(attempt.mode) : Reply{Outcome::Unsupported,50};
        result.target=attempt.mode;result.reply=attempt.reply;
        if (attempt.reply.outcome!=Outcome::Unsupported) return result;
    }
    result.target=Mode::None;
    return result;
}
} // namespace ymcc::sleeprequest
