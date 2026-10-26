#pragma once
// Export-only observation of raw historical System/Kernel-PnP 225 records.
// No current CM_Disable_* causality, current-process attribution, device-health,
// control or recovery authority follows from these records (including an empty query).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winevt.h>
#include <json.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace ymcc::controller::pnpVeto {
using Json = nlohmann::json;
inline constexpr std::size_t maxEvents = 16;
inline constexpr std::size_t maxReadAttempts = 64;
inline constexpr std::size_t maxXmlBytes = 32 * 1024;
inline constexpr std::size_t maxTotalXmlBytes = 64 * 1024;
inline constexpr std::uint64_t checkpointBudgetUs = 250000;
inline constexpr wchar_t query[] =
    L"*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP'] and (EventID=225) "
    L"and TimeCreated[timediff(@SystemTime) >= 0 and timediff(@SystemTime) <= 3600000]]]";
inline constexpr char scope[] =
    "Raw historical System/Microsoft-Windows-Kernel-PnP EventID 225 removal-veto records only; "
    "event-reported process/device text is not current-process identity or current CM_Disable_* "
    "causality, does not prove current visibility/health, and grants no control/recovery authority. "
    "Empty results do not prove absence of veto; log retention, access and delivery are not verified.";

namespace detail {
using Clock = std::chrono::steady_clock;
inline std::uint64_t elapsedUs(Clock::time_point begin) noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}
struct Counters {
    std::size_t nextCalls = 0, readAttempts = 0, kept = 0;
    std::size_t queryFailures = 0, nextFailures = 0, nextTimeouts = 0;
    std::size_t renderProbeCalls = 0, renderCalls = 0, renderFailures = 0;
    std::size_t conversionFailures = 0, malformedXml = 0, emptyXml = 0;
    std::size_t oversizedXml = 0, totalBudgetSkipped = 0;
    std::size_t renderBytesAttempted = 0, renderedUtf16Bytes = 0, retainedUtf8Bytes = 0;
    std::size_t closeAttempts = 0, closeFailures = 0;
    DWORD lastCloseError = ERROR_SUCCESS;
    std::uint64_t queryUs = 0, nextUs = 0, renderUs = 0, conversionUs = 0;
};
// Construct the owner before a Windows call writes a handle. Even failed/odd
// EvtNext results cannot strand a returned handle during exception unwinding.
struct Handle {
    EVT_HANDLE value = nullptr;
    Counters* counters;
    explicit Handle(Counters& c) noexcept : counters(&c) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() noexcept {
        if (!value) return;
        ++counters->closeAttempts;
        if (!EvtClose(value)) {
            const DWORD error = GetLastError();
            ++counters->closeFailures;
            counters->lastCloseError = error;
        }
    }
};

