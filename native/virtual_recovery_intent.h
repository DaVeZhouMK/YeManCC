#pragma once
#include "game_input_override.h"
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace ymcc::inputHost {
// Recovery observes continued demand, not the publication rate of settings.
// The existing persona mutex serializes configure/invalidate. The original
// snapshot revision and active-writer fence still protect frames and receipts.
// No request, clock, device operation or retry allowance is created here.
class VirtualRecoveryIntent final {
    struct Key {
        bool enabled=false, target=false, dedicated=false;
        std::string persona, owner;
        nlohmann::json outputTarget, gyroMotion;
        bool operator==(const Key&) const = default;
    };
    Key key_;
    bool known_=false, exhausted_=false;
    std::atomic<std::uint64_t> revision_{0};
    void advance() noexcept {
        const auto previous=revision_.load(std::memory_order_relaxed);
        if(exhausted_ || previous==(std::numeric_limits<std::uint64_t>::max)()) {
            exhausted_=true;revision_.store(0,std::memory_order_release);return;
        }
        revision_.store(previous+1,std::memory_order_release);
    }
public:
    void configure(const nlohmann::json& effective,const std::string& session,
                   bool enabled,bool target,const std::string& persona) {
        const bool dedicated=ymcc::gameInputOverrideActive(effective,session);
        auto output=effective.value("outputTarget",nlohmann::json::object());
        auto motion=effective.value("gyroMotion",nlohmann::json::object());
        if(output.is_object())output.erase("revision");
        if(motion.is_object())motion.erase("revision");
        Key next{enabled,target,dedicated,persona,dedicated
            ? effective.at("gameOverride").at("identity").get<std::string>() : std::string{},
            std::move(output),std::move(motion)};
        if(known_ && next==key_)return;
        key_=std::move(next);known_=true;advance();
    }
    void invalidate() noexcept { known_=false;advance(); }
    std::uint64_t revision() const noexcept { return revision_.load(std::memory_order_acquire); }
};
}
