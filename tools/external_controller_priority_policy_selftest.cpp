// Offline tests of the actual production policy. No mirrored arbitration implementation.
#include "../native/external_controller_priority_policy.h"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
namespace ep = ymcc::externalpad;
int assertions = 0, passed = 0, total = 0;
void require(bool condition, const char* expression, int line) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(...) require(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
ep::Candidate pad(std::string root = "USB\\EXTERNAL\\A", unsigned slot = 0,
                  std::string location = "PCIROOT(0)#USBROOT(0)#USB(3)") {
    return {std::move(root), std::move(location), slot, true, true, true};
}
void waiting(const ep::Arbitration::Result& r) { CHECK(!r.available); CHECK(!r.release); }
void selected(const ep::Arbitration::Result& r, const ep::Candidate& expected) {
    CHECK(r.available); CHECK(!r.release); CHECK(r.selected.root == expected.root);
    CHECK(r.selected.location == expected.location); CHECK(r.selected.slot == expected.slot);
    CHECK(ep::ready(r.selected));
}
void acquire(ep::Arbitration& a, const ep::Candidate& c, std::uint64_t start = 1000,
             std::uint64_t power = 1) {
    waiting(a.observe(true, true, power, start, {c}));
    waiting(a.observe(true, true, power, start + 500, {c}));
    selected(a.observe(true, true, power, start + 750, {c}), c);
}
void run(const char* name, const std::function<void()>& test) {
    ++total;
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
}
int main() {
    static_assert(ep::Arbitration::arrivalMs == 750);
    static_assert(ep::Arbitration::absenceMs == 2000);
    run("ready rejects every missing evidence field and invalid slot", [] {
        CHECK(ep::ready(pad()));
        std::vector<ep::Candidate> invalid;
        auto c = pad(); c.physical = false; invalid.push_back(c);
        c = pad(); c.external = false; invalid.push_back(c);
        c = pad(); c.fresh = false; invalid.push_back(c);
        c = pad(); c.root.clear(); invalid.push_back(c);
        c = pad(); c.location.clear(); invalid.push_back(c);
        c = pad(); c.slot = 4; invalid.push_back(c);
        c = pad(); c.slot = std::numeric_limits<unsigned>::max(); invalid.push_back(c);
        for (const auto& rejected : invalid) {
            CHECK(!ep::ready(rejected)); ep::Arbitration a;
            for (std::uint64_t t : {1000ULL, 1500ULL, 1750ULL, 3000ULL, 10000ULL})
                waiting(a.observe(true, true, 1, t, {rejected}));
        }
    });
    run("no candidate and mixed unknown virtual nonfresh stay idle", [] {
        ep::Arbitration a;
        for (std::uint64_t t = 1000; t < 10000; t += 500) waiting(a.observe(true, true, 1, t, {}));
        auto unknown = pad("UNKNOWN"); unknown.location.clear();
        auto virtualPad = pad("VIRTUAL"); virtualPad.physical = false;
        auto old = pad("STALE"); old.fresh = false;
        for (std::uint64_t t = 10000; t < 15000; t += 500)
            waiting(a.observe(true, true, 1, t, {unknown, virtualPad, old}));
        acquire(a, pad(), 15000);
    });
    run("neutral ready input qualifies without activity evidence", [] { ep::Arbitration a; acquire(a, pad()); });
    run("750ms exact boundary still requires three observations", [] {
        ep::Arbitration a; const auto c = pad();
        waiting(a.observe(true, true, 1, 1000, {c}));
        waiting(a.observe(true, true, 1, 1749, {c}));
        waiting(a.observe(true, true, 1, 1749, {c}));
        selected(a.observe(true, true, 1, 1750, {c}), c);
        a.reset(); waiting(a.observe(true, true, 1, 2000, {c}));
        waiting(a.observe(true, true, 1, 4000, {c}));
        selected(a.observe(true, true, 1, 4001, {c}), c);
    });
    run("dense observations cannot bypass elapsed arrival time", [] {
        ep::Arbitration a; const auto c = pad();
        for (std::uint64_t t = 1000; t < 1750; ++t) waiting(a.observe(true, true, 1, t, {c}));
        selected(a.observe(true, true, 1, 1750, {c}), c);
    });
    run("arrival hotplug flap restarts time and observation count", [] {
        ep::Arbitration a; const auto c = pad();
        waiting(a.observe(true, true, 1, 1000, {c})); waiting(a.observe(true, true, 1, 1500, {c}));
        waiting(a.observe(true, true, 1, 1750, {}));
        acquire(a, c, 2000);
    });
    run("changing candidate root cannot combine observations", [] {
        ep::Arbitration a; const auto b = pad("USB\\EXTERNAL\\B", 1);
        waiting(a.observe(true, true, 1, 1000, {pad()}));
        waiting(a.observe(true, true, 1, 1500, {b}));
        waiting(a.observe(true, true, 1, 2000, {b})); selected(a.observe(true, true, 1, 2250, {b}), b);
    });
    run("same external stays selected across arbitrary observation intervals", [] {
        ep::Arbitration a; const auto c = pad(); acquire(a, c);
        for (std::uint64_t t : {1751ULL, 2000ULL, 9000ULL, 900000ULL}) selected(a.observe(true, true, 1, t, {c}), c);
    });
    run("multiple pads choose deterministic root then keep selected", [] {
        ep::Arbitration a; const auto z = pad("Z", 3), b = pad("B", 2), early = pad("A", 1);
        waiting(a.observe(true, true, 1, 1000, {z, b})); waiting(a.observe(true, true, 1, 1500, {b, z}));
        selected(a.observe(true, true, 1, 1750, {z, b}), b);
        for (std::uint64_t t = 2000; t < 8000; t += 500) selected(a.observe(true, true, 1, t, {early, z, b}), b);
    });
    run("slot reorder follows identity rather than the old slot", [] {
        ep::Arbitration a; auto c = pad(); acquire(a, c); const auto other = pad("OTHER", 0);
        c.slot = 3; selected(a.observe(true, true, 1, 2000, {other, c}), c);
        c.slot = 1; selected(a.observe(true, true, 1, 2500, {c, other}), c);
    });
    run("same root moved location waits absence then fresh arrival confirmation", [] {
        ep::Arbitration a; auto c = pad(); acquire(a, c); c.location = "PCIROOT(0)#USBROOT(0)#USB(9)";
        waiting(a.observe(true, true, 1, 2000, {c})); waiting(a.observe(true, true, 1, 3999, {c}));
        waiting(a.observe(true, true, 1, 4000, {c})); waiting(a.observe(true, true, 1, 4500, {c}));
        waiting(a.observe(true, true, 1, 4749, {c})); selected(a.observe(true, true, 1, 4750, {c}), c);
    });
    run("pending location flap cannot combine arrival observations", [] {
        ep::Arbitration a; const auto old = pad(); auto moved = old; moved.location = "PCIROOT(0)#USBROOT(0)#USB(9)";
        waiting(a.observe(true, true, 1, 1000, {old})); waiting(a.observe(true, true, 1, 1500, {old}));
        waiting(a.observe(true, true, 1, 1750, {moved})); waiting(a.observe(true, true, 1, 2250, {moved}));
        selected(a.observe(true, true, 1, 2500, {moved}), moved);
    });
    run("original location returns within grace without new takeover", [] {
        ep::Arbitration a; const auto old = pad(); acquire(a, old); auto moved = old; moved.location = "PCIROOT(0)#USBROOT(0)#USB(9)";
        waiting(a.observe(true, true, 1, 2000, {moved})); selected(a.observe(true, true, 1, 3999, {old}), old);
    });
    run("two second absence grace and one terminal release", [] {
        ep::Arbitration a; acquire(a, pad());
        waiting(a.observe(true, true, 1, 2000, {})); waiting(a.observe(true, true, 1, 3999, {}));
        const auto r = a.observe(true, true, 1, 4000, {}); CHECK(!r.available); CHECK(r.release);
        waiting(a.observe(true, true, 1, 4500, {}));
    });
    run("selected nonfresh virtual unknown count as absence", [] {
        for (unsigned mode = 0; mode < 3; ++mode) {
            ep::Arbitration a; auto c = pad(); acquire(a, c);
            if (mode == 0) c.fresh = false; else if (mode == 1) c.physical = false; else c.location.clear();
            waiting(a.observe(true, true, 1, 2000, {c})); waiting(a.observe(true, true, 1, 3999, {c}));
            const auto r = a.observe(true, true, 1, 4000, {c}); CHECK(r.release); CHECK(!r.available);
        }
    });
    run("active flap before absence deadline preserves identity", [] {
        ep::Arbitration a; const auto c = pad(); acquire(a, c);
        waiting(a.observe(true, true, 1, 2000, {})); selected(a.observe(true, true, 1, 3999, {c}), c);
        waiting(a.observe(true, true, 1, 4000, {})); waiting(a.observe(true, true, 1, 5999, {}));
        selected(a.observe(true, true, 1, 6000, {c}), c);
    });
    run("successor waits for absence and its own stable arrival", [] {
        ep::Arbitration a; acquire(a, pad()); const auto b = pad("B", 2);
        waiting(a.observe(true, true, 1, 2000, {b})); waiting(a.observe(true, true, 1, 3999, {b}));
        waiting(a.observe(true, true, 1, 4000, {b})); waiting(a.observe(true, true, 1, 4500, {b}));
        waiting(a.observe(true, true, 1, 4749, {b})); selected(a.observe(true, true, 1, 4750, {b}), b);
    });
    run("suspend is not disconnect and resume retains same external", [] {
        ep::Arbitration a; const auto c = pad(); acquire(a, c);
        waiting(a.observe(true, false, 2, 2000, {})); waiting(a.observe(true, false, 2, 12000, {}));
        selected(a.observe(true, true, 2, 12500, {c}), c);
    });
    run("power generation invalidates pending arrival and absence timers", [] {
        ep::Arbitration a; const auto c = pad();
        waiting(a.observe(true, true, 1, 1000, {c})); waiting(a.observe(true, true, 1, 1500, {c}));
        acquire(a, c, 2000, 2);
        waiting(a.observe(true, true, 2, 3000, {})); waiting(a.observe(true, true, 3, 10000, {}));
        waiting(a.observe(true, true, 3, 11999, {})); CHECK(a.observe(true, true, 3, 12000, {}).release);
    });
    run("disable during active or suspend resets automatic arbitration", [] {
        ep::Arbitration a; acquire(a, pad());
        CHECK(a.observe(false, false, 2, 2000, {}).release); acquire(a, pad(), 3000, 2);
        CHECK(a.observe(false, true, 2, 4000, {pad()}).release); acquire(a, pad(), 5000, 2);
    });
    run("explicit reset removes selected and pending identity", [] {
        ep::Arbitration a; acquire(a, pad()); a.reset(); acquire(a, pad("NEXT", 1), 2000);
    });
    std::cout << "EXTERNALPAD_POLICY_SUMMARY passed=" << passed << " total=" << total << " assertions=" << assertions
              << " hardwareAccess=false productRun=false productionHeader=true\n";
    return passed == total ? 0 : 1;
}
