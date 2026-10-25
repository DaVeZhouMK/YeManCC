// Pure fixture: persistence, process facts and device effects are MEMORY ONLY.
#include "../native/msi_crash_recovery_journal.h"
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace r = ymcc::msi::recovery;
using Json = nlohmann::json;
namespace {
unsigned assertions = 0;
void check(bool condition, const char* expression, int line) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)
const std::string root2 = "USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&4";
const std::string root1 = "USB\\VID_0DB0&PID_1901\\5&3AF6909B&0&4";
const std::string game1 = "HID\\VID_0DB0&PID_1902&MI_00&COL01\\8&226AD&0&0000";
const std::string game2 = "HID\\VID_0DB0&PID_1902&MI_00&COL02\\8&226AD&0&0001";
const std::string vendor = "HID\\VID_0DB0&PID_1902&MI_01&COL03\\8&226AD&0&0002";
const std::string thirdParty = "HID\\VID_045E&PID_028E\\external";
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return s;
}
r::Record sample() {
    r::Record record;
    record.boardKey = "Micro-Star International Co., Ltd.|Claw A1M|MS-1T41|msi-claw";
    record.ownerPid = 4321;
    record.ownerCreationTime = "134045556789123456";
    record.transactionId = "fixture-only-transaction-1";
    record.modeReturnPending = true;
    record.rootInstance = root2;
    record.hiddenPaths = {root2, game1, game2, vendor};
    return record;
}
r::Facts eligible() { return {true, true, true, true, true, true, true, true}; }
std::string bytes(const r::Record& record) {
    auto encoded = r::encodeRecord(record);
    CHECK(!encoded.empty());
    return encoded;
}
void rejects(std::string_view input) {
    auto unchanged = sample();
    unchanged.transactionId = "output-must-stay-exact";
    const auto before = bytes(unchanged);
    CHECK(!r::decode(input, unchanged));
    CHECK(bytes(unchanged) == before);
    CHECK(r::decodeRecord(input).state == r::DecodeState::Invalid);
}
void invalidRecord(const r::Record& record) {
    CHECK(!r::validRecord(record));
    std::string output = "must-stay-exact";
    CHECK(!r::encode(record, output));
    CHECK(output == "must-stay-exact");
    CHECK(r::encodeRecord(record).empty());
}
void roundTrip() {
    const auto original = sample();
    const auto encoded = bytes(original);
    auto decoded = r::decodeRecord(encoded);
    CHECK(decoded.state == r::DecodeState::Valid);
    CHECK(bytes(decoded.record) == encoded);
    CHECK(decoded.record.boardKey == original.boardKey);
    CHECK(decoded.record.rootInstance == root2);
    CHECK(decoded.record.hiddenPaths == original.hiddenPaths);
    CHECK(decoded.record.modeReturnPending);
    CHECK(decoded.record.modeAttemptOwner.empty());
    auto ordered = Json::parse(encoded);
    std::string reversed = "{";
    bool first = true;
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        if (!first) reversed += ',';
        first = false;
        reversed += Json(it.key()).dump() + ":" + it.value().dump();
    }
    reversed += '}';
    CHECK(bytes(r::decodeRecord(reversed).record) == encoded);
    auto unicode = original;
    unicode.boardKey = "MSI|Claw|\xe5\xbe\xae\xe6\x98\x9f|msi";
    unicode.transactionId = "exact \\\" utf8 \xe5\xbe\xae";
    CHECK(r::validRecord(unicode));
    CHECK(bytes(r::decodeRecord(bytes(unicode)).record) == bytes(unicode));
}
void malformedJson() {
    const auto raw = bytes(sample());
    const auto base = Json::parse(raw);
    for (auto it = base.begin(); it != base.end(); ++it) {
        auto missing = base; missing.erase(it.key()); rejects(missing.dump());
        const auto duplicate = raw.substr(0, raw.size() - 1) + ',' + Json(it.key()).dump() + ':' + it.value().dump() + '}';
        rejects(duplicate);
    }
    rejects(raw.substr(0, raw.size() - 1) + ",\"sch\\u0065ma\":1}");
    rejects("{\"schema\":1,\"schema\":2}");
    auto unknown = base; unknown["canRestoreEverything"] = true; rejects(unknown.dump());
    for (const auto& invalid : {std::string(), std::string("null"), std::string("[]"), std::string("{}"),
            std::string("true"), std::string("1"), std::string("\"record\""), raw + "{}", raw + "x",
            "/* comment */" + raw, raw.substr(0, raw.size() - 1) + ",}"}) rejects(invalid);
    // Every possible truncated prefix of an actual complete record.
    for (std::size_t size = 0; size < raw.size(); ++size) rejects(std::string_view(raw).substr(0, size));
    rejects(raw + std::string("\0trailing", 9));
    rejects(std::string("\xef\xbb\xbf") + raw);
    auto nested = base; nested["boardKey"] = Json::object(); rejects(nested.dump());
    rejects("{\"hiddenPaths\":" + std::string(12000, '[') + "0" + std::string(12000, ']') + "}");
    rejects(raw.substr(0, raw.size() - 1) + ",\"x\":[[[[]]]]}");
    auto badUtf8 = base; badUtf8["boardKey"] = std::string("bad\xff|p|s|d");
    rejects(badUtf8.dump(-1, ' ', false, Json::error_handler_t::replace).replace(13, 1, "\xff"));
}
void strictTypes() {
    const auto base = Json::parse(bytes(sample()));
    for (auto it = base.begin(); it != base.end(); ++it) {
        const std::vector<Json> alternatives{nullptr, true, false, 1, -1, 1.0, "1", Json::object(), Json::array()};
        for (const auto& wrong : alternatives) {
            // JSON has one integer spelling; signed positive 1 serializes as unsigned 1.
            if (wrong.type() == it.value().type() ||
                (wrong.is_number_integer() && it.value().is_number_integer() && wrong == 1)) continue;
            auto changed = base; changed[it.key()] = wrong; rejects(changed.dump());
        }
    }
    for (const auto& literal : {"-0", "-1", "1.0", "1e0", "4294967296", "18446744073709551616", "true", "\"4321\""}) {
        const auto position = bytes(sample()).find("\"ownerPid\":4321");
        auto changed = bytes(sample()); changed.replace(position, std::string("\"ownerPid\":4321").size(), std::string("\"ownerPid\":") + literal);
        rejects(changed);
    }
    auto changed = base; changed["schema"] = 2; rejects(changed.dump());
    changed = base; changed["hiddenPaths"] = Json::array({1}); rejects(changed.dump());
    changed["hiddenPaths"] = Json::array({Json::array()}); rejects(changed.dump());
    changed["hiddenPaths"] = Json::array({Json::object()}); rejects(changed.dump());
    changed["hiddenPaths"] = Json::array({nullptr}); rejects(changed.dump());
}
void scalarBounds() {
    const auto base = sample();
    for (const auto& creation : {"", "0", "0000", "-1", "+1", "1.0", "1e3", " 1", "1 ", "0x10",
            "18446744073709551616", "100000000000000000000"}) {
        auto changed = base; changed.ownerCreationTime = creation; invalidRecord(changed);
        auto json = Json::parse(bytes(base)); json["ownerCreationTime"] = creation; rejects(json.dump());
    }
    auto changed = base; changed.ownerCreationTime = "18446744073709551615";
    CHECK(r::validRecord(changed)); CHECK(r::decodeRecord(bytes(changed)).state == r::DecodeState::Valid);
    changed.ownerCreationTime = "0001"; CHECK(r::validRecord(changed));
    changed = base; changed.ownerPid = 0; invalidRecord(changed);
    changed = base; changed.ownerPid = (std::numeric_limits<std::uint32_t>::max)(); CHECK(r::validRecord(changed));
    for (const auto& board : {"", "MSI", "MSI|Claw|Class", "MSI|Claw|System|Class|extra", "MSI||System|Class"}) {
        changed = base; changed.boardKey = board; invalidRecord(changed);
    }
    changed = base; changed.boardKey = std::string(r::maxBoardKeyBytes - 6, 'm') + "|p|s|d"; CHECK(r::validRecord(changed));
    changed.boardKey.insert(0, "m"); invalidRecord(changed);
    changed = base; changed.transactionId.assign(128, 't'); CHECK(r::validRecord(changed));
    changed.transactionId += 't'; invalidRecord(changed);
    changed = base; changed.transactionId = ""; CHECK(r::validRecord(changed));
    changed.transactionId = std::string("x\0y", 3); invalidRecord(changed);
    changed = base; changed.transactionId = "bad\xff"; invalidRecord(changed);
    for (const auto& owner : {"0:1", "1:0", "1", ":1", "1:", "1:1:1", "4294967296:1", "1:18446744073709551616", "-1:1"}) {
        changed = base; changed.modeAttemptOwner = owner; invalidRecord(changed);
    }
}
void pathAllowlist() {
    for (const auto& path : {root1, root2, game1, game2, vendor, lower(root2), lower(vendor),
            std::string("HID\\VID_0DB0&PID_1901&MI_01&COL03\\serial_123-456")}) CHECK(r::validOwnedPath(path));
    const std::vector<std::string> bad{
        "", thirdParty, "USB\\VID_0DB0&PID_9999\\5&3AF6909B&0&4", "USB\\VID_1234&PID_1902\\5&3AF6909B&0&4",
        "USB\\VID_0DB0&PID_1902", "USB\\VID_0DB0&PID_1902\\", "USB\\VID_0DB0&PID_19020\\tail",
        "USB\\VID_0DB0&PID_1902&MI_00\\tail", "USB\\VID_0DB0&PID_1902&REV_0100\\tail",
        "HID\\VID_0DB0&PID_1902&MI_01&COL01\\tail", "HID\\VID_0DB0&PID_1902&MI_01&COL02\\tail",
        "HID\\VID_0DB0&PID_1902&MI_01\\tail", "HID\\VID_0DB0&PID_1902&MI_00&COL03\\tail",
        "HID\\VID_0DB0&PID_1902&MI_02&COL03\\tail", "HID\\VID_0DB0&PID_1902&MI_00&COL01\\",
        "HID\\VID_0DB0&PID_1902&MI_00&COL010\\tail", "HID\\VID_0DB0&PID_1902&MI_00&COL01&COMMAND\\tail",
        "\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01#tail#{guid}", "ROOT\\VID_0DB0&PID_1902\\tail",
        "USB\\VID_0DB0&PID_1902\\tail\\extra", root2 + "\"", root2 + "'", root2 + "\n", root2 + "\t",
        root2 + "\x7f", root2 + "\xff", root2 + " & calc", root2 + ";calc", root2 + "|calc", root2 + ">file",
        root2 + "$()", root2 + "%PATH%", root2 + "`whoami`", root2 + "*", root2 + "/..", root2 + "..",
        root2 + "!", root2 + ":", root2 + std::string("\0", 1)
    };
    for (const auto& path : bad) {
        CHECK(!r::validOwnedPath(path)); CHECK(!r::validRootInstance(path));
        auto changed = sample(); changed.hiddenPaths = {path}; invalidRecord(changed);
        auto json = Json::parse(bytes(sample())); json["hiddenPaths"] = {path}; rejects(json.dump(-1, ' ', false, Json::error_handler_t::replace));
    }
    CHECK(!r::validRootInstance(game1)); CHECK(!r::validRootInstance(vendor));
    auto maxPath = std::string("USB\\VID_0DB0&PID_1902\\"); maxPath.append(r::maxPathBytes - maxPath.size(), 's');
    CHECK(r::validOwnedPath(maxPath)); CHECK(!r::validOwnedPath(maxPath + "s"));
}
void physicalRoot() {
    CHECK(!r::samePhysicalRoot(root1, root2)); CHECK(!r::samePhysicalRoot(root2, root1));
    CHECK(r::samePhysicalRoot(root2, lower(root2))); CHECK(r::samePhysicalRoot(root2, root2));
    for (const auto& bad : {"USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&40", "USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&5",
            "USB\\VID_0DB0&PID_1901\\6&3AF6909B&0&4", "USB\\VID_0DB0&PID_9999\\5&3AF6909B&0&4",
            "HID\\VID_0DB0&PID_1902&MI_00&COL01\\5&3AF6909B&0&4", "USB\\VID_0DB0&PID_1902\\"}) {
        CHECK(!r::samePhysicalRoot(root2, bad)); CHECK(!r::samePhysicalRoot(bad, root2));
    }
    CHECK(!r::samePhysicalRoot("same-untrusted", "same-untrusted"));
    auto other = sample(); other.hiddenPaths = {"USB\\VID_0DB0&PID_1902\\different-physical-machine"}; CHECK(r::validRecord(other));
    other = sample(); other.rootInstance = vendor; invalidRecord(other);
    other = sample(); other.hiddenPaths.push_back(root1); CHECK(r::validRecord(other));
}
void locationEvidenceAndLegacy() {
    const std::string location = "PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    auto record = sample();
    CHECK(record.locationPaths.empty()); CHECK(r::sameDeviceRoot(record, root2, {}));
    CHECK(r::sameDeviceRoot(record, lower(root2), {}));
    CHECK(!r::sameDeviceRoot(record, root1, {})); CHECK(!r::sameDeviceRoot(record, root1, {location}));
    record.locationPaths = {location};
    CHECK(r::validRecord(record)); CHECK(r::validLocationPath(location));
    CHECK(r::decodeRecord(bytes(record)).record.locationPaths == record.locationPaths);
    CHECK(r::sameDeviceRoot(record, root1, {location}));
    CHECK(r::sameDeviceRoot(record, "USB\\VID_0DB0&PID_1901\\new-generation-tail", {location}));
    CHECK(!r::sameDeviceRoot(record, root1, {}));
    CHECK(!r::sameDeviceRoot(record, root1, {location + "#USB(1)"}));
    CHECK(!r::sameDeviceRoot(record, root1, {lower(location)})); // byte-exact location evidence
    CHECK(!r::sameDeviceRoot(record, root1, {"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(40)"}));
    CHECK(!r::sameDeviceRoot(record, "USB\\VID_045E&PID_1901\\tail", {location}));
    CHECK(!r::sameDeviceRoot(record, vendor, {location}));
    auto invalidRoot = record; invalidRoot.rootInstance = thirdParty;
    CHECK(!r::sameDeviceRoot(invalidRoot, root1, {location}));
    CHECK(!r::sameDeviceRoot(record, root1, {""}));
    record.locationPaths.clear();
    for (std::size_t i = 0; i < 16; ++i) record.locationPaths.push_back(location + "#USB(" + std::to_string(i) + ")");
    CHECK(r::validRecord(record)); CHECK(r::decodeRecord(bytes(record)).state == r::DecodeState::Valid);
    auto json = Json::parse(bytes(record)); json["locationPaths"].push_back(location); rejects(json.dump());
    record.locationPaths.push_back(location); invalidRecord(record);
    record = sample(); record.locationPaths = {std::string(1024, 'L')}; CHECK(r::validRecord(record));
    record.locationPaths[0] += 'L'; invalidRecord(record);
    for (const auto& unsafe : {std::string(), std::string("x\""), std::string("x'"), std::string("x\n"),
            std::string("x\t"), std::string("x\xff"), std::string("x\0y", 3), std::string("x;calc"),
            std::string("x|calc"), std::string("x&calc"), std::string("x%PATH%"), std::string("x$()")}) {
        CHECK(!r::validLocationPath(unsafe)); record = sample(); record.locationPaths = {unsafe}; invalidRecord(record);
        json = Json::parse(bytes(sample())); json["locationPaths"] = {unsafe};
        rejects(json.dump(-1, ' ', false, Json::error_handler_t::replace));
    }
    // Mixed historical modes and generations remain intact through merges/reloads.
    record = sample(); record.rootInstance = root1; record.locationPaths = {location};
    CHECK(r::validRecord(record));
    CHECK(r::mergeOwnedPaths(record, {"USB\\VID_0DB0&PID_1902\\old-generation", root1}));
    CHECK(std::find(record.hiddenPaths.begin(), record.hiddenPaths.end(), root2) != record.hiddenPaths.end());
    CHECK(r::decodeRecord(bytes(record)).record.hiddenPaths == record.hiddenPaths);
}
void cardinalityAndUniqueness() {
    auto record = sample(); record.hiddenPaths.push_back(lower(vendor)); invalidRecord(record);
    auto json = Json::parse(bytes(sample())); json["hiddenPaths"].push_back(lower(root2)); rejects(json.dump());
    record = sample(); record.hiddenPaths.clear();
    for (std::size_t i = 0; i < 32; ++i)
        record.hiddenPaths.push_back("HID\\VID_0DB0&PID_1902&MI_00&COL01\\serial" + std::to_string(i));
    CHECK(r::validRecord(record)); CHECK(r::decodeRecord(bytes(record)).state == r::DecodeState::Valid);
    json = Json::parse(bytes(record)); json["hiddenPaths"].push_back(game2); rejects(json.dump());
    record.hiddenPaths.push_back(game2); invalidRecord(record);
    record = sample(); record.hiddenPaths.clear(); record.modeReturnPending = false;
    CHECK(r::validRecord(record)); CHECK(r::decodeRecord(bytes(record)).state == r::DecodeState::Valid);
}
void sizeBound() {
    auto raw = bytes(sample()); raw.append(r::maxJournalBytes - raw.size(), ' ');
    CHECK(raw.size() == 65536); CHECK(r::decodeRecord(raw).state == r::DecodeState::Valid);
    raw += ' '; rejects(raw);
    rejects(std::string(r::maxJournalBytes + 1, '{'));
}
void mergeKeepsLedger() {
    auto record = sample(); record.hiddenPaths = {game1}; record.modeAttemptOwner = "500:600";
    const auto original = record;
    CHECK(r::mergeOwnedPaths(record, {lower(game1), vendor, vendor, root2}));
    CHECK(record.hiddenPaths == std::vector<std::string>({game1, vendor, root2}));
    CHECK(record.modeReturnPending == original.modeReturnPending);
    CHECK(record.modeAttemptOwner == original.modeAttemptOwner);
    CHECK(record.ownerPid == original.ownerPid); CHECK(record.boardKey == original.boardKey);
    const auto before = bytes(record);
    CHECK(r::mergeOwnedPaths(record, {})); CHECK(bytes(record) == before);
    CHECK(r::mergeOwnedPaths(record, {lower(root2)})); CHECK(bytes(record) == before);
    CHECK(!r::mergeOwnedPaths(record, {game2, thirdParty})); CHECK(bytes(record) == before);
    // Historical legal roots remain debt; live ancestry requires caller proof.
    std::vector<std::string> additions;
    for (std::size_t i = 0; i < 32; ++i)
        additions.push_back("HID\\VID_0DB0&PID_1902&MI_00&COL01\\addition" + std::to_string(i));
    CHECK(!r::mergeOwnedPaths(record, additions)); CHECK(bytes(record) == before);
    additions.push_back(game2); CHECK(!r::mergeOwnedPaths(record, additions)); CHECK(bytes(record) == before);
    record = original; record.hiddenPaths.clear(); CHECK(r::mergeOwnedPaths(record, {root1, root2}));
    CHECK(record.hiddenPaths.size() == 2); CHECK(record.modeReturnPending);
    record.ownerPid = 0; CHECK(!r::mergeOwnedPaths(record, {game1}));
}
void claimIdentity() {
    auto record = sample();
    CHECK(r::claimModeAttempt(record, "00800:000900")); CHECK(record.modeAttemptOwner == "800:900");
    const auto once = bytes(record);
    CHECK(!r::claimModeAttempt(record, "800:900")); CHECK(!r::claimModeAttempt(record, "000800:00900"));
    CHECK(bytes(record) == once); CHECK(record.modeReturnPending); CHECK(record.hiddenPaths == sample().hiddenPaths);
    record.modeAttemptOwner = "0800:0900";
    CHECK(!r::claimModeAttempt(record, "800:900"));
    CHECK(r::claimModeAttempt(record, "800:901")); CHECK(record.modeAttemptOwner == "800:901");
    CHECK(r::claimModeAttempt(record, "801:901"));
    for (const auto& invalid : {"", "0:900", "800:0", "800", "800:900:1", "4294967296:900"}) {
        const auto before = bytes(record); CHECK(!r::claimModeAttempt(record, invalid)); CHECK(bytes(record) == before);
    }
    record.modeReturnPending = false; const auto before = bytes(record);
    CHECK(!r::claimModeAttempt(record, "802:902")); CHECK(bytes(record) == before);
}
void factsGates() {
    CHECK(r::decideRecovery({}) == r::RecoveryDecision::UnknownBoard);
    const std::vector<std::pair<bool r::Facts::*, r::RecoveryDecision>> gates{
        {&r::Facts::knownBoard, r::RecoveryDecision::UnknownBoard},
        {&r::Facts::boardMatch, r::RecoveryDecision::BoardMismatch},
        {&r::Facts::oldOwnerDead, r::RecoveryDecision::OldOwnerAliveOrUnknown},
        {&r::Facts::virtualDisabled, r::RecoveryDecision::VirtualStillEnabled},
        {&r::Facts::hostAbsent, r::RecoveryDecision::HostPresentOrUnknown},
        {&r::Facts::currentRootMatches, r::RecoveryDecision::RootMismatch},
        {&r::Facts::sourceFresh, r::RecoveryDecision::SourceStale},
        {&r::Facts::currentDinputObserved, r::RecoveryDecision::DinputNotObserved}
    };
    CHECK(r::decideRecovery(eligible()) == r::RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt);
    for (const auto& gate : gates) {
        auto f = eligible(); f.*(gate.first) = false; CHECK(r::decideRecovery(f) == gate.second);
    }
    auto alive = eligible(); alive.oldOwnerDead = false;
    CHECK(r::decideRecovery(alive) == r::RecoveryDecision::OldOwnerAliveOrUnknown);
    auto vmShared = eligible(); vmShared.knownBoard = false; vmShared.hostAbsent = false;
    CHECK(r::decideRecovery(vmShared) == r::RecoveryDecision::UnknownBoard);
    auto otherMachine = eligible(); otherMachine.boardMatch = false;
    CHECK(r::decideRecovery(otherMachine) == r::RecoveryDecision::BoardMismatch);
}

