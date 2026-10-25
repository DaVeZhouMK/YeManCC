// Offline contract/falsification tests. The parent builds this translation unit.
// No Win32 adapter, process access, Steam API, game launch, or sleep operation.
#ifndef YMCC_SHORTCUT_METADATA_HEADER
#define YMCC_SHORTCUT_METADATA_HEADER "../native/steam_shortcut_metadata_diagnostic.h"
#endif
#include YMCC_SHORTCUT_METADATA_HEADER
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sm = ymcc::shortcutmetadata;
namespace sr = ymcc::steamrecord;
namespace {
using U64 = std::uint64_t;
using U32 = std::uint32_t;
using Json = sm::Json;
constexpr U64 kModule = 0x140000000ULL, kModuleSize = 0x2000000ULL;
constexpr U64 kEngine = 0x220000000ULL, kUsers = 0x230000000ULL;
constexpr U64 kUser = 0x240000000ULL, kNativeRecords = 0x250000000ULL;
constexpr U64 kArena = 0x260000000ULL, kRecord = 0x270000000ULL;
constexpr U64 kOtherRecord = 0x271000000ULL, kOtherRecord2 = 0x272000000ULL;
constexpr U64 kReplacementRecord = 0x273000000ULL, kOtherArena = 0x280000000ULL;
constexpr U64 kAccount = 0x0110000155667788ULL, kOtherAccount = kAccount ^ (1ULL << 32);
constexpr U32 kAppid = 0xf1234567U, kPid = 44551U, kForeground = 44552U;
constexpr U64 kGame = (U64(kAppid) << 32) | 0x02000000ULL;
constexpr U64 kOtherGame = kGame ^ (1ULL << 40);
constexpr U64 kManager = kUser + 0x5a60, kTargetNode = kArena + 32;
constexpr U64 kMaxAddress = 0x7fffffffffffULL;
constexpr const char* kSecret = "S25-PRIVATE-CALLBACK-DO-NOT-EXPORT";
constexpr const char* kSteamPath = "S25_PRIVATE_STEAM_PATH_CANARY";
constexpr const char* kGamePath = "S25_PRIVATE_GAME_PATH_CANARY";

std::size_t assertions = 0, cases = 0;
Json failures = Json::array();
struct CheckFailed { const char* check; };
void expect(bool value, const char* check) {
    ++assertions;
    if (!value) throw CheckFailed{check};
}
template<class F> void test(const std::string& name, F&& body) {
    ++cases;
    try { body(); }
    catch (const CheckFailed& e) { failures.push_back({{"case", name}, {"check", e.check}}); }
    // Never serialize arbitrary callback or nlohmann exception text.
    catch (...) { failures.push_back({{"case", name}, {"check", "unexpected exception escaped collector/test"}}); }
}
bool overlaps(U64 a, std::size_t n, U64 b, std::size_t m) {
    if (!n || !m) return false;
    return a <= b ? b - a < n : a - b < m;
}
std::string hex(U64 value, bool upper = false) {
    std::ostringstream out;
    if (upper) out << std::uppercase;
    out << std::hex << value;
    return out.str();
}
struct Access { U64 address; std::size_t size, ordinal; };
struct Memory {
    std::map<U64, unsigned char> data;
    std::vector<Access> reads;
    std::vector<std::pair<U64, std::size_t>> forbidden;
    std::function<void(Memory&, const Access&)> before;
    std::size_t shortAt = 0, throwAt = 0, forbiddenReads = 0, invalidReads = 0;
    void put(U64 address, U64 value, std::size_t width = 8) {
        for (std::size_t i = 0; i < width; ++i)
            data[address + i] = static_cast<unsigned char>(value >> (8 * i));
    }
    void zero(U64 address, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) data[address + i] = 0;
    }
    void forbid(U64 address, std::size_t size) { forbidden.emplace_back(address, size); }
    void resetTrace() { reads.clear(); forbiddenReads = 0; invalidReads = 0; }
    bool read(U64 address, void* destination, std::size_t size) {
        const Access access{address, size, reads.size() + 1};
        reads.push_back(access); // Includes refused and throwing attempts.
        if (size == 0 || size > 64 || address < 0x10000 || address > kMaxAddress ||
            size - 1 > kMaxAddress - address) {
            ++invalidReads;
            throw std::runtime_error(kSecret);
        }
        for (const auto& range : forbidden) {
            if (overlaps(address, size, range.first, range.second)) {
                ++forbiddenReads;
                throw std::runtime_error(kSecret);
            }
        }
        if (before) before(*this, access);
        if (access.ordinal == throwAt) throw std::runtime_error(kSecret);
        const auto copied = access.ordinal == shortAt ? size - 1 : size;
        for (std::size_t i = 0; i < copied; ++i) {
            const auto byte = data.find(address + i);
            if (byte == data.end()) return false;
            static_cast<unsigned char*>(destination)[i] = byte->second;
        }
        // Read's true contract is exact completion. A short read copies a prefix
        // and returns false, as the Win32 adapter must do for a partial read.
        return access.ordinal != shortAt;
    }
    bool touched(U64 address, std::size_t size) const {
        return std::any_of(reads.begin(), reads.end(), [&](const Access& a) {
            return overlaps(a.address, a.size, address, size);
        });
    }
    std::size_t lastTouch(U64 address, std::size_t size) const {
        std::size_t ordinal = 0;
        for (const auto& a : reads) if (overlaps(a.address, a.size, address, size)) ordinal = a.ordinal;
        return ordinal;
    }
    std::size_t bytesAttempted() const {
        std::size_t total = 0;
        for (const auto& a : reads) total += a.size;
        return total;
    }
};
struct NativeRow { U64 game; U32 primary = kPid, foreground = kForeground; };
struct Fixture {
    Memory memory;
    sm::Query q;
    bool readerAvailable = true, stableAvailable = true, supplyClock = true;
    unsigned stableCalls = 0, clockCalls = 0;
    std::function<bool(Fixture&, unsigned)> stability;
    // Clock is monotonic milliseconds. A constant injected clock avoids host
    // speed and scheduler dependence; timing tests advance it without sleeping.
    std::function<U64(Fixture&, unsigned)> clock;
    Fixture() {
        q.native = sr::Query{kModule, kModuleSize, kAccount, kGame, kPid, true};
        q.appid = kAppid;
        q.scope.steamPid = 33441;
        q.scope.gamePid = kPid;
        q.scope.windowsSession = 1;
        q.scope.steamBirth = 0x01dc45f987654321ULL;
        q.scope.gameBirth = 0x01dc45f987654765ULL;
        q.scope.generation = 0x1122334455667788ULL;
        q.scope.epoch = 0x2233445566778899ULL;
        q.scope.steamPathKey = kSteamPath;
        q.scope.gamePathKey = kGamePath;
        q.phase = sm::Phase::Baseline;
        q.originalTrackerBound = true; // Fixture provenance only; never a live gate.
        constexpr unsigned char getter[] = {
            0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3
        };
        for (std::size_t i = 0; i < sizeof(getter); ++i) memory.put(kModule + 0xcfdc0 + i, getter[i], 1);
        memory.put(kModule + 0x1382c78 + 0x20, kModule + 0xa5e6f0);
        constexpr unsigned char filter[] = {
            0x45,0x84,0xe4,0x74,0x0b,0x48,0x83,0x7d,0x68,0x00,0x0f,0x84,0xe6,0x01,0x00,0x00
        };
        for (std::size_t i = 0; i < sizeof(filter); ++i) memory.put(kModule + 0xa5e790 + i, filter[i], 1);
        memory.put(kModule + 0x181d008, kEngine);
        memory.put(kEngine, kModule + 0x1361f38);
        memory.put(kEngine + 0xfe0, kModule + 0x1361ff0);
        memory.zero(kEngine + 0x1080, 24);
        users({kAccount});
        nativeRows({{kGame}});
        manager();
        normalTree();
        metadataRecord(kRecord, kAppid, 0);
        // Deliberately readable poison records: an incorrect dereference must
        // fail the isolation assertion even if the collector catches our error.
        metadataRecord(kOtherRecord, kAppid + 0x20U, kAccount);
        metadataRecord(kOtherRecord2, kAppid + 0x40U, kOtherAccount);
        memory.forbid(kOtherRecord, 0x400);
        memory.forbid(kOtherRecord2, 0x400);
        memory.forbid(kRecord + 0x78, 0x388); // Names/paths/opaque record tail.
    }
    static U64 user(std::size_t index) { return kUser + 0x10000ULL * index; }
    void users(const std::vector<U64>& accounts, std::size_t selected = 0) {
        memory.put(kEngine + 0x1080, accounts.empty() ? 0 : kUsers);
        memory.put(kEngine + 0x1090, accounts.size(), 4);
        for (std::size_t i = 0; i < accounts.size(); ++i) {
            const auto address = user(i);
            memory.zero(kUsers + 16 * i, 16);
            memory.put(kUsers + 16 * i + 8, address);
            memory.put(address, kModule + 0x136fa80);
            memory.put(address + 0x1e70, kModule + 0x136fbd0);
            memory.put(address + 0x20a, accounts[i]);
            if (i != selected) {
                // Other accounts' native record headers AND shortcut managers
                // are out of bounds even though their identity fields are not.
                memory.forbid(address + 0x2568, 24);
                memory.forbid(address + 0x5a60, 0x60);
            }
        }
    }
    void nativeRows(const std::vector<NativeRow>& rows, std::size_t selected = 0) {
        memory.zero(user(selected) + 0x2568, 24);
        memory.put(user(selected) + 0x2568, rows.empty() ? 0 : kNativeRecords);
        memory.put(user(selected) + 0x2578, rows.size(), 4);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto address = kNativeRecords + 0x330ULL * i;
            memory.put(address + 8, rows[i].game);
            memory.put(address + 0x198, rows[i].foreground, 4);
            memory.put(address + 0x19c, rows[i].primary, 4);
            if (rows[i].game != q.native.gameId64) memory.forbid(address + 0x198, 8);
        }
    }
    void manager(U64 address = kManager, U64 owner = kUser, unsigned loaded = 1,
                 std::int32_t root = 0, U32 count = 3, U64 arena = kArena) {
        memory.zero(address, 0x60);
        memory.put(address, kModule + 0x1382c78);
        memory.put(address + 8, owner);
        memory.put(address + 0x10, loaded, 1);
        memory.put(address + 0x38, static_cast<U32>(root), 4);
        memory.put(address + 0x3c, count, 4);
        memory.put(address + 0x44, count, 4); // Literal native highwater for dense fixtures.
        memory.put(address + 0x50, arena);
        memory.put(address + 0x58, count, 4); // Literal native allocation count.
    }
    void node(U32 index, std::int32_t left, std::int32_t right, std::int32_t parent,
              U32 key, U64 value, U64 arena = kArena) {
        const auto address = arena + 32ULL * index;
        memory.zero(address, 32);
        memory.put(address, static_cast<U32>(left), 4);
        memory.put(address + 4, static_cast<U32>(right), 4);
        memory.put(address + 8, static_cast<U32>(parent), 4);
        memory.put(address + 0x10, key, 4);
        memory.put(address + 0x18, value);
    }
    void normalTree() {
        node(0, 1, 2, -1, kAppid + 0x20U, kOtherRecord);
        node(1, -1, -1, 0, kAppid, kRecord);
        node(2, -1, -1, 0, kAppid + 0x40U, kOtherRecord2);
    }
    void metadataRecord(U64 address, U32 id, U64 field) {
        memory.zero(address, 0x78);
        memory.put(address, id, 4);
        memory.put(address + 0x68, field);
    }
    void absentTree() {
        node(1, -1, -1, 0, kAppid - 1U, kOtherRecord2);
        memory.forbid(kRecord, 0x400);
    }
    void emptyTree(unsigned loaded = 1) { manager(kManager, kUser, loaded, -1, 0, 0); }
    void chain(U32 count) {
        manager(kManager, kUser, 1, 0, count, kArena);
        for (U32 i = 0; i < count; ++i) {
            node(i, -1, i + 1 == count ? -1 : static_cast<std::int32_t>(i + 1),
                i == 0 ? -1 : static_cast<std::int32_t>(i - 1),
                kAppid - (count - 1 - i), i + 1 == count ? kRecord : kOtherRecord);
        }
    }
    sm::Outcome run() {
        memory.resetTrace();
        stableCalls = clockCalls = 0;
        sm::Read reader;
        sm::Stable stable;
        sm::Clock tick;
        if (readerAvailable) reader = [&](U64 a, void* d, std::size_t n) { return memory.read(a, d, n); };
        if (stableAvailable) stable = [&] {
            ++stableCalls;
            return stability ? stability(*this, stableCalls) : true;
        };
        if (supplyClock) tick = [&] {
            ++clockCalls;
            return clock ? clock(*this, clockCalls) : U64{1000};
        };
        return sm::observe(q, reader, stable, tick);
    }
};

