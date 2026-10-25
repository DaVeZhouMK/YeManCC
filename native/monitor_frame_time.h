#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
namespace ymcc::monitorframe {
inline constexpr std::uint64_t missingGraceMs = 5000;
// Producer interval plus three bounded delivery allowances: whole-second poll
// timestamp, the existing 1Hz collector, and the existing 1Hz HUD transport.
// A 3s producer therefore has 6s, not 5s. The original observation anchors it;
// repeated reads never renew it, and even slower producers cannot exceed 6s.
inline std::uint64_t hardwareLifetimeMs(std::uint64_t period) { return (std::max)(missingGraceMs,(std::min)(6000ULL,(std::min)(period,60000ULL)+3000ULL)); }
inline bool valid(double ms) { return std::isfinite(ms) && ms > 0 && ms <= 10000; }
// Bounded history of genuine observations. Retention never creates a sample or
// advances its measurement timestamp. A process change is an immediate reset.
class History {
    std::array<double,120> samples_{};
    std::size_t size_ = 0, next_ = 0;
    std::uint64_t pid_ = 0, birth_ = 0, timestamp_ = 0;
public:
    void reset() { size_ = next_ = 0; pid_ = birth_ = timestamp_ = 0; }
    bool retain(std::uint64_t pid, std::uint64_t birth, std::uint64_t now,std::uint64_t lifetime=missingGraceMs) {
        if (!pid || !birth || pid != pid_ || birth != birth_ || !timestamp_ ||
            now < timestamp_ || now - timestamp_ >= lifetime) { reset(); return false; }
        return size_ > 0;
    }
    bool observeAt(std::uint64_t pid, std::uint64_t birth, std::uint64_t timestamp, double ms,std::uint64_t lifetime=missingGraceMs) {
        if (!pid || !birth) { reset(); return false; }
        if (pid != pid_ || birth != birth_) reset();
        if (!timestamp || !valid(ms)) return false; // transient miss: keep the last proof
        if (timestamp < timestamp_ || (timestamp_ && timestamp - timestamp_ >= lifetime)) reset();
        pid_ = pid; birth_ = birth;
        if (timestamp != timestamp_) {
            samples_[next_] = ms; next_ = (next_ + 1) % samples_.size();
            if (size_ < samples_.size()) ++size_;
            timestamp_ = timestamp;
        } else samples_[(next_ + samples_.size() - 1) % samples_.size()] = ms;
        return true;
    }
    bool observe(std::uint64_t pid, std::uint64_t birth, std::uint64_t poll, double ms,std::uint64_t lifetime=missingGraceMs) {
        return observeAt(pid,birth,poll * 1000ULL,ms,lifetime);
    }
    std::size_t size() const { return size_; }
    double at(std::size_t i) const { return samples_[(next_ + samples_.size() - size_ + i) % samples_.size()]; }
    double last() const { return size_ ? at(size_ - 1) : 0; }
    std::uint64_t timestamp() const { return timestamp_; }
};
}
