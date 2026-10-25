#pragma once
#include <cstddef>
#include <string>
#include <string_view>

// The monitor snapshot is a high-frequency in-memory/cache transport. Keep
// its two JSON strings bounded before they reach the IPC envelope, compatibility
// files, or the diagnostic queue. Reject an oversized document as a whole;
// truncating JSON would turn a temporary size problem into a false reading.
namespace ymcc::monitorsnapshot {
inline constexpr std::size_t topMaxBytes = 16 * 1024;
inline constexpr std::size_t fpsMaxBytes = 16 * 1024;
inline constexpr std::size_t combinedMaxBytes = 24 * 1024;

inline constexpr bool textFits(std::string_view top, std::string_view fps) noexcept {
    return top.size() <= topMaxBytes && fps.size() <= fpsMaxBytes &&
        top.size() <= combinedMaxBytes && fps.size() <= combinedMaxBytes - top.size();
}

inline constexpr bool topFits(std::string_view text) noexcept {
    return text.size() <= topMaxBytes;
}

inline constexpr bool fpsFits(std::string_view text) noexcept {
    return text.size() <= fpsMaxBytes;
}

inline void discard(std::string& text) noexcept {
    std::string{}.swap(text);
}

// Bound retained document bytes AND avoid moving an oversized reserved buffer
// into the long-lived cache. Small normal producer strings take only O(1) checks.
inline bool retain(std::string& text, std::size_t maxBytes) {
    if (text.size() > maxBytes) { discard(text); return false; }
    if (text.capacity() > maxBytes) { std::string compact(text); text.swap(compact); }
    return true;
}
}