bool sameWitness(const sm::Witness& a, const sm::Witness& b) {
    return a.valid == b.valid && a.scope == b.scope && a.moduleBase == b.moduleBase &&
        a.moduleSize == b.moduleSize && a.account64 == b.account64 && a.gameId64 == b.gameId64 &&
        a.appid == b.appid && a.user == b.user && a.manager == b.manager && a.node == b.node && a.record == b.record;
}
const Json& field(const Json& j, const char* key) {
    expect(j.is_object() && j.contains(key), "required diagnostic field missing");
    return j.at(key);
}
void checkPrivacy(const Json& diagnostic, const Fixture& f) {
    const auto text = diagnostic.dump();
    const std::vector<std::string> strings = {
        kSecret, kSteamPath, kGamePath, f.q.scope.steamPathKey, f.q.scope.gamePathKey,
        "S25_CHANGED_PRIVATE_PATH", "private exception detail"
    };
    for (const auto& value : strings) if (!value.empty())
        expect(text.find(value) == std::string::npos, "private path or callback text exported");
    const std::vector<U64> privateNumbers = {
        kModule,kEngine,kUsers,kUser,kManager,kArena,kTargetNode,kRecord,kOtherRecord,kOtherRecord2,
        kReplacementRecord,kOtherArena,kAccount,kOtherAccount,kGame,kOtherGame,kAppid,kPid,kForeground,
        f.q.native.moduleBase,f.q.native.moduleSize,f.q.native.account64,f.q.native.gameId64,f.q.appid,
        f.q.scope.steamBirth,f.q.scope.gameBirth,f.q.scope.generation,f.q.scope.epoch,
        f.q.original.user,f.q.original.manager,f.q.original.node,f.q.original.record
    };
    for (const auto value : privateNumbers) if (value > 65536) {
        expect(text.find(std::to_string(value)) == std::string::npos, "decimal identity or pointer exported");
        expect(text.find(hex(value)) == std::string::npos, "hex identity or pointer exported");
        expect(text.find(hex(value, true)) == std::string::npos, "uppercase identity or pointer exported");
    }
    const std::set<std::string> privateKeys = {
        "witness","original","scope","modulebase","modulesize","account64","gameid64","appid",
        "user","manager","node","record","steampathkey","gamepathkey","steampid","gamepid",
        "steambirth","gamebirth","generation","epoch","windowssession","exception","stack","errorstack"
    };
    const auto walk = [&](const auto& self, const Json& value) -> void {
        expect(!value.is_array(), "diagnostic exports an array of private material");
        if (!value.is_object()) return;
        for (auto it = value.begin(); it != value.end(); ++it) {
            auto key = it.key();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            expect(!privateKeys.count(key), "private witness/identity/error key exported");
            self(self, it.value());
        }
    };
    walk(walk, diagnostic);
}
void checkCommon(const Fixture& f, const sm::Outcome& result) {
    const auto& d = result.diagnostic;
    expect(field(d, "authority") == "diagnostic-only-no-admission", "diagnostic acquired admission authority");
    expect(field(d, "abaExcluded") == false, "ABA exclusion claimed");
    expect(field(d, "loginGenerationLease") == "unknown", "login generation lease invented");
    for (const auto key : {"allocatedCapacityVerified","inputVerified","continueVerified",
                           "accountValuesPersisted","pointerValuesPersisted"})
        expect(field(d, key) == false, "diagnostic overclaims verification/persistence");
    expect(f.memory.forbiddenReads == 0, "forbidden account/shortcut/arena read attempted");
    expect(f.memory.invalidReads == 0, "invalid or oversized callback read attempted");
    expect(f.memory.reads.size() <= 2047, "global attempted-read call cap exceeded");
    expect(f.memory.bytesAttempted() <= 65536, "global attempted-read byte cap exceeded");
    expect(field(d, "readCalls") == f.memory.reads.size(), "attempted callback calls miscounted");
    expect(field(d, "readBytes") == f.memory.bytesAttempted(), "attempted callback bytes miscounted");
    std::set<U64> nodes;
    for (const auto& a : f.memory.reads) {
        expect(a.size > 0 && a.size <= 64, "single-read size cap violated");
        if (overlaps(a.address, a.size, kArena, 4096 * 32)) {
            const U64 begin = std::max(a.address, kArena);
            const U64 end = std::min(a.address + a.size, kArena + 4096 * 32);
            for (U64 p = begin; p < end; ++p) nodes.insert((p - kArena) / 32);
        }
    }
    expect(nodes.size() <= 32, "more than 32 tree path nodes read");
    checkPrivacy(d, f);
}
void checkRereads(const Fixture& f) {
    std::map<U64, unsigned> coverage;
    for (const auto& a : f.memory.reads)
        for (std::size_t i = 0; i < a.size; ++i) ++coverage[a.address + i];
    expect(!coverage.empty(), "successful observation performed no reads");
    expect(std::all_of(coverage.begin(), coverage.end(), [](const auto& byte) { return byte.second >= 2; }),
        "successful observation leaves read bytes without a reread");
}
void checkUnavailable(const Fixture& f, const sm::Outcome& result) {
    checkCommon(f, result);
    const auto& d = result.diagnostic;
    expect(field(d, "available") == false, "rejected observation remains available");
    expect(field(d, "status") == "unavailable", "rejection reported as observed");
    for (const auto key : {"loadedByte","targetRecordPresent","recordField68Nonzero",
                           "remote0PredicateSatisfied","remote1PredicateSatisfied"})
        expect(field(d, key).is_null(), "rejected observation retains partial metadata");
    expect(!result.witness.valid, "rejected observation returns a valid witness");
}
void checkObserved(const Fixture& f, const sm::Outcome& result, unsigned loaded = 1,
                   bool present = true, bool nonzero = false, U64 node = kTargetNode,
                   U64 record = kRecord, U64 selectedUser = kUser) {
    checkCommon(f, result);
    const auto& d = result.diagnostic;
    expect(field(d, "status") == "observed" && field(d, "available") == true, "valid metadata not observed");
    expect(field(d, "loadedByte").is_number_integer() && field(d, "loadedByte") == loaded,
        "loaded byte changed/coerced to boolean");
    expect(field(d, "targetRecordPresent") == present, "wrong requested-key presence");
    expect(field(d, "nativeOriginalPrimaryMatched") == true, "native original primary not verified");
    if (present) {
        expect(field(d, "recordField68Nonzero") == nonzero, "field68 is not reduced to zero/nonzero");
        expect(field(d, "remote0PredicateSatisfied") == true, "remote0 predicate is not unconditional for found record");
        expect(field(d, "remote1PredicateSatisfied") == nonzero, "remote1 predicate does not match field68");
    } else {
        for (const auto key : {"recordField68Nonzero","remote0PredicateSatisfied","remote1PredicateSatisfied"})
            expect(field(d, key).is_null(), "missing key was converted to a false metadata predicate");
    }
    const auto& w = result.witness;
    expect(w.valid, "successful baseline/follow has no private witness");
    expect(w.scope == f.q.scope && w.moduleBase == f.q.native.moduleBase && w.moduleSize == f.q.native.moduleSize &&
        w.account64 == f.q.native.account64 && w.gameId64 == f.q.native.gameId64 && w.appid == f.q.appid,
        "private witness lost original input identity");
    if (f.q.phase == sm::Phase::FollowOriginal) {
        expect(sameWitness(w,f.q.original), "follow silently rebound the original witness");
        const bool same=f.q.original.node==(present?node:0)&&f.q.original.record==(present?record:0);
        expect(field(d,"metadataAllocationMatchesBaseline")==same,"allocation transition hidden");
    } else {
        expect(w.user == selectedUser && w.manager == selectedUser + 0x5a60 &&
            w.node == (present ? node : 0) && w.record == (present ? record : 0),
            "private witness captured wrong user/manager/target pointers");
        expect(field(d,"metadataAllocationMatchesBaseline").is_null(),"baseline claims a previous allocation");
    }
    expect(f.stableCalls >= 2, "successful observation lacks stability bracketing");
    if (f.supplyClock) expect(f.clockCalls >= 2, "injected clock not checked around observation");
    checkRereads(f);
}
sm::Witness capture(Fixture& f, bool present = true) {
    auto result = f.run();
    checkObserved(f, result, 1, present);
    f.q.phase = sm::Phase::FollowOriginal;
    f.q.original = result.witness;
    return result.witness;
}
void noMetadata(Fixture& f) { f.memory.forbid(kManager, 0x60); }
void reject(Fixture& f, bool zeroReads = false) {
    const auto result = f.run();
    checkUnavailable(f, result);
    if (zeroReads) expect(f.memory.reads.empty(), "input/provenance rejection performed a memory read");
}

