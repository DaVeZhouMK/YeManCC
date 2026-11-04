#pragma once
#include "process_inventory.h"
#include <cwchar>
#include <limits>
#include <string_view>

// All executable discovery is supplied by the native inventory owner. These
// names/PIDs are short-lived hints, never process identity or control authority.
namespace ymcc::processNames {
using BasicQuery = ymcc::processInventory::Query;
using BasicResult = ymcc::processInventory::Status;
inline BasicQuery resolveBasicQuery() noexcept { return ymcc::processInventory::resolveQuery(); }
enum class NameMatch { OrdinalIgnoreCase, CrtIgnoreCase };
inline bool matches(std::wstring_view value, std::wstring_view name, NameMatch match) {
    if (value.size() != name.size()) return false;
    if (name.empty()) return true;
    if (name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    return match == NameMatch::CrtIgnoreCase
        ? _wcsnicmp(value.data(), name.data(), name.size()) == 0
        : CompareStringOrdinal(value.data(), static_cast<int>(value.size()),
            name.data(), static_cast<int>(name.size()), TRUE) == CSTR_EQUAL;
}
inline bool parseBasicCandidates(const void* data, std::size_t bytes,
    std::wstring_view name, std::vector<DWORD>& candidates, NameMatch match = NameMatch::OrdinalIgnoreCase) {
    candidates.clear();
    std::vector<ymcc::processInventory::Entry> rows;
    if (name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        !ymcc::processInventory::parseBasic(data, bytes, rows)) return false;
    for (const auto& row : rows)
        if (row.pid && !row.name.empty() && matches(row.name, name, match)) candidates.push_back(row.pid);
    return true;
}
inline BasicResult candidates(std::wstring_view name, std::vector<DWORD>& pids,
    DWORD& error, BasicQuery query = resolveBasicQuery(), NameMatch match = NameMatch::OrdinalIgnoreCase,
    ULONGLONG maxAge = 250) {
    pids.clear(); error = ERROR_SUCCESS;
    if (name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        error = ERROR_INVALID_DATA; return BasicResult::Failed;
    }
    const auto snapshot = ymcc::processInventory::read(query, ymcc::processInventory::currentTime, maxAge);
    error = snapshot->error;
    if (snapshot->status != BasicResult::Complete) return snapshot->status;
    for (const auto& row : snapshot->rows)
        if (row.pid && !row.name.empty() && matches(row.name, name, match)) pids.push_back(row.pid);
    return BasicResult::Complete;
}
// Compatibility for the original query-only API: report basic support separately
// from the owner's centralized old-OS fallback. Production consumers use candidates.
inline BasicResult basicCandidates(std::wstring_view name, std::vector<DWORD>& pids,
    DWORD& error, BasicQuery query = resolveBasicQuery(), NameMatch match = NameMatch::OrdinalIgnoreCase) {
    pids.clear(); error = ERROR_SUCCESS;
    const auto snapshot = ymcc::processInventory::read(query);
    if (snapshot->basicStatus != BasicResult::Complete) {
        if (snapshot->basicStatus == BasicResult::Failed) error = snapshot->error;
        return snapshot->basicStatus;
    }
    for (const auto& row : snapshot->rows)
        if (row.pid && !row.name.empty() && matches(row.name, name, match)) pids.push_back(row.pid);
    return BasicResult::Complete;
}
using SessionQuery = decltype(&::ProcessIdToSessionId);
} // namespace ymcc::processNames
