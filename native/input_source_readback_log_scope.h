#pragma once
#include <cstdint>
#include <limits>

namespace ymcc::inputSourceReadbackLog {
inline constexpr unsigned schema=1;
inline constexpr char marker[]="readbackEvidenceSchema";
inline constexpr char scope[]="sequential-extra-canonical-read-vs-existing-System32-reads;never-used-for-source-or-output";
inline constexpr char cacheScope[]="copied-under-source-lock;not-an-extra-hardware-read-or-atomic-with-system-state";

struct WindowOrdinal {
    std::uint64_t last=0;
    std::uint64_t active=0;
    void open() noexcept {
        active=last==(std::numeric_limits<std::uint64_t>::max)()?0:++last;
    }
    void close() noexcept {active=0;}
};
}
