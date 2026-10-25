#include "../native/steam_native_app_state_diagnostic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace state = ymcc::steamappstate;
using Address = std::uint64_t;
constexpr Address kBase = 0x180000000ULL, kOwner = 0x200000000ULL, kController = 0x200010000ULL;
constexpr Address kHeader = kController + 0x350, kBuckets = 0x200020000ULL, kEntries = 0x200030000ULL;
constexpr Address kApp = 0x200040000ULL, kWords = 0x200050000ULL;
constexpr std::uint32_t kKey = 2399932135U, kEnd = 0xffffffffU;
std::vector<unsigned char> lookupBytes, bitmapBytes, publicBitmapBytes;
unsigned passed = 0, failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void test(const std::string& name, const std::function<void()>& body) {
    try {
        body();
        ++passed;
        std::cout << state::Json({{"case", name}, {"passed", true}}).dump() << '\n';
    } catch (const std::exception& error) {
        ++failed;
        std::cout << state::Json({{"case", name}, {"passed", false}, {"error", error.what()}}).dump() << '\n';
    }
}
std::vector<unsigned char> load(const std::filesystem::path& path, std::size_t expected) {
    std::ifstream input(path, std::ios::binary);
    expect(static_cast<bool>(input), "fixture open failed");
    std::vector<unsigned char> result((std::istreambuf_iterator<char>(input)), {});
    expect(result.size() == expected, "fixture length mismatch");
    return result;
}
struct Fixture {
    state::Query query{kBase, 0x1300000, kKey, true};
    std::map<Address, unsigned char> memory;
    std::map<Address, unsigned> occurrences;
    std::vector<std::pair<Address, std::size_t>> reads;
    std::function<void(Address, unsigned)> beforeRead;
    std::function<bool(unsigned)> identity;
    Address failingAddress = 0, throwingAddress = 0;
    std::size_t attemptedBytes = 0;
    unsigned stableCalls = 0;
    void put(Address address, std::uint64_t value, unsigned size) {
        for (unsigned index = 0; index < size; ++index) memory[address + index] = static_cast<unsigned char>(value >> (8 * index));
    }
    void put32(Address address, std::uint32_t value) { put(address, value, 4); }
    void put64(Address address, std::uint64_t value) { put(address, value, 8); }
    void bytes(Address address, const std::vector<unsigned char>& data) {
        for (std::size_t index = 0; index < data.size(); ++index) memory[address + index] = data[index];
    }
    void table(std::uint32_t count) {
        put32(kHeader + 0x10, count);
        for (std::uint32_t bucket = 0; bucket < count; ++bucket) put32(kBuckets + 4ULL * bucket, kEnd);
    }
    void node(std::uint32_t index, std::uint32_t key, Address app, std::uint32_t next = kEnd) {
        const auto address = kEntries + 24ULL * index;
        put32(address, key);
        put64(address + 8, app);
        put32(address + 0x10, next);
    }
    void app(Address address, std::uint32_t flags) {
        put64(address, kBase + (query.profile == state::ProfileId::Public1792618285 ? 0xda95c8 : 0xda91a8));
        put32(address + 0x18, 123);
        put32(address + 0x20, flags);
    }
    explicit Fixture(state::ProfileId profile = state::ProfileId::Legacy1791918634) {
        query.profile = profile;
        bytes(kBase + 0x17a0f0, lookupBytes);
        bytes(kBase + 0xbde90, bitmapBytes);
        bytes(kBase + 0x639770, {0x48, 0x8b, 0x05, 0xe9, 0xcb, 0xb5, 0x00, 0xc3});
        bytes(kBase + 0xec3c0, {0x8b, 0x41, 0x20, 0xc3});
        put64(kBase + 0xdb02d8, kBase + 0x627480);
        put64(kBase + 0xda91f8, kBase + 0xec3c0);
        put64(kBase + 0x1196360, kOwner);
        put64(kOwner + 0xe58, kController);
        put64(kController, kBase + 0xdb0888);
        put64(kController + 8, kBase + 0xdb08b8);
        put64(kController + 0x20, kBase + 0xdb02d0);
        put64(kHeader, kBuckets);
        put64(kHeader + 0x30, kEntries);
        put32(kHeader + 0x44, 1);
        put32(kHeader + 0x4c, 0);
        put32(kHeader + 0x54, 0);
        put32(kHeader + 0x18, 32);
        put32(kHeader + 0x1c, 1);
        put32(kHeader + 0x20, 0);
        table(16);
        put32(kBuckets + 4 * 4, 0);
        node(0, kKey, kApp);
        app(kApp, 0x2001);
        if (profile == state::ProfileId::Public1792618285) {
            // These values come from the separately verified new PE fixture.
            bytes(kBase + 0xbde90, publicBitmapBytes);
            bytes(kBase + 0x639890, {0x48,0x8b,0x05,0xc9,0xca,0xb5,0x00,0xc3});
            put64(kBase + 0xdb0728, kBase + 0x6275a0);
            put64(kBase + 0xda9618, kBase + 0xec3c0);
            put64(kController, kBase + 0xdb0cd8);
            put64(kController + 8, kBase + 0xdb0d08);
            put64(kController + 0x20, kBase + 0xdb0720);
        }
    }
    bool read(Address address, void* destination, std::size_t size) {
        reads.emplace_back(address, size);
        attemptedBytes += size;
        const auto occurrence = ++occurrences[address];
        expect(address + size <= kHeader + 0x38 || address >= kHeader + 0x3c, "unproved capacity was read");
        if (beforeRead) beforeRead(address, occurrence);
        if (address == throwingAddress) throw std::runtime_error("PRIVATE_CALLBACK_EXCEPTION");
        auto* output = static_cast<unsigned char*>(destination);
        for (std::size_t index = 0; index < size; ++index) {
            const auto found = memory.find(address + index);
            if (found == memory.end()) return false;
            output[index] = found->second;
            if (address == failingAddress) return false;
        }
        return true;
    }
    state::Json run(bool withRead = true, bool withStable = true) {
        state::Read reader = [this](Address address, void* output, std::size_t size) { return read(address, output, size); };
        state::Stable stable = [this]() { ++stableCalls; return identity ? identity(stableCalls) : true; };
        const auto result = state::observe(query, withRead ? reader : state::Read{}, withStable ? stable : state::Stable{});
        expect(result.at("authority") == "diagnostic-only-no-admission", "authority changed");
        for (const auto* flag : {"atomicSnapshot", "abaExcluded", "nativeCapacityVerified", "uniqueLiveControllerVerified", "loadedImageFullyHashed", "inputVerified", "continueVerified"}) {
            expect(result.at(flag) == false, std::string("overclaim: ") + flag);
        }
        expect(result.at("controllerGenerationLease") == "unknown" && result.at("loginGenerationLease") == "unknown", "lease overclaim");
        for (const auto* field : {"sameLiveCallbackObject", "registrationLiveness", "objectConstructionComplete"}) expect(result.at(field) == "unknown", "type binding overclaim");
        expect(result.at("readCalls") == reads.size() && result.at("readBytes") == attemptedBytes, "accounting mismatch");
        expect(reads.size() <= 1024 && attemptedBytes <= 16384, "budget exceeded");
        expect(occurrences[kHeader + 0x38] == 0, "capacity accessed");
        return result;
    }
};
void observed(const state::Json& result, std::uint32_t flags) {
    expect(result.at("status") == "observed" && result.at("available") == true, result.dump());
    expect(result.at("nativeAppFlags").is_number_unsigned() && result.at("nativeAppFlags") == flags, "flags mismatch");
    expect(result.at("runningBitSet") == ((flags & 0x2000U) != 0), "bit mismatch");
}
void rejected(const state::Json& result, const std::string& reason) {
    expect(result.at("status") == "unavailable" && result.at("available") == false, result.dump());
    expect(result.at("nativeAppFlags").is_null() && result.at("runningBitSet").is_null(), "failure fabricated flags");
    expect(result.at("reason") == reason, result.dump());
    expect(result.dump().find("PRIVATE_") == std::string::npos, "callback exception escaped");
}
void rejection(const std::string& reason, const std::function<void()>& body) {
    try { body(); }
    catch (const state::detail::Rejected& error) { expect(error.reason == reason, error.reason); return; }
    throw std::runtime_error("expected internal rejection");
}
void inlineFallback(Fixture& fixture) {
    fixture.query.appid = 1;
    fixture.table(8);
    fixture.node(0, 1, kApp);
    fixture.put32(kBuckets + 3 * 4, 0);
}
void pointerFallback(Fixture& fixture) {
    fixture.query.appid = 12;
    fixture.table(128);
    fixture.node(0, 12, kApp);
    fixture.put32(kBuckets + 51 * 4, 0);
    fixture.put32(kHeader + 0x18, 128);
    fixture.put32(kHeader + 0x1c, 4);
    fixture.put64(kHeader + 0x28, kWords);
    fixture.put32(kWords + 4, 0);
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: selftest <frozen-fixtures-directory>\n"; return 2; }
    try {
        lookupBytes = load(std::filesystem::path(argv[1]) / "17a0f0-17a1a6.bin", 182);
        bitmapBytes = load(std::filesystem::path(argv[1]) / "bde90-bdf00.bin", 112);
        publicBitmapBytes = load(std::filesystem::path(argv[1]) / "public1792618285-bde90-bdf00.bin", 112);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
    test("exact-profile-selection-and-unknown", [] {
        expect(state::profileIdForSha256(state::kUiSha256) == state::ProfileId::Legacy1791918634, "legacy selector");
        expect(state::profileIdForSha256(state::kUiSha256Public1792618285) == state::ProfileId::Public1792618285, "public selector");
        expect(!state::profileIdForSha256("") && !state::profileIdForSha256("unknown"), "unknown selected");
        expect(state::profileSha256(static_cast<state::ProfileId>(999)) == nullptr, "invalid profile hash");
    });
    test("invalid-profile-enum-no-reads", [] {
        Fixture fixture; fixture.query.profile=static_cast<state::ProfileId>(999);
        rejected(fixture.run(), "profile-unverified"); expect(fixture.reads.empty(), "invalid profile read");
    });
    for (const auto flags : {8196u,4u}) test("public-profile-flags-"+std::to_string(flags), [flags] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.put32(kApp+0x20,flags);
        const auto result=fixture.run(); observed(result,flags);
        expect(result.at("profileVersion")=="steamui-cd9226db-app-state-v1", "wrong profile tag");
    });
    test("legacy-image-with-public-profile-rejected", [] {
        Fixture fixture; fixture.query.profile=state::ProfileId::Public1792618285;
        rejected(fixture.run(),"loaded-anchor-mismatch");
    });
    test("public-image-with-legacy-profile-rejected", [] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.query.profile=state::ProfileId::Legacy1791918634;
        rejected(fixture.run(),"loaded-anchor-mismatch");
    });
    test("public-leaf-anchor-corrupt", [] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.put32(kBase+0x639890,0);
        rejected(fixture.run(),"loaded-anchor-mismatch");
    });
    test("public-controller-legacy-vtable-rejected", [] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.put64(kController,kBase+0xdb0888);
        rejected(fixture.run(),"controller-primary-type-mismatch");
    });
    test("public-callback-legacy-target-rejected", [] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.put64(kBase+0xdb0728,kBase+0x627480);
        rejected(fixture.run(),"callback-slot-mismatch");
    });
    test("public-app-legacy-vtable-rejected", [] {
        Fixture fixture(state::ProfileId::Public1792618285); fixture.put64(kApp,kBase+0xda91a8);
        rejected(fixture.run(),"app-type-mismatch");
    });
    test("public-flags-drift-rejected", [] {
        Fixture fixture(state::ProfileId::Public1792618285);
        fixture.beforeRead=[&](Address address,unsigned occurrence){if(address==kApp+0x20&&occurrence==2)fixture.put32(address,4);};
        rejected(fixture.run(),"app-flags-changed");
    });
    test("independent-hash-golden-vectors", [] {
        const std::pair<std::uint32_t, std::uint32_t> vectors[] = {{0, 0}, {1, 0x514e28b7}, {2399932135U, 0x72787e84},
            {4131894616U, 0xcae4ca21}, {0xffffffffU, 0x81f16f39}, {0x80000000U, 0x6d3c65a0}, {12, 0x7c88ad73}};
        for (const auto& vector : vectors) expect(state::detail::hashAppId(vector.first) == vector.second, "hash vector mismatch");
    });
    test("frozen-anchors-triple-type-and-high-bit-hit", [] {
        Fixture fixture;
        observed(fixture.run(), 0x2001);
        expect(fixture.stableCalls == 2, "identity bracket missing");
        for (const auto address : {kController, kController + 8, kController + 0x20, kApp, kApp + 0x18, kApp + 0x20}) {
            expect(fixture.occurrences[address] == 2, "double traversal missing");
        }
    });
    for (const auto flags : {0U, 1U, 0x2000U, 0xffffffffU}) test("flags-uint32-" + std::to_string(flags), [flags] {
        Fixture fixture; fixture.put32(kApp + 0x20, flags); observed(fixture.run(), flags);
    });
    for (const auto& entry : std::vector<std::pair<std::uint32_t, unsigned>>{{1, 7}, {4131894616U, 1}, {0x80000000U, 0}, {0xffffffffU, 9}}) {
        test("appid-" + std::to_string(entry.first), [entry] {
            Fixture fixture; fixture.query.appid = entry.first; fixture.table(16);
            fixture.node(0, entry.first, kApp); fixture.put32(kBuckets + entry.second * 4ULL, 0);
            observed(fixture.run(), 0x2001);
        });
    }
    test("real-bucket-collision-exact-key", [] {
        Fixture fixture;
        expect((state::detail::hashAppId(7) & 15) == 4, "collision fixture incorrect");
        fixture.node(0, 7, 0, 1); fixture.node(1, kKey, kApp);
        observed(fixture.run(), 0x2001);
    });
    test("collision-miss-does-not-borrow-flags", [] {
        Fixture fixture; fixture.node(0, 7, kApp); fixture.put32(kHeader + 0x54, 8);
        rejected(fixture.run(), "target-app-not-observed"); expect(fixture.occurrences[kApp] == 0, "wrong app dereferenced");
    });
    test("inline-bitmap-fallback", [] { Fixture fixture; inlineFallback(fixture); observed(fixture.run(), 0x2001); });
    test("pointer-bitmap-word-one-fallback", [] { Fixture fixture; pointerFallback(fixture); observed(fixture.run(), 0x2001); });
    test("inline-bitmap-set-stops", [] {
        Fixture fixture; inlineFallback(fixture); fixture.put32(kHeader + 0x20, 1U << 3);
        rejected(fixture.run(), "target-app-not-observed"); expect(fixture.occurrences[kEntries] == 0, "crossed bitmap stop");
    });
    test("pointer-bitmap-set-stops", [] {
        Fixture fixture; pointerFallback(fixture); fixture.put32(kWords + 4, 1U << 19);
        rejected(fixture.run(), "target-app-not-observed"); expect(fixture.occurrences[kEntries] == 0, "crossed bitmap stop");
    });
    test("fallback-mask-bound", [] {
        Fixture fixture; inlineFallback(fixture); fixture.put32(kHeader + 0x54, 4);
        rejected(fixture.run(), "target-app-not-observed"); expect(fixture.occurrences[kHeader + 0x18] == 0, "bitmap read past bound");
    });
    test("fallback-bucket-bound", [] {
        Fixture fixture; inlineFallback(fixture); fixture.put32(kHeader + 0x4c, 4);
        rejected(fixture.run(), "target-app-not-observed");
    });
    test("fallback-no-progress-bounded", [] {
        Fixture fixture; fixture.table(1); rejected(fixture.run(), "fallback-no-progress"); expect(fixture.reads.size() < 40, "no-progress loop");
    });
    test("minus-one-is-miss", [] {
        Fixture fixture; fixture.put32(kBuckets + 16, kEnd); fixture.put32(kHeader + 0x54, 8);
        rejected(fixture.run(), "target-app-not-observed");
    });
    for (const auto index : {0xfffffffeU, 65536U}) test("invalid-index-" + std::to_string(index), [index] {
        Fixture fixture; fixture.put32(kBuckets + 16, index); rejected(fixture.run(), "node-index-invalid");
    });
    test("invalid-next-even-on-hit", [] {
        Fixture fixture; fixture.put32(kEntries + 0x10, 0xfffffffeU); rejected(fixture.run(), "node-next-invalid");
    });
    test("cycle-rejected", [] {
        Fixture fixture; fixture.node(0, 7, 0, 1); fixture.node(1, 8, 0, 0); rejected(fixture.run(), "node-chain-cycle");
    });
    test("chain-budget-128", [] {
        Fixture fixture;
        for (unsigned index = 0; index < 128; ++index) fixture.node(index, 7, 0, index + 1);
        rejected(fixture.run(), "node-chain-budget-exceeded"); expect(fixture.occurrences[kEntries + 128 * 24] == 0, "node budget exceeded");
    });
    test("chain-hit-at-budget", [] {
        Fixture fixture;
        for (unsigned index = 0; index < 127; ++index) fixture.node(index, 7, 0, index + 1);
        fixture.node(127, kKey, kApp); observed(fixture.run(), 0x2001);
    });
    test("index-bound-is-not-native-capacity", [] {
        Fixture fixture; fixture.node(65535, kKey, kApp); fixture.put32(kBuckets + 16, 65535);
        observed(fixture.run(), 0x2001);
    });
    for (const auto count : {0U, 3U, 65537U, 0x80000000U}) test("invalid-bucket-count-" + std::to_string(count), [count] {
        Fixture fixture; fixture.put32(kHeader + 0x10, count); rejected(fixture.run(), "bucket-count-invalid");
    });
    test("runtime-count-not-constructor-invariant", [] {
        Fixture fixture; fixture.table(32); fixture.put32(kBuckets + 4 * 4, 0); observed(fixture.run(), 0x2001);
    });
    const std::vector<std::tuple<std::string, Address, std::uint32_t, std::string>> fieldFailures = {
        {"zero-gate", kHeader + 0x44, 0, "target-app-not-observed"}, {"negative-gate", kHeader + 0x44, 0xffffffffU, "entry-gate-invalid"},
        {"bad-bucket-lower", kHeader + 0x4c, 0xffffffffU, "fallback-bound-invalid"}, {"bad-mask-lower", kHeader + 0x54, 65536, "fallback-bound-invalid"},
        {"empty-hit-guard", kApp + 0x18, 0, "app-hit-guard-empty"}};
    for (const auto& entry : fieldFailures) test(std::get<0>(entry), [entry] {
        Fixture fixture; fixture.put32(std::get<1>(entry), std::get<2>(entry)); rejected(fixture.run(), std::get<3>(entry));
    });
    const std::vector<std::pair<Address, std::string>> typeFields = {{kController, "controller-primary-type-mismatch"},
        {kController + 8, "controller-secondary-type-mismatch"}, {kController + 0x20, "controller-callback-type-mismatch"},
        {kApp, "app-type-mismatch"}, {kBase + 0xdb02d8, "callback-slot-mismatch"}, {kBase + 0xda91f8, "app-getter-slot-mismatch"}};
    for (const auto& entry : typeFields) {
        test("type-reject-" + entry.second, [entry] { Fixture fixture; fixture.put64(entry.first, kBase); rejected(fixture.run(), entry.second); });
        test("type-drift-" + entry.second, [entry] {
            Fixture fixture;
            fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == entry.first && occurrence == 2) fixture.put64(address, kBase); };
            rejected(fixture.run(), entry.second);
        });
    }
    for (const auto address : {kBase + 0x17a0f0, kBase + 0xbde90, kBase + 0x639770, kBase + 0xec3c0}) {
        test("loaded-byte-mismatch-" + std::to_string(address), [address] {
            Fixture fixture; fixture.memory[address] ^= 1; rejected(fixture.run(), "loaded-anchor-mismatch");
        });
        test("loaded-byte-drift-" + std::to_string(address), [address] {
            Fixture fixture;
            fixture.beforeRead = [&](Address actual, unsigned occurrence) { if (actual == address && occurrence == 2) fixture.memory[address] ^= 1; };
            rejected(fixture.run(), "loaded-anchor-mismatch");
        });
    }
    for (const auto address : {kBase + 0x1196360, kOwner + 0xe58, kHeader, kHeader + 0x30, kEntries + 8}) {
        for (const auto value : {Address{0}, Address{0x800000000000ULL}, Address{0x200000003ULL}}) {
            test("bad-pointer-" + std::to_string(address) + "-" + std::to_string(value), [address, value] {
                Fixture fixture; fixture.put64(address, value); rejected(fixture.run(), "pointer-invalid");
            });
        }
    }
    test("owner-offset-overflow", [] {
        Fixture fixture; fixture.put64(kBase + 0x1196360, 0x7ffffffffff8ULL); rejected(fixture.run(), "address-out-of-range");
    });
    test("table-range-overflow", [] {
        Fixture fixture; fixture.put64(kHeader, 0x7ffffffffffcULL); rejected(fixture.run(), "table-address-out-of-range");
    });
    test("node-range-overflow", [] {
        Fixture fixture; fixture.put64(kHeader + 0x30, 0x7ffffffffff8ULL); rejected(fixture.run(), "node-address-out-of-range");
    });
    test("entry-index-offset-overflow", [] {
        Fixture fixture; fixture.put64(kHeader + 0x30, 0x7ffffffffff8ULL); fixture.put32(kBuckets + 16, 1);
        rejected(fixture.run(), "address-out-of-range");
    });
    for (const auto& entry : std::vector<std::tuple<std::string, Address, unsigned, std::string>>{
        {"bitmap-empty", kHeader + 0x18, 0, "bitmap-index-invalid"}, {"bitmap-short", kHeader + 0x18, 3, "bitmap-index-invalid"},
        {"bitmap-huge", kHeader + 0x18, 65537, "bitmap-index-invalid"}, {"bitmap-mode-zero", kHeader + 0x1c, 0, "bitmap-mode-invalid"},
        {"bitmap-mode-huge", kHeader + 0x1c, 65537, "bitmap-mode-invalid"}, {"bitmap-inline-too-large", kHeader + 0x18, 33, "bitmap-inline-range-invalid"}}) {
        test(std::get<0>(entry), [entry] {
            Fixture fixture; inlineFallback(fixture); fixture.put32(std::get<1>(entry), std::get<2>(entry)); rejected(fixture.run(), std::get<3>(entry));
        });
    }
    test("bitmap-pointer-invalid", [] {
        Fixture fixture; pointerFallback(fixture); fixture.put64(kHeader + 0x28, 0); rejected(fixture.run(), "pointer-invalid");
    });
    test("profile-unverified-no-reads", [] {
        Fixture fixture; fixture.query.profileVerified = false; rejected(fixture.run(), "profile-unverified"); expect(fixture.reads.empty(), "unexpected read");
    });
    test("appid-missing-no-reads", [] {
        Fixture fixture; fixture.query.appid = 0; rejected(fixture.run(), "appid-missing"); expect(fixture.reads.empty(), "unexpected read");
    });
    test("reader-missing", [] { Fixture fixture; rejected(fixture.run(false), "reader-unavailable"); expect(fixture.stableCalls == 0, "unexpected stable"); });
    test("stable-missing-no-reads", [] { Fixture fixture; rejected(fixture.run(true, false), "stability-check-unavailable"); expect(fixture.reads.empty(), "unexpected read"); });
    for (const auto& range : std::vector<std::pair<Address, Address>>{{0, 0x1300000}, {kBase, 0}, {0x7fffffffffffULL, 2}, {kBase, 0xffffffffffffffffULL}}) {
        test("module-range-" + std::to_string(range.first) + "-" + std::to_string(range.second), [range] {
            Fixture fixture; fixture.query.moduleBase = range.first; fixture.query.moduleSize = range.second;
            rejected(fixture.run(), "module-range-invalid"); expect(fixture.reads.empty(), "unexpected read");
        });
    }
    test("module-anchor-outside-image", [] {
        Fixture fixture; fixture.query.moduleSize = 0x1196367; rejected(fixture.run(), "module-anchor-out-of-range"); expect(fixture.reads.empty(), "unexpected read");
    });
    for (const auto pass : {1U, 2U}) {
        test("identity-false-" + std::to_string(pass), [pass] {
            Fixture fixture; fixture.identity = [pass](unsigned occurrence) { return occurrence != pass; };
            rejected(fixture.run(), "identity-changed"); expect(fixture.stableCalls == pass, "identity count mismatch");
            if (pass == 1) expect(fixture.reads.empty(), "read despite unstable identity");
        });
        test("identity-throws-" + std::to_string(pass), [pass] {
            Fixture fixture; fixture.identity = [pass](unsigned occurrence) { if (occurrence == pass) throw std::runtime_error("PRIVATE_IDENTITY"); return true; };
            rejected(fixture.run(), "identity-check-failed"); expect(fixture.stableCalls == pass, "identity count mismatch");
        });
    }
    test("partial-failed-read", [] {
        Fixture fixture; fixture.failingAddress = kApp + 0x20; rejected(fixture.run(), "memory-read-failed"); expect(fixture.stableCalls == 2, "missing failure bracket");
    });
    test("throwing-read-sanitized", [] {
        Fixture fixture; fixture.throwingAddress = kApp + 0x20; rejected(fixture.run(), "memory-read-exception"); expect(fixture.stableCalls == 2, "missing failure bracket");
    });
    test("unmapped-read", [] {
        Fixture fixture; fixture.memory.erase(kApp + 0x21); rejected(fixture.run(), "memory-read-failed");
    });
    test("flags-drift", [] {
        Fixture fixture; fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kApp + 0x20 && occurrence == 2) fixture.put32(address, 0); };
        rejected(fixture.run(), "app-flags-changed");
    });
    for (const auto address : {kHeader + 0x44, kHeader + 0x4c, kHeader + 0x54, kApp + 0x18}) test("transcript-drift-" + std::to_string(address), [address] {
        Fixture fixture; fixture.beforeRead = [&](Address actual, unsigned occurrence) { if (actual == address && occurrence == 2) fixture.put32(address, 2); };
        rejected(fixture.run(), "lookup-observation-changed");
    });
    test("node-key-drift", [] {
        Fixture fixture; fixture.put32(kHeader + 0x54, 8);
        fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kEntries && occurrence == 2) fixture.put32(address, 7); };
        rejected(fixture.run(), "target-app-not-observed");
    });
    test("app-pointer-drift-same-flags", [] {
        Fixture fixture; fixture.app(kApp + 0x100, 0x2001);
        fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kEntries + 8 && occurrence == 2) fixture.put64(address, kApp + 0x100); };
        rejected(fixture.run(), "lookup-observation-changed");
    });
    test("global-owner-drift-same-controller", [] {
        Fixture fixture; fixture.put64(kOwner + 0x1000 + 0xe58, kController);
        fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kBase + 0x1196360 && occurrence == 2) fixture.put64(address, kOwner + 0x1000); };
        rejected(fixture.run(), "lookup-observation-changed");
    });
    test("controller-drift-valid-type-same-table", [] {
        Fixture fixture;
        const auto original = fixture.memory;
        for (const auto& byte : original) if (byte.first >= kController && byte.first < kController + 0x400) fixture.memory[byte.first + 0x1000] = byte.second;
        fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kOwner + 0xe58 && occurrence == 2) fixture.put64(address, kController + 0x1000); };
        rejected(fixture.run(), "lookup-observation-changed");
    });
    test("bitmap-drift-same-clear-target-bit", [] {
        Fixture fixture; inlineFallback(fixture);
        fixture.beforeRead = [&](Address address, unsigned occurrence) { if (address == kHeader + 0x20 && occurrence == 2) fixture.put32(address, 1); };
        rejected(fixture.run(), "lookup-observation-changed");
    });
    test("query-copied-before-callback", [] {
        Fixture fixture;
        fixture.beforeRead = [&](Address, unsigned) { fixture.query = {}; };
        observed(fixture.run(), 0x2001);
    });
    test("read-call-budget", [] {
        unsigned calls = 0;
        state::Read callback = [&](Address, void*, std::size_t) { ++calls; return true; };
        state::detail::Reader reader(callback); unsigned char data = 0;
        for (unsigned index = 0; index < 1024; ++index) reader.read(kBase, &data, 1);
        rejection("read-budget-exceeded", [&] { reader.read(kBase, &data, 1); });
        expect(calls == 1024 && reader.calls == 1024 && reader.bytes == 1024, "call budget accounting");
    });
    test("read-byte-budget", [] {
        unsigned calls = 0;
        state::Read callback = [&](Address, void*, std::size_t) { ++calls; return true; };
        state::detail::Reader reader(callback); std::array<unsigned char, 64> data{};
        for (unsigned index = 0; index < 256; ++index) reader.read(kBase, data.data(), data.size());
        rejection("read-budget-exceeded", [&] { reader.read(kBase, data.data(), 1); });
        expect(calls == 256 && reader.bytes == 16384, "byte budget accounting");
    });
    test("read-size-and-scalar-size-bounds", [] {
        state::Read callback = [](Address, void*, std::size_t) -> bool { throw std::runtime_error("unexpected callback"); };
        state::detail::Reader reader(callback); std::array<unsigned char, 65> data{};
        rejection("read-size-invalid", [&] { reader.read(kBase, data.data(), 0); });
        rejection("read-size-invalid", [&] { reader.read(kBase, data.data(), 65); });
        rejection("integer-size-invalid", [&] { reader.integer(kBase, 9); });
        expect(reader.calls == 0, "invalid read reached callback");
    });
    std::cout << state::Json({{"summary", true}, {"passed", passed}, {"failed", failed}, {"runtimeAccess", false},
        {"authority", "diagnostic-only-no-admission"}}).dump() << '\n';
    return failed == 0 ? 0 : 1;
}
