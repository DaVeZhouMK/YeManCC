#pragma once
#include <array>
#include <cstdint>
#include <string>

// Finite initialization of an already selected Legion vendor source. Immutable
// recipes come from the total capability library; Port owns asynchronous I/O.
namespace ymcc::legion_activation {
struct Key {
    std::uint64_t power = 0, raw = 0, owner = 0;
    std::uint16_t token = 0;
    std::string physicalIdentity;
    std::uint64_t captureEpoch = 0;
    bool operator==(const Key& b) const noexcept {
        return power == b.power && raw == b.raw && owner == b.owner && token == b.token &&
            physicalIdentity == b.physicalIdentity && captureEpoch == b.captureEpoch;
    }
    bool valid() const noexcept { return power && raw && owner && token && !physicalIdentity.empty() && captureEpoch; }
};
enum class Completion { Pending, Success, Failed };
enum class Phase { Idle, Writing, Complete, Failed, Retiring, Stopped };
struct Evidence {
    Phase phase = Phase::Idle;
    Key key;
    unsigned completedCommands = 0, commandCount = 0, outputReportBytes = 0;
    bool pending = false, resourceTerminal = true;
    std::uint64_t revision = 0;
};
// The existing capture owner serializes this session. No worker/timer, discovery,
// mode reset or unbounded retry is introduced. Each source owns its own slot.
template<class Port> class Session {
public:
    Port port;
    Evidence inspect() const { return {phase_, key_, index_, count_, reportBytes_, pending_, !opened_, revision_}; }
    void stop() {
        if (opened_) {
            if (phase_ != Phase::Retiring) { phase_ = Phase::Retiring; retirement_ = Phase::Stopped; ++revision_; }
            if (pending_ && !cancelled_) { cancelled_ = true; port.cancel(); }
            if (!pending_) finish(Phase::Stopped);
        }
        wanted_ = false;
    }
    // Poll only this slot. A cancelled or failed completion cannot authorize a
    // new command in the same call, even when a newer source is already wanted.
    bool drain(std::uint64_t now) {
        if (pending_) {
            if (!cancelled_ && now >= started_ && now - started_ >= 750) {
                phase_ = Phase::Retiring; retirement_ = Phase::Failed; cancelled_ = true; ++revision_; port.cancel();
            }
            const auto result = port.poll();
            if (result == Completion::Pending) return false;
            pending_ = false;
            if (cancelled_ || result == Completion::Failed) { finish(cancelled_ ? retirement_ : Phase::Failed); return false; }
            ++index_;
        }
        if (opened_ && phase_ == Phase::Retiring) { finish(retirement_); return false; }
        return true;
    }
    template<class Handle, class Recipe>
    void tick(bool wanted, const Key& key, Handle source, unsigned reportBytes,
              const Recipe& recipe, std::uint64_t now) {
        wanted = wanted && key.valid();
        const bool same = key_ == key;
        if (opened_ && (!wanted || !same)) stop();
        if (!drain(now)) return;
        if (!wanted) { wanted_ = false; return; }
        if (!opened_) {
            if (wanted_ && same) return;
            wanted_ = true; key_ = key; index_ = 0; count_ = recipe.count; reportBytes_ = reportBytes; cancelled_ = false;
            bool valid = count_ != 0 && count_ <= recipe.commands.size() &&
                recipe.minReportBytes != 0 && reportBytes >= recipe.minReportBytes && reportBytes <= recipe.maxReportBytes;
            for (unsigned i = 0; valid && i < count_; ++i)
                valid = recipe.commands[i].length != 0 && recipe.commands[i].length <= recipe.commands[i].bytes.size() &&
                    recipe.commands[i].length <= reportBytes;
            if (!valid) { phase_ = Phase::Failed; ++revision_; return; }
            if (!port.open(source, reportBytes)) { opened_ = true; finish(Phase::Failed); return; }
            opened_ = true; phase_ = Phase::Writing; ++revision_;
        }
        if (index_ == count_) { finish(Phase::Complete); return; }
        started_ = now;
        const auto result = port.submit(recipe.commands[index_]);
        if (result == Completion::Pending) pending_ = true;
        else if (result == Completion::Failed) finish(Phase::Failed);
        else { ++index_; if (index_ == count_) finish(Phase::Complete); }
    }
private:
    void finish(Phase phase) {
        if (opened_ && !port.release()) {
            if (phase_ != Phase::Retiring || retirement_ != phase) ++revision_;
            phase_ = Phase::Retiring; retirement_ = phase; return;
        }
        opened_ = pending_ = cancelled_ = false; phase_ = phase; ++revision_;
    }
    Key key_;
    Phase phase_ = Phase::Idle, retirement_ = Phase::Stopped;
    unsigned index_ = 0, count_ = 0, reportBytes_ = 0;
    bool opened_ = false, pending_ = false, cancelled_ = false, wanted_ = false;
    std::uint64_t started_ = 0, revision_ = 0;
};
} // namespace ymcc::legion_activation
