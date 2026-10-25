#pragma once
// External sampling, queue transport, focus, OEM prerequisites and side effects.
// These ports implement no return/owner policy; actual extracted functions do.
inline bool IsWindow(HWND h){return memory::windows.count(h)!=0;}
inline bool IsWindowVisible(HWND h){return IsWindow(h);}
inline bool IsIconic(HWND){return false;}
inline UINT_PTR SetTimer(HWND,UINT,UINT,void*){memory::log("port.SetTimer");return 1;}
inline bool focusMainWindowIsForeground(){return mainports::foreground;}
inline FocusTargetSnapshot focusCaptureForegroundTarget(){return mainports::denied<FocusTargetSnapshot>("focusCaptureForegroundTarget");}
inline void focusBeginReturnToPreviousWindow(ULONGLONG){mainports::forbidden("focusBeginReturnToPreviousWindow");}
inline bool customSteamLibraryInputSuppressed(){return mainports::customSuppressed;}
inline bool customSteamLibraryChildForeground(){return mainports::customForeground;}
inline bool customSteamLibrarySendSemanticAction(const char*){return mainports::denied<bool>("customSteamLibrarySendSemanticAction");}
inline void appendNativeLifecycleLog(const std::string&event,const json&value){memory::log("main.lifecycle",{{"event",event},{"value",value}});}
inline void ipc_emit(const std::string&event,const json&value){mainports::events.push_back({{"event",event},{"value",value}});memory::log("main.ipc_emit",{{"event",event},{"value",value}});}
inline bool configureAppHost(ICoreWebView2*){return mainports::denied<bool>("configureAppHost");}
struct HudPort{void suspendForRadial(bool active){memory::log("port.Hud.suspendForRadial",{{"active",active}});}};
inline HudPort g_desktopMonitorHud;
inline bool tickCostEnabled(){return false;}
inline void gamepadReadStateUnlocked(bool){g_curPad=mainports::sample;g_curW=g_curPad.wButtons;g_padConnected=mainports::connected;memory::log("port.physical-snapshot",{{"buttons",g_curW},{"leftTrigger",g_curPad.bLeftTrigger},{"rightTrigger",g_curPad.bRightTrigger},{"lx",g_curPad.sThumbLX},{"ly",g_curPad.sThumbLY},{"rx",g_curPad.sThumbRX},{"ry",g_curPad.sThumbRY}});}
inline void nativeKeyboardShortcutsEvaluate(WORD,const XINPUT_GAMEPAD&,ULONGLONG,bool){memory::log("port.OEM-rule-evaluation-unselected");}
inline void gamepadRecordShortcutChord(uint64_t){mainports::forbidden("gamepadRecordShortcutChord");}
inline uint64_t gamepadShortcutRecordingMask(WORD,const XINPUT_GAMEPAD&){return mainports::denied<uint64_t>("gamepadShortcutRecordingMask");}
inline uint64_t nativeOemKeyMask(bool){return mainports::denied<uint64_t>("nativeOemKeyMask");}
inline void resetParentShortcutStateForCustomSteamLibrary(){mainports::forbidden("resetParentShortcutStateForCustomSteamLibrary");}
inline bool nativeShortcutLegacyDefaultAllowed(const char*){return false;}
inline void gamepadProcessUiInput(WORD buttons,ULONGLONG){++mainports::parentLaneCalls;memory::log("port.parent-UI-lane",{{"buttons",buttons}});}
inline const std::string&gamepadUiSessionKey(){static const std::string value="memory-parent-session";return value;}
inline bool nativeYmccShortcutSummon(const FocusTargetSnapshot&,bool=false){return mainports::denied<bool>("nativeYmccShortcutSummon");}
inline bool oemProducerTokensCurrent(const OemProducerTokens&){return true;}
inline bool msiWmiGenerationCurrent(uint64_t){return true;}
inline bool msiWmiShortcutAuthoritative(){return false;}
inline bool nativeSteamShortcutEmit(ymcc::FrontendButton,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){return mainports::denied<bool>("nativeSteamShortcutEmit");}
inline bool sgVirtualWriterAllowedNow(){return mainports::denied<bool>("sgVirtualWriterAllowedNow");}
inline bool hardwareWriteAllowed(){return mainports::denied<bool>("hardwareWriteAllowed");}
inline void nativeFrontendButtonReceipt(ymcc::FrontendButton,bool,const char*){mainports::forbidden("nativeFrontendButtonReceipt");}
inline unsigned long long nativeFrontendButtonTargetEpoch(ymcc::FrontendButton){return mainports::denied<unsigned long long>("nativeFrontendButtonTargetEpoch");}
inline bool nativeFrontendButtonSendGamebar(){return mainports::denied<bool>("nativeFrontendButtonSendGamebar");}
struct FrontendPulsePort{bool request(ULONGLONG,unsigned long long){return mainports::denied<bool>("frontendPulse.request");}};
inline FrontendPulsePort g_frontendButtonPulse;
inline bool gamepadSerialSubmit(std::function<void()> job,SerialJobKind kind=SerialJobKind::YmccOwn){memory::log("port.queue-accept",{{"kind",static_cast<int>(kind)}});mainports::queued.push_back(std::move(job));return true;}
inline void gamepadSerialPostEvent(const char*,const json&){mainports::forbidden("gamepadSerialPostEvent");}
inline void radialSummonRequest(const char*,int=0,int=0){memory::log("port.radialSummonRequest");}
inline json nativeShortcutTdpEvent(int){return mainports::denied<json>("nativeShortcutTdpEvent");}
inline void nativeApplyBrightness(int,bool,int){mainports::forbidden("nativeApplyBrightness");}
inline void nativeAdjustTdp(int){mainports::forbidden("nativeAdjustTdp");}
inline void nativeAdjustBrightness(int){mainports::forbidden("nativeAdjustBrightness");}
inline void returnToDesktop(){++mainports::returnDesktopCalls;mainports::actions.push_back({{"action","os.returnDesktop"},{"endpoint","memory-only"}});memory::log("port.returnToDesktop");}
inline void runKillBat(){mainports::forbidden("runKillBat");}
inline void openTouchKeyboard(){mainports::forbidden("openTouchKeyboard");}
inline void toggleMouseMode(const std::string&){mainports::forbidden("toggleMouseMode");}
