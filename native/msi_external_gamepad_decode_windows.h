#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <hidusage.h>
#include <hidpi.h>
#include "msi_external_raw_receipts.h"
#include <array>
#include <cstring>
#include <set>
#include <tuple>

// Passive, one-report observer helper. The caller owns witness identity, sampling
// (at most two selections/second across the observer), and delivery timestamps.
// No cache, timer, device opening, registration, or input/output transaction.
// Microsoft contracts used here:
// https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfow
// https://learn.microsoft.com/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getusagevalue
// https://learn.microsoft.com/windows-hardware/drivers/ddi/hidpi/nf-hidpi-hidp_getusages
namespace ymcc::msi::external {
namespace gamepad_decode_detail {
inline constexpr unsigned maxPreparsedBytes = 65536, maxReportBytes = 4096;
inline constexpr unsigned maxCaps = 32, maxButtons = 128, maxValues = 32;
inline bool axisUsage(unsigned page, unsigned usage) {
    return (page == 1 && usage >= 0x30 && usage <= 0x39) ||
        (page == 2 && (usage == 0xba || usage == 0xbb || usage == 0xc4 || usage == 0xc5));
}
template<class Cap> inline unsigned firstUsage(const Cap& c) {
    return c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
}
template<class Cap> inline unsigned lastUsage(const Cap& c) {
    return c.IsRange ? c.Range.UsageMax : c.NotRange.Usage;
}
template<class Cap> inline bool buttonArray(const Cap& c) {
    // This member is reserved in older SDKs/API versions. Do not query a new API.
    if constexpr (requires { c.ReportCount; }) return c.ReportCount > 1;
    else return false;
}
inline json field(unsigned id, unsigned link, unsigned page, unsigned usage) {
    return {{"reportID", id}, {"linkCollection", link}, {"usagePage", page}, {"usage", usage},
        {"logicalMin", nullptr}, {"logicalMax", nullptr}, {"bitSize", nullptr}, {"HasNull", nullptr},
        {"rawUnsigned", nullptr}, {"signedValue", nullptr}, {"state", "unknown"}};
}
}

inline nlohmann::json decodeGamepadReceipt(HANDLE rawDevice, const RawIdentity& identity,
    const unsigned char* data, unsigned size) {
    using namespace gamepad_decode_detail;
    json result = {{"state", "unknown"}, {"decodedControlsKnown", false}, {"decodeComplete", false},
        {"buttonsDown", json::array()}, {"buttons", json::array()}, {"values", json::array()},
        {"unknown", json::array()}, {"apiErrors", json::array()}, {"limitsExceeded", false}};
    auto unknown = [&](const char* reason, bool limit = false) {
        auto& reasons = result["unknown"];
        if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) reasons.push_back(reason);
        if (limit) result["limitsExceeded"] = true;
    };
    auto hidError = [&](const char* api, NTSTATUS status) {
        result["apiErrors"].push_back({{"api", api}, {"status", static_cast<int64_t>(status)},
            {"statusUnsigned", static_cast<uint32_t>(status)}});
        unknown("hid-api-error");
    };
    auto winError = [&](const char* stage, DWORD error) {
        result["apiErrors"].push_back({{"api", "GetRawInputDeviceInfoW"}, {"stage", stage}, {"win32Error", error}});
        unknown("preparsed-data-api-error");
    };
    const std::string role = rawRole(identity);
    if (role != "msi-dinput-main-collection" && role != "msi-xinput-game-collection") {
        unknown("unqualified-role"); return result;
    }
    if (!rawDevice || rawDevice == INVALID_HANDLE_VALUE || !data || !size || size > maxReportBytes) {
        unknown("invalid-report-arguments", size > maxReportBytes); return result;
    }
    UINT bytes = 0;
    const UINT queried = GetRawInputDeviceInfoW(rawDevice, RIDI_PREPARSEDDATA, nullptr, &bytes);
    if (queried == UINT(-1)) { const DWORD error = GetLastError(); winError("size", error); return result; }
    if (queried != 0 || !bytes || bytes > maxPreparsedBytes) {
        unknown("preparsed-data-size-invalid", bytes > maxPreparsedBytes); return result;
    }
    // RIDI_PREPARSEDDATA requires DWORD alignment. No descriptor/raw payload is logged.
    std::vector<DWORD> storage((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
    UINT capacity = bytes;
    const UINT copied = GetRawInputDeviceInfoW(rawDevice, RIDI_PREPARSEDDATA, storage.data(), &capacity);
    if (copied == UINT(-1)) { const DWORD error = GetLastError(); winError("data", error); return result; }
    if (copied != bytes || capacity != bytes) { unknown("preparsed-data-size-changed"); return result; }
    auto ppd = reinterpret_cast<PHIDP_PREPARSED_DATA>(storage.data());
    HIDP_CAPS caps{};
    NTSTATUS status = HidP_GetCaps(ppd, &caps);
    if (status != HIDP_STATUS_SUCCESS) { hidError("HidP_GetCaps", status); return result; }
    if (caps.UsagePage != identity.usagePage || caps.Usage != identity.usage) {
        unknown("descriptor-identity-mismatch"); return result;
    }
    if (!caps.InputReportByteLength || caps.InputReportByteLength > maxReportBytes) {
        unknown("descriptor-report-length-invalid", caps.InputReportByteLength > maxReportBytes); return result;
    }
    result["inputReportByteLength"] = caps.InputReportByteLength;
    result["buttonCapsDeclared"] = caps.NumberInputButtonCaps;
    result["valueCapsDeclared"] = caps.NumberInputValueCaps;
    if (caps.NumberInputButtonCaps > maxCaps) unknown("button-caps-limit", true);
    if (caps.NumberInputValueCaps > maxCaps) unknown("value-caps-limit", true);
    if (result["limitsExceeded"].get<bool>()) return result;

    std::array<HIDP_BUTTON_CAPS, maxCaps> buttonCaps{};
    std::array<HIDP_VALUE_CAPS, maxCaps> valueCaps{};
    USHORT buttonCount = caps.NumberInputButtonCaps, valueCount = caps.NumberInputValueCaps;
    bool buttonsValid = true, valuesValid = true;
    if (buttonCount) {
        status = HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCount, ppd);
        if (status != HIDP_STATUS_SUCCESS) { hidError("HidP_GetButtonCaps", status); buttonsValid = false; }
        if (buttonCount != caps.NumberInputButtonCaps) { unknown("button-caps-count-mismatch", buttonCount > maxCaps); buttonsValid = false; }
    }
    if (valueCount) {
        status = HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCount, ppd);
        if (status != HIDP_STATUS_SUCCESS) { hidError("HidP_GetValueCaps", status); valuesValid = false; }
        if (valueCount != caps.NumberInputValueCaps) { unknown("value-caps-count-mismatch", valueCount > maxCaps); valuesValid = false; }
    }
    if (!buttonsValid) buttonCount = 0;
    if (!valuesValid) valueCount = 0;
    std::array<bool, 256> ids{};
    for (unsigned i = 0; i < buttonCount; ++i) ids[buttonCaps[i].ReportID] = true;
    for (unsigned i = 0; i < valueCount; ++i) ids[valueCaps[i].ReportID] = true;
    const bool numbered = std::any_of(ids.begin() + 1, ids.end(), [](bool present) { return present; });
    if (ids[0] && numbered) { unknown("mixed-zero-and-numbered-report-ids"); return result; }

