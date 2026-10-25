#if defined(YMCC_MSI_GAMEPAD_DECODE_COMPILE_ONLY)
// This is the FIRST include, without a PCH, Windows adapter, hidsdi.h, or SDK
// definitions supplied by the includer. Compile only; never link or execute.
#include "../native/msi_external_gamepad_decode_windows.h"
nlohmann::json compileGamepadDecode(HANDLE h, const ymcc::msi::external::RawIdentity& id,
    const unsigned char* data, unsigned size) {
    return ymcc::msi::external::decodeGamepadReceipt(h, id, data, size);
}
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <hidusage.h>
#include <hidpi.h>
#include <json.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fixture {
using nlohmann::json;
struct ValueResult { NTSTATUS status = HIDP_STATUS_SUCCESS; ULONG raw = 127; };
struct ButtonResult { NTSTATUS status = HIDP_STATUS_SUCCESS; std::vector<USAGE> down; ULONG reported = 0; };
static HIDP_CAPS caps{};
static std::vector<HIDP_BUTTON_CAPS> buttons;
static std::vector<HIDP_VALUE_CAPS> values;
static std::map<std::tuple<unsigned, unsigned, unsigned>, ValueResult> valueResults;
static std::map<unsigned, ButtonResult> buttonResults;
static NTSTATUS capsStatus, buttonStatus, valueStatus;
static UINT ppdBytes, copiedBytes, capacityBytes;
static int metadataFailure, buttonCountOverride, valueCountOverride;
static DWORD lastError;
static unsigned infoCalls, capsCalls, buttonCapsCalls, valueCapsCalls, usagesCalls, valueCalls, boundaryViolations;
static HANDLE fixtureRawHandle = reinterpret_cast<HANDLE>(uintptr_t(0xabc));
static std::vector<unsigned char> expectedReport;
static bool mutateReport;
static UINT WINAPI rawInfo(HANDLE h, UINT command, LPVOID out, PUINT size) {
    ++infoCalls;
    if (h != fixtureRawHandle || command != RIDI_PREPARSEDDATA || !size) ++boundaryViolations;
    if (metadataFailure == (out ? 2 : 1)) { lastError = ERROR_ACCESS_DENIED; return UINT(-1); }
    if (!out) { *size = ppdBytes; return 0; }
    if (*size != ppdBytes || reinterpret_cast<uintptr_t>(out) % alignof(DWORD)) ++boundaryViolations;
    std::memset(out, 0, *size);
    *size = capacityBytes;
    return copiedBytes;
}
static DWORD WINAPI getLastError() { return lastError; }
static NTSTATUS __stdcall getCaps(PHIDP_PREPARSED_DATA ppd, PHIDP_CAPS out) {
    ++capsCalls;
    if (!ppd) ++boundaryViolations;
    if (capsStatus == HIDP_STATUS_SUCCESS) *out = caps;
    return capsStatus;
}
static NTSTATUS __stdcall getButtons(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS out, PUSHORT count, PHIDP_PREPARSED_DATA ppd) {
    ++buttonCapsCalls;
    if (type != HidP_Input || !ppd || *count > 32 || *count < buttons.size()) ++boundaryViolations;
    if (buttonStatus == HIDP_STATUS_SUCCESS && buttons.size() <= *count) std::copy(buttons.begin(), buttons.end(), out);
    if (buttonCountOverride >= 0) *count = static_cast<USHORT>(buttonCountOverride);
    return buttonStatus;
}
static NTSTATUS __stdcall getValues(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS out, PUSHORT count, PHIDP_PREPARSED_DATA ppd) {
    ++valueCapsCalls;
    if (type != HidP_Input || !ppd || *count > 32 || *count < values.size()) ++boundaryViolations;
    if (valueStatus == HIDP_STATUS_SUCCESS && values.size() <= *count) std::copy(values.begin(), values.end(), out);
    if (valueCountOverride >= 0) *count = static_cast<USHORT>(valueCountOverride);
    return valueStatus;
}
static void reportBoundary(HIDP_REPORT_TYPE type, PHIDP_PREPARSED_DATA ppd, PCHAR report, ULONG length) {
    if (type != HidP_Input || !ppd || length != caps.InputReportByteLength ||
        expectedReport.size() != length || std::memcmp(report, expectedReport.data(), length)) ++boundaryViolations;
    if (mutateReport && length) report[length - 1] ^= 1;
}
static NTSTATUS __stdcall getUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link, PUSAGE out,
    PULONG count, PHIDP_PREPARSED_DATA ppd, PCHAR report, ULONG length) {
    ++usagesCalls;
    reportBoundary(type, ppd, report, length);
    if (page != 9 || *count != 128) ++boundaryViolations;
    const auto found = buttonResults.find(link);
    const ButtonResult r = found == buttonResults.end() ? ButtonResult{} : found->second;
    if (r.status == HIDP_STATUS_SUCCESS) {
        if (r.down.size() <= *count) std::copy(r.down.begin(), r.down.end(), out);
        *count = r.reported ? r.reported : static_cast<ULONG>(r.down.size());
    }
    return r.status;
}
static NTSTATUS __stdcall getValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link, USAGE usage,
    PULONG out, PHIDP_PREPARSED_DATA ppd, PCHAR report, ULONG length) {
    ++valueCalls;
    reportBoundary(type, ppd, report, length);
    if (!((page == 1 && usage >= 0x30 && usage <= 0x39) ||
        (page == 2 && (usage == 0xba || usage == 0xbb || usage == 0xc4 || usage == 0xc5)))) ++boundaryViolations;
    const auto found = valueResults.find({page, link, usage});
    const ValueResult r = found == valueResults.end() ? ValueResult{} : found->second;
    if (r.status == HIDP_STATUS_SUCCESS) *out = r.raw;
    return r.status;
}
}
#define GetRawInputDeviceInfoW fixture::rawInfo
#define GetLastError fixture::getLastError
#define HidP_GetCaps fixture::getCaps
#define HidP_GetButtonCaps fixture::getButtons
#define HidP_GetValueCaps fixture::getValues
#define HidP_GetUsages fixture::getUsages
#define HidP_GetUsageValue fixture::getValue
#include "../native/msi_external_gamepad_decode_windows.h"
#undef GetRawInputDeviceInfoW
#undef GetLastError
#undef HidP_GetCaps
#undef HidP_GetButtonCaps
#undef HidP_GetValueCaps
#undef HidP_GetUsages
#undef HidP_GetUsageValue
using namespace ymcc::msi::external;
using namespace fixture;
static unsigned checks = 0, failures = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
static bool has(const json& r, const char* reason) {
    for (const auto& s : r["unknown"]) if (s == reason) return true;
    return false;
}
static RawIdentity identity() {
    return {true, true, 0x0db0, 0x1902, 1, 5, "\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01#fixture"};
}
static HIDP_VALUE_CAPS axis(unsigned usage = 0x30, unsigned id = 0, unsigned link = 1, unsigned page = 1) {
    HIDP_VALUE_CAPS c{};
    c.UsagePage = static_cast<USAGE>(page); c.ReportID = static_cast<UCHAR>(id);
    c.LinkCollection = static_cast<USHORT>(link); c.NotRange.Usage = static_cast<USAGE>(usage);
    c.BitSize = 8; c.ReportCount = 1; c.LogicalMin = 0; c.LogicalMax = 255; c.IsAbsolute = TRUE;
    return c;
}
static HIDP_BUTTON_CAPS button(unsigned first = 1, unsigned last = 4, unsigned id = 0, unsigned link = 1) {
    HIDP_BUTTON_CAPS c{};
    c.UsagePage = 9; c.ReportID = static_cast<UCHAR>(id); c.LinkCollection = static_cast<USHORT>(link);
    c.IsRange = TRUE; c.Range.UsageMin = static_cast<USAGE>(first); c.Range.UsageMax = static_cast<USAGE>(last);
    return c;
}
static void reset() {
    check(boundaryViolations == 0, "prior case used only bounded fake metadata/input-report APIs");
    caps = {}; caps.UsagePage = 1; caps.Usage = 5; caps.InputReportByteLength = 8;
    buttons.clear(); values = {axis()}; valueResults.clear(); buttonResults.clear();
    capsStatus = buttonStatus = valueStatus = HIDP_STATUS_SUCCESS;
    ppdBytes = copiedBytes = capacityBytes = 64;
    metadataFailure = 0; buttonCountOverride = valueCountOverride = -1;
    lastError = ERROR_SUCCESS;
    infoCalls = capsCalls = buttonCapsCalls = valueCapsCalls = usagesCalls = valueCalls = boundaryViolations = 0;
    expectedReport = {0, 0x71, 0x83, 0x95, 0xa7, 0xb9, 0xcb, 0xdd}; mutateReport = false;
}
static json decode() {
    caps.NumberInputButtonCaps = static_cast<USHORT>(buttons.size());
    caps.NumberInputValueCaps = static_cast<USHORT>(values.size());
    return decodeGamepadReceipt(fixtureRawHandle, identity(), expectedReport.data(), static_cast<unsigned>(expectedReport.size()));
}
static bool unknownControls(const json& r) {
    return r["state"] == "unknown" && r["decodedControlsKnown"] == false && r["decodeComplete"] == false;
}
int main() {
    reset();
    auto id = identity(); id.known = false;
    auto r = decodeGamepadReceipt(fixtureRawHandle, id, expectedReport.data(), 8);
    check(unknownControls(r) && infoCalls == 0, "unknown identity does not touch metadata");
    std::vector<RawIdentity> excluded;
    id = identity(); id.hid = false; excluded.push_back(id);
    id = identity(); id.vid = 0x1234; excluded.push_back(id);
    id = identity(); id.path = "\\\\?\\hid#vid_0db0&pid_1902&mi_00&col02#fixture"; id.usagePage = 0xfff0; id.usage = 0x40; excluded.push_back(id);
    id = identity(); id.path = "\\\\?\\hid#vid_0db0&pid_1902&mi_01&col03#fixture"; id.usagePage = 12; id.usage = 1; excluded.push_back(id);
    id = identity(); id.usage = 6; excluded.push_back(id);
    id = identity(); id.path = "bad-path"; excluded.push_back(id);
    id = {true,true,0x0db0,0x1901,0xffa0,1,"\\\\?\\hid#vid_0db0&pid_1901&mi_01#fixture"}; excluded.push_back(id);
    for (const auto& excludedId : excluded) {
        r = decodeGamepadReceipt(fixtureRawHandle, excludedId, expectedReport.data(), 8);
        check(unknownControls(r) && infoCalls == 0, "excluded HID roles cannot query preparsed data");
    }
    for (HANDLE h : {HANDLE(nullptr), INVALID_HANDLE_VALUE}) {
        r = decodeGamepadReceipt(h, identity(), expectedReport.data(), 8);
        check(unknownControls(r) && infoCalls == 0, "invalid raw handle short circuits");
    }
    r = decodeGamepadReceipt(fixtureRawHandle, identity(), nullptr, 8);
    check(unknownControls(r) && infoCalls == 0, "null report short circuits");
    for (unsigned size : {0u, 4097u, 0xffffffffu}) {
        r = decodeGamepadReceipt(fixtureRawHandle, identity(), expectedReport.data(), size);
        check(unknownControls(r) && infoCalls == 0, "zero/oversized report never reads data or metadata");
    }
    for (int stage : {1, 2}) {
        reset(); metadataFailure = stage; r = decode();
        check(unknownControls(r) && r["apiErrors"][0]["win32Error"] == ERROR_ACCESS_DENIED && capsCalls == 0,
            "metadata API failure preserves GetLastError");
    }
    for (unsigned n : {0u, 65537u}) {
        reset(); ppdBytes = n; r = decode();
        check(unknownControls(r) && infoCalls == 1 && capsCalls == 0, "missing/oversized metadata bounded before allocation/read");
    }
    reset(); ppdBytes = copiedBytes = capacityBytes = 65536; r = decode();
    check(r["decodeComplete"] == true, "exact 64KiB PPD limit accepted");
    for (unsigned copied : {0u, 63u, 65u}) {
        reset(); copiedBytes = copied; r = decode();
        check(unknownControls(r) && capsCalls == 0, "truncated/changed PPD read not passed to HidP");
    }
    reset(); capacityBytes = 63; r = decode();
    check(unknownControls(r) && capsCalls == 0, "PPD capacity race remains unknown");
    reset(); capsStatus = HIDP_STATUS_INVALID_PREPARSED_DATA; r = decode();
    check(unknownControls(r) && r["apiErrors"][0]["status"] == int64_t(capsStatus) && valueCapsCalls == 0,
        "GetCaps failure preserves NTSTATUS");
    for (int kind : {0, 1}) {
        reset(); if (kind) caps.Usage = 4; else caps.UsagePage = 2; r = decode();
        check(unknownControls(r) && valueCapsCalls == 0 && has(r,"descriptor-identity-mismatch"), "descriptor page/usage cross validation");
    }
    for (unsigned n : {0u, 4097u}) {
        reset(); caps.InputReportByteLength = static_cast<USHORT>(n); r = decode();
        check(unknownControls(r) && valueCalls == 0, "invalid descriptor report length");
    }
    for (unsigned n : {6u, 9u}) {
        reset(); expectedReport.resize(n); r = decode();
        check(unknownControls(r) && valueCalls == 0 && has(r,"report-length-mismatch"), "short/long report rejected before parser");
    }
    reset(); values[0].ReportID = 7; expectedReport[0] = 8; r = decode();
    check(unknownControls(r) && valueCalls == 0 && has(r,"report-id-unknown"), "unknown numbered report ID");
    reset(); expectedReport[0] = 0x71; r = decode();
    check(unknownControls(r) && valueCalls == 0, "same-length unnumbered report needs zero prefix");
    reset(); values[0].ReportID = 7; expectedReport[0] = 7; values.push_back(axis(0x31,8));
    values[1].ReportCount = 2; r = decode();
    check(r["decodeComplete"] == true && r["values"].size() == 1 && valueCalls == 1, "only current report determines completeness");
    reset(); values.push_back(axis(0x31,1)); r = decode();
    check(unknownControls(r) && valueCalls == 0 && has(r,"mixed-zero-and-numbered-report-ids"), "mixed zero/numbered descriptor rejected");
    reset(); const auto noId = std::vector<unsigned char>(expectedReport.begin()+1,expectedReport.end());
    caps.NumberInputValueCaps = 1;
    r = decodeGamepadReceipt(fixtureRawHandle,identity(),noId.data(),static_cast<unsigned>(noId.size()));
    check(unknownControls(r) && has(r,"report-length-mismatch") && valueCalls == 0 &&
        !r.contains("reportIdZeroPrepended"), "one-byte-short ID-zero report stays unknown without normalization");
    reset(); values[0].ReportID = 7; caps.NumberInputValueCaps = 1;
    r = decodeGamepadReceipt(fixtureRawHandle,identity(),noId.data(),static_cast<unsigned>(noId.size()));
    check(unknownControls(r) && valueCalls == 0, "one-byte-short numbered report stays unknown without normalization");
    reset(); values.clear(); r = decode();
    check(unknownControls(r) && valueCapsCalls == 0 && buttonCapsCalls == 0, "empty caps never establish controls");
    for (bool buttonOverflow : {false, true}) {
        reset(); if (buttonOverflow) buttons.assign(33,button()); else values.assign(33,axis()); r = decode();
        check(unknownControls(r) && r["limitsExceeded"] == true && valueCapsCalls == 0 && buttonCapsCalls == 0,
            "33 caps rejected without cap API call");
    }
    reset(); values.clear(); for (unsigned i = 0; i < 32; ++i) values.push_back(axis(0x30,0,i+1)); r = decode();
    check(r["decodeComplete"] == true && r["values"].size() == 32 && valueCalls == 32, "32 value caps/fields exact boundary");
    reset(); values.clear(); for (unsigned i = 0; i < 32; ++i) buttons.push_back(button(i+1,i+1)); r = decode();
    check(r["decodeComplete"] == true && r["buttons"].size() == 32 && usagesCalls == 1, "32 button caps share one bounded page/link query");
    for (bool buttonFailure : {false,true}) {
        reset(); buttons = {button()};
        if (buttonFailure) buttonStatus = HIDP_STATUS_INVALID_PREPARSED_DATA;
        else valueStatus = HIDP_STATUS_INVALID_PREPARSED_DATA;
        r = decode();
        check(r["state"] == "partial" && r["decodedControlsKnown"] == true && r["decodeComplete"] == false && r["apiErrors"].size() == 1,
            "one cap API failure retains other decoded fields as partial");
    }
    for (bool buttonCount : {false,true}) {
        reset(); buttons = {button()};
        if (buttonCount) buttonCountOverride = 33; else valueCountOverride = 33; r = decode();
        check(r["state"] == "partial" && r["limitsExceeded"] == true, "cap output count overflow bounded and explicit");
        reset(); buttons = {button()};
        if (buttonCount) buttonCountOverride = 0; else valueCountOverride = 0; r = decode();
        check(r["state"] == "partial" && r["decodeComplete"] == false, "cap truncation cannot establish completeness");
    }
    reset(); values[0].LogicalMin = -128; values[0].LogicalMax = 127; valueResults[{1,1,0x30}].raw = 0xfe; r = decode();
    check(r["decodeComplete"] == true && r["values"][0]["rawUnsigned"] == 254 &&
        r["values"][0]["signedValue"] == -2 && r["values"][0]["interpretedValue"] == -2, "signed 8-bit axis sign extension");
    reset(); valueResults[{1,1,0x30}].raw = 254; r = decode();
    check(r["values"][0]["interpretedValue"] == 254 && r["values"][0]["signedValue"] == -2 &&
        r["values"][0]["isSigned"] == false, "unsigned axis keeps descriptor interpretation separate from signed bits");
    reset(); values[0].BitSize = 32; values[0].LogicalMin = LONG(-2147483647-1); values[0].LogicalMax = 2147483647;
    valueResults[{1,1,0x30}].raw = 0x80000000; r = decode();
    check(r["decodeComplete"] == true && r["values"][0]["signedValue"] == int64_t(-2147483647)-1,
        "32-bit signed axis avoids shift overflow");
    reset(); values[0].HasNull = TRUE; values[0].LogicalMax = 7; valueResults[{1,1,0x30}].raw = 8; r = decode();
    check(r["decodeComplete"] == true && r["decodedControlsKnown"] == true && r["values"][0]["state"] == "null" &&
        r["values"][0]["controlValueKnown"] == false && r["values"][0]["rawUnsigned"] == 8, "declared null parsed without fabricating axis position");
    reset(); values[0].LogicalMax = 7; valueResults[{1,1,0x30}].raw = 8; r = decode();
    check(unknownControls(r) && has(r,"value-outside-logical-range"), "out-of-range without HasNull unknown");
    reset(); valueResults[{1,1,0x30}].raw = 256; r = decode();
    check(unknownControls(r) && has(r,"value-exceeds-bit-size"), "unexpected high bits not silently truncated");
    reset(); values.clear(); buttons = {button()}; buttonResults[1].down = {1,4}; r = decode();
    check(r["decodeComplete"] == true && r["buttonsDown"].size() == 2 && r["buttons"].size() == 4 && usagesCalls == 1,
        "buttons retain page9 usage/link/report metadata and known up fields");
    check(r["buttonsDown"][0]["usagePage"] == 9 && r["buttonsDown"][0]["usage"] == 1 &&
        r["buttonsDown"][0]["bitSize"].is_null(), "button metadata not invented from unavailable descriptor fields");
    reset(); values.clear(); buttons = {button()}; r = decode();
    check(r["decodedControlsKnown"] == true && r["decodeComplete"] == true && r["buttonsDown"].empty(),
        "successfully decoded empty pressed set remains distinguishable from unknown");
    for (unsigned n : {128u,129u}) {
        reset(); values.clear(); buttons = {button(1,n)}; r = decode();
        check(n == 128 ? r["decodeComplete"] == true && r["buttons"].size() == 128 :
            unknownControls(r) && r["limitsExceeded"] == true && usagesCalls == 0, "128 button fields limit");
    }
    reset(); buttons = {button(1,128),button(129,129)}; r = decode();
    check(r["state"] == "partial" && r["limitsExceeded"] == true && r["buttons"].size() == 128, "aggregate button cap expansion bounded");
    reset(); buttons = {button()}; buttonResults[1].reported = 129; r = decode();
    check(r["state"] == "partial" && r["limitsExceeded"] == true && r["buttons"].empty(), "fake success cannot overrun usage output buffer");
    reset(); buttons = {button()}; buttonResults[1].status = HIDP_STATUS_BUFFER_TOO_SMALL; r = decode();
    check(r["state"] == "partial" && r["limitsExceeded"] == true && r["apiErrors"].size() == 1, "GetUsages buffer-too-small explicit limit/API error");
    for (const auto& down : {std::vector<USAGE>{5},std::vector<USAGE>{1,1}}) {
        reset(); buttons = {button()}; buttonResults[1].down = down; r = decode();
        check(r["state"] == "partial" && r["buttons"].empty(), "undeclared/duplicate returned button usages unknown");
    }
    reset(); buttons = {button(1,1,0,0),button(1,1,0,2)}; buttonResults[2].down = {1}; r = decode();
    check(r["state"] == "partial" && usagesCalls == 1 && has(r,"ambiguous-top-level-button-query"), "link zero all-collection ambiguity guarded");
    reset(); values[0].IsAlias = TRUE; r = decode();
    check(unknownControls(r) && valueCalls == 0 && has(r,"unsupported-value-alias"), "value alias not decoded as an independent field");
    reset(); values.push_back(axis(0x31)); values[1].IsAlias = TRUE; r = decode();
    check(r["state"] == "partial" && valueCalls == 1, "unsupported alias lowers completeness without hiding scalar success");
    reset(); buttons = {button()}; buttons[0].IsAlias = TRUE; r = decode();
    check(r["state"] == "partial" && usagesCalls == 0 && has(r,"unsupported-button-alias"), "button alias never queried");
    reset(); buttons = {button()}; buttons[0].IsRange = FALSE; buttons[0].NotRange.Usage = 6;
    buttonResults[1].down = {6}; r = decode();
    check(r["decodeComplete"] == true && r["buttonsDown"][0]["usage"] == 6, "non-range button usage descriptor");
    reset(); buttons = {button()};
    // API-v2 field; the production helper also compiles against older SDKs.
    buttons[0].ReportCount = 2; r = decode();
    check(r["state"] == "partial" && usagesCalls == 0 && has(r,"unsupported-button-array"), "API-v2 button array conservatively unsupported");
    struct LegacyButtonCapsWithoutReportCount {};
    check(!gamepad_decode_detail::buttonArray(LegacyButtonCapsWithoutReportCount{}), "old SDK reserved button count does not require new API");
    reset(); values = {axis(0x30,0,0),axis(0x30,0,2)}; r = decode();
    check(r["state"] == "partial" && valueCalls == 1 && r["values"][0]["state"] == "unknown" && has(r,"ambiguous-value-usage"),
        "link-zero value lookup cannot borrow a nested collection's value");
    reset(); values[0].BitSize = 12; values[0].LogicalMin = -2048; values[0].LogicalMax = 2047;
    valueResults[{1,1,0x30}].raw = 0xffe; r = decode();
    check(r["decodeComplete"] == true && r["values"][0]["signedValue"] == -2, "12-bit signed axis uses BitSize instead of host width");
    reset(); values[0].HasNull = TRUE; values[0].LogicalMin = -4; values[0].LogicalMax = 3;
    valueResults[{1,1,0x30}].raw = 0x80; r = decode();
    check(r["decodeComplete"] == true && r["values"][0]["isNull"] == true && r["values"][0]["signedValue"] == -128,
        "signed HasNull compares the signed logical value");
    reset(); valueResults[{1,1,0x30}].status = HIDP_STATUS_NULL; r = decode();
    check(unknownControls(r) && r["values"][0]["rawUnsigned"].is_null() && r["apiErrors"][0]["status"] == int64_t(HIDP_STATUS_NULL),
        "non-success null status preserves API error without reading undefined output");
    reset(); values[0].ReportCount = 0; r = decode();
    check(unknownControls(r) && valueCalls == 0, "zero report count unsupported");
    reset(); buttons = {button(1,1,0,0),button(1,1,0,2)}; buttons[1].IsAlias = TRUE; r = decode();
    check(r["state"] == "partial" && usagesCalls == 0 && r["buttons"].empty() && has(r,"ambiguous-top-level-button-query"),
        "link-zero cannot borrow usage from an unsupported nested cap");
    reset(); buttons = {button(1,1),button(2,2)}; buttons[1].ReportCount = 2; r = decode();
    check(r["state"] == "partial" && usagesCalls == 0 && has(r,"ambiguous-button-group"),
        "unsupported array in same page/link group cannot establish scalar button state");
    reset(); values[0].ReportCount = 2; r = decode();
    check(unknownControls(r) && valueCalls == 0 && has(r,"unsupported-value-array"), "value array never decoded as first scalar");
    reset(); values.push_back(axis(0x31)); values[1].ReportCount = 2; r = decode();
    check(r["state"] == "partial" && valueCalls == 1, "unsupported array lowers completeness of current report");
    for (unsigned page : {7u,12u,0xff00u}) {
        reset(); values.push_back(axis(1,0,2,page)); buttons = {button()}; buttons[0].UsagePage = static_cast<USAGE>(page); r = decode();
        check(r["state"] == "partial" && valueCalls == 1 && usagesCalls == 0 && r["values"].size() == 1,
            "keyboard/consumer/vendor pages neither parsed nor retained");
    }
    reset(); values = {axis(0x2f)}; r = decode();
    check(unknownControls(r) && valueCalls == 0, "other generic desktop usage unsupported");
    reset(); values.clear(); for (unsigned usage = 0x30; usage <= 0x39; ++usage) values.push_back(axis(usage));
    for (unsigned usage : {0xbau,0xbbu,0xc4u,0xc5u}) values.push_back(axis(usage,0,2,2)); r = decode();
    check(r["decodeComplete"] == true && r["values"].size() == 14, "all admitted desktop/simulation axes");
    reset(); values[0].IsRange = TRUE; values[0].Range.UsageMin = 0x30; values[0].Range.UsageMax = 0x39; r = decode();
    check(r["decodeComplete"] == true && r["values"].size() == 10, "scalar value usage range expanded from descriptor");
    reset(); values.clear(); for (unsigned link = 1; link <= 4; ++link) {
        auto c = axis(0x30,0,link); c.IsRange = TRUE; c.Range.UsageMin = 0x30; c.Range.UsageMax = 0x39; values.push_back(c);
    } r = decode();
    check(r["state"] == "partial" && r["limitsExceeded"] == true && r["values"].size() == 30 && valueCalls == 30,
        "aggregate value expansion max32 explicitly reports omitted cap");
    reset(); values[0].IsRange = TRUE; values[0].Range.UsageMin = 0; values[0].Range.UsageMax = 65535; r = decode();
    check(unknownControls(r) && r["limitsExceeded"] == true && valueCalls == 0, "huge value range bounded without walking usages");
    for (unsigned bits : {0u,33u}) {
        reset(); values[0].BitSize = static_cast<USHORT>(bits); r = decode();
        check(unknownControls(r) && valueCalls == 0, "unsupported bit size not shifted or parsed");
    }
    reset(); values[0].LogicalMin = 5; values[0].LogicalMax = 4; r = decode();
    check(unknownControls(r) && valueCalls == 0, "reversed logical range unknown");
    reset(); values[0].IsRange = TRUE; values[0].Range.UsageMin = 0x31; values[0].Range.UsageMax = 0x30; r = decode();
    check(unknownControls(r) && valueCalls == 0, "reversed usage range unknown");
    reset(); buttons = {button(4,1)}; r = decode();
    check(r["state"] == "partial" && usagesCalls == 0, "reversed button range explicit unknown");
    reset(); values.push_back(values[0]); r = decode();
    check(unknownControls(r) && valueCalls == 0, "duplicate value declaration cannot establish an unambiguous value");
    reset(); buttons = {button(),button()}; r = decode();
    check(r["state"] == "partial" && r["buttons"].size() == 4, "duplicate button declarations cannot claim complete");
    reset(); values.push_back(axis(0x31)); valueResults[{1,1,0x31}].status = HIDP_STATUS_USAGE_NOT_FOUND; r = decode();
    check(r["state"] == "partial" && r["decodedControlsKnown"] == true && r["decodeComplete"] == false &&
        r["values"][1]["state"] == "unknown" && r["values"][1]["rawUnsigned"].is_null(), "per-field failure preserves success and unknown field");
    reset(); valueResults[{1,1,0x30}].status = HIDP_STATUS_INVALID_REPORT_LENGTH; r = decode();
    check(unknownControls(r) && r["apiErrors"][0]["status"] == int64_t(HIDP_STATUS_INVALID_REPORT_LENGTH), "all value failures remain unknown");
    reset(); values.clear(); buttons = {button()}; buttonResults[1].status = HIDP_STATUS_INCOMPATIBLE_REPORT_ID; r = decode();
    check(unknownControls(r) && r["buttonsDown"].empty(), "GetUsages failure is not an empty known button set");
    reset(); caps.NumberInputValueCaps = 1; id = {true,true,0x0db0,0x1901,1,5,"\\\\?\\hid#vid_0db0&pid_1901&mi_02&col01#fixture"};
    r = decodeGamepadReceipt(fixtureRawHandle,id,expectedReport.data(),8);
    check(r["decodeComplete"] == true, "xinput game collection also qualified without adapter");
    reset(); caps.Usage = 4; caps.NumberInputValueCaps = 1; id = identity(); id.usage = 4;
    r = decodeGamepadReceipt(fixtureRawHandle,id,expectedReport.data(),8);
    check(r["decodeComplete"] == true, "joystick top-level usage cross validation");
    reset(); caps.NumberInputValueCaps = 1; mutateReport = true; const auto before = expectedReport;
    r = decodeGamepadReceipt(fixtureRawHandle,identity(),expectedReport.data(),8);
    check(r["decodeComplete"] == true && expectedReport == before, "mutable HidP signature cannot mutate caller report");
    check(r.dump().find("718395a7b9cbdd") == std::string::npos && !r.contains("rawBytes") && !r.contains("path") && !r.contains("hash"),
        "controls do not contain receipt bytes/path/witness data");
    check(boundaryViolations == 0, "all fake boundary validations passed");
    std::cout << "checks=" << checks << " failures=" << failures
        << " productionHelperExercised=true realRawInputCalls=0 realHidCalls=0 RESULT=" << (failures ? "FAIL" : "PASS") << '\n';
    return failures ? 1 : 0;
}
#endif






