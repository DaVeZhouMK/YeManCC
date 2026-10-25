#include "msi_shoulder_modifier_guard.h"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include <type_traits>

namespace guard = ymcc::msi::shoulderModifiers;
using guard::Context;
using guard::Event;
constexpr Context active{true, true, true, true, true, true, 1000, 0x0300};
constexpr Event ctrl{162, true, true, true, 0};
constexpr Event alt{164, true, true, true, 0};
static_assert(guard::shouldSuppress(active, ctrl, 1064, false));
static_assert(!guard::shouldSuppress(active, ctrl, 1065, false));
static_assert(!guard::shouldSuppress(active, Event{162, false, true, true, 0}, 1000, false));
static_assert(noexcept(guard::shouldSuppress(active, ctrl, 1000, false)));
static_assert(std::is_trivial_v<Context> && std::is_standard_layout_v<Context>);
static_assert(std::is_trivial_v<Event> && std::is_standard_layout_v<Event>);

static unsigned checks = 0;
static unsigned failures = 0;
static void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        if (failures < 12) std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void gatesAndProvenance() {
    check(guard::shouldSuppress(active, ctrl, 1000, false), "left Ctrl with physical LB");
    check(guard::shouldSuppress(active, alt, 1000, false), "left Alt with physical RB");
    constexpr bool Context::* gates[]{&Context::knownMsiBoard, &Context::steamDeckBound,
        &Context::targetEnabled, &Context::hostUsable, &Context::sourceLive, &Context::msiDinput};
    for (auto gate : gates) {
        auto context = active;
        context.*gate = false;
        for (auto event : {ctrl, alt})
            check(!guard::shouldSuppress(context, event, 1000, false), "one missing gate passes");
    }
    for (auto event : {ctrl, alt}) {
        check(!guard::shouldSuppress(active, event, 1000, true), "physical keyboard modifier wins");
        for (unsigned flags = 0; flags < 3; ++flags) {
            auto ordinary = event;
            ordinary.injected = (flags & 1) != 0;
            ordinary.lowerIntegrityInjected = (flags & 2) != 0;
            check(!guard::shouldSuppress(active, ordinary, 1000, false), "both injection flags required");
        }
        for (std::uintptr_t tag : {std::uintptr_t{1}, std::uintptr_t{0x594d4343},
                                  (std::numeric_limits<std::uintptr_t>::max)()}) {
            event.extraInfo = tag;
            check(!guard::shouldSuppress(active, event, 1000, false), "own or foreign tagged input passes");
        }
    }
}

static void freshness() {
    struct Row { std::uint64_t sample, now; bool suppress; };
    constexpr auto top = (std::numeric_limits<std::uint64_t>::max)();
    constexpr Row rows[]{
        {1000, 1000, true}, {1000, 1001, true}, {1000, 1063, true}, {1000, 1064, true},
        {1000, 1065, false}, {1000, 1499, false}, {1000, 1500, false}, {1000, 1501, false},
        {0, 0, false}, {0, 1, false}, {0, 64, false}, {0, top, false},
        {1, 1, true}, {1, 65, true}, {1, 66, false}, {1, 0, false},
        {1000, 999, false}, {top, top, true}, {top - 64, top, true},
        {top - 65, top, false}, {top, 0, false}, {top, 63, false}, {1, top, false}
    };
    for (const auto& row : rows) {
        auto context = active;
        context.sampleTick = row.sample;
        for (auto event : {ctrl, alt})
            check(guard::shouldSuppress(context, event, row.now, false) == row.suppress,
                  "freshness golden table including wrap/future/missing sample");
    }
}

