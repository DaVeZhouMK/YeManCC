#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "json.hpp"
namespace ymcc::secondarydisplay {
using Json=nlohmann::json;
inline std::string_view modelId(std::string_view name) noexcept {
    if(name=="AYANEOFlipDS"||name=="HandheldCompanion.Devices.AYANEOFlipDS")return "hc.device.AYANEOFlipDS";
    if(name=="AYANEOFlip1SDS"||name=="HandheldCompanion.Devices.AYANEOFlip1SDS"||name=="HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS")return "hc.device.AYANEOFlip1SDS";
    return {};
}
inline std::string_view capabilityId(std::string_view id) noexcept {
    return id=="hc.device.AYANEOFlipDS"?"model.secondary-display.ayaneo-flip-ds.ec-brightness":
        id=="hc.device.AYANEOFlip1SDS"?"model.secondary-display.ayaneo-flip1s-ds.ec-brightness":"";
}
inline bool validFence(const Json& args){
    if(!args.is_object()||!args.contains("fence")||!args["fence"].is_string())return false;
    const auto& value=args["fence"].get_ref<const std::string&>();if(value.size()!=64)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f')))return false;return true;
}
inline bool validRequest(const std::string& operation,const Json& args){
    if(!args.is_object())return false;
    if(operation=="get"||operation=="release")return true;
    if(!validFence(args))return false;
    if(operation=="toggle")return true;
    if(operation!="set"||!args.contains("target")||!args["target"].is_number_integer())return false;
    const auto& target=args["target"];
    if(target.is_number_unsigned())return target.get<std::uint64_t>()<=32767;
    const auto value=target.get<std::int64_t>();return value>=-32768&&value<=32767;
}
inline Json failure(const std::string& reason){return {{"schema",1},{"ok",false},{"supported",false},{"model",""},{"modelId",""},
    {"capabilityId",""},{"raw",nullptr},{"brightness",nullptr},{"enabled",nullptr},{"fence",""},{"verification","unknown"},{"reason",reason},{"stage","native"}};}
// This is the actual native consumer; transport is the existing version-pinned helper request.
template<class Transport,class Current> Json consume(const std::string& deviceClass,const std::string& operation,
    const Json& args,Transport&& transport,Current&& current){
    try {
        const auto model=modelId(deviceClass);if(model.empty())return failure("当前精确机型没有副屏能力");
        if(!validRequest(operation,args))return failure("副屏请求参数无效");
        if(!current())return failure("副屏来源或电源会话已失效");
        Json request{{"op","secondaryDisplay."+operation}};
        if(operation=="set")request["target"]=args["target"];
        if(operation=="set"||operation=="toggle")request["fence"]=args["fence"];
        auto result=transport(request);
        const auto rejected=[&](const std::string& reason){auto error=failure(reason);auto provider=result;if(provider.is_object())provider.erase("fence");error["providerResult"]=std::move(provider);return error;};
        if(!current())return rejected("副屏请求结束时来源或电源会话已变化");
        if(!result.is_object()||!result.contains("ok")||!result["ok"].is_boolean())return rejected("副屏回执格式无效");
        if(!result["ok"].get<bool>())return rejected(result.contains("reason")&&result["reason"].is_string()?result["reason"].get<std::string>():"副屏执行未确认");
        if(!result.contains("schema")||!result["schema"].is_number_integer()||result["schema"].get<std::int64_t>()!=1||!result.value("supported",false)||result.value("modelId",std::string())!=model||
            result.value("capabilityId",std::string())!=capabilityId(model))return rejected("副屏回执机型或能力不匹配");
        if(operation=="release"){
            if(result.value("verification",std::string())!="released-no-restore"||result.value("fence",std::string())!="")return rejected("副屏释放回执无效");
        }else{
            if(!result.contains("raw")||!result["raw"].is_number_integer())return rejected("副屏回执没有寄存器回读");
            const auto raw=result["raw"].get<std::int64_t>();if(raw<0||raw>255||result.value("verification",std::string())!="ec-register-readback")return rejected("副屏寄存器回读无效");
            if(!result.contains("brightness")||!result["brightness"].is_number_integer()||result["brightness"].get<std::int64_t>()!=(raw*100+127)/255||
                !result.contains("enabled")||!result["enabled"].is_boolean()||result["enabled"].get<bool>()!=(raw!=0))return rejected("副屏回执数值不一致");
            if(operation=="get"){if(!validFence(result))return rejected("副屏读取凭据无效");}
            else {
                if(result.value("stage",std::string())!="complete"||result.value("fence",std::string())!="")return rejected("副屏写入未完成");
                if(operation=="set"){
                    auto target=request["target"].get<std::int64_t>();target=target<0?0:target>100?100:target;
                    if(raw!=target*255/100)return rejected("副屏回读与请求亮度不一致");
                }
            }
        }
        return result;
    }catch(const std::exception& e){return failure(e.what());}catch(...){return failure("副屏请求未确认");}
}
// Requires a real neutral sample after model, power, source, mapping or recording changes.
struct KeyboardEdge {
    std::uint64_t power=0,source=0,rules=0,model=0;bool available=false,armed=false,held=false,seen=false;
    bool consume(bool down,bool eligible,std::uint64_t nextPower,std::uint64_t nextSource,std::uint64_t nextRules,std::uint64_t nextModel=0) noexcept {
        if(!seen||power!=nextPower||source!=nextSource||rules!=nextRules||model!=nextModel||available!=eligible){
            power=nextPower;source=nextSource;rules=nextRules;model=nextModel;available=eligible;armed=held=false;seen=true;
        }
        if(!eligible){armed=held=false;return false;}
        if(!down){armed=true;held=false;return false;}
        const bool emit=armed&&!held;held=true;return emit;
    }
};
}
