#include "../native/steam_native_record_diagnostic.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using ymcc::steamrecord::Json;
using ymcc::steamrecord::Query;
namespace sr = ymcc::steamrecord;
namespace {
using U64 = std::uint64_t;
constexpr U64 MODULE_BASE = 0x140000000, ENGINE_ADDR = 0x220000000, USERS_ADDR = 0x230000000;
constexpr U64 USER_ADDR = 0x240000000, RECORDS_ADDR = 0x250000000, MAX_ADDR = 0x7fffffffffff;
constexpr U64 ACCOUNT = 0x0110000155667788, OTHER_ACCOUNT = ACCOUNT ^ (1ULL << 32);
constexpr U64 GAME = 0xf1234567abc00000, OTHER_GAME = GAME ^ (1ULL << 40);
constexpr std::uint32_t PID = 44551, FOREGROUND = 44552;
unsigned assertions = 0, cases = 0, failures = 0;
Json caseResults = Json::array(), measurements = Json::object();
struct Anchor { U64 rva; std::vector<unsigned char> bytes; };
const std::array<Anchor, 7> PREDICATE_ANCHORS = {{
    {0x9d7a9c, {0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}},
    {0x9d7bc9, {0xff,0x83,0xa8,0x01,0x00,0x00}},
    {0x9d7b2a, {0x8b,0x8b,0x68,0x02,0x00,0x00}},
    {0x9d7a5f, {0x88,0x83,0x00,0x02,0x00,0x00}},
    {0x9d7aef, {0x88,0x83,0x00,0x02,0x00,0x00}},
    {0x9d7bb6, {0x88,0x83,0x00,0x02,0x00,0x00}},
    {0x9d7bd9, {0x88,0x83,0x00,0x02,0x00,0x00}}
}};
constexpr std::array<const char*, 4> RAW_KEYS = {{"lastRecordKeepDecisionRaw", "comparisonLatchRaw",
    "failedCheckCounterRaw", "alternatePidRaw"}};
void expect(bool ok, const std::string& message) {
    ++assertions;
    if (!ok) throw std::runtime_error(message);
}
std::string hex(U64 value) { std::ostringstream out; out << std::hex << value; return out.str(); }
void checkPredicateInputs(const Json& inputs) {
    expect(inputs.is_object(), "only predicateInputs may be a nested object");
    const std::vector<std::string> allowed = {"source","profileVersion","status","available","reason",
        "lastRecordKeepDecisionRaw","comparisonLatchRaw","failedCheckCounterRaw","alternatePidRaw",
        "alternatePidMatchesOriginal","actualPredicateBranch","actualPredicateReturn","importedLivenessReturn",
        "namedEventObserved","backendRunningVerified","recoveryAdmission","atomicSnapshot","abaExcluded",
        "accountValuesPersisted","nativePointersPersisted","targetCalls","memoryWrites"};
    for (auto field = inputs.begin(); field != inputs.end(); ++field)
        expect(std::find(allowed.begin(), allowed.end(), field.key()) != allowed.end() &&
            !field.value().is_structured(), "predicateInputs leaf allowlist");
    expect(inputs.at("source") == "cuser-record-liveness-inputs" &&
        inputs.at("profileVersion") == "steamclient64-e9045710-predicate-inputs-v1", "predicate input provenance");
    expect(inputs.at("actualPredicateBranch") == "unknown" && inputs.at("actualPredicateReturn").is_null() &&
        inputs.at("importedLivenessReturn") == "not-collected", "raw inputs never infer branch or liveness");
    for (const auto* key : {"namedEventObserved","backendRunningVerified","recoveryAdmission","atomicSnapshot",
        "abaExcluded","accountValuesPersisted","nativePointersPersisted"})
        expect(inputs.at(key) == false, std::string("predicate no authority: ") + key);
    expect(inputs.at("targetCalls") == 0 && inputs.at("memoryWrites") == 0, "predicate inputs are read only");
    if (inputs.at("available") == true) {
        expect(inputs.at("status") == "observed" && inputs.at("reason") == "predicate-inputs-double-read-equal",
            "predicate observed receipt");
        for (const auto* key : RAW_KEYS)
            expect(inputs.at(key).is_number_unsigned() && inputs.at(key).get<U64>() <= UINT32_MAX, "raw unsigned domain");
        expect(inputs.at(RAW_KEYS[0]).get<unsigned>() <= 1 && inputs.at(RAW_KEYS[1]).get<unsigned>() <= 1,
            "boolean raw domains");
        expect(inputs.at("alternatePidMatchesOriginal").is_boolean(), "alternate equality is explicit");
    } else {
        expect(inputs.at("status") == "unavailable" && inputs.at("available") == false, "nested unknown status");
        for (const auto* key : RAW_KEYS) expect(inputs.at(key).is_null(), "unknown has no partial raw");
        expect(!inputs.contains("alternatePidMatchesOriginal"), "unknown has no alternate comparison");
    }
}
struct Access { U64 address; std::size_t size; unsigned occurrence; };
struct Mock {
    std::map<U64, unsigned char> memory;
    std::map<std::pair<U64, std::size_t>, unsigned> occurrences;
    std::vector<Access> reads;
    std::vector<std::pair<U64, U64>> forbidden;
    std::function<void(Mock&, const Access&)> before;
    unsigned forbiddenReads = 0;
    std::size_t failAt = 0, throwAt = 0;
    void put(U64 address, U64 value, std::size_t size = 8) {
        for (std::size_t i = 0; i < size; ++i) memory[address + i] = static_cast<unsigned char>(value >> (8 * i));
    }
    void fill(U64 address, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) memory[address + i] = 0;
    }
    void forbid(U64 address, U64 size) { forbidden.emplace_back(address, size); }
    bool read(U64 address, void* destination, std::size_t size) {
        const Access access{address, size, ++occurrences[{address, size}]};
        reads.push_back(access);
        for (const auto& range : forbidden) {
            if (address < range.first + range.second && range.first < address + size) {
                ++forbiddenReads;
                throw std::runtime_error("forbidden account/game data read");
            }
        }
        if (before) before(*this, access);
        if (throwAt == reads.size()) throw std::runtime_error("secret callback text " + std::to_string(ACCOUNT));
        if (failAt == reads.size()) return false;
        for (std::size_t i = 0; i < size; ++i) {
            const auto found = memory.find(address + i);
            if (found == memory.end()) return false;
            static_cast<unsigned char*>(destination)[i] = found->second;
        }
        return true;
    }
    unsigned hits(U64 address, std::size_t size) const {
        const auto found = occurrences.find({address, size});
        return found == occurrences.end() ? 0 : found->second;
    }
};
struct Row { U64 game; std::uint32_t primary = PID, foreground = FOREGROUND; };
struct Fixture {
    Mock m;
    Query q{MODULE_BASE, 0x2000000, ACCOUNT, GAME, PID, true};
    unsigned stableCalls = 0;
    std::function<bool(unsigned)> stability;
    Fixture() {
        const unsigned char getter[] = {0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3};
        for (std::size_t i = 0; i < sizeof(getter); ++i) m.put(MODULE_BASE + 0xcfdc0 + i, getter[i], 1);
        m.put(MODULE_BASE + 0x181d008, ENGINE_ADDR);
        m.put(ENGINE_ADDR, MODULE_BASE + 0x1361f38);
        m.put(ENGINE_ADDR + 0xfe0, MODULE_BASE + 0x1361ff0);
        users({ACCOUNT});
        records({{GAME}});
    }
    static U64 user(unsigned index) { return USER_ADDR + 0x10000ULL * index; }
    void users(const std::vector<U64>& accounts) {
        m.put(ENGINE_ADDR + 0x1080, accounts.empty() ? 0 : USERS_ADDR);
        m.put(ENGINE_ADDR + 0x1090, accounts.size(), 4);
        for (unsigned i = 0; i < accounts.size(); ++i) {
            const auto address = user(i);
            m.put(USERS_ADDR + 0x10ULL * i + 8, address);
            m.put(address, MODULE_BASE + 0x136fa80);
            m.put(address + 0x1e70, MODULE_BASE + 0x136fbd0);
            m.put(address + 0x20a, accounts[i]);
        }
    }
    void records(const std::vector<Row>& rows, unsigned selected = 0) {
        m.put(user(selected) + 0x2568, rows.empty() ? 0 : RECORDS_ADDR);
        m.put(user(selected) + 0x2578, rows.size(), 4);
        for (unsigned i = 0; i < rows.size(); ++i) {
            const auto address = RECORDS_ADDR + 0x330ULL * i;
            m.put(address + 8, rows[i].game);
            m.put(address + 0x198, rows[i].foreground, 4);
            m.put(address + 0x19c, rows[i].primary, 4);
        }
    }
    // Opt in only for extension cases; legacy fixtures deliberately lack these anchors.
    void predicateInputs(unsigned index = 0, unsigned char keep = 1, unsigned char latch = 0,
        std::uint32_t counter = 8, std::uint32_t alternate = PID) {
        for (const auto& anchor : PREDICATE_ANCHORS)
            for (std::size_t i = 0; i < anchor.bytes.size(); ++i)
                m.put(MODULE_BASE + anchor.rva + i, anchor.bytes[i], 1);
        const auto record = RECORDS_ADDR + 0x330ULL * index;
        m.put(record + 0x200, keep, 1); m.put(record + 0x201, latch, 1);
        m.put(record + 0x1a8, counter, 4); m.put(record + 0x268, alternate, 4);
    }
    Json run() {
        return sr::observe(q, [&](U64 address, void* destination, std::size_t size) {
            return m.read(address, destination, size);
        }, [&] { ++stableCalls; return stability ? stability(stableCalls) : true; });
    }
    void check(const Json& result) const {
        expect(result["source"] == "steam-native-cuser-record-diagnostic", "source");
        expect(result["authority"] == "diagnostic-only-no-admission", "authority");
        expect(result["profileVersion"] == sr::kProfileVersion, "profile version");
        expect(result["atomicSnapshot"] == false && result["abaExcluded"] == false, "no atomic/ABA claim");
        expect(result["loginGenerationLease"] == "unknown", "no login lease");
        expect(result["recordAbsenceIsNotAtomicProof"] == true, "absence qualification");
        for (const auto* field : {"inputVerified", "continueVerified", "accountValuesPersisted", "otherAccountsRecordsRead"})
            expect(result[field] == false, field);
        expect(result["readCalls"].get<std::size_t>() == m.reads.size(), "attempted read count");
        std::size_t bytes = 0;
        for (const auto& access : m.reads) {
            bytes += access.size;
            expect(access.size > 0 && access.size <= 64, "single read budget");
            expect(access.address >= 0x10000 && access.address <= MAX_ADDR && access.size - 1 <= MAX_ADDR - access.address,
                "canonical read span");
            // Every tuple is fetched in one read, including unaligned fields.
            if (access.address >= RECORDS_ADDR && access.address < RECORDS_ADDR + 256 * 0x330ULL &&
                (access.address - RECORDS_ADDR) % 0x330 >= 0x198 && (access.address - RECORDS_ADDR) % 0x330 <= 0x19c)
                expect((access.address - RECORDS_ADDR) % 0x330 == 0x198 && access.size == 8, "single PID pair read");
        }
        expect(result["readBytes"].get<std::size_t>() == bytes, "attempted read bytes");
        expect(m.reads.size() < 2048 && bytes <= 65536 && m.forbiddenReads == 0, "total budget/data isolation");
        if (result["status"] == "unavailable") {
            expect(result["available"] == false && result["recordState"] == "unknown", "unavailable stays unknown");
            expect(result["originalPrimaryMatched"].is_null(), "no partial match evidence");
            expect(!result.contains("primaryLivenessPid") && !result.contains("foregroundComparisonPid"), "no partial PIDs");
            expect(result["reason"] == "target-record-ambiguous" || result["matchingTargetRecordCount"].is_null(),
                "failed scan is not record absence");
        } else {
            expect(result["status"] == "observed" && result["available"] == true, "observed receipt");
            expect(stableCalls == 2, "before and after external stability");
        }
        checkPredicateInputs(result.at("predicateInputs"));
        if (result["status"] == "unavailable")
            expect(result.at("predicateInputs").at("available") == false, "outer failure revokes all raw");
        const std::vector<std::string> allowed = {"source","profileVersion","authority","status","available","reason",
            "recordState","matchingTargetRecordCount","originalPrimaryMatched","readCalls","readBytes","atomicSnapshot",
            "abaExcluded","loginGenerationLease","recordAbsenceIsNotAtomicProof","inputVerified","continueVerified",
            "accountValuesPersisted","otherAccountsRecordsRead","primaryLivenessPid","foregroundComparisonPid","predicateInputs"};
        for (auto field = result.begin(); field != result.end(); ++field)
            expect(std::find(allowed.begin(), allowed.end(), field.key()) != allowed.end() &&
                (field.key() == "predicateInputs" || !field.value().is_structured()), "serialized field allowlist");
        const auto serialized = result.dump();
        for (const auto secret : {ACCOUNT, OTHER_ACCOUNT, GAME, OTHER_GAME, MODULE_BASE, ENGINE_ADDR, USERS_ADDR, USER_ADDR, RECORDS_ADDR}) {
            expect(serialized.find(std::to_string(secret)) == std::string::npos, "no decimal account/GameID/address");
            expect(serialized.find(hex(secret)) == std::string::npos, "no hex account/GameID/address");
        }
    }
};
void unavailable(Fixture& f, const char* reason) {
    const auto result = f.run();
    f.check(result);
    expect(result["status"] == "unavailable" && result["reason"] == reason, std::string("reason: ") + reason + " got " + result.dump());
}
template<class F> void test(const std::string& name, F&& body) {
    ++cases;
    const auto before = assertions;
    try { body(); caseResults.push_back({{"name",name},{"status","passed"},{"assertions",assertions - before}}); }
    catch (const std::exception& e) {
        ++failures;
        caseResults.push_back({{"name",name},{"status","failed"},{"assertions",assertions - before},{"error",e.what()}});
    }
}
void scenarioTests() {
    test("one original primary", [] {
        Fixture f; const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "present-original-primary" && result["matchingTargetRecordCount"] == 1, "one target");
        expect(result["primaryLivenessPid"] == PID && result["foregroundComparisonPid"] == FOREGROUND &&
            result["originalPrimaryMatched"] == true, "field order and primary comparison");
        expect(f.m.hits(RECORDS_ADDR + 0x198, 8) == 2 && f.m.hits(RECORDS_ADDR + 8, 8) == 4, "two bracketed target tuple reads");
    });
    test("empty selected record vector", [] {
        Fixture f; f.records({}); const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "not-observed" && result["matchingTargetRecordCount"] == 0, "bounded empty observation");
        expect(result["originalPrimaryMatched"].is_null() && !result.contains("primaryLivenessPid"), "no invented PID");
    });
    test("no target and full GameID comparison", [] {
        Fixture f; f.records({{OTHER_GAME}}); f.m.forbid(RECORDS_ADDR + 0x198, 8);
        const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "not-observed" && f.m.hits(RECORDS_ADDR + 8, 8) == 2, "full GameID checked twice");
    });
    for (const auto primary : {0U, PID + 1}) test("different primary even if foreground matches", [primary] {
        Fixture f; f.records({{GAME, primary, PID}}); const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "different-primary" && result["originalPrimaryMatched"] == false &&
            result["primaryLivenessPid"] == primary, "foreground must not substitute for primary");
    });
    for (const auto primary : {PID, PID + 1}) test("duplicate target cannot be ranked", [primary] {
        Fixture f; f.records({{GAME}, {GAME, primary, FOREGROUND}});
        const auto result = f.run(); f.check(result);
        expect(result["status"] == "unavailable" && result["reason"] == "target-record-ambiguous" &&
            result["matchingTargetRecordCount"] == 2, "all duplicate targets ambiguous");
    });
    test("unique selected account and non-target games isolated", [] {
        Fixture f; f.users({OTHER_ACCOUNT, ACCOUNT, ACCOUNT ^ (2ULL << 32)});
        f.records({{OTHER_GAME}, {GAME}, {OTHER_GAME + 1}}, 1);
        for (const auto index : {0U, 2U}) {
            f.m.put(Fixture::user(index) + 0x2568, std::numeric_limits<U64>::max());
            f.m.put(Fixture::user(index) + 0x2578, 0xffffffff, 4);
            f.m.forbid(Fixture::user(index) + 0x2568, 8);
            f.m.forbid(Fixture::user(index) + 0x2578, 4);
        }
        f.m.forbid(RECORDS_ADDR + 0x198, 8); f.m.forbid(RECORDS_ADDR + 2 * 0x330 + 0x198, 8);
        const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "present-original-primary", "selected only by complete account");
    });
    test("same low account bits are insufficient", [] {
        Fixture f; f.users({OTHER_ACCOUNT}); f.m.forbid(USER_ADDR + 0x2568, 8); f.m.forbid(USER_ADDR + 0x2578, 4);
        unavailable(f, "account-not-found");
    });
    for (const auto alias : {false, true}) test("duplicate complete accounts do not read records", [alias] {
        Fixture f; f.users({ACCOUNT, ACCOUNT});
        if (alias) f.m.put(USERS_ADDR + 0x18, USER_ADDR);
        for (unsigned i = 0; i < 2; ++i) { f.m.forbid(Fixture::user(i) + 0x2568, 8); f.m.forbid(Fixture::user(i) + 0x2578, 4); }
        unavailable(f, "account-ambiguous");
    });
    test("empty user selection", [] { Fixture f; f.users({}); unavailable(f, "account-not-found"); });
    test("unknown profile zero memory reads", [] {
        Fixture f; f.q.profileVerified = false; unavailable(f, "profile-unverified");
        expect(f.m.reads.empty() && f.stableCalls == 0, "profile must precede callbacks");
    });
    for (unsigned field = 0; field < 3; ++field) test("missing identity zero reads", [field] {
        Fixture f;
        if (field == 0) f.q.account64 = 0;
        if (field == 1) f.q.gameId64 = 0;
        if (field == 2) f.q.expectedPid = 0;
        unavailable(f, "identity-missing"); expect(f.m.reads.empty(), "missing identity must not read");
    });
    test("missing callbacks", [] {
        Fixture f;
        auto result = sr::observe(f.q, {}, [] { return true; }); f.check(result);
        expect(result["reason"] == "reader-unavailable", "empty reader");
        result = sr::observe(f.q, [](U64, void*, std::size_t) { return true; }, {}); f.check(result);
        expect(result["reason"] == "stability-check-unavailable", "empty stable");
    });
    test("getter mismatch", [] { Fixture f; f.m.put(MODULE_BASE + 0xcfdc0 + 13, 0, 1); unavailable(f, "getter-mismatch"); });
    for (const auto relative : {0ULL, 0xfe0ULL}) test("engine type mismatch", [relative] {
        Fixture f; f.m.put(ENGINE_ADDR + relative, MODULE_BASE + 0x136fa80); unavailable(f, "engine-type-mismatch");
    });
    for (const auto relative : {0ULL, 0x1e70ULL}) test("CUser type mismatch", [relative] {
        Fixture f; f.m.put(USER_ADDR + relative, MODULE_BASE + 0x1361f38); unavailable(f, "user-type-mismatch");
    });
    for (const auto count : {33U, 0xffffffffU}) test("user vector count outside signed bounds", [count] {
        Fixture f; f.m.put(ENGINE_ADDR + 0x1090, count, 4); unavailable(f, "user-vector-count-invalid");
    });
    for (const auto count : {257U, 0xffffffffU}) test("record count outside signed bounds", [count] {
        Fixture f; f.m.put(USER_ADDR + 0x2578, count, 4); unavailable(f, "record-count-invalid");
    });
    test("maximum bounded vectors", [] {
        Fixture f; std::vector<U64> accounts(32, OTHER_ACCOUNT); accounts.back() = ACCOUNT; f.users(accounts);
        f.records(std::vector<Row>(256, Row{GAME}), 31);
        const auto result = f.run(); f.check(result);
        expect(result["reason"] == "target-record-ambiguous" && result["matchingTargetRecordCount"] == 256,
            "maximum records remain ambiguous");
        expect(f.m.hits(RECORDS_ADDR + 255 * 0x330 + 0x198, 8) == 2, "last allowed record read twice");
    });
    test("last allowed module byte", [] {
        Fixture f; f.q.moduleSize = 0x181d008 + 8; const auto result = f.run(); f.check(result);
        expect(result["status"] == "observed", "anchor ends exactly at module size");
    });
    test("module anchor outside image", [] {
        Fixture f; f.q.moduleSize = 0x181d008 + 7; unavailable(f, "module-anchor-out-of-range");
        expect(f.m.reads.empty(), "all anchors validated before reads");
    });
    for (const auto base : {0ULL, 0xffffULL, MAX_ADDR + 1, std::numeric_limits<U64>::max() - 8}) test("invalid module base or wrap", [base] {
        Fixture f; f.q.moduleBase = base; unavailable(f, "module-range-invalid"); expect(f.m.reads.empty(), "invalid image zero reads");
    });
    test("module image span overflow", [] {
        Fixture f; f.q.moduleSize = std::numeric_limits<U64>::max(); unavailable(f, "module-range-invalid");
        expect(f.m.reads.empty(), "overflow zero reads");
    });
    for (const auto pointer : {0ULL, 0xffffULL, MAX_ADDR + 1, std::numeric_limits<U64>::max()}) test("invalid engine address", [pointer] {
        Fixture f; f.m.put(MODULE_BASE + 0x181d008, pointer); unavailable(f, "address-out-of-range");
    });
    test("engine displacement overflow", [] {
        Fixture f; f.m.put(MODULE_BASE + 0x181d008, MAX_ADDR - 7); f.m.put(MAX_ADDR - 7, MODULE_BASE + 0x1361f38);
        unavailable(f, "address-out-of-range");
    });
    test("user displacement overflow", [] {
        Fixture f; f.m.put(USERS_ADDR + 8, MAX_ADDR - 7); f.m.put(MAX_ADDR - 7, MODULE_BASE + 0x136fa80);
        unavailable(f, "address-out-of-range");
    });
    for (const auto pointer : {0ULL, MAX_ADDR - 15, std::numeric_limits<U64>::max()}) test("invalid user vector span", [pointer] {
        Fixture f; f.users({ACCOUNT, OTHER_ACCOUNT}); f.m.put(ENGINE_ADDR + 0x1080, pointer); unavailable(f, "address-out-of-range");
    });
    for (const auto pointer : {0ULL, MAX_ADDR - 0x32f, std::numeric_limits<U64>::max()}) test("invalid record vector span", [pointer] {
        Fixture f; f.records({{GAME}, {OTHER_GAME}}); f.m.put(USER_ADDR + 0x2568, pointer); unavailable(f, "address-out-of-range");
    });
    test("lower canonical record boundary", [] {
        Fixture f; f.m.put(USER_ADDR + 0x2568, 0x10000); f.m.put(0x10008, GAME);
        f.m.put(0x10198, FOREGROUND, 4); f.m.put(0x1019c, PID, 4);
        const auto result = f.run(); f.check(result); expect(result["status"] == "observed", "lower canonical address inclusive");
    });
    test("upper canonical record span boundary", [] {
        Fixture f; const auto last = MAX_ADDR - 0x32f; f.m.put(USER_ADDR + 0x2568, last);
        f.m.put(last + 8, GAME); f.m.put(last + 0x198, FOREGROUND, 4); f.m.put(last + 0x19c, PID, 4);
        const auto result = f.run(); f.check(result); expect(result["status"] == "observed", "upper canonical span inclusive");
    });
    test("RPM failure at each observation read", [] {
        Fixture baseline; baseline.run(); const auto size = baseline.m.reads.size();
        for (std::size_t i = 1; i <= size; ++i) {
            Fixture f; f.m.failAt = i;
            if (baseline.m.reads[i - 1].address == MODULE_BASE + PREDICATE_ANCHORS.front().rva) {
                const auto result = f.run(); f.check(result);
                expect(result["recordState"] == "present-original-primary" &&
                    result["predicateInputs"]["reason"] == "memory-read-failed", "optional failure preserves old record");
                expect(f.m.reads.size() == size, "optional failure continues full legacy revalidation");
            } else {
                unavailable(f, "memory-read-failed");
                expect(f.m.reads.size() == i, "required read aborts immediately");
            }
            expect(f.stableCalls == 2, "all read failures recheck external identity");
        }
    });
    test("callback exception contents suppressed", [] {
        Fixture f; f.m.throwAt = 2; unavailable(f, "memory-read-exception");
    });
}
void raceTests() {
    for (const auto field : {0x2568ULL, 0x2578ULL}) test("record header races after target read", [field] {
        Fixture f; f.m.before = [field](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 1) m.put(USER_ADDR + field, field == 0x2568 ? RECORDS_ADDR + 0x100000 : 2, field == 0x2568 ? 8 : 4);
        }; unavailable(f, "record-header-changed");
    });
    test("selected account races", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198) m.put(USER_ADDR + 0x20a, OTHER_ACCOUNT);
        }; unavailable(f, "account-changed");
    });
    for (const auto field : {0x198ULL, 0x19cULL}) test("in-place tuple races at same address/count", [field] {
        Fixture f; f.m.before = [field](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 2) m.put(RECORDS_ADDR + field, PID + 100, 4);
        }; unavailable(f, "target-records-changed");
    });
    test("full GameID changes around PID pair", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 1) m.put(RECORDS_ADDR + 8, OTHER_GAME);
        }; unavailable(f, "target-gameid-changed");
    });
    test("target disappears on second pass", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == 3) m.put(RECORDS_ADDR + 8, OTHER_GAME);
        }; unavailable(f, "target-records-changed");
    });
    test("target appears on second pass", [] {
        Fixture f; f.records({{OTHER_GAME}}); f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == 2) m.put(RECORDS_ADDR + 8, GAME);
        }; unavailable(f, "target-records-changed");
    });
    test("same-count in-place target move", [] {
        Fixture f; f.records({{GAME}, {OTHER_GAME}}); f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == 3) { m.put(RECORDS_ADDR + 8, OTHER_GAME); m.put(RECORDS_ADDR + 0x330 + 8, GAME); }
        }; unavailable(f, "target-records-changed");
    });
    for (const auto field : {0x1080ULL, 0x1090ULL}) test("user vector header races", [field] {
        Fixture f; f.m.before = [field](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 1) m.put(ENGINE_ADDR + field, field == 0x1080 ? USERS_ADDR + 0x1000 : 0, field == 0x1080 ? 8 : 4);
        }; unavailable(f, "user-selection-changed");
    });
    test("selected slot changes in place", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198) m.put(USERS_ADDR + 8, USER_ADDR + 0x10000);
        }; unavailable(f, "user-selection-changed");
    });
    test("nonselected slot changes in place", [] {
        Fixture f; f.users({ACCOUNT, OTHER_ACCOUNT, OTHER_ACCOUNT});
        f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 2) m.put(USERS_ADDR + 0x18, Fixture::user(2));
        }; unavailable(f, "user-selection-changed");
    });
    test("global engine changes", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 0x198) m.put(MODULE_BASE + 0x181d008, ENGINE_ADDR + 0x10000);
        }; unavailable(f, "user-selection-changed");
    });
    test("getter changes after records", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == MODULE_BASE + 0xcfdc0 && a.occurrence == 2) m.put(MODULE_BASE + 0xcfdc0, 0, 1);
        }; unavailable(f, "getter-mismatch");
    });
    for (unsigned field = 0; field < 3; ++field) test("final header account and selectedU rechecks", [field] {
        Fixture f; f.m.before = [field](Mock& m, const Access& a) {
            if (a.address == MODULE_BASE + 0xcfdc0 && a.occurrence == 2) {
                if (field == 0) m.put(USER_ADDR + 0x2568, RECORDS_ADDR + 0x100000);
                if (field == 1) m.put(USER_ADDR + 0x20a, OTHER_ACCOUNT);
                if (field == 2) m.put(USERS_ADDR + 8, USER_ADDR + 0x10000);
            }
        }; unavailable(f, field == 0 ? "record-header-changed" : field == 1 ? "account-changed" : "user-selection-changed");
    });
    for (const auto account : {false, true}) test("selection changes during final record header read", [account] {
        Fixture f; f.m.before = [account](Mock& m, const Access& a) {
            if (a.address == USER_ADDR + 0x2578 && a.occurrence == 4)
                m.put(account ? USER_ADDR + 0x20a : USERS_ADDR + 8, account ? OTHER_ACCOUNT : USER_ADDR + 0x10000);
        }; unavailable(f, account ? "account-changed" : "user-selection-changed");
    });
    for (const auto call : {1U, 2U}) test("Stable revokes identity", [call] {
        Fixture f; f.stability = [call](unsigned n) { return n != call; }; unavailable(f, "identity-changed");
        expect(call == 2 || f.m.reads.empty(), "initial revocation has zero reads");
    });
    for (const auto call : {1U, 2U}) test("Stable exception is sanitized", [call] {
        Fixture f; f.stability = [call](unsigned n) {
            if (n == call) throw std::runtime_error(std::to_string(ACCOUNT));
            return true;
        }; unavailable(f, "identity-check-failed");
    });
    test("read failure plus identity revocation", [] {
        Fixture f; f.m.failAt = 2; f.stability = [](unsigned n) { return n == 1; }; unavailable(f, "identity-changed");
    });
    test("query cannot be retargeted by reader", [] {
        Fixture f; f.m.before = [&f](Mock&, const Access&) { f.q.account64 = OTHER_ACCOUNT; f.q.gameId64 = OTHER_GAME; f.q.expectedPid = PID + 1; };
        const auto result = f.run(); f.check(result); expect(result["recordState"] == "present-original-primary", "query is copied before callbacks");
    });
    test("ABA remains an explicit limitation", [] {
        Fixture f; f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == 3) m.put(RECORDS_ADDR + 0x19c, PID + 1, 4);
            if (a.address == RECORDS_ADDR + 0x198 && a.occurrence == 2) m.put(RECORDS_ADDR + 0x19c, PID, 4);
        };
        const auto result = f.run(); f.check(result);
        expect(result["status"] == "observed" && result["abaExcluded"] == false && result["atomicSnapshot"] == false,
            "equal sampled values do not exclude ABA");
    });
}
void originalRecord(Fixture& f, const Json& result) {
    f.check(result);
    expect(result["status"] == "observed" && result["reason"] == "target-record-observed" &&
        result["recordState"] == "present-original-primary" && result["matchingTargetRecordCount"] == 1 &&
        result["primaryLivenessPid"] == PID && result["foregroundComparisonPid"] == FOREGROUND &&
        result["originalPrimaryMatched"] == true, "optional extension preserves complete old record");
}
void nestedUnknown(Fixture& f, const char* reason) {
    const auto result = f.run(); originalRecord(f, result);
    expect(result["predicateInputs"]["available"] == false && result["predicateInputs"]["reason"] == reason,
        std::string("nested reason: ") + reason + " got " + result["predicateInputs"].dump());
}
std::pair<std::size_t, std::size_t> extensionSpan(const Mock& m) {
    std::size_t first = 0;
    while (first < m.reads.size() && m.reads[first].address != MODULE_BASE + PREDICATE_ANCHORS.front().rva) ++first;
    auto end = first;
    while (end < m.reads.size() && m.reads[end].address != MODULE_BASE + 0x181d008) ++end;
    expect(first < end && end < m.reads.size() && end - first == 16, "actual optional block has sixteen reads");
    return {first, end};
}
void sameTail(const Mock& actual, const Mock& baseline, std::size_t begin) {
    const auto count = baseline.reads.size() - begin;
    expect(actual.reads.size() >= count, "legacy tail completed");
    const auto offset = actual.reads.size() - count;
    for (std::size_t i = 0; i < count; ++i)
        expect(actual.reads[offset + i].address == baseline.reads[begin + i].address &&
            actual.reads[offset + i].size == baseline.reads[begin + i].size, "same production revalidation tail");
}
void predicateValueTests() {
    test("legacy fixture extension naturally unavailable", [] {
        Fixture f; const auto result = f.run(); originalRecord(f, result);
        expect(result["predicateInputs"]["reason"] == "memory-read-failed", "legacy fixture has no new anchors");
        expect(f.m.hits(RECORDS_ADDR + 0x200, 2) == 0, "missing anchors prevent raw reads");
    });
    for (unsigned char keep = 0; keep <= 1; ++keep) for (unsigned char latch = 0; latch <= 1; ++latch)
        for (const auto counter : {0U, 1U, 8U, UINT32_MAX}) for (const auto alternate : {0U, PID, UINT32_MAX})
        test("raw values keep=" + std::to_string(keep) + " latch=" + std::to_string(latch) +
            " counter=" + std::to_string(counter) + " alternate=" + std::to_string(alternate), [=] {
            Fixture f; f.predicateInputs(0, keep, latch, counter, alternate);
            const auto result = f.run(); originalRecord(f, result); const auto& inputs = result["predicateInputs"];
            expect(inputs["available"] == true && inputs[RAW_KEYS[0]] == keep && inputs[RAW_KEYS[1]] == latch &&
                inputs[RAW_KEYS[2]] == counter && inputs[RAW_KEYS[3]] == alternate &&
                inputs["alternatePidMatchesOriginal"] == (alternate == PID), "exact raw values including unrestricted uint32");
            expect(f.m.hits(RECORDS_ADDR + 0x200, 2) == 2 && f.m.hits(RECORDS_ADDR + 0x201, 1) == 0 &&
                f.m.hits(RECORDS_ADDR + 0x200, 1) == 0, "both boolean bytes share each two-byte read");
            expect(f.m.hits(RECORDS_ADDR + 0x1a8, 4) == 2 && f.m.hits(RECORDS_ADDR + 0x268, 4) == 2 &&
                f.m.hits(RECORDS_ADDR + 8, 8) == 7, "two complete raw tuples bracketed by three full IDs");
        });
    for (const auto type : {0ULL, 2ULL, 255ULL}) test("no game type gate " + std::to_string(type), [type] {
        Fixture f; f.q.gameId64 = (GAME & ~0xff000000ULL) | (type << 24);
        f.records({{f.q.gameId64}}); f.predicateInputs(); const auto result = f.run(); originalRecord(f, result);
        expect(result["predicateInputs"]["available"] == true, "unique full ID needs no type-two restriction");
    });
    for (const auto field : {0x200ULL, 0x201ULL}) for (const auto value : {2U, 128U, 255U})
        for (const auto occurrence : {1U, 2U})
        test("nonboolean flag=" + hex(field) + " value=" + std::to_string(value) + " sample=" +
            std::to_string(occurrence), [=] {
            Fixture f; f.predicateInputs();
            f.m.before = [=](Mock& m, const Access& a) {
                if (a.address == RECORDS_ADDR + 0x200 && a.occurrence == occurrence)
                    m.put(RECORDS_ADDR + field, value, 1);
            };
            nestedUnknown(f, field == 0x200 ? "predicate-keep-decision-nonboolean" : "predicate-comparison-latch-nonboolean");
        });
    for (const auto primary : {0U, PID + 1}) test("raw alternate cannot replace primary " + std::to_string(primary), [primary] {
        Fixture f; f.records({{GAME, primary, PID}}); f.predicateInputs(); const auto result = f.run(); f.check(result);
        expect(result["recordState"] == "different-primary" && result["originalPrimaryMatched"] == false &&
            result["primaryLivenessPid"] == primary && result["predicateInputs"]["available"] == true &&
            result["predicateInputs"]["alternatePidMatchesOriginal"] == true, "alternate equality never promotes old primary");
    });
    test("extension is between first scan and old revalidation", [] {
        Fixture f; f.records({{GAME}, {OTHER_GAME}}); f.predicateInputs(); bool ordered = false;
        f.m.before = [&](Mock& m, const Access& a) {
            if (a.address == MODULE_BASE + PREDICATE_ANCHORS.front().rva) {
                ordered = m.hits(RECORDS_ADDR + 0x198, 8) == 1 && m.hits(RECORDS_ADDR + 0x330 + 8, 8) == 1 &&
                    m.hits(USER_ADDR + 0x2568, 8) == 1 && m.hits(MODULE_BASE + 0x181d008, 8) == 2;
            }
        };
        const auto result = f.run(); originalRecord(f, result); expect(ordered, "entire first scan precedes optional block");
        const auto [first, end] = extensionSpan(f.m); std::size_t bytes = 0;
        for (auto i = first; i < end; ++i) bytes += f.m.reads[i].size;
        expect(bytes == 87 && f.m.reads[end - 1].address == RECORDS_ADDR + 8, "actual helper footprint is 16 calls and 87 bytes");
        for (const auto& anchor : PREDICATE_ANCHORS)
            expect(f.m.hits(MODULE_BASE + anchor.rva, anchor.bytes.size()) == 1, "all seven loaded-code anchors read exactly once");
        measurements["extension"] = {{"actualCalls",end - first},{"actualBytes",bytes},{"reservedCalls",16},{"reservedBytes",96}};
    });
    for (std::size_t anchor = 0; anchor < PREDICATE_ANCHORS.size(); ++anchor)
        for (std::size_t byte = 0; byte < PREDICATE_ANCHORS[anchor].bytes.size(); ++byte)
        test("loaded anchor mismatch index=" + std::to_string(anchor) + " byte=" + std::to_string(byte), [=] {
            Fixture f; f.predicateInputs();
            f.m.put(MODULE_BASE + PREDICATE_ANCHORS[anchor].rva + byte, PREDICATE_ANCHORS[anchor].bytes[byte] ^ 1U, 1);
            nestedUnknown(f, "predicate-profile-anchor-mismatch");
            expect(f.m.hits(RECORDS_ADDR + 0x200, 2) == 0 && f.m.hits(RECORDS_ADDR + 0x1a8, 4) == 0 &&
                f.m.hits(RECORDS_ADDR + 0x268, 4) == 0, "failed code anchor reads no raw fields");
            for (auto later = anchor + 1; later < PREDICATE_ANCHORS.size(); ++later)
                expect(f.m.hits(MODULE_BASE + PREDICATE_ANCHORS[later].rva, PREDICATE_ANCHORS[later].bytes.size()) == 0,
                    "anchor failure stops optional reads");
        });
}
void predicateReadFailureTests() {
    Fixture baseline; baseline.predicateInputs(); const auto result = baseline.run(); originalRecord(baseline, result);
    const auto [first, end] = extensionSpan(baseline.m);
    for (const auto throws : {false, true}) for (std::size_t i = 0; i < baseline.m.reads.size(); ++i)
        test(std::string(throws ? "throw" : "fail") + " complete raw-enabled read=" + std::to_string(i + 1) +
            (i < first ? " initial" : i < end ? " optional" : " final"), [&, i, throws] {
            Fixture f; f.predicateInputs();
            if (throws) f.m.throwAt = i + 1; else f.m.failAt = i + 1;
            const char* reason = throws ? "memory-read-exception" : "memory-read-failed";
            if (i >= first && i < end) {
                nestedUnknown(f, reason); sameTail(f.m, baseline.m, end);
                expect(f.m.reads.size() == first + (i - first + 1) + (baseline.m.reads.size() - end),
                    "each optional read failure stops only the extension");
            } else {
                unavailable(f, reason); expect(f.m.reads.size() == i + 1, "required failure stops and suppresses raw");
                if (i >= end) expect(f.m.hits(RECORDS_ADDR + 0x200, 2) == 2, "final failure revokes a fully collected tuple");
            }
            expect(f.stableCalls == 2, "read failures preserve final external identity check");
        });
}
void predicateRaceTests() {
    for (unsigned field = 0; field < RAW_KEYS.size(); ++field)
        test("changed raw tuple component " + std::to_string(field), [field] {
        Fixture f; f.predicateInputs(); bool changed = false;
        constexpr std::array<U64, 4> offsets = {{0x200, 0x201, 0x1a8, 0x268}};
        f.m.before = [&, field](Mock& m, const Access& a) {
            const auto address = RECORDS_ADDR + (field < 2 ? 0x200 : offsets[field]);
            if (a.address == address && a.occurrence == 2) {
                m.put(RECORDS_ADDR + offsets[field], field == 0 ? 0 : field == 1 ? 1 : field == 2 ? 9 : PID + 1,
                    field < 2 ? 1 : 4); changed = true;
            }
        };
        nestedUnknown(f, "predicate-inputs-changed"); expect(changed, "second sampled tuple really changed");
    });
    for (const auto occurrence : {3U, 4U, 5U}) test("optional full ID changed sample=" + std::to_string(occurrence), [occurrence] {
        Fixture f; f.predicateInputs(); bool changed = false;
        f.m.before = [&](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == occurrence) {
                m.put(RECORDS_ADDR + 8, OTHER_GAME); changed = true;
            } else if (changed) m.put(RECORDS_ADDR + 8, GAME);
        };
        nestedUnknown(f, "predicate-inputs-fullid-changed"); expect(changed, "full 64-bit ID was changed then restored for old tail");
    });
    for (unsigned field = 0; field < 10; ++field) test("final revocation after complete raw field=" + std::to_string(field), [field] {
        Fixture f; f.predicateInputs(); bool changed = false;
        f.m.before = [&, field](Mock& m, const Access& a) {
            if (a.address != RECORDS_ADDR + 8 || a.occurrence != 5) return;
            changed = true;
            if (field == 0) m.put(USER_ADDR + 0x20a, OTHER_ACCOUNT);
            if (field == 1) m.put(USER_ADDR + 0x2568, RECORDS_ADDR + 0x100000);
            if (field == 2) m.put(USER_ADDR + 0x2578, 0, 4);
            if (field == 3) m.put(RECORDS_ADDR + 0x19c, PID + 1, 4);
            if (field == 4) m.put(RECORDS_ADDR + 0x198, FOREGROUND + 1, 4);
            if (field == 5) m.put(USERS_ADDR + 8, USER_ADDR + 0x10000);
            if (field == 6) m.put(MODULE_BASE + 0x181d008, ENGINE_ADDR + 0x10000);
            if (field == 7) m.put(MODULE_BASE + 0xcfdc0, 0, 1);
            if (field == 8) f.q.profileVerified = false;
            if (field == 9) m.put(ENGINE_ADDR + 0x1080, USERS_ADDR + 0x10000);
        };
        if (field == 8) f.stability = [&](unsigned) { return f.q.profileVerified; };
        const auto* reason = field == 0 ? "account-changed" : field <= 2 ? "record-header-changed" :
            field <= 4 ? "target-records-changed" : field == 7 ? "getter-mismatch" :
            field == 8 ? "identity-changed" : "user-selection-changed";
        unavailable(f, reason);
        expect(changed && f.m.hits(RECORDS_ADDR + 0x200, 2) == 2 && f.m.hits(RECORDS_ADDR + 0x268, 4) == 2,
            "revocation follows successful raw collection");
    });
    test("persistent full ID change after complete raw", [] {
        Fixture f; f.predicateInputs();
        f.m.before = [](Mock& m, const Access& a) {
            if (a.address == RECORDS_ADDR + 8 && a.occurrence == 6) m.put(RECORDS_ADDR + 8, OTHER_GAME);
        };
        unavailable(f, "target-records-changed");
        expect(f.m.hits(RECORDS_ADDR + 0x200, 2) == 2, "new raw cannot survive final full-ID revocation");
    });
    for (const auto throws : {false, true}) test(std::string("external final identity ") + (throws ? "throws" : "revoked"), [throws] {
        Fixture f; f.predicateInputs(); f.stability = [throws](unsigned n) {
            if (n == 2 && throws) throw std::runtime_error("secret callback text " + std::to_string(ACCOUNT));
            return n == 1;
        };
        unavailable(f, throws ? "identity-check-failed" : "identity-changed");
        expect(f.m.hits(RECORDS_ADDR + 0x268, 4) == 2, "external identity revokes collected raw");
    });
    for (unsigned field = 0; field < 3; ++field) test("very last recheck revokes raw field=" + std::to_string(field), [field] {
        Fixture f; f.predicateInputs();
        f.m.before = [field](Mock& m, const Access& a) {
            if (a.address == USER_ADDR + 0x2578 && a.occurrence == 4) {
                if (field == 0) m.put(USER_ADDR + 0x20a, OTHER_ACCOUNT);
                if (field == 1) m.put(USERS_ADDR + 8, USER_ADDR + 0x10000);
                if (field == 2) m.put(ENGINE_ADDR + 0x1090, 0, 4);
            }
        };
        unavailable(f, field == 0 ? "account-changed" : "user-selection-changed");
    });
    for (unsigned mode = 0; mode < 4; ++mode) test("unbound records read no extension mode=" + std::to_string(mode), [mode] {
        Fixture f; f.predicateInputs();
        if (mode == 0) f.records({});
        if (mode == 1) f.records({{OTHER_GAME}});
        if (mode >= 2) f.records({{GAME}, {GAME, mode == 2 ? PID : PID + 1, FOREGROUND}});
        for (const auto& anchor : PREDICATE_ANCHORS) f.m.forbid(MODULE_BASE + anchor.rva, anchor.bytes.size());
        for (unsigned i = 0; i < 2; ++i) {
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x200, 2);
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x1a8, 4);
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x268, 4);
        }
        const auto result = f.run(); f.check(result);
        expect(result["predicateInputs"]["available"] == false &&
            result["recordState"] == (mode < 2 ? "not-observed" : "unknown"), "no candidate ranking or raw for unbound record");
    });
    test("unknown profile with complete raw fixture has zero callbacks", [] {
        Fixture f; f.predicateInputs(); f.q.profileVerified = false; unavailable(f, "profile-unverified");
        expect(f.m.reads.empty() && f.stableCalls == 0, "unknown profile precedes even loaded anchors");
    });
}
void predicateBudgetTests() {
    // Seed ONLY the real Reader's accounting to reach otherwise unreachable
    // boundaries. The helper, decoder, guard and subsequent reads are production code.
    for (unsigned mode = 0; mode < 7; ++mode) test("optional reservation boundary mode=" + std::to_string(mode), [mode] {
        Fixture f; f.predicateInputs(); constexpr U64 scratch = 0x300000000; f.m.fill(scratch, 64);
        const sr::Read read = [&](U64 address, void* out, std::size_t size) { return f.m.read(address, out, size); };
        sr::detail::Reader reader(read);
        const std::size_t tailCalls = 162 + 3 * 256, tailBytes = 8 * tailCalls + 6;
        reader.calls = sr::detail::kMaxReadCalls - tailCalls - 16;
        reader.bytes = sr::detail::kMaxReadBytes - tailBytes - 96;
        if (mode == 1) --reader.bytes; // Call budget exactly inclusive.
        if (mode == 2) --reader.calls; // Byte budget exactly inclusive.
        if (mode == 3) ++reader.calls;
        if (mode == 4) ++reader.bytes;
        if (mode == 5) reader.calls = sr::detail::kMaxReadCalls - tailCalls + 1;
        if (mode == 6) reader.bytes = sr::detail::kMaxReadBytes - tailBytes + 1;
        const auto calls = reader.calls, bytes = reader.bytes;
        const auto inputs = sr::detail::observePredicateInputs(reader, f.q, RECORDS_ADDR, tailCalls, tailBytes);
        checkPredicateInputs(inputs);
        if (mode <= 2) {
            expect(inputs["available"] == true && reader.calls == calls + 16 && reader.bytes == bytes + 87,
                "inclusive reservation permits full helper");
            std::array<unsigned char, 14> data{};
            for (std::size_t i = 0; i < tailCalls; ++i) reader.read(scratch, data.data(), i == 0 ? 14 : 8);
            expect(reader.calls == calls + 16 + tailCalls && reader.bytes == bytes + 87 + tailBytes,
                "all reserved legacy-tail calls and bytes remain usable");
            if (mode == 0) {
                expect(reader.calls == sr::detail::kMaxReadCalls && reader.bytes == sr::detail::kMaxReadBytes - 9,
                    "exact inclusive calls retain conservative nine-byte slack");
                measurements["reservationBoundary"] = {{"tailCalls",tailCalls},{"tailBytes",tailBytes},
                    {"finalCalls",reader.calls},{"finalBytes",reader.bytes},{"unusedReservedBytes",9}};
            }
        } else {
            expect(inputs["available"] == false && inputs["reason"] == "predicate-inputs-read-budget-unavailable" &&
                reader.calls == calls && reader.bytes == bytes && f.m.reads.empty(), "insufficient reservation makes zero new reads");
            expect(reader.calls + tailCalls <= sr::detail::kMaxReadCalls || mode == 5, "call shortage is isolated");
            expect(reader.bytes + tailBytes <= sr::detail::kMaxReadBytes || mode == 6, "byte shortage is isolated");
            if (mode == 3 || mode == 4) {
                std::array<unsigned char, 14> data{};
                for (std::size_t i = 0; i < tailCalls; ++i) reader.read(scratch, data.data(), i == 0 ? 14 : 8);
                expect(reader.calls == calls + tailCalls && reader.bytes == bytes + tailBytes &&
                    f.m.reads.size() == tailCalls, "skipping extension preserves all usable legacy-tail budget");
            }
        }
    });
    test("complete maximum vectors unique final record actual budget", [] {
        Fixture f; std::vector<U64> accounts;
        for (unsigned i = 0; i < 31; ++i) accounts.push_back(OTHER_ACCOUNT + i);
        accounts.push_back(ACCOUNT); f.users(accounts);
        std::vector<Row> rows(256, Row{OTHER_GAME}); rows.back().game = GAME; f.records(rows, 31); f.predicateInputs(255);
        for (unsigned i = 0; i < 31; ++i) {
            f.m.forbid(Fixture::user(i) + 0x2568, 8); f.m.forbid(Fixture::user(i) + 0x2578, 4);
        }
        for (unsigned i = 0; i < 255; ++i) {
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x198, 8);
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x200, 2);
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x1a8, 4);
            f.m.forbid(RECORDS_ADDR + 0x330ULL * i + 0x268, 4);
        }
        const auto result = f.run(); originalRecord(f, result); const auto [first, end] = extensionSpan(f.m);
        expect(result["predicateInputs"]["available"] == true && result["readCalls"] == 837 && result["readBytes"] == 6615,
            "bounded 32-user 256-record unique target actual footprint");
        expect(f.m.reads.size() - end <= 162 + 3 * 256 && first == 401 && f.m.reads.size() - end == 420,
            "reserved old-tail bound contains actual complete revalidation");
        expect(f.m.hits(RECORDS_ADDR + 255 * 0x330 + 8, 8) == 7 &&
            f.m.hits(RECORDS_ADDR + 255 * 0x330 + 0x200, 2) == 2, "last record receives optional raw reads");
        for (unsigned i = 0; i < 255; ++i)
            expect(f.m.hits(RECORDS_ADDR + 0x330ULL * i + 8, 8) == 2, "every non-target full ID scanned both times");
        measurements["maximumUniqueRecord"] = {{"users",32},{"records",256},{"calls",result["readCalls"]},
            {"bytes",result["readBytes"]},{"prefixCalls",first},{"actualTailCalls",f.m.reads.size() - end},
            {"reservedTailCalls",162 + 3 * 256},{"reservedTailBytes",8 * (162 + 3 * 256) + 6}};
    });
}
void budgetTests() {
    // Directly exercise the SAME guard used by observe: valid vector bounds make
    // its current field walk smaller than the budget, so no oversized fixture or
    // alternate implementation is needed to reach each guard boundary.
    test("strict read call budget", [] {
        Mock m; m.fill(0x300000000, 64);
        const sr::Read read = [&](U64 p, void* out, std::size_t n) { return m.read(p, out, n); };
        sr::detail::Reader reader(read); unsigned char data = 0;
        for (unsigned i = 0; i < 2047; ++i) reader.read(0x300000000, &data, 1);
        bool rejected = false;
        try { reader.read(0x300000000, &data, 1); }
        catch (const sr::detail::Rejected& e) { rejected = std::string(e.reason) == "read-budget-exceeded"; }
        expect(rejected && reader.calls == 2047 && m.reads.size() == 2047, "2048th call never reaches callback");
    });
    test("read byte budget inclusive", [] {
        Mock m; m.fill(0x300000000, 64);
        const sr::Read read = [&](U64 p, void* out, std::size_t n) { return m.read(p, out, n); };
        sr::detail::Reader reader(read); std::array<unsigned char, 64> data{};
        for (unsigned i = 0; i < 1024; ++i) reader.read(0x300000000, data.data(), 64);
        bool rejected = false;
        try { reader.read(0x300000000, data.data(), 1); }
        catch (const sr::detail::Rejected& e) { rejected = std::string(e.reason) == "read-budget-exceeded"; }
        expect(rejected && reader.bytes == 65536 && m.reads.size() == 1024, "65537th byte never reaches callback");
    });
    for (const auto size : {std::size_t(0), std::size_t(65), std::numeric_limits<std::size_t>::max()}) test("single read bounds", [size] {
        Mock m; const sr::Read read = [&](U64 p, void* out, std::size_t n) { return m.read(p, out, n); };
        sr::detail::Reader reader(read); std::array<unsigned char, 64> data{}; bool rejected = false;
        try { reader.read(0x10000, data.data(), size); }
        catch (const sr::detail::Rejected& e) { rejected = std::string(e.reason) == "read-size-invalid"; }
        expect(rejected && m.reads.empty() && reader.calls == 0, "invalid read size zero callback reads");
    });
    test("canonical read endpoint cannot wrap", [] {
        Mock m; const sr::Read read = [&](U64 p, void* out, std::size_t n) { return m.read(p, out, n); };
        sr::detail::Reader reader(read); std::array<unsigned char, 8> data{}; bool rejected = false;
        try { reader.read(MAX_ADDR - 6, data.data(), data.size()); }
        catch (const sr::detail::Rejected& e) { rejected = std::string(e.reason) == "address-out-of-range"; }
        expect(rejected && m.reads.empty(), "entire read must be canonical");
    });
}
} // namespace
int main() {
    try {
        scenarioTests(); raceTests(); predicateValueTests(); predicateReadFailureTests();
        predicateRaceTests(); predicateBudgetTests(); budgetTests();
        std::cout << Json({{"status",failures == 0 ? "passed" : "failed"},{"cases",cases},{"assertions",assertions},
            {"failedCases",failures},{"caseResults",caseResults},{"measurements",measurements},
            {"realProcessReads",0},{"privateInterfaceCalls",0},{"writesToSteam",0},
            {"atomicSnapshotVerified",false},{"sleepRecoveryVerified",false}}).dump() << '\n';
        return failures == 0 ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
