#pragma once
#include "json.hpp"
#include <cstdint>
#include <string_view>
#include <utility>

// A synchronous suffix of one already-qualified menu button. This helper has
// no process reader, retained ticket, queue, retry, timer or permission source.
namespace ymcc::menupopup {
using Json=nlohmann::json;
inline bool activationTicket(std::string_view value) noexcept {
    return !value.empty()&&value.size()<=96&&value.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-")==value.npos;
}
template<class Current,class Sender,class Clock>
inline Json continueAfterButton(const Json& request,Json initial,std::uint64_t started,
    Current&& current,Sender&& send,Clock&& now) {
    bool attempted=false,eligible=false;
    const auto unconfirmed=[&](const char* reason) noexcept {
        try {
            Json leaf={{"schema",1},{"scope","held-active-qam-popup-original-activation"},
                {"status","followup-unconfirmed"},{"reason",reason},{"activationPhase","native-followup"},
                {"callbackSubmitted",attempted?Json(nullptr):Json(false)},
                {"inputCapabilityGranted",false},{"physicalInputProven",false},{"followupAttempted",attempted}};
            initial["menuTarget"]["windowFocus"]["menuTreeActivation"]=std::move(leaf);
        }catch(...){} // Preserve the original delivered key even if reporting fails.
    };
    try {
        if(!request.is_object()||request.value("operation",Json())!=Json("menu.button")||
            request.value("menu",Json())!=Json("quick-access")||!request.contains("menuGame")||
            !request["menuGame"].is_object()||request["menuGame"].size()!=13||!initial.is_object()||
            initial.value("ok",Json())!=Json(true)||initial.value("keyDelivered",Json())!=Json(true)||
            initial.value("runtimeAccepted",Json())!=Json(true)||!initial.contains("before")||
            !initial["before"].is_number_integer()||(initial["before"]!=0&&initial["before"]!=1)||
            initial.value("menu",Json())!=Json(2))return initial;
        const auto target=initial.find("menuTarget");
        if(target==initial.end()||!target->is_object())return initial;
        const auto focus=target->find("windowFocus");
        if(focus==target->end()||!focus->is_object())return initial;
        const auto leaf=focus->find("menuTreeActivation");
        if(leaf==focus->end()||!leaf->is_object()||leaf->value("status",Json())!=Json("pending-browser-task"))return initial;
        const auto ticket=leaf->find("ticket");
        if(ticket==leaf->end()||!ticket->is_string()||!activationTicket(ticket->get_ref<const std::string&>()))return initial;
        eligible=true;
        // Count the first native send in this window. Neither a second CDP
        // request nor the JS ticket starts another four seconds here.
        const auto within=[&]() noexcept {try{const auto tick=static_cast<std::uint64_t>(now());return tick>=started&&tick-started<4000;}catch(...){return false;}};
        const auto admitted=[&]() noexcept {try{return within()&&current()&&within();}catch(...){return false;}};
        if(!admitted()){unconfirmed("native-continuation-not-current");return initial;}
        Json continuation={{"operation","menu.qam.observe"},{"menu","quick-access"},
            {"menuGame",request["menuGame"]},{"menuGamePopupActivation",*ticket}};
        attempted=true;
        const auto reply=send(continuation,admitted); // Exactly one existing-channel call; never a button.
        if(!admitted()){unconfirmed("native-continuation-owner-ended");return initial;}
        const auto value=reply.is_object()?reply.find("menuPopupActivation"):reply.end();
        if(!reply.is_object()||reply.value("ok",Json())!=Json(true)||value==reply.end()||!value->is_object()||
            value->size()>16||value->value("schema",Json())!=Json(1)||
            value->value("scope",Json())!=Json("held-active-qam-popup-original-activation")||
            !value->contains("status")||!(*value)["status"].is_string()||(*value)["status"].get_ref<const std::string&>().size()>96||
            !value->contains("callbackSubmitted")||!(*value)["callbackSubmitted"].is_boolean()||
            value->value("inputCapabilityGranted",Json())!=Json(false)||value->value("physicalInputProven",Json())!=Json(false)||
            value->dump().size()>4096){unconfirmed("native-continuation-reply-unconfirmed");return initial;}
        // Only the activation leaf changes. Original button result, delivery,
        // menu state, source diagnostics and uncertainty remain intact.
        initial["menuTarget"]["windowFocus"]["menuTreeActivation"]=*value;
        return initial;
    }catch(...){if(eligible)unconfirmed("native-continuation-unavailable");return initial;}
}
}
