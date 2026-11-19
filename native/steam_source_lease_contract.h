#pragma once
// Pure SourceLeaseV2 contract; no producer, Observer, cache or discovery.
#include "steam_installation_facts.h"
#include <array>
#include <memory>

namespace ymcc::steaminfo {
#ifndef YMCC_STEAM_SOURCE_LEASE_V2_DECLARED
#define YMCC_STEAM_SOURCE_LEASE_V2_DECLARED 1
class SourceLeaseV2 {
public:
    virtual ~SourceLeaseV2() = default;
    virtual const SourceSnapshot& snapshot() const noexcept = 0;
    virtual bool current() noexcept = 0;
    virtual const std::array<void*,3>& cancellationEvents() const noexcept = 0;
    virtual void* providerProcess() const noexcept = 0;
};
std::shared_ptr<SourceLeaseV2> acquireSourceV2() noexcept;
std::shared_ptr<SourceLeaseV2> acquireRegistryCandidateSourceV2() noexcept;
#endif
}
