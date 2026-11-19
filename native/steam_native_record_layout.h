#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// One calibration data source for both existing readers. Digests below are
// profile metadata; the caller still proves file/loaded identity and ownership.
namespace ymcc::steamrecord {
enum class ProfileId { Legacy1791918634, Public1792618285, OwnerScoped };
inline constexpr char kClientSha256[] = "e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e";
inline constexpr char kProfileVersion[] = "steamclient64-e9045710-cuser-record-v1";
inline const char* profileSha256(ProfileId id) {
    switch (id) {
    case ProfileId::Legacy1791918634: return kClientSha256;
    case ProfileId::Public1792618285: return "253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc";
    case ProfileId::OwnerScoped: return "ebf46f84606e4dc49ebd551f6621f7ca5c19927d572a49f0aa4925ce9414096c";
    }
    return nullptr;
}
inline const char* profileVersion(ProfileId id) {
    switch (id) {
    case ProfileId::Legacy1791918634: return kProfileVersion;
    case ProfileId::Public1792618285: return "steamclient64-253e0a11-cuser-record-v1";
    case ProfileId::OwnerScoped: return "steamclient64-owner-scoped-cuser-record-v1";
    }
    return nullptr;
}
inline std::optional<ProfileId> profileIdForSha256(std::string_view sha256) {
    // Exact full digest; no prefix, build-number or unknown-enum fallback.
    for (const auto id : {ProfileId::Legacy1791918634, ProfileId::Public1792618285, ProfileId::OwnerScoped})
        if (sha256 == profileSha256(id)) return id;
    return std::nullopt;
}
namespace detail {
struct Rejected { const char* reason; };
// Independently qualified against each fixed PE. Equal fields are intentional;
// code/RIP/RTTI evidence and full function ranges are in client-profile evidence.
struct ProfileLayout {
    std::uint64_t getterRva, engineRoot, engineVtable, engineInterfaceVtable, userVtable, userInterfaceVtable;
    std::array<unsigned char, 14> getterBytes;
    std::uint64_t engineInterfaceOffset, userVectorOffset, userCountOffset, userStride, userEntryOffset;
    std::uint64_t userInterfaceOffset, accountOffset, recordsOffset, recordCountOffset, recordStride, gameIdOffset, pidPairOffset;
    std::array<std::uint64_t, 7> predicateRvas;
    std::array<std::array<unsigned char, 7>, 7> predicateBytes;
    std::array<std::size_t, 7> predicateSizes;
    std::uint64_t predicateFlagsOffset, predicateCounterOffset, predicateAlternateOffset;
    const char* predicateVersion;
};
inline const ProfileLayout& profileLayout(ProfileId id) {
    static constexpr ProfileLayout legacy {
        0xcfdc0, 0x181d008, 0x1361f38, 0x1361ff0, 0x136fa80, 0x136fbd0,
        {0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3},
        0xfe0, 0x1080, 0x1090, 0x10, 8, 0x1e70, 0x20a, 0x2568, 0x2578, 0x330, 8, 0x198,
        {0x9d7a9c,0x9d7bc9,0x9d7b2a,0x9d7a5f,0x9d7aef,0x9d7bb6,0x9d7bd9},
        {{{0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}, {0xff,0x83,0xa8,0x01,0x00,0x00},
          {0x8b,0x8b,0x68,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}}}, {7,6,6,6,6,6,6},
        0x200, 0x1a8, 0x268, "steamclient64-e9045710-predicate-inputs-v1"
    };
    static constexpr ProfileLayout publicBuild {
        0xcfdc0, 0x181e008, 0x1362478, 0x1362530, 0x1370030, 0x1370180,
        {0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3},
        0xfe0, 0x1080, 0x1090, 0x10, 8, 0x1e70, 0x20a, 0x2568, 0x2578, 0x330, 8, 0x198,
        {0x9d7b7c,0x9d7ca9,0x9d7c0a,0x9d7b3f,0x9d7bcf,0x9d7c96,0x9d7cb9},
        {{{0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}, {0xff,0x83,0xa8,0x01,0x00,0x00},
          {0x8b,0x8b,0x68,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}}}, {7,6,6,6,6,6,6},
        0x200, 0x1a8, 0x268, "steamclient64-253e0a11-predicate-inputs-v1"
    };
    static constexpr ProfileLayout ownerScoped {
        0xcfed0, 0x18546d8, 0x138ff40, 0x138fff8, 0x139d580, 0x139d6d0,
        {0x48,0x8b,0x81,0x92,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3},
        0xfe0, 0x10c8, 0x10d8, 0x10, 8, 0x1e78, 0x20a, 0x2570, 0x2580, 0x330, 8, 0x198,
        {0x9fcccc,0x9fcdf9,0x9fcd5a,0x9fcc8f,0x9fcd1f,0x9fcde6,0x9fce09},
        {{{0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}, {0xff,0x83,0xa8,0x01,0x00,0x00},
          {0x8b,0x8b,0x68,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}}}, {7,6,6,6,6,6,6},
        0x200, 0x1a8, 0x268, "steamclient64-owner-scoped-predicate-inputs-v1"
    };
    switch (id) {
    case ProfileId::Legacy1791918634: return legacy;
    case ProfileId::Public1792618285: return publicBuild;
    case ProfileId::OwnerScoped: return ownerScoped;
    }
    throw Rejected{"profile-unknown"};
}

} // namespace detail
} // namespace ymcc::steamrecord
