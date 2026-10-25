#pragma once
#include "sleep_steam_tracking.h"
#include <set>
namespace ymcc::sleepsteam {
// Same positive guard for preflight WAIT, PID handoff and final observation.
// A clean loaded configuration for this AppID is not itself an open editor.
inline std::string inputInteractionBlock(const Json& context,std::uint32_t appid) {
    const auto editor=context.value("editor",Json::object());
    if(!editor.is_object()||!flag(editor,"available")||!flag(editor,"editingAvailable")||!editor.contains("editingActive")||!editor["editingActive"].is_boolean()||
        (flag(editor,"editingActive")&&number(editor,"editingAppid")!=appid)||editor.value("dirty",Json())!=Json(false)||editor.value("previewActive",Json())!=Json(false)||editor.value("updating",Json())!=Json(false))return "steam-input-editor-busy-or-unknown";
    const auto interactive=context.value("interactive",Json::object());
    if(!interactive.is_object()||!flag(interactive,"available")||!flag(interactive,"idle")||interactive.value("lockScreen",Json())!=Json(false)||interactive.value("keyboard",Json())!=Json(false)||
        interactive.value("overlayOrVrWindow",Json())!=Json(false)||interactive.value("menuOpen",Json())!=Json(0)||interactive.value("menuSuppressed",Json())!=Json(false))return "steam-interactive-ui-active-or-unknown";
    return {};
}
// Only the internal original-PID window handoff may consume this read-only
// positive guard. Neither UI focus nor this guard proves game input/Continue.
inline std::string pidWindowReturnBlock(const Json& steam,std::uint32_t appid) {
    if(!appid||!steam.is_object()||!flag(steam,"ok"))return "steam-pid-return-read-unavailable";
    const auto context=steam.value("inputContext",Json::object());if(!context.is_object())return "steam-pid-return-focus-unknown";
    const auto focus=context.value("focus",Json::object());if(!focus.is_object())return "steam-pid-return-focus-unknown";
    if(!flag(focus,"available")||!focus.contains("appid")||!focus["appid"].is_number_integer())return "steam-pid-return-focus-unknown";
    if(!focus.contains("overlayAppid")||!focus["overlayAppid"].is_number_integer()||focus["overlayAppid"]!=Json(0))return "steam-overlay-focus-active-or-unknown";
    const auto id=number(focus,"appid");if(focus["appid"]!=Json(id)||id>0xffffffffULL)return "steam-pid-return-focus-unknown";if(id!=0&&id!=769&&id!=413080&&id!=appid)return "steam-other-app-focused";
    const auto controllers=context.find("controllers");
    if(!flag(context,"controllerListValid")||controllers==context.end()||!controllers->is_array()||controllers->empty()||controllers->size()>16)return "steam-controller-not-observed";
    std::set<std::uint64_t> indexes;
    for(const auto& c:*controllers)if(!c.is_object()||!c.contains("index")||!c["index"].is_number_integer()||!c.contains("type")||!c["type"].is_number_integer()||
        c["index"]!=Json(number(c,"index"))||c["type"]!=Json(number(c,"type"))||number(c,"index")>=0xffffffffULL||number(c,"type")>0xffffffffULL||!indexes.insert(number(c,"index")).second)return "steam-controller-list-not-exact";
    return inputInteractionBlock(context,appid);
}
// A native focus/config observation is NOT evidence that the original game
// consumed a button. Keep the primary gameplay acceptance explicitly unproven.
inline Json inputVerdict(const Json& steam,const Binding& binding,const Json& association,
    bool sameProcess,bool capturedAtSuspend,bool foregroundAllowed,bool originalWindowFocused) {
    Json result={{"scope","steam-input"},{"status","unknown"},{"reason","steam-input-context-unavailable"},
        {"repairable",false},{"contextMatched",false},{"gameInputVerified",false},{"healthVerified",false}};
    if(!sameProcess||binding.status!="tracked"){result["reason"]="original-game-identity-unavailable";return result;}
    // The primary goal must not be gated by the secondary library button.
    // A bounded native RETURN (never launch/rescope) may use the exact captured
    // live process + tracker + unchanged Steam session when only local overview
    // state is non-running. This does NOT verify/restore Steam association.
    const bool associationCurrent=flag(association,"associationVerified");
    bool localReadyToLaunch=false,localContactReadyToLaunch=false,localRunningWithoutList=false,localCurrentRunning=false;
    const auto rows=steam.find("overviews");if(rows!=steam.end()&&rows->is_array())
        for(const auto& row:*rows)if(number(row,"appid")==binding.appid&&flag(row,"overviewAvailable")){
            if(number(row,"localDisplayStatus")==11&&!flag(row,"localRunning")){
                localReadyToLaunch=true;localContactReadyToLaunch=exactOverviewGameId(row,binding.appid);
            }
            if(number(row,"localDisplayStatus")==4&&flag(row,"localRunning")&&flag(row,"inRunningList"))localCurrentRunning=true;
            if(flag(row,"localRunning")&&(number(row,"localDisplayStatus")==4||number(row,"localDisplayStatus")==1)&&
                row.value("inRunningList",Json())==Json(false))localRunningWithoutList=true;
        }
    // Contact degradation describes the same non-running original. It cannot
    // revoke its existing captured-tracker RETURN merely by changing the label.
    // Require the exact overview GameID and positive contact proof for this case.
    const bool contactDegraded=localContactReadyToLaunch&&flag(association,"contactConnected")&&flag(association,"contactRepairable")&&
        association.value("reason",std::string())=="steam-running-consumer-missing";
    const bool trackedOriginal=capturedAtSuspend&&flag(steam,"ok")&&localReadyToLaunch&&
        (association.value("reason",std::string())=="steam-backend-not-running"||contactDegraded);
    const bool capturedLocalRunning=capturedAtSuspend&&flag(steam,"ok")&&localRunningWithoutList&&
        association.value("reason",std::string())=="steam-running-overview-missing-from-ui";
    result["returnBasis"]=associationCurrent?"current-association":trackedOriginal?"captured-original-tracker":capturedLocalRunning?"captured-original-local-running":"none";
    result["associationVerified"]=associationCurrent;
    if(!associationCurrent&&!trackedOriginal&&!capturedLocalRunning){result["status"]="blocked";result["reason"]="steam-original-game-association-not-current";return result;}
    const auto context=steam.find("inputContext");if(context==steam.end()||!context->is_object())return result;
    const auto focus=context->find("focus");if(focus==context->end()||!focus->is_object()||!flag(*focus,"available"))return result;
    const auto focusID=focus->find("appid");if(focusID==focus->end()||!focusID->is_number_integer()||*focusID!=Json(number(*focus,"appid")))return result;
    if(!flag(*context,"controllerListValid")||!context->contains("controllers")||!(*context)["controllers"].is_array()){
        result["status"]="waiting-controller";result["reason"]="steam-controller-not-observed";return result;}
    if((*context)["controllers"].empty()){
        // Window-only preparation while the exact original OS window is ALREADY
        // current and native association is still Running4. Never borrow this
        // exception to focus a different/paused game or claim controller health.
        const auto focused=number(*focus,"appid");
        if(capturedAtSuspend&&associationCurrent&&localCurrentRunning&&originalWindowFocused&&foregroundAllowed&&
            focus->value("overlayAppid",Json())==Json(0)&&(focused==binding.appid||focused==0||focused==769||focused==413080)&&
            inputInteractionBlock(*context,binding.appid).empty()){
            result["status"]="waiting-controller";result["reason"]="steam-controller-not-observed";result["windowPreparationReason"]="steam-original-window-reassert-before-controller-reconnect";
            result["requireOriginalWindowReturn"]=true;result["controllerReconnectPending"]=true;result["windowPreparationRepairable"]=true;return result;
        }
        result["status"]="waiting-controller";result["reason"]="steam-controller-not-observed";return result;
    }
    // Never consume the single RETURN merely to discover a known protected UI.
    // Re-observe within the wake budget; the actual mutation path rechecks it.
    const auto interactionBlock=inputInteractionBlock(*context,binding.appid);
    if(!interactionBlock.empty()){result["status"]="waiting-interaction";result["reason"]=interactionBlock;return result;}
    const bool cachedOriginal=number(*focus,"appid")==binding.appid;
    if(cachedOriginal&&originalWindowFocused){result["status"]="context-current";result["reason"]="steam-input-context-current-unverified";result["contextMatched"]=true;return result;}
    if(!capturedAtSuspend){result["status"]="blocked";result["reason"]="steam-original-target-not-captured-at-suspend";return result;}
    if(!foregroundAllowed){result["status"]="blocked";result["reason"]="steam-input-foreground-owned-by-user-or-unknown";return result;}
    const auto overlay=focus->find("overlayAppid");
    if(overlay==focus->end()||!overlay->is_number_integer()||*overlay!=Json(0)){result["status"]="blocked";result["reason"]="steam-overlay-focus-active-or-unknown";return result;}
    const auto focused=number(*focus,"appid");if(!cachedOriginal&&focused!=0&&focused!=769&&focused!=413080){result["status"]="blocked";result["reason"]="steam-other-app-focused";return result;}
    // Cached Steam focus can survive while Windows foreground belongs to Steam.
    // Only the native caller's verified original OS instance can satisfy this.
    result["requireOriginalWindowReturn"]=cachedOriginal&&!originalWindowFocused;
    result["status"]="stale-context";result["reason"]=cachedOriginal?"steam-cached-focus-original-window-not-focused":"steam-original-input-context-not-focused";
    result["repairable"]=true;return result;
}
}