// A pending candidate is volatile. A powercut leaves exactly the previous
// committed version. This models an atomic store contract, NOT a Win32 store.
enum class Commit { Durable, Pending, Failed };
struct FakeStore {
    std::string durable;
    std::optional<std::string> pending;
    explicit FakeStore(const r::Record& initial) : durable(bytes(initial)) {}
    Commit submit(const r::Record& candidate, Commit outcome) {
        const auto encoded = bytes(candidate);
        pending.reset();
        if (outcome == Commit::Durable) durable = encoded;
        else if (outcome == Commit::Pending) pending = encoded;
        return outcome;
    }
    void complete() { CHECK(pending.has_value()); durable = *pending; pending.reset(); }
    void kill() { pending.reset(); }
    r::Record reload() const {
        auto decoded = r::decodeRecord(durable); CHECK(decoded.state == r::DecodeState::Valid);
        return decoded.record;
    }
};
bool contains(const std::vector<std::string>& paths, const std::string& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}
void killAcrossHideWriteAhead() {
    // game2 was ALREADY hidden, thirdParty is unrelated. Neither is new debt.
    const std::vector<std::string> additions{root2, game1, vendor};
    for (unsigned killBefore = 0; killBefore <= 8; ++killBefore) {
        auto initial = sample(); initial.hiddenPaths.clear(); initial.modeReturnPending = false;
        FakeStore store(initial); auto candidate = initial;
        std::vector<std::string> hidden{game2, thirdParty};
        bool durablePermit = false;
        for (unsigned phase = 0; phase < killBefore; ++phase) {
            switch (phase) {
            case 0: CHECK(r::mergeOwnedPaths(candidate, additions)); break;
            case 1: durablePermit = store.submit(candidate, Commit::Pending) == Commit::Durable; break;
            case 2: CHECK(!durablePermit); CHECK(hidden.size() == 2); break;
            case 3: store.complete(); durablePermit = true; break;
            case 4: case 5: case 6:
                CHECK(durablePermit); hidden.push_back(additions[phase - 4]); break;
            default: break;
            }
        }
        store.kill(); const auto recovered = store.reload();
        CHECK(recovered.hiddenPaths == (killBefore >= 4 ? additions : std::vector<std::string>{}));
        for (const auto& path : additions) if (contains(hidden, path)) CHECK(contains(recovered.hiddenPaths, path));
        CHECK(!contains(recovered.hiddenPaths, game2)); CHECK(!contains(recovered.hiddenPaths, thirdParty));
        CHECK(contains(hidden, game2)); CHECK(contains(hidden, thirdParty));
    }
}
void mode2RequiresDurablePending() {
    for (unsigned killBefore = 0; killBefore <= 6; ++killBefore) {
        auto initial = sample(); initial.modeReturnPending = false;
        FakeStore store(initial); auto candidate = initial;
        bool permit = false; unsigned mode2Dispatches = 0;
        for (unsigned phase = 0; phase < killBefore; ++phase) {
            switch (phase) {
            case 0: candidate.modeReturnPending = true; break;
            case 1: permit = store.submit(candidate, Commit::Pending) == Commit::Durable; break;
            case 2: CHECK(!permit); CHECK(mode2Dispatches == 0); break;
            case 3: store.complete(); permit = true; break;
            case 4: CHECK(permit); ++mode2Dispatches; break;
            default: break;
            }
        }
        store.kill(); auto recovered = store.reload();
        CHECK(recovered.modeReturnPending == (killBefore >= 4));
        CHECK(mode2Dispatches == 0 || recovered.modeReturnPending);
        CHECK(recovered.hiddenPaths == initial.hiddenPaths);
    }
    auto initial = sample(); initial.modeReturnPending = false; FakeStore failed(initial);
    auto candidate = initial; candidate.modeReturnPending = true;
    CHECK(failed.submit(candidate, Commit::Failed) != Commit::Durable);
    CHECK(!failed.reload().modeReturnPending);
}
void killAcrossModeConsume() {
    for (unsigned killBefore = 0; killBefore <= 7; ++killBefore) {
        FakeStore store(sample()); auto candidate = store.reload();
        bool consumed = false, durablePermit = false; unsigned dispatches = 0;
        for (unsigned phase = 0; phase < killBefore; ++phase) {
            switch (phase) {
            case 0: CHECK(r::decideRecovery(eligible()) == r::RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt); break;
            case 1: consumed = r::claimModeAttempt(candidate, "800:900"); CHECK(consumed); break;
            case 2: durablePermit = store.submit(candidate, Commit::Pending) == Commit::Durable; break;
            case 3: CHECK(!durablePermit); CHECK(dispatches == 0); break;
            case 4: store.complete(); durablePermit = true; break;
            case 5:
                CHECK(consumed && durablePermit);
                CHECK(r::decideRecovery(eligible()) == r::RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt);
                ++dispatches; break; // fake timeout/unknown: NEVER discharge debt
            default: break;
            }
        }
        store.kill(); auto recovered = store.reload(); CHECK(recovered.modeReturnPending);
        CHECK(recovered.hiddenPaths == sample().hiddenPaths);
        CHECK(r::claimModeAttempt(recovered, "800:900") == (killBefore < 5));
        if (dispatches) CHECK(killBefore >= 6);
    }
}
// The fake coordinator proves the integration ORDER required by the pure API.
// It performs no hardware calls and treats even a simulated send as unconfirmed.
bool fakeMode1(FakeStore& store, std::string_view owner, r::Facts before,
               Commit outcome, r::Facts immediatelyBeforeDispatch, unsigned& dispatches) {
    if (r::decideRecovery(before) != r::RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt) return false;
    auto candidate = store.reload();
    if (!r::claimModeAttempt(candidate, owner)) return false;
    if (store.submit(candidate, outcome) != Commit::Durable) return false;
    if (r::decideRecovery(immediatelyBeforeDispatch) != r::RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt) return false;
    ++dispatches; return true;
}
void perOwnerDurableOnce() {
    FakeStore store(sample()); unsigned dispatches = 0;
    CHECK(fakeMode1(store, "800:900", eligible(), Commit::Durable, eligible(), dispatches));
    for (unsigned retry = 0; retry < 4; ++retry) {
        store.kill(); CHECK(!fakeMode1(store, "800:900", eligible(), Commit::Durable, eligible(), dispatches));
    }
    CHECK(dispatches == 1); CHECK(store.reload().modeReturnPending);
    // A new process identity alone is insufficient: every live fact still gates.
    for (auto member : {&r::Facts::knownBoard, &r::Facts::boardMatch, &r::Facts::oldOwnerDead,
            &r::Facts::virtualDisabled, &r::Facts::hostAbsent, &r::Facts::currentRootMatches,
            &r::Facts::sourceFresh, &r::Facts::currentDinputObserved}) {
        auto facts = eligible(); facts.*member = false; const auto previous = store.durable;
        CHECK(!fakeMode1(store, "801:901", facts, Commit::Durable, facts, dispatches));
        CHECK(store.durable == previous); CHECK(dispatches == 1);
    }
    CHECK(fakeMode1(store, "800:901", eligible(), Commit::Durable, eligible(), dispatches)); // PID reuse
    CHECK(dispatches == 2); CHECK(store.reload().modeAttemptOwner == "800:901");
    CHECK(store.reload().modeReturnPending); CHECK(store.reload().hiddenPaths == sample().hiddenPaths);
}
void pendingAndFailedAreNotSuccess() {
    for (auto outcome : {Commit::Pending, Commit::Failed}) {
        FakeStore store(sample()); const auto previous = store.durable; unsigned dispatches = 0;
        CHECK(!fakeMode1(store, "800:900", eligible(), outcome, eligible(), dispatches));
        CHECK(dispatches == 0); CHECK(store.durable == previous);
        store.kill(); CHECK(store.reload().modeAttemptOwner.empty()); CHECK(store.reload().modeReturnPending);
        CHECK(fakeMode1(store, "800:900", eligible(), Commit::Durable, eligible(), dispatches));
        CHECK(dispatches == 1); CHECK(store.reload().modeReturnPending);
    }
}
void changedFactsConsumeWithoutDispatch() {
    for (auto member : {&r::Facts::knownBoard, &r::Facts::boardMatch, &r::Facts::oldOwnerDead,
            &r::Facts::virtualDisabled, &r::Facts::hostAbsent, &r::Facts::currentRootMatches,
            &r::Facts::sourceFresh, &r::Facts::currentDinputObserved}) {
        FakeStore store(sample()); unsigned dispatches = 0;
        auto changed = eligible(); changed.*member = false;
        CHECK(!fakeMode1(store, "800:900", eligible(), Commit::Durable, changed, dispatches));
        CHECK(dispatches == 0); store.kill(); CHECK(store.reload().modeAttemptOwner == "800:900");
        CHECK(store.reload().modeReturnPending);
        CHECK(!fakeMode1(store, "800:900", eligible(), Commit::Durable, eligible(), dispatches));
        CHECK(dispatches == 0);
    }
}
void partialDischargeNeedsDurability() {
    FakeStore store(sample()); std::vector<std::string> hidden = sample().hiddenPaths;
    hidden.push_back(thirdParty);
    // Restore ONE owned effect and positively observe it. A pending revised
    // ledger must leave the old responsibility available across a powercut.
    hidden.erase(std::find(hidden.begin(), hidden.end(), vendor));
    auto revised = store.reload(); revised.hiddenPaths.pop_back();
    CHECK(store.submit(revised, Commit::Pending) == Commit::Pending);
    store.kill(); CHECK(store.reload().hiddenPaths == sample().hiddenPaths);
    CHECK(store.submit(revised, Commit::Durable) == Commit::Durable);
    CHECK(store.reload().hiddenPaths.size() == 3); CHECK(store.reload().modeReturnPending);
    // Fake positive observation of ALL owned restores and mode return; then
    // require a durable replacement before the ledger may become debt-free.
    hidden = {thirdParty}; revised.hiddenPaths.clear(); revised.modeReturnPending = false;
    CHECK(store.submit(revised, Commit::Failed) == Commit::Failed);
    CHECK(store.reload().hiddenPaths.size() == 3); CHECK(store.reload().modeReturnPending);
    CHECK(store.submit(revised, Commit::Pending) == Commit::Pending);
    CHECK(store.reload().modeReturnPending); store.complete(); store.kill();
    CHECK(store.reload().hiddenPaths.empty()); CHECK(!store.reload().modeReturnPending);
    CHECK(hidden == std::vector<std::string>{thirdParty});
}
void boundedMutationRejection() {
    const auto base = bytes(sample());
    // Deterministic corruptions at structural positions, not random/flaky fuzz.
    for (std::size_t i = 0; i < base.size(); ++i) {
        if (base[i] == '{' || base[i] == '}' || base[i] == '[' || base[i] == ']' || base[i] == ',' || base[i] == ':') {
            // Commas/colons inside strings are legal text; only test punctuations
            // that actually produce invalid JSON or an invalid schema.
            auto changed = base; changed[i] = '\0'; rejects(changed);
        }
    }
    auto json = Json::parse(base);
    json["rootInstance"] = "USB\\VID_0DB0&PID_1902\\5&3AF6909B&0&40";
    CHECK(r::decodeRecord(json.dump()).state == r::DecodeState::Valid);
    CHECK(r::decodeRecord(json.dump()).record.hiddenPaths == sample().hiddenPaths); // retain old-generation debt
}
} // namespace
int main() {
    struct Test { const char* name; void (*run)(); };
    const std::vector<Test> tests{
        {"schema1-exact-roundtrip-and-owned4", roundTrip}, {"malformed-duplicates-truncation", malformedJson},
        {"strict-types-no-coercion", strictTypes}, {"scalar-identity-bounds", scalarBounds},
        {"path-allowlist-vendor-control-keyboard-denial", pathAllowlist}, {"full-physical-root-not-VID-only", physicalRoot},
        {"real-location-evidence-legacy-and-cross-mode-debt", locationEvidenceAndLegacy}, {"32-path-cap-and-case-unique", cardinalityAndUniqueness}, {"64KiB-raw-bound", sizeBound},
        {"atomic-idempotent-ledger-merge", mergeKeepsLedger}, {"numeric-per-owner-claim", claimIdentity},
        {"all-live-facts-old-owner-and-VM-denial", factsGates}, {"kill-at-every-hide-write-ahead-boundary", killAcrossHideWriteAhead},
        {"mode2-pending-before-dispatch", mode2RequiresDurablePending}, {"kill-before-after-durable-mode1-consume", killAcrossModeConsume},
        {"one-attempt-per-owner-reload-new-owner-gated", perOwnerDurableOnce}, {"pending-failed-commit-grants-no-write", pendingAndFailedAreNotSuccess},
        {"post-commit-facts-rechecked", changedFactsConsumeWithoutDispatch}, {"partial-observed-discharge-durable-only", partialDischargeNeedsDurability},
        {"bounded-corruptions-rejected", boundedMutationRejection}
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try { test.run(); ++passed; std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.name << " " << error.what() << '\n'; }
    }
    std::cout << "PURE_FAKE_SUMMARY passed=" << passed << " total=" << tests.size()
              << " assertions=" << assertions << " hardwareAccess=false productRun=false SDK=false persistentIO=false\n";
    return passed == tests.size() ? 0 : 1;
}