inline Json captureImpl(Clock::time_point started) {
    Counters c;
    Json result = {
        {"schemaVersion", 1}, {"readOnly", true}, {"status", "unknown"}, {"scope", scope},
        {"channel", "System"}, {"provider", "Microsoft-Windows-Kernel-PnP"}, {"eventId", 225},
        {"lookbackMs", 3600000}, {"order", "reverse"}, {"nextTimeoutMs", 0},
        {"query", "*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP'] and (EventID=225) and TimeCreated[timediff(@SystemTime) >= 0 and timediff(@SystemTime) <= 3600000]]]"},
        {"currentDisableCausalityProven", false}, {"currentProcessIdentityProven", false},
        {"controlOrRecoveryAuthority", false}, {"noVetoProven", false},
        {"queryError", nullptr}, {"nextError", nullptr}, {"lastRenderError", nullptr},
        {"lastConversionError", nullptr}, {"lastCloseError", nullptr},
        {"queryExhausted", false}, {"truncated", false}, {"checkpointBudgetReached", false},
        {"events", Json::array()}, {"omitted", Json::array()},
        {"unknown", Json::array({"current-disable-causality", "current-process-attribution",
            "current-device-state", "system-log-retention-access-and-delivery"})},
        {"limits", {{"events", maxEvents}, {"readAttempts", maxReadAttempts},
            {"xmlBytesPerEvent", maxXmlBytes}, {"totalXmlBytes", maxTotalXmlBytes},
            {"checkpointBudgetUs", checkpointBudgetUs},
            {"byteScope", "Raw UTF-8 XML excluding JSON escaping/metadata; UTF-16 render attempts are independently capped at the same per-event/total bytes, including terminators"},
            {"timeScope", "Checkpoints only; cannot preempt synchronous EvtQuery/EvtNext/EvtRender calls"}}}
    };
    bool exhausted = false, truncated = false, budgetReached = false;
    const char* stopReason = "unknown";
    const auto budgetExpired = [&]() noexcept { return elapsedUs(started) >= checkpointBudgetUs; };
    const auto omit = [&](const char* reason, DWORD error, DWORD requiredBytes) {
        result["omitted"].push_back({{"readAttempt", c.readAttempts}, {"reason", reason},
            {"error", error}, {"requiredBytes", requiredBytes}});
    };
    {
        Handle queryHandle(c);
        const auto queryStart = Clock::now();
        queryHandle.value = EvtQuery(nullptr, L"System", query,
            EvtQueryChannelPath | EvtQueryReverseDirection);
        const DWORD queryError = queryHandle.value ? ERROR_SUCCESS : GetLastError();
        c.queryUs = elapsedUs(queryStart);
        result["queryError"] = queryError;
        if (!queryHandle.value) {
            ++c.queryFailures;
            stopReason = "query-failed";
        } else {
            std::array<wchar_t, maxXmlBytes / sizeof(wchar_t)> buffer{};
            while (c.nextCalls < maxReadAttempts && c.kept < maxEvents) {
                if (budgetExpired()) { budgetReached = true; stopReason = "checkpoint-budget"; break; }
                if (c.renderBytesAttempted == maxTotalXmlBytes || c.retainedUtf8Bytes == maxTotalXmlBytes) {
                    truncated = true; stopReason = "total-xml-budget"; break;
                }
                Handle event(c);
                DWORD returned = 0;
                const auto nextStart = Clock::now();
                ++c.nextCalls;
                const BOOL nextOk = EvtNext(queryHandle.value, 1, &event.value, 0, 0, &returned);
                const DWORD nextError = nextOk ? ERROR_SUCCESS : GetLastError();
                c.nextUs += elapsedUs(nextStart);
                result["nextError"] = nextError;
                if (!nextOk) {
                    if (nextError == ERROR_NO_MORE_ITEMS) { exhausted = true; stopReason = "no-more-items"; }
                    else {
                        ++c.nextFailures;
                        if (nextError == ERROR_TIMEOUT) ++c.nextTimeouts;
                        stopReason = "next-failed-or-timed-out";
                    }
                    break; // No timeout retry, subscription, wait or later polling.
                }
                ++c.readAttempts;
                if (returned != 1 || !event.value) {
                    ++c.nextFailures;
                    stopReason = "next-result-contract-unknown";
                    omit(stopReason, ERROR_INVALID_DATA, 0);
                    break;
                }
                if (budgetExpired()) { budgetReached = true; stopReason = "checkpoint-budget"; break; }

                DWORD needed = 0, properties = 0;
                auto renderStart = Clock::now();
                ++c.renderProbeCalls;
                const BOOL probeOk = EvtRender(nullptr, event.value, EvtRenderEventXml,
                    0, nullptr, &needed, &properties);
                const DWORD probeError = probeOk ? ERROR_SUCCESS : GetLastError();
                c.renderUs += elapsedUs(renderStart);
                if (probeOk || probeError != ERROR_INSUFFICIENT_BUFFER) {
                    ++c.renderFailures;
                    result["lastRenderError"] = probeError;
                    omit("render-size-probe-failed-or-unexpected", probeError, needed);
                    continue;
                }
                if (needed < sizeof(wchar_t) || needed % sizeof(wchar_t)) {
                    ++c.malformedXml;
                    omit("invalid-utf16-render-size", ERROR_INVALID_DATA, needed);
                    continue;
                }
                if (needed > maxXmlBytes) {
                    ++c.oversizedXml; truncated = true;
                    omit("xml-render-byte-limit", ERROR_INSUFFICIENT_BUFFER, needed);
                    continue;
                }
                if (needed > maxTotalXmlBytes - c.renderBytesAttempted) {
                    ++c.totalBudgetSkipped; truncated = true;
                    omit("total-render-byte-limit", ERROR_INSUFFICIENT_BUFFER, needed);
                    continue; // A smaller older record may fit; still at most 64 reads.
                }
                if (budgetExpired()) { budgetReached = true; stopReason = "checkpoint-budget"; break; }
                DWORD used = 0;
                renderStart = Clock::now();
                ++c.renderCalls;
                c.renderBytesAttempted += needed; // Failed calls consume this finite budget too.
                const BOOL renderOk = EvtRender(nullptr, event.value, EvtRenderEventXml,
                    needed, buffer.data(), &used, &properties);
                const DWORD renderError = renderOk ? ERROR_SUCCESS : GetLastError();
                c.renderUs += elapsedUs(renderStart);
                result["lastRenderError"] = renderError;
                if (!renderOk) {
                    ++c.renderFailures;
                    if (renderError == ERROR_INSUFFICIENT_BUFFER) { ++c.oversizedXml; truncated = true; }
                    omit("render-failed-no-retry", renderError, used);
                    continue;
                }
                if (used < sizeof(wchar_t) || used > needed || used % sizeof(wchar_t)) {
                    ++c.malformedXml; omit("invalid-rendered-byte-count", ERROR_INVALID_DATA, used); continue;
                }
                c.renderedUtf16Bytes += used;
                const auto units = static_cast<std::size_t>(used / sizeof(wchar_t));
                bool validTerminator = buffer[units - 1] == L'\0';
                for (std::size_t i = 0; i + 1 < units; ++i)
                    if (buffer[i] == L'\0') { validTerminator = false; break; }
                if (!validTerminator) {
                    ++c.malformedXml; omit("missing-terminator-or-interior-nul", ERROR_INVALID_DATA, used); continue;
                }
                if (units == 1) { ++c.emptyXml; omit("empty-rendered-xml", ERROR_INVALID_DATA, used); continue; }
                if (budgetExpired()) { budgetReached = true; stopReason = "checkpoint-budget"; break; }
                const auto conversionStart = Clock::now();
                const int utf8Bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
                    static_cast<int>(units - 1), nullptr, 0, nullptr, nullptr);
                const DWORD sizeError = utf8Bytes > 0 ? ERROR_SUCCESS : GetLastError();
                c.conversionUs += elapsedUs(conversionStart);
                result["lastConversionError"] = sizeError;
                if (utf8Bytes <= 0) {
                    ++c.conversionFailures; omit("utf8-size-failed", sizeError, used); continue;
                }
                if (static_cast<std::size_t>(utf8Bytes) > maxXmlBytes) {
                    ++c.oversizedXml; truncated = true;
                    omit("xml-utf8-byte-limit", ERROR_INSUFFICIENT_BUFFER, static_cast<DWORD>(utf8Bytes)); continue;
                }
                if (static_cast<std::size_t>(utf8Bytes) > maxTotalXmlBytes - c.retainedUtf8Bytes) {
                    ++c.totalBudgetSkipped; truncated = true;
                    omit("total-utf8-byte-limit", ERROR_INSUFFICIENT_BUFFER, static_cast<DWORD>(utf8Bytes)); continue;
                }
                std::string xml(static_cast<std::size_t>(utf8Bytes), '\0');
                const auto convertStart = Clock::now();
                const int converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
                    static_cast<int>(units - 1), xml.data(), utf8Bytes, nullptr, nullptr);
                const DWORD convertError = converted == utf8Bytes ? ERROR_SUCCESS : GetLastError();
                c.conversionUs += elapsedUs(convertStart);
                result["lastConversionError"] = convertError;
                if (converted != utf8Bytes) {
                    ++c.conversionFailures; omit("utf8-conversion-failed", convertError, used); continue;
                }
                result["events"].push_back({{"readAttempt", c.readAttempts}, {"status", "raw-system-record"},
                    {"renderedUtf16Bytes", used}, {"xmlUtf8Bytes", utf8Bytes}, {"xml", std::move(xml)}});
                c.retainedUtf8Bytes += static_cast<std::size_t>(utf8Bytes);
                ++c.kept;
            }
            if (!exhausted && c.kept == maxEvents) { truncated = true; stopReason = "event-limit"; }
            else if (!exhausted && !budgetReached && !c.nextFailures && c.nextCalls == maxReadAttempts) {
                truncated = true; stopReason = "read-attempt-limit";
            }
        }
    } // Event and query handles have all been closed before projecting close counters.
    const auto failures = c.queryFailures + c.nextFailures + c.renderFailures + c.conversionFailures +
        c.malformedXml + c.emptyXml + c.closeFailures;
    result["status"] = c.kept ? ((failures || truncated || budgetReached) ? "partial" : "captured")
        : ((exhausted && !failures && !truncated && !budgetReached) ? "empty-observation" : "unknown");
    result["stopReason"] = stopReason;
    result["queryExhausted"] = exhausted;
    result["truncated"] = truncated || budgetReached;
    result["checkpointBudgetReached"] = budgetReached;
    result["truncationScope"] = "Bounded capture/omission, not proof that additional records exist";
    if (!c.kept) result["unknown"].push_back("empty-output-does-not-prove-no-veto");
    if (!exhausted) result["unknown"].push_back("query-not-exhausted");
    if (failures) result["unknown"].push_back("capture-or-close-failures");
    if (truncated || budgetReached) result["unknown"].push_back("bounded-or-omitted-records");
    result["lastCloseError"] = c.closeAttempts ? Json(c.lastCloseError) : Json(nullptr);
    result["counts"] = {{"nextCalls", c.nextCalls}, {"readAttempts", c.readAttempts}, {"kept", c.kept},
        {"queryFailures", c.queryFailures}, {"nextFailures", c.nextFailures}, {"nextTimeouts", c.nextTimeouts},
        {"renderProbeCalls", c.renderProbeCalls}, {"renderCalls", c.renderCalls}, {"renderFailures", c.renderFailures},
        {"conversionFailures", c.conversionFailures}, {"malformedXml", c.malformedXml}, {"emptyXml", c.emptyXml},
        {"oversizedXml", c.oversizedXml}, {"totalBudgetSkipped", c.totalBudgetSkipped},
        {"renderBytesAttempted", c.renderBytesAttempted}, {"renderedUtf16Bytes", c.renderedUtf16Bytes},
        {"retainedXmlUtf8Bytes", c.retainedUtf8Bytes}, {"closeAttempts", c.closeAttempts},
        {"closeFailures", c.closeFailures}, {"failures", failures},
        {"unretainedReads", c.readAttempts - c.kept}, {"omissionDetails", result["omitted"].size()},
        {"boundedStopCount", (truncated || budgetReached) ? 1u : 0u}};
    result["timingUs"] = {{"query", c.queryUs}, {"next", c.nextUs}, {"render", c.renderUs},
        {"conversion", c.conversionUs}, {"total", elapsedUs(started)}};
    return result;
}
} // namespace detail

inline nlohmann::json capture() noexcept {
    try {
        return detail::captureImpl(detail::Clock::now());
    } catch (...) {
        try {
            return {{"schemaVersion", 1}, {"readOnly", true}, {"status", "capture-exception"},
                {"scope", scope}, {"queryError", nullptr}, {"nextError", nullptr},
                {"events", Json::array()}, {"counts", nullptr}, {"timingUs", nullptr},
                {"queryExhausted", false}, {"currentDisableCausalityProven", false}, {"currentProcessIdentityProven", false},
                {"controlOrRecoveryAuthority", false}, {"noVetoProven", false},
                {"unknown", Json::array({"capture-exception", "partial-observations-unavailable"})}};
        } catch (...) {
            // If even failure evidence cannot allocate, JSON null is the allocation-free
            // UNKNOWN fallback. No exception escapes the ordinary export boundary.
            return Json();
        }
    }
}
} // namespace ymcc::controller::pnpVeto
