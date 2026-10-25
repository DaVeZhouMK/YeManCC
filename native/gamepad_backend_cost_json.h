#pragma once
#include "gamepad_backend_cost.h"
#include <utility>
namespace ymcc::input::backendCost {
template<class Json> Json snapshotJson(const Snapshot& s) {
    Json value = Json::object();
    value["n"] = s.n;
    value["sumUs"] = s.sumUs;
    value["maxUs"] = s.maxUs;
    value["meanUs"] = s.n ? Json(s.sumUs / s.n) : Json(nullptr);
    value["success"] = s.success;
    value["nonSuccess"] = s.nonSuccess;
    Json codes = Json::array();
    for (const auto& row : s.resultCodes) {
        if (!row.n) continue;
        Json code = Json::object();
        code["codeU32"] = row.code;
        code["n"] = row.n;
        codes.push_back(std::move(code));
    }
    value["resultCodes"] = std::move(codes);
    value["otherCodeCount"] = s.otherCodeCount;
    value["lastOtherCodeU32"] = s.otherCodeCount ? Json(s.lastOtherCode) : Json(nullptr);
    return value;
}
template<class Json> Json windowJson(Counters& counters) {
    Json value = Json::object();
    value["schema"] = "ymcc-gamepad-backend-cost-v1";
    value["measurement"] = "steady_clock elapsed around calls; not thread CPU or pure hardware I/O";
    value["windowSemantics"] = "enabled observations since preceding drain; gate gaps may exist; independent atomic counters may straddle emissions";
    value["resultCodeCapacityPerRoute"] = kResultCodeCapacity;
    value["resultCodeAssignment"] = "first eight distinct U32 results per route per process; assignments retained across drains; overflow counted with last observed sample";
    value["coverage"] = "gamepadReadState wrappers (before fault override), MSI Poll/GetDeviceState and System32 diagnostic; lifecycle target probes excluded";
    value["msiCanonicalSnapshot"] = "published atomic frame copy inside gpRead; no new device call and no separate duration here";
    static constexpr const char* sources[kSourceCount] = {"bus", "ui-wm-input", "ui-timer", "other"};
    static constexpr const char* purposes[kPurposeCount] = {"realtime", "discovery", "guard"};
    Json xinput = Json::object();
    for (std::size_t src = 0; src < kSourceCount; ++src) {
        Json byPurpose = Json::object();
        for (std::size_t p = 0; p < kPurposeCount; ++p) {
            Json apis = Json::object();
            apis["canonical"] = snapshotJson<Json>(counters.xinput[Counters::route(src, p, false)].drain());
            apis["ordinal100"] = snapshotJson<Json>(counters.xinput[Counters::route(src, p, true)].drain());
            byPurpose[purposes[p]] = std::move(apis);
        }
        xinput[sources[src]] = std::move(byPurpose);
    }
    value["xinputWrappers"] = std::move(xinput);
    value["system32Diagnostic"] = snapshotJson<Json>(counters.system32.drain());
    value["msiPoll"] = snapshotJson<Json>(counters.msiPoll.drain());
    value["msiGetDeviceState"] = snapshotJson<Json>(counters.msiGetDeviceState.drain());
    return value;
}
} // namespace ymcc::input::backendCost
