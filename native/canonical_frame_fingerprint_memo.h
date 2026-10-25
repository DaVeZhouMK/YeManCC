#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ymcc::input {
// A pure hash memo, never a sample/report cache. Every float is keyed by its
// original IEEE-754 bits; signed zero and NaN payloads stay distinct.
class CanonicalFrameFingerprintMemo {
    std::array<std::uint32_t, 7> key_{};
    std::array<char, 64> hash_{};
    bool valid_ = false;
    static std::uint32_t bits(float value) noexcept {
        std::uint32_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
public:
    template<class Hash>
    std::string compute(int buttons, float lx, float ly, float rx, float ry,
                        float lt, float rt, Hash&& hash) {
        const std::array<std::uint32_t, 7> key{static_cast<std::uint32_t>(buttons),
            bits(lx), bits(ly), bits(rx), bits(ry), bits(lt), bits(rt)};
        if (valid_ && key == key_) return std::string(hash_.data(), hash_.size());
        char canonical[160]{};
        std::snprintf(canonical, sizeof(canonical), "%d|%08X|%08X|%08X|%08X|%08X|%08X", buttons,
            key[1], key[2], key[3], key[4], key[5], key[6]);
        auto result = hash(std::string(canonical));
        // Failed/incomplete computations never become hits. Keep fixed storage
        // so a fully changing input adds no heap allocation on a cache miss.
        valid_ = result.size() == hash_.size();
        if (valid_) { key_ = key; std::memcpy(hash_.data(), result.data(), hash_.size()); }
        return result;
    }
};
} // namespace ymcc::input