    // Preserve the received bytes exactly: even ID-zero input must already have
    // the descriptor's full report length and its actual leading report-ID byte.
    if (size != caps.InputReportByteLength) { unknown("report-length-mismatch"); return result; }
    const unsigned reportID = data[0];
    if (!ids[reportID]) { unknown("report-id-unknown"); return result; }
    result["reportID"] = reportID;
    // HidP has a mutable signature. A private, identical copy protects the caller.
    std::vector<char> report(size);
    std::memcpy(report.data(), data, size);

    // Expand bounded button declarations before querying. GetUsages is a query
    // for the entire page/link group, not for one cap or usage range.
    struct Button { unsigned link, usage; };
    std::vector<Button> buttons;
    std::set<std::pair<unsigned, unsigned>> buttonKeys;
    std::set<unsigned> unsafeButtonLinks;
    for (unsigned i = 0; i < buttonCount; ++i) {
        const auto& c = buttonCaps[i];
        if (c.ReportID != reportID) continue;
        if (c.UsagePage != 9) { unknown("unsupported-button-page"); continue; }
        if (c.IsAlias) { unknown("unsupported-button-alias"); unsafeButtonLinks.insert(c.LinkCollection); continue; }
        if (buttonArray(c)) { unknown("unsupported-button-array"); unsafeButtonLinks.insert(c.LinkCollection); continue; }
        const unsigned first = firstUsage(c), last = lastUsage(c);
        if (!first || last < first) { unknown("invalid-button-usage-range"); continue; }
        if (last - first + 1 > maxButtons - buttons.size()) { unknown("button-fields-limit", true); continue; }
        for (unsigned usage = first; usage <= last; ++usage) {
            if (!buttonKeys.emplace(c.LinkCollection, usage).second) { unknown("ambiguous-button-usage"); continue; }
            buttons.push_back({c.LinkCollection, usage});
        }
    }
    std::set<unsigned> links;
    for (const auto& b : buttons) links.insert(b.link);
    for (unsigned link : links) {
        // Link zero searches all collections: overlapping nested declarations
        // cannot establish the state of a button declared directly at link zero.
        bool nestedButtons = false;
        if (!link) for (unsigned i = 0; i < buttonCount; ++i) {
            const auto& c = buttonCaps[i];
            if (c.ReportID == reportID && c.UsagePage == 9 && c.LinkCollection != 0) nestedButtons = true;
        }
        if (nestedButtons) { unknown("ambiguous-top-level-button-query"); continue; }
        if (unsafeButtonLinks.count(link)) { unknown("ambiguous-button-group"); continue; }
        std::array<USAGE, maxButtons> down{};
        ULONG count = maxButtons;
        status = HidP_GetUsages(HidP_Input, 9, static_cast<USHORT>(link), down.data(), &count, ppd,
            report.data(), static_cast<ULONG>(report.size()));
        if (status != HIDP_STATUS_SUCCESS) {
            hidError("HidP_GetUsages", status);
            if (status == HIDP_STATUS_BUFFER_TOO_SMALL) unknown("button-usages-limit", true);
            continue;
        }
        if (count > maxButtons) { unknown("button-usages-limit", true); continue; }
        bool valid = true;
        std::set<unsigned> active;
        for (unsigned n = 0; n < count; ++n) {
            if (!buttonKeys.count({link, down[n]}) || !active.insert(down[n]).second) valid = false;
        }
        if (!valid) { unknown("button-usages-not-declared-or-ambiguous"); continue; }
        for (const auto& b : buttons) if (b.link == link) {
            json f = field(reportID, b.link, 9, b.usage);
            const unsigned pressed = active.count(b.usage) ? 1 : 0;
            // Button caps expose no logical limits, BitSize, or HasNull. Leave
            // these unknown; rawUnsigned here is API usage presence, not wire bits.
            f["valueSource"] = "HidP_GetUsages-presence";
            f["rawUnsigned"] = pressed;
            f["signedValue"] = pressed;
            f["state"] = "decoded";
            result["buttons"].push_back(f);
            if (pressed) result["buttonsDown"].push_back(f);
            result["decodedControlsKnown"] = true;
        }
    }

