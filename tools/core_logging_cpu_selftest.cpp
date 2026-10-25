// Compile only through core_logging_cpu_selftest.mjs: bound headers/functions live in evidence.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <json.hpp>
#include "baseline/buffered_domain_log.h"
#include "baseline/log_frequency_policy.h"
#include "candidate/buffered_domain_log.h"
#include "candidate/log_frequency_policy.h"
#include "current/buffered_domain_log.h"
#include "current/log_frequency_policy.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
static constexpr unsigned long long kLogMaxBytes = 5ULL * 1024 * 1024;
static ULONGLONG auditTick = 100;
static unsigned long long tickCalls = 0;
static ULONGLONG fixtureTick() { ++tickCalls; return auditTick; }
static DWORD fixturePid() { return 12345; }
#define GetTickCount64 fixtureTick
#define GetCurrentProcessId fixturePid
#include "bound_domain_calls.h"
#include "bound_events.h"
static json checks = json::array();
static unsigned failures = 0;
static void check(const std::string& name, bool pass) {
    checks.push_back({{"name", name}, {"pass", pass}}); if (!pass) ++failures;
}
static std::string read(const fspath::path& p) {
    std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}};
}
static void seed(const fspath::path& p, const std::string& text) { std::ofstream out(p, std::ios::binary); out << text; }
static std::vector<json> rows(const fspath::path& p) {
    std::ifstream in(p, std::ios::binary); std::string line; std::vector<json> result;
    while (std::getline(in, line)) result.push_back(json::parse(line)); return result;
}
template<class Log> static void bufferSuite(const fspath::path& root, const std::string& label) {
    const auto dir = root / label; fspath::create_directories(dir);
    auto ck = [&](const char* name, bool pass) { check(label + "/" + name, pass); };
    auto noRotate = [](const auto&, auto) {};
    {
        Log log; const auto p = dir / "batch.log"; std::string expected;
        bool ok = true, pending = true;
        for (unsigned i = 0; i < 1000; ++i) {
            const auto line = json({{"i", i}, {"text", "ASCII/中文/\\n/\\\""}}).dump();
            ok = log.append(p.wstring(), line, 100 + i / 100, false, kLogMaxBytes, noRotate) && ok;
            expected += line + '\n'; pending = pending && log.pendingRows() == (i + 1) % 16;
        }
        log.flush(); ck("1000-exact-bytes-count-order", ok && pending && read(p) == expected);
        ck("flush-retains-open-stream", log.isOpen() && log.pendingRows() == 0);
        log.close(); ck("close-drains-and-closes", !log.isOpen() && read(p) == expected);
        log.flush(); log.close(); ck("repeat-empty-close-no-new-bytes", read(p) == expected);
    }
    {
        Log log; const auto p = dir / "time.log";
        log.append(p.wstring(), "a", 999, false, kLogMaxBytes, noRotate);
        ck("before-1000ms-keeps-tail", log.pendingRows() == 1);
        log.append(p.wstring(), "b", 1000, false, kLogMaxBytes, noRotate);
        ck("1000ms-exact-trigger", log.pendingRows() == 0 && read(p) == "a\nb\n");
        log.append(p.wstring(), "c", 1999, false, kLogMaxBytes, noRotate);
        ck("elapsed-999-keeps-tail", log.pendingRows() == 1);
        log.append(p.wstring(), "d", 2000, false, kLogMaxBytes, noRotate);
        ck("elapsed-1000-drains-previous-tail", log.pendingRows() == 0 && read(p) == "a\nb\nc\nd\n");
        log.append(p.wstring(), "error", 2001, true, kLogMaxBytes, noRotate);
        ck("urgent-visible-immediately", log.pendingRows() == 0 && read(p) == "a\nb\nc\nd\nerror\n");
    }
    {
        Log log; const auto a = dir / "path-a.log", b = dir / "path-b.log";
        log.append(a.wstring(), "old-tail", 100, false, kLogMaxBytes, noRotate);
        log.append(b.wstring(), "new-tail", 101, false, kLogMaxBytes, noRotate);
        log.close(); ck("path-switch-flushes-old-before-new", read(a) == "old-tail\n" && read(b) == "new-tail\n");
        fspath::remove(b); log.append(b.wstring(), "after-clear", 102, false, kLogMaxBytes, noRotate); log.close();
        ck("clear-close-does-not-resurrect-tail", read(b) == "after-clear\n");
    }
    {
        Log log; const auto p = dir / "export.log", copy = dir / "export-copy.log";
        log.append(p.wstring(), "tail", 100, false, kLogMaxBytes, noRotate);
        log.flush(); fspath::copy_file(p, copy);
        log.append(p.wstring(), "continues", 101, false, kLogMaxBytes, noRotate); log.close();
        ck("export-tail-snapshot-and-continued-order", read(copy) == "tail\n" && read(p) == "tail\ncontinues\n");
    }
    {
        Log log; const auto p = dir / "interrupted.log"; seed(p, "interrupted");
        log.append(p.wstring(), "complete", 100, true, kLogMaxBytes, noRotate); log.close();
        ck("open-only-jsonl-tail-boundary-retained", read(p) == "interrupted\ncomplete\n");
    }
    {
        Log log; const auto p = dir / "rotation.log"; unsigned attempts = 0;
        auto rotate = [&](const auto& file, auto limit) { ++attempts; bound_baseline::rotateOneLogFile(file, limit); };
        log.append(p.wstring(), "1234567", 100, false, 8, rotate);
        ck("threshold-crossing-row-not-prematurely-rotated", attempts == 0);
        log.append(p.wstring(), "x", 101, false, 8, rotate); log.close();
        ck("rotation-before-next-row-exact-archive", attempts == 1 && read(p.wstring() + L".1") == "1234567\n" && read(p) == "x\n");
        log.append(p.wstring(), "1234567", 102, false, 8, rotate);
        log.append(p.wstring(), "y", 103, false, 8, rotate); log.close();
        ck("successful-rotation-no-one-second-delay", attempts == 2 && read(p.wstring() + L".1") == "x\n1234567\n" && read(p) == "y\n");
    }
    {
        Log log; const auto p = dir / "failed-rotation.log"; unsigned attempts = 0;
        const auto backup = fspath::path(p.wstring() + L".1"); fspath::create_directory(backup); seed(backup / "block", "retained");
        auto rotate = [&](const auto& file, auto limit) { ++attempts; bound_baseline::rotateOneLogFile(file, limit); };
        log.append(p.wstring(), "1234567", 100, false, 8, rotate);
        log.append(p.wstring(), "a", 101, false, 8, rotate);
        log.append(p.wstring(), "b", 1100, false, 8, rotate);
        ck("backup-remove-failure-retry-999ms-blocked", attempts == 1);
        log.append(p.wstring(), "c", 1101, false, 8, rotate); log.close();
        ck("failure-budget-exact-1000ms-and-no-loss", attempts == 2 && read(p) == "1234567\na\nb\nc\n" && read(backup / "block") == "retained");
    }
    {
        Log log; const auto p = dir / "rename-blocked.log"; seed(p, "1234567\n");
        HANDLE held = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        unsigned attempts = 0;
        auto rotate = [&](const auto& file, auto limit) { ++attempts; bound_baseline::rotateOneLogFile(file, limit); };
        bool ok = log.append(p.wstring(), "a", 100, true, 8, rotate); log.close();
        if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
        ck("real-rename-denied-retains-active-records", held != INVALID_HANDLE_VALUE && ok && attempts == 1 && read(p) == "1234567\na\n");
    }
    {
        Log log; const auto p = dir / "missing-parent" / "open.log";
        ck("failed-open-is-false-and-closed", !log.append(p.wstring(), "lost", 100, false, 8, noRotate) && !log.isOpen() && log.pendingRows() == 0);
        fspath::create_directory(p.parent_path());
        ck("open-retry-remains-per-append", log.append(p.wstring(), "recovered", 101, true, 8, noRotate) && read(p) == "recovered\n");
    }
    {
        Log log; const auto p = dir / "throwing-rotate.log"; seed(p, "1234567\n"); bool threw = false;
        try { log.append(p.wstring(), "not-written", 100, false, 8, [](const auto&, auto) { throw std::runtime_error("fixture"); }); }
        catch (const std::runtime_error&) { threw = true; }
        unsigned attempts = 0; auto retry = [&](const auto&, auto) { ++attempts; };
        log.append(p.wstring(), "next", 101, true, 8, retry); log.close();
        ck("throwing-rotation-preserves-budget-and-old-tail", threw && attempts == 0 && read(p) == "1234567\nnext\n");
    }
    {
        const auto p = dir / "destructor.log"; { Log log; log.append(p.wstring(), "tail", 100, false, kLogMaxBytes, noRotate); }
        ck("stream-destructor-retains-tail", read(p) == "tail\n");
    }
}
static void policySuite() {
    bool equivalent = true; std::uint64_t tested = 0;
    auto compare = [&](std::string_view value) {
        for (bool explicitFailure : {false, true}) {
            const bool ref = baseline::logging::detailNeedsImmediateFlush(value, explicitFailure);
            equivalent = equivalent && candidate::logging::detailNeedsImmediateFlush(value, explicitFailure) == ref && current::logging::detailNeedsImmediateFlush(value, explicitFailure) == ref;
            ++tested;
        }
    };
    for (const auto& value : directEvents) compare(value);
    for (const std::string token : {"fault", "fail", "error", "reject", "timeout"}) {
        for (unsigned i = 0; i < 128; ++i) {
            compare(std::string(i, 'x') + token); compare(token + std::string(i, 'x'));
            compare(std::string(i, '\0') + token + std::string(i, '\0'));
            for (size_t cut = 0; cut <= token.size(); ++cut) compare(std::string(i, 'x') + token.substr(0, cut));
        }
    }
    for (const std::string value : {"", "FAULT", "Fail", "ERROR", "Reject", "TIMEOUT", "default", "not-a-failure", "\xFF\xFE"}) compare(value);
    const std::string alphabet = "faulterimo";
    std::string value;
    auto exhaustive = [&](auto&& self, unsigned remaining) -> void {
        compare(value); if (!remaining) return;
        for (char c : alphabet) { value.push_back(c); self(self, remaining - 1); value.pop_back(); }
    };
    exhaustive(exhaustive, 5);
    std::mt19937 random(20261017); const std::array<std::string, 5> tokens = {"fault", "fail", "error", "reject", "timeout"};
    for (unsigned i = 0; i < 50000; ++i) {
        std::string text(random() % 128, '\0'); for (auto& c : text) c = static_cast<char>(random() & 255);
        if ((i & 1) == 0) text.insert(random() % (text.size() + 1), tokens[random() % tokens.size()]); compare(text);
    }
    check("policy/candidate-and-current-equivalent-" + std::to_string(tested) + "-decisions", equivalent);
    bool lanes = true;
    for (bool a : {false, true}) for (bool b : {false, true}) for (bool c : {false, true})
        lanes = lanes && static_cast<int>(baseline::logging::nativeLane(a,b,c)) == static_cast<int>(current::logging::nativeLane(a,b,c)) && static_cast<int>(candidate::logging::nativeLane(a,b,c)) == static_cast<int>(current::logging::nativeLane(a,b,c));
    check("policy/all-eight-native-lane-states", lanes);
    bool flags = true;
    for (const std::string s : {"", "1", "enabled", " EnAbLeD\t\r\n", "disabled", "0", "true", "enabledX", "\venabled\f"})
        flags = flags && baseline::logging::explicitLogFlag(s) == current::logging::explicitLogFlag(s) && candidate::logging::explicitLogFlag(s) == current::logging::explicitLogFlag(s);
    check("policy/explicit-flags-unchanged", flags);
    current::logging::SnapshotLogState snapshot;
    check("policy/snapshot-first-repeat-content-generation", snapshot.observe("a", 1) && !snapshot.observe("a", 1) && snapshot.observe("a", 2) && snapshot.observe("b", 2));
    current::logging::BusRateLogState bus;
    check("policy/bus-category-edges", !bus.observe(0) && bus.observe(1) && !bus.observe(1) && bus.observe(0));
    check("policy/samples-retained", current::logging::inputSampleKind("sample") && current::logging::inputSampleKind("controller-family-imu") && !current::logging::inputSampleKind("event"));
    bool tree = true;
    for (unsigned code : {0, 7, 0x8000}) for (bool standby : {false,true}) for (bool eligible : {false,true})
        tree = tree && current::logging::sleepDeviceTreeSample(code,standby,eligible) == (code == 7 && !standby && !eligible);
    check("policy/device-tree-classification", tree);
    static_assert(candidate::logging::detailNeedsImmediateFlush("input-fault", false));
    static_assert(!candidate::logging::detailNeedsImmediateFlush("sample", false));
    static_assert(noexcept(candidate::logging::detailNeedsImmediateFlush("sample", false)));
}
using Append = void (*)(const char*, const char*, const json&);
template<class Log> static void domainSuite(const fspath::path& dir, const std::string& label, std::wstring& dataRoot, std::atomic<bool>* gates, Log (&streams)[3], unsigned& pathCalls, unsigned& timestampCalls, Append append) {
    fspath::create_directories(dir); dataRoot = dir.wstring(); gates[0] = false; gates[1] = false;
    const json payload = {{"i",1},{"pid",77},{"writerPid",999},{"ok",true}};
    auto ck = [&](const char* name, bool pass) { check(label + "/" + name, pass); };
    const auto beforeTick = tickCalls; append("virtual", "sample", payload); append("gyro", "sample", payload); append("focus", "sample", payload); append(nullptr,"sample",payload);
    ck("off-no-path-json-timestamp-tick-file-work", pathCalls == 0 && timestampCalls == 0 && tickCalls == beforeTick && fspath::is_empty(dir));
    gates[0] = true; gates[1] = true; auditTick = 100;
    append("virtual", "virtual-sample", payload);
    ck("success-remains-buffered", streams[1].pendingRows() == 1);
    append("virtual", "virtual-write-error", {{"i",2},{"ok",true}});
    auto r = rows(dir / "virtual-gamepad.log");
    ck("named-error-flushes-prior-success-in-order", r.size() == 2 && r[0]["i"] == 1 && r[1]["i"] == 2 && streams[1].pendingRows() == 0);
    ck("payload-and-writer-identity-preserved", r.size() == 2 && r[0]["pid"] == 77 && r[0]["writerPid"] == 12345 && r[0]["event"] == "virtual-sample");
    append("gyro", "sample", {{"ok",false}}); append("focus", "sample", {{"success",false}});
    ck("explicit-ok-and-success-false-immediate", streams[0].pendingRows() == 0 && streams[2].pendingRows() == 0 && rows(dir / "gyro-motion.log").size() == 1 && rows(dir / "focus-trace.log").size() == 1);
    append("gyro", "sample", {{"ok","false"}});
    ck("nonboolean-false-not-promoted", streams[0].pendingRows() == 1);
    for (unsigned i = 3; i < 1003; ++i) { ++auditTick; append("virtual", "virtual-sample", {{"i",i},{"ok",true}}); }
    for (auto& stream : streams) stream.flush(); r = rows(dir / "virtual-gamepad.log");
    bool ordered = r.size() == 1002; for (size_t i = 0; i < r.size(); ++i) ordered = ordered && r[i]["i"] == i + 1;
    ck("actual-writer-1002-records-no-drop-reorder", ordered);
    const auto snapshot = dir / "export-copy.log"; fspath::copy_file(dir / "virtual-gamepad.log", snapshot);
    ck("actual-writer-export-contains-tail", read(snapshot) == read(dir / "virtual-gamepad.log"));
    gates[0] = false; gates[1] = false; for (auto& stream : streams) stream.close();
    const auto beforePath = pathCalls; const auto beforeTime = timestampCalls; append("virtual", "virtual-sample", payload);
    ck("gate-off-close-does-not-reopen", !streams[1].isOpen() && beforePath == pathCalls && beforeTime == timestampCalls && read(snapshot) == read(dir / "virtual-gamepad.log"));
}
using Classify = bool (*)(std::string_view, bool);
__declspec(noinline) static bool baselineClassify(std::string_view s, bool b) { return baseline::logging::detailNeedsImmediateFlush(s,b); }
__declspec(noinline) static bool candidateClassify(std::string_view s, bool b) { return candidate::logging::detailNeedsImmediateFlush(s,b); }
__declspec(noinline) static bool currentClassify(std::string_view s, bool b) { return current::logging::detailNeedsImmediateFlush(s,b); }
static unsigned long long cpuCycles() { ULONG64 cycles = 0; if (!QueryThreadCycleTime(GetCurrentThread(), &cycles)) throw std::runtime_error("QueryThreadCycleTime failed"); return cycles; }
static volatile unsigned long long resultSink = 0;
struct Measurement { double cycles, ns; };
template<class Work> static Measurement measure(unsigned iterations, Work work) {
    auto start = std::chrono::steady_clock::now(); auto before = cpuCycles(); work(); auto after = cpuCycles(); auto end = std::chrono::steady_clock::now();
    return {double(after - before) / iterations, std::chrono::duration<double,std::nano>(end - start).count() / iterations};
}
static double median(std::vector<double> values) { std::sort(values.begin(),values.end()); return values[values.size()/2]; }
static json summarize(const std::vector<Measurement>& values) {
    std::vector<double> c, n; for (auto v : values) { c.push_back(v.cycles); n.push_back(v.ns); }
    return {{"cyclesPerCall",c},{"nsPerCall",n},{"medianCyclesPerCall",median(c)},{"medianNsPerCall",median(n)}};
}
static json classifierBench(const std::vector<std::string>& events, bool failureFlag) {
    constexpr unsigned iterations = 262144, rounds = 9; std::array<std::vector<Measurement>,3> measurements;
    const std::array<Classify,3> classifiers = {baselineClassify,candidateClassify,currentClassify};
    for (unsigned round = 0; round < rounds; ++round) for (unsigned offset = 0; offset < 3; ++offset) {
        unsigned variant = (round + offset) % 3; const auto fn = classifiers[variant];
        measurements[variant].push_back(measure(iterations,[&] { unsigned long long total = 0;
            for (unsigned i = 0; i < iterations; ++i) total += fn(events[i % events.size()],failureFlag); resultSink = total; }));
    }
    return {{"iterations",iterations},{"rounds",rounds},{"corpusSize",events.size()},{"baseline",summarize(measurements[0])},{"candidate",summarize(measurements[1])},{"current",summarize(measurements[2])}};
}
static json writerBench(const fspath::path& root) {
    constexpr unsigned iterations = 24576, rounds = 7; std::array<std::vector<Measurement>,3> measurements;
    const std::array<Append,3> writers = {bound_baseline::appendDomainLog,bound_candidate::appendDomainLog,bound_current::appendDomainLog};
    const std::array<std::wstring*,3> roots = {&bound_baseline::dataRoot,&bound_candidate::dataRoot,&bound_current::dataRoot};
    bound_baseline::g_domainLogEnabled[1]=true; bound_candidate::g_domainLogEnabled[1]=true; bound_current::g_domainLogEnabled[1]=true;
    std::vector<std::string> events; for (const auto& event : directEvents) if (!baselineClassify(event,false)) events.push_back(event);
    const json payload = {{"ok",true},{"i",123},{"pid",77}};
    bool fileBudgets = true;
    for (unsigned round = 0; round < rounds; ++round) for (unsigned offset = 0; offset < 3; ++offset) {
        const unsigned variant = (round + offset) % 3; const auto fn = writers[variant];
        const auto dir = root / ("writer-"+std::to_string(round)+"-"+std::to_string(variant)); fspath::create_directories(dir); *roots[variant]=dir.wstring(); auditTick += 10000;
        for (unsigned i=0;i<17;++i) fn("virtual",events[i%events.size()].c_str(),payload);
        measurements[variant].push_back(measure(iterations,[&]{for(unsigned i=0;i<iterations;++i){++auditTick;fn("virtual",events[i%events.size()].c_str(),payload);}}));
        const auto file = dir / "virtual-gamepad.log"; const auto size=fspath::file_size(file);
        fileBudgets = fileBudgets && size < kLogMaxBytes && !fspath::exists(file.wstring()+L".1");
    }
    bound_baseline::flushDomainLogs(true); bound_candidate::flushDomainLogs(true); bound_current::flushDomainLogs(true);
    check("benchmark/no-rotation-confounder",fileBudgets);
    return {{"iterations",iterations},{"rounds",rounds},{"baseline",summarize(measurements[0])},{"candidate",summarize(measurements[1])},{"current",summarize(measurements[2])}};
}
int main(int argc, char** argv) {
    try {
        if(argc != 2) throw std::runtime_error("fixture output directory required"); const fspath::path root=argv[1]; fspath::create_directories(root);
        policySuite();
        bufferSuite<baseline::logging::BufferedDomainLog>(root,"buffer-baseline"); bufferSuite<candidate::logging::BufferedDomainLog>(root,"buffer-candidate"); bufferSuite<current::logging::BufferedDomainLog>(root,"buffer-current");
        domainSuite(root/"domain-baseline","domain-baseline",bound_baseline::dataRoot,bound_baseline::g_domainLogEnabled,bound_baseline::g_domainLogStreams,bound_baseline::pathCalls,bound_baseline::timestampCalls,bound_baseline::appendDomainLog);
        domainSuite(root/"domain-candidate","domain-candidate",bound_candidate::dataRoot,bound_candidate::g_domainLogEnabled,bound_candidate::g_domainLogStreams,bound_candidate::pathCalls,bound_candidate::timestampCalls,bound_candidate::appendDomainLog);
        domainSuite(root/"domain-current","domain-current",bound_current::dataRoot,bound_current::g_domainLogEnabled,bound_current::g_domainLogStreams,bound_current::pathCalls,bound_current::timestampCalls,bound_current::appendDomainLog);
        std::vector<std::string> successEvents, errorEvents;
        for(const auto& event:directEvents)(baselineClassify(event,false)?errorEvents:successEvents).push_back(event);
        if(errorEvents.empty()) errorEvents={"fault","fail","error","reject","timeout"};
        json benchmarks={{"successClassifier",classifierBench(successEvents,false)},{"namedErrorClassifier",classifierBench(errorEvents,false)},{"explicitFailureClassifier",classifierBench(successEvents,true)},{"extractedSuccessWriter",writerBench(root)}};
        std::cout << json({{"failures",failures},{"checks",checks},{"benchmarks",benchmarks}}).dump(2) << '\n'; return failures?3:0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
