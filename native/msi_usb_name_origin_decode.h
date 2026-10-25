#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <json.hpp>

namespace ymcc::msi::usbName {
using json = nlohmann::json;
inline constexpr std::size_t maxConfigurationBytes = 16384;

namespace detail {
// Parsing is bounded by USB lengths; evidence copying is linear in the supplied
// vector so even bytes outside a descriptor's reported extent remain available.
inline json evidence(const std::vector<std::uint8_t>& bytes) {
    json result = {{"valid", false}, {"error", ""}, {"rawBytes", bytes},
                   {"actualBytes", bytes.size()}};
    if (!bytes.empty()) result["reportedLength"] = bytes[0];
    if (bytes.size() >= 2) result["reportedType"] = bytes[1];
    return result;
}

// Call only after checking that offset and offset + 1 are within the extent.
inline std::uint16_t littleEndian16(const std::vector<std::uint8_t>& bytes,
                                  std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[offset]) |
                                    (static_cast<unsigned>(bytes[offset + 1]) << 8));
}

inline void trailingEvidence(json& result, const std::vector<std::uint8_t>& bytes,
                             std::size_t extent) {
    auto trailing = json::array();
    for (std::size_t offset = extent; offset < bytes.size(); ++offset)
        trailing.push_back(bytes[offset]);
    result["trailingBytes"] = std::move(trailing);
}

inline json fail(json result, const char* error) {
    result["error"] = error;
    return result;
}

inline bool wellFormedUtf16(const std::vector<std::uint16_t>& units) {
    for (std::size_t offset = 0; offset < units.size(); ++offset) {
        const auto unit = units[offset];
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (offset + 1 == units.size()) return false;
            const auto next = units[++offset];
            if (next < 0xdc00 || next > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
    }
    return true;
}
} // namespace detail

// This receipt describes the supplied descriptor, never an active device state.
inline json deviceDescriptor(const std::vector<std::uint8_t>& bytes) {
    auto result = detail::evidence(bytes);
    if (bytes.size() < 2) return detail::fail(std::move(result), "device-header-truncated");
    if (bytes[0] != 18) return detail::fail(std::move(result), "device-length-invalid");
    if (bytes[1] != 1) return detail::fail(std::move(result), "device-type-invalid");
    if (bytes.size() < 18) return detail::fail(std::move(result), "device-truncated");

    result["vendorId"] = detail::littleEndian16(bytes, 8);
    result["productId"] = detail::littleEndian16(bytes, 10);
    result["iManufacturer"] = bytes[14];
    result["iProduct"] = bytes[15];
    result["iSerialNumber"] = bytes[16];
    result["numConfigurations"] = bytes[17];
    detail::trailingEvidence(result, bytes, 18);
    result["valid"] = true;
    return result;
}

inline json configurationDescriptor(const std::vector<std::uint8_t>& bytes) {
    auto result = detail::evidence(bytes);
    result["interfaces"] = json::array();
    result["iads"] = json::array();
    if (bytes.size() < 2)
        return detail::fail(std::move(result), "configuration-header-truncated");
    if (bytes[0] != 9)
        return detail::fail(std::move(result), "configuration-length-invalid");
    if (bytes[1] != 2)
        return detail::fail(std::move(result), "configuration-type-invalid");
    if (bytes.size() < 9)
        return detail::fail(std::move(result), "configuration-header-truncated");

    const std::size_t totalLength = detail::littleEndian16(bytes, 2);
    result["totalLength"] = totalLength;
    result["configurationValue"] = bytes[5];
    result["numInterfaces"] = bytes[4];
    if (totalLength < 9)
        return detail::fail(std::move(result), "configuration-total-length-invalid");
    if (totalLength > maxConfigurationBytes)
        return detail::fail(std::move(result), "configuration-total-length-limit");
    if (totalLength > bytes.size())
        return detail::fail(std::move(result), "configuration-truncated");

    detail::trailingEvidence(result, bytes, totalLength);
    for (std::size_t offset = 9; offset < totalLength;) {
        // A failing receipt can contain earlier parsed entries as evidence only.
        // Consumers must check valid before treating the complete walk as valid.
        result["errorOffset"] = offset;
        const auto remaining = totalLength - offset;
        if (remaining < 2)
            return detail::fail(std::move(result), "descriptor-header-truncated");
        const std::size_t length = bytes[offset];
        const auto type = bytes[offset + 1];
        if (length < 2)
            return detail::fail(std::move(result), "descriptor-length-invalid");
        if (length > remaining)
            return detail::fail(std::move(result), "descriptor-truncated");
        if (type == 4) {
            if (length < 9)
                return detail::fail(std::move(result), "interface-length-invalid");
            result["interfaces"].push_back({{"number", bytes[offset + 2]},
                                           {"alternateSetting", bytes[offset + 3]},
                                           {"iInterface", bytes[offset + 8]}});
        } else if (type == 11) {
            if (length < 8)
                return detail::fail(std::move(result), "iad-length-invalid");
            result["iads"].push_back({{"firstInterface", bytes[offset + 2]},
                                     {"interfaceCount", bytes[offset + 3]},
                                     {"iFunction", bytes[offset + 7]}});
        }
        offset += length; // At least two bytes of progress, within totalLength.
    }
    result.erase("errorOffset");
    result["valid"] = true;
    return result;
}

// Index zero is a LANGID list. Indexed strings expose numeric UTF-16 units;
// no text conversion, name interpretation, OS access, or caller state is used.
// Ill-formed UTF-16 retains its units but returns valid=false and an error.
inline json stringDescriptor(const std::vector<std::uint8_t>& bytes, std::uint8_t index) {
    auto result = detail::evidence(bytes);
    result["index"] = index;
    if (bytes.size() < 2) return detail::fail(std::move(result), "string-header-truncated");
    if (bytes[1] != 3) return detail::fail(std::move(result), "string-type-invalid");
    const std::size_t length = bytes[0];
    if (length < 2 || length % 2 != 0)
        return detail::fail(std::move(result), "string-length-invalid");
    if (length > bytes.size()) return detail::fail(std::move(result), "string-truncated");
    detail::trailingEvidence(result, bytes, length);

    if (index == 0) {
        result["languageIds"] = json::array();
        if (length < 4) return detail::fail(std::move(result), "language-list-empty");
        auto languageIds = json::array();
        for (std::size_t offset = 2; offset < length; offset += 2) {
            const auto language = detail::littleEndian16(bytes, offset);
            if (language == 0) return detail::fail(std::move(result), "language-id-zero");
            languageIds.push_back(language);
        }
        result["languageIds"] = std::move(languageIds);
    } else {
        std::vector<std::uint16_t> units;
        units.reserve((length - 2) / 2); // bLength is one byte: at most 126 units.
        for (std::size_t offset = 2; offset < length; offset += 2)
            units.push_back(detail::littleEndian16(bytes, offset));
        result["utf16"] = units;
        const bool wellFormed = detail::wellFormedUtf16(units);
        result["utf16WellFormed"] = wellFormed;
        if (!wellFormed) return detail::fail(std::move(result), "utf16-ill-formed");
    }
    result["valid"] = true;
    return result;
}
} // namespace ymcc::msi::usbName
