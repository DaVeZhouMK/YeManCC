// One sparse boot identity query; local OS owner only, never game consumption.
#pragma once
#include <cstdint>
#include <string>
#include <json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
namespace ymcc::inputChainOwner {
using Json=nlohmann::json;
struct Raw { std::uint32_t pid=0; std::uint64_t creationFiletime=0; bool querySucceeded=false; };
inline Json project(const Raw& raw) noexcept {
    try {
        const bool observed=raw.querySucceeded&&raw.pid!=0&&raw.creationFiletime!=0;
        return Json{{"schema",1},{"authority","native-self-process-identity"},
            {"pid",raw.pid},{"creationFiletime",observed?Json(std::to_string(raw.creationFiletime)):Json(nullptr)},
            {"creationSource","GetProcessTimes-self"},{"querySucceeded",raw.querySucceeded},
            {"status",observed?"observed":"unknown"},{"readOnly",true},
            {"gameInputUnverified",true},{"steamConsumptionProven",false}};
    } catch(...) { return Json(nullptr); }
}
#ifdef _WIN32
inline Json readSelf() noexcept {
    FILETIME created{},exited{},kernel{},user{};ULARGE_INTEGER value{};
    const auto pid=GetCurrentProcessId();
    const bool ok=GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE;
    value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;
    return project({pid,value.QuadPart,ok});
}
#endif
}
