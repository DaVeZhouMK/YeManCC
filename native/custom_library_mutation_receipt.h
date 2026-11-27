#pragma once

#include "json.hpp"
#include <cstdint>
#include <mutex>
#include <utility>

namespace ymcc::customLibrary {
// One Native-owned slot for the main renderer's serialized mutations. Tickets
// never repeat within the process, including across full document reloads.
class MutationReceipt {
public:
    enum class Kind { enable, disable, launch };
    struct Document {
        std::uint64_t view, navigation;
        bool operator==(const Document&) const = default;
        explicit operator bool() const { return view && navigation; }
    };
private:
    using Json = nlohmann::json;
    enum class State { empty, reserved, queued, running, completed, retired };
    std::mutex mutex_;
    std::uint64_t ticket_=0, deadline_=0;
    State state_=State::empty;
    Document document_{};
    Kind kind_=Kind::enable;
    Json outcome_;

    void expire(std::uint64_t now) {
        if ((state_==State::reserved || state_==State::queued) && now>=deadline_)
            state_=State::retired;
    }
public:
    void documentChanged(Document document) {
        std::lock_guard lock(mutex_);
        if (document!=document_ && (state_==State::reserved || state_==State::queued)) state_=State::retired;
    }
    std::uint64_t reserve(Document document, Kind kind, std::uint64_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if (!document || state_==State::queued || state_==State::running || ticket_>=9007199254740991ULL) return 0;
        document_=document;
        kind_=kind;
        state_=State::reserved;
        deadline_=now+8000;
        outcome_=nullptr;
        return ++ticket_;
    }
    bool queue(Document document, Kind kind, std::uint64_t ticket, std::uint64_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if (!ticket || ticket!=ticket_ || document!=document_ || kind!=kind_ || state_!=State::reserved) return false;
        state_=State::queued;
        return true;
    }
    bool start(std::uint64_t ticket, std::uint64_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if (!ticket || ticket!=ticket_ || state_!=State::queued) return false;
        state_=State::running;
        return true;
    }
    bool finish(std::uint64_t ticket, const Json& outcome, bool rejectedBeforeStart=false) {
        std::lock_guard lock(mutex_);
        if (!ticket || ticket!=ticket_ || state_!=(rejectedBeforeStart?State::queued:State::running)) return false;
        outcome_=outcome;
        state_=State::completed;
        return true;
    }
    Json settle(Document document, std::uint64_t ticket, std::uint64_t now) {
        std::lock_guard lock(mutex_);
        expire(now);
        if (!ticket || ticket!=ticket_ || document!=document_) return {{"ticket",ticket},{"state","unknown"}};
        // The same lock arbitrates worker start. Once retired, this ticket can
        // never execute, including if its original message arrives after this.
        if (state_==State::reserved || state_==State::queued) state_=State::retired;
        if (state_==State::running) return {{"ticket",ticket},{"state","running"}};
        if (state_==State::retired) return {{"ticket",ticket},{"state","retired"}};
        return {{"ticket",ticket},{"state","completed"},{"outcome",outcome_}};
    }
};
}
