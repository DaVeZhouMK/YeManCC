#pragma once
#include "json.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
namespace ymcc::steamrecordidentity {
using Json=nlohmann::json;
struct Read {std::uint64_t account64=0,gameId64=0;std::uint32_t appid=0;bool available=false;std::string reason="native-record-ui-identity-unavailable";};
inline bool decimal64(const Json& value,std::uint64_t& out) {
    if(!value.is_string())return false;const auto& text=value.get_ref<const std::string&>();
    if(text.empty()||text.size()>20||text[0]=='0')return false;std::uint64_t n=0;
    for(unsigned char c:text){if(c<'0'||c>'9')return false;const auto d=std::uint64_t(c-'0');if(n>(std::numeric_limits<std::uint64_t>::max()-d)/10)return false;n=n*10+d;}
    if(!n)return false;out=n;return true;
}
inline bool appid32(const Json& value,std::uint32_t& out) {
    std::uint64_t n=0;if(value.is_number_unsigned())n=value.get<std::uint64_t>();
    else if(value.is_number_integer()){const auto x=value.get<std::int64_t>();if(x<=0)return false;n=static_cast<std::uint64_t>(x);}else return false;
    if(!n||n>0xffffffffULL)return false;out=static_cast<std::uint32_t>(n);return true;
}
// Remove the complete private account value before any reply can be logged or
// exported. Even malformed/unrequested private replies are stripped.
inline Read consume(Json& reply) {
    Read result;if(!reply.is_object())return result;
    auto it=reply.find("_ymccPrivateNativeRecord");if(it==reply.end())return result;
    Json privateValue=std::move(*it);reply.erase(it);
    if(!privateValue.is_object())return result;
    if(privateValue.value("status",Json())!="observed") {
        const auto reason=privateValue.value("reason",Json());
        if(reason.is_string()){
            const auto s=reason.get<std::string>();
            if(s=="native-record-ui-identity-schema-changed"||s=="native-record-ui-identity-raced"||s=="native-record-ui-game-unavailable"||s=="native-record-ui-account-unavailable")result.reason=s;
        }
        return result;
    }
    if(privateValue.value("source",Json())!="steam-ui-exact-native-record-identity")return result;
    const auto a=privateValue.find("steamid64"),g=privateValue.find("gameid64"),id=privateValue.find("appid");
    if(a==privateValue.end()||g==privateValue.end()||id==privateValue.end()||!decimal64(*a,result.account64)||!decimal64(*g,result.gameId64)||!appid32(*id,result.appid))return Read{};
    result.available=true;result.reason="native-record-ui-identity-observed";return result;
}
struct PrefetchWitness {std::uint64_t generation=0,epoch=0,observedTick=0,observedFileTime=0;};
// A diagnostic snapshot may only precede this capture; a wake read can never
// backfill the original. This is freshness evidence, not a login lease.
inline bool beforeCapture(const PrefetchWitness& w,std::uint64_t generation,std::uint64_t epoch,std::uint64_t captureTick,std::uint64_t captureFileTime) {
    return generation&&epoch&&w.generation==generation&&w.epoch==epoch&&w.observedTick&&w.observedFileTime&&captureTick&&captureFileTime&&
        w.observedTick<=captureTick&&captureTick-w.observedTick<=60000&&w.observedFileTime<=captureFileTime;
}
} // namespace ymcc::steamrecordidentity
