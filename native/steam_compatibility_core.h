#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>

// A compatibility layout is selected by the code/data contract actually read.
// Full image hashes and marketing/client build versions do not grant admission.
namespace ymcc::steamcompat {
enum class Layout { Legacy, Public };
using ReadRva = std::function<bool(std::uint64_t, void*, std::size_t)>;
struct SectionSpec {
    std::uint64_t rva = 0, virtualSize = 0, rawSize = 0;
    std::uint32_t characteristics = 0;
    std::array<char, 8> name{};
};
struct RuntimeFunction { std::uint64_t tableRva = 0, begin = 0, end = 0, unwind = 0; };
struct Image {
    ReadRva read;
    std::uint64_t preferredBase = 0, imageSize = 0;
    std::span<const SectionSpec> sections{};
    std::span<const RuntimeFunction> functions{};
    bool loadedImage = false;
};
struct Range {
    std::uint64_t rva = 0;
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
};
struct Evidence {
    bool matched = false;
    std::uint64_t rangesChecked = 0, bytesChecked = 0, failedRva = 0;
    const char* reason = "contract-unqualified";
    std::uint64_t mutableRangesIgnored = 0;
};
inline Evidence checkRanges(const Image& image, std::span<const Range> ranges) noexcept {
    Evidence proof;
    if (!image.read || image.preferredBase < 0x10000 || !image.imageSize ||
        image.imageSize > 0x40000000 || ranges.empty() || ranges.size() > 4096) {
        proof.reason = "contract-image-or-range-invalid"; return proof;
    }
    constexpr std::uint64_t maxBytes = 1024 * 1024;
    std::uint64_t total = 0;
    for (const auto& range : ranges) {
        if (!range.bytes || !range.size || range.rva >= image.imageSize ||
            range.size > image.imageSize - range.rva || range.size > maxBytes - total) {
            proof.failedRva = range.rva; proof.reason = "contract-range-invalid"; return proof;
        }
        total += range.size;
    }
    std::array<unsigned char, 512> block{};
    try {
        for (const auto& range : ranges) {
            // Writable globals contain live state after loading. Their PE zero
            // initializer is not a runtime invariant; code/readonly references
            // are still checked and the original account/record is typed later.
            bool mutableData=false;
            if(image.loadedImage)for(const auto& section:image.sections)
                if((section.characteristics&0x80000000u)&&range.rva>=section.rva&&
                    range.rva-section.rva<(std::max)(section.virtualSize,section.rawSize)&&
                    range.size<=(std::max)(section.virtualSize,section.rawSize)-(range.rva-section.rva))mutableData=true;
            if(mutableData){++proof.mutableRangesIgnored;continue;}
            for (std::size_t offset = 0; offset < range.size;) {
                const auto count = (std::min)(block.size(), range.size - offset);
                proof.failedRva = range.rva + offset;
                if (!image.read(range.rva + offset, block.data(), count)) {
                    proof.reason = "contract-read-unavailable"; return proof;
                }
                proof.bytesChecked += count;
                if (std::memcmp(block.data(), range.bytes + offset, count)) {
                    proof.reason = "contract-bytes-incompatible"; return proof;
                }
                offset += count;
            }
            ++proof.rangesChecked;
        }
    } catch (...) { proof.reason = "contract-read-exception"; return proof; }
    proof.matched = true; proof.failedRva = 0; proof.reason = "contract-matched"; return proof;
}
inline bool validLayout(Layout layout) noexcept {
    return layout == Layout::Legacy || layout == Layout::Public;
}
inline Evidence invalidLayout() noexcept { return {false, 0, 0, 0, "contract-layout-invalid"}; }
}
