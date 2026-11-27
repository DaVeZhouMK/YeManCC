#pragma once
// Steam shortcuts use the same native menu transport as the touch buttons.
// PS/Xbox still use their original virtual Guide pulse in the caller.
static bool nativeSteamShortcutEmit(ymcc::FrontendButton button,uint64_t sourceEpoch,uint64_t wmiGeneration,
    uint64_t wmiSequence,bool keyboardWmiFallback,OemProducerTokens producerTokens) {
    if(!g_inputReady.load(std::memory_order_acquire)||!sgVirtualWriterAllowedNow()) {
        nativeFrontendButtonReceipt(button,false,"input-power-gate");return false;
    }
    const auto generation=currentPowerGeneration();
    const auto actionEpoch=sourceEpoch==kOemShortcutUnboundEpoch?g_oemShortcutEdgeEpoch.load(std::memory_order_acquire):sourceEpoch;
    const auto revision=GamepadSerialEvent::captureRevision();
    const auto coldDeadline=nativeSteamMenuColdDeadline(GetTickCount64());
    const bool quick=button==ymcc::FrontendButton::SteamQuickAccess;
    const auto origin=quick?nativeSteamQamOrigin():NativeSteamQamOrigin{};
    const auto qamTicket=ymcc::qamfollowup::snapshot();
    const auto allowed=[generation,actionEpoch,revision,wmiGeneration,keyboardWmiFallback,producerTokens] {
        return currentPowerGeneration()==generation && GamepadSerialEvent::currentRevision(revision) &&
            actionEpoch==g_oemShortcutEdgeEpoch.load(std::memory_order_acquire) && oemProducerTokensCurrent(producerTokens) &&
            (!wmiGeneration || msiWmiGenerationCurrent(wmiGeneration)) &&
            (!keyboardWmiFallback || !msiWmiShortcutAuthoritative()) &&
            !g_exitRequested.load() && !g_shortcutRecordingActive.load() && !radialHasInputOwner() &&
            g_inputReady.load(std::memory_order_acquire) && sgVirtualWriterAllowedNow();
    };
    const bool queued=nativeYmccShortcutSubmit([button,allowed,qamTicket,coldDeadline,actionEpoch,generation,revision,quick,origin] {
        if(!allowed()) {
            nativeFrontendButtonReceipt(button,false,"input-power-gate");return;
        }
        // A new menu key supersedes an older touch recovery. Abandon only the
        // captured ticket; a later touch must keep its own recovery window.
        ymcc::qamfollowup::abandon(qamTicket);
        auto result=nativeSteamMenuButton(quick,allowed,json(),coldDeadline,actionEpoch,quick?&origin:nullptr);
        try {result["menuActionOrigin"]={{"schema",1},{"channel","shortcut"},
            {"generation",std::to_string(actionEpoch)},{"inputRevision",std::to_string(revision)},
            {"powerGeneration",std::to_string(generation)},{"powerSource","native-before-action-queue"}};
        }catch(...){} // Diagnostics must not change the original receipt or key.
        const bool ok=result.value("ok",false);
        nativeFrontendButtonReceipt(button,ok,ok?"steam-native-button-sent":
            result.value("mutated",false)?"steam-menu-result-uncertain":"steam-menu-unavailable",result);
    },SerialJobKind::ExternalInjection,sourceEpoch,wmiGeneration,wmiSequence,keyboardWmiFallback,producerTokens);
    if(!queued)nativeFrontendButtonReceipt(button,false,"action-queue-unavailable");
    return queued;
}
