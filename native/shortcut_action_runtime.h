#pragma once
// Included beside nativeKeyboardShortcutDispatch, after original source/power helpers.
static void ipc_on(const std::string&, std::function<json(const json&)>);
static ymcc::ShortcutActionMailbox g_shortcutActionMailbox(
    std::string("shortcut-")+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
static int64_t nativeShortcutUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
static bool nativeShortcutUiDispatch(const NativeKeyboardShortcutRule& rule,uint64_t sourceEpoch,
    uint64_t wmiGeneration,uint64_t wmiSequence,bool keyboardWmiFallback,OemProducerTokens producerTokens) {
    const auto action=rule.uiAction;const auto params=rule.uiParams;
    const auto generation=currentPowerGeneration();
    const auto revision=ymSettingsSection("input").value("revision",0ll);
    const auto current=[generation,revision,sourceEpoch,wmiGeneration,keyboardWmiFallback,producerTokens] {
        return !g_exitRequested.load(std::memory_order_acquire) && !g_shortcutRecordingActive.load(std::memory_order_acquire) &&
            g_inputReady.load(std::memory_order_acquire) && sgVirtualWriterAllowedNow() && currentPowerGeneration()==generation &&
            ymSettingsSection("input").value("revision",0ll)==revision && oemProducerTokensCurrent(producerTokens) &&
            (sourceEpoch==kOemShortcutUnboundEpoch || sourceEpoch==g_oemShortcutEdgeEpoch.load(std::memory_order_acquire)) &&
            (!wmiGeneration || msiWmiGenerationCurrent(wmiGeneration)) && (!keyboardWmiFallback || !msiWmiShortcutAuthoritative());
    };
    return nativeYmccShortcutSubmit([action,params,generation,revision,current] {
        if(!current())return;
        const auto request=g_shortcutActionMailbox.issue(action,params,generation,static_cast<uint64_t>(revision),
            GetTickCount64(),nativeShortcutUnixMs(),current);
        if(request.empty()) {
            appendNativeLifecycleLog("shortcut-action-request",{{"actionId",action},{"status","not-issued"}});return;
        }
        appendNativeLifecycleLog("shortcut-action-request",{{"id",request["id"]},{"actionId",action},{"status","issued"}});
        ipc_emit("input.shortcutAction",request);
    },SerialJobKind::YmccOwn,sourceEpoch,wmiGeneration,wmiSequence,keyboardWmiFallback,producerTokens);
}
static json nativeShortcutTdpEvent(int delta) {
    const auto generation=currentPowerGeneration();const auto revision=ymSettingsSection("input").value("revision",0ll);
    const auto epoch=g_oemShortcutEdgeEpoch.load(std::memory_order_acquire);
    const auto current=[generation,revision,epoch] {return currentPowerGeneration()==generation && g_inputReady.load(std::memory_order_acquire) &&
        sgVirtualWriterAllowedNow() && !g_exitRequested.load(std::memory_order_acquire) && !g_shortcutRecordingActive.load(std::memory_order_acquire) &&
        g_oemShortcutEdgeEpoch.load(std::memory_order_acquire)==epoch && ymSettingsSection("input").value("revision",0ll)==revision;};
    json event={{"delta",delta}};
    const auto ticket=g_shortcutActionMailbox.issueResultTicket("power.tdpAdjust",generation,static_cast<uint64_t>(revision),GetTickCount64(),nativeShortcutUnixMs(),current);
    if(!ticket.empty())event["feedbackId"]=ticket["id"];
    else event["delta"]=0; // bounded admission: never enqueue an untracked duplicate burst
    return event;
}
static void nativeShortcutActionRegisterIpc() {
    ipc_on("input.shortcutAction.claim",[](const json& args)->json {
        if(!args.is_object()||!args.contains("id")||!args["id"].is_string()||args["id"].get_ref<const std::string&>().size()>128)return {{"ok",false}};
        const auto request=g_shortcutActionMailbox.claim(args["id"].get<std::string>(),GetTickCount64());
        return {{"ok",!request.empty()}};
    });
    ipc_on("input.shortcutAction.complete",[](const json& args)->json {
        if(!args.is_object()||!args.contains("id")||!args["id"].is_string()||!args.contains("outcome"))return {{"ok",false}};
        const auto feedback=g_shortcutActionMailbox.complete(args["id"].get<std::string>(),args["outcome"],GetTickCount64(),nativeShortcutUnixMs());
        if(feedback.empty())return {{"ok",false}};
        appendNativeLifecycleLog("shortcut-action-result",feedback);
        const bool silent=args["outcome"].is_object() && args["outcome"].value("silent",false);
        if(!silent)ipc_emit("input.shortcutFeedback",feedback);
        return {{"ok",true}};
    });
}
