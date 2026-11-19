#pragma once
#include <cstdint>
#include <string_view>

namespace ymcc::sleepRecoveryRecords {

inline std::uint32_t pidForName(std::wstring_view name) noexcept {
    constexpr std::wstring_view pendingSuffix = L".suspend-pending.json", journalSuffix = L".resume.json";
    std::wstring_view digits;
    if (name.ends_with(L".txt")) {
        digits = name.substr(0, name.size() - 4);
    } else if (name.size() > pendingSuffix.size() + 1 && name.front() == L'.' && name.ends_with(pendingSuffix)) {
        digits = name.substr(1, name.size() - 1 - pendingSuffix.size());
    } else if (name.size() > journalSuffix.size() + 1 && name.front() == L'.' && name.ends_with(journalSuffix)) {
        digits = name.substr(1, name.size() - 1 - journalSuffix.size());
    }
    if (digits.empty() || digits.size() > 10 || digits.front() == L'0') return 0;
    std::uint64_t pid = 0;
    for (const auto digit : digits) {
        if (digit < L'0' || digit > L'9') return 0;
        pid = pid * 10 + (digit - L'0');
    }
    return pid > 4 && pid <= 0x7fffffffU ? static_cast<std::uint32_t>(pid) : 0;
}

} // namespace ymcc::sleepRecoveryRecords
