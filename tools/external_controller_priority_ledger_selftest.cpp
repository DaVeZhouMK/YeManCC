// Isolated filesystem fixture only. Never includes/runs main.cpp or the product.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
BOOL WINAPI fixtureWriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI fixtureReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL WINAPI fixtureFlushFileBuffers(HANDLE);
BOOL WINAPI fixtureMoveFileExW(LPCWSTR, LPCWSTR, DWORD);
}
// Faults affect this test translation unit only; production has no test hooks.
#define ReadFile fixtureReadFile
#define WriteFile fixtureWriteFile
#define FlushFileBuffers fixtureFlushFileBuffers
#define MoveFileExW fixtureMoveFileExW
#include "../native/external_controller_priority_ledger.h"
#undef ReadFile
#undef WriteFile
#undef FlushFileBuffers
#undef MoveFileExW
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace ep = ymcc::externalpad;
using Json = nlohmann::json;
namespace {
unsigned assertions = 0;
unsigned passed = 0;
unsigned total = 0;
enum class Fault { None, PartialWrite, ZeroWrite, Flush, Rename, Interrupt, ReadError, ZeroRead };
Fault fault = Fault::None;
unsigned writeCalls = 0;
DWORD lastRenameFlags = 0;
void check(bool condition, const char* expression, int line) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
BOOL WINAPI fixtureWriteFile(HANDLE file, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED overlapped) {
    if (fault == Fault::PartialWrite) {
        if (++writeCalls == 1) return WriteFile(file, data, (std::max)(DWORD{1}, size / 2), written, overlapped);
        SetLastError(ERROR_DISK_FULL); return FALSE;
    }
    if (fault == Fault::ZeroWrite) { *written = 0; return TRUE; }
    return WriteFile(file, data, size, written, overlapped);
}
BOOL WINAPI fixtureReadFile(HANDLE file, LPVOID data, DWORD size, LPDWORD read, LPOVERLAPPED overlapped) {
    if (fault == Fault::ReadError) { SetLastError(ERROR_READ_FAULT); return FALSE; }
    if (fault == Fault::ZeroRead) { *read = 0; return TRUE; }
    return ReadFile(file, data, size, read, overlapped);
}
BOOL WINAPI fixtureFlushFileBuffers(HANDLE file) {
    if (fault == Fault::Flush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    return FlushFileBuffers(file);
}
BOOL WINAPI fixtureMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    lastRenameFlags = flags;
    // Actual child-process interruption after successful flush/close, before rename.
    if (fault == Fault::Interrupt) ExitProcess(73);
    if (fault == Fault::Rename) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return MoveFileExW(from, to, flags);
}
struct ResetFault {
    explicit ResetFault(Fault value) { fault = value; writeCalls = 0; }
    ~ResetFault() { fault = Fault::None; }
};
const std::wstring hid = L"HID\\VID_0DB0&PID_1902&MI_00&COL01\\8&226AD&0&0000";
const std::wstring usb = L"USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&4";
ep::VisibilityDebt sample() {
    ep::VisibilityDebt debt;
    debt.hiddenPaths = {hid, usb};
    debt.applications = {L"C:\\Games\\Exact GAME.EXE", L"\\\\server\\share\\Apps\\helper.exe"};
    debt.cloakChanged = true;
    debt.builtinRoot = "USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&4";
    return debt;
}
ep::VisibilityDebt pendingSample() {
    auto debt = sample();
    debt.refreshPending = true;
    debt.locationPath = "pciroot(0)#PCI(1400)#UsbRoot(0)#USB(4)";
    return debt;
}
bool equal(const ep::VisibilityDebt& a, const ep::VisibilityDebt& b) {
    return a.owner == b.owner && a.hiddenPaths == b.hiddenPaths && a.applications == b.applications &&
        a.cloakChanged == b.cloakChanged && a.builtinRoot == b.builtinRoot &&
        a.refreshPending == b.refreshPending && a.locationPath == b.locationPath;
}
std::wstring tempBase() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    CHECK(size > 0 && size < buffer.size());
    return std::wstring(buffer.data(), size);
}
void writeRaw(const std::wstring& path, const std::string& bytes) {
    ep::detail::File file;
    file.handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file.handle != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    CHECK(WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr));
    CHECK(written == bytes.size());
}
std::string readRaw(const std::wstring& path) {
    ep::detail::File file;
    file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file.handle != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size{};
    CHECK(GetFileSizeEx(file.handle, &size));
    CHECK(size.QuadPart >= 0 && size.QuadPart <= 2 * static_cast<LONGLONG>(ep::maxDebtBytes));
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    CHECK(ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr));
    CHECK(read == bytes.size());
    return bytes;
}
struct Fixture {
    std::wstring root;
    Fixture() {
        const auto base = tempBase();
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            root = base + L"ymcc-externalpad-ledger-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
            if (CreateDirectoryW(root.c_str(), nullptr)) return;
            CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
        }
        throw std::runtime_error("cannot create private temp fixture");
    }
    std::wstring path(const wchar_t* name = L"ledger.json") const { return root + L"\\" + name; }
    std::vector<std::wstring> files() const {
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW data{};
        const HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return names;
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.push_back(root + L"\\" + data.cFileName);
        } while (FindNextFileW(find, &data));
        FindClose(find);
        return names;
    }
    ~Fixture() {
        // Nonrecursive cleanup only inside the freshly created private temp directory.
        for (const auto& file : files()) { SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(file.c_str()); }
        RemoveDirectoryW(root.c_str());
    }
};
void save(const std::wstring& path, const ep::VisibilityDebt& debt) {
    std::string error = "stale error";
    CHECK(ep::saveDebt(path, debt, error));
    CHECK(error.empty());
    CHECK(lastRenameFlags == (MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
}
ep::VisibilityDebt load(const std::wstring& path) {
    auto debt = sample();
    std::string error = "stale error";
    CHECK(ep::loadDebt(path, debt, error));
    CHECK(error.empty());
    return debt;
}
void rejects(const std::wstring& path, const std::string& bytes) {
    writeRaw(path, bytes);
    auto out = sample();
    const auto before = out;
    std::string error;
    ep::DebtLoadFailure failure = ep::DebtLoadFailure::None;
    CHECK(!ep::loadDebt(path, out, error, failure));
    CHECK(failure == ep::DebtLoadFailure::InvalidRecord);
    CHECK(!error.empty() && error.size() < 160);
    CHECK(equal(out, before));
    CHECK(readRaw(path) == bytes); // No repair or clearing on invalid load.
}
void invalidSave(const std::wstring& path, const ep::VisibilityDebt& debt, const std::string& old) {
    const auto before = debt;
    std::string error;
    CHECK(!ep::saveDebt(path, debt, error));
    CHECK(!error.empty() && error.size() < 160);
    CHECK(equal(before, debt));
    CHECK(readRaw(path) == old);
}
void missingAndRoundTrip() {
    Fixture fixture;
    CHECK(equal(load(fixture.path()), ep::VisibilityDebt{}));
    CHECK(GetFileAttributesW(fixture.path().c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(fixture.files().empty());
    const auto original = sample();
    save(fixture.path(), original);
    CHECK(equal(load(fixture.path()), original));
    CHECK(fixture.files().size() == 1);
    auto json = Json::parse(readRaw(fixture.path()));
    CHECK(json.size() == 8 && json["version"] == 1 && json["owner"] == ep::debtOwner);
    std::string reversed = "{";
    for (auto it = json.rbegin(); it != json.rend(); ++it) {
        if (reversed.size() > 1) reversed += ',';
        reversed += Json(it.key()).dump() + ":" + it.value().dump();
    }
    reversed += "}";
    writeRaw(fixture.path(), reversed);
    CHECK(equal(load(fixture.path()), original));
}
void unicodeAndExactStrings() {
    Fixture fixture;
    const auto path = fixture.path(L"\u8d26\u672c-\U0001f3ae.json");
    auto original = sample();
    original.hiddenPaths[0] = L"hid\\Vid_0db0&PID_1902&mi_00&col01\\8&226ad&0&0000";
    original.builtinRoot = "usb\\Vid_0db0&pid_1902\\5&3af6909b&0&4";
    original.applications = {L"C:\\Games\\\u6e38\u620f \U0001f3ae\\Exact\u00e9.exe",
        L"C:\\Games\\e\u0301\\different.exe", L"\\\\\u670d\u52a1\u5668\\\u5171\u4eab\\\u6e38\u620f.EXE"};
    save(path, original);
    CHECK(equal(load(path), original));
    const auto bytes = readRaw(path);
    CHECK(bytes.find("\xe6\xb8\xb8\xe6\x88\x8f") != std::string::npos);
    auto json = Json::parse(bytes);
    writeRaw(path, json.dump(-1, ' ', true)); // Escaped surrogate pairs also round-trip exactly.
    CHECK(equal(load(path), original));
    auto invalid = original;
    invalid.applications[0] = L"C:\\Games\\bad" + std::wstring(1, static_cast<wchar_t>(0xd800)) + L".exe";
    invalidSave(path, invalid, readRaw(path));
}
void clearingAndPartialReturn() {
    Fixture fixture;
    const auto original = sample();
    save(fixture.path(), original);
    auto partial = original;
    partial.hiddenPaths.erase(partial.hiddenPaths.begin());
    save(fixture.path(), partial);
    CHECK(equal(load(fixture.path()), partial));
    // A failed/unknown readback is represented by retaining pending fields.
    CHECK(load(fixture.path()).cloakChanged);
    save(fixture.path(), ep::VisibilityDebt{});
    CHECK(GetFileAttributesW(fixture.path().c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(equal(load(fixture.path()), ep::VisibilityDebt{}));
    CHECK(Json::parse(readRaw(fixture.path()))["hiddenPaths"].empty());
    CHECK(fixture.files().size() == 1);
}
void corruptionAndTruncation() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto bytes = readRaw(fixture.path());
    // Exclude the optional newline: dropping only whitespace is still valid JSON.
    const auto close = bytes.find_last_of('}');
    for (std::size_t size = 0; size <= close; ++size) rejects(fixture.path(), bytes.substr(0, size));
    for (const auto& invalid : {std::string("null"), std::string("[]"), std::string("{}"), std::string("true"),
        std::string("1"), std::string("\"ledger\""), bytes + "{}", bytes + "x", "/*comment*/" + bytes,
        bytes.substr(0, close) + ",}", std::string("\xef\xbb\xbf") + bytes,
        bytes + std::string("\0trailing", 9)}) rejects(fixture.path(), invalid);
    rejects(fixture.path(), std::string(ep::maxDebtBytes + 1, ' '));
    rejects(fixture.path(), "{\"hiddenPaths\":" + std::string(20000, '[') + "0" + std::string(20000, ']') + "}");
    const auto index = bytes.find("Exact GAME.EXE");
    CHECK(index != std::string::npos);
    auto badUtf8 = bytes; badUtf8[index] = static_cast<char>(0xff); rejects(fixture.path(), badUtf8);
    rejects(fixture.path(), "{\"owner\":\"\\ud800\"}");
}
void strictSchemaAndOwner() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto bytes = readRaw(fixture.path());
    const auto base = Json::parse(bytes);
    for (auto it = base.begin(); it != base.end(); ++it) {
        auto missing = base; missing.erase(it.key());
        if (it.key() == "refreshPending" || it.key() == "locationPath") {
            writeRaw(fixture.path(), missing.dump()); CHECK(equal(load(fixture.path()), sample()));
        } else rejects(fixture.path(), missing.dump());
        rejects(fixture.path(), bytes.substr(0, bytes.find_last_of('}')) + ',' + Json(it.key()).dump() + ':' + it.value().dump() + '}');
        for (const auto& wrong : {Json(nullptr), Json::object(), Json::array(), Json(7.5)}) {
            if (it.value().is_array() && wrong.is_array()) continue; // Empty debt arrays are valid.
            auto bad = base; bad[it.key()] = wrong; rejects(fixture.path(), bad.dump());
        }
    }
    rejects(fixture.path(), bytes.substr(0, bytes.find_last_of('}')) + ",\"ow\\u006eer\":\"ymcc-external-controller-priority-v1\"}");
    for (const auto& value : {Json(0), Json(2), Json(-1), Json(1.0), Json(true), Json("1")}) {
        auto bad = base; bad["version"] = value; rejects(fixture.path(), bad.dump());
    }
    for (const auto& value : {Json("other-owner"), Json("ymcc-external-controller-priority-v2"),
        Json("YMCC-external-controller-priority-v1"), Json("ymcc-external-controller-priority-v1 "), Json(1)}) {
        auto bad = base; bad["owner"] = value; rejects(fixture.path(), bad.dump());
    }
    for (const auto& value : {Json(0), Json("false"), Json::array({true})}) {
        auto bad = base; bad["cloakChanged"] = value; rejects(fixture.path(), bad.dump());
    }
    auto bad = base; bad["allDevices"] = true; rejects(fixture.path(), bad.dump());
    bad = base; bad["applications"] = Json::array({Json::array()}); rejects(fixture.path(), bad.dump());
    bad = base; bad["hiddenPaths"] = Json::array({42}); rejects(fixture.path(), bad.dump());
    save(fixture.path(), sample());
    auto invalid = sample(); invalid.owner = "other-owner"; invalidSave(fixture.path(), invalid, readRaw(fixture.path()));
}
void invalidTargetsAndPaths() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto old = readRaw(fixture.path());
    const auto base = Json::parse(old);
    for (const auto& target : {L"", L"HID", L"HID\\VID_0DB0&PID_1902", L"HID\\VID_0DB0&PID_1902\\*",
        L"*\\x\\y", L"HID\\x\\..", L"HID\\x\\a\\b", L"HID\\x\\", L"HID\\x\\a b", L"HID\\x\\a\n",
        L"HID\\x\\a/b", L"HID\\x\\a\"", L"HID\\x\\a;", L"HID\\x\\a:stream", L"\\\\?\\HID#x#y",
        L"C:\\dir\\file", L"HID\\x\\\u6e38"}) {
        auto invalid = sample(); invalid.hiddenPaths = {target}; invalidSave(fixture.path(), invalid, old);
        auto bad = base; std::string encoded; CHECK(ep::detail::toUtf8(target, encoded));
        bad["hiddenPaths"] = Json::array({encoded}); rejects(fixture.path(L"bad.json"), bad.dump());
    }
    for (const auto& app : {L"game.exe", L"C:game.exe", L"\\Games\\game.exe", L"C:/Games/game.exe",
        L"C:\\Games\\..\\game.exe", L"C:\\Games\\.\\game.exe", L"C:\\Games\\g*.exe", L"C:\\Games\\game.dll",
        L"C:\\Games\\game.exe ", L"C:\\Games\\game.exe.", L"C:\\Games\\game.exe:stream.exe",
        L"C:\\Games\\game\n.exe", L"\\\\?\\C:\\Games\\game.exe", L"\\\\.\\C:\\game.exe",
        L"\\Device\\HarddiskVolume1\\game.exe", L"C:\\NUL.exe", L"C:\\COM1.exe", L"C:\\LPT\u00b9.exe", L"C:\\NUL .exe", L"C:\\ COM1.exe",
        L"C:\\COM0.exe", L"C:\\CONIN$.exe",
        L"\\\\server\\game.exe", L"C:\\\\game.exe", L"C:\\.exe"}) {
        auto invalid = sample(); invalid.applications = {app}; invalidSave(fixture.path(), invalid, old);
        auto bad = base; std::string encoded; CHECK(ep::detail::toUtf8(app, encoded));
        bad["applications"] = Json::array({encoded}); rejects(fixture.path(L"bad.json"), bad.dump());
    }
    auto invalid = sample(); invalid.builtinRoot = "USB\\x\\*"; invalidSave(fixture.path(), invalid, old);
    invalid = sample(); invalid.builtinRoot = std::string("USB\\x\\a\0b", 9); invalidSave(fixture.path(), invalid, old);
    for (const auto& path : {L"ledger.json", L"C:ledger.json", L"\\ledger.json", L"\\\\.\\PhysicalDrive0", L"C:\\NUL.json", L"C:\\NUL .json", L"C:\\COM0.json"}) {
        auto out = sample(); std::string error;
        CHECK(!ep::loadDebt(path, out, error)); CHECK(equal(out, sample())); CHECK(!error.empty());
        CHECK(!ep::saveDebt(path, sample(), error)); CHECK(!error.empty());
    }
    const auto nulPath = fixture.path() + std::wstring(1, L'\0') + L"suffix";
    std::string error; auto out = sample();
    CHECK(!ep::loadDebt(nulPath, out, error)); CHECK(equal(out, sample()));
    CHECK(!ep::saveDebt(nulPath, sample(), error)); CHECK(readRaw(fixture.path()) == old);
}
void boundsAndDuplicates() {
    Fixture fixture;
    auto bounded = sample(); bounded.hiddenPaths.clear(); bounded.applications.clear();
    for (std::size_t i = 0; i < ep::maxHiddenPaths; ++i) bounded.hiddenPaths.push_back(L"HID\\exact\\instance-" + std::to_wstring(i));
    for (std::size_t i = 0; i < ep::maxApplications; ++i) bounded.applications.push_back(L"C:\\Games\\exact-" + std::to_wstring(i) + L".exe");
    save(fixture.path(), bounded); CHECK(equal(load(fixture.path()), bounded));
    const auto old = readRaw(fixture.path());
    auto invalid = bounded; invalid.hiddenPaths.push_back(L"HID\\exact\\extra"); invalidSave(fixture.path(), invalid, old);
    auto bad = Json::parse(old); bad["hiddenPaths"].push_back("HID\\exact\\extra"); rejects(fixture.path(L"bad.json"), bad.dump());
    invalid = bounded; invalid.applications.push_back(L"C:\\Games\\extra.exe"); invalidSave(fixture.path(), invalid, old);
    bad = Json::parse(old); bad["applications"].push_back("C:\\Games\\extra.exe"); rejects(fixture.path(L"bad.json"), bad.dump());
    invalid = sample(); invalid.hiddenPaths.push_back(L"hid\\vid_0db0&pid_1902&mi_00&col01\\8&226ad&0&0000");
    invalidSave(fixture.path(), invalid, old);
    invalid = sample(); invalid.applications.push_back(L"c:\\games\\exact game.exe"); invalidSave(fixture.path(), invalid, old);
    bad = Json::parse(old); bad["applications"].push_back("c:\\games\\exact-0.EXE"); rejects(fixture.path(L"bad.json"), bad.dump());
    bad = Json::parse(old); bad["hiddenPaths"].push_back("hid\\EXACT\\INSTANCE-0"); rejects(fixture.path(L"bad.json"), bad.dump());
    invalid = sample(); invalid.hiddenPaths = {L"HID\\x\\" + std::wstring(ep::maxInstanceChars, L'a')}; invalidSave(fixture.path(), invalid, old);
    invalid = sample(); invalid.applications = {L"C:\\" + std::wstring(ep::maxApplicationChars, L'a') + L".exe"}; invalidSave(fixture.path(), invalid, old);
    // Each path is within its character limit; the combined UTF-8 document exceeds the file budget.
    invalid = bounded;
    const auto segment = std::wstring(230, L'\u6e38') + L'\\';
    for (std::size_t i = 0; i < invalid.applications.size(); ++i)
        invalid.applications[i] = L"C:\\" + segment + segment + segment + segment + std::to_wstring(i) + L".exe";
    invalidSave(fixture.path(), invalid, old);
    std::string error; auto out = sample();
    CHECK(!ep::loadDebt(L"C:\\" + std::wstring(ep::maxLedgerPathChars, L'a'), out, error));
    CHECK(equal(out, sample()));
}
void refreshRoundTripAndLegacyDefaults() {
    Fixture fixture;
    auto original = pendingSample();
    original.builtinRoot = "usb\\Vid_0db0&pid_1902\\5&3af6909b&0&4";
    save(fixture.path(), original);
    CHECK(equal(load(fixture.path()), original));
    auto json = Json::parse(readRaw(fixture.path()));
    CHECK(json["version"] == 1 && json["refreshPending"] == true);
    CHECK(json["builtinRoot"] == original.builtinRoot && json["locationPath"] == original.locationPath);
    auto reduced = original;
    reduced.hiddenPaths.clear(); reduced.applications.clear(); reduced.cloakChanged = false;
    save(fixture.path(), reduced);
    CHECK(equal(load(fixture.path()), reduced));
    CHECK(load(fixture.path()).refreshPending); // All unhides complete, helper acknowledgement still owed.
    CHECK(load(fixture.path()).builtinRoot == original.builtinRoot);
    CHECK(load(fixture.path()).locationPath == original.locationPath);
    save(fixture.path(), reduced); // Unknown/failed helper acknowledgement retains the entire refresh duty.
    CHECK(equal(load(fixture.path()), reduced));
    save(fixture.path(), ep::VisibilityDebt{});
    const auto clean = load(fixture.path());
    CHECK(!clean.refreshPending && clean.locationPath.empty() && clean.builtinRoot.empty());
    CHECK(GetFileAttributesW(fixture.path().c_str()) != INVALID_FILE_ATTRIBUTES);
    const auto cleanJson = Json::parse(readRaw(fixture.path()));
    CHECK(cleanJson["refreshPending"] == false && cleanJson["locationPath"] == "");
    save(fixture.path(), sample());
    json = Json::parse(readRaw(fixture.path()));
    json.erase("refreshPending"); json.erase("locationPath");
    writeRaw(fixture.path(), json.dump());
    auto out = pendingSample(); std::string error;
    CHECK(ep::loadDebt(fixture.path(), out, error)); CHECK(error.empty());
    CHECK(equal(out, sample())); // A populated caller receives actual backward defaults.
    json["refreshPending"] = false; writeRaw(fixture.path(), json.dump());
    CHECK(equal(load(fixture.path()), sample()));
    json.erase("refreshPending"); json["locationPath"] = original.locationPath;
    writeRaw(fixture.path(), json.dump());
    auto defaults = sample(); defaults.locationPath = original.locationPath;
    CHECK(equal(load(fixture.path()), defaults));
    json["refreshPending"] = true; writeRaw(fixture.path(), json.dump());
    auto upgraded = defaults; upgraded.refreshPending = true;
    CHECK(equal(load(fixture.path()), upgraded));
    auto boundary = pendingSample(); boundary.locationPath = "USB" + std::string(ep::maxLocationPathBytes - 3, '_');
    save(fixture.path(), boundary); CHECK(equal(load(fixture.path()), boundary));
    // Exact minimal VID/PID root is sufficient; no device is queried.
    boundary.builtinRoot = "USB\\VID_abCD&PID_0000\\x";
    save(fixture.path(), boundary); CHECK(equal(load(fixture.path()), boundary));
}
void refreshValidationErrors() {
    Fixture fixture;
    const auto original = pendingSample();
    save(fixture.path(), original);
    const auto old = readRaw(fixture.path());
    const auto base = Json::parse(old);
    for (const auto& root : {"", "HID\\VID_0DB0&PID_1902\\x", "PCI\\VID_0DB0&PID_1902\\x",
        "USB\\VID_0DB&PID_1902\\x", "USB\\VID_0DBG&PID_1902\\x", "USB\\VID_0DB0&PID_190G\\x",
        "USB\\VID_0DB0&PID_1902&MI_00\\x", "USB\\other\\x", "USB\\VID_0DB0&PID_1902\\*"}) {
        auto invalid = original; invalid.builtinRoot = root; invalidSave(fixture.path(), invalid, old);
        auto bad = base; bad["builtinRoot"] = root; rejects(fixture.path(L"bad.json"), bad.dump());
    }
    for (const auto& location : {std::string(), std::string("PCIROOT(0)#PCI(1400)"),
        std::string("USB(4)\""), std::string("USB(4)'"), std::string("USB(4);other"), std::string("USB(4)|other"),
        std::string("USB(4)\x7f"), std::string("USB(4)\xff"), std::string("USB(\xe6\xb8\xb8)"),
        "USB" + std::string(ep::maxLocationPathBytes - 2, '_')}) {
        auto invalid = original; invalid.locationPath = location; invalidSave(fixture.path(), invalid, old);
        if (location.find('\xff') == std::string::npos) {
            auto bad = base; bad["locationPath"] = location; rejects(fixture.path(L"bad.json"), bad.dump());
        }
    }
    for (unsigned c = 0; c < 32; ++c) {
        const auto location = std::string("USB(4)") + static_cast<char>(c);
        auto invalid = original; invalid.locationPath = location; invalidSave(fixture.path(), invalid, old);
        auto bad = base; bad["locationPath"] = location; rejects(fixture.path(L"bad.json"), bad.dump());
    }
    auto bad = base; bad.erase("locationPath"); rejects(fixture.path(L"bad.json"), bad.dump());
    for (const auto& value : {Json(nullptr), Json(1), Json(0), Json("true"), Json::object(), Json::array()}) {
        bad = base; bad["refreshPending"] = value; rejects(fixture.path(L"bad.json"), bad.dump());
    }
    for (const auto& value : {Json(nullptr), Json(1), Json(true), Json::object(), Json::array()}) {
        bad = base; bad["locationPath"] = value; rejects(fixture.path(L"bad.json"), bad.dump());
    }
    rejects(fixture.path(L"bad.json"), old.substr(0, old.find_last_of('}')) + ",\"refreshP\\u0065nding\":true}");
    auto invalid = original; invalid.refreshPending = false; invalid.locationPath = "USB(4)\"";
    invalidSave(fixture.path(), invalid, old); // Optional does not mean unvalidated when present.
    invalid = original; invalid.refreshPending = false; invalid.builtinRoot = "HID\\VID_0DB0&PID_1902\\x";
    save(fixture.path(), invalid); CHECK(equal(load(fixture.path()), invalid)); // USB root is mandatory only for refresh.
}
void injectedFailuresPreserveOld() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto old = readRaw(fixture.path());
    auto next = pendingSample(); next.applications.push_back(L"C:\\Games\\next.exe");
    for (const auto value : {Fault::PartialWrite, Fault::ZeroWrite, Fault::Flush, Fault::Rename}) {
        ResetFault reset(value);
        invalidSave(fixture.path(), next, old);
        CHECK(fixture.files().size() == 1);
        CHECK(equal(load(fixture.path()), sample()));
    }
    for (const auto value : {Fault::PartialWrite, Fault::Flush, Fault::Rename}) {
        ResetFault reset(value);
        std::string error;
        CHECK(!ep::saveDebt(fixture.path(L"first.json"), sample(), error)); CHECK(!error.empty());
        CHECK(GetFileAttributesW(fixture.path(L"first.json").c_str()) == INVALID_FILE_ATTRIBUTES);
        CHECK(equal(load(fixture.path(L"first.json")), ep::VisibilityDebt{}));
        CHECK(fixture.files().size() == 1);
    }
}
void classifiedReadFailures() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto original = readRaw(fixture.path());
    auto out = sample(); out.cloakChanged = false;
    const auto before = out;
    std::string error;
    ep::DebtLoadFailure failure = ep::DebtLoadFailure::None;
    {
        ep::detail::File lock;
        lock.handle = CreateFileW(fixture.path().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(lock.handle != INVALID_HANDLE_VALUE);
        CHECK(!ep::loadDebt(fixture.path(), out, error, failure));
        CHECK(failure == ep::DebtLoadFailure::RetryableIo);
        CHECK(error.find("(32)") != std::string::npos); CHECK(equal(out, before));
    }
    CHECK(ep::loadDebt(fixture.path(), out, error, failure));
    CHECK(failure == ep::DebtLoadFailure::None); CHECK(error.empty()); CHECK(equal(out, sample()));
    for (const auto faultValue : {Fault::ReadError, Fault::ZeroRead}) {
        ResetFault injected(faultValue);
        CHECK(!ep::loadDebt(fixture.path(), out, error, failure));
        CHECK(failure == (faultValue == Fault::ReadError ? ep::DebtLoadFailure::RetryableIo : ep::DebtLoadFailure::InvalidRecord));
        CHECK(equal(out, sample())); CHECK(readRaw(fixture.path()) == original);
    }
    CHECK(ep::loadDebt(fixture.path(), out, error, failure)); CHECK(failure == ep::DebtLoadFailure::None);
    CHECK(ep::loadDebt(fixture.path(L"missing.json"), out, error, failure));
    CHECK(failure == ep::DebtLoadFailure::None); CHECK(equal(out, ep::VisibilityDebt{}));
    CHECK(!ep::loadDebt(fixture.root, out, error, failure)); CHECK(failure == ep::DebtLoadFailure::InvalidRecord);
    CHECK(!ep::loadDebt(L"relative.json", out, error, failure)); CHECK(failure == ep::DebtLoadFailure::InvalidRecord);
}
void realFilesystemFailures() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto old = readRaw(fixture.path());
    auto next = sample(); next.cloakChanged = false;
    {
        ep::detail::File lock;
        lock.handle = CreateFileW(fixture.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(lock.handle != INVALID_HANDLE_VALUE);
        invalidSave(fixture.path(), next, old); // Actual rename rejected by sharing mode.
        CHECK(fixture.files().size() == 1);
    }
    {
        ep::detail::File lock;
        lock.handle = CreateFileW(fixture.path().c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(lock.handle != INVALID_HANDLE_VALUE);
        std::string error; auto out = next;
        CHECK(!ep::loadDebt(fixture.path(), out, error)); CHECK(!error.empty()); CHECK(equal(out, next));
    }
    CHECK(SetFileAttributesW(fixture.path().c_str(), FILE_ATTRIBUTE_READONLY));
    invalidSave(fixture.path(), next, old);
    CHECK(SetFileAttributesW(fixture.path().c_str(), FILE_ATTRIBUTE_NORMAL));
    const auto missingParent = fixture.path(L"missing\\ledger.json");
    std::string error;
    CHECK(!ep::saveDebt(missingParent, sample(), error)); CHECK(!error.empty());
    CHECK(GetFileAttributesW(fixture.path(L"missing").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(equal(load(missingParent), ep::VisibilityDebt{}));
    auto out = sample();
    CHECK(!ep::loadDebt(fixture.root, out, error)); CHECK(!error.empty()); CHECK(equal(out, sample()));
    CHECK(!ep::saveDebt(fixture.root, sample(), error)); CHECK(!error.empty());
    // Final-component symlink refusal, where unprivileged symlink creation is available.
    const auto link = fixture.path(L"link.json");
    if (CreateSymbolicLinkW(link.c_str(), fixture.path().c_str(), 0x2)) {
        CHECK(!ep::loadDebt(link, out, error)); CHECK(equal(out, sample()));
        CHECK(!ep::saveDebt(link, next, error)); CHECK(readRaw(fixture.path()) == old);
        CHECK(DeleteFileW(link.c_str()));
    } else std::cout << "NOTE symlink fixture unavailable Win32=" << GetLastError() << '\n';
}
std::wstring selfExecutable() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    CHECK(size > 0 && size < buffer.size());
    return std::wstring(buffer.data(), size);
}
void interruptionPreservesOld() {
    Fixture fixture;
    save(fixture.path(), sample());
    const auto old = readRaw(fixture.path());
    writeRaw(fixture.path(L"fixture-only.marker"), "externalpad-test-only");
    std::wstring command = L"\"" + selfExecutable() + L"\" --interrupt-save \"" + fixture.root + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
        fixture.root.c_str(), &startup, &process));
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
    if (wait != WAIT_OBJECT_0) { TerminateProcess(process.hProcess, 74); WaitForSingleObject(process.hProcess, 5000); }
    DWORD exitCode = 0;
    const BOOL gotExit = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CHECK(wait == WAIT_OBJECT_0 && gotExit && exitCode == 73);
    CHECK(readRaw(fixture.path()) == old);
    CHECK(equal(load(fixture.path()), sample()));
    const auto files = fixture.files();
    CHECK(files.size() == 3);
    const auto orphan = std::find_if(files.begin(), files.end(), [](const std::wstring& name) {
        return name.find(L"ledger.json.tmp.") != std::wstring::npos;
    });
    CHECK(orphan != files.end());
    auto next = pendingSample(); next.applications.push_back(L"C:\\Games\\next.exe");
    CHECK(equal(load(*orphan), next)); // The orphan is a complete flushed replacement, ignored by normal load.
    save(fixture.path(), next);
    CHECK(equal(load(fixture.path()), next));
    CHECK(readRaw(*orphan) != old); // Saving neither adopts nor removes interrupted files.
    const auto absent = fixture.path(L"absent.json");
    writeRaw(absent + L".tmp.interrupted", readRaw(*orphan));
    CHECK(equal(load(absent), ep::VisibilityDebt{}));
}
int childInterrupt(const std::wstring& root) {
    // Refuse a caller-selected real ledger: child mode is restricted to this fixture namespace and marker.
    const auto prefix = tempBase() + L"ymcc-externalpad-ledger-fixture-";
    CHECK(root.size() > prefix.size() && root.compare(0, prefix.size(), prefix) == 0);
    CHECK(root.find(L'\\', prefix.size()) == std::wstring::npos && root.find(L'/', prefix.size()) == std::wstring::npos);
    CHECK(readRaw(root + L"\\fixture-only.marker") == "externalpad-test-only");
    auto next = pendingSample(); next.applications.push_back(L"C:\\Games\\next.exe");
    ResetFault reset(Fault::Interrupt);
    std::string error;
    ep::saveDebt(root + L"\\ledger.json", next, error);
    return 75; // The interruption point must have exited instead.
}
void run(const char* name, void (*test)()) {
    ++total;
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& error) { fault = Fault::None; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--interrupt-save") {
        try { return childInterrupt(argv[2]); } catch (...) { return 76; }
    }
    if (argc != 1) return 77;
    run("missing-file and exact round-trip", missingAndRoundTrip);
    run("Unicode paths and original strings", unicodeAndExactStrings);
    run("partial return and atomic empty overwrite", clearingAndPartialReturn);
    run("corruption, every truncation and bounded parsing", corruptionAndTruncation);
    run("strict schema, duplicate keys, types and exact owner", strictSchemaAndOwner);
    run("instance syntax, executable and ledger paths", invalidTargetsAndPaths);
    run("entry limits, byte budget and duplicates", boundsAndDuplicates);
    run("pending refresh, reduced debt, clean save and v1 backward defaults", refreshRoundTripAndLegacyDefaults);
    run("refresh USB VID/PID root, location, bounds and type errors", refreshValidationErrors);
    run("partial-write, zero-write, flush and rename failures", injectedFailuresPreserveOld);
    run("R53 typed sharing/read errors recover; malformed/truncated inputs remain invalid", classifiedReadFailures);
    run("actual locks, read-only target and missing parent", realFilesystemFailures);
    run("child interruption with flushed orphan and old file preserved", interruptionPreservesOld);
    std::cout << "EXTERNALPAD_LEDGER_SUMMARY passed=" << passed << " total=" << total << " assertions=" << assertions
        << " hardwareAccess=false productRun=false filesystem=private-temp-only\n";
    return passed == total ? 0 : 1;
}
