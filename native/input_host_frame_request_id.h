#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>

namespace ymcc::inputHost {
// Correlation only; startup nonce, tuple, sequence and content checks still
// authenticate/validate every frame. The producer owns this counter under
// g_inputHostMx for its whole process lifetime, across target/session resets.
class FrameRequestIds final {
    std::uint64_t issued_ = 0;
public:
    explicit FrameRequestIds(std::uint64_t alreadyIssued = 0) noexcept : issued_(alreadyIssued) {}
    bool tryNext(std::string& id) {
        if (issued_ == (std::numeric_limits<std::uint64_t>::max)()) return false;
        char number[20];
        const auto value = ++issued_; // A failed allocation burns this ID; never reuse it.
        const auto converted = std::to_chars(number, number + sizeof(number), value);
        id.assign("frame-");
        id.append(number, converted.ptr);
        return true;
    }
};
}