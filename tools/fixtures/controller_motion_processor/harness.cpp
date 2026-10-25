// Pure effect sinks. The mutated header alone may read this fake Windows cache;
// the production header has no reference to it or to any platform sensor state.
namespace windows_sensor_fixture {
constexpr float x = 901.25f, y = -902.5f, z = 903.75f;
constexpr int weight = 904;
}
#include "controller_motion_processor.h"

#include <array>
#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ymcc::controller_motion;

static_assert(sizeof(void*) == 8, "Actual GM pointer seam is x64");
static_assert(!std::is_copy_constructible_v<Owner> && !std::is_copy_assignable_v<Owner>);
static_assert(!std::is_move_constructible_v<Owner> && !std::is_move_assignable_v<Owner>);
#if defined(_MSC_VER)
using ActualCreate = void* (__cdecl*)();
using ActualDestroy = void (__cdecl*)(void*);
using ActualSetMode = void (__cdecl*)(void*, int);
using ActualSetOffset = void (__cdecl*)(void*, float, float, float, int);
using ActualProcess = void (__cdecl*)(void*, float, float, float, float, float, float, float);
static_assert(std::is_same_v<decltype(Ops::create), ActualCreate>);
static_assert(std::is_same_v<decltype(Ops::destroy), ActualDestroy>);
static_assert(std::is_same_v<decltype(Ops::setMode), ActualSetMode>);
static_assert(std::is_same_v<decltype(Ops::setOffset), ActualSetOffset>);
static_assert(std::is_same_v<decltype(Ops::process), ActualProcess>);
#endif

