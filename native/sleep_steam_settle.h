#pragma once
#include "sleep_steam_input_context.h"
namespace ymcc::sleepsteam {
// Device enumeration and Steam's focus cache settle independently of power Ready.
// A stable native context is still NOT proof the game consumed controller input.
inline constexpr std::uint64_t kWakeObserveMinimumMs=30000,kWakeObserveLimitMs=60000,
    kWakeStableMs=9000,kWakePollMs=1500;
// Stable at thirty seconds is provisional: keep the original-instance owner
// alive through the last poll interval of the existing sixty-second budget.
// Write permission still expires at kWakeObserveLimitMs.
inline constexpr std::uint64_t kWakeFinalizeMinimumMs=kWakeObserveLimitMs-kWakePollMs;
static_assert(kWakeObserveMinimumMs<=kWakeFinalizeMinimumMs&&kWakeFinalizeMinimumMs<kWakeObserveLimitMs);
inline Json continueVerdict(const Json& steam,const Json& association) {
    Json result={{"scope","steam-continue"},{"status","unknown"},{"reason","steam-continue-state-unavailable"},
        {"candidate",false},{"continueButtonVerified",false},{"displayMutated",false}};
    if(!flag(association,"associationVerified")) {result["reason"]="steam-original-game-association-not-current";return result;}
    const auto rows=steam.find("overviews");if(rows==steam.end()||!rows->is_array())return result;
    for(const auto& row:*rows)if(number(row,"appid")==number(association,"appid")) {
        result["localDisplayStatus"]=row.value("localDisplayStatus",Json());
        result["selectedDisplayStatus"]=row.value("selectedDisplayStatus",Json());
        if(!row.contains("selectedDisplayStatus")||!row["selectedDisplayStatus"].is_number_integer())return result;
        if(number(row,"localDisplayStatus")!=4){result["status"]="waiting";result["reason"]="steam-original-game-not-yet-running";return result;}
        if(number(row,"selectedDisplayStatus")!=4){result["status"]="waiting";result["reason"]="steam-selected-client-not-running";return result;}
        result["status"]="runtime-running-unverified";result["reason"]="steam-running-state-observed-button-not-tested";
        result["candidate"]=flag(row,"inRunningList");return result;
    }
    return result;
}
inline std::string settleReason(const Json& report) {
    if(!flag(report.value("game",Json::object()),"identityMatches"))return "original-game-identity-unavailable";
    if(!flag(report.value("suspendCapture",Json::object()),"available"))return "steam-original-target-not-captured-at-suspend";
    if(report.contains("steamSessionState")&&!flag(report["steamSessionState"],"ready"))return report["steamSessionState"].value("reason",std::string("steam-session-not-ready"));
    if(flag(report.value("gamePause",Json::object()),"paused"))return "steam-original-game-paused";
    if(!flag(report.value("steamRecovery",Json::object()),"associationVerified"))return "steam-original-game-association-not-current";
    const auto steam=report.value("steam",Json::object()),context=steam.value("inputContext",Json::object());
    const auto controllers=context.find("controllers");
    if(!flag(context,"controllerListValid")||controllers==context.end()||!controllers->is_array()||controllers->empty())return "steam-controller-reconnect-not-observed";
    const auto focus=context.value("focus",Json::object());
    if(!focus.contains("overlayAppid")||!focus["overlayAppid"].is_number_integer()||focus["overlayAppid"]!=Json(0))return "steam-overlay-focus-active-or-unknown";
    const auto interactionBlock=inputInteractionBlock(context,static_cast<std::uint32_t>(number(report.value("tracker",Json::object()),"appid")));
    if(!interactionBlock.empty())return interactionBlock;
    if(!flag(report.value("continueRecovery",Json::object()),"candidate"))return "steam-continue-state-not-current";
    if(!flag(report.value("steamInputRecovery",Json::object()),"contextMatched"))return "steam-input-context-not-retained";
    if(report.value("foreground",Json::object()).value("role",std::string())!="original-game")return "steam-original-window-not-focused";
    return {};
}
inline Json clientBindingStamp(const Json& client) {
    if(!client.is_object())return Json();
    return {{"pid",client.value("pid",Json())},{"created",client.value("created",Json())},{"identityToken",client.value("identityToken",Json())},
        {"processVerified",client.value("processVerified",Json())},{"accountKnown",client.value("accountKnown",Json())}};
}
struct SettleWindow {
    std::uint64_t stableSince=0,lastElapsed=0;unsigned samples=0;
    bool ready=false;Json stamp;std::string reason="steam-wake-observation-pending";
    bool observe(const Json& report,std::uint64_t elapsed) {
        ++samples;reason=settleReason(report);
        const auto context=report.value("steam",Json::object()).value("inputContext",Json::object());
        const Json next={{"game",report.value("game",Json::object())},{"client",clientBindingStamp(report.value("steamClient",Json::object()))},
            {"controllers",context.value("controllers",Json())},{"configs",context.value("configQueries",Json())},
            {"focus",context.value("focus",Json::object())},{"continue",report.value("continueRecovery",Json::object())}};
        const bool good=reason.empty();
        if(!good||!ready||next!=stamp||elapsed<lastElapsed)stableSince=elapsed;
        stamp=next;ready=good;lastElapsed=elapsed;
        return good&&samples>=3&&elapsed>=kWakeObserveMinimumMs&&elapsed>=stableSince&&elapsed-stableSince>=kWakeStableMs;
    }
    Json summary() const {return {{"minimumObserveMs",kWakeObserveMinimumMs},{"maxObserveMs",kWakeObserveLimitMs},
        {"minimumFinalizeMs",kWakeFinalizeMinimumMs},{"stableWindowMs",kWakeStableMs},{"pollMs",kWakePollMs},{"samples",samples},{"lastElapsedMs",lastElapsed},
        {"stableSinceMs",stableSince},{"contextReady",ready},{"pendingReason",reason},
        {"gameInputVerified",false},{"continueButtonVerified",false}};}
};
}
