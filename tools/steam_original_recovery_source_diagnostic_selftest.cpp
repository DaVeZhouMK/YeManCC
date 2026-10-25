#include "../native/steam_original_recovery_source_diagnostic.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace diag = ymcc::steamOriginalSourceDiagnostic;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

namespace {
constexpr std::uint64_t base = 0x180000000ULL;
constexpr std::uint64_t imageSize = 0x1400000;
constexpr std::uint64_t user = 0x240000000ULL;
struct Request {
    std::uint64_t address;
    std::size_t size;
    bool operator==(const Request&) const = default;
};
struct Cell {
    std::uint64_t address;
    std::vector<std::uint8_t> bytes;
};

// Independent, exact sparse fixture: every request must match one entire cell.
// No contiguous heap/module fallback, scanner, process API or getter execution.
struct Fixture {
    std::vector<Cell> cells;
    std::vector<Request> trace;
    std::size_t failCall = 0;
    std::size_t throwCall = 0;
    std::size_t corruptCall = 0;
    std::size_t corruptByte = 0;
    bool unexpected = false;

    void put(std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
        cells.push_back({address, bytes});
    }
    void integer(std::uint64_t address, std::uint64_t value, std::size_t size) {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i)
            bytes[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xff);
        const auto found = std::find_if(cells.begin(), cells.end(),
            [=](const Cell& cell) { return cell.address == address; });
        if (found == cells.end()) cells.push_back({address, std::move(bytes)});
        else found->bytes = std::move(bytes);
    }
    Fixture() {
        put(base + 0x501a0a, {0x48, 0x8b, 0xd7});
        put(base + 0x501a0d, {0x48, 0x8d, 0x4f, 0x48});
        put(base + 0x501a11, {0xe8, 0x0a, 0x6a, 0x0b, 0x00});
        put(base + 0x5b8430, {0x48, 0x89, 0x91, 0x68, 0x06, 0x00, 0x00});
        put(base + 0x6bc4b0, {0x48, 0x8b, 0xc1, 0xc3});
        integer(user + 0x6b0, user, 8);
        integer(user, base + 0x136fa80, 8);
        integer(base + 0x136fba0, base + 0x6bc4b0, 8);
        integer(user + 0x178, 4, 4);
        put(user + 0x2294, {1, 2, 0x80, 0xff});
        put(user + 0x17c, {0xa5});
    }
    bool operator()(std::uint64_t address, void* destination, std::size_t size) {
        trace.push_back({address, size});
        const auto found = std::find_if(cells.begin(), cells.end(),
            [=](const Cell& cell) { return cell.address == address && cell.bytes.size() == size; });
        if (found == cells.end()) {
            unexpected = true;
            return false;
        }
        if (trace.size() == throwCall) throw 42;
        if (trace.size() == failCall) {
            // A failed callback may have partially filled its output.
            std::memset(destination, 0xfe, size);
            return false;
        }
        std::memcpy(destination, found->bytes.data(), size);
        if (trace.size() == corruptCall) {
            CHECK(corruptByte < size);
            static_cast<std::uint8_t*>(destination)[corruptByte] ^= 1;
        }
        return true;
    }
};

std::vector<Request> expectedTrace() {
    const std::vector<Request> guards{
        {base + 0x501a0a, 3}, {base + 0x501a0d, 4}, {base + 0x501a11, 5},
        {base + 0x5b8430, 7}, {base + 0x6bc4b0, 4}, {user + 0x6b0, 8},
        {user, 8}, {base + 0x136fba0, 8}};
    const std::vector<Request> raw{
        {user + 0x178, 4}, {user + 0x2294, 4}, {user + 0x17c, 1}};
    auto expected = guards;
    expected.insert(expected.end(), raw.begin(), raw.end());
    expected.insert(expected.end(), raw.begin(), raw.end());
    expected.insert(expected.end(), guards.begin(), guards.end());
    return expected;
}

void checkAccounting(const Fixture& fixture, const diag::Result& result) {
    CHECK(!fixture.unexpected);
    CHECK(result.readCalls == fixture.trace.size());
    std::size_t bytes = 0;
    for (const auto& request : fixture.trace) bytes += request.size;
    CHECK(result.requestedBytes == bytes);
    CHECK(result.readCalls <= 32);
    CHECK(result.requestedBytes <= 256);
    const auto expected = expectedTrace();
    CHECK(fixture.trace.size() <= expected.size());
    CHECK(std::equal(fixture.trace.begin(), fixture.trace.end(), expected.begin()));
}
void unavailable(const Fixture& fixture, const diag::Result& result, diag::Reason reason) {
    checkAccounting(fixture, result);
    CHECK(!result.available);
    CHECK(result.reason == reason);
    CHECK(!result.ownerMatched && !result.getterVerified);
    CHECK(result.userStateRaw == 0);
    CHECK((result.postLogonProgressRaw == std::array<std::uint8_t, 4>{}));
    CHECK(result.logonWaitingRaw == 0);
}
diag::Result observe(Fixture& fixture) {
    return diag::observe(fixture, base, imageSize, user, true);
}
struct Tests {
    unsigned passed = 0;
    template<class Test> void run(const char* name, Test&& test) {
        test();
        ++passed;
        std::printf("PASS %s\n", name);
    }
};
} // namespace

