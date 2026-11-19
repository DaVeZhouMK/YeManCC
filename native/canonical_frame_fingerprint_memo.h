#pragma once
#include <array>
#include <cstdint>
#include <charconv>
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
        char canonical[160];
        // int plus six fixed-width IEEE words fits the original buffer.
        // Keep the exact decimal sign and uppercase eight-digit hex bytes.
        char* cursor = std::to_chars(canonical, canonical + sizeof(canonical), buttons).ptr;
        static constexpr char hex[] = "0123456789ABCDEF";
        for (std::size_t axis = 1; axis < key.size(); ++axis) {
            *cursor++ = '|';
            for (int shift = 28; shift >= 0; shift -= 4)
                *cursor++ = hex[(key[axis] >> shift) & 0xFu];
        }
        auto result = hash(std::string(canonical, static_cast<std::size_t>(cursor - canonical)));
        // Failed/incomplete computations never become hits. Keep fixed storage
        // so a fully changing input adds no heap allocation on a cache miss.
        valid_ = result.size() == hash_.size();
        if (valid_) { key_ = key; std::memcpy(hash_.data(), result.data(), hash_.size()); }
        return result;
    }
};
} // namespace ymcc::input
