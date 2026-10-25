#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace ymcc::gesturesummon {
enum class Phase { Down, Move, Up, Cancel };
struct EdgeBounds { int left,top,right,bottom; };
inline EdgeBounds centeredEdge(int left,int top,int right,int bottom,int width,bool fromLeft) noexcept {
    const int marginY=(bottom-top)/10;
    return {fromLeft?left:right-width,top+marginY,fromLeft?left+width:right,bottom-marginY};
}
// Coordinates and thresholds share the surface's physical-pixel space.
class Recognizer {
    std::array<std::uint32_t, 16> contacts_{};
    std::uint32_t candidate_ = 0, started_ = 0;
    float startX_ = 0, startY_ = 0, scale_ = 1;
    bool left_ = false, rejected_ = false;
public:
    bool hasContacts() const { for (const auto id : contacts_) if (id) return true; return false; }
    void reset(bool left, float scale) {
        contacts_ = {}; candidate_ = 0; rejected_ = false;
        left_ = left; scale_ = std::isfinite(scale) && scale > 0 ? scale : 1;
    }
    bool event(std::uint32_t id, Phase phase, float x, float y, std::uint32_t time, bool touch = true) {
        if (!id) return false;
        auto slot = contacts_.end(); unsigned count = 0;
        for (auto it = contacts_.begin(); it != contacts_.end(); ++it) {
            if (*it) ++count;
            if (*it == id) slot = it;
        }
        if (phase == Phase::Down) {
            if (slot != contacts_.end()) { rejected_ = true; return false; }
            for (auto& contact : contacts_) if (!contact) { contact = id; break; }
            if (!count) {
                candidate_ = id; startX_ = x; startY_ = y; started_ = time;
                rejected_ = !touch || !std::isfinite(x) || !std::isfinite(y);
            } else rejected_ = true; // Block the whole overlapping sequence.
            return false;
        }
        if (slot == contacts_.end()) return false;
        const float inward = (left_ ? x - startX_ : startX_ - x) / scale_;
        const float vertical = std::abs(y - startY_) / scale_;
        if (!touch || !std::isfinite(x) || !std::isfinite(y) || phase == Phase::Cancel ||
            time - started_ > 1000 || inward < -16 || vertical > 48) rejected_ = true;
        if (phase != Phase::Up && phase != Phase::Cancel) return false;
        const bool summon = id == candidate_ && !rejected_ && count == 1 &&
            phase == Phase::Up && inward >= 80 && vertical <= inward * 0.6f;
        *slot = 0;
        if (count == 1) { candidate_ = 0; rejected_ = false; }
        return summon;
    }
};
} // namespace ymcc::gesturesummon
