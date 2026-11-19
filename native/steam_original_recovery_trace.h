#pragma once
// Per-operation diagnostic projection only. It never publishes Steam/power facts
// or grants recovery; the caller supplies the unique Native generation at begin.
#include "../deps/json/json.hpp"
#include <cstdint>
namespace ymcc::steamOriginalTrace {
class CaptureBinding {
    using Json=nlohmann::json;
    std::uint64_t serial_=0,power_=0;
    static std::uint64_t serial(const Json& row) noexcept {
        try{if(!row.is_object()||row.value("captureTraceVersion",0)!=1||
            !row.contains("captureSerial")||!row.at("captureSerial").is_number_unsigned())return 0;
            return row.at("captureSerial").get<std::uint64_t>();}catch(...){return 0;}
    }
public:
    static bool isBegin(const Json& row) noexcept {
        try{return row.value("event",std::string{})=="source-capture-begin"&&serial(row)!=0;}catch(...){return false;}
    }
    void begin(const Json& row,std::uint64_t nativeGeneration) noexcept {
        if(!isBegin(row))return;
        serial_=serial(row);power_=nativeGeneration;
    }
    Json tag(const Json& row) const {
        auto value=row;const auto event=row.value("event",std::string{});
        if(event!="source-capture-begin"&&event!="source-capture-end")return value;
        const auto id=serial(row);const bool known=id!=0&&id==serial_;
        value["capturePowerKnown"]=known;
        value["capturePowerGeneration"]=known?Json(power_):Json(nullptr);
        value["capturePowerBasis"]=known?"native-generation-at-capture-begin":"unknown";
        return value;
    }
};
} // namespace ymcc::steamOriginalTrace
