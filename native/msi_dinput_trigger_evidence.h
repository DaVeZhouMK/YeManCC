#pragma once
#include <array>
#include <cstdint>

// Pump-owned evidence only. Never maps, admits, polls or emits input.
namespace ymcc::msi::triggerEvidence {
struct Sample {
    std::array<std::int32_t, 6> rawAxes{}, normalizedAxes{}, actualMin{}, actualMax{};
    std::array<std::uint32_t, 4> rawButtons{}, rawLowerBitButtons{}, rawLowBitOnlyButtons{};
    std::array<std::uint8_t, 128> rawButtonBytes{};
    std::array<std::int16_t, 4> sticks{};
    std::uint64_t startedTick = 0, completedTick = 0, power = 0, mode = 0,
        hostEpoch = 0, sourceRevision = 0, canonicalReadSequence = 0;
    std::uint32_t buttons = 0, pov = 0xffffffffu, reportedButtonCount = 0;
    unsigned rawRangeKnownMask = 0, rawRangeViolationMask = 0, normalizedRangeViolationMask = 0;
    bool canonicalKnown = false;
};
// completedTick uses the existing read-completion millisecond clock. No clock query here.
inline constexpr std::uint64_t sustainedIntervalMs = 5000;
inline constexpr unsigned contextBurst = 4, sustainedCapacity = 32, contextCapacity = 8;
struct Decision {
    std::uint64_t sample = 0, recordSelections = 0, sustainedSelections = 0,
        contextSelections = 0, contextSuppressed = 0, meaningfulSuppressed = 0;
    unsigned reasonBits = 0, contextChangeBits = 0;
    bool legacyRecordSelected = false; // Only the original first64 plus one cap receipt.
    bool sustainedWitness = false, contextBoundarySelected = false, contextBoundarySuppressed = false;
    unsigned emitted = 0;
    // record selects a candidate; the caller keeps its existing virtual-domain log policy.
    bool record = false, firstSamples = false, triggerEdge = false, outcomeEdge = false,
         capacityReached = false, buttonEdge = false, rawButtonEdge = false, povEdge = false,
         stickEdge = false, rangeEdge = false, contextEdge = false;
};
struct Edge {
    Sample signal{};
    std::uint64_t sample = 0;
    unsigned left = 0, right = 0, outcome = 0, reasons = 0;
};
struct ContextBoundary {
    Edge before{}, after{};
    std::uint64_t ordinal = 0;
    unsigned changeBits = 0;
    bool contextQuotaSelected = false, recordSelected = false;
};
struct Snapshot {
    std::uint64_t generation = 0, samples = 0, meaningful = 0, triggerEdges = 0,
        buttonEdges = 0, rawButtonEdges = 0, povEdges = 0, stickEdges = 0,
        rangeEdges = 0, outcomeEdges = 0, contextEdges = 0;
    std::uint64_t recordSelections = 0, sustainedSelections = 0, sustainedSkippedIntervals = 0,
        contextSelections = 0, contextSuppressed = 0, meaningfulSuppressed = 0,
        clockRepeats = 0, clockRollbacks = 0, clockHighWater = 0;
    unsigned logAttempts = 0;
    bool capacityReported = false;
    std::array<Edge, 64> recent{}, initial{};
    std::array<Edge, sustainedCapacity> sustained{};
    std::array<ContextBoundary, contextCapacity> firstContexts{}, recentContexts{};
};
class Observer {
    std::uint64_t generation_ = 0, samples_ = 0, meaningful_ = 0;
    unsigned emitted_ = 0, previousClass_ = 0, previousOutcome_ = 0, previousSticks_ = 0;
    Sample previous_{};
    bool havePrevious_ = false, capacityReported_ = false;
    std::array<Edge, 64> recent_{}, initial_{};
    std::array<Edge, sustainedCapacity> sustained_{};
    std::array<ContextBoundary, contextCapacity> firstContexts_{}, recentContexts_{};
    std::uint64_t recordSelections_ = 0, sustainedSelections_ = 0, sustainedSkippedIntervals_ = 0,
        contextSelections_ = 0, contextSuppressed_ = 0, meaningfulSuppressed_ = 0,
        clockRepeats_ = 0, clockRollbacks_ = 0, clockHighWater_ = 0,
        sustainedTick_ = 0, contextRefillTick_ = 0;
    unsigned contextTokens_ = contextBurst, previousLeft_ = 0, previousRight_ = 0;
    std::uint64_t triggerEdges_ = 0, buttonEdges_ = 0, rawButtonEdges_ = 0, povEdges_ = 0,
        stickEdges_ = 0, rangeEdges_ = 0, outcomeEdges_ = 0, contextEdges_ = 0;
    static unsigned stickClass(std::int16_t value, unsigned deadzone) noexcept {
        const int v = value;
        // Evidence classes only. No deadzone or clipping is applied to output.
        if (v >= 32760) return 3;
        if (v <= -32760) return 4;
        if (v > static_cast<int>(deadzone)) return 1;
        if (v < -static_cast<int>(deadzone)) return 2;
        return 0;
    }
    static unsigned sticks(const Sample& s) noexcept {
        unsigned result = 0;
        for (unsigned i = 0; i < 4; ++i)
            result |= stickClass(s.sticks[i], i < 2 ? 7849u : 8689u) << (i * 3);
        return result;
    }
    void reset(std::uint64_t generation) noexcept {
        generation_ = generation; samples_ = meaningful_ = 0;
        emitted_ = previousClass_ = previousOutcome_ = previousSticks_ = 0;
        havePrevious_ = capacityReported_ = false;
        triggerEdges_ = buttonEdges_ = rawButtonEdges_ = povEdges_ = stickEdges_ = 0;
        rangeEdges_ = outcomeEdges_ = contextEdges_ = 0;
        recordSelections_ = sustainedSelections_ = sustainedSkippedIntervals_ = 0;
        contextSelections_ = contextSuppressed_ = meaningfulSuppressed_ = 0;
        clockRepeats_ = clockRollbacks_ = clockHighWater_ = sustainedTick_ = contextRefillTick_ = 0;
        contextTokens_ = contextBurst; previousLeft_ = previousRight_ = 0;
        // Counts gate every array. Unused old slots are never exposed after reset.
    }
public:
    static constexpr unsigned firstSamples = 4, capacity = 64;
    Decision observe(std::uint64_t generation, unsigned left, unsigned right,
                     unsigned outcome, const Sample& signal = Sample{}) noexcept {
        if (generation_ != generation) reset(generation);
        const unsigned state = (left != 0 ? 1u : 0u) | (right != 0 ? 2u : 0u) |
            (left >= 30 ? 4u : 0u) | (right >= 30 ? 8u : 0u) |
            (left == 127 && right == 127 ? 16u : 0u) |
            (left == 127 ? 32u : 0u) | (right == 127 ? 64u : 0u) |
            (left == 255 ? 128u : 0u) | (right == 255 ? 256u : 0u);
        const unsigned stickState = sticks(signal);
        Decision result;
        result.sample = ++samples_;
        result.firstSamples = samples_ <= firstSamples;
        result.triggerEdge = havePrevious_ && previousClass_ != state;
        result.outcomeEdge = havePrevious_ && previousOutcome_ != outcome;
        result.buttonEdge = havePrevious_ && (previous_.buttons != signal.buttons || previous_.canonicalKnown != signal.canonicalKnown);
        result.rawButtonEdge = havePrevious_ && (previous_.rawButtons != signal.rawButtons || previous_.rawButtonBytes != signal.rawButtonBytes);
        result.povEdge = havePrevious_ && previous_.pov != signal.pov;
        result.stickEdge = havePrevious_ && previousSticks_ != stickState;
        result.rangeEdge = havePrevious_ && (previous_.rawRangeKnownMask != signal.rawRangeKnownMask ||
            previous_.rawRangeViolationMask != signal.rawRangeViolationMask ||
            previous_.normalizedRangeViolationMask != signal.normalizedRangeViolationMask);
        result.contextEdge = havePrevious_ && (previous_.power != signal.power || previous_.mode != signal.mode ||
            previous_.hostEpoch != signal.hostEpoch || previous_.sourceRevision != signal.sourceRevision);
        triggerEdges_ += result.triggerEdge; buttonEdges_ += result.buttonEdge;
        rawButtonEdges_ += result.rawButtonEdge; povEdges_ += result.povEdge;
        stickEdges_ += result.stickEdge; rangeEdges_ += result.rangeEdge;
        outcomeEdges_ += result.outcomeEdge; contextEdges_ += result.contextEdge;
        if (!havePrevious_) {
            clockHighWater_ = sustainedTick_ = contextRefillTick_ = signal.completedTick;
        } else if (signal.completedTick < clockHighWater_) {
            ++clockRollbacks_;
        } else if (signal.completedTick == clockHighWater_) {
            ++clockRepeats_;
        } else {
            clockHighWater_ = signal.completedTick;
        }
        // A regression never rebases either budget. Repeated/backward timestamps
        // cannot earn tokens; a large forward jump selects one frame, no catch-up loop.
        const auto elapsed = clockHighWater_ - sustainedTick_;
        if (elapsed >= sustainedIntervalMs) {
            result.sustainedWitness = true;
            sustainedSkippedIntervals_ += elapsed / sustainedIntervalMs - 1;
            sustainedTick_ = clockHighWater_;
        }
        const auto refill = (clockHighWater_ - contextRefillTick_) / sustainedIntervalMs;
        if (refill) {
            const auto tokens = refill + contextTokens_;
            contextTokens_ = tokens >= contextBurst ? contextBurst : static_cast<unsigned>(tokens);
            contextRefillTick_ = clockHighWater_ - (clockHighWater_ - contextRefillTick_) % sustainedIntervalMs;
        }
        if (result.contextEdge) {
            result.contextChangeBits = (previous_.power != signal.power ? 1u : 0u) |
                (previous_.mode != signal.mode ? 2u : 0u) |
                (previous_.hostEpoch != signal.hostEpoch ? 4u : 0u) |
                (previous_.sourceRevision != signal.sourceRevision ? 8u : 0u);
            if (contextTokens_) {
                --contextTokens_; ++contextSelections_; result.contextBoundarySelected = true;
            } else {
                ++contextSuppressed_; result.contextBoundarySuppressed = true;
            }
        }
        const unsigned reasons = (result.firstSamples ? 1u : 0u) | (result.triggerEdge ? 2u : 0u) |
            (result.outcomeEdge ? 4u : 0u) | (result.buttonEdge ? 8u : 0u) |
            (result.rawButtonEdge ? 16u : 0u) | (result.povEdge ? 32u : 0u) |
            (result.stickEdge ? 64u : 0u) | (result.rangeEdge ? 128u : 0u) |
            (result.contextEdge ? 256u : 0u);
        if (reasons) {
            recent_[meaningful_++ % capacity] = {signal, samples_, left, right, outcome, reasons};
            if (emitted_ < capacity) {
                initial_[emitted_++] = {signal, samples_, left, right, outcome, reasons};
                result.record = true;
            } else if (!capacityReported_) {
                result.record = true; result.capacityReached = true; capacityReported_ = true;
            }
        }
        result.legacyRecordSelected = result.record;
        result.record = result.record || result.sustainedWitness || result.contextBoundarySelected;
        result.reasonBits = reasons | (result.sustainedWitness ? 512u : 0u) |
            (result.contextBoundarySelected ? 1024u : 0u) | (result.capacityReached ? 2048u : 0u);
        if (result.sustainedWitness || result.contextEdge) {
            const Edge current{signal, samples_, left, right, outcome, result.reasonBits};
            if (result.sustainedWitness)
                sustained_[sustainedSelections_++ % sustainedCapacity] = current;
            if (result.contextEdge) {
                // Independent from input-edge churn: preserve the first boundaries and
                // a separate recent boundary ring, including the actual pre-change sample.
                const ContextBoundary boundary{
                    {previous_, samples_ - 1, previousLeft_, previousRight_, previousOutcome_, 0},
                    current, contextEdges_, result.contextChangeBits, result.contextBoundarySelected, result.record};
                if (contextEdges_ <= contextCapacity) firstContexts_[contextEdges_ - 1] = boundary;
                recentContexts_[(contextEdges_ - 1) % contextCapacity] = boundary;
            }
        }
        if (result.record) ++recordSelections_;
        else if (reasons) ++meaningfulSuppressed_;
        result.recordSelections = recordSelections_;
        result.sustainedSelections = sustainedSelections_;
        result.contextSelections = contextSelections_;
        result.contextSuppressed = contextSuppressed_;
        result.meaningfulSuppressed = meaningfulSuppressed_;
        previousClass_ = state; previousOutcome_ = outcome; previousSticks_ = stickState;
        previousLeft_ = left; previousRight_ = right;
        previous_ = signal; havePrevious_ = true;
        result.emitted = emitted_;
        return result;
    }
    Snapshot snapshot() const noexcept {
        Snapshot s;
        s.generation = generation_; s.samples = samples_; s.meaningful = meaningful_;
        s.triggerEdges = triggerEdges_; s.buttonEdges = buttonEdges_; s.rawButtonEdges = rawButtonEdges_;
        s.povEdges = povEdges_; s.stickEdges = stickEdges_; s.rangeEdges = rangeEdges_;
        s.outcomeEdges = outcomeEdges_; s.contextEdges = contextEdges_;
        s.logAttempts = emitted_; s.capacityReported = capacityReported_;
        s.recordSelections = recordSelections_; s.sustainedSelections = sustainedSelections_;
        s.sustainedSkippedIntervals = sustainedSkippedIntervals_;
        s.contextSelections = contextSelections_; s.contextSuppressed = contextSuppressed_;
        s.meaningfulSuppressed = meaningfulSuppressed_; s.clockRepeats = clockRepeats_;
        s.clockRollbacks = clockRollbacks_; s.clockHighWater = clockHighWater_;
        for (unsigned i = 0; i < emitted_; ++i) s.initial[i] = initial_[i];
        const auto sustainedCount = sustainedSelections_ < sustainedCapacity ? sustainedSelections_ : sustainedCapacity;
        for (std::uint64_t i = 0; i < sustainedCount; ++i)
            s.sustained[static_cast<unsigned>(i)] = sustained_[(sustainedSelections_ - sustainedCount + i) % sustainedCapacity];
        const auto contextCount = contextEdges_ < contextCapacity ? contextEdges_ : contextCapacity;
        for (std::uint64_t i = 0; i < contextCount; ++i) {
            s.firstContexts[static_cast<unsigned>(i)] = firstContexts_[static_cast<unsigned>(i)];
            s.recentContexts[static_cast<unsigned>(i)] = recentContexts_[(contextEdges_ - contextCount + i) % contextCapacity];
        }
        const auto count = meaningful_ < capacity ? meaningful_ : capacity;
        const auto first = meaningful_ - count;
        for (std::uint64_t i = 0; i < count; ++i) s.recent[static_cast<unsigned>(i)] = recent_[(first + i) % capacity];
        return s;
    }
};
} // namespace ymcc::msi::triggerEvidence
