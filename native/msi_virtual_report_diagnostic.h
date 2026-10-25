#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

// Pure, single-owner state only. The caller serializes access and supplies a
// monotonic millisecond tick. No logger, backend, timer, identity discovery,
// control policy, JSON, OS, file or device code belongs in this helper.
namespace ymcc::msi::virtualReportDiag {

// Caller supplies ONE canonical seqlock snapshot: sequence is the pump's
// 64-bit canonical-publish ordinal; generation is the verified DInput binding
// ordinal (bindCount), NOT Host epoch or USB report ordinal; capturedTick is
// the pump's existing now from that same publish. Never attach generation later.
struct SourceStamp {
    uint64_t sequence = 0, generation = 0, capturedTick = 0;
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    bool valid = false;
};

struct Binding {
    uint32_t nativePid = 0;
    std::string runId;
    uint64_t epoch = 0, powerGeneration = 0;
};

struct Gates {
    bool knownMsiBoard = false, visibilityDiagnosticBuild = false,
         virtualDetailLogEnabled = false, exitFence = false, powerReady = false;
};

struct PrepareAuthorization {
    Binding binding;
    std::string sessionId;
    uint32_t durationMs = 10000, maxMeaningfulEdges = 128;
};

struct FrameMetadata {
    std::string sessionId;
    SourceStamp source;
};

class Session {
public:
    Session() noexcept = default;
    // Copying/moving a live authorization would create ambiguous owners.
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    // This is the ONLY entry point that creates intent. Lifecycle/persona/
    // focus/wake/virtual-switch code must never call it automatically. Use a
    // distinct session ID for each intentional session to correlate receipts.
    // By-value argument construction is the caller's responsibility; no
    // allocation occurs inside Request itself.
    bool Request(std::string sessionId, uint32_t durationMs = 10000,
                 uint32_t maxMeaningfulEdges = 128) noexcept {
        if (phase_ != Phase::Off || !SafeSessionId(sessionId) ||
            durationMs < 1 || durationMs > 30000 ||
            maxMeaningfulEdges < 8 || maxMeaningfulEdges > 128) return false;
        Close();
        sessionId_.swap(sessionId);
        durationMs_ = durationMs;
        maxMeaningfulEdges_ = maxMeaningfulEdges;
        phase_ = Phase::Pending;
        return true;
    }

    // version() reports the observed value; ONLY exactly 1 is a capability.
    // An unconsumed pending intent survives an unsupported HELLO, but nothing
    // is offered or armed by HELLO. Consumed authorizations fail closed.
    void ObserveHello(unsigned version) noexcept {
        version_ = version;
        if (version != 1 && phase_ != Phase::Pending) Close();
    }

    std::optional<PrepareAuthorization> OfferPrepare(
        const Binding& binding, const Gates& gates, uint64_t now) noexcept {
        if (phase_ != Phase::Pending || version_ != 1 ||
            !Complete(binding) || !Eligible(gates)) return std::nullopt;
        try {
            // Allocate every owning copy before committing the one-shot offer.
            // Return values never reference internal strings or caller storage.
            Binding ownedBinding = binding;
            std::optional<PrepareAuthorization> result{
                PrepareAuthorization{binding, sessionId_, durationMs_, maxMeaningfulEdges_}};
            binding_ = std::move(ownedBinding);
            offeredTick_ = now;
            phase_ = Phase::Offered; // Intent is consumed, even if ACK never arrives.
            return result;
        } catch (...) {
            Close(); // Allocation failure affects diagnostics only; never retry intent.
            return std::nullopt;
        }
    }

    // The offer waits for ACK for at most durationMs (exclusive deadline).
    // The ACTIVE TTL starts at this successful ACK, not at request/offer time.
    // ACK after ClearHost/Close, duplicate ACK, wrong tuple/ID, unarmed ACK,
    // clock regression or an unrepresentable active deadline all fail closed.
    bool AcceptPrepare(const Binding& binding, bool ackArmed,
                       const std::string& ackSessionId, uint64_t now) noexcept {
        if (phase_ != Phase::Offered || version_ != 1 || !ackArmed ||
            !Same(binding_, binding) || ackSessionId != sessionId_ ||
            now < offeredTick_ || now - offeredTick_ >= durationMs_ ||
            now > (std::numeric_limits<uint64_t>::max)() - durationMs_) {
            Close();
            return false;
        }
        activeTick_ = now;
        deadline_ = now + durationMs_;
        phase_ = Phase::Active;
        return true;
    }