void positiveCases() {
    for (const unsigned loaded : {0U,1U,255U}) for (const U64 value : {U64{0},U64{1},U64{1} << 63,kAccount})
        test("baseline/loaded-" + std::to_string(loaded) + "/field-kind-" +
             std::to_string(value == 0 ? 0 : value == 1 ? 1 : value == kAccount ? 3 : 2), [=] {
            Fixture f;
            f.memory.put(kManager + 0x10, loaded, 1);
            f.memory.put(kRecord + 0x68, value);
            checkObserved(f, f.run(), loaded, true, value != 0);
        });
    test("baseline/right-branch-unsigned-appid", [] {
        Fixture f;
        f.node(0, 2, 1, -1, kAppid - 0x20U, kOtherRecord);
        f.node(2, -1, -1, 0, kAppid - 0x40U, kOtherRecord2);
        checkObserved(f, f.run());
    });
    test("baseline/high-bit-versus-low-bit-key", [] {
        Fixture f;
        f.node(0, -1, 1, -1, 0x71234567U, kOtherRecord);
        checkObserved(f, f.run());
    });
    for (const unsigned loaded : {0U,1U}) test("baseline/empty-tree-loaded-" + std::to_string(loaded), [=] {
        Fixture f; f.emptyTree(loaded); f.memory.forbid(kArena, 4096 * 32); f.memory.forbid(kRecord, 0x400);
        checkObserved(f, f.run(), loaded, false);
    });
    test("baseline/nonempty-tree-requested-key-absent", [] {
        Fixture f; f.absentTree(); checkObserved(f, f.run(), 1, false);
    });
    test("follow/unchanged-original-witness", [] {
        Fixture f; const auto original = capture(f);
        const auto result = f.run(); checkObserved(f, result);
        expect(sameWitness(result.witness, original), "follow replaced original witness");
    });
    test("follow/empty-original-stays-empty", [] {
        Fixture f; f.emptyTree(); const auto original = capture(f, false);
        const auto result = f.run(); checkObserved(f, result, 1, false);
        expect(sameWitness(result.witness, original), "empty follow reissued a different witness");
    });
    test("follow/metadata-values-may-change-between-observations", [] {
        Fixture f; const auto original = capture(f);
        f.memory.put(kRecord + 0x68, kAccount); f.memory.put(kManager + 0x10, 0, 1);
        const auto result = f.run(); checkObserved(f, result, 0, true, true);
        expect(sameWitness(result.witness, original), "metadata value update minted a new identity");
    });
    test("baseline/optional-clock-omitted", [] {
        Fixture f; f.supplyClock = false; checkObserved(f, f.run());
    });
    test("baseline/other-account-records-and-shortcuts-stay-forbidden", [] {
        Fixture f; f.users({kAccount,kOtherAccount}); checkObserved(f, f.run());
    });
    test("baseline/target-account-not-first", [] {
        Fixture f; f.users({kOtherAccount,kAccount}, 1); f.nativeRows({{kGame}}, 1);
        f.manager(Fixture::user(1) + 0x5a60, Fixture::user(1));
        // The original selected user now belongs to another account.
        f.memory.forbid(kUser + 0x2568, 24); f.memory.forbid(kManager, 0x60);
        checkObserved(f, f.run(), 1, true, false, kTargetNode, kRecord, Fixture::user(1));
    });
    test("baseline/non-target-native-record-pids-stay-forbidden", [] {
        Fixture f; f.nativeRows({{kOtherGame},{kGame},{kGame ^ 1ULL}}); checkObserved(f, f.run());
    });
    test("baseline/native-foreground-is-not-primary", [] {
        Fixture f; f.nativeRows({{kGame,kPid,0}}); checkObserved(f, f.run());
    });
    test("baseline/metadata-count-upper-bound-last-index", [] {
        Fixture f; f.manager(kManager,kUser,1,4095,4096,kArena);
        f.node(4095,-1,-1,-1,kAppid,kRecord);
        f.memory.forbid(kArena, 4095 * 32);
        checkObserved(f, f.run(),1,true,false,kArena + 4095 * 32);
    });
    test("baseline/32-node-path-is-within-cap", [] {
        Fixture f; f.chain(32); checkObserved(f, f.run(),1,true,false,kArena + 31 * 32);
    });
}
void provenanceCases() {
    test("gate/default-follow-without-witness-performs-zero-reads", [] {
        Fixture f; sm::Query defaults;
        expect(defaults.phase == sm::Phase::FollowOriginal, "Query default phase is not FollowOriginal");
        f.q.phase = defaults.phase; reject(f,true);
    });
    test("gate/baseline-existing-valid-witness-cannot-rebind", [] {
        Fixture f; capture(f); f.q.phase = sm::Phase::Baseline; reject(f,true);
    });
    test("gate/baseline-needs-original-tracker-context", [] {
        Fixture f; f.q.originalTrackerBound = false; reject(f,true);
    });
    test("gate/follow-needs-original-tracker-context", [] {
        Fixture f; capture(f); f.q.originalTrackerBound = false; reject(f,true);
    });
    test("gate/invalid-witness-retains-no-authority", [] {
        Fixture f; capture(f); f.q.original.valid = false; reject(f,true);
    });
    test("gate/invalid-phase-does-not-default-to-baseline", [] {
        Fixture f; f.q.phase = static_cast<sm::Phase>(99); reject(f,true);
    });
    using Change = std::pair<const char*, std::function<void(sm::Query&)>>;
    const std::vector<Change> invalid = {
        {"steam-pid-zero",[](auto& q) { q.scope.steamPid = 0; }},
        {"game-pid-zero",[](auto& q) { q.scope.gamePid = 0; }},
        {"session-zero",[](auto& q) { q.scope.windowsSession = 0; }},
        {"steam-birth-zero",[](auto& q) { q.scope.steamBirth = 0; }},
        {"game-birth-zero",[](auto& q) { q.scope.gameBirth = 0; }},
        {"generation-zero",[](auto& q) { q.scope.generation = 0; }},
        {"epoch-zero",[](auto& q) { q.scope.epoch = 0; }},
        {"steam-path-empty",[](auto& q) { q.scope.steamPathKey.clear(); }},
        {"game-path-empty",[](auto& q) { q.scope.gamePathKey.clear(); }},
        {"scope-native-pid-disagree",[](auto& q) { ++q.native.expectedPid; }},
        {"native-account-zero",[](auto& q) { q.native.account64 = 0; }},
        {"native-game-zero",[](auto& q) { q.native.gameId64 = 0; }},
        {"native-pid-zero",[](auto& q) { q.native.expectedPid = 0; }},
        {"profile-unverified",[](auto& q) { q.native.profileVerified = false; }},
        {"new-profile-not-qualified-for-metadata",[](auto& q) { q.native.profile = sr::ProfileId::Public1792618285; }},
        {"invalid-profile-not-qualified-for-metadata",[](auto& q) { q.native.profile = static_cast<sr::ProfileId>(99); }},
        {"appid-zero",[](auto& q) { q.appid = 0; }},
        {"appid-below-shortcut-min",[](auto& q) {
            q.appid = 0x80001387U; q.native.gameId64 = (U64(q.appid) << 32) | 0x02000000ULL;
        }},
        {"appid-only-high-bit",[](auto& q) {
            q.appid = 0x80000000U; q.native.gameId64 = (U64(q.appid) << 32) | 0x02000000ULL;
        }},
        {"appid-game-high-dword-disagree",[](auto& q) { ++q.appid; }},
        {"game-low-dword-wrong-shortcut-type",[](auto& q) { q.native.gameId64 ^= 1ULL; }},
        {"game-low-dword-zero",[](auto& q) { q.native.gameId64 &= 0xffffffff00000000ULL; }},
        {"game-low-dword-extra-flags",[](auto& q) { q.native.gameId64 |= 0x04000000ULL; }}
    };
    for (const auto& change : invalid) test(std::string("gate/") + change.first, [&] {
        Fixture f; change.second(f.q); reject(f,true);
    });
    test("gate/empty-reader", [] { Fixture f; f.readerAvailable = false; reject(f,true); });
    test("gate/empty-stable", [] { Fixture f; f.stableAvailable = false; reject(f,true); });
    test("gate/shortcut-appid-min-is-inclusive", [] {
        Fixture f; f.q.appid = 0x80001388U;
        f.q.native.gameId64 = (U64(f.q.appid) << 32) | 0x02000000ULL;
        f.nativeRows({{f.q.native.gameId64}});
        f.manager(kManager,kUser,1,0,1,kArena);
        f.node(0,-1,-1,-1,f.q.appid,kRecord);
        f.metadataRecord(kRecord,f.q.appid,0);
        checkObserved(f,f.run(),1,true,false,kArena);
    });
    test("gate/shortcut-appid-maximum", [] {
        Fixture f; f.q.appid = 0xffffffffU;
        f.q.native.gameId64 = (U64(f.q.appid) << 32) | 0x02000000ULL;
        f.nativeRows({{f.q.native.gameId64}});
        f.manager(kManager,kUser,1,0,1,kArena);
        f.node(0,-1,-1,-1,f.q.appid,kRecord);
        f.metadataRecord(kRecord,f.q.appid,0);
        checkObserved(f,f.run(),1,true,false,kArena);
    });
    const std::vector<Change> changed = {
        {"steam-pid",[](auto& q) { ++q.scope.steamPid; }},
        {"game-pid",[](auto& q) { ++q.scope.gamePid; ++q.native.expectedPid; }},
        {"session",[](auto& q) { ++q.scope.windowsSession; }},
        {"steam-birth",[](auto& q) { ++q.scope.steamBirth; }},
        {"game-birth",[](auto& q) { ++q.scope.gameBirth; }},
        {"generation",[](auto& q) { ++q.scope.generation; }},
        {"epoch",[](auto& q) { ++q.scope.epoch; }},
        {"steam-path",[](auto& q) { q.scope.steamPathKey = "S25_CHANGED_PRIVATE_PATH"; }},
        {"game-path",[](auto& q) { q.scope.gamePathKey = "S25_CHANGED_PRIVATE_PATH"; }},
        {"module-base",[](auto& q) { q.native.moduleBase += 0x100000; }},
        {"module-size",[](auto& q) { ++q.native.moduleSize; }},
        {"account",[](auto& q) { q.native.account64 = kOtherAccount; }},
        {"gameid-and-appid",[](auto& q) {
            ++q.appid; q.native.gameId64 = (U64(q.appid) << 32) | 0x02000000ULL;
        }}
    };
    for (const auto& change : changed) test(std::string("follow/input-change/") + change.first, [&] {
        Fixture f; capture(f); change.second(f.q); reject(f,true);
    });
    // These edits falsify the supplied private witness while leaving input valid.
    // A current record may never repair an invalid/mismatching old witness.
    using EditWitness = std::pair<const char*, std::function<void(sm::Witness&)>>;
    const std::vector<EditWitness> mismatches = {
        {"scope-path",[](auto& w) { w.scope.gamePathKey = "S25_CHANGED_PRIVATE_PATH"; }},
        {"scope-birth",[](auto& w) { ++w.scope.gameBirth; }},
        {"scope-epoch",[](auto& w) { ++w.scope.epoch; }},
        {"scope-generation",[](auto& w) { ++w.scope.generation; }},
        {"module-base",[](auto& w) { w.moduleBase += 0x100000; }},
        {"module-size",[](auto& w) { ++w.moduleSize; }},
        {"account",[](auto& w) { w.account64 = kOtherAccount; }},
        {"gameid",[](auto& w) { w.gameId64 = kOtherGame; }},
        {"appid",[](auto& w) { ++w.appid; }},
        {"user-zero",[](auto& w) { w.user = 0; }},
        {"manager-zero",[](auto& w) { w.manager = 0; }}
    };
    for (const auto& edit : mismatches) test(std::string("follow/witness-mismatch/") + edit.first, [&] {
        Fixture f; capture(f); edit.second(f.q.original); noMetadata(f); reject(f);
    });
    test("follow/user-pointer-moved-same-account-and-game", [] {
        Fixture f; capture(f);
        const U64 replacement = Fixture::user(1);
        f.memory.put(kUsers + 8,replacement);
        f.memory.put(replacement,kModule + 0x136fa80);
        f.memory.put(replacement + 0x1e70,kModule + 0x136fbd0);
        f.memory.put(replacement + 0x20a,kAccount);
        f.nativeRows({{kGame}},1); f.manager(replacement + 0x5a60,replacement);
        f.memory.forbid(replacement + 0x5a60,0x60); noMetadata(f); reject(f);
    });
    test("follow/manager-witness-address-mismatch", [] {
        Fixture f; capture(f); f.q.original.manager += 8; noMetadata(f); reject(f);
    });
    test("follow/new-record-same-original-owner-key-is-tagged-not-rebound", [] {
        Fixture f; const auto original=capture(f);
        f.metadataRecord(kReplacementRecord,kAppid,1);f.memory.put(kTargetNode+0x18,kReplacementRecord);
        f.memory.forbid(kRecord,0x400);const auto result=f.run();
        checkObserved(f,result,1,true,true,kTargetNode,kReplacementRecord);
        expect(field(result.diagnostic,"metadataTransition")=="target-record-replaced","replacement not tagged");
        expect(sameWitness(result.witness,original),"replacement rebound witness");
    });
    test("follow/node-relocated-same-record-is-tagged", [] {
        Fixture f;capture(f);f.node(2,-1,-1,0,kAppid,kRecord);f.memory.put(kArena,2,4);
        const auto result=f.run();checkObserved(f,result,1,true,false,kArena+64,kRecord);
        expect(field(result.diagnostic,"metadataTransition")=="target-node-relocated","node relocation not tagged");
    });
    test("follow/arena-relocated-same-record-is-tagged", [] {
        Fixture f;capture(f);f.memory.put(kManager+0x50,kOtherArena);f.node(0,-1,-1,-1,kAppid,kRecord,kOtherArena);
        f.memory.put(kManager+0x38,0,4);f.memory.put(kManager+0x3c,1,4);const auto result=f.run();
        checkObserved(f,result,1,true,false,kOtherArena,kRecord);expect(field(result.diagnostic,"metadataTransition")=="target-node-relocated","arena relocation not tagged");
    });
    test("follow/original-game-still-present-but-metadata-key-removed", [] {
        Fixture f;capture(f);f.absentTree();const auto result=f.run();checkObserved(f,result,1,false);
        expect(field(result.diagnostic,"metadataTransition")=="target-record-removed","removal hidden");
    });
    test("follow/absent-metadata-key-appeared-without-rebinding", [] {
        Fixture f;f.absentTree();capture(f,false);f.normalTree();
        std::erase_if(f.memory.forbidden,[](const auto& x){return x.first==kRecord&&x.second==0x400;});
        const auto result=f.run();checkObserved(f,result);expect(field(result.diagnostic,"metadataTransition")=="target-record-appeared","appearance hidden");
    });
    test("follow/empty-tree-target-appeared-without-rebinding", [] {
        Fixture f;f.emptyTree();capture(f,false);f.manager();f.normalTree();const auto result=f.run();checkObserved(f,result);
        expect(field(result.diagnostic,"metadataTransition")=="target-record-appeared","empty appearance hidden");
    });
    test("follow/target-removed-to-empty-tree-without-rebinding", [] {
        Fixture f;capture(f);f.emptyTree();const auto result=f.run();checkObserved(f,result,1,false);
        expect(field(result.diagnostic,"metadataTransition")=="target-record-removed","empty removal hidden");
    });
    test("follow/replacement-record-wrong-key-is-refused", [] {
        Fixture f;capture(f);f.metadataRecord(kReplacementRecord,kAppid+1,1);f.memory.put(kTargetNode+0x18,kReplacementRecord);
        f.memory.forbid(kRecord,0x400);f.memory.forbid(kReplacementRecord+0x68,8);reject(f);
    });
    for(const bool gameId:{false,true})test(gameId?"follow/replacement-post-read-gameid-change-refused":"follow/replacement-post-read-primary-change-refused",[=]{
        Fixture f;capture(f);f.metadataRecord(kReplacementRecord,kAppid,1);f.memory.put(kTargetNode+0x18,kReplacementRecord);f.memory.forbid(kRecord,0x400);
        unsigned seen=0;f.memory.before=[&](Memory& memory,const Access& access){if(access.address==kReplacementRecord+0x68&&++seen==2){if(gameId)memory.put(kNativeRecords+8,kOtherGame);else memory.put(kNativeRecords+0x19c,kPid+77,4);}};
        reject(f);
    });
}

