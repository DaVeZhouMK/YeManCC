// Standalone fixture: includes only extracted exportRva/read statements, never the engine.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>
#include "json.hpp"
using J = nlohmann::json;
void need(bool value, const char* why) { if (!value) throw std::runtime_error(why); }

struct Plan {
  std::vector<unsigned char> bytes;
  size_t stop = SIZE_MAX;
  bool throws = false;
  bool opened = true;
};
struct Trace {
  unsigned opens = 0, seeks = 0, boundaryHits = 0, bulkCalls = 0;
  size_t position = 0, maxRequest = 0;
  std::wstring path;
  std::ios::openmode mode{};
};
Plan plan;
Trace trace;
class FixtureBuf : public std::streambuf {
  size_t pos = 0;
  bool boundaryPassed = false;
  int_type underflow() override {
    trace.position = pos;
    if (!plan.opened || pos >= plan.bytes.size()) return traits_type::eof();
    if (!boundaryPassed && pos == plan.stop) {
      boundaryPassed = true;
      ++trace.boundaryHits;
      if (plan.throws) throw std::runtime_error("injected-file-read-error");
      return traits_type::eof(); // A retry would expose the remaining bytes.
    }
    return traits_type::to_int_type(static_cast<char>(plan.bytes[pos]));
  }
  int_type uflow() override {
    const auto v = underflow();
    if (!traits_type::eq_int_type(v, traits_type::eof())) ++pos;
    trace.position = pos;
    return v;
  }
  std::streamsize xsgetn(char* p, std::streamsize n) override {
    ++trace.bulkCalls;
    trace.maxRequest = (std::max)(trace.maxRequest, static_cast<size_t>(n));
    return std::streambuf::xsgetn(p, n);
  }
  pos_type seekoff(off_type, std::ios::seekdir, std::ios::openmode) override {
    ++trace.seeks; throw std::runtime_error("fixture-seek-forbidden");
  }
  pos_type seekpos(pos_type, std::ios::openmode) override {
    ++trace.seeks; throw std::runtime_error("fixture-seek-forbidden");
  }
};
class FixtureInput : public std::istream {
  FixtureBuf buffer;
public:
  FixtureInput(const std::filesystem::path& path, std::ios::openmode mode)
      : std::istream(nullptr) {
    rdbuf(&buffer);
    ++trace.opens; trace.path = path.wstring(); trace.mode = mode;
    if (!plan.opened) setstate(std::ios::failbit);
  }
};
// The two native functions below are included byte-for-byte, including their names.
namespace old_native {
#include "old.export.inc"
#include "old.reader.inc"
}
namespace new_native {
#include "new.export.inc"
#include "new.reader.inc"
}
// Only the input type is substituted in these copies; all statements stay intact.
namespace old_fault {
#include "old.fault.export.inc"
#include "old.fault.reader.inc"
}
namespace new_fault {
#include "new.fault.export.inc"
#include "new.fault.reader.inc"
}
namespace retry_mutant {
#include "retry.fault.reader.inc"
}
namespace first_block_mutant {
#include "first.reader.inc"
}

