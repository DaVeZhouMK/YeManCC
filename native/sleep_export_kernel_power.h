#pragma once

#include <windows.h>
#include <winevt.h>
#include "json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace ymcc::sleepexport {

using KernelHistoryJson = nlohmann::json;
inline constexpr unsigned kernelHistoryLookbackMs = 3 * 60 * 60 * 1000;
inline constexpr unsigned kernelHistoryMaxEvents = 64;
inline constexpr unsigned kernelHistoryMaxXmlBytes = 32 * 1024;
inline constexpr unsigned kernelHistoryMaxTotalXmlBytes = 256 * 1024;
inline constexpr unsigned kernelHistoryCheckpointMs = 500;

namespace kernelhistory {
struct EventHandle {
    EVT_HANDLE handle = nullptr;
    EventHandle() = default;
    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
    ~EventHandle() { if (handle) EvtClose(handle); }
};
} // namespace kernelhistory

// Historical XML for explicit export. It supplies no request or recovery authority.
template<class Current>
KernelHistoryJson captureKernelPowerHistory(Current current) noexcept {
    try {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        KernelHistoryJson result = {
            {"schemaVersion", 1}, {"channel", "System"}, {"provider", "Microsoft-Windows-Kernel-Power"},
            {"eventIds", {42, 107, 187, 506, 507}}, {"order", "newest-first"},
            {"lookbackMs", kernelHistoryLookbackMs}, {"maxEvents", kernelHistoryMaxEvents},
            {"maxXmlBytesPerEvent", kernelHistoryMaxXmlBytes}, {"maxTotalXmlBytes", kernelHistoryMaxTotalXmlBytes},
            {"checkpointBudgetMs", kernelHistoryCheckpointMs}, {"queryExhausted", false},
            {"automaticRepairAuthorized", false}, {"completeWindowsCoverageProven", false},
            {"requestAttributionProven", false}, {"events", KernelHistoryJson::array()}
        };
        auto& rows = result.at("events");
        const auto finish = [&](const char* state, DWORD error = ERROR_SUCCESS) {
            result["status"] = state;
            result["win32Error"] = error ? KernelHistoryJson(error) : KernelHistoryJson(nullptr);
            result["elapsedMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
            return std::move(result);
        };
        const auto canceled = [&] {
            rows.clear();
            return finish("export-power-observation-changed", ERROR_CANCELLED);
        };
        const auto expired = [&] { return Clock::now() - started >= std::chrono::milliseconds(kernelHistoryCheckpointMs); };
        if (!current()) return canceled();

        const std::wstring xpath =
            L"*[System[Provider[@Name='Microsoft-Windows-Kernel-Power'] and "
            L"(EventID=42 or EventID=107 or EventID=187 or EventID=506 or EventID=507) and "
            L"TimeCreated[timediff(@SystemTime) >= 0 and timediff(@SystemTime) <= " +
            std::to_wstring(kernelHistoryLookbackMs) + L"]]]";
        kernelhistory::EventHandle query;
        query.handle = EvtQuery(nullptr, L"System", xpath.c_str(), EvtQueryChannelPath | EvtQueryReverseDirection);
        const DWORD queryError = query.handle ? ERROR_SUCCESS : GetLastError();
        if (!current()) return canceled();
        if (!query.handle) return finish("query-failed", queryError);

        std::array<wchar_t, kernelHistoryMaxXmlBytes / sizeof(wchar_t)> buffer;
        std::size_t total = 0;
        for (;;) {
            if (!current()) return canceled();
            if (expired()) return finish("checkpoint-budget-reached");
            kernelhistory::EventHandle event;
            DWORD count = 0;
            const BOOL next = EvtNext(query.handle, 1, &event.handle, 0, 0, &count);
            const DWORD nextError = next ? ERROR_SUCCESS : GetLastError();
            if (!current()) return canceled();
            if (!next) {
                if (nextError != ERROR_NO_MORE_ITEMS) return finish("next-failed", nextError);
                result["queryExhausted"] = true;
                return finish(rows.empty() ? "no-matching-records" : "events-collected");
            }
            if (count != 1 || !event.handle) return finish("invalid-next-result", ERROR_INVALID_DATA);
            if (rows.size() == kernelHistoryMaxEvents) return finish("event-limit-reached");
            if (expired()) return finish("checkpoint-budget-reached");

            DWORD used = 0, properties = 0;
            const BOOL rendered = EvtRender(nullptr, event.handle, EvtRenderEventXml, kernelHistoryMaxXmlBytes,
                buffer.data(), &used, &properties);
            const DWORD renderError = rendered ? ERROR_SUCCESS : GetLastError();
            if (!current()) return canceled();
            if (!rendered)
                return finish(renderError == ERROR_INSUFFICIENT_BUFFER ? "event-xml-limit-reached" : "render-failed", renderError);
            const auto units = used / sizeof(wchar_t);
            if (used > kernelHistoryMaxXmlBytes || used % sizeof(wchar_t) != 0 || units < 2 ||
                    std::find(buffer.begin(), buffer.begin() + units, L'\0') != buffer.begin() + units - 1)
                return finish("invalid-render-result", ERROR_INVALID_DATA);
            const auto textUnits = static_cast<int>(units - 1);
            const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), textUnits,
                nullptr, 0, nullptr, nullptr);
            if (!bytes) return finish("xml-conversion-failed", GetLastError());
            if (static_cast<unsigned>(bytes) > kernelHistoryMaxXmlBytes)
                return finish("event-xml-limit-reached");
            if (static_cast<unsigned>(bytes) > kernelHistoryMaxTotalXmlBytes - total)
                return finish("total-xml-limit-reached");
            std::string xml(static_cast<std::size_t>(bytes), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), textUnits,
                    xml.data(), bytes, nullptr, nullptr) != bytes)
                return finish("xml-conversion-failed", GetLastError());
            total += xml.size();
            rows.push_back({{"xml", std::move(xml)}});
        }
    } catch (...) {
        return nullptr;
    }
}

} // namespace ymcc::sleepexport