void anchorAndNativeCases() {
    const auto forbidHeap = [](Fixture& f) {
        f.memory.forbid(kEngine,0x2000); f.memory.forbid(kUsers,32 * 16);
        f.memory.forbid(kUser,0x6000); f.memory.forbid(kNativeRecords,256 * 0x330);
        f.memory.forbid(kArena,4096 * 32); f.memory.forbid(kRecord,0x400);
    };
    test("anchor/shortcut-vslot-mismatch-before-heap", [&] {
        Fixture f; f.memory.put(kModule + 0x1382c78 + 0x20,kModule + 0xa5e6f1);
        forbidHeap(f); reject(f);
    });
    constexpr std::array<unsigned char,16> filter = {
        0x45,0x84,0xe4,0x74,0x0b,0x48,0x83,0x7d,0x68,0x00,0x0f,0x84,0xe6,0x01,0x00,0x00
    };
    for (std::size_t i = 0; i < filter.size(); ++i) test("anchor/filter-byte-" + std::to_string(i), [&,i] {
        Fixture f; f.memory.put(kModule + 0xa5e790 + i,filter[i] ^ 1U,1);
        forbidHeap(f); reject(f);
    });
    test("anchor/r50-getter-retained", [&] {
        Fixture f; f.memory.put(kModule + 0xcfdc0 + 13,0,1); forbidHeap(f); reject(f);
    });
    for (const auto base : {U64{0},U64{0xffff},kMaxAddress + 1,std::numeric_limits<U64>::max() - 8})
        test("anchor/invalid-module-base-" + std::to_string(base < 0x10000 ? base : base == kMaxAddress + 1 ? 2 : 3), [=] {
            Fixture f; f.q.native.moduleBase = base; reject(f,true);
        });
    test("anchor/module-span-overflow", [] {
        Fixture f; f.q.native.moduleSize = std::numeric_limits<U64>::max(); reject(f,true);
    });
    test("anchor/module-image-truncates-required-global", [] {
        Fixture f; f.q.native.moduleSize = 0x181d00f; reject(f,true);
    });
    test("native/duplicate-account-before-metadata", [] {
        Fixture f; f.users({kAccount,kAccount}); noMetadata(f); reject(f);
    });
    test("native/account-not-found-before-metadata", [] {
        Fixture f; f.memory.put(kUser + 0x20a,kOtherAccount); noMetadata(f); reject(f);
    });
    test("native/account-high-bits-must-match", [] {
        Fixture f; f.memory.put(kUser + 0x20a,kAccount ^ (1ULL << 32)); noMetadata(f); reject(f);
    });
    test("native/empty-user-vector-before-metadata", [] {
        Fixture f; f.users({}); noMetadata(f); reject(f);
    });
    for (const U32 primary : {0U,kPid + 1}) test("native/primary-mismatch-" + std::to_string(primary == 0), [=] {
        Fixture f; f.nativeRows({{kGame,primary,kPid}}); noMetadata(f); reject(f);
    });
    test("native/target-record-absent-before-metadata", [] {
        Fixture f; f.nativeRows({{kOtherGame}}); noMetadata(f); reject(f);
    });
    test("native/empty-record-vector-is-not-empty-shortcut-tree", [] {
        Fixture f; f.nativeRows({}); noMetadata(f); reject(f);
    });
    test("native/full-gameid-not-only-high-dword", [] {
        Fixture f; f.nativeRows({{kGame ^ 1ULL}}); noMetadata(f); reject(f);
    });
    test("native/duplicate-full-gameid-before-metadata", [] {
        Fixture f; f.nativeRows({{kGame},{kGame}}); noMetadata(f); reject(f);
    });
    for (const auto offset : {U64{0},U64{0xfe0}}) test("native/engine-type-" + std::to_string(offset), [=] {
        Fixture f; f.memory.put(kEngine + offset,kModule + 0x136fa80); noMetadata(f); reject(f);
    });
    for (const auto offset : {U64{0},U64{0x1e70}}) test("native/user-type-" + std::to_string(offset), [=] {
        Fixture f; f.memory.put(kUser + offset,kModule + 0x1361f38); noMetadata(f); reject(f);
    });
    for (const U32 count : {33U,0x80000000U,0xffffffffU}) test("native/user-count-invalid-" + std::to_string(count), [=] {
        Fixture f; f.memory.put(kEngine + 0x1090,count,4); noMetadata(f);
        f.memory.forbid(kUsers,32 * 16); reject(f);
    });
    for (const U32 count : {257U,0x80000000U,0xffffffffU}) test("native/record-count-invalid-" + std::to_string(count), [=] {
        Fixture f; f.memory.put(kUser + 0x2578,count,4); noMetadata(f);
        f.memory.forbid(kNativeRecords,256 * 0x330); reject(f);
    });
}
void malformedMetadataCases() {
    for (const auto vptr : {kModule + 0x1339658,kModule + 0x136fa80,U64{0}})
        test("metadata/manager-wrong-type-" + std::to_string(vptr == 0 ? 0 : vptr == kModule + 0x1339658 ? 1 : 2), [=] {
            Fixture f; f.memory.put(kManager,vptr); f.memory.forbid(kArena,4096 * 32); reject(f);
        });
    test("metadata/manager-owner-is-another-user", [] {
        Fixture f; f.memory.put(kManager + 8,Fixture::user(1));
        f.memory.forbid(kArena,4096 * 32); reject(f);
    });
    test("metadata/manager-owner-null", [] {
        Fixture f; f.memory.put(kManager + 8,0); f.memory.forbid(kArena,4096 * 32); reject(f);
    });
    test("metadata/requested-record-id-mismatch", [] {
        Fixture f; f.memory.put(kRecord,kAppid + 1U,4); reject(f);
    });
    test("metadata/requested-record-id-low31-collision", [] {
        Fixture f; f.memory.put(kRecord,kAppid & 0x7fffffffU,4); reject(f);
    });
    for (const U32 count : {4097U,0x80000000U,0xffffffffU}) test("metadata/tree-count-invalid-" + std::to_string(count), [=] {
        Fixture f; f.memory.put(kManager + 0x3c,count,4);
        f.memory.forbid(kArena,4097 * 32); reject(f);
    });
    test("metadata/zero-count-nonempty-root", [] {
        Fixture f; f.memory.put(kManager + 0x3c,0,4); f.memory.forbid(kArena,4096 * 32); reject(f);
    });
    test("metadata/nonzero-count-empty-root", [] {
        Fixture f; f.memory.put(kManager + 0x38,0xffffffffU,4);
        f.memory.forbid(kArena,4096 * 32); reject(f);
    });
    test("metadata/native-capacity-allows-one-live-node-at-sparse-index-five", [] {
        Fixture f; f.manager(kManager, kUser, 1, 5, 1, kArena);
        f.memory.put(kManager + 0x44, 6, 4); f.memory.put(kManager + 0x58, 8, 4);
        f.node(5, -1, -1, -1, kAppid, kRecord);
        f.memory.forbid(kArena, 5 * 32); // Never scan freed/unallocated holes.
        const auto result = f.run();
        checkObserved(f, result, 1, true, false, kArena + 5 * 32);
        expect(f.memory.forbiddenReads == 0, "sparse lookup scanned holes");
    });
    test("metadata/native-capacity-allows-sparse-child-beyond-live-count", [] {
        Fixture f; f.manager(kManager, kUser, 1, 0, 2, kArena);
        f.memory.put(kManager + 0x44, 6, 4); f.memory.put(kManager + 0x58, 8, 4);
        f.node(0, 5, -1, -1, kAppid + 0x20U, kOtherRecord);
        f.node(5, -1, -1, 0, kAppid, kRecord);
        f.memory.forbid(kArena + 32, 4 * 32);
        const auto result = f.run();
        checkObserved(f, result, 1, true, false, kArena + 5 * 32);
        expect(f.memory.forbiddenReads == 0, "sparse child lookup scanned holes");
    });
    test("metadata/sparse-root-physically-present-but-outside-count", [] {
        Fixture f; f.node(3,-1,-1,-1,kAppid,kRecord);
        f.memory.put(kManager + 0x38,3,4); // count == 3, NOT allocation capacity.
        f.memory.forbid(kArena + 3 * 32,32); f.memory.forbid(kRecord,0x400); reject(f);
    });
    test("metadata/sparse-child-physically-present-but-outside-count", [] {
        Fixture f; f.node(3,-1,-1,0,kAppid,kRecord); f.memory.put(kArena,3,4);
        f.memory.forbid(kArena + 3 * 32,32); f.memory.forbid(kRecord,0x400); reject(f);
    });
    for (const U32 root : {0xfffffffeU,0x80000000U,0x7fffffffU})
        test("metadata/invalid-root-index-" + std::to_string(root), [=] {
            Fixture f; f.memory.put(kManager + 0x38,root,4);
            f.memory.forbid(kArena,4096 * 32); reject(f);
        });
    test("metadata/self-cycle", [] {
        Fixture f; f.memory.put(kArena,0,4); f.memory.forbid(kRecord,0x400); reject(f);
    });
    test("metadata/two-node-cycle", [] {
        Fixture f; f.node(1,0,-1,0,kAppid + 1U,kOtherRecord2);
        f.memory.forbid(kRecord,0x400); reject(f);
    });
    test("metadata/root-parent-must-be-sentinel", [] {
        Fixture f; f.memory.put(kArena + 8,0,4); f.memory.forbid(kRecord,0x400); reject(f);
    });
    for (const U32 parent : {0xffffffffU,2U,3U,0xfffffffeU})
        test("metadata/invalid-child-parent-" + std::to_string(parent), [=] {
            Fixture f; f.memory.put(kTargetNode + 8,parent,4); f.memory.forbid(kRecord,0x400); reject(f);
        });
    test("metadata/negative-link-other-than-minus-one", [] {
        Fixture f; f.memory.put(kArena,0xfffffffeU,4); f.memory.forbid(kRecord,0x400); reject(f);
    });
    test("metadata/path-key-order-violates-ancestor-bound", [] {
        Fixture f; f.node(1,-1,-1,0,kAppid + 0x30U,kOtherRecord2);
        f.memory.forbid(kRecord,0x400); reject(f);
    });
    test("metadata/33rd-path-node-must-not-be-read", [] {
        Fixture f; f.chain(33); f.memory.forbid(kArena + 32 * 32,32);
        f.memory.forbid(kRecord,0x400); reject(f);
    });
    const std::vector<U64> invalidPointers = {0,0xffff,kMaxAddress + 1,std::numeric_limits<U64>::max()};
    for (std::size_t i = 0; i < invalidPointers.size(); ++i) {
        test("metadata/invalid-target-record-pointer-" + std::to_string(i), [&,i] {
            Fixture f; f.memory.put(kTargetNode + 0x18,invalidPointers[i]); reject(f);
        });
        test("metadata/invalid-arena-pointer-" + std::to_string(i), [&,i] {
            Fixture f; f.memory.put(kManager + 0x50,invalidPointers[i]); reject(f);
        });
        test("native/invalid-engine-pointer-" + std::to_string(i), [&,i] {
            Fixture f; f.memory.put(kModule + 0x181d008,invalidPointers[i]); noMetadata(f); reject(f);
        });
        test("native/invalid-user-vector-pointer-" + std::to_string(i), [&,i] {
            Fixture f; f.memory.put(kEngine + 0x1080,invalidPointers[i]); noMetadata(f); reject(f);
        });
        test("native/invalid-record-vector-pointer-" + std::to_string(i), [&,i] {
            Fixture f; f.memory.put(kUser + 0x2568,invalidPointers[i]); noMetadata(f); reject(f);
        });
    }
    test("metadata/arena-span-crosses-address-ceiling", [] {
        Fixture f; f.memory.put(kManager + 0x50,kMaxAddress - 31); reject(f);
    });
    test("metadata/record-field-span-crosses-address-ceiling", [] {
        Fixture f; f.memory.put(kTargetNode + 0x18,kMaxAddress - 0x68);
        f.memory.forbid(kMaxAddress - 0x68,0x69); reject(f);
    });
}

