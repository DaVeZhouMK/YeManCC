#pragma once

#include "json.hpp"
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <utility>

namespace ymcc::gameResume {
class Receipts {
    using Json = nlohmann::json;
    enum class State { queued, running, completed };
    struct Entry {
        std::string id;
        std::uint64_t token, deadline;
        State state = State::queued;
        Json outcome;
    };
    std::mutex gate_;
    std::list<Entry> entries_;
    std::uint64_t next_ = 0;

    void expire(std::uint64_t now) {
        for (auto& row : entries_) {
            if (row.state == State::queued && now >= row.deadline) {
                row.outcome = {{"error", "game resume expired before execution"}};
                row.state = State::completed;
            }
        }
    }
public:
    static constexpr std::size_t capacity = 16;
    static bool valid(const std::string& id) {
        if (id.empty() || id.size() > 96) return false;
        for (const unsigned char c : id)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        return true;
    }
    std::uint64_t begin(const std::string& id, std::uint64_t now, std::uint64_t deadline) {
        if (!valid(id) || deadline <= now) return 0;
        std::lock_guard lock(gate_);
        expire(now);
        for (const auto& row : entries_)
            if (row.id == id || row.state != State::completed) return 0;
        if (entries_.size() == capacity) entries_.pop_front();
        entries_.push_back({id, ++next_, deadline, State::queued, {}});
        return next_;
    }
    bool start(std::uint64_t token, std::uint64_t now) {
        std::lock_guard lock(gate_);
        expire(now);
        for (auto& row : entries_) if (row.token == token && row.state == State::queued) {
            row.state = State::running;
            return true;
        }
        return false;
    }
    bool finish(std::uint64_t token, Json outcome, bool rejectedBeforeStart = false) {
        std::lock_guard lock(gate_);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->token != token || it->state != (rejectedBeforeStart ? State::queued : State::running)) continue;
            it->outcome = std::move(outcome);
            it->state = State::completed;
            entries_.splice(entries_.end(), entries_, it);
            return true;
        }
        return false;
    }
    Json read(const std::string& id, std::uint64_t now) {
        if (!valid(id)) return {{"requestId", ""}, {"state", "unknown"}};
        std::lock_guard lock(gate_);
        expire(now);
        for (const auto& row : entries_) if (row.id == id) {
            if (row.state == State::completed) {
                Json result = row.outcome;
                result["requestId"] = id;
                result["state"] = "completed";
                return result;
            }
            return {{"requestId", id}, {"state", row.state == State::queued ? "queued" : "running"}};
        }
        return {{"requestId", id}, {"state", "unknown"}};
    }
};
}