int main() {
    static_assert(noexcept(diag::observe(std::declval<Fixture&>(), base, imageSize, user, true)));
    try {
        Tests tests;
        tests.run("healthy state4/progress; exact 22 calls/112B; no extra reads", [] {
            Fixture fixture;
            const auto result = observe(fixture);
            checkAccounting(fixture, result);
            CHECK(result.available && result.reason == diag::Reason::Available);
            CHECK(result.ownerMatched && result.getterVerified);
            CHECK(result.userStateRaw == 4);
            CHECK((result.postLogonProgressRaw == std::array<std::uint8_t, 4>{1, 2, 0x80, 0xff}));
            CHECK(result.logonWaitingRaw == 0xa5);
            CHECK(result.readCalls == 22 && result.requestedBytes == 112);
        });
        tests.run("unqualified profile makes zero calls even with invalid addresses", [] {
            Fixture fixture;
            fixture.throwCall = 1;
            const auto result = diag::observe(fixture, UINT64_MAX, UINT64_MAX, UINT64_MAX, false);
            unavailable(fixture, result, diag::Reason::FixedProfileUnqualified);
            CHECK(result.readCalls == 0);
        });
        tests.run("raw values never interpreted as admission or logon generation", [] {
            for (const auto state : {0U, 4U, 0x80000000U, 0xffffffffU}) {
                Fixture fixture;
                fixture.integer(user + 0x178, state, 4);
                fixture.integer(user + 0x2294, 0xff008001, 4);
                fixture.integer(user + 0x17c, 0xff, 1);
                const auto result = observe(fixture);
                checkAccounting(fixture, result);
                CHECK(result.available && result.userStateRaw == state);
                CHECK((result.postLogonProgressRaw == std::array<std::uint8_t, 4>{1, 0x80, 0, 0xff}));
                CHECK(result.logonWaitingRaw == 0xff);
            }
        });
        tests.run("every byte of every code anchor rejected, including repeated check", [] {
            const std::size_t sizes[]{3, 4, 5, 7, 4};
            for (std::size_t i = 0; i < 5; ++i) {
                for (std::size_t byte = 0; byte < sizes[i]; ++byte) {
                    for (const auto start : {std::size_t{1}, std::size_t{15}}) {
                        Fixture fixture;
                        fixture.corruptCall = start + i;
                        fixture.corruptByte = byte;
                        unavailable(fixture, observe(fixture), diag::Reason::AnchorMismatch);
                        CHECK(fixture.trace.size() == start + i);
                    }
                }
            }
        });
        tests.run("wrong owner/vtable/slot, every byte, before and after raw", [] {
            const diag::Reason reasons[]{diag::Reason::OwnerMismatch,
                diag::Reason::VtableMismatch, diag::Reason::GetterSlotMismatch};
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t byte = 0; byte < 8; ++byte) {
                    for (const auto start : {std::size_t{6}, std::size_t{20}}) {
                        Fixture fixture;
                        fixture.corruptCall = start + i;
                        fixture.corruptByte = byte;
                        unavailable(fixture, observe(fixture), reasons[i]);
                        CHECK(fixture.trace.size() == start + i);
                    }
                }
            }
        });
        tests.run("wrong-vtable getter-number mixup rejected despite valid owner and leaf", [] {
            Fixture fixture;
            fixture.integer(user, base + 0x136f900, 8);
            fixture.integer(base + 0x136f900 + 0x120, base + 0x6bc4b0, 8);
            unavailable(fixture, observe(fixture), diag::Reason::VtableMismatch);
            CHECK(fixture.trace.size() == 7);
        });
        tests.run("slot is byte offset 0x120, not ordinal 120", [] {
            Fixture fixture;
            fixture.integer(base + 0x136fba0, base + 0x6bc4c0, 8);
            fixture.integer(base + 0x136fa80 + 120 * 8, base + 0x6bc4b0, 8);
            unavailable(fixture, observe(fixture), diag::Reason::GetterSlotMismatch);
            CHECK(fixture.trace.size() == 8);
        });
        tests.run("false and exceptions at each of the 22 reads stop immediately", [] {
            for (std::size_t call = 1; call <= 22; ++call) {
                Fixture failed;
                failed.failCall = call;
                unavailable(failed, observe(failed), diag::Reason::ReadFailed);
                CHECK(failed.trace.size() == call);
                Fixture thrown;
                thrown.throwCall = call;
                unavailable(thrown, observe(thrown), diag::Reason::ReadException);
                CHECK(thrown.trace.size() == call);
            }
        });
        tests.run("changes in every raw byte in either collection are rejected", [] {
            const std::size_t sizes[]{4, 4, 1};
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t byte = 0; byte < sizes[i]; ++byte) {
                    for (const auto start : {std::size_t{9}, std::size_t{12}}) {
                        Fixture fixture;
                        fixture.corruptCall = start + i;
                        fixture.corruptByte = byte;
                        unavailable(fixture, observe(fixture), diag::Reason::RawChanged);
                        CHECK(fixture.trace.size() == 22);
                    }
                }
            }
        });
        tests.run("invalid user, module and all offset/extent overflows: zero reads", [] {
            struct Input { std::uint64_t module, size, selected; diag::Reason reason; };
            const Input cases[]{
                {base, imageSize, 0, diag::Reason::InvalidUser},
                {base, imageSize, 0xffff, diag::Reason::InvalidUser},
                {0, imageSize, user, diag::Reason::ModuleRangeInvalid},
                {base, 0, user, diag::Reason::ModuleRangeInvalid},
                {base, 0x136fba7, user, diag::Reason::ModuleRangeInvalid},
                {base, 0x501a0c, user, diag::Reason::ModuleRangeInvalid},
                {base, UINT64_MAX, user, diag::Reason::AddressOverflow},
                {UINT64_MAX - imageSize + 1, imageSize, user, diag::Reason::AddressOverflow},
                {UINT64_MAX, 1, user, diag::Reason::AddressOverflow},
                {base, imageSize, UINT64_MAX, diag::Reason::AddressOverflow},
                {base, imageSize, UINT64_MAX - 0x2296, diag::Reason::AddressOverflow}
            };
            for (const auto& input : cases) {
                Fixture fixture;
                unavailable(fixture, diag::observe(fixture, input.module, input.size,
                    input.selected, true), input.reason);
                CHECK(fixture.trace.empty());
            }
        });
        tests.run("exact module upper boundary permits last slot byte", [] {
            Fixture fixture;
            const auto result = diag::observe(fixture, base, 0x136fba8, user, true);
            checkAccounting(fixture, result);
            CHECK(result.available);
        });
        tests.run("exact user overflow boundary permits last progress byte", [] {
            Fixture fixture;
            constexpr auto highUser = UINT64_MAX - 0x2297;
            for (auto& cell : fixture.cells) {
                if (cell.address >= user && cell.address <= user + 0x2297)
                    cell.address = highUser + (cell.address - user);
            }
            fixture.integer(highUser + 0x6b0, highUser, 8);
            const auto result = diag::observe(fixture, base, imageSize, highUser, true);
            CHECK(result.available && !fixture.unexpected);
            CHECK(result.readCalls == 22 && result.requestedBytes == 112);
        });
        tests.run("temporary callable; fresh independent invocation; reason strings", [] {
            const auto result = diag::observe(Fixture{}, base, imageSize, user, true);
            CHECK(result.available && result.readCalls == 22 && result.requestedBytes == 112);
            const std::pair<diag::Reason, const char*> names[]{
                {diag::Reason::Available, "available"},
                {diag::Reason::FixedProfileUnqualified, "fixed-profile-unqualified"},
                {diag::Reason::InvalidUser, "invalid-user"},
                {diag::Reason::AddressOverflow, "address-overflow"},
                {diag::Reason::ModuleRangeInvalid, "module-range-invalid"},
                {diag::Reason::ReadFailed, "read-failed"},
                {diag::Reason::ReadException, "read-exception"},
                {diag::Reason::AnchorMismatch, "anchor-mismatch"},
                {diag::Reason::OwnerMismatch, "owner-mismatch"},
                {diag::Reason::VtableMismatch, "vtable-mismatch"},
                {diag::Reason::GetterSlotMismatch, "getter-slot-mismatch"},
                {diag::Reason::RawChanged, "raw-changed"}
            };
            for (const auto& [reason, name] : names) CHECK(std::strcmp(diag::reasonName(reason), name) == 0);
            CHECK(std::strcmp(diag::reasonName(static_cast<diag::Reason>(999)), "unknown") == 0);
        });
        std::printf("PASS all %u groups; fixture only, no Steam; healthy budget 22 calls / 112 bytes\n", tests.passed);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "FAIL unexpected test exception\n");
        return 1;
    }
}