    std::set<std::tuple<unsigned, unsigned, unsigned>> valueKeys;
    for (unsigned i = 0; i < valueCount; ++i) {
        const auto& c = valueCaps[i];
        if (c.ReportID != reportID) continue;
        const unsigned first = firstUsage(c), last = lastUsage(c);
        if (last < first) { unknown("invalid-value-usage-range"); continue; }
        if (c.UsagePage != 1 && c.UsagePage != 2) { unknown("unsupported-value-page"); continue; }
        if (c.IsAlias) { unknown("unsupported-value-alias"); continue; }
        if (c.ReportCount != 1) { unknown("unsupported-value-array"); continue; }
        if (!c.BitSize || c.BitSize > 32 || c.LogicalMin > c.LogicalMax) {
            unknown("unsupported-value-metadata"); continue;
        }
        if (last - first + 1 > maxValues - result["values"].size()) { unknown("value-fields-limit", true); continue; }
        for (unsigned usage = first; usage <= last; ++usage) {
            if (!axisUsage(c.UsagePage, usage)) { unknown("unsupported-value-usage"); continue; }
            if (!valueKeys.emplace(c.LinkCollection, c.UsagePage, usage).second) { unknown("ambiguous-value-usage"); continue; }
            json f = field(reportID, c.LinkCollection, c.UsagePage, usage);
            f["logicalMin"] = c.LogicalMin; f["logicalMax"] = c.LogicalMax;
            f["bitSize"] = c.BitSize; f["HasNull"] = c.HasNull != FALSE;
            f["isSigned"] = c.LogicalMin < 0; f["isAbsolute"] = c.IsAbsolute != FALSE;
            f["isNull"] = nullptr;
            // A zero link queries across collections. Also reject duplicate
            // declarations that cannot be distinguished by this API's key.
            bool ambiguous = false;
            for (unsigned n = 0; n < valueCount; ++n) if (n != i) {
                const auto& other = valueCaps[n];
                if (other.ReportID == reportID && other.UsagePage == c.UsagePage &&
                    firstUsage(other) <= usage && usage <= lastUsage(other) &&
                    (other.LinkCollection == c.LinkCollection || c.LinkCollection == 0)) ambiguous = true;
            }
            if (ambiguous) {
                unknown("ambiguous-value-usage");
                result["values"].push_back(f);
                continue;
            }
            ULONG raw = 0;
            status = HidP_GetUsageValue(HidP_Input, c.UsagePage, c.LinkCollection, static_cast<USAGE>(usage),
                &raw, ppd, report.data(), static_cast<ULONG>(report.size()));
            if (status != HIDP_STATUS_SUCCESS) {
                hidError("HidP_GetUsageValue", status);
                auto& error = result["apiErrors"].back();
                error["reportID"] = reportID; error["linkCollection"] = c.LinkCollection;
                error["usagePage"] = c.UsagePage; error["usage"] = usage;
                f["status"] = static_cast<int64_t>(status);
            } else {
                const uint64_t modulus = uint64_t(1) << c.BitSize;
                const uint64_t bits = uint64_t(raw) & (modulus - 1);
                const int64_t signedValue = bits & (modulus >> 1) ? int64_t(bits) - int64_t(modulus) : int64_t(bits);
                const int64_t interpreted = c.LogicalMin < 0 ? signedValue : int64_t(bits);
                f["rawUnsigned"] = raw; f["signedValue"] = signedValue;
                f["interpretedValue"] = interpreted;
                const bool outside = interpreted < c.LogicalMin || interpreted > c.LogicalMax;
                if (uint64_t(raw) != bits) {
                    unknown("value-exceeds-bit-size");
                } else if (outside && !c.HasNull) {
                    unknown("value-outside-logical-range");
                } else {
                    f["isNull"] = outside;
                    f["state"] = outside ? "null" : "decoded";
                    f["controlValueKnown"] = !outside;
                    result["decodedControlsKnown"] = true;
                }
            }
            result["values"].push_back(f);
        }
    }
    const bool known = result["decodedControlsKnown"].get<bool>();
    if (!known && result["unknown"].empty()) unknown("no-supported-fields-in-report");
    const bool complete = known && result["unknown"].empty();
    result["decodeComplete"] = complete;
    result["state"] = complete ? "decoded" : known ? "partial" : "unknown";
    return result;
}
}