using Export = std::uint64_t (*)(const std::wstring&, const char*);
using Reader = std::vector<unsigned char> (*)(const std::wstring&);
J outcome(Export fn, const std::wstring& path, const char* name) {
  try { return J{{"rva", fn(path, name)}}; }
  catch (const std::exception& e) { return J{{"error", e.what()}}; }
}
J readOutcome(Reader fn, const std::wstring& path) {
  try {
    const auto actual = fn(path);
    const size_t expected = plan.opened ? (std::min)(plan.stop, plan.bytes.size()) : 0;
    const bool same = actual.size() == expected &&
      std::equal(actual.begin(), actual.end(), plan.bytes.begin());
    return J{{"bytes", actual.size()}, {"exactPrefix", same}};
  } catch (const std::exception& e) { return J{{"error", e.what()}}; }
}
void saveBytes(const std::filesystem::path& path, const std::vector<unsigned char>& b) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  need(f.good(), "fixture-write");
}
std::vector<unsigned char> loadBytes(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::filesystem::path utf8Path(const std::string& text) {
  return std::filesystem::path(std::u8string(
    reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
J tests(const std::filesystem::path& manifest, const std::filesystem::path& ownDll) {
  std::ifstream input(manifest);
  const J cases = J::parse(input);
  J results = J::array();
  unsigned failures = 0;
  auto add = [&](const std::string& name, bool ok, const J& detail) {
    results.push_back(J{{"name", name}, {"passed", ok}, {"detail", detail}});
    if (!ok) ++failures;
  };
  for (const auto& c : cases) {
    const auto path = utf8Path(c.at("path").get<std::string>());
    const auto name = c.at("export").get<std::string>();
    const auto a = outcome(old_native::exportRva, path.wstring(), name.c_str());
    const auto b = outcome(new_native::exportRva, path.wstring(), name.c_str());
    const auto oldBytes = old_native::readBytes(path.wstring());
    const auto newBytes = new_native::readBytes(path.wstring());
    add(c.at("name"), a == b && a == c.at("expected") && oldBytes == newBytes,
        J{{"old", a}, {"new", b}, {"expected", c.at("expected")},
          {"sameAllReadBytes", oldBytes == newBytes}, {"readBytes", oldBytes.size()}});
  }
  const auto base = utf8Path(cases.at(0).at("path").get<std::string>());
  const auto seed = loadBytes(base);
  const auto originalOwn = loadBytes(ownDll);
  add("real-own-trial-dll-all-bytes", old_native::readBytes(ownDll.wstring()) == originalOwn &&
      new_native::readBytes(ownDll.wstring()) == originalOwn,
      J{{"bytes", originalOwn.size()}});
  const auto ownA = outcome(old_native::exportRva, ownDll.wstring(), "RunningTrialControl");
  const auto ownB = outcome(new_native::exportRva, ownDll.wstring(), "RunningTrialControl");
  add("real-own-trial-dll-export", ownA == ownB && ownA.contains("rva"),
      J{{"old", ownA}, {"new", ownB}});
  HANDLE locked = CreateFileW(base.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  need(locked != INVALID_HANDLE_VALUE, "fixture-exclusive-lock");
  const auto lockA = outcome(old_native::exportRva, base.wstring(), "RunningTrialControl");
  const auto lockB = outcome(new_native::exportRva, base.wstring(), "RunningTrialControl");
  CloseHandle(locked);
  add("real-sharing-denied-open", lockA == lockB && lockA == J{{"error", "dll-file"}},
      J{{"old", lockA}, {"new", lockB}});
  const auto replacePath = manifest.parent_path() / L"same-path-replaced.dll";
  saveBytes(replacePath, seed);
  const auto beforeA = outcome(old_native::exportRva, replacePath.wstring(), "RunningTrialControl");
  const auto beforeB = outcome(new_native::exportRva, replacePath.wstring(), "RunningTrialControl");
  auto changed = seed;
  const DWORD changedRva = 0x2a00;
  std::memcpy(changed.data() + 0x2100, &changedRva, sizeof(changedRva));
  saveBytes(replacePath, changed);
  const auto afterA = outcome(old_native::exportRva, replacePath.wstring(), "RunningTrialControl");
  const auto afterB = outcome(new_native::exportRva, replacePath.wstring(), "RunningTrialControl");
  add("same-path-fresh-open-no-rva-cache", beforeA == beforeB && afterA == afterB && beforeA != afterA &&
      afterA == J{{"rva", changedRva}}, J{{"before", beforeA}, {"after", afterA}});
  // Restore no source or old evidence: this path belongs to this run's generated fixtures.
  saveBytes(replacePath, {});
  add("same-path-truncated-to-zero", outcome(old_native::exportRva, replacePath.wstring(), "RunningTrialControl") ==
      J{{"error", "dll-file"}} && outcome(new_native::exportRva, replacePath.wstring(), "RunningTrialControl") ==
      J{{"error", "dll-file"}}, J::object());

  plan.bytes = seed;
  plan.bytes.resize(3 * 65536 + 97, 0xa5);
  const std::wstring fakePath = L"fixture-only/\u6587\u4ef6.dll";
  const std::vector<size_t> stops = {0, 1, 4096, 4097, 8192, 12288, 65535, 65536, 65537,
                                    131072, 131073, SIZE_MAX};
  for (const auto stop : stops) {
    for (const bool throwing : {false, true}) {
      plan.stop = stop; plan.throws = throwing; plan.opened = true;
      trace = {}; const auto a = readOutcome(old_fault::readBytes, fakePath); const auto ta = trace;
      trace = {}; const auto b = readOutcome(new_fault::readBytes, fakePath); const auto tb = trace;
      const bool boundary = ta.opens == 1 && tb.opens == 1 && ta.seeks == 0 && tb.seeks == 0 &&
        ta.path == fakePath && tb.path == fakePath && ta.mode == std::ios::binary &&
        tb.mode == std::ios::binary && tb.maxRequest == 65536 && ta.position == tb.position &&
        ta.boundaryHits == tb.boundaryHits;
      const bool prefix = a.contains("error") || a.at("exactPrefix").get<bool>();
      const std::string label = (throwing ? "throw-at-" : "eof-or-read-error-at-") + std::to_string(stop);
      add(label + "-bytes", a == b && prefix && boundary,
          J{{"old", a}, {"new", b}, {"position", tb.position},
            {"newBulkCalls", tb.bulkCalls}, {"boundaryHits", tb.boundaryHits}});
      // Only safe prefixes: the unchanged PE parser is not a malformed-input hardening target.
      if (stop != 1) {
        trace = {}; const auto ea = outcome(old_fault::exportRva, fakePath, "RunningTrialControl");
        trace = {}; const auto eb = outcome(new_fault::exportRva, fakePath, "RunningTrialControl");
        add(label + "-export", ea == eb, J{{"old", ea}, {"new", eb}});
      }
    }
  }
  plan.opened = false; plan.stop = SIZE_MAX; plan.throws = false;
  trace = {}; const auto fa = outcome(old_fault::exportRva, fakePath, "RunningTrialControl");
  trace = {}; const auto fb = outcome(new_fault::exportRva, fakePath, "RunningTrialControl");
  add("injected-open-failure", fa == fb && fa == J{{"error", "dll-file"}}, J{{"old", fa}, {"new", fb}});
  plan.opened = true; plan.stop = 65537;
  trace = {}; const auto badRetry = readOutcome(retry_mutant::readBytes, fakePath);
  add("negative-control-retry-after-short-read-detected", !badRetry.at("exactPrefix").get<bool>(), badRetry);
  const auto longPath = manifest.parent_path() / L"mutation-long.dll";
  saveBytes(longPath, plan.bytes);
  add("negative-control-first-block-only-detected", first_block_mutant::readBytes(longPath.wstring()) != plan.bytes,
      J{{"expectedBytes", plan.bytes.size()}});
  return J{{"success", failures == 0}, {"passed", results.size() - failures},
           {"total", results.size()}, {"cases", results},
           {"steamProcessReads", 0}, {"dllLoads", 0}, {"applicationActions", 0}};
}
std::uint64_t fileTimeValue(FILETIME f) {
  return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}
std::uint64_t threadCpu() {
  FILETIME created{}, exited{}, kernel{}, user{};
  need(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != 0, "thread-cpu-clock");
  return fileTimeValue(kernel) + fileTimeValue(user);
}
J benchmark(const std::filesystem::path& file, unsigned iterations, unsigned rounds) {
  HANDLE pin = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  need(pin != INVALID_HANDLE_VALUE, "benchmark-read-only-pin");
  const auto expected = old_native::exportRva(file.wstring(), "RunningTrialControl");
  need(new_native::exportRva(file.wstring(), "RunningTrialControl") == expected, "benchmark-export-differential");
  for (unsigned i = 0; i < 32; ++i) {
    need(old_native::exportRva(file.wstring(), "RunningTrialControl") == expected, "warm-old-rva");
    need(new_native::exportRva(file.wstring(), "RunningTrialControl") == expected, "warm-new-rva");
  }
  J samples = J::array();
  for (unsigned round = 0; round < rounds; ++round) {
    for (unsigned order = 0; order < 2; ++order) {
      const bool useNew = ((round + order) % 2) != 0;
      const auto fn = useNew ? new_native::exportRva : old_native::exportRva;
      const auto cpu0 = threadCpu();
      const auto wall0 = std::chrono::steady_clock::now();
      std::uint64_t checksum = 0;
      for (unsigned i = 0; i < iterations; ++i) {
        const auto rva = fn(file.wstring(), "RunningTrialControl");
        need(rva == expected, "benchmark-every-output-identical");
        checksum += rva;
      }
      const auto wall1 = std::chrono::steady_clock::now();
      const auto cpu1 = threadCpu();
      const double wallMs = std::chrono::duration<double, std::milli>(wall1 - wall0).count();
      samples.push_back(J{{"round", round}, {"order", order}, {"variant", useNew ? "new" : "old"},
                          {"calls", iterations}, {"wallMs", wallMs}, {"threadCpuMs", (cpu1 - cpu0) / 10000.0},
                          {"rva", expected}, {"checksum", checksum}});
    }
  }
  CloseHandle(pin);
  return J{{"success", true}, {"samples", samples}, {"warmupPerVariant", 32},
           {"scope", "warm-cache standalone exportRva open/read/PE-parse/close only"},
           {"cpuClock", "GetThreadTimes current fixture thread, kernel+user; 100ns units, scheduler granularity"},
           {"inputWriteDeleteDeniedDuringMeasurement", true}, {"dllLoads", 0}, {"steamProcessReads", 0}};
}
int wmain(int argc, wchar_t** argv) {
  try {
    need(argc >= 3, "usage: test manifest own-dll OR bench own-dll iterations rounds");
    J result;
    if (std::wstring(argv[1]) == L"test") {
      need(argc == 4, "test-arguments"); result = tests(argv[2], argv[3]);
    } else {
      need(std::wstring(argv[1]) == L"bench" && argc == 5, "bench-arguments");
      result = benchmark(argv[2], static_cast<unsigned>(std::stoul(argv[3])),
                         static_cast<unsigned>(std::stoul(argv[4])));
    }
    std::cout << result.dump(2) << '\n';
    return result.at("success").get<bool>() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 2;
  }
}
