// Real production file consumers. All writes are confined to test fixtures.
#define wmain steamCompatibilityFileRegressionEntry
#include "steam_compatibility_file_selftest.cpp"
#undef wmain
#include "../native/steam_original_recovery_file_hash.h"
#include <array>
#include <cmath>

namespace {
struct Input {
    const char* name; fs::path file; compat::ModuleKind kind; compat::Layout layout; const char* expectedSha;
};
struct Clock { LARGE_INTEGER wall{}; ULONG64 cycles = 0; };
LARGE_INTEGER frequency{};
Clock now() {
    Clock value;
    require(QueryPerformanceCounter(&value.wall) != FALSE, "QPC");
    require(QueryThreadCycleTime(GetCurrentThread(), &value.cycles) != FALSE, "own-thread-cycle-query");
    return value;
}
double elapsedMs(const Clock& start, const Clock& end) {
    return double(end.wall.QuadPart - start.wall.QuadPart) * 1000.0 / double(frequency.QuadPart);
}
struct ColdProof {
    std::string sample, sha; std::uint64_t fileSize, readBytes, hashBytes, cycles; double ms;
};
struct Measurement {
    unsigned repetition = 0; bool baseline = false; double ms = 0; std::uint64_t cycles = 0, readBytes = 0, hashBytes = 0;
    bool clientHit = false, uiHit = false;
};
struct Bench {
    std::string profile; unsigned repetitions = 0; std::uint64_t payload = 0;
    std::vector<Measurement> values;
};
std::vector<ColdProof> coldProofs;
std::vector<Bench> benches;
void replaceFixture(const fs::path& file, const Bytes& bytes) {
    const auto next = fs::path(file.wstring() + L".next");
    writeFile(next, bytes);
    require(MoveFileExW(next.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE, "owned fixture replace");
}
compat::Qualification checkedFile(const Input& input, const fs::path& file, const Bytes& bytes,
    const std::string& label, bool hit) {
    const auto q = compat::qualifyFile(file.wstring(), input.kind);
    const auto reference = ymcc::steamOriginalHash::hashFileFully(file.wstring());
    const auto expected = digest(bytes);
    check(label + "/layout-qualified", q.layout == input.layout, q.reason);
    check(label + "/stream-and-memory-reference-agree", reference.sha256 == expected && reference.bytes == bytes.size());
    checkQualificationDigest(label, q, expected, bytes.size(), hit);
    return q;
}
void digestCases(const Input& input) {
    const auto original = readFile(input.file);
    check(std::string(input.name) + "/actual-original-known-SHA", digest(original) == input.expectedSha, digest(original));
    Header header(original);
    const auto file = output / (std::string(input.name) + "-digest.pe.bin");
    writeFile(file, original);
    const auto begin = now();
    const auto cold = compat::qualifyFile(file.wstring(), input.kind);
    const auto end = now();
    check(std::string(input.name) + "/cold-layout", cold.layout == input.layout && !cold.cacheHit, cold.reason);
    checkQualificationDigest(std::string(input.name) + "/cold-first-miss", cold, input.expectedSha, original.size(), false);
    coldProofs.push_back({input.name, cold.sha256, original.size(), cold.fileBytesRead, cold.digestBytesHashed, end.cycles - begin.cycles, elapsedMs(begin, end)});
    const auto initialReference = ymcc::steamOriginalHash::hashFileFully(file.wstring());
    check(std::string(input.name) + "/cold-independent-real-file-hash", initialReference.sha256 == cold.sha256 && initialReference.bytes == original.size());
    const auto hot = checkedFile(input, file, original, std::string(input.name) + "/hot-cache", true);
    // Replace only timestamp/version metadata, preserving every critical range.
    auto timestamp = original;
    put<DWORD>(timestamp, header.file + offsetof(IMAGE_FILE_HEADER, TimeDateStamp), header.fileHeader.TimeDateStamp ^ 0xa5a5a5a5);
    put<WORD>(timestamp, header.optional + offsetof(IMAGE_OPTIONAL_HEADER64, MajorImageVersion), 0x2323);
    replaceFixture(file, timestamp);
    const auto changed = checkedFile(input, file, timestamp, std::string(input.name) + "/timestamp-replacement", false);
    check(std::string(input.name) + "/same-size-new-identity-new-digest", changed.sha256 != cold.sha256 && !(changed.identity == cold.identity) && timestamp.size() == original.size());
    (void)checkedFile(input, file, timestamp, std::string(input.name) + "/timestamp-hot", true);
    // Same file ID and size, only write stamp/content change: no sleep needed.
    auto inplace = timestamp;
    const auto stampOffset = header.file + offsetof(IMAGE_FILE_HEADER, TimeDateStamp);
    inplace[stampOffset] ^= 0x40;
    {
        compat::detail::Handle handle(CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        require(handle.valid(), "fixture in-place exclusive open");
        LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(stampOffset);
        require(SetFilePointerEx(handle.value, position, nullptr, FILE_BEGIN) != FALSE, "fixture stamp seek");
        DWORD wrote = 0;
        require(WriteFile(handle.value, inplace.data() + stampOffset, 1, &wrote, nullptr) != FALSE && wrote == 1, "fixture stamp write");
        require(FlushFileBuffers(handle.value) != FALSE, "fixture flush");
        ULARGE_INTEGER stamp{}; stamp.HighPart = changed.identity.writeHigh; stamp.LowPart = changed.identity.writeLow; stamp.QuadPart += 100000;
        FILETIME finalStamp{stamp.LowPart, stamp.HighPart};
        require(SetFileTime(handle.value, nullptr, nullptr, &finalStamp) != FALSE, "fixture deterministic changed write stamp");
    }
    const auto inPlaceQ = checkedFile(input, file, inplace, std::string(input.name) + "/same-file-stamp-invalidation", false);
    check(std::string(input.name) + "/same-ID-and-size-stamp-really-changed", inPlaceQ.identity.volume == changed.identity.volume &&
        inPlaceQ.identity.indexHigh == changed.identity.indexHigh && inPlaceQ.identity.indexLow == changed.identity.indexLow &&
        inPlaceQ.identity.sizeLow == changed.identity.sizeLow &&
        (inPlaceQ.identity.writeHigh != changed.identity.writeHigh || inPlaceQ.identity.writeLow != changed.identity.writeLow) && inPlaceQ.sha256 != changed.sha256);
    auto overlay = original;
    constexpr unsigned char extra[]{'d','i','g','e','s','t','-','r','e','u','s','e','-','o','v','e','r','l','a','y'};
    overlay.insert(overlay.end(), std::begin(extra), std::end(extra));
    replaceFixture(file, overlay);
    const auto appended = checkedFile(input, file, overlay, std::string(input.name) + "/overlay-replacement", false);
    check(std::string(input.name) + "/overlay-new-size-and-digest", appended.sha256 != cold.sha256 && appended.sha256 != changed.sha256 && overlay.size() > original.size());
    (void)checkedFile(input, file, overlay, std::string(input.name) + "/overlay-hot", true);
    check(std::string(input.name) + "/earlier-returned-digests-stay-bound-to-their-content", cold.sha256 == input.expectedSha && hot.sha256 == input.expectedSha && changed.sha256 == digest(timestamp) && inPlaceQ.sha256 == digest(inplace));
    require(DeleteFileW(file.c_str()) != FALSE, "delete digest fixture with qualifications still alive");
    const auto deleted = compat::qualifyFile(file.wstring(), input.kind);
    check(std::string(input.name) + "/deleted-file-no-stale-digest", !deleted.layout && deleted.sha256.empty() && !deleted.cacheHit && deleted.fileBytesRead == 0 && deleted.digestBytesHashed == 0);
    // Optional no-digest consumer must not add hash CPU; default true upgrades
    // its same-identity cached metadata by re-reading/checking one whole buffer.
    const auto optionalFile = output / (std::string(input.name) + "-optional-digest.pe.bin");
    writeFile(optionalFile, original);
    const auto noDigest = compat::qualifyFile(optionalFile.wstring(), input.kind, false);
    check(std::string(input.name) + "/optional/false-cold-reads-without-hashing", noDigest.layout == input.layout && !noDigest.cacheHit &&
        noDigest.sha256.empty() && noDigest.fileBytesRead == original.size() && noDigest.digestBytesHashed == 0,
        "read=" + std::to_string(noDigest.fileBytesRead) + "; hashed=" + std::to_string(noDigest.digestBytesHashed));
    const auto falseHit = compat::qualifyFile(optionalFile.wstring(), input.kind, false);
    check(std::string(input.name) + "/optional/false-hit-zero-full-bytes", falseHit.layout == input.layout && falseHit.cacheHit &&
        falseHit.sha256.empty() && !falseHit.fileBytesRead && !falseHit.digestBytesHashed);
    const auto upgraded = compat::qualifyFile(optionalFile.wstring(), input.kind, true);
    checkQualificationDigest(std::string(input.name) + "/optional/false-to-true-upgrade", upgraded, input.expectedSha, original.size(), false);
    check(std::string(input.name) + "/optional/upgrade-same-file-identity", upgraded.layout == input.layout && upgraded.identity == noDigest.identity);
    const auto trueHit = compat::qualifyFile(optionalFile.wstring(), input.kind);
    checkQualificationDigest(std::string(input.name) + "/optional/upgraded-true-hot", trueHit, input.expectedSha, original.size(), true);
    const auto falseAfterTrue = compat::qualifyFile(optionalFile.wstring(), input.kind, false);
    check(std::string(input.name) + "/optional/false-served-by-true-cache-zero-full-bytes", falseAfterTrue.layout == input.layout &&
        falseAfterTrue.cacheHit && !falseAfterTrue.fileBytesRead && !falseAfterTrue.digestBytesHashed &&
        (falseAfterTrue.sha256.empty() || falseAfterTrue.sha256 == input.expectedSha));
    check(std::string(input.name) + "/optional/prior-return-remains-no-digest", noDigest.sha256.empty() && falseHit.sha256.empty());
    require(DeleteFileW(optionalFile.c_str()) != FALSE, "delete optional digest fixture");
    auto semanticBad = original;
    const auto contract = input.kind == compat::ModuleKind::Client ? recordRanges(input.layout) : uiRanges(input.layout);
    semanticBad[header.rawOffset(semanticBad, contract.front().rva, 1)] ^= 1;
    writeFile(optionalFile, semanticBad);
    const auto badFalse = compat::qualifyFile(optionalFile.wstring(), input.kind, false);
    check(std::string(input.name) + "/optional/false-still-refuses-semantic-corruption", !badFalse.layout && !badFalse.cacheHit && badFalse.sha256.empty() &&
        badFalse.fileBytesRead == semanticBad.size() && badFalse.digestBytesHashed == 0, badFalse.reason);
    const auto badFalseHit = compat::qualifyFile(optionalFile.wstring(), input.kind, false);
    check(std::string(input.name) + "/optional/rejected-false-cache-hit-zero-work", !badFalseHit.layout && badFalseHit.cacheHit && badFalseHit.sha256.empty() && !badFalseHit.fileBytesRead && !badFalseHit.digestBytesHashed, badFalseHit.reason);
    const auto badUpgrade = compat::qualifyFile(optionalFile.wstring(), input.kind, true);
    checkQualificationDigest(std::string(input.name) + "/optional/rejected-false-to-true-upgrade", badUpgrade, digest(semanticBad), semanticBad.size(), false);
    check(std::string(input.name) + "/optional/digest-upgrade-never-grants-rejected-layout", !badUpgrade.layout, badUpgrade.reason);
    require(DeleteFileW(optionalFile.c_str()) != FALSE, "delete rejected optional digest fixture");
    std::cerr << "DIGEST_CASES_DONE " << input.name << " checks=" << cases.size() << std::endl;
}
Measurement measurePair(const Input& client, const Input& ui, unsigned repetition, bool baseline) {
    Measurement m; m.repetition = repetition; m.baseline = baseline;
    ymcc::steamOriginalHash::FileDigest cHash, uHash;
    const auto start = now();
    if (baseline) {
        cHash = ymcc::steamOriginalHash::hashFileFully(client.file.wstring());
        uHash = ymcc::steamOriginalHash::hashFileFully(ui.file.wstring());
    }
    const auto c = compat::qualifyFile(client.file.wstring(), client.kind);
    const auto u = compat::qualifyFile(ui.file.wstring(), ui.kind);
    const auto finish = now();
    m.ms = elapsedMs(start, finish); m.cycles = finish.cycles - start.cycles;
    m.readBytes = cHash.bytes + uHash.bytes + c.fileBytesRead + u.fileBytesRead;
    m.hashBytes = cHash.bytes + uHash.bytes + c.digestBytesHashed + u.digestBytesHashed;
    m.clientHit = c.cacheHit; m.uiHit = u.cacheHit;
    require(c.layout == client.layout && u.layout == ui.layout && c.sha256 == client.expectedSha && u.sha256 == ui.expectedSha, "benchmark qualification/digest mismatch");
    require(c.cacheHit && u.cacheHit && !c.fileBytesRead && !u.fileBytesRead && !c.digestBytesHashed && !u.digestBytesHashed, "benchmark not warm or stale this-call counters");
    if (baseline) require(cHash.sha256 == c.sha256 && uHash.sha256 == u.sha256, "benchmark separate hashes disagree");
    return m;
}
void benchmark(const Input& client, const Input& ui, unsigned repetitions) {
    Bench bench; bench.profile = client.layout == compat::Layout::Legacy ? "legacy" : "public";
    bench.repetitions = repetitions;
    const auto clientSize = fs::file_size(client.file), uiSize = fs::file_size(ui.file);
    const auto clientStamp = fs::last_write_time(client.file), uiStamp = fs::last_write_time(ui.file);
    bench.payload = clientSize + uiSize;
    // Warm-up qualification is unmeasured and separate from the cold fixture proof.
    const auto warmC = compat::qualifyFile(client.file.wstring(), client.kind);
    const auto warmU = compat::qualifyFile(ui.file.wstring(), ui.kind);
    require(warmC.layout == client.layout && warmU.layout == ui.layout && warmC.sha256 == client.expectedSha && warmU.sha256 == ui.expectedSha, "warmup actual SHA mismatch");
    const auto proofC = compat::qualifyFile(client.file.wstring(), client.kind);
    const auto proofU = compat::qualifyFile(ui.file.wstring(), ui.kind);
    require(proofC.cacheHit && proofU.cacheHit && !proofC.fileBytesRead && !proofU.fileBytesRead && !proofC.digestBytesHashed && !proofU.digestBytesHashed, "warmup cache proof");
    for (unsigned i = 0; i < repetitions; ++i) {
        const bool oldFirst = (i % 2) == 0;
        auto first = measurePair(client, ui, i + 1, oldFirst);
        auto second = measurePair(client, ui, i + 1, !oldFirst);
        auto& old = oldFirst ? first : second;
        auto& fresh = oldFirst ? second : first;
        check("bench/" + bench.profile + "/pair-" + std::to_string(i + 1), old.readBytes == bench.payload && old.hashBytes == bench.payload && fresh.readBytes == 0 && fresh.hashBytes == 0 && old.clientHit && old.uiHit && fresh.clientHit && fresh.uiHit,
            "baselinePayload=" + std::to_string(old.readBytes) + "; reusedPayload=" + std::to_string(fresh.readBytes));
        bench.values.push_back(first); bench.values.push_back(second);
    }
    check("bench/" + bench.profile + "/read-only-source-size-and-stamp-unchanged", fs::file_size(client.file) == clientSize && fs::file_size(ui.file) == uiSize && fs::last_write_time(client.file) == clientStamp && fs::last_write_time(ui.file) == uiStamp);
    std::cerr << "BENCH_DONE " << bench.profile << " alternatingPairs=" << repetitions << " payloadPerBaseline=" << bench.payload << std::endl;
    benches.push_back(std::move(bench));
}
double percentile(std::vector<double> values, double fraction) {
    require(!values.empty(), "nonempty benchmark samples");
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(fraction * double(values.size()))) - 1;
    return values[(std::min)(index, values.size() - 1)];
}
void printStats(const Bench& bench, bool baseline) {
    std::vector<double> ms, cycles; std::uint64_t read = 0, hashed = 0;
    for (const auto& m : bench.values) if (m.baseline == baseline) { ms.push_back(m.ms); cycles.push_back(static_cast<double>(m.cycles)); read += m.readBytes; hashed += m.hashBytes; }
    std::cout << "{\"samples\":" << ms.size() << ",\"wallMsMedian\":" << percentile(ms, .5) << ",\"wallMsP95\":" << percentile(ms, .95)
        << ",\"threadCyclesMedian\":" << percentile(cycles, .5) << ",\"threadCyclesP95\":" << percentile(cycles, .95)
        << ",\"fullPayloadBytesRead\":" << read << ",\"bytesHashed\":" << hashed << '}';
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc != 6 && argc != 7) { std::cerr << "usage: digest OUT LEGACY_CLIENT PUBLIC_CLIENT LEGACY_UI PUBLIC_UI [10|20]\n"; return 2; }
    try {
        output = fs::absolute(argv[1]); fs::create_directories(output);
        require(QueryPerformanceFrequency(&frequency) != FALSE, "QPC frequency");
        const unsigned repetitions = argc == 7 ? static_cast<unsigned>(std::stoul(argv[6])) : 20;
        require(repetitions == 10 || repetitions == 20, "bounded benchmark repetitions");
        const std::array<Input,4> inputs{{
            {"legacy-client", argv[2], compat::ModuleKind::Client, compat::Layout::Legacy, "e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e"},
            {"public-client", argv[3], compat::ModuleKind::Client, compat::Layout::Public, "253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc"},
            {"legacy-ui", argv[4], compat::ModuleKind::Ui, compat::Layout::Legacy, "1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252"},
            {"public-ui", argv[5], compat::ModuleKind::Ui, compat::Layout::Public, "cd9226db4e6c28942df70c22c0475d91fdd62cba5f100baae2d65af57cabb610"},
        }};
        for (const auto& input : inputs) digestCases(input);
        benchmark(inputs[0], inputs[2], repetitions);
        benchmark(inputs[1], inputs[3], repetitions);
    } catch (const std::exception& e) { check("digest-or-benchmark-fatal", false, e.what()); }
    const auto failed = std::count_if(cases.begin(), cases.end(), [](const auto& c) { return !c.pass; });
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "{\"pass\":" << (failed ? "false" : "true") << ",\"checks\":" << cases.size() << ",\"failed\":" << failed
        << ",\"scope\":\"digest/qualifyFile stage only; OS cache uncontrolled and warmed; metadata identity opens still occur; no loaded qualification/engine/recovery timing\",\"clock\":\"QPC wall and QueryThreadCycleTime of current test thread\",\"coldQualificationMisses\":[";
    bool first = true;
    for (const auto& c : coldProofs) {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"sample\":" << quoted(c.sample) << ",\"sha256\":" << quoted(c.sha) << ",\"fileSize\":" << c.fileSize << ",\"fileBytesRead\":" << c.readBytes
            << ",\"digestBytesHashed\":" << c.hashBytes << ",\"wallMs\":" << c.ms << ",\"threadCycles\":" << c.cycles << '}';
    }
    std::cout << "],\"benchmarks\":["; first = true;
    for (const auto& bench : benches) {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"profile\":" << quoted(bench.profile) << ",\"pairedRepetitions\":" << bench.repetitions << ",\"payloadBytesPerPair\":" << bench.payload << ",\"baseline\":";
        printStats(bench, true); std::cout << ",\"digestReuse\":"; printStats(bench, false); std::cout << ",\"measurements\":[";
        bool firstValue = true;
        for (const auto& m : bench.values) {
            if (!firstValue) std::cout << ','; firstValue = false;
            std::cout << "{\"repetition\":" << m.repetition << ",\"baseline\":" << (m.baseline ? "true" : "false") << ",\"wallMs\":" << m.ms << ",\"threadCycles\":" << m.cycles
                << ",\"fullPayloadBytesRead\":" << m.readBytes << ",\"bytesHashed\":" << m.hashBytes << ",\"clientCacheHit\":" << (m.clientHit ? "true" : "false") << ",\"uiCacheHit\":" << (m.uiHit ? "true" : "false") << '}';
        }
        std::cout << "]}";
    }
    std::cout << "],\"cases\":["; first = true;
    for (const auto& c : cases) {
        if (!first) std::cout << ','; first = false;
        std::cout << "{\"name\":" << quoted(c.name) << ",\"pass\":" << (c.pass ? "true" : "false") << ",\"observed\":" << quoted(c.observed) << '}';
    }
    std::cout << "]}\n";
    return failed ? 1 : 0;
}