static void keyAndButtonScope() {
    struct Row { std::uint16_t buttons; bool ctrlDown, altDown; };
    constexpr Row rows[]{
        {0, false, false}, {0x0100, true, false}, {0x0200, false, true},
        {0x0300, true, true}, {0x00ff, false, false}, {0xfcff, false, false},
        {0xfdff, true, false}, {0xfeff, false, true}, {0xffff, true, true}
    };
    for (const auto& row : rows) {
        auto context = active;
        context.buttons = row.buttons;
        check(guard::shouldSuppress(context, ctrl, 1000, false) == row.ctrlDown, "physical LB only for Ctrl");
        check(guard::shouldSuppress(context, alt, 1000, false) == row.altDown, "physical RB only for Alt");
    }
    // Exhaust the uint16_t event domain: every up and every unrelated key passes,
    // even in the strongest suppression context. No Windows constants or APIs.
    for (unsigned vk = 0; vk <= 0xffffu; ++vk) {
        Event event{static_cast<std::uint16_t>(vk), false, true, true, 0};
        check(!guard::shouldSuppress(active, event, 1000, false), "all key-ups pass");
        check(!guard::shouldSuppress(active, event, 1064, true), "held physical modifier never eats ups");
        if (vk != 162 && vk != 164) {
            event.down = true;
            check(!guard::shouldSuppress(active, event, 1000, false),
                  "ordinary/generic/right/shift/other keys pass");
        }
    }
    for (unsigned bit = 0; bit < 16; ++bit) {
        if (bit == 8 || bit == 9) continue;
        auto context = active;
        context.buttons = static_cast<std::uint16_t>(1u << bit);
        check(!guard::shouldSuppress(context, ctrl, 1000, false), "unrelated source bit cannot become LB");
        check(!guard::shouldSuppress(context, alt, 1000, false), "unrelated source bit cannot become RB");
    }
}

static void familyScopeAndReplay() {
    // Contract fixtures for caller-provided board authority, not a board resolver
    // or hardware validation. Other-family noKnownMSI must stay false.
    struct Family { const char* name; bool known; };
    constexpr Family families[]{
        {"MS-1T41/A1M", true}, {"MS-1T42/A2VM", true}, {"MS-1T52/A2VM", true},
        {"MS-1T91/CG3EM", true}, {"MS-1T8K/BZ2EM", true},
        {"unknown MSI board", false}, {"Steam Deck/noKnownMSI", false},
        {"ASUS/noKnownMSI", false}, {"Lenovo/noKnownMSI", false},
        {"GPD/noKnownMSI", false}, {"unknown board/noKnownMSI", false}
    };
    for (const auto& family : families) {
        auto context = active;
        context.knownMsiBoard = family.known;
        for (auto event : {ctrl, alt})
            check(guard::shouldSuppress(context, event, 1000, false) == family.known, family.name);
    }
    // Three correlated observations may be half a second apart. Each needs its
    // own fresh physical sample; interval alone neither admits nor rejects.
    for (std::uint64_t tick : {1000ull, 1500ull, 2000ull}) {
        auto fresh = active;
        fresh.sampleTick = tick;
        check(guard::shouldSuppress(fresh, ctrl, tick, false), "fresh correlated down independent of interval");
        check(guard::shouldSuppress(fresh, alt, tick + 64, false), "fresh Alt at inclusive boundary");
        check(!guard::shouldSuppress(fresh, ctrl, tick + 65, false), "stale repeated down passes");
        auto up = ctrl;
        up.down = false;
        check(!guard::shouldSuppress(fresh, up, tick + 1, false), "suppressed down never owns following up");
    }
    const auto savedContext = active;
    const auto savedEvent = ctrl;
    for (unsigned i = 0; i < 32; ++i)
        check(guard::shouldSuppress(savedContext, savedEvent, 1000, false), "pure repeat has no hidden latch");
    check(savedContext.sampleTick == 1000 && savedContext.buttons == 0x0300 && savedEvent.down,
          "caller values remain unchanged");
}

int main() {
    gatesAndProvenance();
    freshness();
    keyAndButtonScope();
    familyScopeAndReplay();
    std::printf("{\"pass\":%s,\"checks\":%u,\"failures\":%u,\"groups\":4,"
                "\"actualOSInput\":false,\"boardScope\":\"caller-authority fixtures only\"}\n",
                failures == 0 ? "true" : "false", checks, failures);
    return failures == 0 ? 0 : 1;
}
