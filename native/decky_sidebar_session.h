#pragma once
// Decky-only account provenance. Registry zero is not an anonymous account:
// a verified current UI read supplies a nonzero account which the binding child
// checks again before injecting credentials. Never infer from loginusers/userdata.
#include "steam_session_observer.h"
#include "json.hpp"
namespace ymcc::deckysession {
using Json=nlohmann::json;
struct Account { DWORD value=0; bool fromUi=false; };
inline DWORD positiveAccount(const Json& v) {
    if(!v.is_number_integer())return 0;
    if(v.is_number_unsigned()){const auto n=v.get<uint64_t>();return n>0&&n<=MAXDWORD?static_cast<DWORD>(n):0;}
    const auto n=v.get<int64_t>();return n>0&&n<=MAXDWORD?static_cast<DWORD>(n):0;
}
inline Account resolve(const steamsession::Session& session,const Json& mode) {
    if(!session.pid||!session.created||session.root.empty())return {};
    // Existing registry identity remains the default, with no new online gate.
    if(session.account)return {session.account,false};
    try {
        if(!mode.is_object()||mode.value("known",false)!=true||mode.value("running",false)!=true||
           positiveAccount(mode.at("pid"))!=session.pid||mode.value("birth",std::string{})!=std::to_string(session.created)||
           mode.value("deckyAccountWatchAvailable",false)!=true)return {};
        const auto value=positiveAccount(mode.at("observedAccount"));
        if(!value)return {};
        return {value,true};
    }catch(...){return {};}
}
inline bool bigPicture(const steamsession::Session& session,const Json& mode) {
    try{const auto account=resolve(session,mode);const auto observed=positiveAccount(mode.value("observedAccount",Json()));
        return account.value&&(!observed||observed==account.value)&&mode.value("known",false)&&mode.value("running",false)&&
        positiveAccount(mode.at("pid"))==session.pid&&mode.value("birth",std::string{})==std::to_string(session.created)&&
        mode.value("mode",std::string{})=="bigpicture";}
    catch(...){return false;}
}
inline bool registryCompatible(DWORD observed,DWORD expected,bool uiProof) {
    return expected&&(observed==expected||(uiProof&&observed==0));
}
} // namespace ymcc::deckysession
