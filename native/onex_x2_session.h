#pragma once
#include "onex_x2_protocol.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Transport-neutral state for the new OneX adapter (runtime integration pending).
// No Windows calls, clock,
// threads, sleeps, device enumeration, producer publication or hardware claims.
namespace ymcc::onex_x2 {
struct Context {
    std::uint64_t power = 0, topology = 0;
    bool operator==(const Context& other) const noexcept { return power == other.power && topology == other.topology; }
    bool operator!=(const Context& other) const noexcept { return !(*this == other); }
};
enum class InputLayout : std::uint8_t { Unverified, LogicalFrame, ZeroReportIdThenFrame, ReportIdThenFrame };
struct TransportContract {
    InputLayout inputLayout = InputLayout::Unverified;
    std::size_t inputBytes = 0, outputBytes = 0;
};
struct Operation {
    std::uint64_t generation = 0, sequence = 0;
    bool operator==(const Operation& other) const noexcept { return generation == other.generation && sequence == other.sequence; }
};
enum class Phase : std::uint8_t { Detached, Live, Retaining };
enum class StopReason : std::uint8_t { None, SelectionChanged, ReadFailed, WriteFailed, WriteTimeout, QueueFull, ObservationFailed };
enum class EventKind : std::uint8_t { RearChanged, Click, RearObserved };
struct Event {
    EventKind kind = EventKind::RearChanged;
    Button button = Button::None;
    std::uint8_t rearBits = 0;
    std::uint64_t generation = 0, capturedAt = 0;
    Context context{};
    std::uint8_t vendorButtonId = 0;
};
struct Cancellation { bool read = false, write = false; };
struct Snapshot {
    Model requested = Model::Unsupported, bound = Model::Unsupported;
    Context context{};
    Phase phase = Phase::Detached;
    StopReason reason = StopReason::None;
    std::uint64_t generation = 0, publicationRevision = 0, retryAt = 0;
    bool readPending = false, writePending = false, retainedNotice = false, initialized = false;
    std::size_t initIndex = 0, queuedEvents = 0;
    unsigned reinitializations = 0;
    std::uint8_t rearBits = 0;
    std::uint64_t validFrames = 0, invalidFrames = 0;
    bool reportIdObserved = false;
    std::uint8_t lastReportId = 0;
    InputLayout inputLayout = InputLayout::Unverified;
    std::size_t inputBytes = 0, outputBytes = 0;
};
class Session final {
public:
    static constexpr std::size_t maxInputBytes = 512, eventCapacity = 128;
    static constexpr std::uint64_t insertionDelayMs = 4000, writeTimeoutMs = 250;
    static constexpr std::uint64_t retainedNoticeMs = 1000, bindRetryMs = 2000;
    static constexpr std::uint64_t statusDelayMs = 200, reinitIntervalMs = 2000;
    static constexpr unsigned maxReinitializations = 4;
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    void request(Model model, Context context, std::uint64_t now) noexcept {
        if (!supported(model)) model = Model::Unsupported;
        if (requested_ == model && context_ == context) return;
        retire(StopReason::SelectionChanged, now);
        requested_ = model; context_ = context;
    }
    bool canBind(std::uint64_t now) const noexcept {
        return supported(requested_) && phase_ == Phase::Detached && !read_ && !write_ && now >= retryAt_;
    }
    // The caller proves its transport framing before binding. Input/output
    // lengths are independent. The state machine never auto-detects an offset.
    bool bind(TransportContract contract, std::uint64_t now) noexcept {
        if (!canBind(now) || contract.inputBytes > maxInputBytes ||
            (contract.outputBytes != 64 && contract.outputBytes != 65)) return false;
        if ((contract.inputLayout == InputLayout::LogicalFrame && contract.inputBytes < 14) ||
            ((contract.inputLayout == InputLayout::ZeroReportIdThenFrame || contract.inputLayout == InputLayout::ReportIdThenFrame) && contract.inputBytes < 15) ||
            (contract.inputLayout != InputLayout::LogicalFrame && contract.inputLayout != InputLayout::ZeroReportIdThenFrame && contract.inputLayout != InputLayout::ReportIdThenFrame)) return false;
        contract_ = contract; bound_ = requested_; phase_ = Phase::Live;
        ++generation_; ++publicationRevision_; reason_ = StopReason::None;
        initialized_ = false; initIndex_ = 0; initDue_ = add(now, insertionDelayMs);
        read_.reset(); write_.reset(); cancelledRead_ = cancelledWrite_ = false;
        retained_ = false; rearBits_ = 0; eventsHead_ = eventsCount_ = 0;
        reinitializations_ = 0; hasReinitialized_ = false; lastReinitAt_ = 0;
        reinitDue_.reset(); reportIdObserved_ = false; lastReportId_ = 0;
        validFrames_ = invalidFrames_ = 0; // An old binding cannot supply new-source evidence.
        return true;
    }
    void stop(StopReason reason, std::uint64_t now) noexcept { retire(reason, now); }
    // Called only by the existing owner tick. A notice never frees pending I/O.
    void advance(std::uint64_t now) noexcept {
        if (phase_ == Phase::Retaining) {
            retained_ = now >= add(retiredAt_, retainedNoticeMs); return;
        }
        if (phase_ != Phase::Live) return;
        if (write_ && now >= add(writeStartedAt_, writeTimeoutMs)) {
            retire(StopReason::WriteTimeout, now); return;
        }
        if (reinitDue_ && now >= *reinitDue_ && !write_) {
            if (reinitializations_ < maxReinitializations &&
                (!hasReinitialized_ || now >= add(lastReinitAt_, reinitIntervalMs))) {
                ++reinitializations_; hasReinitialized_ = true; lastReinitAt_ = now;
                initIndex_ = 0; initialized_ = false; initDue_ = now;
            }
            reinitDue_.reset();
        }
        settleInitialization(now);
    }
    std::optional<Operation> beginRead() noexcept {
        if (phase_ != Phase::Live || read_) return std::nullopt;
        readBuffer_.fill(0); read_ = Operation{generation_, ++sequence_}; return read_;
    }
    std::uint8_t* readBuffer() noexcept { return readBuffer_.data(); }
    std::size_t readSize() const noexcept { return contract_.inputBytes; }
    bool completeRead(Operation operation, bool success, std::size_t bytes, std::uint64_t now) noexcept {
        if (!read_ || !(*read_ == operation)) return false;
        read_.reset();
        if (phase_ != Phase::Live || operation.generation != generation_) { finishRetirement(); return true; }
        if (!success) { retire(StopReason::ReadFailed, now); return true; }
        if (bytes != contract_.inputBytes || bytes > maxInputBytes) { ++invalidFrames_; return true; }
        std::size_t offset = 0;
        if (contract_.inputLayout == InputLayout::ZeroReportIdThenFrame || contract_.inputLayout == InputLayout::ReportIdThenFrame) {
            // HC 3.2.49 HidReport stores raw[0] as ReportId and always strips
            // one byte. Zero is required only by the optional strict variant.
            reportIdObserved_ = true; lastReportId_ = readBuffer_[0];
            if (contract_.inputLayout == InputLayout::ZeroReportIdThenFrame && readBuffer_[0] != 0) { ++invalidFrames_; return true; }
            offset = 1;
        }
        const auto frame = decodeLogicalFrame(bound_, readBuffer_.data() + offset, bytes - offset);
        if (!frame) { ++invalidFrames_; return true; }
        ++validFrames_;
        if (frame->kind == FrameKind::Status) {
            settleInitialization(now); // A status read may precede advance on the exact final-page due tick.
            if (frame->status == 0x20 && initialized_ && !reinitDue_ && reinitializations_ < maxReinitializations) {
                const auto earliest = hasReinitialized_ ? add(lastReinitAt_, reinitIntervalMs) : now;
                reinitDue_ = now > earliest ? add(now, statusDelayMs) : add(earliest, statusDelayMs);
            }
            return true;
        }
        if (frame->button == Button::None) return true;
        if (clickOnly(frame->button)) {
            if (frame->pressed) push({EventKind::Click, frame->button, rearBits_, generation_, now, context_, frame->buttonId}, now);
            return true;
        }
        const std::uint8_t bit = frame->button == Button::L4 ? 1 : 2;
        const std::uint8_t next = frame->pressed ? static_cast<std::uint8_t>(rearBits_ | bit)
                                               : static_cast<std::uint8_t>(rearBits_ & static_cast<std::uint8_t>(~bit));
        if (next != rearBits_) { rearBits_ = next; push({EventKind::RearChanged, frame->button, rearBits_, generation_, now, context_, frame->buttonId}, now); }
        else if (!frame->pressed) push({EventKind::RearObserved, frame->button, rearBits_, generation_, now, context_, frame->buttonId}, now); // Actual UP may release a retained cross-generation hold fence.
        return true;
    }
    std::optional<Operation> beginWrite(std::uint64_t now) noexcept {
        advance(now);
        if (phase_ != Phase::Live || write_ || initialized_ || now < initDue_) return std::nullopt;
        const auto step = initStep(bound_, initIndex_);
        if (!step) return std::nullopt;
        const auto report = encodeReport(*step, contract_.outputBytes);
        if (!report) { retire(StopReason::WriteFailed, now); return std::nullopt; }
        writeBuffer_ = *report; writeDelayAfter_ = step->delayAfterMs; writeStartedAt_ = now;
        write_ = Operation{generation_, ++sequence_}; return write_;
    }
    const std::uint8_t* writeBuffer() const noexcept { return writeBuffer_.bytes.data(); }
    std::size_t writeSize() const noexcept { return writeBuffer_.size; }
    bool completeWrite(Operation operation, bool success, std::size_t bytes, std::uint64_t now) noexcept {
        if (!write_ || !(*write_ == operation)) return false;
        write_.reset();
        if (phase_ != Phase::Live || operation.generation != generation_) { finishRetirement(); return true; }
        if (!success || bytes != writeBuffer_.size) { retire(StopReason::WriteFailed, now); return true; }
        ++initIndex_; initDue_ = add(now, writeDelayAfter_); return true;
    }
    // Only the transport may withdraw a prepared ticket before it calls the OS.
    // Never use this for an operation already submitted to the kernel.
    bool discardPreparedWrite(Operation operation) noexcept {
        if (!write_ || !(*write_ == operation) || phase_ != Phase::Live || operation.generation != generation_) return false;
        write_.reset(); return true;
    }
    Cancellation takeCancellation() noexcept {
        Cancellation result;
        if (phase_ == Phase::Retaining) {
            result.read = read_.has_value() && !cancelledRead_;
            result.write = write_.has_value() && !cancelledWrite_;
            cancelledRead_ = cancelledRead_ || result.read; cancelledWrite_ = cancelledWrite_ || result.write;
        }
        return result;
    }
    std::optional<Event> popEvent() noexcept {
        if (phase_ != Phase::Live || !eventsCount_) return std::nullopt;
        const auto event = events_[eventsHead_]; eventsHead_ = (eventsHead_ + 1) % eventCapacity; --eventsCount_;
        return event;
    }
    Snapshot snapshot() const noexcept {
        return {requested_, bound_, context_, phase_, reason_, generation_, publicationRevision_, retryAt_,
            read_.has_value(), write_.has_value(), retained_, initialized_, initIndex_, eventsCount_, reinitializations_, rearBits_, validFrames_, invalidFrames_, reportIdObserved_, lastReportId_, contract_.inputLayout, contract_.inputBytes, contract_.outputBytes};
    }
private:
    void settleInitialization(std::uint64_t now) noexcept {
        if (phase_ == Phase::Live && !write_ && !initialized_ && now >= initDue_ && !initStep(bound_, initIndex_)) initialized_ = true;
    }
    static std::uint64_t add(std::uint64_t first, std::uint64_t second) noexcept {
        return first > UINT64_MAX - second ? UINT64_MAX : first + second;
    }
    void retire(StopReason reason, std::uint64_t now) noexcept {
        if (phase_ == Phase::Retaining) return; // Observation failures cannot restart the same cancellation deadline.
        if (phase_ == Phase::Detached && !read_ && !write_) return;
        ++generation_; ++publicationRevision_; reason_ = reason;
        phase_ = Phase::Retaining; retiredAt_ = now; retryAt_ = add(now, bindRetryMs);
        initialized_ = false; rearBits_ = 0; eventsHead_ = eventsCount_ = 0; reinitDue_.reset();
        finishRetirement();
    }
    void finishRetirement() noexcept {
        if (phase_ != Phase::Retaining || read_ || write_) return;
        phase_ = Phase::Detached; bound_ = Model::Unsupported;
        cancelledRead_ = cancelledWrite_ = retained_ = false;
    }
    void push(Event event, std::uint64_t now) noexcept {
        if (eventsCount_ == eventCapacity) { retire(StopReason::QueueFull, now); return; }
        events_[(eventsHead_ + eventsCount_) % eventCapacity] = event; ++eventsCount_;
    }
    Model requested_ = Model::Unsupported, bound_ = Model::Unsupported;
    Context context_{};
    TransportContract contract_{};
    Phase phase_ = Phase::Detached;
    StopReason reason_ = StopReason::None;
    std::uint64_t generation_ = 0, sequence_ = 0, publicationRevision_ = 0;
    std::uint64_t retryAt_ = 0, retiredAt_ = 0, initDue_ = 0, writeStartedAt_ = 0;
    std::uint64_t lastReinitAt_ = 0, validFrames_ = 0, invalidFrames_ = 0;
    std::optional<Operation> read_, write_;
    std::optional<std::uint64_t> reinitDue_;
    bool cancelledRead_ = false, cancelledWrite_ = false, retained_ = false;
    bool initialized_ = false, hasReinitialized_ = false, reportIdObserved_ = false;
    std::uint8_t lastReportId_ = 0;
    std::size_t initIndex_ = 0;
    unsigned writeDelayAfter_ = 0, reinitializations_ = 0;
    std::uint8_t rearBits_ = 0;
    std::array<std::uint8_t, maxInputBytes> readBuffer_{};
    EncodedReport writeBuffer_{};
    std::array<Event, eventCapacity> events_{};
    std::size_t eventsHead_ = 0, eventsCount_ = 0;
};
} // namespace ymcc::onex_x2