struct Mutation { const char* name; U64 address, value; std::size_t width; };
void mutationCases() {
    // Last-touch ordinals are discovered from a real successful public observe
    // trace, not copied from the collector's implementation or its call counts.
    const std::vector<Mutation> mutations = {
        {"getter",kModule + 0xcfdc0,0x49,1},
        {"shortcut-vslot",kModule + 0x1382c98,kModule + 0xa5e6f1,8},
        {"filter-anchor",kModule + 0xa5e790,0x44,1},
        {"engine-global",kModule + 0x181d008,kEngine + 0x100000,8},
        {"engine-type",kEngine,kModule + 0x136fa80,8},
        {"engine-subobject-type",kEngine + 0xfe0,kModule + 0x136fa80,8},
        {"users-base",kEngine + 0x1080,kUsers + 16,8},
        {"users-count",kEngine + 0x1090,2,4},
        {"selected-user",kUsers + 8,Fixture::user(1),8},
        {"user-type",kUser,kModule + 0x1361f38,8},
        {"user-subobject-type",kUser + 0x1e70,kModule + 0x1361ff0,8},
        {"account",kUser + 0x20a,kOtherAccount,8},
        {"native-vector",kUser + 0x2568,kNativeRecords + 0x330,8},
        {"native-count",kUser + 0x2578,2,4},
        {"native-full-gameid",kNativeRecords + 8,kOtherGame,8},
        {"native-primary",kNativeRecords + 0x19c,kPid + 1U,4},
        {"native-foreground",kNativeRecords + 0x198,kForeground + 1U,4},
        {"manager-type",kManager,kModule + 0x1339658,8},
        {"manager-owner",kManager + 8,Fixture::user(1),8},
        {"loaded-byte",kManager + 0x10,0,1},
        {"tree-root",kManager + 0x38,1,4},
        {"tree-count",kManager + 0x3c,4,4},
        {"tree-arena",kManager + 0x50,kOtherArena,8},
        {"root-left",kArena,2,4},
        {"root-right",kArena + 4,0xffffffffU,4},
        {"root-parent",kArena + 8,0,4},
        {"root-key",kArena + 0x10,kAppid + 0x21U,4},
        {"non-target-node-value",kArena + 0x18,kOtherRecord2,8},
        {"target-left",kTargetNode,0,4},
        {"target-right",kTargetNode + 4,2,4},
        {"target-parent",kTargetNode + 8,0xffffffffU,4},
        {"target-key",kTargetNode + 0x10,kAppid + 1U,4},
        {"target-record-pointer",kTargetNode + 0x18,kReplacementRecord,8},
        {"record-appid",kRecord,kAppid + 1U,4},
        {"field68",kRecord + 0x68,1,8}
    };
    for (const bool follow : {false,true}) {
        Fixture reference;
        if (follow) capture(reference);
        const auto good = reference.run();
        test(std::string("mutation/reference/") + (follow ? "follow" : "baseline"), [&] {
            checkObserved(reference,good);
        });
        if (!good.diagnostic.value("available",false)) continue;
        for (const auto& mutation : mutations) {
            const auto ordinal = reference.memory.lastTouch(mutation.address,mutation.width);
            test(std::string("mutation/") + (follow ? "follow/" : "baseline/") + mutation.name, [&,ordinal] {
                expect(ordinal > 0,"mutation field absent from successful trace");
                Fixture f; if (follow) capture(f);
                bool injected = false;
                f.memory.before = [&](Memory& memory, const Access& access) {
                    if (access.ordinal == ordinal) {
                        memory.put(mutation.address,mutation.value,mutation.width); injected = true;
                    }
                };
                reject(f);
                expect(injected,"last-reread mutation never reached callback");
            });
        }
    }
    test("mutation/early-loaded-byte-before-second-overlapping-read", [] {
        Fixture f; unsigned overlapsSeen = 0;
        f.memory.before = [&](Memory& memory, const Access& access) {
            if (overlaps(access.address,access.size,kManager + 0x10,1) && ++overlapsSeen == 2)
                memory.put(kManager + 0x10,0,1);
        };
        reject(f); expect(overlapsSeen >= 2,"loaded-byte reread was never reached");
    });
    test("mutation/raw-field68-nonzero-to-another-nonzero", [] {
        Fixture f; f.memory.put(kRecord + 0x68,1); unsigned overlapsSeen = 0;
        f.memory.before = [&](Memory& memory, const Access& access) {
            if (overlaps(access.address,access.size,kRecord + 0x68,8) && ++overlapsSeen == 2)
                memory.put(kRecord + 0x68,kAccount);
        };
        reject(f); expect(overlapsSeen >= 2,"raw field68 was not reread");
    });
}