namespace {
unsigned checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct Handle { unsigned id; bool alive = true; };
struct Event {
    std::string kind;
    int bank;
    Handle* handle;
    std::array<float, 7> values{};
    int integer = 0;
};
struct Sink {
    std::vector<std::unique_ptr<Handle>> handles;
    std::vector<Event> events;
    unsigned violations = 0;
    bool failCreate = false;
    unsigned count(const std::string& kind) const {
        unsigned n = 0;
        for (const auto& event : events) if (event.kind == kind) ++n;
        return n;
    }
    const Event& last(const std::string& kind) const {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->kind == kind) return *it;
        throw std::runtime_error("missing effect: " + kind);
    }
    Handle* lookup(void* raw) {
        for (const auto& h : handles) if (h.get() == raw) return h.get();
        ++violations;
        return nullptr;
    }
    void clean() const {
        require(violations == 0, "effect sink detected invalid or repeated resource use");
        for (const auto& h : handles) require(!h->alive, "resource leaked");
    }
};
Sink* sink = nullptr;
struct Fixture {
    Sink effects;
    Fixture() { sink = &effects; }
    ~Fixture() { sink = nullptr; }
};
void* createIn(int bank) {
    if (sink->failCreate) {
        sink->failCreate = false;
        sink->events.push_back({"create-failed", bank, nullptr});
        return nullptr;
    }
    auto h = std::make_unique<Handle>();
    h->id = static_cast<unsigned>(sink->handles.size() + 1);
    auto raw = h.get();
    sink->handles.push_back(std::move(h));
    sink->events.push_back({"create", bank, raw});
    return raw;
}
void destroyIn(int bank, void* raw) {
    auto h = sink->lookup(raw);
    sink->events.push_back({"destroy", bank, h});
    if (!h || !h->alive) { ++sink->violations; return; }
    h->alive = false;
}
void modeIn(int bank, void* raw, int mode) {
    auto h = sink->lookup(raw);
    if (!h || !h->alive) ++sink->violations;
    Event e{"mode", bank, h}; e.integer = mode;
    sink->events.push_back(e);
}
void offsetIn(int bank, void* raw, float x, float y, float z, int weight) {
    auto h = sink->lookup(raw);
    if (!h || !h->alive) ++sink->violations;
    Event e{"offset", bank, h}; e.values = {x, y, z, 0, 0, 0, 0}; e.integer = weight;
    sink->events.push_back(e);
}
void processIn(int bank, void* raw, float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    auto h = sink->lookup(raw);
    if (!h || !h->alive) ++sink->violations;
    Event e{"process", bank, h}; e.values = {gx, gy, gz, ax, ay, az, dt};
    sink->events.push_back(e);
}
void* create0() { return createIn(0); }
void* create1() { return createIn(1); }
void destroy0(void* p) { destroyIn(0, p); }
void destroy1(void* p) { destroyIn(1, p); }
void mode0(void* p, int m) { modeIn(0, p, m); }
void mode1(void* p, int m) { modeIn(1, p, m); }
void offset0(void* p, float x, float y, float z, int w) { offsetIn(0, p, x, y, z, w); }
void offset1(void* p, float x, float y, float z, int w) { offsetIn(1, p, x, y, z, w); }
void process0(void* p, float a, float b, float c, float d, float e, float f, float t) { processIn(0, p, a, b, c, d, e, f, t); }
void process1(void* p, float a, float b, float c, float d, float e, float f, float t) { processIn(1, p, a, b, c, d, e, f, t); }
Ops operations() { return {create0, destroy0, mode0, offset0, process0}; }
Key token() { return {"controller/container/Physical-A", 10, 20, 30, 0}; }
Calibration offset() { return {1.25f, -2.5f, 3.75f, 17}; }
RawSixAxes sample() { return {1, -2, 3, -4, 5, -6}; }
std::uint32_t bits(float f) { std::uint32_t u; std::memcpy(&u, &f, sizeof u); return u; }
void matchesOffset(const Sink& s, Calibration c) {
    const auto& event = s.last("offset");
    require(bits(event.values[0]) == bits(c.x) && bits(event.values[1]) == bits(c.y) &&
            bits(event.values[2]) == bits(c.z) && event.integer == c.weight,
            "offset must come from this physical controller and sensor");
}
void initialOrder(const Sink& s) {
    require(s.events.size() == 3, "selection must emit only create/mode/offset; no fabricated sample");
    require(s.events[0].kind == "create" && s.events[1].kind == "mode" &&
            s.events[1].integer == 0 && s.events[2].kind == "offset", "Manual mode must precede offset");
    require(s.events[0].handle == s.events[1].handle && s.events[1].handle == s.events[2].handle,
            "initialization must address the created handle");
}
void testInitial() {
    Fixture fixture; auto& s = fixture.effects;
    { Owner owner;
      auto key = token(); const auto original = key; auto cal = offset();
      require(owner.select(key, operations(), cal), "first selection failed");
      initialOrder(s); matchesOffset(s, cal);
      key.physicalIdentity = "mutated-caller-string"; cal.x = 100;
      require(owner.handleFor(original) != nullptr && owner.handleFor(key) == nullptr,
              "owner must own its exact identity copy");
      require(s.count("process") == 0, "select cannot invent sample-ready axes");
    }
    s.clean(); require(s.count("destroy") == 1, "destructor must release exactly once");
}
void testIdempotence() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; const auto key = token();
    require(owner.select(key, operations(), offset()), "initial select failed");
    void* handle = owner.handleFor(key);
    for (int n = 0; n < 5; ++n) {
        require(owner.select(key, operations(), {100.0f + static_cast<float>(n), -200, 300, 0}), "unchanged selection failed");
        require(owner.handleFor(key) == handle, "unchanged selection must retain handle");
    }
    require(s.count("create") == 1 && s.count("destroy") == 0 && s.count("mode") == 1 && s.count("offset") == 1,
            "unchanged selection must not recreate or rewrite calibration");
    matchesOffset(s, offset()); owner.revoke(); s.clean();
}
Key changed(Key key, unsigned field) {
    switch (field) {
    case 0: key.physicalIdentity += "/other"; break;
    case 1: ++key.power; break;
    case 2: ++key.rawGeneration; break;
    case 3: ++key.captureEpoch; break;
    case 4: ++key.sensorIndex; break;
    }
    return key;
}
void testBoundary(unsigned field) {
    Fixture fixture; auto& s = fixture.effects; Owner owner;
    const Key oldKey = token(), nextKey = changed(oldKey, field);
    require(owner.select(oldKey, operations(), offset()), "initial select failed");
    void* oldHandle = owner.handleFor(oldKey);
    require(owner.handleFor(nextKey) == nullptr, "foreign token exposed current handle");
    require(!owner.process(nextKey, sample(), 0.01f), "foreign token processed current handle");
    require(s.count("process") == 0, "foreign token emitted a process effect");
    require(owner.select(nextKey, operations(), {-11, 22, -33, 0}), "changed token selection failed");
    require(s.count("create") == 2 && s.count("destroy") == 1, "changed token must replace resource");
    require(s.events[3].kind == "destroy" && s.events[3].handle == oldHandle && s.events[4].kind == "create",
            "old resource must be destroyed before new creation");
    require(owner.handleFor(nextKey) != oldHandle && owner.handleFor(oldKey) == nullptr,
            "old token must lose the handle after replacement");
    require(!owner.process(oldKey, sample(), 1), "old token processed replacement resource");
    require(owner.process(nextKey, sample(), 1), "new token cannot process its resource");
    matchesOffset(s, {-11, 22, -33, 0}); owner.revoke(); s.clean();
}
void testInvalidKeys() {
    for (unsigned field = 0; field < 4; ++field) {
        Fixture fixture; auto& s = fixture.effects; Owner owner;
        const auto old = token(); auto key = old;
        if (field == 0) key.physicalIdentity.clear();
        if (field == 1) key.power = 0;
        if (field == 2) key.rawGeneration = 0;
        if (field == 3) key.captureEpoch = 0;
        require(!key.valid() && !calibrationKeyFor(key), "empty identity or zero generation was admitted");
        require(owner.select(old, operations(), offset()), "initial select failed");
        require(!owner.select(key, operations(), offset()), "invalid key selected");
        require(s.count("create") == 1 && s.count("destroy") == 1, "invalid replacement must retire old handle without create");
        require(!owner.process(old, sample(), 1) && !owner.process(key, sample(), 1), "invalid select retained authorization");
        owner.revoke(); s.clean();
    }
    Fixture fixture; auto& s = fixture.effects; Owner owner;
    const Key exact{" ", UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT_MAX};
    require(owner.select(exact, operations(), {0, 0, 0, 0}), "exact nonempty identity/nonzero generations should be accepted");
    owner.revoke(); s.clean();
}
Ops missing(Ops ops, unsigned field) {
    if (field == 0) ops.create = nullptr;
    if (field == 1) ops.destroy = nullptr;
    if (field == 2) ops.setMode = nullptr;
    if (field == 3) ops.setOffset = nullptr;
    if (field == 4) ops.process = nullptr;
    return ops;
}
void testMissingCallbacks() {
    for (unsigned field = 0; field < 5; ++field) {
        Fixture fixture; auto& s = fixture.effects; Owner owner;
        require(!owner.select(token(), missing(operations(), field), offset()), "missing callback was accepted");
        require(s.events.empty(), "missing callback must not call create");
        require(owner.select(token(), operations(), offset()), "valid select after incomplete ops failed");
        require(!owner.select(token(), missing(operations(), field), offset()), "same-key incomplete ops was accepted");
        require(s.count("create") == 1 && s.count("destroy") == 1, "incomplete ops must retire through old destroy without create");
        require(!owner.process(token(), sample(), 1), "missing callback retained process permission");
        owner.revoke(); s.clean();
    }
}
Ops replacement(Ops ops, unsigned field) {
    if (field == 0) ops.create = create1;
    if (field == 1) ops.destroy = destroy1;
    if (field == 2) ops.setMode = mode1;
    if (field == 3) ops.setOffset = offset1;
    if (field == 4) ops.process = process1;
    return ops;
}
void testAllCallbackChanges() {
    for (unsigned field = 0; field < 5; ++field) {
        Fixture fixture; auto& s = fixture.effects; Owner owner; const auto key = token();
        require(owner.select(key, operations(), offset()), "initial select failed");
        auto old = owner.handleFor(key); auto ops = replacement(operations(), field);
        require(owner.select(key, ops, {-1, 2, -3, 9}), "callback-only replacement failed");
        require(s.count("destroy") == 1 && s.count("create") == 2 && owner.handleFor(key) != old,
                "every callback pointer change must replace resource");
        require(s.events[3].kind == "destroy" && s.events[3].bank == 0 && s.events[4].kind == "create",
                "callback change must retire with original destroy before new create");
        require(s.last("create").bank == (field == 0 ? 1 : 0), "replacement create pointer not used");
        require(s.last("mode").bank == (field == 2 ? 1 : 0), "replacement mode pointer not used");
        require(s.last("offset").bank == (field == 3 ? 1 : 0), "replacement offset pointer not used");
        require(owner.process(key, sample(), 1), "replacement cannot process");
        require(s.last("process").bank == (field == 4 ? 1 : 0), "replacement process pointer not used");
        matchesOffset(s, {-1, 2, -3, 9});
        require(owner.select(key, ops, offset()) && s.count("create") == 2, "identical replacement ops should be idempotent");
        owner.revoke(); require(s.last("destroy").bank == (field == 1 ? 1 : 0), "replacement destroy pointer not used"); s.clean();
    }
}
void testInvalidCalibration() {
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    for (unsigned field = 0; field < 4; ++field) for (float value : invalid) {
        Fixture fixture; auto& s = fixture.effects; Owner owner; auto cal = offset();
        if (field == 0) cal.x = value;
        if (field == 1) cal.y = value;
        if (field == 2) cal.z = value;
        if (field == 3) cal.weight = -1;
        require(!owner.select(token(), operations(), cal), "nonfinite calibration or negative weight selected");
        require(s.events.empty(), "invalid calibration called create");
        require(owner.select(token(), operations(), offset()), "valid calibration selection failed");
        require(!owner.select(token(), operations(), cal), "same-key invalid calibration accepted");
        require(owner.handleFor(token()) == nullptr && s.count("destroy") == 1, "invalid calibration must revoke old selection");
        owner.clear(); s.clean();
    }
}
void testUnrestrictedFiniteCalibration() {
    Fixture fixture; auto& s = fixture.effects; Owner owner;
    const float maximum = std::numeric_limits<float>::max();
    const Calibration cal{maximum, -maximum, std::numeric_limits<float>::denorm_min(), 0};
    require(owner.select(token(), operations(), cal), "finite calibration must not have axis/confidence thresholds");
    matchesOffset(s, cal);
    auto next = token(); ++next.captureEpoch;
    require(owner.select(next, operations(), {0, -0.0f, 0, INT_MAX}), "large nonnegative weight rejected");
    matchesOffset(s, {0, -0.0f, 0, INT_MAX}); owner.clear(); s.clean();
}
void testCreateFailure() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; const auto old = token();
    auto next = old; ++next.captureEpoch;
    require(owner.select(old, operations(), offset()), "initial select failed");
    s.failCreate = true;
    require(!owner.select(next, operations(), offset()), "create failure was accepted");
    require(owner.handleFor(old) == nullptr && owner.handleFor(next) == nullptr,
            "create failure must reject both old and new tokens");
    require(s.events.size() == 5 && s.events[3].kind == "destroy" && s.events[4].kind == "create-failed",
            "failed create must happen after old destroy without calibration callbacks");
    require(!owner.process(old, sample(), 1) && !owner.process(next, sample(), 1), "failed create allowed process");
    owner.clear(); owner.revoke(); require(s.count("destroy") == 1, "failed create caused repeated destroy");
    const auto effects = s.events.size();
    require(!owner.select(old, operations(), offset()), "old handle authorization revived after failed create");
    require(!owner.select(next, operations(), offset()), "failed-create token was reused");
    require(s.events.size() == effects, "retired token attempted native creation");
    ++next.captureEpoch;
    require(owner.select(next, operations(), offset()), "fresh epoch cannot recover from create failure");
    owner.clear(); s.clean();
}
void testRevokeOnce() {
    Fixture fixture; auto& s = fixture.effects;
    { Owner owner; const auto key = token();
      require(owner.select(key, operations(), offset()), "initial select failed");
      owner.clear(); owner.revoke(); owner.clear();
      require(s.count("destroy") == 1 && s.violations == 0, "repeated clear/revoke destroyed resource twice");
      require(owner.handleFor(key) == nullptr && !owner.process(key, sample(), 1), "revoked token remained usable");
    }
    require(s.count("destroy") == 1, "destructor repeated explicit destruction"); s.clean();
}
void testRetiredReselect() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; auto key = token();
    require(owner.select(key, operations(), offset()), "initial select failed"); owner.revoke();
    require(!owner.select(key, operations(), offset()), "retired token was reauthorized");
    require(s.count("create") == 1, "retired token called create again");
    const auto original = key; ++key.captureEpoch;
    require(owner.select(key, operations(), offset()), "fresh epoch after revoke failed");
    require(!owner.select(original, operations(), offset()), "switch back revived retired token");
    require(!owner.process(original, sample(), 1) && !owner.process(key, sample(), 1), "rejected switch retained handle");
    owner.revoke(); s.clean();
}
void testControllerCalibration() {
    Fixture fixture; auto& s = fixture.effects;
    auto left = token(), right = left; right.sensorIndex = 1;
    auto resumed = left; ++resumed.power; ++resumed.rawGeneration; ++resumed.captureEpoch;
    const auto lk = calibrationKeyFor(left), rk = calibrationKeyFor(right), resumedKey = calibrationKeyFor(resumed);
    require(lk && rk && resumedKey, "valid calibration key unavailable");
    require(*lk == *resumedKey, "generation leaked into persistent calibration key");
    require(!(*lk == *rk), "sensor index missing from persistent calibration key");
    auto other = left; other.physicalIdentity += "/different";
    require(!(*lk == *calibrationKeyFor(other)), "physical identity missing from calibration key");
    std::map<CalibrationKey, Calibration> controllerOnly{{*lk, {11, -12, 13, 0}}, {*rk, {-21, 22, -23, 87}}};
    const auto load = [&](const Key& key) {
        const auto found = controllerOnly.find(*calibrationKeyFor(key));
        require(found != controllerOnly.end(), "controller calibration miss; Windows fallback forbidden");
        return found->second;
    };
    { Owner owner, independentRight;
      require(owner.select(left, operations(), load(left)), "left select failed"); matchesOffset(s, {11, -12, 13, 0});
      require(independentRight.select(right, operations(), load(right)), "right select failed"); matchesOffset(s, {-21, 22, -23, 87});
      require(owner.handleFor(left) != independentRight.handleFor(right), "independent sensors shared a resource");
      require(!owner.process(right, sample(), 1) && !independentRight.process(left, sample(), 1), "cross-owner token used");
      require(owner.select(resumed, operations(), load(resumed)), "resume selection failed"); matchesOffset(s, {11, -12, 13, 0});
      require(!owner.process(left, sample(), 1), "sleep generation reused old handle");
      require(independentRight.process(right, sample(), 1), "left resume invalidated independent right owner");
    }
    require(s.count("create") == 3 && s.count("destroy") == 3, "per-controller resource accounting mismatch"); s.clean();
}
void testDtAndStationary() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; const auto key = token();
    require(owner.select(key, operations(), {0, 0, 0, 0}), "zero calibration rejected");
    require(owner.process(key, {}, 0), "all-zero stationary sample must process");
    for (unsigned i = 0; i < 7; ++i) require(bits(s.last("process").values[i]) == bits(0.0f), "stationary sample changed");
    const RawSixAxes axes{-0.0f, -23.25f, 31.5f, std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()};
    const std::array<float, 6> expected{axes.gx, axes.gy, axes.gz, axes.ax, axes.ay, axes.az};
    const float deltas[] = {0.0f, -0.0f, -1.25f, 0.0000001f, 0.016f, 1500.0f,
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    for (float dt : deltas) {
        require(owner.process(key, axes, dt), "caller dt was clamped or rejected");
        const auto& actual = s.last("process").values;
        for (unsigned i = 0; i < 6; ++i) require(bits(actual[i]) == bits(expected[i]), "raw axes were synthesized or transformed");
        require(bits(actual[6]) == bits(dt), "dt must preserve caller units and bit pattern");
    }
    owner.revoke(); s.clean();
}
void testInvalidSamples() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; const auto key = token();
    require(owner.select(key, operations(), offset()), "initial select failed");
    const float invalid[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity()};
    for (unsigned i = 0; i < 6; ++i) for (float value : invalid) {
        std::array<float, 6> a{1, 2, 3, 4, 5, 6}; a[i] = value;
        require(!owner.process(key, {a[0], a[1], a[2], a[3], a[4], a[5]}, 1), "nonfinite raw axis processed");
    }
    require(s.count("process") == 0, "invalid samples emitted effects");
    require(owner.process(key, {}, 1), "invalid sample must not revoke valid resource");
    owner.revoke(); s.clean();
}
void testExactIdentity() {
    Fixture fixture; auto& s = fixture.effects; Owner owner; const auto key = token();
    require(owner.select(key, operations(), offset()), "initial select failed");
    auto foreign = key; foreign.physicalIdentity = "CONTROLLER/CONTAINER/PHYSICAL-A";
    require(owner.handleFor(foreign) == nullptr && !owner.process(foreign, sample(), 1), "identity was case folded");
    foreign = key; foreign.physicalIdentity += " ";
    require(owner.handleFor(foreign) == nullptr, "identity was trimmed");
    foreign = key; foreign.physicalIdentity += std::string("\0tail", 5);
    require(owner.handleFor(foreign) == nullptr, "identity was compared as a C string");
    owner.revoke(); s.clean();
}
} // namespace

