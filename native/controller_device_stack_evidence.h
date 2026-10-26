#pragma once
// Read-only, caller-selected PnP property evidence. These logs are NOT proof of
// realtime loaded driver images, filter attachment, Steam input or hiding success.
// Upper/LowerFilters are raw configuration, never equivalent to Device_Stack.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <devpropdef.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ymcc::controller::deviceStack {
using Json = nlohmann::json;
inline constexpr size_t maxTargets = 16, maxPropertyBytes = 16 * 1024,
                        maxListEntries = 64, maxTokenUnits = 512, maxCaptureRawBytes = 64 * 1024;
// SDK devpkey.h values, local linkage; no INITGUID or extern GUID dependency.
inline constexpr DEVPROPKEY stackKey = {{0x540b947e,0x8b40,0x45bc,{0xa8,0xa2,0x6a,0x0b,0x89,0x4c,0xbd,0xa2}},14};
inline constexpr DEVPROPKEY serviceKey = {{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},6};
inline constexpr DEVPROPKEY upperKey = {{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},19};
inline constexpr DEVPROPKEY lowerKey = {{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},20};
inline bool equalId(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
inline bool tokenValid(const std::wstring& s) {
    if (s.size() > maxTokenUnits) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<uint16_t>(s[i]);
        if (c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xfeff ||
            c == 0xfffe || c == 0xffff || c == 0x00ad || c == 0x061c || c == 0x180e ||
            (c >= 0x200b && c <= 0x200f) || (c >= 0x2028 && c <= 0x202e) ||
            (c >= 0x2060 && c <= 0x206f)) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == s.size() || s[i] < 0xdc00 || s[i] > 0xdfff) return false;
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    return true;
}
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
        static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string result(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
        static_cast<int>(s.size()), result.data(), n, nullptr, nullptr) != n) return {};
    return result;
}
// Pure bounded parser shared with the existing probe --selftest. Bytes are
// UTF-16LE, not wchar_t casts; malformed data never becomes an empty list.
inline Json decode(DEVPROPTYPE type, const std::vector<uint8_t>& bytes, DEVPROPTYPE expectedType) {
    Json result = {{"status","unknown"},{"error",nullptr},{"type",type},
        {"bytes",bytes.size()},{"values",nullptr},{"empty",nullptr}};
    const auto fail = [&](const char* why) { result["error"] = why; return result; };
    if (expectedType != DEVPROP_TYPE_STRING && expectedType != DEVPROP_TYPE_STRING_LIST)
        return fail("unsupported-expected-type");
    if (type != expectedType) return fail("type-mismatch");
    if (bytes.size() > maxPropertyBytes) return fail("property-byte-limit");
    if (bytes.size() % 2) return fail("odd-byte-count");
    const bool list = expectedType == DEVPROP_TYPE_STRING_LIST;
    if (bytes.size() < (list ? 4u : 2u)) return fail("missing-terminator");
    std::wstring units;
    units.reserve(bytes.size() / 2);
    for (size_t i = 0; i < bytes.size(); i += 2)
        units.push_back(static_cast<wchar_t>(uint16_t(bytes[i]) | (uint16_t(bytes[i+1]) << 8)));
    if (units.back() != 0 || (list && units[units.size()-2] != 0))
        return fail("missing-terminator");
    std::vector<std::wstring> unique;
    Json values = Json::array();
    size_t rawCount = 0;
    const auto append = [&](const std::wstring& value) -> const char* {
        if (value.size() > maxTokenUnits) return "token-unit-limit";
        if (!tokenValid(value)) return "invalid-utf16-or-control";
        if (++rawCount > maxListEntries) return "list-entry-limit";
        if (std::none_of(unique.begin(), unique.end(), [&](const auto& old) { return equalId(old,value); })) {
            auto text = utf8(value);
            if (!value.empty() && text.empty()) return "utf8-conversion-failed";
            unique.push_back(value); values.push_back(std::move(text));
        }
        return nullptr;
    };
    if (!list) {
        if (units.find(L'\0') != units.size()-1) return fail("interior-nul-or-trailing-data");
        if (const auto error = append(units.substr(0,units.size()-1))) return fail(error);
    } else if (!(units.size() == 2 && units[0] == 0)) {
        size_t start = 0;
        while (start < units.size()-1) {
            const auto end = units.find(L'\0',start);
            if (end == start || end == std::wstring::npos || end >= units.size()-1)
                return fail("interior-empty-or-trailing-data");
            if (const auto error = append(units.substr(start,end-start))) return fail(error);
            start = end+1;
        }
    }
    result["status"] = "known"; result["values"] = std::move(values);
    result["empty"] = list ? rawCount == 0 : units.size() == 1;
    result["rawTokenCount"] = rawCount; result["duplicateCount"] = rawCount-unique.size();
    return result;
}
inline std::string hidHideListed(const Json& parsed, bool identityStable) {
    if (!identityStable || parsed.value("status",std::string()) != "known" ||
        parsed.value("type",DEVPROPTYPE(0)) != DEVPROP_TYPE_STRING_LIST ||
        !parsed.contains("values") || !parsed["values"].is_array()) return "unknown";
    for (const auto& value : parsed["values"]) {
        if (!value.is_string()) return "unknown";
        auto token = value.get<std::string>();
        for (auto& c : token) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (token == "hidhide" || token == "\\driver\\hidhide") return "listed";
    }
    return "not-listed";
}
inline const char* exactId(const std::string& id, std::wstring& wide) {
    wide.clear();
    if (id.empty() || id.size() > maxTokenUnits*4) return "invalid-instance-size";
    const int n = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),static_cast<int>(id.size()),nullptr,0);
    if (n <= 0 || static_cast<size_t>(n) > maxTokenUnits) return "invalid-instance-utf8-or-size";
    wide.resize(static_cast<size_t>(n));
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,id.data(),static_cast<int>(id.size()),wide.data(),n) != n ||
        !tokenValid(wide)) return "invalid-instance-utf16-or-control";
    if (wide.find_first_of(L"/\"#?* ") != std::wstring::npos || wide.find(L'\0') != std::wstring::npos)
        return "not-exact-instance-id";
    const auto first = wide.find(L'\\'), last = wide.rfind(L'\\');
    if (first == std::wstring::npos || first == 0 || first == last || last == first+1 || last+1 == wide.size() ||
        wide.find(L'\\',first+1) != last) return "invalid-instance-components";
    auto upper = wide;
    for (auto& c : upper) if (c >= L'a' && c <= L'z') c -= L'a'-L'A';
    for (const auto marker : {L"VIRTUAL",L"VIGEM",L"VJOY",L"HYPER",L"VMBUS",L"HIDMAESTRO",L"HM-CTL"})
        if (upper.find(marker) != std::wstring::npos) return "virtual-or-hyper-instance-unknown";
    if (upper.substr(0,first) != L"HID" && upper.substr(0,first) != L"USB")
        return "non-hid-usb-instance-unknown";
    return nullptr;
}
inline Json notCalled() {
    return {{"attempted",false},{"configRet",nullptr},{"win32Error",nullptr},
        {"type",nullptr},{"bytes",nullptr},{"status","not-called"}};
}
inline Json cmStep(CONFIGRET rc) {
    return {{"attempted",true},{"configRet",static_cast<unsigned long>(rc)},
        {"win32Error",CM_MapCrToWin32Err(rc,ERROR_GEN_FAILURE)},
        {"status",rc == CR_SUCCESS ? "success" : rc == CR_BUFFER_SMALL ? "buffer-small" : "error"}};
}
struct RawBudget { size_t remaining = maxCaptureRawBytes, unreadProperties = 0; };
inline bool reserveRaw(RawBudget& budget, size_t bytes) {
    if (bytes == 0 || bytes > maxPropertyBytes || bytes > budget.remaining) {
        ++budget.unreadProperties; return false;
    }
    budget.remaining -= bytes; return true;
}
inline Json readProperty(DEVINST node, const DEVPROPKEY& key, DEVPROPTYPE expected, const char* kind, RawBudget& budget) {
    Json result = {{"status","unknown"},{"evidenceKind",kind},{"expectedType",expected},
        {"query",notCalled()},{"read",notCalled()},{"decode",nullptr},{"error",nullptr}};
    if (budget.remaining == 0) {
        ++budget.unreadProperties; result["error"] = "budget-exhausted-unknown"; return result;
    }
    DEVPROPTYPE type = 0; ULONG bytes = 0;
    auto rc = CM_Get_DevNode_PropertyW(node,&key,&type,nullptr,&bytes,0);
    result["query"] = cmStep(rc); result["query"]["type"] = type; result["query"]["bytes"] = bytes;
    if (rc != CR_BUFFER_SMALL && rc != CR_SUCCESS) {
        result["error"] = rc == CR_NO_SUCH_VALUE ? "property-not-reported" :
            rc == CR_CALL_NOT_IMPLEMENTED || rc == CR_INVALID_PROPERTY ? "unsupported" : "query-error";
        return result;
    }
    if (type != expected || bytes > maxPropertyBytes || bytes % 2 || bytes < (expected == DEVPROP_TYPE_STRING_LIST ? 4u : 2u)) {
        result["error"] = type != expected ? "query-type-mismatch" : "query-invalid-byte-count"; return result;
    }
    if (!reserveRaw(budget,bytes)) {
        result["error"] = "budget-exhausted-unknown"; return result;
    } // Reservation survives read failure; never retry.
    const ULONG allocated = bytes;
    std::vector<uint8_t> buffer(bytes,0xa5);
    type = 0;
    rc = CM_Get_DevNode_PropertyW(node,&key,&type,buffer.data(),&bytes,0);
    result["read"] = cmStep(rc); result["read"]["type"] = type;
    result["read"]["bytes"] = bytes; result["read"]["allocatedBytes"] = allocated;
    if (rc != CR_SUCCESS) { result["error"] = "read-error"; return result; }
    if (bytes > allocated || bytes != allocated) { result["error"] = "property-size-changed"; return result; }
    result["decode"] = decode(type,buffer,expected);
    result["status"] = result["decode"]["status"]; result["error"] = result["decode"]["error"];
    return result;
}
inline Json nodeIdentity(DEVINST node, const std::wstring& expected) {
    std::array<wchar_t,maxTokenUnits+1> buffer;
    buffer.fill(static_cast<wchar_t>(0xffff));
    const auto rc = CM_Get_Device_IDW(node,buffer.data(),static_cast<ULONG>(buffer.size()),0);
    auto result = cmStep(rc); result["devInst"] = node; result["exactMatch"] = false;
    result["id"] = nullptr; result["bufferUnits"] = buffer.size(); result["error"] = nullptr;
    if (rc != CR_SUCCESS) { result["error"] = "device-id-read-error"; return result; }
    const auto end = std::find(buffer.begin(),buffer.end(),L'\0');
    if (end == buffer.end()) { result["error"] = "device-id-not-terminated"; return result; }
    const std::wstring id(buffer.begin(),end);
    if (!tokenValid(id)) { result["error"] = "invalid-device-id"; return result; }
    result["id"] = utf8(id); result["exactMatch"] = equalId(id,expected);
    if (!result["exactMatch"].get<bool>()) result["error"] = "device-id-mismatch";
    return result;
}
inline Json capture(const std::vector<std::string>& exactInstances) noexcept {
    Json result;
    try {
        result = {{"version",1},{"status","unknown"},{"readOnly",true},
            {"requestedCount",exactInstances.size()},{"maxTargets",maxTargets},
            {"maxPropertyBytes",maxPropertyBytes},{"maxListEntries",maxListEntries},{"maxTokenUnits",maxTokenUnits},
            {"rawBudgetBytes",maxCaptureRawBytes},{"rawBytesReserved",0},{"unreadPropertyCount",0},
            {"omitted",exactInstances.size() > maxTargets ? exactInstances.size()-maxTargets : 0},
            {"omittedStatus",exactInstances.size() > maxTargets ? "unknown" : "none"},
            {"duplicates",0},{"targets",Json::array()},
            {"filterAttached","unknown"},{"externalVisibility","unknown"},
            {"scope","Caller-selected exact HID/USB nodes only; reported PnP properties; physical origin and realtime loaded images unproven"}};
        RawBudget budget;
        std::vector<std::wstring> seen;
        const size_t count = (std::min)(exactInstances.size(),maxTargets);
        bool allKnown = result["omitted"] == 0 && count != 0;
        for (size_t i = 0; i < count; ++i) {
            std::wstring id;
            const auto error = exactId(exactInstances[i],id);
            Json row = {{"inputIndex",i},{"status","unknown"},{"error",nullptr},
                {"exactInstanceId",nullptr},{"locate",notCalled()},
                {"identityBefore",notCalled()},{"identityAfter",notCalled()},
                {"sameDevInstReads",true},{"nodeIdentityStableAcrossRead",false},
                {"identityComparison","ordinal-ignore-case; entire instance ID"},
                {"properties",Json::object()},{"hidHideListed","unknown"},
                {"filterAttached","unknown"},{"externalVisibility","unknown"}};
            if (error) { row["error"] = error; allKnown = false; result["targets"].push_back(std::move(row)); continue; }
            row["exactInstanceId"] = utf8(id);
            if (std::any_of(seen.begin(),seen.end(),[&](const auto& old) { return equalId(old,id); })) {
                result["duplicates"] = result["duplicates"].get<size_t>()+1; continue;
            }
            seen.push_back(id);
            DEVINST node = 0;
            const auto rc = CM_Locate_DevNodeW(&node,id.data(),CM_LOCATE_DEVNODE_NORMAL);
            row["locate"] = cmStep(rc);
            if (rc != CR_SUCCESS) row["error"] = "locate-error";
            else {
                row["devInst"] = node; row["identityBefore"] = nodeIdentity(node,id);
                if (!row["identityBefore"].value("exactMatch",false)) row["error"] = "identity-before-unknown";
                else {
                    auto& properties = row["properties"];
                    properties["stack"] = readProperty(node,stackKey,DEVPROP_TYPE_STRING_LIST,"reported-device-stack",budget);
                    properties["service"] = readProperty(node,serviceKey,DEVPROP_TYPE_STRING,"raw-service-property",budget);
                    properties["upperFilters"] = readProperty(node,upperKey,DEVPROP_TYPE_STRING_LIST,"raw-filter-configuration",budget);
                    properties["lowerFilters"] = readProperty(node,lowerKey,DEVPROP_TYPE_STRING_LIST,"raw-filter-configuration",budget);
                    row["identityAfter"] = nodeIdentity(node,id);
                    const bool stable = row["identityAfter"].value("exactMatch",false);
                    row["nodeIdentityStableAcrossRead"] = stable;
                    row["hidHideListed"] = properties["stack"]["decode"].is_object() ?
                        hidHideListed(properties["stack"]["decode"],stable) : "unknown";
                    bool known = stable;
                    for (const auto& property : properties) if (property["status"] != "known") known = false;
                    row["status"] = known ? "known" : "unknown";
                    if (!stable) row["error"] = "identity-after-unknown";
                }
            }
            if (row["status"] != "known") allKnown = false;
            result["targets"].push_back(std::move(row));
        }
        result["rawBytesReserved"] = maxCaptureRawBytes-budget.remaining;
        result["unreadPropertyCount"] = budget.unreadProperties;
        if (budget.unreadProperties) result["error"] = "budget-exhausted-unknown";
        result["status"] = allKnown ? "known" : "unknown";
    } catch (...) {
        // Diagnosis must not change product verdicts or propagate exceptions.
        try { result = {{"version",1},{"status","unknown"},{"error","capture-exception"},
            {"readOnly",true},{"targets",Json::array()},{"filterAttached","unknown"},{"externalVisibility","unknown"}}; }
        catch (...) { return Json(); }
    }
    return result;
}
} // namespace ymcc::controller::deviceStack