void postMetadataNativeRevalidationCases() {
    // Regression for the native-check -> final metadata-sweep window. Do NOT
    // alter a metadata byte or make Stable fail: those would mask the bug.
    for (const bool changeGame : {false,true})
        test(std::string("regression/native-changed-during-final-metadata-reread/") +
             (changeGame ? "full-gameid" : "primary-pid"), [=] {
            Fixture f;
            unsigned fieldReads = 0;
            bool injected = false;
            f.stability = [](auto&,unsigned) { return true; };
            f.memory.before = [&](Memory& memory, const Access& access) {
                if (overlaps(access.address,access.size,kRecord + 0x68,8) && ++fieldReads == 2) {
                    if (changeGame) memory.put(kNativeRecords + 8,kOtherGame);
                    else memory.put(kNativeRecords + 0x19c,kPid + 1U,4);
                    injected = true;
                }
            };
            const auto result = f.run();
            expect(injected,"final metadata reread did not trigger native mutation");
            checkUnavailable(f,result);
            expect(f.stableCalls >= 2,"regression did not retain Stable=true bracketing");
        });
}

void callbackFailureCases() {
    for (const bool follow : {false,true}) {
        Fixture reference; if (follow) capture(reference);
        const auto good = reference.run();
        test(std::string("fault/reference/") + (follow ? "follow" : "baseline"), [&] { checkObserved(reference,good); });
        if (!good.diagnostic.value("available",false)) continue;
        const std::size_t count = reference.memory.reads.size();
        for (std::size_t ordinal = 1; ordinal <= count; ++ordinal) {
            test(std::string("short-read/") + (follow ? "follow/" : "baseline/") + std::to_string(ordinal), [=] {
                Fixture f; if (follow) capture(f); f.memory.shortAt = ordinal;
                reject(f); expect(f.memory.reads.size() >= ordinal,"short-read injection was not reached");
            });
            test(std::string("read-exception/") + (follow ? "follow/" : "baseline/") + std::to_string(ordinal), [=] {
                Fixture f; if (follow) capture(f); f.memory.throwAt = ordinal;
                reject(f); expect(f.memory.reads.size() >= ordinal,"throwing-read injection was not reached");
            });
        }
    }
}

