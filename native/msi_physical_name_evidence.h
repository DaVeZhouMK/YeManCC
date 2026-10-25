#pragma once
// Pure bounded name/provenance formatting. No HID/USB/DirectInput calls or decisions.
#include <cstddef>
#include <cstdint>
#include <string>
#include "json.hpp"
namespace ymcc::msi::nameEvidence {
using Json=nlohmann::json;
inline Json utf16BufferWitness(const wchar_t* data,size_t capacity,const char* source) {
    Json units=Json::array();size_t length=0;bool terminated=false;
    if(data){for(;length<capacity;++length){const auto unit=static_cast<std::uint16_t>(data[length]);if(!unit){terminated=true;break;}units.push_back(unit);}}
    return {{"source",source},{"encoding","raw-UTF16-code-units-before-UTF8-conversion"},
        {"codeUnits",units},{"codeUnitsBeforeNul",length},{"bufferCapacityCodeUnits",capacity},
        {"terminatedWithinBuffer",data?Json(terminated):Json("not-observed")},
        {"bufferBoundary",!data?"not-observed":terminated?"nul-terminated":"no-nul-in-bounded-buffer"},
        {"firstCodeUnit",units.empty()?Json(nullptr):units[0]},
        {"descriptorOrLanguageInterpretation","not-observed-no-USB-descriptor-or-string-index-request"}};
}
inline Json apiWindow(const char* api,std::uint64_t begin,std::uint64_t end,bool known,
    bool success,std::int64_t result,const char* resultKind) {
    return {{"api",api},{"beginTick",begin},{"endTick",end},{"durationMs",end>=begin?end-begin:0ULL},
        {"attempted",known},{"success",known?Json(success):Json("not-observed")},
        {"resultKind",resultKind},{"resultCode",known?Json(result):Json("not-observed")},
        {"timestampSource","GetTickCount64-existing-call-boundary"},{"crossApiAtomicSnapshot",false}};
}
inline Json compareNames(const Json& dinputUnits,const Json& hidUnits,bool dinputKnown,bool hidKnown) {
    return {{"dinputAndHidRawComparison",!dinputKnown||!hidKnown?"not-observed":dinputUnits==hidUnits?"same-code-units":"different-code-units"},
        {"interpretation","comparison-only-not-a-driver-firmware-language-or-Steam-source-diagnosis"}};
}
inline Json logEmissionWitness(std::uint64_t emitted,const Json& captureEnd) {
    Json delay="not-observed";std::string status="missing-capture-end";
    const bool valid=captureEnd.is_number_unsigned() || (captureEnd.is_number_integer() && captureEnd.get<std::int64_t>()>=0);
    if(valid){const auto end=captureEnd.get<std::uint64_t>();
        if(emitted>=end){delay=emitted-end;status="observed-monotonic-delay";}else status="emission-before-capture-end";
    }else if(!captureEnd.is_null())status="invalid-capture-end";
    return {{"emittedTick",emitted},{"logEmissionDelayMs",delay},{"logEmissionDelayStatus",status},
        {"timeFieldMeaning","log-emission-local-time-not-atomic-API-snapshot"}};
}
} // namespace ymcc::msi::nameEvidence
