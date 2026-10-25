#pragma once
// Completion/result notifications only. No monitor preference, output or input mutation.
static void nativeShortcutFeedback(const std::string& action,const char* state,const char* title,
    const std::string& message,const json& value=json::object()) {
    static std::atomic<unsigned long long> sequence{0};
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    json result={{"schemaVersion",1},{"id",std::string("shortcut-native-")+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++sequence)},
        {"actionId",action},{"origin","controller-shortcut"},{"state",state},{"title",title},{"message",message},
        {"occurredAtUnixMs",now},{"expiresAtUnixMs",now+(std::strcmp(state,"failed")==0?5000:2500)},
        {"powerGeneration",currentPowerGeneration()},{"presentation",{{"fullscreen",true},{"leftMonitor",true}}}};
    if(value.is_object()&&!value.empty())result["value"]=value;
    ipc_emit("input.shortcutFeedback",result);
}
static void nativeShortcutFeedbackFromResult(const std::string& event,const json& data) {
    if(!data.is_object()||!data.contains("ok")||!data["ok"].is_boolean())return;
    const bool ok=data["ok"].get<bool>();
    if(event=="gamepad.brightness") {
        if(ok&&data.contains("value")&&data["value"].is_number_integer()) {
            const int value=data["value"].get<int>();nativeShortcutFeedback("display.brightnessAdjust","applied","亮度",
                "亮度已调整为 "+std::to_string(value)+"%。",{{"text",std::to_string(value)+"%"},{"number",value},{"unit","%"}});
        } else nativeShortcutFeedback("display.brightnessAdjust","failed","亮度","亮度未确认调整，请检查当前电源方案和显示设备。");
    }
    // Mouse policy transitions use the automatic-only bottom-right HUD hint.
}
