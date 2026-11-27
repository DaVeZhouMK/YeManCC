#pragma once

#include <cstdint>

namespace ymcc::win5Menu {

// Bits 0-17 remain controller buttons/triggers, 20-63 remain existing OEM IDs.
inline constexpr std::uint64_t shortcutBit = 1ull << 19;
inline constexpr const char* keyId = "gpd-menu";
inline constexpr const char* label = "GPD Menu";

struct Source {
    std::uint64_t power = 0, topology = 0, identity = 0, intent = 0, episode = 0;
    unsigned slot = 4;
    bool operator==(const Source&) const = default;
    bool valid() const noexcept { return power && topology && identity && episode && slot < 4; }
};

struct Session {
    Source source;
    std::uint64_t ruleEpoch = 0;
    std::uintptr_t foreground = 0;
    bool recording = false;
    bool operator==(const Session&) const = default;
};

enum class Reading { Unavailable, Up, Down };

class ShortPress {
    Session session_{};
    std::uint64_t downAt_ = 0, lastAt_ = 0;
    bool neutral_ = false;
public:
    static constexpr std::uint64_t maximumHoldMs = 500;
    static constexpr std::uint64_t maximumSampleGapMs = 125;

    void reset() noexcept { *this = {}; }

    bool observe(const Session& session, Reading reading, std::uint64_t now) noexcept {
        if (!session.source.valid() || !session.ruleEpoch || !session.foreground ||
            !now || reading == Reading::Unavailable) {
            reset();
            return false;
        }
        if (session != session_ || now < lastAt_ || now - lastAt_ > maximumSampleGapMs) {
            reset();
            session_ = session;
        }
        lastAt_ = now;
        if (reading == Reading::Down) {
            if (neutral_ && !downAt_) downAt_ = now;
            neutral_ = false;
            return false;
        }
        const bool complete = downAt_ && now > downAt_ && now - downAt_ <= maximumHoldMs;
        downAt_ = 0;
        neutral_ = true;
        return complete;
    }
};

inline bool qualifies(bool exactBoard, bool exactPhysicalPath, bool current,
                      bool admitted, bool virtualEndpoint, bool identityUnknown,
                      int usbPortKind) noexcept {
    return exactBoard && exactPhysicalPath && current && admitted && !virtualEndpoint &&
        !identityUnknown && usbPortKind == 0;
}

} // namespace ymcc::win5Menu