void stabilityAndTimingCases() {
    test("stable/false-before-reads", [] {
        Fixture f; f.stability = [](auto&,unsigned) { return false; }; reject(f,true);
    });
    test("stable/secret-exception-before-reads", [] {
        Fixture f; f.stability = [](auto&,unsigned) -> bool { throw std::runtime_error(kSecret); }; reject(f,true);
    });
    for (const bool throws : {false,true}) test(std::string("stable/late-") + (throws ? "secret-exception" : "false"), [=] {
        Fixture f; bool late = false;
        f.stability = [&](auto& current,unsigned) {
            if (!current.memory.touched(kRecord + 0x68,8)) return true;
            late = true; if (throws) throw std::runtime_error(kSecret); return false;
        };
        reject(f); expect(late,"late stability rejection not reached");
    });
    test("stable/final-check-revokes-already-assembled-metadata", [] {
        Fixture reference; const auto good = reference.run(); checkObserved(reference,good);
        const auto last = reference.stableCalls;
        Fixture f; f.stability = [=](auto&,unsigned call) { return call < last; }; reject(f);
        expect(f.stableCalls >= last,"final stability check was not exercised");
    });
    test("clock/exactly-100ms-checkpoint-is-allowed", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) { return U64{1000} + (current.memory.reads.empty() ? 0 : 100); };
        checkObserved(f,f.run());
    });
    test("clock/101ms-after-first-read-clears-metadata", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) { return U64{1000} + (current.memory.reads.empty() ? 0 : 101); };
        reject(f); expect(!f.memory.reads.empty(),"time budget did not reach read callback");
    });
    test("clock/late-101ms-after-field68-clears-metadata", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) { return U64{1000} + (current.memory.touched(kRecord + 0x68,8) ? 101 : 0); };
        reject(f); expect(f.memory.touched(kRecord + 0x68,8),"late timing failure never reached metadata");
    });
    test("clock/backwards-time-is-rejected", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) { return current.memory.reads.empty() ? U64{1000} : U64{999}; };
        reject(f);
    });
    test("clock/wrapped-time-is-rejected", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) {
            return current.memory.reads.empty() ? std::numeric_limits<U64>::max() - 50 : U64{50};
        };
        reject(f);
    });
    test("clock/secret-exception-at-start", [] {
        Fixture f; f.clock = [](auto&,unsigned) -> U64 { throw std::runtime_error(kSecret); }; reject(f,true);
    });
    test("clock/secret-exception-after-metadata", [] {
        Fixture f;
        f.clock = [](auto& current,unsigned) -> U64 {
            if (current.memory.touched(kRecord + 0x68,8)) throw std::runtime_error(kSecret);
            return 1000;
        };
        reject(f); expect(f.memory.touched(kRecord + 0x68,8),"late throwing clock was not reached");
    });
}

