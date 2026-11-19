#pragma once
// Pure bounded facts from an existing failure receipt. No discovery, IO,
// retry, action authority, or arbitrary remote text enters this projection.
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ymcc::steamfailure {
using Json = nlohmann::json;
inline constexpr std::size_t textLimit = 1024, signatureLimit = 6, recordLimit = 2048;
enum class Phase { Selected, Started, Finished, Rejected, Observation };
enum class Operation { Observe, Reconnect, RecycleLoader, RebuildDocument, ReopenBigPicture, Binding, ModeRead };
enum class Domain { Unknown, WinHttp, Http, CdpProtocol, JavaScript, WebSocketClose };
enum class SourceStage { Discovery, Upgrade, Send, Receive, Evaluate };
struct FailureContext {
    std::uint64_t actionId=0, recoveryEpisode=0, generation=0, powerGeneration=0, steamBirth=0;
    std::uint32_t steamPid=0;
    Phase phase=Phase::Observation;
    Operation operation=Operation::Observe;
};
inline const char* name(Phase v) {
    switch(v){case Phase::Selected:return "selected";case Phase::Started:return "started";
        case Phase::Finished:return "finished";case Phase::Rejected:return "rejected";default:return "observation";}
}
inline const char* name(Operation v) {
    switch(v){case Operation::Reconnect:return "reconnect";case Operation::RecycleLoader:return "recycle-owned-loader";
        case Operation::RebuildDocument:return "rebuild-ui-document";case Operation::ReopenBigPicture:return "reopen-bigpicture";
        case Operation::Binding:return "binding";case Operation::ModeRead:return "mode-read";default:return "observe";}
}
inline const char* name(Domain v) {
    switch(v){case Domain::WinHttp:return "winhttp";case Domain::Http:return "http";case Domain::CdpProtocol:return "cdp-protocol";
        case Domain::JavaScript:return "javascript";case Domain::WebSocketClose:return "websocket-close";default:return "unknown";}
}
inline const char* name(SourceStage v) {
    switch(v){case SourceStage::Discovery:return "discovery";case SourceStage::Upgrade:return "upgrade";
        case SourceStage::Send:return "send";case SourceStage::Receive:return "receive";default:return "evaluate";}
}
namespace detail {
inline const Json* field(const Json& value,const char* key) {
    if(!value.is_object())return nullptr;const auto it=value.find(key);return it==value.end()?nullptr:&*it;
}
inline std::string_view text(const Json* value) {
    return value&&value->is_string()?std::string_view(value->get_ref<const std::string&>()):std::string_view{};
}
inline bool flag(const Json* value) { return value&&value->is_boolean()&&value->get<bool>(); }
inline bool number(const Json* value,std::int64_t low,std::uint64_t high,std::int64_t& result) {
    if(!value||!value->is_number_integer())return false;
    if(value->is_number_unsigned()){
        const auto n=value->get<std::uint64_t>();if(n>high||n>std::uint64_t(INT64_MAX))return false;
        result=static_cast<std::int64_t>(n);return result>=low;
    }
    result=value->get<std::int64_t>();return result>=low&&(result<0||std::uint64_t(result)<=high);
}
inline constexpr std::array<std::string_view,9> exceptionClasses{
    "Error","TypeError","ReferenceError","SyntaxError","RangeError","EvalError","URIError","AggregateError","DOMException"};
struct Signature { std::string_view needle, value; };
inline constexpr std::array<Signature,13> signatures{{
    {"is not a function","not-a-function"},{"cannot read properties of undefined","undefined-property"},
    {"cannot read properties of null","null-property"},{"is not defined","undefined-symbol"},
    {"execution context was destroyed","context-destroyed"},{"cannot find context","context-missing"},
    {"target closed","target-closed"},{"connection closed","connection-closed"},
    {"timed out","timeout"},{"timeout","timeout"},{"not supported","unsupported"},
    {"access denied","access-denied"},{"connection refused","connection-refused"}
}};
inline Json empty(Domain domain,SourceStage stage) {
    return {{"schema",1},{"domain",name(domain)},{"sourceStage",name(stage)},
        {"codeKnown",false},{"exceptionClassKnown",false},{"lineKnown",false},{"columnKnown",false},
        {"signatures",Json::array()},{"textInspectedBytes",std::uint64_t{0}},{"textTruncated",false}};
}
inline void inspect(Json& result,const Json* input) {
    if(!input||!input->is_string())return;
    const auto value=text(input);const auto count=(std::min)(value.size(),textLimit);
    result["textInspectedBytes"]=result["textInspectedBytes"].get<std::uint64_t>()+count;
    if(value.size()>count)result["textTruncated"]=true;
    std::array<char,textLimit> lower{};
    for(std::size_t i=0;i<count;++i){const auto c=static_cast<unsigned char>(value[i]);lower[i]=c>='A'&&c<='Z'?char(c+('a'-'A')):char(c);}
    const std::string_view bounded(lower.data(),count);
    for(const auto& signature:signatures){
        auto& found=result["signatures"];
        if(found.size()>=signatureLimit)break;
        if(bounded.find(signature.needle)!=std::string_view::npos&&
            std::find(found.begin(),found.end(),Json(signature.value))==found.end())found.push_back(signature.value);
    }
}
} // namespace detail
inline Json native(Domain domain,std::uint32_t code,SourceStage stage) {
    auto result=detail::empty(domain,stage);result["codeKnown"]=true;result["code"]=code;return result;
}
inline Json cdp(const Json& packet) {
    using namespace detail;
    const auto* error=field(packet,"error");
    const auto* exception=field(packet,"result");exception=exception?field(*exception,"exceptionDetails"):nullptr;
    auto result=empty(error?Domain::CdpProtocol:exception?Domain::JavaScript:Domain::Unknown,SourceStage::Evaluate);
    if(error){
        std::int64_t code=0;if(number(field(*error,"code"),INT32_MIN,INT32_MAX,code)){result["codeKnown"]=true;result["code"]=code;}
        inspect(result,field(*error,"message"));
    }
    if(exception){
        const auto* object=field(*exception,"exception");
        const auto type=object?text(field(*object,"className")):std::string_view{};
        if(std::find(exceptionClasses.begin(),exceptionClasses.end(),type)!=exceptionClasses.end()){
            result["exceptionClassKnown"]=true;result["exceptionClass"]=type;
        }
        std::int64_t position=0;
        for(const auto* key:{"lineNumber","columnNumber"})if(number(field(*exception,key),0,INT32_MAX,position)){
            const bool line=key[0]=='l';result[line?"lineKnown":"columnKnown"]=true;result[line?"line":"column"]=position;
        }
        inspect(result,field(*exception,"text"));
        if(object)inspect(result,field(*object,"description"));
    }
    return result;
}
// Validate again at the consumer boundary. A child cannot smuggle arbitrary
// strings, a stack trace, or an unbounded collection through steamFailure.
inline Json normalize(const Json& input) {
    using namespace detail;
    Domain domain=Domain::Unknown;SourceStage stage=SourceStage::Evaluate;
    for(auto value:{Domain::WinHttp,Domain::Http,Domain::CdpProtocol,Domain::JavaScript,Domain::WebSocketClose})
        if(text(field(input,"domain"))==name(value))domain=value;
    for(auto value:{SourceStage::Discovery,SourceStage::Upgrade,SourceStage::Send,SourceStage::Receive})
        if(text(field(input,"sourceStage"))==name(value))stage=value;
    auto result=empty(domain,stage);
    if(!input.is_object()||!input.contains("schema")||input["schema"]!=1)return result;
    std::int64_t n=0;
    if(domain!=Domain::Unknown&&flag(field(input,"codeKnown"))&&number(field(input,"code"),
        domain==Domain::CdpProtocol?INT32_MIN:0,domain==Domain::CdpProtocol?INT32_MAX:UINT32_MAX,n)){
        result["codeKnown"]=true;result["code"]=n;
    }
    const auto type=text(field(input,"exceptionClass"));
    if(flag(field(input,"exceptionClassKnown"))&&std::find(exceptionClasses.begin(),exceptionClasses.end(),type)!=exceptionClasses.end()){
        result["exceptionClassKnown"]=true;result["exceptionClass"]=type;
    }
    for(const auto* key:{"line","column"}){
        const bool line=key[0]=='l';const auto known=line?"lineKnown":"columnKnown";
        if(flag(field(input,known))&&number(field(input,key),0,INT32_MAX,n)){result[known]=true;result[key]=n;}
    }
    const auto* items=field(input,"signatures");
    if(items&&items->is_array())for(std::size_t i=0;i<(std::min)(items->size(),signatureLimit);++i){
        const auto candidate=text(&(*items)[i]);auto& selected=result["signatures"];
        if(std::any_of(signatures.begin(),signatures.end(),[&](const auto& s){return candidate==s.value;})&&
            std::find(selected.begin(),selected.end(),Json(candidate))==selected.end())selected.push_back(candidate);
    }
    if(number(field(input,"textInspectedBytes"),0,textLimit*3,n))result["textInspectedBytes"]=n;
    result["textTruncated"]=flag(field(input,"textTruncated"));return result;
}
inline Json project(const Json& receipt,const FailureContext& context) {
    Json result={{"schema",1},{"actionId",std::to_string(context.actionId)},{"recoveryEpisode",context.recoveryEpisode},
        {"generation",context.generation},{"nativePowerGeneration",context.powerGeneration},
        {"steamPid",context.steamPid},{"steamBirth",std::to_string(context.steamBirth)},
        {"actionPhase",name(context.phase)},{"actionOperation",name(context.operation)}};
    for(const auto* key:{"ok","mutated","uncertain","requestDispatched"}){
        const auto* value=detail::field(receipt,key);if(value&&value->is_boolean())result[key]=*value;
    }
    const auto* failure=detail::field(receipt,"steamFailure");
    result["steamFailure"]=failure?normalize(*failure):detail::empty(Domain::Unknown,SourceStage::Evaluate);
    return result; // Fixed keys and bounded scalars keep the complete object < recordLimit.
}
} // namespace ymcc::steamfailure
