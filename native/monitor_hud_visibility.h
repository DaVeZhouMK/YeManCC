#pragma once
// Presentation only: consume the resident YMCC game policy AFTER its existing
// exit/switch protection, through the existing sequenced/leased HUD frame.
// No recognition, process checks, timers or independent exit delay.
#include "json.hpp"
#include <charconv>
#include <cstdint>
#include <string_view>
namespace ymcc::hudvisibility {
enum class Mode { Always, GameRunning, GameFocused };
inline Mode mode(std::string_view value) noexcept {
    return value=="game-running"?Mode::GameRunning:value=="game-focused"?Mode::GameFocused:Mode::Always;
}
inline std::uint32_t policyGamePid(const nlohmann::json& frame) {
    const auto game=frame.find("policyGame");
    if(game==frame.end()||!game->is_object())return 0;
    const auto pid=game->find("pid"),created=game->find("processCreated");
    if(pid==game->end()||!pid->is_number_integer()||created==game->end()||!created->is_string())return 0;
    const auto value=pid->get<std::uint64_t>();
    if(!value||value>UINT32_MAX)return 0;
    const auto& birth=created->get_ref<const std::string&>();
    std::uint64_t parsed=0;
    const auto result=std::from_chars(birth.data(),birth.data()+birth.size(),parsed);
    return result.ec==std::errc{}&&result.ptr==birth.data()+birth.size()&&parsed?static_cast<std::uint32_t>(value):0;
}
inline bool permits(Mode policy,std::uint32_t gamePid,std::uint32_t foregroundPid) noexcept {
    if(policy==Mode::Always)return true;
    return gamePid!=0&&(policy==Mode::GameRunning||foregroundPid==gamePid);
}
}
