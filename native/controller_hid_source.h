#pragma once

// R2 candidate: identical public API to the preserved frozen header. Ignore
// non-semantic cap metadata absent from historical Win5 capture. Select ONE
// header epoch per target; do not include both definitions in one program.

#include <array>
#include <cstdint>
#include <span>

#if defined(_WIN32)
#include <windows.h>
#include <Xinput.h>
#endif

namespace ymcc::controllerhid {

inline constexpr char inputSource[] = "xinput-hid";

#if defined(_WIN32)
using Gamepad = XINPUT_GAMEPAD;
#else
// XINPUT_GAMEPAD field names and widths; no Windows calls or SDK needed here.
struct Gamepad {
    std::uint16_t wButtons = 0;
    std::uint8_t bLeftTrigger = 0, bRightTrigger = 0;
    std::int16_t sThumbLX = 0, sThumbLY = 0, sThumbRX = 0, sThumbRY = 0;
};
#endif

// Copy every input cap, including caps that appear irrelevant. Never filter,
// clamp counts, truncate, mask logical maxima, or substitute default ranges.
// For NotRange copy Usage/DataIndex to BOTH first and last members.
struct ValueCaps {
    std::uint32_t page = 0, usageFirst = 0, usageLast = 0;
    std::uint32_t dataFirst = 0, dataLast = 0;
    std::uint32_t linkCollection = 0, linkUsagePage = 0, linkUsage = 0;
    std::uint32_t reportId = 0, bitSize = 0, reportCount = 0, bitField = 0;
    std::int32_t logicalMin = 0, logicalMax = 0;
    bool isRange = false, isAbsolute = false, isAlias = false, hasNull = false;
};

struct ButtonCaps {
    std::uint32_t page = 0, usageFirst = 0, usageLast = 0;
    std::uint32_t dataFirst = 0, dataLast = 0;
    std::uint32_t linkCollection = 0, linkUsagePage = 0, linkUsage = 0;
    std::uint32_t reportId = 0, bitField = 0;
    bool isRange = false, isAbsolute = false, isAlias = false;
};

// A borrowed view of one current physical top-level collection. Caller owns
// backing arrays and verifies successful HIDP_CAPS / ALL input cap calls.
// Structural qualification does not authorize a physical source. Capture must
// separately bind this collection to current xusb22 + XInput subtype GAMEPAD.
struct Descriptor {
    std::uint32_t vendor = 0, product = 0, page = 0, usage = 0;
    // linkCollections == 0 means not recorded in historical evidence. A
    // nonzero declared count bounds every referenced collection. It need not
    // equal four: empty/non-input collections cannot change these input data.
    std::uint32_t reportBytes = 0, dataIndices = 0, linkCollections = 0;
    std::uint32_t declaredValueCaps = 0, declaredButtonCaps = 0;
    std::span<const ValueCaps> values{};
    std::span<const ButtonCaps> buttons{};
};

// One HIDP_DATA: RawValue is meaningful only for a value; On only for a
// button. Omitted buttons are UP after a SUCCESSFUL complete HidP_GetData.
struct DataPoint {
    std::uint32_t index = 0, value = 0;
    bool down = false;
};

using ValueCap = ValueCaps;
using ButtonCap = ButtonCaps;
using Datum = DataPoint;

// Exactly one complete input report parsed using this Descriptor's preparsed
// data. bytes includes report-ID byte 0. Do not concatenate reportCount > 1.
struct Report {
    std::uint32_t bytes = 0, reportId = 0;
    std::span<const DataPoint> data{};
};

enum class Profile : std::uint8_t {
    Unknown,
    XusbCombinedZ10Buttons,
    XusbCombinedZ16Buttons
};

enum class TriggerMode : std::uint8_t { Unavailable, CombinedZPartial };

struct FieldValidity {
    bool lx = false, ly = false, rx = false, ry = false;
    bool hat = false, buttons = false, lt = false, rt = false;
};

struct Frame {
    Gamepad pad{};
    bool independentTriggers = false;
    bool completeBasic = false;
    FieldValidity valid{};
    TriggerMode triggerMode = TriggerMode::Unavailable;
    std::uint32_t combinedTriggerRaw = 0;
    // All declared button usages, including known ROG usages 11..16. Only
    // usages 1..10 have a proved Xbox gamepad mapping; preserve the rest here.
    std::uint16_t buttonUsages = 0;
};

enum class DecodeStatus : std::uint8_t {
    PartialCombinedTrigger,
    RejectedDescriptor,
    RejectedReport,
    RejectedData,
    MissingValue,
    OutOfRange
};

[[nodiscard]] inline Profile profile(const Descriptor& d) noexcept;
[[nodiscard]] inline Profile qualify(const Descriptor& d) noexcept;
[[nodiscard]] inline bool qualified(const Descriptor& d) noexcept;
// Transactional: on ANY failure, out (including its validity) is unchanged.
[[nodiscard]] inline DecodeStatus decode(const Descriptor& d, const Report& report,
                                         Frame& out) noexcept;
[[nodiscard]] inline bool decoded(DecodeStatus result) noexcept {
    return result == DecodeStatus::PartialCombinedTrigger;
}

namespace detail {

// Semantic roles, NOT HID data indices or link collection numbers.
enum Role : unsigned { X, Y, Rx, Ry, Z, Hat, RoleCount };

inline int role(std::uint32_t usage) noexcept {
    switch (usage) {
    case 0x30: return X;
    case 0x31: return Y;
    case 0x33: return Rx;
    case 0x34: return Ry;
    case 0x32: return Z;
    case 0x39: return Hat;
    default: return -1;
    }
}

struct Layout {
    Profile profile = Profile::Unknown;
    std::array<const ValueCap*, RoleCount> axes{};
    const ButtonCap* buttons = nullptr;
};

inline bool auditedLayout(const Descriptor& d, Layout& out) noexcept {
    // The full known XUSB subtype GAMEPAD shapes. VID/PID are diagnostic;
    // caller proves current physical xusb22/subtype separately. Other XUSB
    // subtypes (including flight sticks with Z/Rz) do NOT qualify here.
    if (d.page != 1 || d.usage != 5 || d.linkCollections > 65535 ||
        d.declaredValueCaps != RoleCount || d.values.size() != RoleCount ||
        d.declaredButtonCaps != 1 || d.buttons.size() != 1) return false;

    const auto& b = d.buttons.front();
    if (b.page != 9 || b.reportId != 0 || !b.isRange || !b.isAbsolute ||
        b.isAlias || b.linkCollection != 0 || b.usageFirst != 1 ||
        b.dataFirst > b.dataLast || b.usageLast < b.usageFirst) return false;

    Profile selected = Profile::Unknown;
    if (b.usageLast == 10 && d.reportBytes == 15 && d.dataIndices == 16)
        selected = Profile::XusbCombinedZ10Buttons;
    else if (b.usageLast == 16 && d.reportBytes == 16 && d.dataIndices == 22)
        selected = Profile::XusbCombinedZ16Buttons;
    else return false;
    if (b.dataLast >= d.dataIndices || b.dataLast - b.dataFirst != b.usageLast - 1)
        return false;

    Layout layout;
    layout.profile = selected;
    layout.buttons = &b;
    std::uint32_t seen = 0;
    for (std::uint32_t index = b.dataFirst; index <= b.dataLast; ++index)
        seen |= std::uint32_t{1} << index;

    for (const auto& c : d.values) {
        const int semantic = role(c.usageFirst);
        if (c.page != 1 || semantic < 0 || c.usageFirst != c.usageLast ||
            c.isRange || !c.isAbsolute || c.isAlias || c.reportCount != 1 ||
            c.reportId != 0 || c.dataFirst != c.dataLast ||
            c.dataFirst >= d.dataIndices || c.linkCollection > 65535 ||
            (d.linkCollections != 0 && c.linkCollection >= d.linkCollections))
            return false;
        const auto r = static_cast<unsigned>(semantic);
        const auto bit = std::uint32_t{1} << c.dataFirst;
        if (layout.axes[r] || (seen & bit)) return false;
        seen |= bit;
        if (r == Hat) {
            // Observed XUSB gamepad null is 0, directions 1..8. Metadata such
            // as HasNull/BitField/LinkUsage was absent in Win5's historical
            // capture and is not an additional qualification gate. The report
            // domain remains exactly 0..8; other values are still rejected.
            if (c.bitSize != 4 || c.logicalMin != 1 || c.logicalMax != 8 ||
                c.linkCollection != 0) return false;
        } else {
            // Exactly the known unsigned 16-bit logical range. -1 is the
            // observed HIDP representation of 0xffff, ONLY for this shape.
            // Never mask arbitrary maxima (e.g. 0x1ffff) down to 16 bits.
            if (c.bitSize != 16 || c.logicalMin != 0 ||
                (c.logicalMax != -1 && c.logicalMax != 65535) ||
                c.linkCollection == 0) return false;
        }
        layout.axes[r] = &c;
    }

    for (const auto* c : layout.axes) if (!c) return false;
    // Audit complete index coverage, including the ROG extra six buttons.
    if (seen != ((std::uint32_t{1} << d.dataIndices) - 1)) return false;

    // Preserve actual collection relationships, allowing collection numbers
    // to be permuted. Never borrow X/Y from different physical collections.
    const auto left = layout.axes[X]->linkCollection;
    const auto right = layout.axes[Rx]->linkCollection;
    const auto trigger = layout.axes[Z]->linkCollection;
    if (left != layout.axes[Y]->linkCollection ||
        right != layout.axes[Ry]->linkCollection ||
        left == right || left == trigger || right == trigger) return false;

    out = layout;
    return true;
}

inline std::int16_t stick(std::uint32_t raw, bool invert) noexcept {
    const auto v = static_cast<std::int32_t>(raw) - 32768;
    const auto scaled = invert ? (v == -32768 ? 32767 : -v) : v;
    return static_cast<std::int16_t>(scaled);
}

inline constexpr std::array<std::uint16_t, 10> buttonMasks{
    0x1000, 0x2000, 0x4000, 0x8000, // A B X Y
    0x0100, 0x0200, 0x0020, 0x0010, // LB RB Back Start
    0x0040, 0x0080                    // LS RS
};
inline constexpr std::array<std::uint16_t, 9> hats{
    0, 0x0001, 0x0009, 0x0008, 0x000a, 0x0002, 0x0006, 0x0004, 0x0005
};

} // namespace detail

[[nodiscard]] inline Profile qualify(const Descriptor& d) noexcept {
    detail::Layout layout;
    return detail::auditedLayout(d, layout) ? layout.profile : Profile::Unknown;
}

[[nodiscard]] inline Profile profile(const Descriptor& d) noexcept {
    return qualify(d);
}

[[nodiscard]] inline bool qualified(const Descriptor& d) noexcept {
    return qualify(d) != Profile::Unknown;
}

[[nodiscard]] inline DecodeStatus decode(const Descriptor& d, const Report& report,
                                         Frame& out) noexcept {
    detail::Layout layout;
    if (!detail::auditedLayout(d, layout)) return DecodeStatus::RejectedDescriptor;
    if (report.bytes != d.reportBytes || report.reportId != 0)
        return DecodeStatus::RejectedReport;
    if (report.data.size() > d.dataIndices) return DecodeStatus::RejectedData;

    Frame frame;
    std::array<std::uint32_t, detail::RoleCount> values{};
    std::uint32_t seen = 0, haveValues = 0;
    const auto& b = *layout.buttons;
    for (const auto& item : report.data) {
        if (item.index >= d.dataIndices) return DecodeStatus::RejectedData;
        const auto bit = std::uint32_t{1} << item.index;
        if (seen & bit) return DecodeStatus::RejectedData;
        seen |= bit;
        if (item.index >= b.dataFirst && item.index <= b.dataLast) {
            if (item.down) {
                const auto usage = b.usageFirst + item.index - b.dataFirst;
                frame.buttonUsages = static_cast<std::uint16_t>(frame.buttonUsages |
                    (std::uint32_t{1} << (usage - 1)));
                if (usage <= detail::buttonMasks.size())
                    frame.pad.wButtons = static_cast<std::uint16_t>(frame.pad.wButtons |
                        detail::buttonMasks[usage - 1]);
            }
            continue;
        }
        bool found = false;
        for (unsigned r = 0; r < detail::RoleCount; ++r) {
            if (layout.axes[r]->dataFirst != item.index) continue;
            if (item.value > (r == detail::Hat ? 8u : 65535u))
                return DecodeStatus::OutOfRange;
            values[r] = item.value;
            haveValues |= std::uint32_t{1} << r;
            found = true;
            break;
        }
        if (!found) return DecodeStatus::RejectedData;
    }
    // HidP_GetData omits UP buttons, but never omit any required value (even Z).
    if (haveValues != ((std::uint32_t{1} << detail::RoleCount) - 1))
        return DecodeStatus::MissingValue;

    frame.pad.sThumbLX = detail::stick(values[detail::X], false);
    frame.pad.sThumbLY = detail::stick(values[detail::Y], true);
    frame.pad.sThumbRX = detail::stick(values[detail::Rx], false);
    frame.pad.sThumbRY = detail::stick(values[detail::Ry], true);
    frame.pad.wButtons = static_cast<std::uint16_t>(frame.pad.wButtons |
        detail::hats[values[detail::Hat]]);
    frame.completeBasic = true;
    frame.valid = {true, true, true, true, true, true, false, false};
    frame.triggerMode = TriggerMode::CombinedZPartial;
    frame.combinedTriggerRaw = values[detail::Z];
    // LT/RT remain zero AND invalid. Parent preserves native LT/RT; do not
    // present these zeros as proved releases or infer two triggers from Z.
    frame.independentTriggers = false;
    out = frame;
    return DecodeStatus::PartialCombinedTrigger;
}

// No held state, epoch, ownership, timer, transport, device API or BUS owner.
// Parent owns power/topology/identity/occupant/slot and rejects stale reports.

} // namespace ymcc::controllerhid
