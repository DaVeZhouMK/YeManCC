#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct NativeDetectedGame;
class NativeOriginalGameRuleQualification;
class NativeOriginalGameRulesLease;
struct NativeGameRulesSnapshot {
    std::vector<std::wstring> excludes, whitelist, configured;
    std::string customConfiguration;
    bool known = false;
    bool samePolicy(const NativeGameRulesSnapshot& other) const {
        return known && other.known && excludes == other.excludes &&
            whitelist == other.whitelist && configured == other.configured &&
            customConfiguration == other.customConfiguration;
    }
};
static std::shared_ptr<const NativeOriginalGameRuleQualification> nativeQualifyOriginalGameRules(
    const NativeDetectedGame&, unsigned long, unsigned long long);
static std::shared_ptr<NativeOriginalGameRulesLease> nativeBindOriginalGameRulesLocked(
    const std::shared_ptr<const NativeOriginalGameRuleQualification>&, unsigned long long);
static bool nativeContinueOriginalGameRulesLocked(NativeOriginalGameRulesLease&,
    const NativeGameRulesSnapshot&, unsigned long long, unsigned long long) noexcept;

// Only the shared game owner builds this witness from the actual one-PID result.
// It contains no Steam account/GameID, process liveness or publication authority.
class NativeOriginalGameRuleQualification final {
    friend std::shared_ptr<const NativeOriginalGameRuleQualification> nativeQualifyOriginalGameRules(
        const NativeDetectedGame&, unsigned long, unsigned long long);
    NativeOriginalGameRuleQualification(unsigned long process, std::uint64_t created,
        std::wstring image, std::wstring exe, unsigned long session,
        std::uint64_t epoch, bool white, bool custom)
        : pid(process), birth(created), path(std::move(image)), name(std::move(exe)),
          windowsSession(session), originEpoch(epoch), whitelisted(white), customConfigured(custom) {}
public:
    const unsigned long pid;
    const std::uint64_t birth;
    const std::wstring path, name;
    const unsigned long windowsSession;
    const std::uint64_t originEpoch;
    const bool whitelisted, customConfigured;
};

// The existing captureMx serializes every mutable field. No mutex, OS resource,
// watcher, manager or Current callback is created by this memory-only lease.
class NativeOriginalGameRulesLease final {
    friend std::shared_ptr<NativeOriginalGameRulesLease> nativeBindOriginalGameRulesLocked(
        const std::shared_ptr<const NativeOriginalGameRuleQualification>&, unsigned long long);
    friend bool nativeContinueOriginalGameRulesLocked(NativeOriginalGameRulesLease&,
        const NativeGameRulesSnapshot&, unsigned long long, unsigned long long) noexcept;
    std::uint64_t checkedEpoch_;
    bool revoked_ = false;
    NativeOriginalGameRulesLease(std::shared_ptr<const NativeOriginalGameRuleQualification> proof,
        std::uint64_t serial) : checkedEpoch_(proof->originEpoch), qualification(std::move(proof)), requestSerial(serial) {}
public:
    const std::shared_ptr<const NativeOriginalGameRuleQualification> qualification;
    const std::uint64_t requestSerial;
    bool authorizedAt(std::uint64_t epoch, std::uint64_t serial) const noexcept {
        return !revoked_ && epoch && checkedEpoch_ == epoch && requestSerial == serial;
    }
    void revoke() noexcept { revoked_ = true; }
    std::uint64_t checkedEpoch() const noexcept { return checkedEpoch_; }
};
