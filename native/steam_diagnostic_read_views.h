#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ymcc::steamdiagnostic {
using Read = std::function<bool(std::uint64_t, void*, std::size_t)>;
using ModulePointerNormalizer = std::function<bool(std::uint64_t, std::uint64_t&)>;
// Explicit read intent only; these callbacks do not establish module/process,
// power, selection, provenance or action authority. Module reads fall back to
// rawObject and module pointers retain their raw value when ports are absent.
struct ReadViews {
    Read rawObject;
    Read canonicalModule;
    ModulePointerNormalizer explicitModulePointerNormalizer;
};
}
