#pragma once
#include "process_inventory.h"
#include <string_view>

// Steam consumers share the existing process producer. These are candidates,
// not an identity/action lease. Independent EXEs still have independent stores;
// their final file/operation checks cannot borrow another process's old result.
namespace ymcc::steamprocess {
enum class Presence { Unknown, Absent, Present };
inline std::shared_ptr<const processInventory::Snapshot> candidates(
    ULONGLONG maxAgeMs = 250, processInventory::Query query = processInventory::resolveQuery()) {
    return processInventory::read(query, processInventory::currentTime, maxAgeMs);
}
inline Presence presence(const wchar_t* name = L"steam.exe", ULONGLONG maxAgeMs = 250,
    processInventory::Query query = processInventory::resolveQuery()) noexcept {
    try {
        if (!name || !*name) return Presence::Unknown;
        const auto receipt = candidates(maxAgeMs, query);
        if (!receipt || receipt->status != processInventory::Status::Complete) return Presence::Unknown;
        for (const auto& row : receipt->rows)
            if (!_wcsicmp(row.name.c_str(), name)) return Presence::Present;
        return Presence::Absent;
    } catch (...) { return Presence::Unknown; }
}
}
