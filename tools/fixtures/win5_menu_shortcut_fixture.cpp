#include "win5_menu_shortcut_policy.h"

#include <cstdlib>
#include <iostream>

using namespace ymcc::win5Menu;

static int checks = 0;
static void require(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::cerr << name << '\n'; std::exit(1); }
}

int main() {
    const Session base{{11, 22, 33, 44, 55, 0}, 6, 7, true};
    ShortPress press;
    auto sample = [&](Reading state, std::uint64_t at) { return press.observe(base, state, at); };
    require(qualifies(true, true, true, true, false, false, 0), "current internal source");
    require(!qualifies(false, true, true, true, false, false, 0), "other board");
    require(!qualifies(true, false, true, true, false, false, 0), "unbound path");
    require(!qualifies(true, true, false, true, false, false, 0), "retired source");
    require(!qualifies(true, true, true, false, false, false, 0), "unadmitted source");
    require(!qualifies(true, true, true, true, true, false, 0), "virtual target");
    require(!qualifies(true, true, true, true, false, true, 0), "unknown identity");
    require(!qualifies(true, true, true, true, false, false, 1), "external port");
    require(!qualifies(true, true, true, true, false, false, -1), "unknown port");

    require(!sample(Reading::Down, 100), "held at capture start");
    require(!sample(Reading::Up, 120), "initial held release is not a gesture");
    require(!sample(Reading::Down, 150), "wait for release");
    require(!sample(Reading::Down, 165), "held sample does not repeat");
    require(sample(Reading::Up, 190), "one confirmed short press");
    require(!sample(Reading::Up, 205), "duplicate release does not repeat");
    require(!sample(Reading::Down, 240) && sample(Reading::Up, 270), "next press is independent");
    require(!sample(Reading::Down, 300), "long hold starts");
    for (std::uint64_t at = 400; at <= 800; at += 100) require(!sample(Reading::Down, at), "long hold remains quiet");
    require(!sample(Reading::Up, 810), "long hold rejected on release");

    require(!sample(Reading::Down, 850), "start before read failure");
    require(!sample(Reading::Unavailable, 860), "failed read isn't up");
    require(!sample(Reading::Down, 870) && !sample(Reading::Up, 880), "read recovery waits actual neutral");
    require(!sample(Reading::Down, 900) && !sample(Reading::Up, 1030), "sample gap invalidates press");
    require(!sample(Reading::Down, 1040) && !sample(Reading::Up, 1035), "clock reversal invalidates press");

    for (int kind = 0; kind < 9; ++kind) {
        press.reset();
        require(!sample(Reading::Up, 100) && !sample(Reading::Down, 105), "prepare boundary case");
        auto changed = base;
        switch (kind) {
        case 0: ++changed.source.power; break;
        case 1: ++changed.source.topology; break;
        case 2: ++changed.source.identity; break;
        case 3: ++changed.source.intent; break;
        case 4: ++changed.source.episode; break;
        case 5: ++changed.source.slot; break;
        case 6: ++changed.ruleEpoch; break;
        case 7: ++changed.foreground; break;
        case 8: changed.recording = false; break;
        }
        require(!press.observe(changed, Reading::Up, 110), "changed scope discards old down");
        require(!press.observe(changed, Reading::Down, 120) && press.observe(changed, Reading::Up, 140),
            "current scope may act after neutral");
    }

    std::cout << checks << " Win5 Menu policy checks passed\n";
}