    // Metadata ONLY: this cannot change/suppress a frame, manufacture a neutral
    // source, or derive hardware source generation from a Host epoch. Neutral
    // frames may attach metadata if the caller actually supplies a fresh source.
    // No attempt is charged here: backend/control must run independently first.
    // Gate changes during an outstanding offer must be closed by the caller;
    // AcceptPrepare intentionally has no Gates parameter.
    std::optional<FrameMetadata> SourceForFrame(
        const Binding& binding, const Gates& gates, const SourceStamp& source,
        uint64_t now) noexcept {
        if (phase_ != Phase::Active) return std::nullopt;
        if (version_ != 1 || !Same(binding_, binding) || !Eligible(gates) || !Live(now)) {
            Close();
            return std::nullopt;
        }
        if (!Fresh(source, now)) return std::nullopt; // Unknown stays unknown.
        try {
            return FrameMetadata{sessionId_, source}; // Preserve all raw axes.
        } catch (...) {
            Close();
            return std::nullopt;
        }
    }

    // Call AFTER the direct backend hook, then let main isolate any logger
    // exceptions. Each active call (even invalid metadata) charges one Native
    // attempt. submittedButtons are FINAL native output bits, NEVER physical
    // source bits. Raw source buttons/triggers remain a separate key. Host's <=4096 backend-call budget is separate. The last allowed
    // record/attempt can return true while closing this diagnostic session.
    bool ShouldRecordSource(const FrameMetadata& metadata, bool semanticNeutral,
                            uint64_t now, uint32_t submittedButtons = 0) noexcept {
        if (phase_ != Phase::Active) return false;
        if (!Live(now)) {
            Close();
            return false;
        }
        ++attempts_;
        bool record = false;
        if (metadata.sessionId == sessionId_ && Fresh(metadata.source, now)) {
            const auto& source = metadata.source;
            const MeaningfulKey key{source.generation, source.buttons,
                                    source.leftTrigger, source.rightTrigger, semanticNeutral, submittedButtons};
            record = !haveKey_ || !SameKey(key_, key);
            if (record) {
                key_ = key;
                haveKey_ = true;
                ++records_;
            }
        }
        if (attempts_ >= 4096 || records_ >= maxMeaningfulEdges_) Close();
        return record;
    }

    // Initial Host clearing must not erase unconsumed explicit intent. Once
    // offered, there is NO automatic rearm across recreation/retarget/wake.
    void ClearHost() noexcept {
        version_ = 0;
        if (phase_ != Phase::Pending) Close();
    }

    void Close() noexcept {
        phase_ = Phase::Off;
        sessionId_.clear();
        binding_.nativePid = 0;
        binding_.runId.clear();
        binding_.epoch = binding_.powerGeneration = 0;
        offeredTick_ = activeTick_ = deadline_ = 0;
        attempts_ = records_ = 0;
        durationMs_ = 10000;
        maxMeaningfulEdges_ = 128;
        haveKey_ = false;
        key_ = {};
        // Capability is not cleared here; ClearHost is the capability fence.
    }

    // State query, not a clock read: expiry is observed at the next timed call.
    bool active() const noexcept { return phase_ == Phase::Active; }
    unsigned version() const noexcept { return version_; }

private:
    enum class Phase { Off, Pending, Offered, Active };
    struct MeaningfulKey {
        uint64_t generation = 0;
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0, rightTrigger = 0;
        bool semanticNeutral = false;
        uint32_t submittedButtons = 0;
    };

    static bool SafeSessionId(const std::string& id) noexcept {
        if (id.empty() || id.size() > 96) return false;
        for (const unsigned char c : id) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
        }
        return true;
    }
    static bool Complete(const Binding& b) noexcept {
        return b.nativePid != 0 && !b.runId.empty() && b.epoch != 0 && b.powerGeneration != 0;
    }
    static bool Same(const Binding& a, const Binding& b) noexcept {
        return a.nativePid == b.nativePid && a.runId == b.runId &&
               a.epoch == b.epoch && a.powerGeneration == b.powerGeneration;
    }
    static bool Eligible(const Gates& g) noexcept {
        return g.knownMsiBoard && g.visibilityDiagnosticBuild &&
               g.virtualDetailLogEnabled && !g.exitFence && g.powerReady;
    }
    static bool Fresh(const SourceStamp& s, uint64_t now) noexcept {
        return s.valid && s.sequence != 0 && s.generation != 0 &&
               s.capturedTick <= now && now - s.capturedTick <= 1000;
    }
    bool Live(uint64_t now) const noexcept { return now >= activeTick_ && now < deadline_; }
    static bool SameKey(const MeaningfulKey& a, const MeaningfulKey& b) noexcept {
        return a.generation == b.generation && a.buttons == b.buttons &&
               a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger &&
               a.semanticNeutral == b.semanticNeutral && a.submittedButtons == b.submittedButtons;
    }

    Phase phase_ = Phase::Off;
    unsigned version_ = 0;
    std::string sessionId_;
    Binding binding_;
    uint64_t offeredTick_ = 0, activeTick_ = 0, deadline_ = 0;
    uint32_t durationMs_ = 10000, maxMeaningfulEdges_ = 128, attempts_ = 0, records_ = 0;
    bool haveKey_ = false;
    MeaningfulKey key_;
};

} // namespace ymcc::msi::virtualReportDiag