int main(int argc, char** argv) {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"initial-order-and-destructor", testInitial}, {"unchanged-selection", testIdempotence},
        {"physical-identity-boundary", [] { testBoundary(0); }},
        {"power-boundary", [] { testBoundary(1); }},
        {"raw-generation-boundary", [] { testBoundary(2); }},
        {"capture-epoch-boundary", [] { testBoundary(3); }},
        {"sensor-index-boundary", [] { testBoundary(4); }},
        {"invalid-keys", testInvalidKeys}, {"missing-callbacks", testMissingCallbacks},
        {"every-callback-change", testAllCallbackChanges}, {"invalid-calibration", testInvalidCalibration},
        {"finite-calibration-no-thresholds", testUnrestrictedFiniteCalibration},
        {"create-failure-revokes", testCreateFailure}, {"revoke-once", testRevokeOnce},
        {"retired-token-cannot-reselect", testRetiredReselect},
        {"calibration-source-is-controller", testControllerCalibration},
        {"stationary-and-dt-verbatim", testDtAndStationary}, {"nonfinite-raw-axes", testInvalidSamples},
        {"exact-identity", testExactIdentity}
    };
    const std::string filter = argc == 3 && std::string(argv[1]) == "--case" ? argv[2] : "";
    if (argc != 1 && filter.empty()) { std::cerr << "invalid arguments\n"; return 2; }
    unsigned ran = 0;
    for (const auto& item : cases) {
        if (!filter.empty() && filter != item.first) continue;
        try { const auto before = checks; item.second(); ++ran;
              std::cout << "PASS " << item.first << " checks=" << checks - before << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << item.first << ": " << e.what() << '\n'; return 1; }
    }
    if (!ran) { std::cerr << "unknown case\n"; return 2; }
    std::cout << "SUMMARY cases=" << ran << " checks=" << checks << '\n';
    return 0;
}
