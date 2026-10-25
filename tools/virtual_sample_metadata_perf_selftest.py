"""Extract actual before/current diagnostic metadata builders and delta blocks.

Only standalone fixtures execute. Production and the queue tests are read-only.
Requires MSVC BuildTools and the repository's actual nlohmann JSON header.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = Path(__file__).resolve().parent.parent
SHA = lambda data: hashlib.sha256(data).hexdigest()


def matching_brace(text, opening):
    depth = 0
    quote = None
    comment = None
    i = opening
    while i < len(text):
        c, pair = text[i], text[i:i + 2]
        if comment == 'line':
            if c == '\n':
                comment = None
        elif comment == 'block':
            if pair == '*/':
                comment = None
                i += 1
        elif quote:
            if c == '\\':
                i += 1
            elif c == quote:
                quote = None
        elif pair == '//':
            comment = 'line'
            i += 1
        elif pair == '/*':
            comment = 'block'
            i += 1
        elif c in ('"', "'"):
            quote = c
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError('Unclosed production block')


def declaration(text, marker):
    assert text.count(marker) == 1, marker
    start = text.rfind('\n', 0, text.index(marker)) + 1
    opening = text.index('{', text.index(marker))
    end = matching_brace(text, opening) + 1
    assert text[end] == ';', marker
    return text[start:end + 1]


def initializer(block, lazy=False):
    opening = block.index('{', block.index('return ') if lazy else block.index('='))
    return block[opening:matching_brace(block, opening) + 1]


def extract(text, current):
    selector = declaration(text, 'const auto buildHcSelectorDiagnostic =' if current else 'const json hcSelectorObj =')
    config = declaration(text, 'const auto buildTestLaneConfigDiagnostic =' if current else 'const json testLaneConfigObj =')
    marker = 'static std::string g_lastSampleSelectorFp;'
    assert text.count(marker) == 1
    start = text.rfind('\n', 0, text.index(marker)) + 1
    last_if = text.index('if (testLaneCfgFp != g_lastSampleTestLaneCfgFp)', start)
    opening = text.index('{', last_if)
    delta = text[start:matching_brace(text, opening) + 1]
    persona = ''
    if current:
        match = re.search(r'^ +const std::string sampleConfiguredPersona = inputHostConfiguredPersona\(\);\r?$', text, re.M)
        assert match is not None
        persona = match.group()
    return {'selector': selector, 'config': config, 'personaSnapshot': persona, 'delta': delta}


PREAMBLE = r'''
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;

// Count ordinary global new calls used by the real JSON allocator, separately
// from uninstrumented timing. No hardware/BUS/production runtime is linked.
static thread_local bool trackAlloc = false;
static thread_local std::uint64_t allocCalls = 0, allocBytes = 0;
void* operator new(std::size_t size) {
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    if (trackAlloc) { ++allocCalls; allocBytes += size; }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

struct SelectorLeaf {
    std::string dmiManufacturer = "Fixture Manufacturer";
    std::string dmiProduct = "Fixture Product";
    std::string dmiSystemProduct = "Fixture System Product";
    std::string selectorId = "fixture-selector-identity";
    std::string deviceClass = "fixture-handheld-class";
    std::string gyroMatrixId = "fixture-gyro-matrix-identity";
    std::string accelMatrixId = "fixture-accel-matrix-identity";
    std::string matrixSourceSha256 = std::string(64, 'a');
    bool resolved = false;
};
struct ConfigLeaf {
    bool standaloneTestLane = false, enabled = false, virtualTargetEnabled = true;
    bool targetGyroEnabled = true, motionActionEnabled = false, motionEnabled = true;
    std::string motionMode = "fixture-player-space";
    std::string motionTrigger = "fixture-motion-trigger";
    std::string targetStick = "fixture-right-stick";
};
struct SensorLeaf { bool gyroHcLegacyFields = false, accelHcLegacyFields = false; };
struct Context {
    SelectorLeaf selector;
    ConfigLeaf config;
    SensorLeaf sensor;
    std::string persona = "fixture-dualsense-data-only";
    std::string gyroProvider = "fixture-WinRT-gyroscope-provider";
    std::string accelProvider = "fixture-WinRT-accelerometer-provider";
    json recipe = {{"identity", std::string(64, 'b')}, {"revision", 1},
        {"source", {{"snapshot", "fixture-identity-only"}, {"stage", "frozen-winrt-base-candidate"}}}};
    std::uint64_t personaReads = 0, receiptReads = 0;
};
static thread_local Context* activeContext = nullptr;
struct ActiveContext {
    Context* previous;
    explicit ActiveContext(Context& c) : previous(activeContext) { activeContext = &c; }
    ~ActiveContext() { activeContext = previous; }
};
std::string inputHostConfiguredPersona() {
    ++activeContext->personaReads;
    return activeContext->persona; // stable leaf value; no fake race/wire claim
}
json inputCaptureCandidateMatrixReceipt(const SensorLeaf&) {
    ++activeContext->receiptReads;
    return activeContext->recipe; // explicit identity JSON, no recipe/BUS simulation
}
'''

ALIASES = r'''
    ActiveContext active(ctx);
    const auto& g_hcMotionSelector = ctx.selector;
    const auto& g_realStickTest = ctx.config;
    const auto& sensorMeta = ctx.sensor;
    const auto& gyroProvider = ctx.gyroProvider;
    const auto& accelProvider = ctx.accelProvider;
    json rec = json::object();
'''

BODY = r'''
static std::uint64_t assertions = 0, comparedFrames = 0;
static volatile std::uint64_t sink = 0;
static json wireExamples = json::array(), cultureCases = json::array();
void check(bool condition, const std::string& description) {
    ++assertions;
    if (!condition) throw std::runtime_error(description);
}
template<int Slot>
void pair(const Context& input, const std::string& label, int selectorExpected = -1,
          int configExpected = -1, bool keepExample = false) {
    Context a = input, b = input;
    a.personaReads = a.receiptReads = b.personaReads = b.receiptReads = 0;
    const json oldRecord = beforeFixture::sample<Slot>(a);
    const json newRecord = currentFixture::sample<Slot>(b);
    const auto oldWire = oldRecord.dump(), newWire = newRecord.dump();
    check(oldWire == newWire, label + ": output bytes differ");
    check(a.personaReads == 2 && b.personaReads == 1, label + ": persona reads != 2->1");
    check(a.receiptReads == 1, label + ": eager receipt reads != 1");
    check(b.receiptReads == (newRecord.contains("hcSelectorCandidate") ? 1u : 0u),
          label + ": lazy receipt not tied to original selector delta");
    if (selectorExpected >= 0)
        check(newRecord.contains("hcSelectorCandidate") == (selectorExpected != 0), label + ": selector delta changed");
    if (configExpected >= 0)
        check(newRecord.contains("testLaneConfig") == (configExpected != 0), label + ": config delta changed");
    if (keepExample) wireExamples.push_back({{"case", label}, {"beforeBytes", oldWire}, {"afterBytes", newWire},
        {"personaBefore", a.personaReads}, {"personaAfter", b.personaReads},
        {"receiptBefore", a.receiptReads}, {"receiptAfter", b.receiptReads}});
    ++comparedFrames;
}
void correctness() {
    Context base;
    pair<1>(base, "first", 1, 1, true);
    pair<1>(base, "same", 0, 0, true);
    for (int i = 0; i < 200; ++i) pair<1>(base, "same-repeat", 0, 0);
    const std::array<std::string SelectorLeaf::*, 8> selectors = {
        &SelectorLeaf::dmiManufacturer, &SelectorLeaf::dmiProduct, &SelectorLeaf::dmiSystemProduct,
        &SelectorLeaf::selectorId, &SelectorLeaf::deviceClass, &SelectorLeaf::gyroMatrixId,
        &SelectorLeaf::accelMatrixId, &SelectorLeaf::matrixSourceSha256};
    for (std::size_t i = 0; i < selectors.size(); ++i) {
        Context change = base;
        change.selector.*selectors[i] += "|changed-" + std::to_string(i);
        pair<1>(change, "selector-string-" + std::to_string(i), 1, 0);
        pair<1>(change, "selector-held", 0, 0);
        pair<1>(base, "selector-restore", 1, 0);
    }
    Context resolved = base; resolved.selector.resolved = !base.selector.resolved;
    pair<1>(resolved, "selector-resolved", 1, 0); pair<1>(base, "resolved-restore", 1, 0);
    Context legacy = base; legacy.sensor.gyroHcLegacyFields = true;
    pair<1>(legacy, "gyro-legacy", 1, 0); pair<1>(legacy, "gyro-legacy-held", 0, 0);
    legacy.sensor.accelHcLegacyFields = true;
    pair<1>(legacy, "both-legacy", 1, 0); legacy.sensor.gyroHcLegacyFields = false;
    pair<1>(legacy, "accel-legacy", 1, 0); pair<1>(base, "legacy-restore", 1, 0);
    const std::array<bool ConfigLeaf::*, 6> flags = {
        &ConfigLeaf::standaloneTestLane, &ConfigLeaf::enabled, &ConfigLeaf::virtualTargetEnabled,
        &ConfigLeaf::targetGyroEnabled, &ConfigLeaf::motionActionEnabled, &ConfigLeaf::motionEnabled};
    for (std::size_t i = 0; i < flags.size(); ++i) {
        Context change = base; change.config.*flags[i] = !(change.config.*flags[i]);
        pair<1>(change, "config-flag-" + std::to_string(i), 0, 1);
        pair<1>(change, "config-flag-held", 0, 0); pair<1>(base, "config-flag-restore", 0, 1);
    }
    const std::array<std::string ConfigLeaf::*, 3> strings = {
        &ConfigLeaf::motionMode, &ConfigLeaf::motionTrigger, &ConfigLeaf::targetStick};
    for (std::size_t i = 0; i < strings.size(); ++i) {
        Context change = base; change.config.*strings[i] += "-changed";
        pair<1>(change, "config-string-" + std::to_string(i), 0, 1);
        pair<1>(change, "config-string-held", 0, 0); pair<1>(base, "config-string-restore", 0, 1);
    }
    Context persona = base; persona.persona = "fixture-steam-deck-composite";
    pair<1>(persona, "persona-change", 0, 1, true); pair<1>(persona, "persona-held", 0, 0);
    pair<1>(base, "persona-restore", 0, 1);
    Context providers = base; providers.gyroProvider = "dynamic-gyro-provider";
    pair<1>(providers, "gyro-provider-change", 0, 1);
    providers.accelProvider = "dynamic-accel-provider";
    pair<1>(providers, "accel-provider-change", 0, 1); pair<1>(providers, "providers-held", 0, 0);
    pair<1>(base, "providers-restore", 0, 1);
    Context receipt = base; receipt.recipe["revision"] = 2;
    receipt.recipe["source"]["snapshot"] = "changed-receipt-only";
    pair<1>(receipt, "receipt-change-fingerprint-same", 0, 0, true);
    receipt.selector.selectorId += "-publish-latest-receipt";
    pair<1>(receipt, "receipt-published-on-original-fingerprint-change", 1, 0, true);
    pair<1>(base, "receipt-restore", 1, 0);
    Context multi = base;
    for (auto member : selectors) multi.selector.*member += "-multi";
    multi.selector.resolved = true; multi.sensor.gyroHcLegacyFields = multi.sensor.accelHcLegacyFields = true;
    for (auto member : flags) multi.config.*member = !(multi.config.*member);
    for (auto member : strings) multi.config.*member += "-multi";
    multi.persona = "multi-persona"; multi.gyroProvider = "multi-gyro"; multi.accelProvider = "multi-accel";
    multi.recipe["revision"] = 3;
    pair<1>(multi, "many-fields-change", 1, 1, true); pair<1>(multi, "many-fields-held", 0, 0);
    // Separate template instances retain the production static declarations,
    // while giving each fixture an independent logical restart.
    pair<2>(base, "independent-logical-restart", 1, 1);
    pair<2>(base, "independent-restart-held", 0, 0);
    const auto originalLocale = std::locale();
    for (const char* name : {"C", "en-US.UTF-8", "fr-FR.UTF-8", "zh-CN.UTF-8"}) {
        try {
            std::locale::global(std::locale(name));
            pair<2>(base, std::string("culture-stable-") + name, 0, 0);
            cultureCases.push_back({{"locale", name}, {"available", true}});
        } catch (const std::runtime_error& error) {
            // Failed locale construction is not a skipped correctness failure.
            if (std::string(error.what()).find("locale") == std::string::npos) throw;
            cultureCases.push_back({{"locale", name}, {"available", false}, {"reason", error.what()}});
        }
    }
    std::locale::global(originalLocale);
    Context unicode = base;
    unicode.selector.dmiManufacturer = u8"\u8bbe\u5907 \"quoted\" \\ slash\n";
    unicode.persona = u8"\u624b\u67c4-\u00e9";
    unicode.gyroProvider = u8"\u9640\u87ba-\u00df"; unicode.accelProvider = u8"\u52a0\u901f-\u03a9";
    pair<2>(unicode, "unicode-leaf-values", 1, 1, true); pair<2>(unicode, "unicode-held", 0, 0);
    std::mt19937 rng(0x20261014);
    Context random = base;
    pair<3>(random, "random-initial", 1, 1);
    for (int i = 0; i < 3000; ++i) {
        switch (rng() % 8u) {
            case 0: random.selector.*selectors[rng() % selectors.size()] = "random-selector-" + std::to_string(rng()); break;
            case 1: random.config.*flags[rng() % flags.size()] = (rng() & 1u) != 0; break;
            case 2: random.config.*strings[rng() % strings.size()] = "random-config-" + std::to_string(rng()); break;
            case 3: random.persona = "random-persona-" + std::to_string(rng()); break;
            case 4: random.gyroProvider = "random-gyro-" + std::to_string(rng()); random.accelProvider = "random-accel-" + std::to_string(rng()); break;
            case 5: random.sensor.gyroHcLegacyFields = !random.sensor.gyroHcLegacyFields; break;
            case 6: random.sensor.accelHcLegacyFields = !random.sensor.accelHcLegacyFields; break;
            default: random.recipe["revision"] = i; break;
        }
        pair<3>(random, "random-change"); pair<3>(random, "random-held", 0, 0);
    }
}
using SampleFn = json (*)(Context&);
struct Measurement { double ns; std::uint64_t calls, bytes, persona, receipt; };
Measurement measure(SampleFn sample, Context& c, int workload, int frames, bool countAlloc) {
    c.personaReads = c.receiptReads = 0;
    allocCalls = allocBytes = 0; trackAlloc = countAlloc;
    std::uint64_t sum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        if (workload == 1 || workload == 3) c.selector.resolved = (i & 1) != 0;
        if (workload == 2 || workload == 3) c.config.enabled = (i & 1) != 0;
        if (workload == 4) c.recipe["revision"] = i;
        { const json record = sample(c); sum += record.size(); }
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    trackAlloc = false; sink = sum;
    return {elapsed / frames, allocCalls, allocBytes, c.personaReads, c.receiptReads};
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end()); return values[values.size() / 2];
}
template<int Slot>
json warmBenchmark(const char* name, int workload) {
    Context oldContext, newContext;
    measure(beforeFixture::sample<Slot>, oldContext, workload, 1000, false);
    measure(currentFixture::sample<Slot>, newContext, workload, 1000, false);
    constexpr int frames = 20000, repeats = 5;
    std::vector<double> oldTimes, newTimes;
    for (int i = 0; i < repeats; ++i) {
        if (i & 1) {
            newTimes.push_back(measure(currentFixture::sample<Slot>, newContext, workload, frames, false).ns);
            oldTimes.push_back(measure(beforeFixture::sample<Slot>, oldContext, workload, frames, false).ns);
        } else {
            oldTimes.push_back(measure(beforeFixture::sample<Slot>, oldContext, workload, frames, false).ns);
            newTimes.push_back(measure(currentFixture::sample<Slot>, newContext, workload, frames, false).ns);
        }
    }
    constexpr int allocationFrames = 2000;
    const auto a = measure(beforeFixture::sample<Slot>, oldContext, workload, allocationFrames, true);
    const auto b = measure(currentFixture::sample<Slot>, newContext, workload, allocationFrames, true);
    check(a.persona == 2u * allocationFrames && b.persona == allocationFrames, std::string(name) + ": benchmark persona reads changed");
    check(a.receipt == allocationFrames, std::string(name) + ": benchmark eager receipt reads changed");
    const auto oldMedian = median(oldTimes), newMedian = median(newTimes);
    return {{"name", name}, {"framesPerSample", frames}, {"samples", repeats}, {"allocationFrames", allocationFrames},
        {"beforeMedianNs", oldMedian}, {"afterMedianNs", newMedian}, {"ratio", newMedian / oldMedian},
        {"beforeSamplesNs", oldTimes}, {"afterSamplesNs", newTimes},
        {"beforeNewCallsPerFrame", static_cast<double>(a.calls) / allocationFrames},
        {"afterNewCallsPerFrame", static_cast<double>(b.calls) / allocationFrames},
        {"beforeNewBytesPerFrame", static_cast<double>(a.bytes) / allocationFrames},
        {"afterNewBytesPerFrame", static_cast<double>(b.bytes) / allocationFrames},
        {"beforePersonaReadsPerFrame", static_cast<double>(a.persona) / allocationFrames},
        {"afterPersonaReadsPerFrame", static_cast<double>(b.persona) / allocationFrames},
        {"beforeReceiptReadsPerFrame", static_cast<double>(a.receipt) / allocationFrames},
        {"afterReceiptReadsPerFrame", static_cast<double>(b.receipt) / allocationFrames}};
}
template<int Slot>
json coldCall(bool newFirst) {
    Context a, b;
    Measurement oldResult{}, newResult{};
    if (newFirst) {
        newResult = measure(currentFixture::sample<Slot>, b, 0, 1, true);
        oldResult = measure(beforeFixture::sample<Slot>, a, 0, 1, true);
    } else {
        oldResult = measure(beforeFixture::sample<Slot>, a, 0, 1, true);
        newResult = measure(currentFixture::sample<Slot>, b, 0, 1, true);
    }
    check(oldResult.persona == 2 && newResult.persona == 1, "cold persona read count");
    check(oldResult.receipt == 1 && newResult.receipt == 1, "cold receipt read count");
    return {{"slot", Slot}, {"newMeasuredFirst", newFirst}, {"beforeNs", oldResult.ns}, {"afterNs", newResult.ns},
        {"beforeNewCalls", oldResult.calls}, {"afterNewCalls", newResult.calls},
        {"beforeNewBytes", oldResult.bytes}, {"afterNewBytes", newResult.bytes}};
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("results path required");
        correctness();
        json result = json::object();
        result["schema"] = 1;
        result["correctness"] = {{"passed", true}, {"comparedFrames", comparedFrames},
            {"outputBytesEqual", true}, {"personaReadsBefore", 2}, {"personaReadsAfter", 1},
            {"recipeChangeWithUnchangedFingerprintPreserved", true},
            {"selectorStringFieldsTested", 8}, {"selectorResolvedAndTwoLegacyFlagsTested", true},
            {"configBooleanFieldsTested", 6}, {"configStringFieldsTested", 3},
            {"personaAndTwoProvidersTested", true}, {"randomTransitions", 3000},
            {"logicalRestartsIndependent", true}, {"fakeConcurrentPersona", false}};
        result["wireExamples"] = wireExamples; result["cultureCases"] = cultureCases;
        result["jsonHeaderVersion"] = std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
            std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." + std::to_string(NLOHMANN_JSON_VERSION_PATCH);
        if (argc == 2) {
            result["coldFirstSamples"] = json::array({coldCall<200>(false), coldCall<201>(true),
                coldCall<202>(false), coldCall<203>(true), coldCall<204>(false), coldCall<205>(true),
                coldCall<206>(false), coldCall<207>(true), coldCall<208>(false), coldCall<209>(true)});
            result["warmBenchmarks"] = json::array({warmBenchmark<101>("same", 0),
                warmBenchmark<102>("selector-every-frame", 1), warmBenchmark<103>("config-every-frame", 2),
                warmBenchmark<104>("selector-and-config-every-frame", 3),
                warmBenchmark<105>("receipt-change-fingerprint-unchanged", 4)});
        }
        result["correctness"]["assertions"] = assertions;
        result["measurementScope"] = "extracted two JSON constructions/lambdas + exact fingerprints/delta + metadata record update/destruction; excludes dump, unrelated sample fields, queues, BUS and full product";
        result["allocationScope"] = "ordinary C++ global operator new calls requested by fixture and real JSON allocator; timing measured separately with counters disabled";
        result["coldScope"] = "independent static-fingerprint instances; libraries already warm, not process startup";
        result["recipeLeaf"] = "identity JSON copy; production receipt tree/provider work is not simulated";
        result["hardwareAccess"] = false; result["productRun"] = false;
        std::ofstream output(argv[1], std::ios::binary); output << result.dump(2) << '\n'; output.close();
        if (!output) throw std::runtime_error("cannot save results");
        std::cout << "METADATA_PASS frames=" << comparedFrames << " assertions=" << assertions
            << " bytesEqual=true personaReads=2->1 hardwareAccess=false\n";
        return 0;
    } catch (const std::exception& error) {
        trackAlloc = false;
        std::cerr << "METADATA_FAIL " << error.what() << '\n'; return 1;
    }
}
'''


def bound_fixture(old, new):
    def implementation(namespace, parts):
        exact = '\n'.join(parts[k] for k in ['selector', 'personaSnapshot', 'config', 'delta'] if parts[k])
        return '\nnamespace ' + namespace + ' {\ntemplate<int Slot> json sample(Context& ctx) {\n' + ALIASES + '\n' + exact + '\n    return rec;\n}\n}\n'
    return PREAMBLE + implementation('beforeFixture', old) + implementation('currentFixture', new) + BODY


def dump_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--current', type=Path, default=REPO / 'native/main.cpp')
    parser.add_argument('--vcvars', type=Path, default=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'))
    args = parser.parse_args()
    before_path, current_path, out = args.before.resolve(), args.current.resolve(), args.output.resolve()
    if out.exists():
        raise SystemExit('Refusing to overwrite existing evidence: ' + str(out))
    for path in [out, args.vcvars, REPO]:
        if any(c in str(path) for c in '\n\r%&|<>^!"'):
            raise SystemExit('Unsafe shell metacharacter in build path')
    before_bytes, current_bytes = before_path.read_bytes(), current_path.read_bytes()
    old_text, new_text = before_bytes.decode('utf-8-sig'), current_bytes.decode('utf-8-sig')
    old, new = extract(old_text, False), extract(new_text, True)
    assert initializer(old['selector']) == initializer(new['selector'], True), 'Selector fields changed'
    assert initializer(old['config']).replace('inputHostConfiguredPersona()', 'sampleConfiguredPersona') == initializer(new['config'], True), 'Config fields changed beyond persona snapshot'
    expected_delta = old['delta'].replace('rec["hcSelectorCandidate"] = hcSelectorObj;', 'rec["hcSelectorCandidate"] = buildHcSelectorDiagnostic();').replace('rec["testLaneConfig"] = testLaneConfigObj;', 'rec["testLaneConfig"] = buildTestLaneConfigDiagnostic();').replace('testLaneCfgFp += inputHostConfiguredPersona();', 'testLaneCfgFp += sampleConfiguredPersona;')
    assert expected_delta == new['delta'], 'Delta changed beyond permitted builder calls/persona snapshot'
    assert sum(x.count('inputHostConfiguredPersona()') for x in old.values()) == 2
    assert sum(x.count('inputHostConfiguredPersona()') for x in new.values()) == 1
    assert current_bytes == current_path.read_bytes(), 'Current source changed during initial capture; retry with a fresh evidence directory'
    if not args.vcvars.is_file():
        raise SystemExit('MSVC vcvars64.bat not found: ' + str(args.vcvars))
    out.mkdir(parents=True)
    header = REPO / 'deps/json/json.hpp'
    binding = {'schema': 1, 'capturedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'beforePath': str(before_path), 'beforeMainSha256': SHA(before_bytes),
        'currentPath': str(current_path), 'currentMainSha256AtCapture': SHA(current_bytes),
        'testPath': str(Path(__file__).resolve()), 'testSha256': SHA(Path(__file__).read_bytes()),
        'jsonHeaderPath': str(header), 'jsonHeaderSha256': SHA(header.read_bytes()),
        'selectorInitializerByteIdentical': True, 'configInitializerIdenticalAfterExpectedPersonaSubstitution': True,
        'deltaIdenticalAfterOnlyBuilderCallsAndPersonaSubstitution': True,
        'productionEdited': False, 'queueTestEdited': False, 'fakeScope': ['leaf structs', 'stable persona getter', 'identity recipeReceipt JSON'],
        'sourceFragments': {'before': {}, 'current': {}}}
    for label, parts, text in [('before', old, old_text), ('current', new, new_text)]:
        for name, code in parts.items():
            if not code:
                continue
            fragment = out / (label + '-' + name + '.cpp.txt')
            fragment.write_text(code, encoding='utf-8', newline='')
            start = text.index(code)
            binding['sourceFragments'][label][name] = {'path': str(fragment), 'sha256': SHA(code.encode()),
                'startLine': text[:start].count('\n') + 1, 'endLine': text[:start + len(code)].count('\n') + 1}
    fixture = out / 'bound-metadata-fixture.cpp'
    fixture.write_text(bound_fixture(old, new), encoding='utf-8', newline='\n')
    binding['fixtureSha256'] = SHA(fixture.read_bytes())
    dump_json(out / 'source-binding.json', binding)
    profiles = {}
    for profile, flags in [('release', ['/O2', '/DNDEBUG']), ('checked', ['/Od', '/RTC1'])]:
        exe = out / (profile + '.exe')
        command = ['cl.exe', '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/utf-8', '/MT', *flags,
            str(fixture), '/I' + str(header.parent), '/Fo' + str(out / (profile + '.obj')), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO']
        batch = out / ('build-' + profile + '.cmd')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "' + str(args.vcvars) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\nwhere cl.exe\n' + subprocess.list2cmdline(command) + '\n', encoding='utf-8')
        built = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=out, capture_output=True, encoding='utf-8', errors='replace')
        build_log = out / (profile + '-build.log')
        build_log.write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            print(built.stdout + built.stderr)
            raise SystemExit(built.returncode)
        results_path = out / (profile + '-results.json')
        run_command = [str(exe), str(results_path)] + (['--correctness-only'] if profile == 'checked' else [])
        ran = subprocess.run(run_command, cwd=out, capture_output=True, encoding='utf-8', errors='replace', timeout=60)
        (out / (profile + '-run.log')).write_text(ran.stdout + ran.stderr, encoding='utf-8')
        print(profile, ran.stdout.strip(), flush=True)
        if ran.returncode:
            print(ran.stderr)
            raise SystemExit(ran.returncode)
        data = json.loads(results_path.read_text('utf-8'))
        assert data['correctness']['passed'] and data['correctness']['outputBytesEqual']
        profiles[profile] = {'buildExitCode': built.returncode, 'runExitCode': ran.returncode,
            'command': command, 'binarySha256': SHA(exe.read_bytes()), 'resultsPath': str(results_path),
            'resultsSha256': SHA(results_path.read_bytes()), 'correctness': data['correctness'],
            'buildLogSha256': SHA(build_log.read_bytes())}
    now_bytes = current_path.read_bytes()
    binding['beforeUnchangedDuringRun'] = before_bytes == before_path.read_bytes()
    binding['currentMetadataFragmentsUnchangedDuringRun'] = new == extract(now_bytes.decode('utf-8-sig'), True)
    binding['currentMainSha256AfterRun'] = SHA(now_bytes)
    binding['wholeCurrentMainUnchangedDuringRun'] = current_bytes == now_bytes
    binding['testUnchangedDuringRun'] = binding['testSha256'] == SHA(Path(__file__).read_bytes())
    binding['jsonHeaderUnchangedDuringRun'] = binding['jsonHeaderSha256'] == SHA(header.read_bytes())
    assert all(binding[k] for k in ['beforeUnchangedDuringRun', 'currentMetadataFragmentsUnchangedDuringRun', 'testUnchangedDuringRun', 'jsonHeaderUnchangedDuringRun'])
    dump_json(out / 'run-binding.json', binding)
    dump_json(out / 'result.json', {'schema': 1, 'completedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'sourceBindingPath': str(out / 'run-binding.json'), 'sourceBindingSha256': SHA((out / 'run-binding.json').read_bytes()),
        'profiles': profiles, 'productionEdited': False, 'deviceReads': False, 'fullBusSimulated': False,
        'wholeMainIdentityClaim': 'capture hashes only; concurrent unrelated changes are permitted',
        'limits': ['Metadata excerpt equivalence only, not whole-sample wire or real device/CPU proof.',
            'Cold means first delta for an independent static instance, not full process startup.',
            'Recipe getter returns fixture identity JSON; cost depends on this recorded leaf shape.']})
    for row in json.loads((out / 'release-results.json').read_text('utf-8'))['warmBenchmarks']:
        print(row['name'], 'ns', round(row['beforeMedianNs'], 2), '->', round(row['afterMedianNs'], 2),
            'new_calls', row['beforeNewCallsPerFrame'], '->', row['afterNewCallsPerFrame'],
            'new_bytes', row['beforeNewBytesPerFrame'], '->', row['afterNewBytesPerFrame'])
    print('EVIDENCE', out)


if __name__ == '__main__':
    main()