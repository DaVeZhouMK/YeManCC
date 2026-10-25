#pragma once
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace ymcc::steamappstate {
using Json = nlohmann::json;
using Read = std::function<bool(std::uint64_t, void*, std::size_t)>;
using Stable = std::function<bool()>;
enum class ProfileId { Legacy1791918634, Public1792618285 };
struct Query {
    std::uint64_t moduleBase = 0, moduleSize = 0;
    std::uint32_t appid = 0;
    bool profileVerified = false;
    ProfileId profile = ProfileId::Legacy1791918634;
};
inline constexpr char kUiSha256[] = "1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252";
inline constexpr char kProfileVersion[] = "steamui-1b945794-app-state-v1";
inline constexpr char kUiSha256Public1792618285[] = "cd9226db4e6c28942df70c22c0475d91fdd62cba5f100baae2d65af57cabb610";
inline std::optional<ProfileId> profileIdForSha256(std::string_view hash) noexcept {
    if (hash == kUiSha256) return ProfileId::Legacy1791918634;
    if (hash == kUiSha256Public1792618285) return ProfileId::Public1792618285;
    return std::nullopt;
}
inline const char* profileSha256(ProfileId id) noexcept {
    switch (id) {
    case ProfileId::Legacy1791918634: return kUiSha256;
    case ProfileId::Public1792618285: return kUiSha256Public1792618285;
    }
    return nullptr;
}
inline const char* profileVersion(ProfileId id) noexcept {
    switch (id) {
    case ProfileId::Legacy1791918634: return kProfileVersion;
    case ProfileId::Public1792618285: return "steamui-cd9226db-app-state-v1";
    }
    return "unqualified-profile";
}


namespace detail {
inline constexpr std::uint64_t kMinAddress = 0x10000, kMaxAddress = 0x7fffffffffff;
inline constexpr std::uint32_t kMaxIndex = 65535, kMaxBucket = 65535, kMaxChain = 128, kMaxFallbacks = 32;
inline constexpr std::size_t kMaxReadCalls = 1024, kMaxReadBytes = 16384, kMaxSingleRead = 64;
inline constexpr std::uint64_t kGlobalRva = 0x1196360, kAppVptrRva = 0xda91a8;
inline constexpr char kLookupBytes[] =
    "405355565741564883ec20837944004c8bf2488bf90f848b000000448b028b711041c1e810ffce4433028bde4169c06bcaeb85"
    "448bc041c1e80d4433c04169c035aeb2c28be8c1ed1033e823dd0f1f00488b074863cb8b048883f8ff74294c8b4730458b0e"
    "6666660f1f8400000000004863c8488d1449453b0cd0742e418b44d01083f8ff75e9d1fe3b77547c188bde23dd3b5f4c7c0f"
    "488d4f188bd3e8fe3cf4ff84c074aab8ffffffff4883c420415e5f5e5d5bc3";
inline constexpr char kBitmapBytes[] =
    "48895c2408574883ec204863fa488bd985d278043b397c284c8d05f95cb900ba4a010000488d0d5558b900ff15dfd4b80084c0"
    "750bff15cdd4b80084c07401cc837b040175064883c308eb04488b5b10488bc78bcf48c1f80583e11f8b04830fa3c8488b5c24"
    "300f92c04883c4205fc3";
inline constexpr char kGlobalLeafBytes[] = "488b05e9cbb500c3";
inline constexpr char kAppGetterBytes[] = "8b4120c3";
inline constexpr char kBitmapPublic1792618285Bytes[] =
    "48895c2408574883ec204863fa488bd985d278043b397c284c8d05095db900ba4a010000488d0d5558b900ff15dfd4b80084c0"
    "750bff15cdd4b80084c07401cc837b040175064883c308eb04488b5b10488bc78bcf48c1f80583e11f8b04830fa3c8488b5c24"
    "300f92c04883c4205fc3";
inline constexpr char kGlobalLeafPublic1792618285Bytes[] = "488b05c9cab500c3";
struct ProfileAnchors {
    std::uint64_t appVptr, globalLeaf, controllerPrimary, controllerSecondary, callbackTable, callback;
};
inline constexpr ProfileAnchors kLegacyAnchors{0xda91a8,0x639770,0xdb0888,0xdb08b8,0xdb02d0,0x627480};
inline constexpr ProfileAnchors kPublic1792618285Anchors{0xda95c8,0x639890,0xdb0cd8,0xdb0d08,0xdb0720,0x6275a0};
inline const ProfileAnchors& anchors(const Query& query) {
    // The caller's complete module hash selects the profile before any reads.
    // Invalid enum values are rejected by observe/checkAnchors, never guessed.
    return query.profile == ProfileId::Public1792618285 ? kPublic1792618285Anchors : kLegacyAnchors;
}

struct Rejected { const char* reason; };
inline void require(bool condition, const char* reason) {
    if (!condition) throw Rejected{reason};
}
inline bool inRange(std::uint64_t address, std::uint64_t size) {
    return address >= kMinAddress && address <= kMaxAddress && size != 0 && size - 1 <= kMaxAddress - address;
}
inline std::uint64_t offset(std::uint64_t address, std::uint64_t displacement) {
    require(inRange(address, 1) && displacement <= kMaxAddress - address, "address-out-of-range");
    return address + displacement;
}
inline void pointer(std::uint64_t address, std::uint64_t alignment) {
    require(inRange(address, 1) && address % alignment == 0, "pointer-invalid");
}
inline std::uint64_t moduleAnchor(const Query& query, std::uint64_t rva, std::uint64_t size) {
    require(rva <= query.moduleSize && size <= query.moduleSize - rva, "module-anchor-out-of-range");
    const auto address = offset(query.moduleBase, rva);
    require(inRange(address, size), "address-out-of-range");
    return address;
}
struct Field {
    std::uint64_t address = 0, value = 0;
    std::size_t size = 0;
    bool operator==(const Field& other) const {
        return address == other.address && value == other.value && size == other.size;
    }
};
class Reader {
    const Read& callback_;
public:
    std::size_t calls = 0, bytes = 0;
    std::vector<Field> fields;
    explicit Reader(const Read& callback) : callback_(callback) {}
    void read(std::uint64_t address, void* destination, std::size_t size) {
        require(size != 0 && size <= kMaxSingleRead, "read-size-invalid");
        require(inRange(address, size), "address-out-of-range");
        require(calls < kMaxReadCalls && bytes <= kMaxReadBytes && size <= kMaxReadBytes - bytes, "read-budget-exceeded");
        ++calls;
        bytes += size;
        bool success = false;
        try { success = callback_(address, destination, size); }
        catch (...) { throw Rejected{"memory-read-exception"}; }
        require(success, "memory-read-failed");
    }
    std::uint64_t integer(std::uint64_t address, std::size_t size) {
        require(size == 4 || size == 8, "integer-size-invalid");
        std::array<unsigned char, 8> data{};
        read(address, data.data(), size);
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < size; ++index) value |= std::uint64_t(data[index]) << (8 * index);
        fields.push_back({address, value, size});
        return value;
    }
    std::uint32_t u32(std::uint64_t address) { return static_cast<std::uint32_t>(integer(address, 4)); }
    std::uint64_t u64(std::uint64_t address) { return integer(address, 8); }
};
inline unsigned char hexDigit(char value) {
    return static_cast<unsigned char>(value <= '9' ? value - '0' : value - 'a' + 10);
}
template<std::size_t Size>
inline void checkAnchor(Reader& reader, const Query& query, std::uint64_t rva, const char (&expected)[Size]) {
    static_assert((Size - 1) % 2 == 0);
    constexpr auto length = (Size - 1) / 2;
    const auto address = moduleAnchor(query, rva, length);
    std::array<unsigned char, kMaxSingleRead> data{};
    for (std::size_t begin = 0; begin < length; begin += kMaxSingleRead) {
        const auto count = (std::min)(kMaxSingleRead, length - begin);
        reader.read(offset(address, begin), data.data(), count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto character = 2 * (begin + index);
            const auto wanted = static_cast<unsigned char>((hexDigit(expected[character]) << 4) | hexDigit(expected[character + 1]));
            require(data[index] == wanted, "loaded-anchor-mismatch");
        }
    }
}
inline void checkAnchors(Reader& reader, const Query& query) {
    require(profileSha256(query.profile) != nullptr, "profile-unverified");
    const auto& profile = anchors(query);
    checkAnchor(reader, query, 0x17a0f0, kLookupBytes);
    if (query.profile == ProfileId::Public1792618285) {
        checkAnchor(reader, query, 0xbde90, kBitmapPublic1792618285Bytes);
        checkAnchor(reader, query, profile.globalLeaf, kGlobalLeafPublic1792618285Bytes);
    } else {
        checkAnchor(reader, query, 0xbde90, kBitmapBytes);
        checkAnchor(reader, query, profile.globalLeaf, kGlobalLeafBytes);
    }
    checkAnchor(reader, query, 0xec3c0, kAppGetterBytes);
    require(reader.u64(moduleAnchor(query, profile.callbackTable + 8, 8)) == moduleAnchor(query, profile.callback, 1), "callback-slot-mismatch");
    require(reader.u64(moduleAnchor(query, profile.appVptr + 0x50, 8)) == moduleAnchor(query, 0xec3c0, 4), "app-getter-slot-mismatch");
}
inline std::uint32_t hashAppId(std::uint32_t key) {
    auto hash = static_cast<std::uint32_t>((key ^ (key >> 16)) * std::uint32_t{0x85ebca6b});
    hash = static_cast<std::uint32_t>((hash ^ (hash >> 13)) * std::uint32_t{0xc2b2ae35});
    return hash ^ (hash >> 16);
}
struct Header {
    std::uint64_t address = 0, buckets = 0, entries = 0;
    std::uint32_t bucketCount = 0, gate = 0, bucketLower = 0, maskLower = 0;
};
inline Header readHeader(Reader& reader, const Query& query) {
    const auto owner = reader.u64(moduleAnchor(query, kGlobalRva, 8));
    pointer(owner, 8);
    const auto controller = reader.u64(offset(owner, 0xe58));
    pointer(controller, 8);
    require(reader.u64(controller) == moduleAnchor(query, anchors(query).controllerPrimary, 8), "controller-primary-type-mismatch");
    require(reader.u64(offset(controller, 8)) == moduleAnchor(query, anchors(query).controllerSecondary, 8), "controller-secondary-type-mismatch");
    require(reader.u64(offset(controller, 0x20)) == moduleAnchor(query, anchors(query).callbackTable, 16), "controller-callback-type-mismatch");
    Header header;
    header.address = offset(controller, 0x350);
    header.buckets = reader.u64(header.address);
    header.bucketCount = reader.u32(offset(header.address, 0x10));
    header.entries = reader.u64(offset(header.address, 0x30));
    header.gate = reader.u32(offset(header.address, 0x44));
    header.bucketLower = reader.u32(offset(header.address, 0x4c));
    header.maskLower = reader.u32(offset(header.address, 0x54));
    require(header.gate <= 0x7fffffffU, "entry-gate-invalid");
    require(header.gate != 0, "target-app-not-observed");
    require(header.bucketCount != 0 && header.bucketCount <= kMaxBucket + 1 &&
        (header.bucketCount & (header.bucketCount - 1)) == 0, "bucket-count-invalid");
    require(header.bucketLower <= kMaxBucket && header.maskLower <= kMaxBucket, "fallback-bound-invalid");
    pointer(header.buckets, 4);
    pointer(header.entries, 8);
    require(inRange(header.buckets, 4ULL * header.bucketCount) && inRange(header.entries, 1), "table-address-out-of-range");
    return header;
}
inline bool bitmap(Reader& reader, const Header& header, std::uint32_t bucket) {
    const auto address = offset(header.address, 0x18);
    const auto bitCount = reader.u32(address), mode = reader.u32(offset(address, 4));
    require(bitCount != 0 && bitCount <= kMaxBucket + 1 && bucket < bitCount, "bitmap-index-invalid");
    require(mode != 0 && mode <= kMaxBucket + 1, "bitmap-mode-invalid");
    std::uint64_t words = 0;
    if (mode == 1) {
        require(bitCount <= 32, "bitmap-inline-range-invalid");
        words = offset(address, 8);
    } else {
        words = reader.u64(offset(address, 0x10));
    }
    pointer(words, 4);
    const auto word = reader.u32(offset(words, std::uint64_t(bucket >> 5) * 4));
    return (word & (std::uint32_t{1} << (bucket & 31))) != 0;
}
inline std::uint32_t readFlags(Reader& reader, const Query& query) {
    const auto header = readHeader(reader, query);
    const auto hash = hashAppId(query.appid);
    auto mask = header.bucketCount - 1;
    unsigned totalNodes = 0, fallbacks = 0;
    for (;;) {
        const auto bucket = hash & mask;
        require(bucket <= kMaxBucket && bucket < header.bucketCount, "bucket-index-invalid");
        auto index = reader.u32(offset(header.buckets, std::uint64_t(bucket) * 4));
        std::vector<std::uint32_t> visited;
        while (index != 0xffffffffU) {
            require(index <= kMaxIndex, "node-index-invalid");
            require(std::find(visited.begin(), visited.end(), index) == visited.end(), "node-chain-cycle");
            require(totalNodes < kMaxChain, "node-chain-budget-exceeded");
            ++totalNodes;
            visited.push_back(index);
            const auto node = offset(header.entries, std::uint64_t(index) * 24);
            require(inRange(node, 24), "node-address-out-of-range");
            const auto key = reader.u32(node);
            const auto app = reader.u64(offset(node, 8));
            const auto next = reader.u32(offset(node, 0x10));
            require(next == 0xffffffffU || next <= kMaxIndex, "node-next-invalid");
            if (key == query.appid) {
                pointer(app, 8);
                require(reader.u64(app) == moduleAnchor(query, anchors(query).appVptr, 8), "app-type-mismatch");
                require(reader.u32(offset(app, 0x18)) != 0, "app-hit-guard-empty");
                return reader.u32(offset(app, 0x20));
            }
            index = next;
        }
        require(fallbacks < kMaxFallbacks, "fallback-budget-exceeded");
        ++fallbacks;
        const auto nextMask = mask >> 1;
        require(nextMask >= header.maskLower, "target-app-not-observed");
        const auto nextBucket = hash & nextMask;
        require(nextBucket >= header.bucketLower, "target-app-not-observed");
        require(!bitmap(reader, header, nextBucket), "target-app-not-observed");
        require(nextMask < mask, "fallback-no-progress");
        mask = nextMask;
    }
}
inline bool stable(const Stable& callback) {
    try { return callback(); }
    catch (...) { throw Rejected{"identity-check-failed"}; }
}
}

