#pragma once
// Low-frequency diagnostics have an explicit field allowlist. Bootstrap tokens,
// request args, endpoints, config contents and game identities never enter logs.
#include <string>
#include "json.hpp"
namespace ymcc::deckydiag {
using Json=nlohmann::json;
inline constexpr unsigned long long maxBytes=1024ull*1024ull;
inline Json record(const std::string& event,const Json& detail) {
    Json result={{"event",event}};
    if(!detail.is_object())return result;
    for(const auto* key:{"ready","enabled","phase","reason","error","pid","revision","bindingRetry","bindingAttempts",
      "launchStage","launchMode","bootstrapStage","contextPid","contextCreated","contextGeneration","watching","ok","mutated","active","connected","steamKnown","steamRunning","steamDirectory","resourceDirectory","loaderPath","pluginPath","missingPath",
      "powerControlDirectory","directorySource","configuredOverride","logPath","createdSteamDebugMarker"})
      if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
    return result;
}
}
