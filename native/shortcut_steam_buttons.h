#pragma once
// Existing virtual writer only; the fallback is the user's explicit Ctrl+1 for
// BOTH Steam shortcuts. The screen touchpad's separate native-menu path is untouched.
static bool nativeFrontendButtonNoVirtual() {
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    return !g_inputHostProcess;
}
static bool nativeFrontendButtonSendCtrl1() {
    // Keep an already-held physical Ctrl down. Other held modifiers would
    // change the requested chord, and are never synthetically released.
    for(int key:{VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(key)&0x8000)return false;
    const bool ctrlHeld=(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_LCONTROL)&0x8000)||(GetAsyncKeyState(VK_RCONTROL)&0x8000);
    auto inputs=ymcc::screenactions::menuChord(false,0);
    INPUT* first=inputs.data()+(ctrlHeld?1:0);const UINT count=ctrlHeld?2u:4u;
    const auto sent=keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId::NativeKeyboardShortcut,count,first);
    if(sent && sent<count)keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId::NativeKeyboardShortcut,ctrlHeld?1u:2u,inputs.data()+2);
    return sent==count;
}
static bool nativeSteamShortcutEmit(ymcc::FrontendButton button,uint64_t sourceEpoch,uint64_t wmiGeneration,
    uint64_t wmiSequence,bool keyboardWmiFallback,OemProducerTokens producerTokens) {
    if(!g_inputReady.load(std::memory_order_acquire)||!sgVirtualWriterAllowedNow()) {
        nativeFrontendButtonReceipt(button,false,"input-power-gate");return false;
    }
    const auto epoch=nativeFrontendButtonTargetEpoch(button);
    const bool noVirtual=!epoch&&nativeFrontendButtonNoVirtual();
    if(!epoch&&!noVirtual) {
        nativeFrontendButtonReceipt(button,false,"matching-virtual-gamepad-not-ready");return false;
    }
    const auto generation=currentPowerGeneration();
    if(focusMainWindowIsForeground())focusBeginReturnToPreviousWindow(GetTickCount64());
    const bool queued=nativeYmccShortcutSubmit([button,epoch,noVirtual,generation] {
        if(currentPowerGeneration()!=generation||!g_inputReady.load(std::memory_order_acquire)||!sgVirtualWriterAllowedNow()) {
            nativeFrontendButtonReceipt(button,false,"input-power-gate");return;
        }
        if(noVirtual) {
            if(!nativeFrontendButtonNoVirtual()) {nativeFrontendButtonReceipt(button,false,"virtual-target-changed");return;}
            const bool ok=nativeFrontendButtonSendCtrl1();
            nativeFrontendButtonReceipt(button,ok,ok?"ctrl-1-sent":"physical-modifier-held-or-keyboard-injection-failed");return;
        }
        if(epoch!=nativeFrontendButtonTargetEpoch(button)) {nativeFrontendButtonReceipt(button,false,"virtual-target-changed");return;}
        const unsigned mask=button==ymcc::FrontendButton::SteamQuickAccess?2u:1u;
        const bool ok=g_frontendButtonPulse.request(GetTickCount64(),epoch,mask);
        nativeFrontendButtonReceipt(button,ok,ok?"steam-special-pulse-queued":"guide-pulse-already-pending");
    },SerialJobKind::ExternalInjection,sourceEpoch,wmiGeneration,wmiSequence,keyboardWmiFallback,producerTokens);
    if(!queued)nativeFrontendButtonReceipt(button,false,"action-queue-unavailable");
    return queued;
}
