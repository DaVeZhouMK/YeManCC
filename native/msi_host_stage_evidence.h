#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include "json.hpp"

// MSI engineering evidence only. This channel never carries controller input
// or changes admission, retries, timeouts, report submission, or mode policy.
namespace ymcc::msi::hostStageEvidence {
using Json = nlohmann::json;
inline constexpr std::size_t capacity = 1024;
inline constexpr std::uint64_t magic = 0x315647455453484DULL;
using Bytes = std::array<unsigned char, capacity>;
inline std::uint64_t u64(const Bytes& b, std::size_t at) noexcept {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= std::uint64_t(b[at + i]) << (8 * i);
    return result;
}
inline bool guidN(const std::string& text) noexcept {
    if (text.size() != 32) return false;
    for (const char c : text) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool text(const Bytes& b, std::size_t lenAt, std::size_t dataAt, std::size_t limit, std::string& out) {
    const auto length = u64(b, lenAt);
    if (length > limit) return false; // -1 invalid marker also exceeds the bound.
    out.clear(); out.reserve(static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = b[dataAt + i];
        if (c < 32 || c > 126) return false;
        out.push_back(static_cast<char>(c));
    }
    return true;
}
struct Expected {
    std::uint64_t parentPid = 0, hostPid = 0, sequence = 0, epoch = 0;
    std::string hostInstance, requestId, runId, command;
};
inline const char* phaseName(std::uint64_t phase) noexcept {
    switch (phase) {
    case 1: return "created"; case 2: return "read-await-or-continuation";
    case 3: return "read-returned"; case 4: return "handle-pending";
    case 5: return "parsed-envelope"; case 6: return "submit-frame-handler";
    case 7: return "submit-frame-handler-returned"; case 8: return "handle-returned";
    case 9: return "write-await-or-continuation"; case 10: return "write-returned";
    case 11: return "closed";
    case 12: return "backend-switch"; case 13: return "backend-switch-returned";
    case 14: return "cost-window"; case 15: return "cost-window-returned";
    default: return "unknown";
    }
}
// Only the explicit reserved-tail marker authorizes extension decoding. Zero means old v1.
// A return stamp also covers exceptional unwind; it never proves a successful SDK submit or ACK.
inline bool extensionValid(const Bytes& b) noexcept {
    if (u64(b, 448) != 1) return false;
    const auto begin = u64(b, 456), end = u64(b, 464), cost = u64(b, 472), returned = u64(b, 480);
    const auto submit = u64(b, 104), since = u64(b, 64), phase = u64(b, 56);
    for (const auto stamp : {begin, end, cost, returned})
        if (stamp && (!submit || stamp < submit || stamp > since)) return false;
    if ((end && (!begin || end < begin)) || (returned && (!cost || returned < cost))) return false;
    if (cost && begin && (!end || cost < end)) return false;
    if (phase == 12 && (!begin || end || cost || returned)) return false;
    if (phase == 13 && (!end || cost || returned)) return false;
    if (phase == 14 && (!cost || returned)) return false;
    if (phase == 15 && !returned) return false;
    return true;
}
inline Json unknown(const char* reason) {
    return { {"version", 1}, {"observed", false}, {"reason", reason},
        {"controlAuthority", false}, {"steamConsumption", "unobserved"} };
}
// Reader supplies sequence(), barrier(), copy(Bytes&). The native adapter only
// reads a FILE_MAP_READ view. Three immediate attempts; no sleeps or worker.
template<class Reader> bool stableCopy(Reader& reader, Bytes& out, unsigned& attempts) {
    attempts = 0;
    while (attempts < 3) {
        ++attempts;
        const auto first = reader.sequence();
        if (!first || (first & 1)) continue;
        reader.barrier(); reader.copy(out); reader.barrier();
        const auto second = reader.sequence();
        if (first == second && !(second & 1) && u64(out, 24) == first) return true;
    }
    return false;
}
inline Json decode(const Bytes& b, const Expected& e, std::uint64_t nativeQpc, std::uint64_t nativeFrequency) {
    if (u64(b, 0) != magic || u64(b, 8) != 1 || u64(b, 16) != capacity) return unknown("format-mismatch");
    if (!u64(b, 24) || (u64(b, 24) & 1)) return unknown("publication-not-complete");
    if (!e.parentPid || !e.hostPid || !guidN(e.hostInstance) ||
        u64(b, 32) != e.hostPid || u64(b, 40) != e.parentPid) return unknown("process-identity-mismatch");
    std::string instance, request, run, command;
    const auto bits = u64(b, 176);
    if (!(bits & 1) || !text(b, 192, 200, 64, instance) || instance != e.hostInstance)
        return unknown("host-instance-mismatch");
    const auto phase = u64(b, 56);
    if (phase < 1 || phase > 15) return unknown("phase-invalid");
    const auto extensionVersion = u64(b, 448);
    const bool extended = extensionValid(b);
    if (phase >= 12 && !extended) return unknown("extension-invalid");
    const bool requestKnown = (bits & 2) && text(b, 264, 272, 64, request) && !request.empty();
    const bool runKnown = (bits & 4) && text(b, 336, 344, 64, run) && !run.empty();
    const bool commandKnown = (bits & 8) && text(b, 408, 416, 32, command) && !command.empty();
    const bool match = requestKnown && runKnown && commandKnown && !e.requestId.empty() &&
        request == e.requestId && run == e.runId && command == e.command &&
        u64(b, 144) == e.sequence && u64(b, 152) == e.epoch;
    const auto freq = u64(b, 48), since = u64(b, 64);
    const bool clockComparable = freq && freq == nativeFrequency && nativeQpc >= since;
    Json row = { {"version", 1}, {"observed", true}, {"snapshotScope", "latest-coherent-host-checkpoint"},
        {"publicationSequence", u64(b, 24)}, {"parentPid", e.parentPid}, {"hostPid", e.hostPid},
        {"hostInstanceId", instance}, {"phase", phaseName(phase)}, {"phaseCode", phase},
        {"phaseSinceQpc", since}, {"hostQpcFrequency", freq}, {"nativeSnapshotQpc", nativeQpc},
        {"clockComparable", clockComparable}, {"currentRequestMatchesNativeRequest", match},
        {"requestIdentityKnown", requestKnown && runKnown && commandKnown},
        {"requestId", requestKnown ? request : ""}, {"runId", runKnown ? run : ""},
        {"command", commandKnown ? command : ""}, {"sequence", u64(b, 144)},
        {"epoch", u64(b, 152)}, {"powerGeneration", u64(b, 160)},
        {"incomingChars", u64(b, 168)}, {"lastResponseWriteReturnedSequence", u64(b, 184)},
        {"stagesQpc", {{"readBegin", u64(b, 72)}, {"readEnd", u64(b, 80)},
            {"handleBegin", u64(b, 88)}, {"decoded", u64(b, 96)}, {"submitBegin", u64(b, 104)},
            {"submitEnd", u64(b, 112)}, {"handleEnd", u64(b, 120)},
            {"writeBegin", u64(b, 128)}, {"writeEnd", u64(b, 136)}}},
        {"parsedMetadataIsAdmissionProof", false}, {"submitScope", "TrySubmitFrame-including-encoding-backend-and-host-cost-window-path"},
        {"writeReturnIsNativeReceiptProof", false}, {"steamConsumption", "unobserved"}, {"controlAuthority", false} };
    row["extensionVersion"] = extensionVersion;
    row["extensionObserved"] = extended;
    row["extensionReason"] = extended ? "supported" : extensionVersion == 0 ? "absent-old-v1" :
        extensionVersion == 1 ? "invalid-stamps" : "unsupported-version";
    const auto stamp = [&](std::size_t at) -> Json { return extended && u64(b, at) ? Json(u64(b, at)) : Json(nullptr); };
    row["stagesQpc"]["backendBegin"] = stamp(456); row["stagesQpc"]["backendEnd"] = stamp(464);
    row["stagesQpc"]["costBegin"] = stamp(472); row["stagesQpc"]["costEnd"] = stamp(480);
    row["backendScope"] = "persona-switch-including-report-encoding-and-exceptional-unwind";
    row["costScope"] = "Host-due-window-path-legacy-synchronous-or-MSI-bounded-handoff-background-worker-excluded";
    row["backendEndIsConsumerProof"] = false; row["costReturnIsAckProof"] = false;
    if (clockComparable) row["phaseAgeMsAtSnapshot"] = double(nativeQpc - since) * 1000.0 / double(freq);
    row["nativeExpected"] = {{"requestId", e.requestId}, {"sequence", e.sequence},
        {"runId", e.runId}, {"epoch", e.epoch}, {"command", e.command}};
    return row;
}
#if defined(_WIN32) && !defined(YMCC_MSI_STAGE_CODEC_ONLY)
inline Json read(const Expected& e) {
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
    return unknown("fault-build-no-live-mapping");
#else
    struct LastErrorScope { DWORD saved = GetLastError(); ~LastErrorScope() { SetLastError(saved); } } lastErrorScope;
    if (!e.parentPid || !e.hostPid || !guidN(e.hostInstance)) return unknown("identity-unavailable");
    const auto name = std::wstring(L"Local\\YeManCC-MSI-HostStage-v1-") + std::to_wstring(e.parentPid) + L"-" +
        std::to_wstring(e.hostPid) + L"-" + std::wstring(e.hostInstance.begin(), e.hostInstance.end());
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!mapping) { auto row = unknown("mapping-unavailable"); row["win32Error"] = GetLastError(); return row; }
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, capacity);
    if (!view) {
        const DWORD error = GetLastError(); CloseHandle(mapping);
        auto row = unknown("mapping-view-unavailable"); row["win32Error"] = error; return row;
    }
    struct Reader {
        const void* view;
        std::uint64_t sequence() const noexcept {
            return static_cast<std::uint64_t>(*reinterpret_cast<const volatile LONG64*>(
                static_cast<const unsigned char*>(view) + 24));
        }
        void barrier() const noexcept { MemoryBarrier(); }
        void copy(Bytes& out) const noexcept { std::memcpy(out.data(), view, capacity); }
    } reader{view};
    Bytes bytes{}; unsigned attempts = 0;
    const bool stable = stableCopy(reader, bytes, attempts);
    LARGE_INTEGER now{}, frequency{};
    const bool clockRead = QueryPerformanceCounter(&now) && QueryPerformanceFrequency(&frequency);
    UnmapViewOfFile(view); CloseHandle(mapping);
    Json row = stable ? decode(bytes, e, clockRead ? static_cast<std::uint64_t>(now.QuadPart) : 0,
        clockRead ? static_cast<std::uint64_t>(frequency.QuadPart) : 0) : unknown("publication-unstable");
    row["copyAttempts"] = attempts; row["readOnlyMapping"] = true; return row;
#endif
}
#endif
} // namespace ymcc::msi::hostStageEvidence