inline Json observe(const Query& query, const Read& read, const Stable& stable) {
    using namespace detail;
    const Query input = query;
    Json result = {{"source", "steam-native-app-state-diagnostic"}, {"profileVersion", profileVersion(input.profile)},
        {"authority", "diagnostic-only-no-admission"}, {"status", "unavailable"}, {"available", false},
        {"reason", "identity-missing"}, {"nativeAppFlags", nullptr}, {"runningBitSet", nullptr},
        {"readCalls", 0}, {"readBytes", 0}, {"atomicSnapshot", false}, {"abaExcluded", false},
        {"nativeCapacityVerified", false}, {"controllerBinding", "triple-typed-controller-global-candidate"},
        {"uniqueLiveControllerVerified", false}, {"controllerGenerationLease", "unknown"},
        {"sameLiveCallbackObject", "unknown"}, {"registrationLiveness", "unknown"},
        {"objectConstructionComplete", "unknown"}, {"loadedImageFullyHashed", false},
        {"loginGenerationLease", "unknown"}, {"inputVerified", false}, {"continueVerified", false}};
    Reader reader(read);
    bool identityStarted = false;
    std::uint32_t flags = 0;
    const char* failure = nullptr;
    try {
        require(input.profileVerified && profileSha256(input.profile) != nullptr, "profile-unverified");
        require(input.appid != 0, "appid-missing");
        require(static_cast<bool>(read), "reader-unavailable");
        require(static_cast<bool>(stable), "stability-check-unavailable");
        require(inRange(input.moduleBase, input.moduleSize), "module-range-invalid");
        moduleAnchor(input, kGlobalRva, 8);
        moduleAnchor(input, anchors(input).appVptr, 8);
        moduleAnchor(input, 0x17a0f0, (sizeof(kLookupBytes) - 1) / 2);
        moduleAnchor(input, 0xbde90, (sizeof(kBitmapBytes) - 1) / 2);
        moduleAnchor(input, anchors(input).globalLeaf, (sizeof(kGlobalLeafBytes) - 1) / 2);
        moduleAnchor(input, 0xec3c0, 4);
        moduleAnchor(input, anchors(input).callbackTable, 16);
        moduleAnchor(input, anchors(input).controllerPrimary, 8);
        moduleAnchor(input, anchors(input).controllerSecondary, 8);
        moduleAnchor(input, anchors(input).appVptr + 0x50, 8);
        moduleAnchor(input, anchors(input).callback, 1);
        require(detail::stable(stable), "identity-changed");
        identityStarted = true;
        checkAnchors(reader, input);
        reader.fields.clear();
        flags = readFlags(reader, input);
        const auto before = reader.fields;
        reader.fields.clear();
        const auto afterFlags = readFlags(reader, input);
        require(flags == afterFlags, "app-flags-changed");
        require(before == reader.fields, "lookup-observation-changed");
        checkAnchors(reader, input);
    } catch (const Rejected& error) { failure = error.reason; }
    catch (...) { failure = "diagnostic-decode-failed"; }
    if (identityStarted) {
        try { require(detail::stable(stable), "identity-changed"); }
        catch (const Rejected& error) { failure = error.reason; }
    }
    result["readCalls"] = reader.calls;
    result["readBytes"] = reader.bytes;
    if (failure) {
        result["reason"] = failure;
        return result;
    }
    result["status"] = "observed";
    result["available"] = true;
    result["reason"] = "target-app-state-observed";
    result["nativeAppFlags"] = flags;
    result["runningBitSet"] = (flags & 0x2000U) != 0;
    return result;
}
}
