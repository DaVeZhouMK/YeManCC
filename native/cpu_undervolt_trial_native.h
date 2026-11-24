#pragma once
#include "sleep_power_state.h"
// UI-thread owner: only a pending trial has a Win32 timer. It survives route
// changes and hidden WebView pages; no renderer timer can commit preferences.
static constexpr UINT_PTR CPU_UV_TRIAL_TIMER = 0xA130;
static bool cpuUvExcluded() {
    const auto family=g_machineIdentity.family;
    return family==YmccFamily::AsusRogAlly || family==YmccFamily::AsusRogAllyX ||
        family==YmccFamily::AsusRogAllyClassic || family==YmccFamily::AsusRogAllyXClassic ||
        g_machineIdentity.deviceClass.find("ROGAlly")!=std::string::npos;
}
static bool cpuUvPreferenceValid(const json& p) {
    if(!p.is_object()||!p.contains("schema")||!p["schema"].is_number_integer()||p["schema"]!=1||!p.contains("enabled")||!p["enabled"].is_boolean()||
        !p.contains("value")||!p["value"].is_number_integer()||!p.contains("identity")||!p["identity"].is_object())return false;
    if(p["value"].is_number_unsigned()&&p["value"].get<unsigned long long>()!=0)return false;
    const auto value=p["value"].get<long long>();
    return value>=-32768&&value<=0&&p.contains("vendor")&&p["vendor"].is_string()&&p.contains("unit")&&p["unit"].is_string()&&
        ((p["vendor"]=="amd"&&p["unit"]=="co_step")||(p["vendor"]=="intel"&&p["unit"]=="mv"));
}
struct CpuUvTrialState {
    unsigned long long token=0; ULONGLONG started=0;
    bool initialized=false,pending=false;
    ymcc::sleeppower::SharedPowerV1 power;
    json saved=nullptr,baseline=nullptr,candidate=nullptr;
    std::string stage="idle",error;
};
static CpuUvTrialState g_cpuUvTrial;
static json cpuUvDiskPreference(const json& document) {
    if(!document.is_object()||!document.contains("cpu")||!document["cpu"].is_object())return nullptr;
    return document["cpu"].value("undervolt",json(nullptr));
}
static json cpuUvTrialSnapshot() {
    auto& s=g_cpuUvTrial;
    if(!s.initialized){s.saved=cpuUvDiskPreference(ymSettingsRead());if(!cpuUvPreferenceValid(s.saved))s.saved=nullptr;s.initialized=true;}
    const auto elapsed=s.pending?GetTickCount64()-s.started:0;
    return {{"ok",true},{"token",s.token},{"pending",s.pending},{"remaining",s.pending?int((elapsed>=30000?0:30000-elapsed)+999)/1000:0},
        {"stage",s.stage},{"error",s.error},{"candidate",s.pending?s.candidate:json(nullptr)},{"saved",s.saved},{"excluded",cpuUvExcluded()}};
}
static void cpuUvTrialEmit(){try{ipc_emit("cpu.undervolt.state",cpuUvTrialSnapshot());}catch(...){}}
static void cpuUvTrialStop(const char* reason) {
    KillTimer(g_hwnd,CPU_UV_TRIAL_TIMER);++g_cpuUvTrial.token;g_cpuUvTrial.pending=false;
    g_cpuUvTrial.power={};
    g_cpuUvTrial.candidate=nullptr;g_cpuUvTrial.stage=reason;g_cpuUvTrial.error.clear();
}
static bool cpuUvTrialPowerChanged() {
    auto& s=g_cpuUvTrial;
    if((!s.pending&&s.stage!="applying")||sharedPowerLifecycleCurrent(s.power))return false;
    cpuUvTrialStop("suspended");cpuUvTrialEmit();return true;
}
static json cpuUvTrialBegin() {
    cpuUvTrialStop("applying");auto& s=g_cpuUvTrial;
    if(!captureSharedPowerLifecycle(s.power)){s.stage="suspended";return cpuUvTrialSnapshot();}
    s.baseline=cpuUvDiskPreference(ymSettingsRead());
    s.saved=cpuUvPreferenceValid(s.baseline)?s.baseline:json(nullptr);s.initialized=true;
    (void)cpuUvTrialPowerChanged();
    return cpuUvTrialSnapshot();
}
static json cpuUvTrialArm(const json& a) {
    auto& s=g_cpuUvTrial;
    if(!a.contains("token")||(!a["token"].is_number_unsigned()&& !a["token"].is_number_integer())||a["token"]!=s.token)
        return {{"ok",false},{"error","trial_superseded"}};
    if(s.stage!="applying")return {{"ok",false},{"error","trial_superseded"}};
    if(cpuUvExcluded())return {{"ok",false},{"error","device_unsupported"}};
    const auto p=a.value("preference",json(nullptr)),r=a.value("receipt",json(nullptr));
    if(!cpuUvPreferenceValid(p)||!r.is_object()||r.value("accepted",false)!=true||r.value("vendor",std::string{})!=p["vendor"].get<std::string>()||
        r.value("unit",std::string{})!=p["unit"].get<std::string>()||!r.contains("requested")||!r["requested"].is_object()||
        r["requested"].value("value",2147483647)!=(p["enabled"].get<bool>()?p["value"].get<int>():0))
        return {{"ok",false},{"error","accepted_voltage_receipt_required"}};
    if(cpuUvTrialPowerChanged())return {{"ok",false},{"error","trial_superseded"}};
    s.candidate=p;s.started=GetTickCount64();s.pending=true;s.stage="pending";s.error.clear();
    if(!SetTimer(g_hwnd,CPU_UV_TRIAL_TIMER,1000,nullptr)){s.pending=false;s.stage="failed";s.error="timer_start_failed";}
    cpuUvTrialEmit();return cpuUvTrialSnapshot();
}
static void cpuUvTrialTick() {
    auto& s=g_cpuUvTrial;if(!s.pending){KillTimer(g_hwnd,CPU_UV_TRIAL_TIMER);return;}
    if(cpuUvTrialPowerChanged())return;
    if(GetTickCount64()-s.started<30000){cpuUvTrialEmit();return;}
    KillTimer(g_hwnd,CPU_UV_TRIAL_TIMER);
    try{
        SettingsFileGuard guard;
        const auto raw=ymSettingsReadRaw(YM_SETTINGS_FILE);auto all=ymSettingsReadUnlocked();
        if(!ymSettingsHasCompatibleWriteBase(raw,all))throw std::runtime_error("settings_not_writable");
        if(cpuUvDiskPreference(all)!=s.baseline)throw std::runtime_error("voltage_settings_changed");
        if(cpuUvExcluded()||!cpuUvPreferenceValid(s.candidate))throw std::runtime_error("device_or_preference_changed");
        auto next=all;if(!next.contains("cpu")||!next["cpu"].is_object())next["cpu"]=json::object();next["cpu"]["undervolt"]=s.candidate;
        const auto serialized=next.dump();
        // Also check after waiting for the settings lock and preparing the write.
        if(!sharedPowerLifecycleCurrent(s.power))cpuUvTrialStop("suspended");
        else{
            if(!ymSettingsWriteDocumentUnlocked(serialized,nullptr,&all))throw std::runtime_error("voltage_save_failed");
            s.saved=s.candidate;s.stage="saved";s.error.clear();
        }
    }catch(const std::exception& e){s.stage="failed";s.error=e.what();}
    s.pending=false;s.power={};
    cpuUvTrialEmit();
}