void combinedBudgetCases() {
    test("caps/32-accounts-256-native-rows-32-node-path-share-budget", [] {
        Fixture f;
        std::vector<U64> accounts(32);
        for (std::size_t i = 0; i < accounts.size(); ++i) accounts[i] = kOtherAccount + i + 1;
        accounts.back() = kAccount; f.users(accounts,31);
        std::vector<NativeRow> rows(256,NativeRow{kOtherGame}); rows.back() = NativeRow{kGame};
        f.nativeRows(rows,31);
        f.manager(Fixture::user(31) + 0x5a60,Fixture::user(31),1,0,32,kArena);
        // Populate a valid chain without changing the selected manager.
        for (U32 i = 0; i < 32; ++i)
            f.node(i,-1,i == 31 ? -1 : static_cast<std::int32_t>(i + 1),
                   i == 0 ? -1 : static_cast<std::int32_t>(i - 1),kAppid - (31 - i),
                   i == 31 ? kRecord : kOtherRecord);
        const auto result = f.run();
        if (result.diagnostic.value("available",false))
            checkObserved(f,result,1,true,false,kArena + 31 * 32,kRecord,Fixture::user(31));
        else {
            checkUnavailable(f,result);
            expect(f.memory.reads.size() >= 2000 || f.memory.bytesAttempted() >= 65000,
                "valid maximal arrays rejected without approaching a resource cap");
        }
    });
    test("caps/maximum-native-duplicates-cannot-unlock-metadata", [] {
        Fixture f;
        std::vector<U64> accounts(32,kOtherAccount); accounts.front() = kAccount; f.users(accounts);
        f.nativeRows(std::vector<NativeRow>(256,NativeRow{kGame})); noMetadata(f); reject(f);
    });
    test("privacy/field68-opaque-identity-value-never-exported", [] {
        Fixture f; f.memory.put(kRecord + 0x68,kOtherAccount); checkObserved(f,f.run(),1,true,true);
    });
    test("privacy/all-private-witness-values-stay-in-memory", [] {
        Fixture f; const auto original = capture(f); const auto result = f.run(); checkObserved(f,result);
        expect(sameWitness(result.witness,original),"follow mutated private witness while serializing diagnostic");
        expect(result.diagnostic.dump().find(kSecret) == std::string::npos,"private error canary exported");
    });
}
} // namespace

int main() {
    try {
        positiveCases();
        provenanceCases();
        anchorAndNativeCases();
        malformedMetadataCases();
        mutationCases();
        postMetadataNativeRevalidationCases();
        callbackFailureCases();
        stabilityAndTimingCases();
        combinedBudgetCases();
    } catch (const CheckFailed& e) {
        failures.push_back({{"case","suite/reference-fixture"},{"check",e.check}});
    } catch (...) {
        failures.push_back({{"case","suite"},{"check","unexpected exception; private text suppressed"}});
    }
    const auto failed = failures.size();
    // Failure output contains static scenario/check labels only, never a remote
    // address, account, path, Witness, arbitrary exception, or raw diagnostic.
    Json summary = {{"schema",1},{"suite","shortcut-metadata-offline"},
                    {"cases",cases},{"assertions",assertions},{"failed",failed}};
    if (failed) {
        Json sample = Json::array();
        for (std::size_t i = 0; i < std::min<std::size_t>(failed,24); ++i) sample.push_back(failures[i]);
        summary["failures"] = std::move(sample);
        if (failed > 24) summary["omittedFailures"] = failed - 24;
    }
    std::cout << summary.dump() << '\n';
    return failed ? 1 : 0;
}
