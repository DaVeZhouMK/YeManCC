#pragma once
// Low-frequency diagnostics have an explicit field allowlist. Bootstrap tokens,
// request args, endpoints, config contents and game identities never enter logs.
#include <string>
#include "json.hpp"
namespace ymcc::deckydiag {
using Json=nlohmann::json;
inline constexpr unsigned long long maxBytes=1024ull*1024ull;
inline bool validBuildId(const Json& value) {
    if(!value.is_string())return false;
    const auto& text=value.get_ref<const std::string&>();if(text.size()!=64)return false;
    for(const auto c:text)if(!((c>='a'&&c<='f')||(c>='0'&&c<='9')))return false;
    return true;
}
// Renderer telemetry remains bounded facts only. Never arbitrary log content.
inline Json frontendRecord(const Json& args) {
    if(!args.is_object()||args.size()>22||!args.contains("event")||!args["event"].is_string())return Json{};
    const auto event=args["event"].get<std::string>();
    if(event!="lossless-read-start"&&event!="lossless-read-timeout"&&event!="lossless-read-finished"&&
       event!="lossless-action-start"&&event!="lossless-action-timeout"&&event!="lossless-action-finished"&&
       event!="snapshot-demand"&&event!="snapshot-read-start"&&event!="snapshot-read-finished")return Json{};
    Json result={{"event",event}};
    for(const auto* key:{"elapsedMs","powerMs","powerSourceMs","scheduleMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs"})
        if(args.contains(key)&&args[key].is_number_unsigned())result[key]=(std::min)(args[key].get<unsigned long long>(),3600000ull);
    for(const auto* key:{"readId","demandEpoch"})
        if(args.contains(key)&&args[key].is_number_unsigned())result[key]=(std::min)(args[key].get<unsigned long long>(),9007199254740991ull);
    for(const auto* key:{"ok","subscribe","discarded","completed","applied","pending"})if(args.contains(key)&&args[key].is_boolean())result[key]=args[key];
    if(args.contains("stage")&&args["stage"].is_string()){
        const auto stage=args["stage"].get<std::string>();if(stage=="settled"||stage=="late"||stage=="failed"||stage=="partial")result["stage"]=stage;
    }
    return result;
}
inline Json record(const std::string& event,const Json& detail) {
    Json result={{"event",event}};
    if(!detail.is_object())return result;
    for(const auto* key:{"generation","bindingRequested","scope","recoveryPhase","attempted","uncertain","elapsedMs","stage","ready","enabled","phase","reason","error","pid","revision","bindingRetry","bindingAttempts",
      "launchStage","launchMode","admissionRetry","admissionAttempts","admissionDetail","admissionElapsedMs","admissionNextRetryMs","admissionRecovery","deadlineMs","bootstrapStage","contextPid","contextCreated","contextGeneration","watching","ok","mutated","active","connected","steamKnown","steamRunning","steamDirectory","resourceDirectory","loaderPath","pluginPath","missingPath",
      "powerControlDirectory","directorySource","configuredOverride","logPath","createdSteamDebugMarker"})
      if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
    if(event=="lossless-launch-receipt"||event=="lossless-scale-receipt"||event=="lossless-reset-receipt"){
        for(const auto* key:{"started","reused","invoked","stopInvoked","restarted","cancelled","pending","paused","readable","reapply","refreshWindow","refreshAttempted","windowRefreshed","refreshHelperCreated","refreshHelperDestroyed","refreshLeftGame","refreshReturnRequested","refreshReturned","refreshCancelled","refreshApiAccepted","refreshExperiment","refreshFrameAccepted","refreshRedrawAccepted","refreshTopAccepted","refreshForegroundAccepted","failureResetRequested","failureResetAttempted","failureResetTerminated","failureResetClosed"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        if(detail.contains("processCreated")&&detail["processCreated"].is_string()){
            const auto text=detail["processCreated"].get<std::string>();
            if(!text.empty()&&text.size()<=20&&text.find_first_not_of("0123456789")==std::string::npos)result["processCreated"]=text;
        }
        if(detail.contains("failureResetTrigger")&&detail["failureResetTrigger"].is_string()&&detail["failureResetTrigger"]=="scaling-ipc-timeout")result["failureResetTrigger"]="scaling-ipc-timeout";
        if(detail.contains("failureResetPolicy")&&detail["failureResetPolicy"].is_string()&&detail["failureResetPolicy"]=="ipc-timeout-only")result["failureResetPolicy"]="ipc-timeout-only";
        if(detail.contains("retryAttempt")&&detail["retryAttempt"].is_number_unsigned()&&detail["retryAttempt"].get<unsigned>()<=1)result["retryAttempt"]=detail["retryAttempt"];
        if(detail.contains("refreshMethod")&&detail["refreshMethod"].is_number_unsigned()&&detail["refreshMethod"].get<unsigned>()<=7)result["refreshMethod"]=detail["refreshMethod"];
        if(detail.contains("refreshMethodName")&&detail["refreshMethodName"].is_string()){
            const auto name=detail["refreshMethodName"].get<std::string>();
            if(name=="redraw"||name=="frame-changed"||name=="foreground-request"||name=="normal-helper")result["refreshMethodName"]=name;
        }
        if(detail.contains("win32Error")&&detail["win32Error"].is_number_unsigned())result["win32Error"]=detail["win32Error"];
        if(detail.contains("refreshElapsedMs")&&detail["refreshElapsedMs"].is_number_unsigned())result["refreshElapsedMs"]=(std::min)(detail["refreshElapsedMs"].get<unsigned long long>(),12000ull);
        if(detail.contains("failureResetError")&&detail["failureResetError"].is_number_unsigned())result["failureResetError"]=detail["failureResetError"];
        for(const auto* key:{"failureResetReason","refreshReason"})if(detail.contains(key)&&detail[key].is_string()){
            const auto reason=detail[key].get<std::string>();
            if(reason.size()<=64&&reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz-")==std::string::npos)result[key]=reason;
        }
    }
    if(event=="lossless-action-finished")for(const auto* key:{"completed","applied","pending"})
        if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
    if(event=="mirror-snapshot-request"||event=="snapshot-demand"||event=="snapshot-read-start"||event=="snapshot-read-finished") {
        for(const auto* key:{"subscribe","readId","demandEpoch","discarded","powerMs","powerSourceMs","scheduleMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs"})
            if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
    }
    if(event=="wallpaper-inventory"||event=="wallpaper-prepare") {
        result={{"event",event}};
        for(const auto* key:{"elapsedMs","targetCount","shortcutCount","verifiedCount","shortcutTargets","shortcutPending","slotCount","retryDelayMs"})if(detail.contains(key)&&detail[key].is_number_unsigned())result[key]=(std::min)(detail[key].get<unsigned long long>(),3600000ull);
        for(const auto* key:{"ok","accountStable","shortcutsAvailable","staticReady","dynamicReady"})if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        const auto allow=[&](const char* key,std::initializer_list<const char*> choices){if(!detail.contains(key)||!detail[key].is_string())return;const auto& value=detail[key].get_ref<const std::string&>();for(const auto* choice:choices)if(value==choice){result[key]=value;break;}};
        allow("stage",{"started","finished","superseded","failed"});
        allow("reason",{"ready","waiting-steam-account","shortcut-inventory-unavailable","account-changed","inventory-read-failed","identity-unconfirmed","superseded","network-failed","prepare-failed"});
    }
    if(event=="plugin-build"&&detail.contains("buildId")&&validBuildId(detail["buildId"]))result["buildId"]=detail["buildId"];
    if(event=="prepare"||event=="environment") {
        for(const auto* key:{"loaderExpectedSha256","loaderObservedSha256"})
            if(detail.contains(key)&&validBuildId(detail[key]))result[key]=detail[key];
        for(const auto* key:{"resourceCacheAvailable","resourceRepairAttempted","resourceWorkerDestroyed"})if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        if(detail.contains("resourcesRepaired")&&detail["resourcesRepaired"].is_number_unsigned()&&detail["resourcesRepaired"]<=7)result["resourcesRepaired"]=detail["resourcesRepaired"];
        if(detail.contains("resourceRepairReason")&&detail["resourceRepairReason"].is_string()){const auto reason=detail["resourceRepairReason"].get<std::string>();if(reason.size()<=96&&reason.starts_with("resource-cache-")&&reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz-")==std::string::npos)result["resourceRepairReason"]=reason;}
        if(detail.contains("managedPackageSource")&&detail["managedPackageSource"].is_string()){const auto source=detail["managedPackageSource"].get<std::string>();if(source=="receipt"||source=="package"||source=="legacy")result["managedPackageSource"]=source;}
        if(detail.contains("managedPackageVerified")&&detail["managedPackageVerified"].is_boolean())result["managedPackageVerified"]=detail["managedPackageVerified"];
        if(detail.contains("loaderHashMatched")&&detail["loaderHashMatched"].is_boolean())result["loaderHashMatched"]=detail["loaderHashMatched"];
        if(detail.value("loaderOrigin",Json())=="official-release")result["loaderOrigin"]="official-release";
        if(detail.contains("loaderVersion")&&detail["loaderVersion"].is_string()){const auto& v=detail["loaderVersion"].get_ref<const std::string&>();if(!v.empty()&&v.size()<=32&&v.find_first_not_of("v0123456789.")==std::string::npos)result["loaderVersion"]=v;}
        if(detail.contains("loaderModified")&&detail["loaderModified"].is_boolean())result["loaderModified"]=detail["loaderModified"];
        if(detail.contains("loaderConsumerContract")&&detail["loaderConsumerContract"].is_number_integer()&&detail["loaderConsumerContract"]>=1&&detail["loaderConsumerContract"]<=1000)result["loaderConsumerContract"]=detail["loaderConsumerContract"];
        if(detail.contains("loaderPolicyVersion")&&detail["loaderPolicyVersion"].is_number_integer()&&detail["loaderPolicyVersion"]>=0&&detail["loaderPolicyVersion"]<=1000)
            result["loaderPolicyVersion"]=detail["loaderPolicyVersion"];
    }
    // CSS callbacks already provide these bounded facts, but the generic
    // allowlist previously dropped them, making cleanup look verified by error=0.
    if(event=="steam-css-use"||event=="steam-css-guard"||event=="steam-mode-signal") {
        for(const auto* key:{"policyVersion","steamPid","steamBirth","loaderPid","linked","attempts","verified","handleFallback",
            "remaining","unverified","secondaryCheckPending","minimumHoldPending","secondaryDelayMs","minimumHoldMs","helperExitKnown","helperExitCode","helperResultKnown","helperPhase","helperReason","helperFailureCode","helperHResult","backendReady","configuredMatchesBackend","domConfirmed","minimumWindowConfirmed","backendExitConfirmed","backendProcessCount","editorProtected","editorStateUnknown"})
            if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
        if(detail.contains("loaders")&&detail["loaders"].is_array()) {
            auto rows=Json::array();
            for(const auto& row:detail["loaders"]) {
                if(rows.size()>=16)break;
                if(row.is_object()&&row.contains("pid")&&row["pid"].is_number_integer()&&row.contains("birth")&&row["birth"].is_string()&&row["birth"].get_ref<const std::string&>().size()<=20)
                    rows.push_back({{"pid",row["pid"]},{"birth",row["birth"]}});
            }
            result["loaders"]=rows;result["loaderIdentitiesTruncated"]=detail["loaders"].size()>rows.size();
        }
    }
    return result;
}
}
