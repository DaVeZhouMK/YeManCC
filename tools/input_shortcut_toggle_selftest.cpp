// Included by the generated native fixture: uses real production functions.
// All storage, desktop-key reads, injection, serial jobs and host refresh are memory-only.
#include <future>

static int passed = 0;
static void check(bool ok, const std::string& name) {
    if (!ok) throw std::runtime_error(name);
    ++passed; std::cout << "PASS " << name << '\n';
}
static json baseInput(bool gyro = false, const std::string& persona = "disabled") {
    return {{"revision", 7}, {"outputTarget", {{"revision", 7}, {"persona", persona},
        {"buttonMappingEnabled", persona != "disabled"}, {"gyroEnabled", gyro}, {"futureTarget", 42}}},
        {"gyroMotion", {{"revision", 7}, {"enabled", gyro}, {"preset", "custom"}, {"activePreset", "custom"},
            {"outputMode", "virtual-stick"}, {"virtualPadLink", true}, {"gyroMultiplier", 2.3},
            {"outputStick", "right"}, {"futureMotion", {{"keep", true}}}, {"presets", json::object()}}},
        {"buttonMapping", {{"rules", {{"futureMapping", true}}}}}, {"futureInput", {{"keep", true}}}};
}
static json dedicated(json input, const std::string& identity = "game-A", bool gyro = true, const std::string& persona = "elite") {
    auto active = baseInput(gyro, persona);
    input["gameOverride"] = {{"identity", identity}, {"sessionId", "memory-session"},
        {"outputTarget", active["outputTarget"]}, {"gyroMotion", active["gyroMotion"]}, {"futureGame", 81}};
    return input;
}
static json rule(const std::string& action, const std::string& preset, const std::string& trigger = "press", const std::string& source = "controller", const std::string& code = "a") {
    return {{"id", "fixture"}, {"enabled", true}, {"actionId", action}, {"params", {{"preset", preset}}},
        {"inputs", json::array({{{"source", source}, {"code", code}}})}, {"trigger", trigger},
        {"holdMs", 500}, {"doubleWindowMs", 500}, {"intervalMs", 150}};
}
static void resetRoute(const json& rules, const json& input = baseInput()) {
    fixtureInput = input; fixtureInput["buttonMapping"]["rules"]["shortcutRules"] = rules;
    fixtureAssets = true; fixtureLogs.clear(); fixtureInjections.clear(); fixtureDesktopDown.clear();
    fixtureOemMask = 0; fixtureJobs.clear(); fixtureRefreshes = 0; fixtureJoyxoffCloses = 0;
    g_exitRequested = false; g_shortcutRecordingActive = false;
    fixtureRecordedShortcutClicks.clear();
    fixtureRecordedShortcutChords.clear();
    g_shortcutRecordingOemMask=0;g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.deviceClass.clear();g_controllerFrontendIsolationEnabled=true;
    g_inputShortcutRuntime.clear(); g_nativeKeyboardShortcutLastRefresh = 0;
    g_nativeKeyboardShortcutRevision = -1; g_nativeKeyboardShortcuts.clear();
#if !defined(YMCC_SHORTCUT_SOURCE_SCOPES)
    g_nativeKeyboardShortcutRequireRelease = true;
#endif
    fixtureOwnActions.clear(); fixtureDeltas.clear();
    fixtureWindowVisible=true;fixtureWindowIconic=fixtureWindowZoomed=fixtureWindowForeground=false;g_fullHeight=false;
    fixtureFocusAllowed=true;fixtureSummonTimerStarts=fixtureSummonTimerKills=0;
    fixtureCapturedTarget={true,0};g_pendingSummonTarget={};fixturePreparedTargets.clear();
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=true;
    g_inputReady=true;fixtureWriterAllowed=true;fixtureFrontendPersona=g_inputHostBoundPersona="disabled";g_inputHostEpochMirror=1;
    g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostFaultHoldsStart=false;
    g_frontendButtonPulse.clear();fixtureSendInputs.clear();fixtureSendCount=UINT_MAX;
    g_inputHostProcess=reinterpret_cast<HANDLE>(1);
    fixtureSteamAttempts=0;fixtureSteamThrows=false;fixtureSteamRequests.clear();fixtureSteamBeforeSend={};
    fixtureSteamResult={{"ok",true},{"mutated",true},{"keyDelivered",true},{"runtimeAccepted",false},
        {"response","key-delivered-no-menu-state-change"},{"menuTarget",{{"source","steam-game-overlay"}}}};
    ymcc::qamfollowup::abandon(ymcc::qamfollowup::snapshot());
#if defined(YMCC_PRODUCER_EDGE_SOURCE)
#if defined(YMCC_ROG_CLICK_SOURCE)
    g_rogOemKey = {}; fixtureLegacyRog = 0;
#endif
#if defined(YMCC_HID_READ_SLOT_SOURCE)
    fixtureIoWait=WAIT_TIMEOUT;fixtureIoResultError=ERROR_IO_INCOMPLETE;fixtureIoReadError=ERROR_IO_PENDING;
    fixtureIoEnumCalls=fixtureIoOpenCalls=fixtureIoEventCreates=0;fixtureIoEnumAvailable=false;fixtureIoOpenOk=fixtureIoEventOk=true;fixtureIoVendor=0x17EF;fixtureIoProduct=0x6182;fixtureIoUsagePage=0xFFA0;g_rog={};
    fixtureIoResultOk=fixtureIoReadOk=false;fixtureIoResultCalls=fixtureIoReadCalls=0;
    fixtureIoCancelOk=true;fixtureIoCancelError=ERROR_SUCCESS;fixtureIoClosed.clear();
    fixtureIoWaitBudgets.clear();fixtureIoResultWaitFlags.clear();fixtureIoBytes=64;
    std::fill(std::begin(fixtureIoData),std::end(fixtureIoData),0);
#endif
    g_oemProducerStates = {}; g_oemProducerEdges = {};
    for (auto& generation : g_oemProducerGenerations) generation = 0;
    for (auto& authority : g_oemProducerAuthorities) authority = 0;
    g_oemProducerEdgeHead = g_oemProducerEdgeCount = 0; g_oemProducerEdgeSequence = 0;
    g_legionBack.~LegionBackSession(); new (&g_legionBack) LegionBackSession;
    g_legionSBack.~LegionSBackSession(); new (&g_legionSBack) LegionSBackSession;
    g_gpdBack = {}; g_onexBack = {}; g_msiDinput.actionGeneration = 0;
    g_oemBackState = g_msiDinputBackBits = 0; g_oemRearMapToL5 = g_oemRearMapToR5 = 0;
    fixtureProducerPosts = fixtureCancelCalls = 0;
#endif

    fixtureJobAccept = true;
#if defined(YMCC_WMI_CLICK_SOURCE)
    fixtureTick = 10000; g_nativeWmiAuthoritySeen = false;
    g_msiWmiEnabled = false; g_msiWmiStop = false; g_msiWmiActiveGeneration = 0;
    g_msiWmiState = 0; g_msiWmiFailedAt = 0; g_msiWmiPulseMask = 0; g_msiWmiPulseUntil = 0;
    g_msiWmiClickCount = g_msiWmiClickHead = 0; g_msiWmiClickSequence = 0;
    g_msiWmiThread = nullptr; g_msiWmiThreadGeneration = g_msiWmiNextGeneration = 0;
    fixtureWmiCreates = fixtureWmiCloses = fixtureWmiWakePosts = fixtureLegacyClaw = 0;
    fixtureWmiTerminal = false; fixtureWmiCreateOk = true; fixtureLastWaitMs = UINT_MAX;
#endif
#ifdef YMCC_RECORDED_CHORD_SOURCE
    g_desktopChordWanted=false;g_desktopChordCaptureKeys={};g_desktopChordKeys={};g_desktopChordArmedKeys={};g_desktopChordLlOwned={};g_desktopChordRules.clear();
    g_desktopChordPower={};g_desktopChordEpoch=0;g_desktopChordBlocked=false;g_desktopChordCancellation=0;
#endif
    g_oemKbDown = 0; g_oemMouseLeft = false; g_oemMouseX2 = false;
    g_oemKbDownRaw = 0; g_oemBackStateKb = 0;
    g_oemVirtualKeyboardConsumed=0;g_oemVirtualKeyboardProfile=-1;
#if defined(YMCC_VIRTUAL_POLICY_SOURCE)
    g_oemVirtualBindingPolicy.store(nullptr);g_oemVirtualKeyboardSample.store(nullptr);
    g_oemVirtualKeyboardLeases={};g_oemVirtualReservationsActive=false;
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    g_oemVirtualSourcePressSequence=0;g_oemVirtualKeyboardEdges={};g_oemVirtualKeyboardEdgeSequence=0;
    g_oemVirtualKeyboardCapacityRefusals=0;
#endif
    g_oemVirtualKeyboardSampleWanted=false;g_oemVirtualKeyboardPressSequence=0;
#if defined(YMCC_VIRTUAL_KEYBOARD_WRITER_SOURCE)
    g_oemVirtualKeyboardWriter={};
#endif
#endif
    g_oemRawPathActive=false;g_oemRawPathWanted=false;g_desktopFunctionEdges={};g_desktopFunctionCaptureMask=0;g_desktopFunctionArmedMask=0;g_desktopFunctionKeys=0;g_desktopFunctionPhysicalKeys=0;g_desktopFunctionLlOwned=0;g_desktopFunctionPresses={};
    for(auto& value:g_desktopFunctionCancellations)value=0;
    g_desktopFunctionHookActive=false;g_desktopFunctionRuleEpoch=0;g_desktopFunctionPostFailures=0;g_desktopFunctionConsumed=0;fixtureDesktopWake=0;
    g_oemKeyMask = 0; g_oemKeyMaskHid = 0; g_oemKeyMaskRog = 0;
    g_oemKeyMaskMsi = 0; g_oemKeyMaskMsiDinput = 0;
    g_oemKeyHooksActive = true; g_gpdBackIfaceReady = true; g_gpdBackIfaceGen = 1;
    g_oemBackProfileId = 4; fixtureMsiAuthority = 0; fixturePostOk = true;
    g_hwnd = reinterpret_cast<HWND>(1); fixturePhysicalReceipts.clear(); fixtureReceiptTimes.clear(); fixtureMessageTime = static_cast<LONG>(GetTickCount64());
    g_oemShortcutEdgePostFailures = 0; g_curW = 0; g_curPad = {};
#if defined(YMCC_OEM_CONSUME_SOURCE)
    fixtureThreadId = 100; fixtureWindowThreadId = 100;
    fixtureHookInstalls = 0; fixtureHookRemoves = 0; fixtureHookInstallOk = true; fixtureReconcilePosts = 0;
    g_oemBackHook = reinterpret_cast<HHOOK>(1); g_oemMouseHook = reinterpret_cast<HHOOK>(2);
    g_oemKeyboardConsume = {};
#ifdef YMCC_OEM_MOUSE_CONSUME_SOURCE
    g_oemMouseConsume = {};
#endif
    g_oemKeyboardHookOwnerTid = GetCurrentThreadId(); g_oemHookNextInstallAttempt = 0;
    g_oemHooksWanted = true; g_oemConsumePolicyMask = 0; g_oemConsumePolicyEpoch = 0;
    g_oemConsumePolicyProfile = -1;
    g_oemConsumeMatched = 0; g_oemConsumeReplayed = 0; g_oemConsumeReplayFailures = 0; g_oemConsumeCancelled = 0;
    fixtureReplayed.clear(); fixtureSendLimit = UINT_MAX;
#ifdef YMCC_MSI_QS_MIRROR_SOURCE
    g_msiQsKeyboardMirrorPolicy=0;g_msiQsKeyboardMirrorSourceGeneration=0;
#endif
#ifdef YMCC_MSI_NATIVE_ONLY_REAR_SOURCE
    fixtureMsiKnownBoard=true;
#endif
#endif
#ifdef YMCC_SHORTCUT_NATIVE_OEM_CASE_MODE
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
#endif

}
#if defined(YMCC_WMI_CLICK_SOURCE)
// Mock only the external WMI property object. Real COM sink/producer/queue/lifecycle
// and action dispatch below run against it, with no WMI subscription or hook install.
class FixtureWmiObject final : public IWbemClassObject {
public:
    long code = 41;
    std::function<void()> beforeGet;
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetQualifierSet(IWbemQualifierSet **ppQualSet) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Get(LPCWSTR wszName, long lFlags, VARIANT *pVal, CIMTYPE *pType, long *plFlavor) override { if (beforeGet) beforeGet(); if (!pVal || std::wstring(wszName) != L"MSIEvt") return E_FAIL; VariantInit(pVal); pVal->vt = VT_I4; pVal->lVal = code; return S_OK; }
    HRESULT STDMETHODCALLTYPE Put(LPCWSTR wszName, long lFlags, VARIANT *pVal, CIMTYPE Type) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Delete(LPCWSTR wszName) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetNames(LPCWSTR wszQualifierName, long lFlags, VARIANT *pQualifierVal, SAFEARRAY * *pNames) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginEnumeration(long lEnumFlags) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Next(long lFlags, BSTR *strName, VARIANT *pVal, CIMTYPE *pType, long *plFlavor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndEnumeration(void) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPropertyQualifierSet(LPCWSTR wszProperty, IWbemQualifierSet **ppQualSet) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IWbemClassObject **ppCopy) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetObjectText(long lFlags, BSTR *pstrObjectText) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SpawnDerivedClass(long lFlags, IWbemClassObject **ppNewClass) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SpawnInstance(long lFlags, IWbemClassObject **ppNewInstance) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CompareTo(long lFlags, IWbemClassObject *pCompareTo) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPropertyOrigin(LPCWSTR wszName, BSTR *pstrClassName) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InheritsFrom(LPCWSTR strAncestor) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMethod(LPCWSTR wszName, long lFlags, IWbemClassObject **ppInSignature, IWbemClassObject **ppOutSignature) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE PutMethod(LPCWSTR wszName, long lFlags, IWbemClassObject *pInSignature, IWbemClassObject *pOutSignature) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DeleteMethod(LPCWSTR wszName) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginMethodEnumeration(long lEnumFlags) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE NextMethod(long lFlags, BSTR *pstrName, IWbemClassObject **ppInSignature, IWbemClassObject **ppOutSignature) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndMethodEnumeration(void) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMethodQualifierSet(LPCWSTR wszMethod, IWbemQualifierSet **ppQualSet) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMethodOrigin(LPCWSTR wszMethodName, BSTR *pstrClassName) override { return E_NOTIMPL; }
};
static uint64_t activateWmi() {
    msiWmiSetEnabled(true);
    g_msiWmiState = 2; // external async-subscription success boundary
    return g_msiWmiActiveGeneration.load();
}
static bool clickStatus(const char* status, const char* reason = nullptr) {
    return std::any_of(fixtureLogs.begin(),fixtureLogs.end(),[&](const json& e) {
        return e.value("event", "") == "oem-shortcut-click" && e.value("status", "") == status &&
            (!reason || e.value("reason", "") == reason);
    });
}
#endif
#if defined(YMCC_PRODUCER_EDGE_SOURCE)
// Physical device enumeration/binding is a boundary mock; production parsers,
// publication, queue/dispatcher and close functions are copied from current source.
static uint64_t activateProducer(OemProducerId id) {
    uint64_t generation = oemProducerBegin(id);
    switch (id) {
        case OemProducerId::Legion: g_legionBack.bound=true; g_legionBack.actionGeneration=generation; break;
        case OemProducerId::LegionS: g_legionSBack.bound=true; g_legionSBack.actionGeneration=generation; break;
        case OemProducerId::Gpd: g_gpdBack.bound=true; g_gpdBack.actionGeneration=generation; break;
        case OemProducerId::OneX: g_onexBack.bound=true; g_onexBack.actionGeneration=generation; break;
#if defined(YMCC_ROG_CLICK_SOURCE)
        case OemProducerId::RogClick: g_rogOemKey.bound=true;g_rogOemKey.actionGeneration=generation; break;
#endif
        case OemProducerId::Msi:
            if (!fixtureMsiAuthority) fixtureMsiAuthority=static_cast<int>(kOemBitM1|kOemBitM2);
            g_msiDinput.actionGeneration=generation; break;
        default: break;
    }
    return generation;
}
static void producerSample(OemProducerId id, uint64_t mask, uint64_t expected=0) {
    const uint64_t generation=expected ? expected : g_oemProducerGenerations[static_cast<size_t>(id)].load();
    BYTE data[64]{};
    switch (id) {
        case OemProducerId::Legion:
            data[0]=1; data[1]=1; // numbered raw report, HC offsets retained
            if(mask&kOemBitLegionL) data[20]|=0x10;
            if(mask&kOemBitLegionR) data[20]|=0x80;
            if(mask&kOemBitLegionY2) data[20]|=0x40;
            if(mask&kOemBitLegionY3) data[20]|=0x20;
            if(mask&kOemBitLegionM2) data[20]|=0x08;
            if(mask&kOemBitLegionM3) data[20]|=0x04;
            if(mask&kOemBitLegionFrontL) data[18]|=0x80;
            if(mask&kOemBitLegionFrontR) data[18]|=0x40;
            legionBackParse(data,sizeof(data),generation); break;
        case OemProducerId::LegionS:
            // Windows unnumbered report-ID 0 precedes the HC payload.
            if(mask&kOemBitLegionL) data[3]|=kLegionSBackBitLeft;
            if(mask&kOemBitLegionR) data[3]|=kLegionSBackBitRight;
            if(mask&kOemBitLegionFrontL) data[1]|=0x01;
            if(mask&kOemBitLegionFrontR) data[1]|=0x02;
            legionSBackParse(data,sizeof(data),generation); break;
        case OemProducerId::Gpd:
            if(mask&kOemBitL4) data[9]=kGpdBackMask;
            if(mask&kOemBitR4) data[10]=kGpdBackMask;
            gpdBackParse(data,sizeof(data),generation); break;
        case OemProducerId::OneX: {
            data[0]=kOnexBackBtnCmd; data[1]=data[62]=kOnexBackFrame;
            const uint64_t special=g_onexBack.desc.role21Orange ? kOemBitOrange : kOemBitSpecial;
            const std::pair<uint64_t,BYTE> buttons[]={{kOemBitL4,kOnexBackL4Id},{kOemBitR4,kOnexBackR4Id},{special,kOnexBackSpecialId},{kOemBitKeyboard,kOnexBackKeyboardId}};
            for (const auto& button:buttons) {
                data[6]=button.second; data[12]=(mask&button.first) ? 1 : 0;
                onexBackParse(data,sizeof(data),generation);
            }
            break;
        }
        case OemProducerId::Msi: (void)msiDinputShortcutPublish(mask,generation); break;
        default: break;
    }
}
static void closeProducer(OemProducerId id) {
    switch (id) {
        case OemProducerId::Legion: legionBackClose(); break;
        case OemProducerId::LegionS: legionSBackClose(); break;
        case OemProducerId::Gpd: gpdBackClose(); break;
        case OemProducerId::OneX: onexBackClose(); break;
        case OemProducerId::Msi: msiDinputShortcutCancel(); fixtureMsiAuthority=0; break;
        default: break;
    }
}
static json producerRule(const char* code, const char* trigger="press") {
    auto result=rule("os.openTouchKeyboard","unused",trigger,"oem",code);
    result["params"]=json::object(); return result;
}
#endif
static void tick(WORD buttons, ULONGLONG now) { nativeKeyboardShortcutsEvaluate(buttons, XINPUT_GAMEPAD{}, now); }
#if defined(YMCC_SHORTCUT_EDGE_SOURCE)
static void deliverPhysicalReceipts() {
    for (size_t i = 0; i < fixturePhysicalReceipts.size(); ++i) {
        fixtureMessageTime = fixtureReceiptTimes[i];
        nativeKeyboardShortcutHandleOemEdge(fixturePhysicalReceipts[i].first, fixturePhysicalReceipts[i].second);
    }
}
#endif
static void flushJobs() { auto jobs = std::move(fixtureJobs); fixtureJobs.clear(); for (auto& job : jobs) job.first(); }
#include "shortcut_summon_frontend_cases.h"
#if defined(YMCC_PRODUCER_EDGE_SOURCE) && defined(YMCC_SHORTCUT_EDGE_SOURCE)
// Both retained entry functions are production code. Only source delivery,
// library ownership, saved target state and final OS calls are fixture leaves.
// Selected post-admission boundary; inputs, parser, original source delivery,
// dispatch and consumed serial closure are actual production implementations.
#if defined(YMCC_KEYBOARD_WORKER_SOURCE)
static void nativeOemKeyboardWorkerCases(int selection) {
    unsigned ordinal=0;
    const auto saveWorkerCase=[&](const std::string& label,bool revokedBeforeStop) {
        const auto threadName=[](std::thread::id id){std::ostringstream out;out<<id;return out.str();};
        json rows=json::array();std::set<std::string> senders;
        {std::lock_guard<std::mutex> lock(fixtureKeyboardMx);for(const auto& entry:fixtureKeyboardTrace){const auto sender=threadName(entry.thread);senders.insert(sender);rows.push_back({{"vk",entry.vk},{"up",entry.up},{"accepted",entry.accepted},{"thread",sender}});}}
        const auto observer=threadName(std::this_thread::get_id());
        if(rows.empty()||senders.size()!=1||senders.count(observer))throw std::runtime_error("Worker trace lacks a distinct actual sender thread");
        std::ofstream("worker-case-"+std::to_string(ordinal)+".json")<<json{{"label",label},{"trace",rows},{"senderThreads",senders},{"observerThread",observer},{"revocationObservedBeforeStop",revokedBeforeStop},{"originalStopReturned",true},{"actualOsSendInput",false}}.dump(2);
    };
    const auto count=[&](bool up,bool accepted=true){std::lock_guard<std::mutex> lock(fixtureKeyboardMx);return std::count_if(fixtureKeyboardTrace.begin(),fixtureKeyboardTrace.end(),[&](const auto& e){return e.up==up&&e.accepted==accepted;});};
    const auto waitCount=[&](bool up,unsigned n,unsigned milliseconds){std::unique_lock<std::mutex> lock(fixtureKeyboardMx);return fixtureKeyboardCv.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return std::count_if(fixtureKeyboardTrace.begin(),fixtureKeyboardTrace.end(),[&](const auto& e){return e.up==up&&e.accepted;})>=n;});};
    const auto cleanFlags=[&]{g_inputReleaseRequired=false;fixtureCustomLibraryOwner=false;fixtureRadialOwner=false;g_shortcutRecordingActive=false;g_exitRequested=false;g_inputReady=true;fixtureShortcutSettingsThrow=false;fixtureKeyboardRejectUps=false;};
    const auto prepare=[&](bool mixed,const std::string& persona,bool physicalControl=false){
        nativeKeyboardSequenceStop();
        g_nativeKeyboardSequenceOwner.~NativeKeyboardSequenceOwner();new (&g_nativeKeyboardSequenceOwner) NativeKeyboardSequenceOwner;
        cleanFlags();resetRoute(json::array(),baseInput(false,persona));
        GamepadSerialEvent::RulesApplied.store(GamepadSerialEvent::RulesRequired.load());
        fixtureExternalForeground=reinterpret_cast<HWND>(42);fixtureWindowForeground=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=persona!="disabled";
        g_inputHostProcess=persona=="disabled"?nullptr:reinterpret_cast<HANDLE>(1);g_inputHostPrepared=g_inputHostNeutralized=persona!="disabled";
        activateProducer(OemProducerId::OneX);
        {std::lock_guard<std::mutex> lock(fixtureKeyboardMx);fixtureKeyboardTrace.clear();fixtureKeyboardPauseDown=true;fixtureKeyboardDownEntered=false;fixtureKeyboardResumeDown=false;}
        if(physicalControl)fixtureDesktopDown.insert(VK_LCONTROL);
        NativeKeyboardShortcutRule rule;rule.requiredMask=kOemBitL4|(mixed?XINPUT_GAMEPAD_A:0);rule.outputId="worker-"+std::to_string(++ordinal);
        rule.keyboardPlan.steps.push_back({{{VK_LCONTROL,false},{'7',false}},2000});rule.keyboardPlan.toggle=true;
        return rule;
    };
    const auto start=[&](const NativeKeyboardShortcutRule& rule,const std::string& label,uint64_t wmi=0,bool fallback=false){
        const auto tokens=oemProducerTokensFor(rule.requiredMask);
        check(nativeKeyboardSequenceDispatch(rule,g_oemShortcutEdgeEpoch.load(),wmi,0,fallback,tokens)&&fixtureJobs.size()==1,
            "keyboard actual dispatcher admits one serial job "+label);
        flushJobs();
        std::unique_lock<std::mutex> lock(fixtureKeyboardMx);check(fixtureKeyboardCv.wait_for(lock,std::chrono::seconds(2),[]{return fixtureKeyboardDownEntered;}),
            "actual independent worker reaches counted first key-down "+label);
    };
    const auto resume=[&]{std::lock_guard<std::mutex> lock(fixtureKeyboardMx);fixtureKeyboardResumeDown=true;fixtureKeyboardCv.notify_all();};
    const auto cancellation=[&](const std::string& boundary,bool mixed,const std::string& persona){
        auto rule=prepare(mixed,persona);
        uint64_t wmi=0;const bool fallback=boundary=="fallback-authority";
        if(boundary=="wmi")wmi=activateWmi();
        start(rule,persona+"/"+boundary,wmi,fallback);
        if(boundary=="release")g_inputReleaseRequired=true;
        else if(boundary=="child")fixtureCustomLibraryOwner=true;
        else if(boundary=="rules-published")GamepadSerialEvent::rulesPublished();
        else if(boundary=="recording")g_shortcutRecordingActive=true;
        else if(boundary=="exit")g_exitRequested=true;
        else if(boundary=="input-ready")g_inputReady=false;
        else if(boundary=="power")++fixturePowerGeneration;
        else if(boundary=="producer")oemProducerCancel(OemProducerId::OneX,g_oemProducerGenerations[static_cast<size_t>(OemProducerId::OneX)].load());
        else if(boundary=="source-epoch")++g_oemShortcutEdgeEpoch;
        else if(boundary=="foreground")fixtureExternalForeground=reinterpret_cast<HWND>(43);
        else if(boundary=="radial")fixtureRadialOwner=true;
        else if(boundary=="wmi"){fixtureWmiTerminal=true;msiWmiSetEnabled(false);}
        else if(boundary=="fallback-authority")activateWmi();
        resume();const bool released=waitCount(true,2,500);
        cleanFlags();
        // Engine must already be cancelled. Restoration supplies no new user trigger.
        const auto oldDowns=count(false);std::this_thread::sleep_for(std::chrono::milliseconds(70));
        const bool noRestart=count(false)==oldDowns;
        nativeKeyboardSequenceStop();
        saveWorkerCase(persona+"/"+boundary,released);
        check(released,"worker observes current revocation before original2000ms hold ends "+persona+"/"+boundary);
        check(noRestart&&oldDowns==2,"original Engine cancellation cannot revive after guard restoration "+persona+"/"+boundary);
        {std::lock_guard<std::mutex> lock(fixtureKeyboardMx);std::vector<WORD> ups;for(const auto& e:fixtureKeyboardTrace)if(e.up&&e.accepted)ups.push_back(e.vk);
            check(ups==std::vector<WORD>{'7',VK_LCONTROL},"cancelled worker releases only owned keys in reverse order "+persona+"/"+boundary);}
        check(count(false)==2&&count(true)==2,"joined original worker has no extra injected downs "+persona+"/"+boundary);
    };
    if(selection!=2){
        for(const std::string persona:{"disabled","steamdeck"})for(const std::string boundary:{"release","child"}){
            cancellation(boundary,true,persona);
            auto pure=prepare(false,persona);start(pure,persona+"/pure-"+boundary);
            if(boundary=="release")g_inputReleaseRequired=true;else fixtureCustomLibraryOwner=true;
            resume();check(waitCount(false,2,500),"pure OEM reaches both owned keys under controller gate "+persona+"/"+boundary);
            const bool earlyUp=waitCount(true,1,70);nativeKeyboardSequenceStop();saveWorkerCase(persona+"/pure-"+boundary,false);
            check(!earlyUp,"pure OEM has no new controller Host or foreground admission "+persona+"/"+boundary);
            check(count(true)==2,"explicit stop joins pure OEM worker and returns its owned keys "+persona+"/"+boundary);cleanFlags();
        }
    }
    if(selection!=1)cancellation("rules-published",false,"disabled");
    if(selection==0){
        for(const std::string boundary:{"recording","exit","input-ready","power","producer","source-epoch","foreground","radial","wmi","fallback-authority"})cancellation(boundary,false,"disabled");
        auto rule=prepare(true,"disabled");start(rule,"release-debt");fixtureKeyboardRejectUps=true;g_inputReleaseRequired=true;resume();
        {std::unique_lock<std::mutex> lock(fixtureKeyboardMx);check(fixtureKeyboardCv.wait_for(lock,std::chrono::seconds(2),[]{return std::any_of(fixtureKeyboardTrace.begin(),fixtureKeyboardTrace.end(),[](const auto& e){return e.up&&!e.accepted;});}),
            "actual revoked worker retains a rejected key-up debt");}
        ymcc::keyboardSequence::Plan later;later.steps.push_back({{{'8',false}},40});
        check(!g_nativeKeyboardSequenceOwner.submit("must-not-run-under-debt",later,[]{return true;}),"existing release debt blocks a new output slot");
        g_inputReleaseRequired=false;fixtureKeyboardRejectUps=false;
        check(waitCount(true,2,1000),"original release-only retry returns held keys after sender recovers");
        check(count(false)==2,"restored guard and repaired key-up do not replay cancelled input");
        rule.outputId="new-user-press-after-debt";rule.keyboardPlan=later;
        check(nativeKeyboardSequenceDispatch(rule,g_oemShortcutEdgeEpoch.load(),0,0,false,oemProducerTokensFor(rule.requiredMask)),"new explicit press can request output after debt repair");flushJobs();
        check(waitCount(false,3,500)&&waitCount(true,3,500),"new explicit request runs without replaying the rejected slot");nativeKeyboardSequenceStop();saveWorkerCase("release-debt",true);
        auto physical=prepare(false,"disabled",true);start(physical,"physical-Ctrl");g_inputReady=false;resume();
        const bool physicalReleased=waitCount(true,1,500);nativeKeyboardSequenceStop();saveWorkerCase("physical-Ctrl",physicalReleased);
        check(physicalReleased&&count(false)==1&&count(true)==1,"cancellation never releases a modifier already held physically");
        {std::lock_guard<std::mutex> lock(fixtureKeyboardMx);check(std::all_of(fixtureKeyboardTrace.begin(),fixtureKeyboardTrace.end(),[](const auto& e){return e.vk=='7';}),"owned cleanup sends no unrelated Control key-up");}
        const auto stopped=count(false)+count(true);std::this_thread::sleep_for(std::chrono::milliseconds(70));
        check(count(false)+count(true)==stopped,"real original worker stop joined before final no-late-send observation");cleanFlags();fixtureDesktopDown.clear();
    }
}
#endif
static void nativeOemUiMailboxCases() {
    unsigned requestCase=0; uint64_t sourceGeneration=0;
    const auto prepare=[&](int source,bool mixed,const std::string& persona) {
        auto savedRule=producerRule(source==0?"l4":source==1?"gamepad":"claw");
        savedRule["actionId"]="monitor.toggle";savedRule["params"]={{"operation","toggle"}};
        if(mixed)savedRule["inputs"].push_back({{"source","controller"},{"code","a"}});
        resetRoute(json::array({savedRule}),baseInput(false,persona));
        fixtureRadialOwner=false;fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;fixtureShortcutSettingsThrow=false;
        fixtureShortcutActionRequests.clear();
        g_shortcutActionMailbox.~ShortcutActionMailbox();
        new (&g_shortcutActionMailbox) ymcc::ShortcutActionMailbox("mailbox-case-"+std::to_string(++requestCase));
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=persona!="disabled";
        g_inputHostProcess=persona=="disabled"?nullptr:reinterpret_cast<HANDLE>(1);
        g_inputHostPrepared=g_inputHostNeutralized=persona!="disabled";
        g_machineIdentity.family=source==0?YmccFamily::OneXPlayer:source==1?YmccFamily::Gpd:YmccFamily::MsiClaw;
        g_machineIdentity.deviceClass=source==0?"HandheldCompanion.Devices.OneXPlayerMini":source==1?"HandheldCompanion.Devices.GPDWin5":"HandheldCompanion.Devices.ClawA1M";
        g_oemBackProfileId=static_cast<int>(OemChordProfileId::GpdWin5);
        sourceGeneration=source==0?activateProducer(OemProducerId::OneX):source==2?activateWmi():0;
        tick(0,fixtureTick);
        if(source==0){producerSample(OemProducerId::OneX,0);nativeOemProducerDrain();}
    };
    const auto deliver=[&](int source,bool down) {
        fixtureTick+=20;
        if(source==0){producerSample(OemProducerId::OneX,down?kOemBitL4:0);nativeOemProducerDrain();}
        else if(source==1){
            KBDLLHOOKSTRUCT key{};key.vkCode=VK_F13;key.flags=down?0:LLKHF_UP;
            oemBackHookProc(HC_ACTION,down?WM_KEYDOWN:WM_KEYUP,reinterpret_cast<LPARAM>(&key));
            deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
        }else if(down){msiWmiHandleEvent(kMsiWmiEventClaw,sourceGeneration);nativeMsiWmiShortcutDrain();}
    };
    const auto issue=[&](int source,bool mixed,const std::string& label) {
        const auto previous=fixtureShortcutActionRequests.size();
        g_curW=mixed?XINPUT_GAMEPAD_A:0;deliver(source,true);
        check(fixtureJobs.size()==1,"UI request uses original serial admission "+label);
        flushJobs();
        check(fixtureShortcutActionRequests.size()==previous+1,"actual UI dispatcher issues one request "+label);
        return fixtureShortcutActionRequests.back();
    };
    const json outcome={{"state","saved"},{"title","fixture"},{"message","request result only"}};
    for(const std::string persona:{"disabled","steamdeck"})for(int source=0;source<3;++source)
    for(const bool mixed:{true,false})for(const std::string gate:{"release","child","radial"}) {
        const std::string label=persona+(source==0?"/OneX-HID":source==1?"/GPD-keyboard":"/MSI-WMI")+(mixed?"/mixed/":"/pure/")+gate;
        prepare(source,mixed,persona);const auto saved=fixtureInput;
        const auto request=issue(source,mixed,label+"/initial");const auto id=request["id"].get<std::string>();
        check(g_shortcutActionMailbox.claim(id,GetTickCount64())==request&&g_shortcutActionMailbox.claim(id,GetTickCount64())==request,
            "healthy repeated verify retains existing mailbox semantics "+label);
        g_inputReleaseRequired=gate=="release";fixtureCustomLibraryOwner=gate=="child";fixtureRadialOwner=gate=="radial";
        const bool revoked=mixed||gate=="radial";
        const auto claimed=g_shortcutActionMailbox.claim(id,GetTickCount64());
        check(revoked?claimed.empty():claimed==request,"issued UI request respects current original owner gate "+label);
        g_inputReleaseRequired=false;fixtureCustomLibraryOwner=false;fixtureRadialOwner=false;
        const auto restored=g_shortcutActionMailbox.claim(id,GetTickCount64());
        check(revoked?restored.empty():restored==request,"observed UI request revocation cannot revive on owner return "+label);
        deliver(source,false);g_curW=0;tick(0,fixtureTick+1);
        const auto fresh=issue(source,mixed,label+"/fresh");const auto freshId=fresh["id"].get<std::string>();
        check(freshId!=id&&g_shortcutActionMailbox.claim(freshId,GetTickCount64())==fresh,
            "new physical request owns a separate current state "+label);
        check(!g_shortcutActionMailbox.complete(freshId,outcome,GetTickCount64(),nativeShortcutUnixMs()).empty()&&
            g_shortcutActionMailbox.complete(freshId,outcome,GetTickCount64(),nativeShortcutUnixMs()).empty(),
            "one result consumes the actual mailbox entry "+label);
        check(fixtureInput==saved&&fixtureRefreshes==0,"UI current guard preserves saved virtual intent "+label);
        if(source==2){fixtureWmiTerminal=true;msiWmiSetEnabled(false);}
    }
    for(const std::string boundary:{"rule-disable","source-epoch","power","input-ready","recording","exit","producer","wmi","settings-read-error"}) {
        const int source=boundary=="wmi"?2:0;prepare(source,false,"disabled");
        const auto request=issue(source,false,"boundary/"+boundary);const auto id=request["id"].get<std::string>();
        check(!g_shortcutActionMailbox.claim(id,GetTickCount64()).empty(),"UI boundary begins with a current actual claim "+boundary);
        if(boundary=="rule-disable"){
            fixtureInput["buttonMapping"]["rules"]["shortcutRules"][0]["enabled"]=false;
            fixtureInput["revision"]=fixtureInput["revision"].get<long long>()+1;GamepadSerialEvent::rulesPublished();nativeKeyboardShortcutsRefresh(fixtureTick+300);
        }else if(boundary=="source-epoch")++g_oemShortcutEdgeEpoch;
        else if(boundary=="power")++fixturePowerGeneration;
        else if(boundary=="input-ready")g_inputReady=false;
        else if(boundary=="recording")g_shortcutRecordingActive=true;
        else if(boundary=="exit")g_exitRequested=true;
        else if(boundary=="producer")oemProducerCancel(OemProducerId::OneX,sourceGeneration);
        else if(boundary=="wmi"){fixtureWmiTerminal=true;msiWmiSetEnabled(false);}
        else fixtureShortcutSettingsThrow=true;
        bool threw=false;json rejected;try{rejected=g_shortcutActionMailbox.claim(id,GetTickCount64());}catch(...){threw=true;}
        check(!threw&&rejected.empty(),"UI source or read failure rejects the original claim "+boundary);
        g_inputReady=true;g_shortcutRecordingActive=false;g_exitRequested=false;fixtureShortcutSettingsThrow=false;
        check(g_shortcutActionMailbox.claim(id,GetTickCount64()).empty(),"UI failed request stays revoked across later guard copies "+boundary);
        check(g_shortcutActionMailbox.complete(id,outcome,GetTickCount64(),nativeShortcutUnixMs()).empty(),"cancelled UI request cannot fabricate a successful result "+boundary);
    }
    prepare(0,true,"disabled");const auto concurrent=issue(0,true,"shared-copy");const auto concurrentId=concurrent["id"].get<std::string>();
    g_inputReleaseRequired=true;
    auto first=std::async(std::launch::async,[&]{return g_shortcutActionMailbox.claim(concurrentId,GetTickCount64());});
    auto second=std::async(std::launch::async,[&]{return g_shortcutActionMailbox.claim(concurrentId,GetTickCount64());});
    const auto one=first.get(),two=second.get();g_inputReleaseRequired=false;
    check(one.empty()&&two.empty()&&g_shortcutActionMailbox.claim(concurrentId,GetTickCount64()).empty(),
        "concurrent actual claim copies share one observed request revocation");
    ymcc::ShortcutActionMailbox capacity("unchanged-capacity");
    for(unsigned n=0;n<16;++n)check(!capacity.issue("monitor.toggle",{{"operation","toggle"}},1,1,1000,100000,[]{return true;}).empty(),"original mailbox capacity slot "+std::to_string(n));
    check(capacity.issue("monitor.toggle",{{"operation","toggle"}},1,1,1000,100000,[]{return true;}).empty(),"original mailbox capacity rejects seventeenth pending request");
    check(!capacity.issue("monitor.toggle",{{"operation","toggle"}},1,1,31000,130000,[]{return true;}).empty()&&capacity.size()==1,
        "original mailbox TTL reclaims expired entries without a new timer");
}
#ifdef YMCC_SHORTCUT_TOKEN_INTERLEAVE
static void sampledTokenWindowCases(){
    unsigned cases=0;
    const auto setup=[](OemProducerId id,const char* code){
        auto savedRule=producerRule(code);resetRoute(json::array({savedRule}));fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        const auto generation=activateProducer(id);tick(0,fixtureTick);producerSample(id,0);nativeOemProducerDrain();
        check(!g_nativeKeyboardShortcuts[0].needsNeutral,"sampled boundary starts with armed real rule");return generation;
    };
    for(auto id:{OemProducerId::OneX,OemProducerId::Gpd,OemProducerId::Legion}){
        const char* code=id==OemProducerId::Legion?"legionr":"l4";
        const uint64_t bit=id==OemProducerId::Legion?kOemBitLegionR:kOemBitL4;
        for(bool rebind:{false,true}){
            const auto old=setup(id,code);producerSample(id,bit);g_oemProducerEdgeCount=0;fixtureJobs.clear();fixtureOwnActions.clear();
            bool cancelled=false;uint64_t replacement=0;
            fixtureTokenCaptureInterleave=[&]{cancelled=oemProducerCancel(id,old);if(rebind)replacement=activateProducer(id);};
            nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+10);
            const auto queued=fixtureJobs.size();flushJobs();
            check(cancelled,"actual sampled source cancels before token capture");
            check(queued==0&&fixtureOwnActions.empty(),"cancel or replacement cannot pair old mask with a new token");
            check(!g_nativeKeyboardShortcuts[0].needsNeutral,"rejected attempt preserves original established neutral state");
            if(!rebind)replacement=activateProducer(id);
            check(replacement&&replacement!=old,"normal replacement keeps fresh nonzero generation");
            producerSample(id,0);nativeOemProducerDrain();tick(0,fixtureTick+20);
            producerSample(id,bit);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==1,"fresh neutral and press recover once without restart");
            ++cases;
        }
        setup(id,code);producerSample(id,bit);g_oemProducerEdgeCount=0;fixtureJobs.clear();fixtureOwnActions.clear();
        nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+10);flushJobs();
        check(fixtureOwnActions.size()==1,"unchanged sampled producer still acts once");++cases;
    }
    // A retained keyboard edge without a producer generation is legitimate.
    auto savedRule=producerRule("gamepad");resetRoute(json::array({savedRule}));
    fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWin5";
    g_oemBackProfileId=static_cast<int>(OemChordProfileId::GpdWin5);tick(0,fixtureTick);
    KBDLLHOOKSTRUCT key{};key.vkCode=VK_F13;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&key));
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==1,"zero-producer keyboard fallback remains valid");++cases;
    std::cout<<"SAMPLED_TOKEN_BOUNDARIES "<<cases<<" checks="<<passed<<'\n';
}
static void freshRetainedAfterRejectedSample(){
    auto saved=producerRule("l4");resetRoute(json::array({saved}));fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    const auto old=activateProducer(OemProducerId::OneX);tick(0,fixtureTick);producerSample(OemProducerId::OneX,0);nativeOemProducerDrain();
    producerSample(OemProducerId::OneX,kOemBitL4);g_oemProducerEdgeCount=0;fixtureJobs.clear();fixtureOwnActions.clear();
    fixtureTokenCaptureInterleave=[&]{check(oemProducerCancel(OemProducerId::OneX,old),"real cancellation at sampled-token boundary");activateProducer(OemProducerId::OneX);};
    tick(0,fixtureTick+10);flushJobs();check(fixtureOwnActions.empty(),"stale sampled action rejected before replacement edge");
    producerSample(OemProducerId::OneX,kOemBitL4);nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"fresh retained source DOWN keeps established neutral without extra UP or restart");
}
#endif

#ifdef YMCC_SHORTCUT_TOKEN_INTERLEAVE
static void sampledTokenIndependentFenceCases(){
    // A replacement can publish the same authority bits: generation remains
    // an independent part of this rule's source observation.
    auto saved=producerRule("l4");resetRoute(json::array({saved}));fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    auto generation=activateProducer(OemProducerId::OneX);tick(0,fixtureTick);producerSample(OemProducerId::OneX,0);nativeOemProducerDrain();
    producerSample(OemProducerId::OneX,kOemBitL4);g_oemProducerEdgeCount=0;fixtureJobs.clear();fixtureOwnActions.clear();
    const auto authority=g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].load();
    fixtureTokenCaptureInterleave=[&]{
        check(oemProducerCancel(OemProducerId::OneX,generation),"generation-only boundary completes actual old cancel");
        generation=activateProducer(OemProducerId::OneX);producerSample(OemProducerId::OneX,0);
        check(g_oemProducerAuthorities[static_cast<size_t>(OemProducerId::OneX)].load()==authority,"replacement restores same authority but keeps distinct generation");
    };
    tick(0,fixtureTick+10);const auto queued=fixtureJobs.size();flushJobs();
    check(queued==0&&fixtureOwnActions.empty(),"different generation alone rejects old sampled mask");
    saved=producerRule("m1");resetRoute(json::array({saved}));fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    fixtureMsiAuthority=static_cast<int>(kOemBitM1);generation=activateProducer(OemProducerId::Msi);tick(0,fixtureTick);producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();
    producerSample(OemProducerId::Msi,kOemBitM1);g_oemProducerEdgeCount=0;fixtureJobs.clear();fixtureOwnActions.clear();
    fixtureTokenCaptureInterleave=[&]{fixtureMsiAuthority=0;producerSample(OemProducerId::Msi,0);
        check(g_oemProducerGenerations[static_cast<size_t>(OemProducerId::Msi)].load()==generation,"authority-only boundary keeps same producer generation");};
    tick(0,fixtureTick+10);const auto authorityQueued=fixtureJobs.size();flushJobs();
    check(authorityQueued==0&&fixtureOwnActions.empty(),"changed required authority alone rejects old sampled mask");
    std::cout<<"TOKEN_INDEPENDENT_FENCES "<<passed<<'\n';
}
#endif

#ifdef YMCC_SHORTCUT_OEM_DEMAND
using DemandEvaluator=void(*)(WORD,const XINPUT_GAMEPAD&,ULONGLONG,bool,const uint64_t*,bool,uint64_t,const OemProducerTokens*,const ymcc::input::desktopFunction::Edge*);
static void demandEval(DemandEvaluator fn,WORD buttons=0,ULONGLONG now=0,bool allowed=true,const uint64_t* receipt=nullptr){
 fn(buttons,XINPUT_GAMEPAD{},now?now:fixtureTick,allowed,receipt,false,0,nullptr,nullptr);
}
static json demandOwn(const char* source="controller",const char* code="a",const char* trigger="press"){
 auto r=rule("os.openTouchKeyboard","unused",trigger,source,code);r["params"]=json::object();return r;
}
static json demandDesktop(){auto r=demandOwn("keyboard","AltLeft");r["inputs"].push_back({{"source","keyboard"},{"code","Digit2"}});return r;}
static void demandSetup(const json& rules){
 resetRoute(rules,baseInput(false,"disabled"));fixtureCustomLibraryOwner=false;g_inputReleaseRequired=false;
 g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
 fixtureTick+=10000;
}
static json demandRuleState(){
 json rows=json::array();for(const auto& r:g_nativeKeyboardShortcuts)rows.push_back({{"required",r.requiredMask},{"desktop",r.desktopKeys},
 {"neutral",r.needsNeutral},{"matched",r.matchedLastTick},{"hold",r.holdStartedAt},{"fired",r.holdFired},{"press",r.lastPressAt},{"emit",r.lastEmitAt}});
 return rows;
}
static void demandReadCases(DemandEvaluator fn,bool optimized){
 struct Shape {const char* name;json rules;bool oem;};
 const auto mixed=[] {auto r=producerRule("l4");r["inputs"].push_back({{"source","controller"},{"code","a"}});return r;}();
 std::vector<Shape> shapes={
  {"desktop",json::array({demandDesktop()}),false},{"controller",json::array({demandOwn()}),false},
  {"oem",json::array({producerRule("l4")}),true},{"mixed",json::array({mixed}),true},
  {"oem-last",json::array({demandDesktop(),demandOwn(),producerRule("l4")}),true},
  {"oem-first",json::array({producerRule("l4"),demandOwn(),demandDesktop()}),true}};
 for(const auto& shape:shapes){
  demandSetup(shape.rules);demandEval(fn);check(g_nativeKeyboardShortcuts.size()==shape.rules.size(),std::string("demand actual parser selected ")+shape.name);
  fixtureDemandOemReads=0;demandEval(fn);check(fixtureDemandOemReads==unsigned(!optimized||shape.oem),std::string("demand live mask calls ")+shape.name);
  const auto state=demandRuleState();uint64_t receipt=0;fixtureDemandOemReads=0;demandEval(fn,0,0,true,&receipt);
  check(fixtureDemandOemReads==0,std::string("demand supplied receipt avoids live query ")+shape.name);
  check(demandRuleState()==state,std::string("demand neutral receipt state parity ")+shape.name);
 }
 demandSetup(json::array({demandDesktop()}));demandEval(fn);
 auto change=[&](const json& rules){fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=rules;fixtureInput["revision"]=fixtureInput["revision"].get<int>()+1;g_nativeKeyboardShortcutLastRefresh=0;fixtureDemandOemReads=0;demandEval(fn);};
 change(json::array({producerRule("l4")}));check(g_nativeKeyboardShortcuts.size()==1&&(g_nativeKeyboardShortcuts[0].requiredMask>>20),"demand refresh installs OEM consumer");check(fixtureDemandOemReads==1,"demand first OEM refresh resumes original live read");
 change(json::array({demandDesktop()}));check(g_nativeKeyboardShortcuts.size()==1&&g_nativeKeyboardShortcuts[0].requiredMask==0,"demand refresh returns to keyboard table");check(fixtureDemandOemReads==unsigned(!optimized),"demand removal stops unconsumed live read");
 demandSetup(json::array());fixtureDemandOemReads=0;demandEval(fn);check(fixtureDemandOemReads==0,"demand empty table retains original early return");
}
static void shortcutOemDemandCases(){demandReadCases(nativeKeyboardShortcutsEvaluate,true);}
#endif

static void nativeOemQueuedOwnerCases() {
    for (const std::string persona : {"disabled", "steamdeck"}) {
        for (int source = 0; source < 3; ++source) for (const bool mixed : {true, false}) {
            for (const bool libraryOwner : {false, true}) {
                const char* code = source == 0 ? "l4" : source == 1 ? "gamepad" : "claw";
                const std::string label = persona + (source == 0 ? "/OneX-HID" : source == 1 ? "/GPD-keyboard" : "/MSI-WMI-click") +
                    (mixed ? "/mixed" : "/pure-OEM") + (libraryOwner ? "/child-owner" : "/release-required");
                auto savedRule = producerRule(code);
                if (mixed) savedRule["inputs"].push_back({{"source", "controller"}, {"code", "a"}});
                resetRoute(json::array({savedRule}), baseInput(false, persona));
                fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
                g_realStickTest.enabled = g_realStickTest.virtualTargetEnabled = persona != "disabled";
                g_inputHostProcess = persona == "disabled" ? nullptr : reinterpret_cast<HANDLE>(1);
                g_inputHostPrepared = g_inputHostNeutralized = persona != "disabled";
                g_machineIdentity.family = source == 0 ? YmccFamily::OneXPlayer : source == 1 ? YmccFamily::Gpd : YmccFamily::MsiClaw;
                g_machineIdentity.deviceClass = source == 0 ? "HandheldCompanion.Devices.OneXPlayerMini" : source == 1 ? "HandheldCompanion.Devices.GPDWin5" : "HandheldCompanion.Devices.ClawA1M";
                g_oemBackProfileId = static_cast<int>(OemChordProfileId::GpdWin5);
                uint64_t generation = source == 0 ? activateProducer(OemProducerId::OneX) : source == 2 ? activateWmi() : 0;
                tick(0, fixtureTick);
                if (source == 0) { producerSample(OemProducerId::OneX, 0); nativeOemProducerDrain(); }
                const auto deliver = [&](bool down) {
                    fixtureTick += 20;
                    if (source == 0) { producerSample(OemProducerId::OneX, down ? kOemBitL4 : 0); nativeOemProducerDrain(); }
                    else if (source == 1) {
                        KBDLLHOOKSTRUCT key{}; key.vkCode = VK_F13; key.flags = down ? 0 : LLKHF_UP;
                        oemBackHookProc(HC_ACTION, down ? WM_KEYDOWN : WM_KEYUP, reinterpret_cast<LPARAM>(&key));
                        deliverPhysicalReceipts(); fixturePhysicalReceipts.clear(); fixtureReceiptTimes.clear();
                    } else if (down) { msiWmiHandleEvent(kMsiWmiEventClaw, generation); nativeMsiWmiShortcutDrain(); }
                };
                const auto saved = fixtureInput;
                g_curW = mixed ? XINPUT_GAMEPAD_A : 0; deliver(true);
                check(fixtureOwnActions.empty() && fixtureJobs.size() == 1, "queue owner begins with one actual admitted action " + label);
                fixtureCustomLibraryOwner = libraryOwner; g_inputReleaseRequired = !libraryOwner;
                flushJobs();
                check(fixtureOwnActions.size() == (mixed ? 0u : 1u), "queued mixed action checks owner while pure OEM remains independent " + label);
                if (mixed) check(std::any_of(fixtureLogs.begin(), fixtureLogs.end(), [](const json& row) {
                    return row.value("event", "") == "oem-shortcut-edge-delivery" && row.value("reason", "") == "queued-controller-owner-or-return";
                }), "queued owner rejection is present in ordinary evidence " + label);
                fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false; flushJobs();
                check(fixtureOwnActions.size() == (mixed ? 0u : 1u), "consumed queued cancellation cannot revive when owner returns " + label);
                deliver(false); g_curW = 0; tick(0, fixtureTick + 1); flushJobs();
                const auto prior = fixtureOwnActions.size();
                g_curW = mixed ? XINPUT_GAMEPAD_A : 0; deliver(true); flushJobs();
                check(fixtureOwnActions.size() == prior + 1, "fresh current input recovers after queued owner rejection " + label);
                check(fixtureInput == saved && fixtureRefreshes == 0, "queued owner preserves saved intent without a new Host " + label);
                fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
                if (source == 2) { fixtureWmiTerminal = true; msiWmiSetEnabled(false); }
            }
        }
    }
    // Exercise existing cancellation around a serial closure without repeating
    // transport matrices already accepted by the earlier source-generation suite.
    for (const std::string boundary : {"explicit-rule-disable", "recording", "exit", "producer-replaced"}) {
        auto savedRule = producerRule("l4");
        resetRoute(json::array({savedRule})); fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
        g_realStickTest.enabled = g_realStickTest.virtualTargetEnabled = false;
        g_inputHostProcess = nullptr; g_inputHostPrepared = g_inputHostNeutralized = false;
        const auto generation = activateProducer(OemProducerId::OneX);
        tick(0, fixtureTick); producerSample(OemProducerId::OneX, 0); nativeOemProducerDrain();
        producerSample(OemProducerId::OneX, kOemBitL4); nativeOemProducerDrain();
        check(fixtureJobs.size() == 1 && fixtureOwnActions.empty(), "queued lifecycle begins with admitted OEM job " + boundary);
        if (boundary == "explicit-rule-disable") {
            fixtureInput["buttonMapping"]["rules"]["shortcutRules"][0]["enabled"] = false;
            fixtureInput["revision"] = fixtureInput["revision"].get<long long>() + 1;
            GamepadSerialEvent::rulesPublished();
            nativeKeyboardShortcutsRefresh(fixtureTick + 300);
        } else if (boundary == "recording") g_shortcutRecordingActive = true;
        else if (boundary == "exit") g_exitRequested = true;
        else oemProducerCancel(OemProducerId::OneX, generation);
        flushJobs(); check(fixtureOwnActions.empty(), "existing queued cancellation remains effective " + boundary);
    }
}
static void nativeOemOwnerBoundaryCases() {
    for (const std::string persona : {"disabled", "steamdeck"}) {
        for (const bool keyboard : {false, true}) for (const bool libraryOwner : {false, true}) {
            const char* code = keyboard ? "gamepad" : "l4";
            const std::string label = persona + (keyboard ? "/keyboard" : "/OneX-HID") +
                (libraryOwner ? "/child-owner" : "/release-required");
            const auto configure = [&](bool mixed) {
                auto saved = producerRule(code);
                if (mixed) saved["inputs"].push_back({{"source", "controller"}, {"code", "a"}});
                resetRoute(json::array({saved}), baseInput(false, persona));
                fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
                g_machineIdentity.family = keyboard ? YmccFamily::Gpd : YmccFamily::OneXPlayer;
                g_machineIdentity.deviceClass = keyboard ? "HandheldCompanion.Devices.GPDWin5" : "HandheldCompanion.Devices.OneXPlayerMini";
                g_realStickTest.enabled = g_realStickTest.virtualTargetEnabled = persona != "disabled";
                g_inputHostProcess = persona == "disabled" ? nullptr : reinterpret_cast<HANDLE>(1);
                g_inputHostPrepared = g_inputHostNeutralized = persona != "disabled";
                g_oemBackProfileId = static_cast<int>(OemChordProfileId::GpdWin5);
                if (!keyboard) activateProducer(OemProducerId::OneX);
                tick(0, fixtureTick);
                if (!keyboard) { producerSample(OemProducerId::OneX, 0); nativeOemProducerDrain(); }
                check(g_nativeKeyboardShortcuts.size() == 1 && !g_nativeKeyboardShortcuts[0].needsNeutral,
                    "owner boundary starts from an armed actual rule " + label);
            };
            const auto source = [&](bool down) {
                fixtureTick += 20;
                if (keyboard) {
                    KBDLLHOOKSTRUCT key{}; key.vkCode = VK_F13; key.flags = down ? 0 : LLKHF_UP;
                    oemBackHookProc(HC_ACTION, down ? WM_KEYDOWN : WM_KEYUP, reinterpret_cast<LPARAM>(&key));
                    deliverPhysicalReceipts(); fixturePhysicalReceipts.clear(); fixtureReceiptTimes.clear();
                } else {
                    producerSample(OemProducerId::OneX, down ? kOemBitL4 : 0); nativeOemProducerDrain();
                }
                flushJobs();
            };
            configure(true);
            const auto saved = fixtureInput;
            g_curW = XINPUT_GAMEPAD_A;
            fixtureCustomLibraryOwner = libraryOwner; g_inputReleaseRequired = !libraryOwner;
            source(true);
            check(fixtureOwnActions.empty(), "mixed retained edge respects current controller owner " + label);
            fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
            nativeKeyboardShortcutsEvaluate(g_curW, g_curPad, fixtureTick + 1);
            flushJobs();
            check(fixtureOwnActions.empty(), "restoring owner cannot replay a held mixed chord " + label);
            source(false); g_curW = 0; tick(0, fixtureTick + 2);
            g_curW = XINPUT_GAMEPAD_A; source(true);
            check(fixtureOwnActions.size() == 1, "mixed rule recovers on fresh source after real neutral " + label);
            source(false);
            check(fixtureOwnActions.size() == 1 && fixtureInput == saved && fixtureRefreshes == 0,
                "mixed owner repair keeps one action and unchanged virtual intent " + label);
            configure(false);
            g_curW = XINPUT_GAMEPAD_A;
            fixtureCustomLibraryOwner = libraryOwner; g_inputReleaseRequired = !libraryOwner;
            source(true);
            check(fixtureOwnActions.size() == 1, "pure OEM action remains independent of controller owner " + label);
            source(false);
            fixtureCustomLibraryOwner = false; g_inputReleaseRequired = false;
        }
    }
}
#endif

#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
const json r12Defaults = json::parse(R"DEFAULTS([{"actionId":"window.summon","defaultKey":"enabled","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-enabled","inputs":[{"source":"controller","code":"lb"},{"source":"controller","code":"rb"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"window.hideToTray","defaultKey":"bDoubleMinimize","doubleWindowMs":500,"enabled":true,"holdMs":2000,"id":"native-bDoubleMinimize","inputs":[{"code":"b","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"double"},{"actionId":"keyboard.numeric","defaultKey":"startDoubleF7","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-startDoubleF7","inputs":[{"code":"start","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"key":"F7"},"trigger":"double"},{"actionId":"power.tdpAdjust","defaultKey":"tdpShortcut","doubleWindowMs":500,"enabled":true,"holdMs":0,"id":"native-tdpShortcut","inputs":[{"code":"start","source":"controller"},{"code":"dpadUp","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"amount":"1","direction":"up","linearAcceleration":true},"trigger":"repeat"},{"actionId":"display.brightnessAdjust","defaultKey":"fpsShortcut","doubleWindowMs":500,"enabled":true,"holdMs":0,"id":"native-fpsShortcut","inputs":[{"code":"start","source":"controller"},{"code":"dpadRight","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"amount":"5","direction":"right","linearAcceleration":true},"trigger":"repeat"},{"actionId":"game.killCurrent","defaultKey":"killGame","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-killGame","inputs":[{"code":"back","source":"controller"},{"code":"b","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"os.openTouchKeyboard","defaultKey":"openKeyboard","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-openKeyboard","inputs":[{"code":"back","source":"controller"},{"code":"x","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"os.returnDesktop","defaultKey":"returnDesktop","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-returnDesktop","inputs":[{"code":"back","source":"controller"},{"code":"a","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"press"},{"actionId":"mouse.toggle","defaultKey":"mouseToggle","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-mouseToggle","inputs":[{"code":"back","source":"controller"},{"code":"y","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"}])DEFAULTS");
static void startF7RetirementCases() {
    // Keep the historical enabled row to exercise the real consumer's retirement,
    // independently of frontend migration or the compatibility boolean.
    const auto replayDoubleStart = [] {
        tick(0,10000); tick(XINPUT_GAMEPAD_START,10100); tick(0,10140);
        tick(XINPUT_GAMEPAD_START,10200); tick(XINPUT_GAMEPAD_START,10220);
        tick(0,10240); tick(0,11000); flushJobs();
    };
    const auto retiredStartF7 = r12Defaults[2];
    resetRoute(json::array({retiredStartF7})); replayDoubleStart();
    std::cout << "retired Start F7 replay: F7=" << std::count_if(fixtureInjections.begin(),fixtureInjections.end(),
        [](const auto& sent){return sent.first==VK_F7;}) << " total=" << fixtureInjections.size() << "\n";
    check(fixtureInjections.empty() && fixtureJobs.empty() && fixtureOwnActions.empty(),
        "retired enabled native-default double Start never sends F7");
    check(g_nativeKeyboardShortcuts.empty() && g_nativeShortcutLegacyOverrideMask==0,
        "retired Start F7 adds no rule owner or other default override");
    auto customStartF7 = retiredStartF7;
    customStartF7["id"]="custom-double-start-f7"; customStartF7["origin"]="custom";
    resetRoute(json::array({customStartF7})); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.size()==1 && fixtureInjections.size()==1 &&
          fixtureInjections[0].first==VK_F7 && !fixtureInjections[0].second && fixtureRefreshes==0,
        "custom identical double Start F7 executes exactly once even with historical defaultKey");
    customStartF7.erase("defaultKey");
    resetRoute(json::array({retiredStartF7,customStartF7})); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.size()==1 && fixtureInjections.size()==1 &&
          fixtureInjections[0].first==VK_F7 && !fixtureInjections[0].second && fixtureRefreshes==0,
        "saved retired default beside custom double Start sends only the custom F7 once");
    auto untaggedStartF7=customStartF7; untaggedStartF7.erase("origin");
    resetRoute(json::array({untaggedStartF7})); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.size()==1 && fixtureInjections.size()==1 && fixtureInjections[0].first==VK_F7,
        "untagged user double Start F7 remains executable");
    auto otherDefaultF7=retiredStartF7; otherDefaultF7["defaultKey"]="enabled";
    resetRoute(json::array({otherDefaultF7})); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.size()==1 && fixtureInjections.size()==1 && fixtureInjections[0].first==VK_F7 &&
          !nativeShortcutLegacyDefaultAllowed("enabled"),
        "another native default edited to identical double Start F7 retains its rule owner");
    auto editedRetiredF7=retiredStartF7; editedRetiredF7["params"]["key"]="F8";
    resetRoute(json::array({editedRetiredF7})); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.empty() && fixtureInjections.empty() && g_nativeShortcutLegacyOverrideMask==0,
        "retirement follows exact default tags even when the historical key is edited");
    resetRoute(r12Defaults); replayDoubleStart();
    check(g_nativeKeyboardShortcuts.empty() && fixtureInjections.empty() && g_nativeShortcutLegacyOverrideMask==0,
        "R12 historical defaults retain eight legacy owners without retired Start F7");
}
#endif
// Selected family consumer group: each case executes the existing real rules,
// physical report/chord parser, queued receipt and serial own-action callback.
// No real Windows input, HID, motion activation or product process is used.
#if defined(YMCC_SHORTCUT_ONEX_BINDING_SOURCE)
static void ayaNativeConsumptionCases(bool extensionsOnly=false) {
    std::set<int> observedProfiles;
    unsigned legacyAirModels=0;
    for(const auto& cls:fixtureAyaClasses){
        const auto& binding=oemUnifiedInputBindingFor(YmccFamily::Ayanero,cls);
        if(extensionsOnly&&binding.model)continue;
        const bool oldAir=cls=="HandheldCompanion.Devices.AYANEOAIR"||
            cls=="HandheldCompanion.Devices.AYANEOAIRLite"||cls=="HandheldCompanion.Devices.AYANEOAIRPro";
        if(!oldAir&&observedProfiles.count(binding.profileId))continue;
        observedProfiles.insert(binding.profileId);legacyAirModels+=oldAir;
        const auto& profile=oemChordProfileFor(binding.profileId);
        check(binding.catalog.count>0&&profile.count>0,"AYA exact existing model/profile/catalog: "+cls);
        for(int index=0;index<profile.count;++index){
            const auto& chord=profile.rules[index];const char* code=nullptr;
            for(int k=0;k<binding.catalog.count;++k){uint64_t bit=0;
                if(nativeKeyboardShortcutMaskBit(binding.catalog.keys[k].keyId,bit)&&bit==chord.bit){code=binding.catalog.keys[k].keyId;break;}}
            check(code!=nullptr,"AYA actual chord has a declared action ID: "+cls);
            const auto prepare=[&](bool configured){
                resetRoute(configured?json::array({producerRule(code)}):json::array(),baseInput(false,"disabled"));
                g_machineIdentity.family=YmccFamily::Ayanero;g_machineIdentity.deviceClass=cls;
                g_oemBackProfileId=binding.profileId;tick(0,fixtureTick);
            };
            const auto emit=[&](bool injected){
                if(chord.mouseChord){
                    MSLLHOOKSTRUCT mouse{};mouse.flags=injected?LLMHF_INJECTED:0;
                    oemBackMouseHookProc(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&mouse));mouse.mouseData=XBUTTON2<<16;
                    oemBackMouseHookProc(HC_ACTION,WM_XBUTTONDOWN,reinterpret_cast<LPARAM>(&mouse));
                    oemBackMouseHookProc(HC_ACTION,WM_XBUTTONUP,reinterpret_cast<LPARAM>(&mouse));
                    oemBackMouseHookProc(HC_ACTION,WM_LBUTTONUP,reinterpret_cast<LPARAM>(&mouse));
                }else{
                    uint32_t remaining=chord.keys;std::vector<DWORD> keys;
                    for(DWORD vk=1;vk<256;++vk){const auto bit=oemBackHookVkBit(vk);if(bit&&(remaining&bit)){keys.push_back(vk);remaining&=~bit;}}
                    check(!remaining&&!keys.empty(),"AYA chord uses existing VK decoder: "+cls+"/"+code);
                    for(const auto vk:keys){++fixtureTick;KBDLLHOOKSTRUCT key{};key.vkCode=vk;key.flags=injected?LLKHF_INJECTED:0;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&key));}
                    for(auto it=keys.rbegin();it!=keys.rend();++it){++fixtureTick;KBDLLHOOKSTRUCT key{};key.vkCode=*it;key.flags=LLKHF_UP|(injected?LLKHF_INJECTED:0);oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&key));}
                }
                deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
            };
            const std::string label=cls+"/"+code+"/source-"+std::to_string(index);
            prepare(true);const auto saved=fixtureInput;emit(false);
            check(fixtureOwnActions.size()==1,"AYA no-Host bound chord dispatches once: "+label);
            tick(0,fixtureTick+10);flushJobs();
            check(fixtureOwnActions.size()==1,"AYA neutral and release do not replay action: "+label);
            check(!g_realStickTest.virtualTargetEnabled&&!g_inputHostProcess&&!g_inputHostPrepared&&fixtureRefreshes==0&&fixtureInput==saved,
                "AYA action retains native-only intent and settings: "+label);
            prepare(false);emit(false);check(fixtureOwnActions.empty(),"AYA unconfigured source invents no action: "+label);
            prepare(true);emit(true);check(fixtureOwnActions.empty()&&g_oemKbDown==0,"AYA injected source is not promoted to physical: "+label);
        }
    }
    const std::set<int> expected={int(OemChordProfileId::Ayaneo2021),int(OemChordProfileId::Ayaneo3),int(OemChordProfileId::AyaneoAir),
        int(OemChordProfileId::AyaneoCec),int(OemChordProfileId::AyaneoFlip1S),int(OemChordProfileId::AyaneoFlip1SKb),
        int(OemChordProfileId::AyaneoFlipDs),int(OemChordProfileId::AyaneoKun),int(OemChordProfileId::AyaneoNext),int(OemChordProfileId::AyaneoNext2)};
    const std::set<int> extensionProfiles={int(OemChordProfileId::Ayaneo3),int(OemChordProfileId::AyaneoNext2),
        int(OemChordProfileId::AyaneoCec),int(OemChordProfileId::AyaneoFlip1SKb)};
    check(extensionsOnly?(observedProfiles==extensionProfiles&&legacyAirModels==0):(observedProfiles==expected&&legacyAirModels==3),
        extensionsOnly?"AYA explicit extensions keep existing family fallback without frozen model entries":"AYA covers ten existing profiles and all three legacy AIR model bindings");
}
#if defined(YMCC_SHORTCUT_GATE_CASE_MODE)
#ifdef YMCC_SHORTCUT_FUNCTION_SUBSET
static void desktopFunctionSubsetCases() {
    static_assert(VK_F13==0x7c && VK_F24==0x87);
    std::array<bool,512> saved{};std::copy(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),saved.begin());
    const auto savedPublished=g_desktopFunctionPhysicalKeys.load();unsigned layouts=0,singleGoldens=0;
    const auto clear=[] {std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),false);};
    const auto validate=[&](int one=-1) {
        uint32_t expected=0;
        for(unsigned slot=0;slot<512;++slot)if(g_oemKeyboardConsume.physicalDown[slot]){
            const unsigned vk=slot&255u;if(vk>=0x7c&&vk<=0x87)expected|=1u<<(vk-0x7c);
        }
        const auto actual=nativeDesktopFunctionPhysicalKeys();
        if(actual!=expected||g_desktopFunctionPhysicalKeys.load(std::memory_order_acquire)!=expected)
            throw std::runtime_error("function subset value/publication mismatch layout="+std::to_string(layouts));
        if(one>=0){const unsigned vk=static_cast<unsigned>(one)&255u;const auto golden=vk>=0x7c&&vk<=0x87?1u<<(vk-0x7c):0u;
            if(actual!=golden)throw std::runtime_error("function subset independent single-slot golden mismatch");++singleGoldens;}
        ++layouts;
    };
    clear();validate();
    std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),true);validate();
    for(unsigned slot=0;slot<512;++slot)g_oemKeyboardConsume.physicalDown[slot]=(slot&255u)<0x7c||(slot&255u)>0x87;validate();
    for(unsigned slot=0;slot<512;++slot){clear();g_oemKeyboardConsume.physicalDown[slot]=true;validate(static_cast<int>(slot));}
    for(unsigned mask=0;mask<4096;++mask){clear();const auto extended=((mask<<1)|(mask>>11))&0xfffu;
        for(unsigned i=0;i<12;++i){g_oemKeyboardConsume.physicalDown[VK_F13+i]=(mask&(1u<<i))!=0;g_oemKeyboardConsume.physicalDown[VK_F13+i+256]=(extended&(1u<<i))!=0;}validate();}
    std::uint32_t random=0x137a6c51u;
    for(unsigned i=0;i<256;++i){for(auto& down:g_oemKeyboardConsume.physicalDown){random^=random<<13;random^=random>>17;random^=random<<5;down=(random&7u)==0;}validate();}
    std::copy(saved.begin(),saved.end(),std::begin(g_oemKeyboardConsume.physicalDown));g_desktopFunctionPhysicalKeys.store(savedPublished,std::memory_order_release);
    check(layouts==4867&&singleGoldens==512,"function subset ignores all unrelated keys and preserves extended releases/publication");
    std::cout<<"FUNCTION_SUBSET_LAYOUTS "<<layouts<<" singleGoldens="<<singleGoldens<<'\n';
}
#endif

#ifdef YMCC_SHORTCUT_FIFO_REJECTED_NEW_DOWN
static void fifoBoundaries(const std::string& mode){
    const auto explicitRule=[](const char* code,const char* trigger="press"){
        auto r=rule("os.openTouchKeyboard","unused",trigger,"keyboard",code);r["params"]=json::object();return r;
    };
    const auto setup=[&](const char* code,const char* trigger="press"){
        resetRoute(json::array({explicitRule(code,trigger)}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog-control";
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false,DWORD flags=0){
        ++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.scanCode=0x63;k.flags=flags|(up?LLKHF_UP:0);
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));
    };

    const auto fill=[&](){for(unsigned n=0;n<32;++n){key(VK_F18);key(VK_F18,true);}};
    if(mode=="overflow"||mode=="all"){
    setup("F18");for(unsigned n=0;n<32;++n){key(VK_F18);key(VK_F18,true);}
    check(g_desktopFunctionEdges.size()==64&&fixtureDesktopWake==1,"one wake owns a strictly bounded 64-edge FIFO");
    check(key(VK_F18)==0&&key(VK_F18,true)==0,"full FIFO leaves a new original pair visible");
    nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==32,"bounded FIFO consumes each admitted short press once");

    }
    if(mode=="hold"||mode=="all"){
        setup("F18");fill();const auto stamp=g_desktopFunctionCancellations[5].load();
        check(key(VK_F18)==0,"overflow rejected new DOWN remains forwarded");
        check(g_desktopFunctionPresses[5].blocked&&!g_desktopFunctionPresses[5].active&&g_desktopFunctionKeys.load()==0,"overflow hold owns no admitted key");
        for(unsigned n=0;n<100;++n)check(key(VK_F18)==0,"rejected same-slot repeat stays forwarded");
        check(g_desktopFunctionEdges.size()==64&&g_desktopFunctionCancellations[5].load()==stamp,"rejected repeat creates no receipt or cancellation");
        nativeDesktopFunctionDrain();flushJobs();
        check(fixtureOwnActions.size()==32&&g_desktopFunctionEdges.size()==0,"drain while rejected hold preserves exactly32 old pairs");
        tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.size()==32,"rejected held input never becomes phantom action");
        check(key(VK_F18,true)==0&&!g_desktopFunctionPresses[5].blocked,"real rejected-hold UP remains forwarded and clears block");
        tick(0,fixtureTick+1);check(key(VK_F18)==1&&key(VK_F18,true)==1,"next fresh pair is accepted after real neutral");
        nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==33,"fresh pair executes exactly once");
    }
    if(mode=="dual"||mode=="all"){
        for(unsigned bank=0;bank<2;++bank){
            setup("F18");const DWORD a=bank?LLKHF_EXTENDED:0,b=bank?0:LLKHF_EXTENDED;
            check(key(VK_F18,false,a)==1&&key(VK_F18,false,b)==1&&key(VK_F18,true,a)==1,"dual-bank old hold setup admitted");
            auto r=rule("os.openTouchKeyboard","unused","press","keyboard","F19");r["params"]=json::object();
            // Fill through the real FIFO API without changing rule epochs.
            ymcc::input::desktopFunction::Edge filler;
            while(g_desktopFunctionEdges.size()<64)check(g_desktopFunctionEdges.publish(filler,[]{return true;}),"existing FIFO filler admitted");
            const auto stamp=g_desktopFunctionCancellations[5].load();
            check(!g_desktopFunctionPresses[5].active&&g_oemKeyboardConsume.physicalDown[VK_F18+(bank?0:256)],"other bank remains physically held after original bank UP");
            check(key(VK_F18,false,a)==0&&g_desktopFunctionCancellations[5].load()==stamp+1,"opposite-bank held overflow retains original cancellation");
            key(VK_F18,true,a);key(VK_F18,true,b);g_desktopFunctionEdges={};
        }
    }
    if(mode=="failure"||mode=="all"){
        setup("F18");check(key(VK_F18)==1,"active hold is admitted before lost-UP control");
        ymcc::input::desktopFunction::Edge filler;
        while(g_desktopFunctionEdges.size()<64)g_desktopFunctionEdges.publish(filler,[]{return true;});
        auto stamp=g_desktopFunctionCancellations[5].load();
        check(key(VK_F18,true)==1&&g_desktopFunctionCancellations[5].load()==stamp+1&&!g_desktopFunctionPresses[5].active,"full FIFO lost-UP still cancels but preserves paired suppression");
        g_desktopFunctionEdges={};flushJobs();check(fixtureOwnActions.empty(),"cancelled active hold has no queued action");
        setup("F18");fixturePostOk=false;stamp=g_desktopFunctionCancellations[5].load();
        check(key(VK_F18)==0&&g_desktopFunctionEdges.size()==0&&g_desktopFunctionCancellations[5].load()==stamp+1,"empty FIFO wake failure still cancels");
        fixturePostOk=true;key(VK_F18,true);
        for(const auto reason:{"rule","power","recording","exit"}){
            setup("F18");fill();key(VK_F18);
            if(std::string(reason)=="rule"){fixtureInput["revision"]=90;fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();tick(0,fixtureTick+401);}
            if(std::string(reason)=="power")++fixturePowerGeneration;
            if(std::string(reason)=="recording")g_shortcutRecordingActive=true;
            if(std::string(reason)=="exit")g_exitRequested=true;
            key(VK_F18,true);nativeDesktopFunctionDrain();flushJobs();
            check(fixtureOwnActions.empty(),std::string("overflow-preserved receipts still respect final veto: ")+reason);
            g_exitRequested=false;g_shortcutRecordingActive=false;
        }
    }
}
#endif
static void nativeDesktopFunctionCases() {
#ifdef YMCC_SHORTCUT_FUNCTION_SUBSET
    desktopFunctionSubsetCases();
#endif
    const auto explicitRule=[](const char* code,const char* trigger="press"){
        auto r=rule("os.openTouchKeyboard","unused",trigger,"keyboard",code);r["params"]=json::object();return r;
    };
    const auto setup=[&](const char* code,const char* trigger="press"){
        resetRoute(json::array({explicitRule(code,trigger)}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog-control";
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false,DWORD flags=0){
        ++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.scanCode=0x63;k.flags=flags|(up?LLKHF_UP:0);
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));
    };
    for(unsigned index=0;index<12;++index){
        const auto code="F"+std::to_string(13+index);const DWORD vk=VK_F13+index;setup(code.c_str());
        check(g_oemRawPathActive && g_oemHooksWanted && g_desktopFunctionHookActive,"explicit global keyboard can coexist with ROG device-qualified Raw OEM source: "+code);
        check(key(vk)==1 && key(vk)==1 && key(vk,true)==1,"bound DOWN/repeat/UP are swallowed: "+code);
        check(g_oemKbDown==0&&g_oemKeyMask==0,"global LL does not republish ROG raw OEM source: "+code);
        check(g_desktopFunctionEdges.size()==2 && fixtureDesktopWake==1,"short pair retains exactly two edges and one posted wake: "+code);
        tick(0,fixtureTick+100);check(fixtureOwnActions.empty(),"live neutral cannot overtake retained short pair: "+code);
        nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==1,"one very short global keyboard pair executes the bound native action: "+code);
        check(fixtureReplayed.empty()&&!oemKeyboardConsumeBusy()&&g_desktopFunctionEdges.size()==0,"paired original input is returned without replay debt: "+code);
        check(!g_inputHostProcess&&!g_inputHostPrepared&&fixtureRefreshes==0,"explicit key requires no virtual Host or refresh: "+code);
    }
    setup("F18");g_inputReleaseRequired=true;fixtureCustomLibraryOwner=true;
    check(key(VK_F18)==1&&key(VK_F18,true)==1,"pure keyboard receipt has no controller release/source prerequisite");
    nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==1,"pure global keyboard action works while controller input is disallowed");
    g_inputReleaseRequired=false;fixtureCustomLibraryOwner=false;
    setup("F18");check(key('A')==0&&key('A',true)==0&&key(VK_F17)==0&&key(VK_F17,true)==0,"ordinary and unbound function keys pass");
    check(key(VK_F18,false,LLKHF_INJECTED)==0&&key(VK_F18,true,LLKHF_INJECTED)==0&&g_desktopFunctionEdges.size()==0,"injected keys cannot own physical global function receipts");
    setup("F18");fixturePostOk=false;check(key(VK_F18)==0&&key(VK_F18,true)==0,"failed UI wake preserves original input instead of swallowing it");
    fixturePostOk=true;nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.empty()&&g_desktopFunctionEdges.size()==0,"failed post creates no phantom bound action");
    setup("F18");for(unsigned n=0;n<32;++n){key(VK_F18);key(VK_F18,true);}
    check(g_desktopFunctionEdges.size()==64&&fixtureDesktopWake==1,"one wake owns a strictly bounded 64-edge FIFO");
    check(key(VK_F18)==0&&key(VK_F18,true)==0,"full FIFO leaves a new original pair visible");
    nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==32,"bounded FIFO consumes each admitted short press once");
    for(const auto reason:{"rule","power","recording","exit"}){
        setup("F18");check(key(VK_F18)==1,"revocation control begins with one consumed DOWN");
        if(std::string(reason)=="rule"){fixtureInput["revision"]=90;fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();tick(0,fixtureTick+401);}
        if(std::string(reason)=="power")++fixturePowerGeneration;
        if(std::string(reason)=="recording")g_shortcutRecordingActive=true;
        if(std::string(reason)=="exit")g_exitRequested=true;
        check(key(VK_F18,true)==1,"revocation still swallows the already-owned original UP");
        nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.empty(),std::string("revoked captured action is not replayed: ")+reason);
        g_exitRequested=false;g_shortcutRecordingActive=false;
    }
    setup("F18","release");key(VK_F18);nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.empty(),"release action waits for actual physical UP");
    key(VK_F18,true);nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==1,"release rule uses retained real UP");
    setup("F18","double");key(VK_F18);key(VK_F18,true);nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.empty(),"first click waits for explicit double rule");
    key(VK_F18);key(VK_F18,true);nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==1,"second complete short pair fires original double rule once");
    setup("F18","hold");key(VK_F18);nativeDesktopFunctionDrain();tick(0,fixtureTick+501);flushJobs();check(fixtureOwnActions.size()==1,"hold uses original timer budget and retained physical state");
    key(VK_F18,true);nativeDesktopFunctionDrain();tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.size()==1,"physical UP ends the held rule without repeat");
#ifdef YMCC_SHORTCUT_FIFO_REJECTED_NEW_DOWN
    fifoBoundaries("hold");fifoBoundaries("dual");fifoBoundaries("failure");
#endif
    std::cout<<"DESKTOP_CHECKS "<<passed<<'\n';
}

static void nativeDesktopFunctionBoundaryCases() {
    const auto r=[](const char* code,const char* trigger="press"){
        auto value=rule("os.openTouchKeyboard","unused",trigger,"keyboard",code);value["params"]=json::object();return value;
    };
    const auto setup=[&](const char* trigger="hold"){
        resetRoute(json::array({r("F18",trigger),r("F17")}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.scanCode=0x63;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    setup();fixturePostOk=false;check(key(VK_F18)==0,"failed DOWN wake leaves original held key visible");
    fixturePostOk=true;tick(0,fixtureTick+1000);flushJobs();
    check(fixtureOwnActions.empty(),"failed held DOWN cannot reappear as live hold action");
    check(key(VK_F18)==0,"failed held repeat is not acquired later");key(VK_F18,true);tick(0,fixtureTick+1);
    check(key(VK_F18)==1,"fresh physical DOWN recovers after failed original pair released");
    nativeDesktopFunctionDrain();tick(0,fixtureTick+501);flushJobs();check(fixtureOwnActions.size()==1,"fresh recovered hold retains original budget");key(VK_F18,true);nativeDesktopFunctionDrain();
    setup();key(VK_F18);nativeDesktopFunctionDrain();
    for(unsigned n=0;n<32;++n){key(VK_F17);key(VK_F17,true);}
    check(g_desktopFunctionEdges.size()==64,"FIFO can fill while a different already-consumed DOWN remains held");
    check(key(VK_F18,true)==1,"lost UP receipt still pairs the original swallowed UP");
    nativeDesktopFunctionDrain();tick(0,fixtureTick+1000);flushJobs();
    check(fixtureOwnActions.size()==32,"lost held UP cancels its gesture without deleting other accepted key presses");
    check(!g_nativeKeyboardShortcuts.front().matchedLastTick&&!g_nativeKeyboardShortcuts.front().holdFired,"lost UP cannot leave matched or held state in matcher");
    setup();for(unsigned n=0;n<32;++n){key(VK_F17);key(VK_F17,true);}
    check(key(VK_F18)==0,"full FIFO rejects a fresh held DOWN without swallowing it");
    nativeDesktopFunctionDrain();tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.size()==32,"full-FIFO rejected held DOWN is never live-sampled into an action");key(VK_F18,true);
    setup("press");key(VK_F18);key(VK_F18,true);nativeDesktopFunctionDrain();check(fixtureJobs.size()==1,"accepted edge reaches original serial executor");
    ++fixturePowerGeneration;flushJobs();check(fixtureOwnActions.empty(),"final queued action rechecks original shared power");
    setup("press");key(VK_F18);key(VK_F18,true);nativeDesktopFunctionDrain();g_powerLifecycle=PowerLifecycle::Suspended;flushJobs();
    check(fixtureOwnActions.empty(),"nonReady revocation without generation change cancels final action");g_powerLifecycle=PowerLifecycle::Ready;
    for(const bool recording:{false,true}) {
        setup();key(VK_F18);nativeDesktopFunctionDrain();
        if(recording)g_shortcutRecordingActive=true;else g_powerLifecycle=PowerLifecycle::Suspended;
        tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.empty(),"recording or sleep revokes actual held gesture");
        g_shortcutRecordingActive=false;g_powerLifecycle=PowerLifecycle::Ready;
        tick(0,fixtureTick+1500);flushJobs();check(fixtureOwnActions.empty(),"return to Ready or recording-off does not revive old held gesture");
        key(VK_F18,true);tick(0,fixtureTick+1);check(key(VK_F18)==1,"new real press recovers after revoked key released");
        nativeDesktopFunctionDrain();tick(0,fixtureTick+501);flushJobs();check(fixtureOwnActions.size()==1,"new post-revocation gesture can act");key(VK_F18,true);nativeDesktopFunctionDrain();
    }
    setup("press");fixtureDesktopDown.insert(VK_F18);g_oemKeyboardConsume={};oemKeyboardConsumeSeedPassedKeys();
    tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.empty(),"already-held installation snapshot never creates a new action");
    check(key(VK_F18)==0&&key(VK_F18,true)==0,"pre-held original pair remains passed");fixtureDesktopDown.clear();
    tick(0,fixtureTick+1);check(key(VK_F18)==1&&key(VK_F18,true)==1,"new pair after pre-held release can be consumed");nativeDesktopFunctionDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"post-preheld fresh pair dispatches exactly once");
    std::cout<<"DESKTOP_BOUNDARY_CHECKS "<<passed<<'\n';
}

static void nativeDesktopFunctionFinalCases(bool tail=false) {
    const auto r=[](const char* trigger="press",const char* code="F14"){
        auto value=rule("os.openTouchKeyboard","unused",trigger,"keyboard",code);value["params"]=json::object();return value;
    };
    const auto setup=[&](const char* trigger="press",bool nativeOem=false){
        auto rows=json::array({r(trigger)});
        if(nativeOem)rows.push_back(producerRule("l4"));
        resetRoute(rows,baseInput(false,"disabled"));g_inputHostProcess=nullptr;g_inputHostPrepared=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=nativeOem?YmccFamily::Gpd:YmccFamily::AsusRogAllyX;
        g_machineIdentity.deviceClass=nativeOem?"HandheldCompanion.Devices.GPDWin5":"HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=!nativeOem;g_rog.hasControl=!nativeOem;g_rog.controlPath=L"qualified-rog";
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    if(!tail){
    setup();check(key(VK_F14)==1&&key(VK_F14,true)==1,"final source still owns a fresh short pair");nativeDesktopFunctionDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"final source dispatches the short pair once");
    setup("release");key(VK_F14);nativeDesktopFunctionDrain();key(VK_F14,true);
    check(g_nativeKeyboardShortcuts.front().matchedLastTick&&g_desktopFunctionEdges.size()==1,"release window has matched DOWN and queued real UP");
    g_shortcutRecordingActive=true;g_oemShortcutEdgeEpoch.fetch_add(1);g_shortcutRecordingActive=false;g_oemShortcutEdgeEpoch.fetch_add(1);
    nativeDesktopFunctionDrain();tick(0,fixtureTick+1000);flushJobs();
    check(fixtureOwnActions.empty()&&!g_nativeKeyboardShortcuts.front().matchedLastTick,"recording start-stop without Evaluate rejects queued old UP and cannot reconstruct a release");
    key(VK_F14);key(VK_F14,true);nativeDesktopFunctionDrain();flushJobs();check(fixtureOwnActions.size()==1,"new release gesture recovers after old recording epoch");
    setup("hold");key(VK_F14);nativeDesktopFunctionDrain();
    g_shortcutRecordingActive=true;g_oemShortcutEdgeEpoch.fetch_add(1);g_shortcutRecordingActive=false;g_oemShortcutEdgeEpoch.fetch_add(1);
    tick(0,fixtureTick+1000);flushJobs();check(fixtureOwnActions.empty(),"held recording start-stop without a sample cannot revive old hold");
    key(VK_F14,true);
    setup();key(VK_F14);key(VK_F14,true);nativeDesktopFunctionDrain();
    // A healthy read-only publication is not a revocation. The fake shared
    // reader intentionally has no publication counter; that real ABI invariant
    // is additionally covered by its existing pure guards, not faked here.
    check(fixtureJobs.size()==1,"final source retains action on original queued executor");
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;GamepadSerialEvent::ConfigurationRevision.fetch_add(1);flushJobs();
    check(fixtureOwnActions.empty(),"configuration change between edge drain and serial action cancels it");
    setup("press",true);check(key(VK_F14)==1,"explicit F14 admitted on existing Win5 LL source");
    check(g_oemKeyMask==0&&fixturePhysicalReceipts.empty(),"same admitted LL event is not republished as OEM L4");
    key(VK_F14,true);nativeDesktopFunctionDrain();deliverPhysicalReceipts();flushJobs();
    check(fixtureOwnActions.size()==1,"one LL event with explicit and OEM definitions has one owner and one action");
    check(g_nativeKeyboardShortcuts.size()==2,"source ownership never deletes either stored definition");
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({producerRule("l4")});fixtureInput["revision"]=77;tick(0,fixtureTick+401);
    key(VK_F14);key(VK_F14,true);deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
    check(fixtureOwnActions.size()==2,"removing explicit rule returns next real pair to existing OEM path");
    }
    setup("press",true);fixturePostOk=false;const auto refused=key(VK_F14);
    check(refused==0 || !fixtureReplayed.empty(),"failed explicit post preserves original input through pass or original OEM replay");fixturePostOk=true;
    check(g_desktopFunctionLlOwned==0,"failed explicit admission cannot remove OEM key contribution");key(VK_F14,true);
    setup();g_oemKbDownRaw=kVkF18;oemBackHookRecompute();fixtureDesktopDown.insert(VK_F14);g_oemKeyboardConsume={};oemKeyboardConsumeSeedPassedKeys();
    check(g_oemKbDown==0&&g_oemKbDownRaw==kVkF18&&g_oemKeyMask==kOemBitM1,"preheld desktop seed cannot publish an LL duplicate over independent ROG raw source");
    fixtureDesktopDown.clear();key(VK_F14,true);
    std::cout<<"DESKTOP_FINAL_CHECKS "<<passed<<'\n';
}

static void nativeDesktopFunctionProofCases(bool tail=false,bool finalOnly=false) {
    const auto setup=[](const char* trigger="press"){
        auto r=rule("os.openTouchKeyboard","unused",trigger,"keyboard","F18");r["params"]=json::object();
        resetRoute(json::array({r}),baseInput(false,"disabled"));g_inputHostProcess=nullptr;g_inputHostPrepared=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;g_machineIdentity.resolved=true;
        g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"rog";
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=VK_F18;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    if(!tail){
    setup();check(key()==1&&key(true)==1,"retained explicit pair is suppressed before original state sampling");
    nativeDesktopFunctionDrain();g_powerLifecycle=PowerLifecycle::Suspended;flushJobs();
    check(fixtureOwnActions.empty(),"original shared power witness is checked at actual queued action");g_powerLifecycle=PowerLifecycle::Ready;
    setup();key();key(true);nativeDesktopFunctionDrain();++fixturePublicationRevision;flushJobs();
    check(fixtureOwnActions.size()==1,"healthy publication-only change does not revoke the original press");
    }
    if(!finalOnly){
    setup("hold");fixturePostOk=false;key();fixturePostOk=true;
    tick(0,fixtureTick+1000);tick(0,fixtureTick+1600);flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral&&g_nativeKeyboardShortcuts.front().holdStartedAt==0,"failed held DOWN waits for real neutral and cannot become a new press or hold");
    key(true);
    setup();key();key(true);fixtureDesktopDiagnosticThrows=true;nativeDesktopFunctionDrain();fixtureDesktopDiagnosticThrows=false;flushJobs();
    check(fixtureOwnActions.size()==1&&g_desktopFunctionEdges.size()==0,"new diagnostic exception cannot interrupt FIFO or change accepted action");
    }
    setup();key();key(true);fixtureTick+=401;fixtureShortcutSettingsThrow=true;nativeDesktopFunctionDrain();fixtureShortcutSettingsThrow=false;
    tick(0,fixtureTick+1000);flushJobs();
    check(fixtureOwnActions.empty()&&g_desktopFunctionEdges.size()==0,"settings exception cancels its receipt and cannot wedge the FIFO");
    setup();const auto before=g_desktopFunctionPostFailures.load();g_desktopFunctionCaptureMask=0;g_desktopFunctionKeys=0;g_desktopFunctionLlOwned=0;g_desktopFunctionEdges={};
    nativeDesktopFunctionMaintain();check(g_desktopFunctionPostFailures==before,"disabled explicit feature adds no queued work");
    std::cout<<"DESKTOP_PROOF_CHECKS "<<passed<<'\n';
}


static void nativeDesktopFunctionTeardownCases() {
    const auto setup=[] {
        auto r=rule("os.openTouchKeyboard","unused","press","keyboard","F14");r["params"]=json::object();
        resetRoute(json::array({r}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWin5";g_gpdBackIfaceReady=true;
        g_oemRawPathActive=false;g_oemRawAdmitDev=nullptr;
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    setup();check(g_desktopFunctionHookActive&&g_oemBackHook,"actual Win5 native gate installs explicit capture");
    check(key('A')==0&&key(VK_F14)==0&&g_desktopFunctionEdges.size()==1&&
        (g_desktopFunctionLlOwned.load()&ymcc::input::desktopFunction::keyBit(VK_F14)),"passed ordinary key leaves an admitted explicit gesture unconsumed");
    nativeDesktopFunctionDrain();check(fixtureJobs.size()==1,"old explicit gesture reaches actual serial queue before teardown");
    oemBackMappingSetEnabled(false);
    check(!g_oemBackHook&&!g_oemMouseHook&&!oemKeyboardConsumeBusy(),"passed-only ledger can actually retire without synthetic releases");
    fixtureDesktopDown.clear(); // physical releases occur while no hook exists
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({producerRule("l4")});
    fixtureInput["revision"]=90;fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;
    tick(0,fixtureTick);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    flushJobs();check(fixtureOwnActions.empty(),"retired explicit action cannot revive in the new OEM hook generation");
    const auto first=key(VK_F14);
    check(first==1&&(g_oemKeyMask.load()&kOemBitL4)!=0,"first fresh OEM F14 DOWN survives passed explicit teardown and reinstall");
    check(key(VK_F14,true)==1,"first fresh OEM F14 UP remains paired and suppressed");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,"fresh first OEM pair executes once with no virtual target");
    setup();check(key(VK_F14)==1,"paired debt control starts with a swallowed explicit DOWN");
    oemBackMappingSetEnabled(false);
    check(g_oemBackHook&&oemKeyboardConsumeBusy()&&(g_desktopFunctionLlOwned.load()&ymcc::input::desktopFunction::keyBit(VK_F14)),"disable retains original swallowed UP debt and same LL owner");
    check(key(VK_F14,true)==1,"disabled hook still swallows its original owned UP");
    oemBackMappingReconcileOnUi();
    check(!g_oemBackHook&&!oemKeyboardConsumeBusy()&&g_desktopFunctionLlOwned.load()==0,"LL ownership clears only with the actual retired ledger");
}


static void nativeDesktopFunctionTeardownGateCases() {
    auto r=rule("os.openTouchKeyboard","unused","press","keyboard","F18");r["params"]=json::object();
    resetRoute(json::array({r,producerRule("m1")}),baseInput(false,"disabled"));
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
    tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    check(g_oemRawPathActive&&g_oemBackHook&&g_desktopFunctionHookActive,"same ROG actual gate keeps qualified Raw OEM and explicit F18 LL");
    g_oemKbDownRaw=oemBackHookVkBit(VK_F17);oemBackHookRecompute();
    const auto rawBefore=g_oemKbDownRaw.load();
    check(key('A')==0&&key(VK_F18)==0&&g_desktopFunctionEdges.size()==1,"ROG explicit F18 is admitted but passed with an ordinary passed key");
    nativeDesktopFunctionDrain();check(fixtureJobs.size()==1,"ROG old explicit action is queued before settings removal");
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({producerRule("m1")});
    fixtureInput["revision"]=91;fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;
    tick(0,fixtureTick);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();
    check(!g_oemBackHook&&!g_oemMouseHook&&g_oemRawPathActive&&g_oemKbDownRaw==rawBefore,"removing only explicit rule actually retires LL and preserves the independent Raw state");
    fixtureDesktopDown.clear(); // physical UPs happen without a hook callback
    fixtureRogReferenceReady=false;fixtureTick+=501;
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    check(g_oemBackHook&&!g_oemRawPathActive&&g_oemKbDownRaw==0,"same ROG identity failure uses real LL fallback and original Raw withdrawal");
    flushJobs();check(fixtureOwnActions.empty(),"removed explicit action cannot revive through actual ROG source fallback");
    check(key(VK_F18)==1&&(g_oemKeyMask.load()&kOemBitM1)!=0,"first fresh ROG OEM F18 DOWN survives actual fallback and configured consumption");
    check(key(VK_F18,true)==1,"ROG fallback UP retains matching original consumed-pair ownership");
    deliverPhysicalReceipts();flushJobs();
    check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,"first ROG OEM pair executes once without a virtual target");
}

static json sampledDesktopRule(const json& inputs,const char* trigger="press") {
    auto r=rule("os.openTouchKeyboard","unused",trigger,"keyboard","F11");
    r["params"]=json::object();r["inputs"]=inputs;return r;
}
static void sampledDesktopSetup(const json& value,const std::set<WORD>& held={}) {
    resetRoute(json::array({value}),baseInput(false,"disabled"));
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    fixtureDesktopDown=held;
    nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},++fixtureTick,false);
}
static void sampledDesktopTick(unsigned elapsed=1,bool allowed=false,WORD buttons=0) {
    fixtureTick+=elapsed;nativeKeyboardShortcutsEvaluate(buttons,XINPUT_GAMEPAD{},fixtureTick,allowed);
}
static auto recordedMouseInputs(const char* first,const char* second=nullptr,bool keyboardFirst=false) {
    auto inputs=json::array({{{"source",keyboardFirst?"keyboard":"mouse"},{"code",first}}});
    if(second)inputs.push_back({{"source","mouse"},{"code",second}});
    return inputs;
}
static void recordedMouseSetup(const json& inputs,const char* trigger="press") {
    sampledDesktopSetup(sampledDesktopRule(inputs,trigger));
    g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Ayn;
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.AynLoki";
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
}
static LRESULT recordedMouse(WORD vk,bool up=false,DWORD flags=0) {
    ++fixtureTick;MSLLHOOKSTRUCT m{};m.pt={500,300};m.flags=flags;
    WPARAM message=vk==VK_LBUTTON?(up?WM_LBUTTONUP:WM_LBUTTONDOWN):
        vk==VK_RBUTTON?(up?WM_RBUTTONUP:WM_RBUTTONDOWN):
        vk==VK_MBUTTON?(up?WM_MBUTTONUP:WM_MBUTTONDOWN):(up?WM_XBUTTONUP:WM_XBUTTONDOWN);
    if(vk==VK_XBUTTON1||vk==VK_XBUTTON2)m.mouseData=DWORD(vk==VK_XBUTTON1?XBUTTON1:XBUTTON2)<<16;
    return oemBackMouseHookProc(HC_ACTION,message,reinterpret_cast<LPARAM>(&m));
}
static LRESULT recordedMouseKeyboard(WORD vk,bool up=false,DWORD flags=0) {
    ++fixtureTick;KBDLLHOOKSTRUCT key{};key.vkCode=vk;key.scanCode=vk;key.flags=flags|(up?LLKHF_UP:0);
    return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&key));
}
static void recordedMouseDrain() {
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();nativeDesktopFunctionDrain();flushJobs();
}
static void nativeRecordedMousePulseCases() {
    struct Item{const char* code;WORD vk;};
    for(const auto item:{Item{"MouseLeft",VK_LBUTTON},Item{"MouseRight",VK_RBUTTON},Item{"MouseMiddle",VK_MBUTTON},
                         Item{"MouseBack",VK_XBUTTON1},Item{"MouseForward",VK_XBUTTON2}}){
        recordedMouseSetup(recordedMouseInputs(item.code));const auto saved=fixtureInput;
        const auto down=recordedMouse(item.vk),up=recordedMouse(item.vk,true);recordedMouseDrain();
        check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,
            "retained mouse pulse dispatches once without a virtual Host: "+std::string(item.code));
        check(down==1&&up==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy()&&fixtureInput==saved,
            "typed mouse pulse owns both original edges and preserves its saved source: "+std::string(item.code));
    }
    for(const bool reverse:{false,true}){
        recordedMouseSetup(recordedMouseInputs("MouseLeft","MouseForward"));
        auto oem=producerRule("guide");fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(oem);
        ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
        const auto first=reverse?VK_XBUTTON2:VK_LBUTTON,second=reverse?VK_LBUTTON:VK_XBUTTON2;
        const auto a=recordedMouse(first),b=recordedMouse(second),c=recordedMouse(second,true),d=recordedMouse(first,true);
        recordedMouseDrain();
        check(a==1&&b==1&&c==1&&d==1&&fixtureReplayed.empty()&&fixtureOwnActions.size()==1&&g_oemKeyMask==0,
            "explicit mouse pair has one LL interpretation while same-named OEM Guide remains separate: "+std::to_string(reverse));
        check(!oemKeyboardConsumeBusy()&&g_desktopFunctionEdges.size()==0,
            "explicit two-mouse pair completes with zero delivery debt: "+std::to_string(reverse));
    }
    for(const bool reverse:{false,true}){
        recordedMouseSetup(recordedMouseInputs("ShiftRight","MouseForward",true));
        const auto a=reverse?recordedMouse(VK_XBUTTON2):recordedMouseKeyboard(VK_RSHIFT);
        const auto b=reverse?recordedMouseKeyboard(VK_RSHIFT):recordedMouse(VK_XBUTTON2);
        const auto c=reverse?recordedMouseKeyboard(VK_RSHIFT,true):recordedMouse(VK_XBUTTON2,true);
        const auto d=reverse?recordedMouse(VK_XBUTTON2,true):recordedMouseKeyboard(VK_RSHIFT,true);
        recordedMouseDrain();
        check(a==1&&b==1&&c==1&&d==1&&fixtureOwnActions.size()==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy(),
            "keyboard-mouse chord retains either arrival order without sampled input: "+std::to_string(reverse));
    }
    recordedMouseSetup(recordedMouseInputs("MouseBack"),"release");recordedMouse(VK_XBUTTON1);recordedMouseDrain();
    check(fixtureOwnActions.empty(),"saved mouse release action waits for the actual release");
    recordedMouse(VK_XBUTTON1,true);recordedMouseDrain();check(fixtureOwnActions.size()==1,"saved mouse release action executes once on retained actual UP");
    recordedMouseSetup(recordedMouseInputs("MouseMiddle"),"double");recordedMouse(VK_MBUTTON);recordedMouse(VK_MBUTTON,true);recordedMouseDrain();
    check(fixtureOwnActions.empty(),"saved mouse double uses the original first-click window");
    recordedMouse(VK_MBUTTON);recordedMouse(VK_MBUTTON,true);recordedMouseDrain();check(fixtureOwnActions.size()==1,"saved mouse second pulse executes one configured double action");
    recordedMouseSetup(recordedMouseInputs("MouseRight"),"hold");recordedMouse(VK_RBUTTON);recordedMouseDrain();sampledDesktopTick(501);flushJobs();
    check(fixtureOwnActions.size()==1,"saved held mouse uses the original hold evaluator");recordedMouse(VK_RBUTTON,true);recordedMouseDrain();
    check(!oemKeyboardConsumeBusy(),"saved held mouse returns only the actual paired UP");
}
static void nativeRecordedMousePublicationCases() {
    recordedMouseSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);nativeDesktopFunctionDrain();
    g_inputSignalMouseHookInstalled.store(false,std::memory_order_release);flushJobs();
    check(g_oemMouseHook&&fixtureOwnActions.empty(),
        "queued mouse action consumes atomic hook publication rather than the UI-only HHOOK");
    recordedMouseSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard('L');nativeDesktopFunctionDrain();
    g_inputSignalMouseHookInstalled.store(false,std::memory_order_release);flushJobs();
    check(fixtureOwnActions.size()==1,"mouse publication revocation leaves independent queued keyboard work valid");
    recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
}
static void nativeRecordedMouseIndependentOwnerCases() {
    recordedMouseSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(producerRule("guide"));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    recordedMouse(VK_LBUTTON);recordedMouse(VK_XBUTTON2);recordedMouseDrain();
    check(fixtureOwnActions.size()==1&&g_oemMouseConsume.visibility[0]==3,
        "independent-mouse boundary starts with a real earlier OEM-owned pair");fixtureOwnActions.clear();
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(sampledDesktopRule(recordedMouseInputs("MouseBack")));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(sampledDesktopRule(recordedMouseInputs("MouseLeft")));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);nativeDesktopFunctionDrain();
    const auto a=recordedMouse(VK_LBUTTON,true),b=recordedMouse(VK_XBUTTON2,true);recordedMouseDrain();
    check(fixtureOwnActions.size()==1,
        "returning old OEM Left pair preserves an independent queued MouseBack action");
    check(a==1&&b==1&&!oemKeyboardConsumeBusy()&&g_oemKeyMask==0,
        "old OEM UPs stay owned without a phantom newly saved MouseLeft action");
}
static void nativeRecordedMousePriorOwnerCases() {
    recordedMouseSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(producerRule("guide"));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    recordedMouse(VK_LBUTTON);recordedMouse(VK_XBUTTON2);recordedMouseDrain();
    check(fixtureOwnActions.size()==1&&g_oemMouseConsume.visibility[0]==3&&g_oemMouseConsume.visibility[1]==3,
        "prior OEM Guide owns its actual held mouse pair before explicit mouse edit");
    fixtureOwnActions.clear();
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(sampledDesktopRule(recordedMouseInputs("MouseLeft")));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard('L');nativeDesktopFunctionDrain();
    check(recordedMouse(VK_LBUTTON,true)==1&&recordedMouse(VK_XBUTTON2,true)==1,
        "prior OEM Guide keeps both original UPs after a new explicit mouse definition");
    recordedMouseDrain();check(fixtureOwnActions.size()==1,
        "prior OEM mouse owner revokes only mouse-dependent work and preserves queued pure-keyboard action");
    recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
    check(!oemKeyboardConsumeBusy()&&g_oemKeyMask==0,
        "prior OEM and explicit source ownership return independently without hidden debt");
}
static void nativeRecordedMousePartialHookCases() {
    recordedMouseSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(sampledDesktopRule(recordedMouseInputs("MouseBack")));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();
    g_oemBackHook=g_oemMouseHook=nullptr;g_desktopFunctionHookActive=false;g_oemHookNextInstallAttempt=0;
    fixtureMouseHookInstallOk=false;oemBackMappingReconcileOnUi();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    const auto a=recordedMouseKeyboard(VK_F11),b=recordedMouseKeyboard('L');
    const auto c=recordedMouseKeyboard('L',true),d=recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
    check(g_oemBackHook&&!g_oemMouseHook&&fixtureOwnActions.size()==1,
        "unrelated failed mouse hook cannot revoke an actual retained pure-keyboard chord");
    check(a==1&&b==1&&c==1&&d==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy(),
        "partial-hook source keeps original keyboard swallowing and paired UP completion");
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard('L');nativeDesktopFunctionDrain();
    fixtureMouseHookInstallOk=true;g_oemHookNextInstallAttempt=0;oemBackMappingReconcileOnUi();flushJobs();
    check(g_oemMouseHook&&fixtureOwnActions.size()==2,
        "restoring the unrelated mouse hook preserves already queued pure-keyboard action");
    recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
}
static void mouseOnlyHookSetup(const json& inputs,const char* trigger="press",const std::set<WORD>& held={}) {
    fixtureKeyboardHookInstallOk=fixtureMouseHookInstallOk=true;
    recordedMouseSetup(inputs,trigger);
    g_oemBackHook=g_oemMouseHook=nullptr;g_desktopFunctionHookActive=false;g_oemKeyHooksActive=false;
    fixtureKeyboardHookInstallOk=false;fixtureDesktopDown=held;g_oemHookNextInstallAttempt=0;
    oemBackMappingReconcileOnUi();sampledDesktopTick();
    if(g_oemBackHook||!g_oemMouseHook||g_desktopFunctionHookActive||!g_inputSignalMouseHookInstalled)
        throw std::runtime_error("actual keyboard-failed/mouse-ready installation ports not reached");
}
static void retryKeyboardForMouse(bool success,const std::set<WORD>& sampledHeld={}) {
    fixtureKeyboardHookInstallOk=success;fixtureDesktopDown=sampledHeld;
    g_oemHookNextInstallAttempt=0;fixtureTick+=501;oemBackMappingReconcileOnUi();
}
static void nativeRecordedMouseOnlyHookCases() {
    struct Item{const char* code;WORD vk;};
    for(const auto item:{Item{"MouseLeft",VK_LBUTTON},Item{"MouseRight",VK_RBUTTON},Item{"MouseMiddle",VK_MBUTTON},
                         Item{"MouseBack",VK_XBUTTON1},Item{"MouseForward",VK_XBUTTON2}}){
        mouseOnlyHookSetup(recordedMouseInputs(item.code));const auto saved=fixtureInput;
        const auto a=recordedMouse(item.vk),b=recordedMouse(item.vk,true);
        check(a==1&&b==1&&g_desktopFunctionEdges.size()==2,
            "mouse-only source retains and consumes both short edges without keyboard hook: "+std::string(item.code));
        recordedMouseDrain();
        check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,
            "mouse-only source actually executes its saved action without virtual Host: "+std::string(item.code));
        check(!oemKeyboardConsumeBusy()&&fixtureReplayed.empty()&&fixtureInput==saved,
            "mouse-only paired completion preserves stored source and leaves no hidden debt: "+std::string(item.code));
    }
    mouseOnlyHookSetup(recordedMouseInputs("MouseBack"));
    auto f13=sampledDesktopRule(json::array({{{"source","keyboard"},{"code","F13"}}}));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(f13);
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    fixtureDesktopDown={VK_F13};sampledDesktopTick();flushJobs();
    check(!g_desktopFunctionHookActive&&g_oemMouseHook&&fixtureOwnActions.size()==1,
        "mouse-only installation keeps independent F13 keyboard sampled fallback available");

    mouseOnlyHookSetup(recordedMouseInputs("ShiftRight","MouseForward",true));
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(producerRule("guide"));
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    const auto a=recordedMouse(VK_LBUTTON),b=recordedMouse(VK_XBUTTON2);recordedMouseDrain();
    check(a==1&&b==1&&fixtureOwnActions.size()==1&&g_desktopFunctionEdges.size()==0&&
          !g_desktopChordLlOwned.has(VK_XBUTTON2),
        "unavailable mixed rule does not steal the surviving actual OEM Guide mouse source");
    retryKeyboardForMouse(true,{VK_LBUTTON,VK_XBUTTON2});
    check(g_oemMouseConsume.visibility[0]==3&&g_oemMouseConsume.visibility[1]==3,
        "successful keyboard attach preserves both old OEM-owned mouse DOWNs");
    const auto c=recordedMouse(VK_XBUTTON2,true),d=recordedMouse(VK_LBUTTON,true);fixtureDesktopDown.clear();recordedMouseDrain();
    check(c==1&&d==1&&fixtureOwnActions.size()==1&&!oemKeyboardConsumeBusy(),
        "new mixed route observes neutral while the old OEM owner still receives both original UPs");
    sampledDesktopTick();recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_XBUTTON2);
    recordedMouse(VK_XBUTTON2,true);recordedMouseKeyboard(VK_RSHIFT,true);recordedMouseDrain();
    check(fixtureOwnActions.size()==2&&!oemKeyboardConsumeBusy(),
        "mixed binding starts only after both sources are ready and a new actual pair arrives");
    fixtureKeyboardHookInstallOk=fixtureMouseHookInstallOk=true;
    nativeRecordedMousePartialHookCases();
}
static void nativeRecordedMouseOnlyRetryCases() {
    for(const bool success:{false,true}){
        mouseOnlyHookSetup(recordedMouseInputs("MouseBack"));
        const auto down=recordedMouse(VK_XBUTTON1);const auto queue=g_desktopFunctionEdges.size();
        retryKeyboardForMouse(success,{VK_XBUTTON1,VK_F11});
        check(down==1&&g_oemKeyboardConsume.physicalDown[VK_XBUTTON1]&&
              g_oemKeyboardConsume.visibility[VK_XBUTTON1]==3&&g_desktopChordLlOwned.has(VK_XBUTTON1)&&
              g_desktopFunctionEdges.size()==queue,
            "keyboard retry preserves surviving mouse physical/visibility/FIFO/owner: "+std::to_string(success));
        check(bool(g_oemBackHook)==success&&g_oemMouseHook&&fixtureReplayed.empty(),
            "actual keyboard retry result does not replace the live mouse route: "+std::to_string(success));
        const auto up=recordedMouse(VK_XBUTTON1,true);recordedMouseDrain();
        check(up==1&&fixtureOwnActions.size()==1&&!oemKeyboardConsumeBusy(),
            "surviving mouse retains its actual paired UP and queued action after keyboard retry: "+std::to_string(success));
    }
    mouseOnlyHookSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);
    nativeDesktopFunctionDrain();retryKeyboardForMouse(true,{VK_XBUTTON1,VK_F11});flushJobs();
    check(fixtureOwnActions.size()==1&&!g_oemKeyboardConsume.physicalDown[VK_XBUTTON1]&&
          g_oemKeyboardConsume.visibility[VK_XBUTTON1]==0&&!oemKeyboardConsumeBusy(),
        "keyboard cold seed neither recreates an already released mouse nor cancels its pending action");

    mouseOnlyHookSetup(recordedMouseInputs("MouseLeft","MouseForward"));
    recordedMouse(VK_LBUTTON);fixtureSendLimit=0;recordedMouse(VK_LBUTTON,true);
    retryKeyboardForMouse(true,{VK_LBUTTON});
    check(g_oemKeyboardConsume.replayFault&&g_oemKeyboardConsume.replayCount==2&&
          g_oemKeyboardConsume.visibility[VK_LBUTTON]==2&&!g_oemKeyboardConsume.physicalDown[VK_LBUTTON]&&
          g_oemKeyboardConsume.replay[0].type==INPUT_MOUSE&&g_oemKeyboardConsume.replay[1].type==INPUT_MOUSE,
        "keyboard retry preserves exact failed mouse replay and real released physical state");
    fixtureSendLimit=UINT_MAX;fixtureTick+=kOemKeyboardReplayRetryMs+1;oemKeyboardConsumeMaintain(fixtureTick);recordedMouseDrain();
    check(fixtureReplayed.size()==2&&!oemKeyboardConsumeBusy()&&fixtureOwnActions.empty(),
        "old mouse replay repays original DOWN then UP after independent keyboard recovery");
}
static void nativeRecordedMouseOnlyColdCases() {
    mouseOnlyHookSetup(recordedMouseInputs("MouseBack"),"press",{VK_XBUTTON1});
    recordedMouse(VK_XBUTTON1);recordedMouseDrain();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral&&
          g_oemKeyboardConsume.physicalDown[VK_XBUTTON1]&&g_oemKeyboardConsume.visibility[VK_XBUTTON1]==1,
        "mouse-only cold install seeds a real preheld mouse as passed, never as new press");
    recordedMouse(VK_XBUTTON1,true);fixtureDesktopDown.clear();recordedMouseDrain();sampledDesktopTick();
    recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);recordedMouseDrain();
    check(fixtureOwnActions.size()==1&&!oemKeyboardConsumeBusy(),
        "cold mouse-only source recovers after actual neutral and a fresh pulse");

    mouseOnlyHookSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    fixtureDesktopDown={VK_F11,'L'};sampledDesktopTick();
    retryKeyboardForMouse(true,{VK_F11,'L'});flushJobs();sampledDesktopTick();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,
        "new keyboard hook revokes old sampled keyboard work and requires real cold neutral");
    recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);fixtureDesktopDown.clear();recordedMouseDrain();sampledDesktopTick();
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard('L');recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
    check(fixtureOwnActions.size()==1&&!oemKeyboardConsumeBusy(),
        "recovered keyboard source executes once on its new actual pair");

    fixtureKeyboardHookInstallOk=true;recordedMouseSetup(json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}));
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard('L');nativeDesktopFunctionDrain();
    g_keyboardModifierHookInstalled.store(false,std::memory_order_release);flushJobs();
    check(fixtureOwnActions.empty(),"queued keyboard-dependent chord checks the original atomic keyboard hook publication");
    recordedMouseKeyboard('L',true);recordedMouseKeyboard(VK_F11,true);recordedMouseDrain();
    mouseOnlyHookSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);nativeDesktopFunctionDrain();
    g_keyboardModifierHookInstalled.store(false,std::memory_order_release);flushJobs();
    check(fixtureOwnActions.size()==1,"atomic keyboard publication does not gate an independent mouse-only action");

    mouseOnlyHookSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);nativeDesktopFunctionDrain();
    ++fixtureNotificationSequence;flushJobs();check(fixtureOwnActions.empty(),"mouse-only retained action still checks original shared Native power revocation");
    check(recordedMouse(VK_XBUTTON1,true)==1,"power cancellation retains actual mouse-only paired UP debt");recordedMouseDrain();
}

static void nativeRecordedMouseGuardCases() {
    const auto inputs=recordedMouseInputs("ShiftRight","MouseForward",true);
    const auto pair=[]{recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_XBUTTON2);recordedMouse(VK_XBUTTON2,true);recordedMouseKeyboard(VK_RSHIFT,true);};
    recordedMouseSetup(inputs);recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_XBUTTON2);nativeDesktopFunctionDrain();
    ++fixtureNotificationSequence;flushJobs();check(fixtureOwnActions.empty(),"shared Native power revocation cancels queued mouse-chord action");
    check(recordedMouse(VK_XBUTTON2,true)==1&&recordedMouseKeyboard(VK_RSHIFT,true)==1,"power cancellation keeps actual swallowed mouse and keyboard UP debt");
    recordedMouseDrain();sampledDesktopTick();pair();recordedMouseDrain();check(fixtureOwnActions.size()==1,"mouse chord recovers only on a fresh pair after real neutral");
    recordedMouseSetup(inputs,"release");recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_XBUTTON2);recordedMouseDrain();
    g_shortcutRecordingActive=true;++g_oemShortcutEdgeEpoch;recordedMouse(VK_XBUTTON2,true);recordedMouseKeyboard(VK_RSHIFT,true);
    g_shortcutRecordingActive=false;++g_oemShortcutEdgeEpoch;recordedMouseDrain();
    check(fixtureOwnActions.empty()&&!oemKeyboardConsumeBusy(),"recording revokes old mouse release action without losing hidden pair ownership");
    recordedMouseSetup(inputs);pair();++g_oemShortcutEdgeEpoch;recordedMouseDrain();check(fixtureOwnActions.empty(),"saved rule epoch rejects already queued mouse pulses");
    recordedMouseSetup(inputs);fixturePostOk=false;pair();fixturePostOk=true;recordedMouseDrain();
    check(fixtureOwnActions.empty()&&g_desktopFunctionEdges.size()==0&&!oemKeyboardConsumeBusy(),"failed mouse FIFO wake preserves originals without a phantom action");
    recordedMouseSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1,false,LLMHF_INJECTED);recordedMouse(VK_XBUTTON1,true,LLMHF_LOWER_IL_INJECTED);recordedMouseDrain();
    check(fixtureOwnActions.empty()&&g_desktopFunctionEdges.size()==0,"injected mouse replay never re-enters physical shortcut capture");
    recordedMouseSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);g_exitRequested=true;recordedMouseDrain();
    check(fixtureOwnActions.empty()&&recordedMouse(VK_XBUTTON1,true)==1,"exit revokes mouse action while retaining its actual release");
    recordedMouseSetup(recordedMouseInputs("MouseBack"));recordedMouse(VK_XBUTTON1);recordedMouseDrain();
    g_oemHooksWanted=false;oemBackMappingReconcileOnUi();check(fixtureHookRemoves==0&&oemKeyboardConsumeBusy(),"mouse debt retains the installing hooks during disable");
    check(recordedMouse(VK_XBUTTON1,true)==1,"disabled mouse source consumes its original owed UP");oemBackMappingReconcileOnUi();
    check(fixtureHookRemoves==2&&!oemKeyboardConsumeBusy(),"both old hooks retire after actual mouse pair completion");
    recordedMouseSetup(recordedMouseInputs("MouseBack"));g_oemBackHook=g_oemMouseHook=nullptr;g_desktopFunctionHookActive=false;g_oemKeyHooksActive=false;
    g_oemHookNextInstallAttempt=0;fixtureHookInstallOk=false;fixtureDesktopDown={VK_XBUTTON1};sampledDesktopTick();flushJobs();
    check(!g_desktopFunctionHookActive&&fixtureOwnActions.size()==1,"failed hooks retain the original admitted sampled mouse fallback");
    recordedMouseSetup(recordedMouseInputs("MouseBack"));g_oemBackHook=g_oemMouseHook=nullptr;fixtureDesktopDown={VK_XBUTTON1};g_oemHookNextInstallAttempt=0;
    oemBackMappingReconcileOnUi();sampledDesktopTick();recordedMouse(VK_XBUTTON1);recordedMouseDrain();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,"cold mouse hook seeding does not synthesize a preheld shortcut");
    recordedMouse(VK_XBUTTON1,true);fixtureDesktopDown.clear();recordedMouseDrain();sampledDesktopTick();recordedMouse(VK_XBUTTON1);recordedMouse(VK_XBUTTON1,true);recordedMouseDrain();
    check(fixtureOwnActions.size()==1,"cold preheld mouse recovers after true release and a new pulse");
}
static void nativeRecordedMouseReplayCases() {
    const auto inputs=recordedMouseInputs("ShiftRight","MouseForward",true);
    recordedMouseSetup(inputs);check(recordedMouse(VK_XBUTTON2)==1,"explicit mouse candidate starts the existing bounded prefix");
    const auto started=g_oemKeyboardConsume.startedAt;fixtureTick=started+kOemKeyboardPrefixBudgetMs;
    oemKeyboardConsumeMaintain(fixtureTick);check(recordedMouse(VK_XBUTTON2,true)==0,"expired mouse prefix returns its actual original release normally");recordedMouseDrain();
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==1&&fixtureReplayed[0].type==INPUT_MOUSE&&
        (fixtureReplayed[0].mi.dwFlags&MOUSEEVENTF_XDOWN)&&fixtureReplayed[0].mi.mouseData==XBUTTON2&&
        fixtureReplayed[0].mi.dx>0&&fixtureReplayed[0].mi.dy>0&&!oemKeyboardConsumeBusy(),
        "mouse prefix replay preserves XButton identity and original virtual-desktop coordinates");
    recordedMouseSetup(inputs);recordedMouse(VK_XBUTTON2);fixtureSendLimit=0;recordedMouse(VK_XBUTTON2,true);
    check(g_oemKeyboardConsume.replayFault&&g_oemKeyboardConsume.replayCount==2&&fixtureReplayed.empty(),"failed mouse prefix repayment retains exact DOWN and UP debt");
    fixtureSendLimit=UINT_MAX;fixtureTick+=kOemKeyboardReplayRetryMs+1;oemKeyboardConsumeMaintain(fixtureTick);recordedMouseDrain();
    check(fixtureReplayed.size()==2&&(fixtureReplayed[0].mi.dwFlags&MOUSEEVENTF_XDOWN)&&
        (fixtureReplayed[1].mi.dwFlags&MOUSEEVENTF_XUP)&&!oemKeyboardConsumeBusy()&&fixtureOwnActions.empty(),
        "original retry repays mouse DOWN then UP without inventing an action");
    recordedMouseSetup(inputs);recordedMouse(VK_XBUTTON2);recordedMouseKeyboard('Q');recordedMouseKeyboard('Q',true);recordedMouse(VK_XBUTTON2,true);recordedMouseDrain();
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==2&&fixtureReplayed[0].type==INPUT_MOUSE&&fixtureReplayed[1].type==INPUT_KEYBOARD&&
        fixtureReplayed[1].ki.wVk=='Q'&&!oemKeyboardConsumeBusy(),"mixed replay preserves mouse-before-unrelated-key causal order");
    recordedMouseSetup(inputs);recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_LBUTTON);recordedMouse(VK_LBUTTON,true);recordedMouseKeyboard(VK_RSHIFT,true);recordedMouseDrain();
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==1&&fixtureReplayed[0].type==INPUT_KEYBOARD&&!oemKeyboardConsumeBusy(),
        "unrelated OEM mouse stream repays older keyboard prefix without duplicate mouse replay");
    recordedMouseSetup(recordedMouseInputs("MouseLeft","MouseForward"));recordedMouse(VK_LBUTTON);fixtureSendLimit=1;recordedMouse(VK_LBUTTON,true);
    check(g_oemKeyboardConsume.replayCount==1&&g_oemKeyboardConsume.visibility[VK_LBUTTON]==1,"partial mouse replay preserves outstanding real UP after accepted DOWN");
    fixtureSendLimit=UINT_MAX;fixtureTick+=kOemKeyboardReplayRetryMs+1;oemKeyboardConsumeMaintain(fixtureTick);recordedMouseDrain();
    check(fixtureReplayed.size()==2&&!oemKeyboardConsumeBusy()&&fixtureOwnActions.empty(),"partial mouse repayment completes without deleting physical debt");
    recordedMouseSetup(recordedMouseInputs("MouseLeft","MouseForward"));recordedMouseKeyboard('Q');
    check(recordedMouse(VK_LBUTTON)==0&&recordedMouse(VK_XBUTTON2)==0&&recordedMouse(VK_XBUTTON2,true)==0&&recordedMouse(VK_LBUTTON,true)==0,
        "already passed held keyboard input prevents partial mouse-original suppression");recordedMouseKeyboard('Q',true);recordedMouseDrain();
    check(!oemKeyboardConsumeBusy()&&fixtureReplayed.empty(),"passed mouse stream never creates hidden replay debt");
}

static void nativeRecordedKeyboardDefinitionCases() {
    struct Definition {const char* modifier;DWORD vk;DWORD flags;const char* base;DWORD baseVk;};
    const Definition definitions[]={{"ControlRight",VK_RCONTROL,LLKHF_EXTENDED,"F12",VK_F12},
        {"Control",VK_LCONTROL,0,"KeyG",'G'},{"ShiftRight",VK_RSHIFT,0,"F17",VK_F17}};
    for(const auto& item:definitions){
        const auto inputs=json::array({{{"source","keyboard"},{"code",item.modifier}},{{"source","keyboard"},{"code",item.base}}});
        sampledDesktopSetup(sampledDesktopRule(inputs));sampledDesktopTick();const auto saved=fixtureInput;
        const auto key=[&](DWORD vk,bool up=false,DWORD flags=0){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=flags|(up?LLKHF_UP:0);
            return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
        const auto a=key(item.vk,false,item.flags),b=key(item.baseVk),c=key(item.baseVk,true),d=key(item.vk,true,item.flags);
        nativeDesktopFunctionDrain();flushJobs();
        check(a==1&&b==1&&c==1&&d==1&&!oemKeyboardConsumeBusy()&&fixtureReplayed.empty(),"recorded exact modifier/alias pair uses original consume ledger: "+std::string(item.modifier));
        check(fixtureOwnActions.size()==1&&fixtureInput==saved&&!g_inputHostProcess,"recorded exact modifier/alias dispatch preserves saved definition without Host: "+std::string(item.modifier));
    }
    auto mouse=sampledDesktopRule(json::array({{{"source","keyboard"},{"code","ShiftRight"}},{{"source","mouse"},{"code","MouseForward"}}}));
    sampledDesktopSetup(mouse);sampledDesktopTick();recordedMouseKeyboard(VK_RSHIFT);recordedMouse(VK_XBUTTON2);
    recordedMouse(VK_XBUTTON2,true);recordedMouseKeyboard(VK_RSHIFT,true);recordedMouseDrain();
    check(g_desktopChordWanted&&fixtureOwnActions.size()==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy(),"mixed mouse definition consumes its typed physical pair without sampled input or a virtual Host");
    auto mixed=sampledDesktopRule(json::array({{{"source","controller"},{"code","a"}},{{"source","keyboard"},{"code","KeyG"}}}));
    sampledDesktopSetup(mixed);fixtureDesktopDown={'G'};sampledDesktopTick(1,false,XINPUT_GAMEPAD_A);flushJobs();
    check(!g_desktopChordWanted&&fixtureOwnActions.empty(),"mixed controller definition retains original controller admission without a virtual Host");
}

static void nativeRecordedKeyboardGuardCases(bool tail=false,bool fallbackOnly=false,bool preheldOnly=false) {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    const auto setup=[&]{
        sampledDesktopSetup(sampledDesktopRule(inputs));
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWinMini";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto pair=[&]{key(VK_F11);key('L');key('L',true);key(VK_F11,true);};
    const auto drain=[]{deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();nativeDesktopFunctionDrain();flushJobs();};
    const auto fallback=[&]{setup();g_oemBackHook=g_oemMouseHook=nullptr;g_oemKeyHooksActive=false;g_desktopFunctionHookActive=false;
        g_oemHookNextInstallAttempt=0;fixtureHookInstallOk=false;sampledDesktopTick();};
    if(!preheldOnly){
    if(!tail){
    fallback();fixtureDesktopDown={VK_F11,'L'};sampledDesktopTick();
    check(!g_desktopFunctionHookActive&&g_nativeKeyboardShortcuts.front().desktopWitness.power.generation&&fixtureJobs.size()==1,
        "failed hook installation retains sampled keyboard admission with real original power witness");
    if(fallbackOnly)return;
    ++fixtureNotificationSequence;flushJobs();check(fixtureOwnActions.empty(),"sampled fallback queued action rejects original native power revocation");
    fallback();fixtureDesktopDown={VK_F11,'L'};sampledDesktopTick();++fixturePublicationRevision;flushJobs();
    check(fixtureOwnActions.size()==1,"healthy publication-only change preserves admitted sampled keyboard action");
    fallback();g_powerLifecycle=PowerLifecycle::Suspending;fixtureDesktopDown={VK_F11,'L'};sampledDesktopTick();
    g_powerLifecycle=PowerLifecycle::Ready;sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,"inactive-hook nonReady input requires real neutral after Ready");
    fixtureDesktopDown.clear();sampledDesktopTick();fixtureDesktopDown={VK_F11,'L'};sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"inactive-hook sampled input recovers only on a fresh actual pair");
    setup();for(unsigned i=0;i<17;++i)pair();
    check(g_desktopFunctionEdges.size()==64,"existing bounded64 FIFO reaches capacity without allocating hook edges");drain();
    check(fixtureOwnActions.empty()&&!oemKeyboardConsumeBusy(),"FIFO overflow cancels old action receipts without losing physical delivery debt");
    sampledDesktopTick();pair();drain();check(fixtureOwnActions.size()==1,"FIFO overflow recovery requires neutral and a fresh actual chord");
    }
    setup();key(VK_F11);fixtureSendLimit=0;const auto t=g_oemKeyboardConsume.startedAt;
    fixtureTick=t+kOemKeyboardPrefixBudgetMs;oemKeyboardConsumeMaintain(fixtureTick);key(VK_F11,true);
    check(g_oemKeyboardConsume.replayFault&&g_oemKeyboardConsume.replayCount==2&&fixtureReplayed.empty(),"failed prefix replay retains exact original down/up debt");
    fixtureSendLimit=UINT_MAX;fixtureTick+=kOemKeyboardReplayRetryMs+1;oemKeyboardConsumeMaintain(fixtureTick);drain();
    check(!oemKeyboardConsumeBusy()&&fixtureReplayed.size()==2&&fixtureOwnActions.empty(),"original retry repays down then actual up without configured action");
    setup();fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(producerRule("l4"));fixtureInput["revision"]=99;
    sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);pair();drain();
    check(fixtureOwnActions.size()==1&&g_oemKeyMask==0,"same LL explicit keyboard owns its interpretation without duplicating the OEM action");
    setup();auto f13=rule("os.openTouchKeyboard","unused","press","keyboard","F13");f13["params"]=json::object();
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(f13);fixtureInput["revision"]=100;
    sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);key(VK_F13);key(VK_F13,true);pair();drain();
    check(fixtureOwnActions.size()==2&&g_desktopFunctionEdges.size()==0&&!oemKeyboardConsumeBusy(),"original single F13 and recorded chord share FIFO without cross-dispatch or UP loss");
    setup();g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";oemBackMappingRefreshGate(false,"disabled",false);sampledDesktopTick();
    const auto raw=oemBackHookVkBit(VK_F17);g_oemKbDownRaw=raw;oemBackHookRecompute();pair();drain();
    check(g_oemRawPathActive&&g_oemKbDownRaw==raw&&nativeOemKeyMask(true)==kOemBitM2&&fixtureOwnActions.size()==1,
        "explicit keyboard snapshot/consumption does not clear independent qualified Raw OEM source");
    }
    setup();g_oemBackHook=g_oemMouseHook=nullptr;fixtureDesktopDown={VK_F11,'L'};g_oemHookNextInstallAttempt=0;oemBackMappingReconcileOnUi();sampledDesktopTick();
    key(VK_F11);drain();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral&&g_oemKeyboardConsume.physicalDown[VK_F11]&&g_oemKeyboardConsume.physicalDown['L'],
        "cold hook seed retains both real preheld recorded keys and does not fabricate a new press");
    key('L',true);key(VK_F11,true);drain();sampledDesktopTick();pair();drain();
    check(fixtureOwnActions.size()==1,"preheld recorded binding recovers after actual releases and a fresh pair");
}


static void nativeRecordedKeyboardDiagnosticCases() {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    const auto setup=[&]{
        sampledDesktopSetup(sampledDesktopRule(inputs));
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWinMini";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto drain=[]{nativeDesktopFunctionDrain();flushJobs();};
    const auto rows=[](const char* source="recorded-keyboard-chord"){
        std::vector<json> out;for(const auto& row:fixtureLogs)
            if(row.value("event","")=="oem-shortcut-edge-delivery"&&row.value("source","")==source)out.push_back(row);
        return out;
    };
    setup();key(VK_F11);key('L');key('L',true);key(VK_F11,true);drain();auto log=rows();
    check(log.size()==4&&log[0].value("vk",0)==VK_F11&&log[1].value("vk",0)=='L'&&
        !log[0].value("keyUp",true)&&log[2].value("keyUp",false)&&log[3].value("keyUp",false),
        "hook evidence identifies the exact four saved keyboard edges");
    check(log.size()==4&&std::all_of(log.begin(),log.end(),[](const json& r){return r.value("hookDecisionKnown",false)&&r.value("consumed",false);})&&
        log[0].value("hookDecision","")=="prefix-buffered"&&log[1].value("hookDecision","")=="suppressed"&&
        log[2].value("hookDecision","")=="suppressed"&&log[3].value("hookDecision","")=="suppressed",
        "hook evidence distinguishes buffered prefix from the actual swallowed chord and UPs");
    check(fixtureOwnActions.size()==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy()&&
        std::all_of(log.begin(),log.end(),[](const json& r){return r.contains("externalDefaultActionSuppressionVerified")&&!r.value("externalDefaultActionSuppressionVerified",true);}),
        "hook evidence keeps one no-virtual action and does not claim external OEM suppression");
    setup();key(VK_F11);drain();const auto started=g_oemKeyboardConsume.startedAt;
    fixtureTick=started+kOemKeyboardPrefixBudgetMs;oemKeyboardConsumeMaintain(fixtureTick);key(VK_F11,true);drain();log=rows();
    check(log.size()==2&&log[0].value("hookDecision","")=="prefix-buffered"&&log[0].value("consumed",false)&&
        log[1].value("hookDecision","")=="forwarded"&&!log[1].value("consumed",true),
        "expired prefix evidence preserves its original decision and reports the actual forwarded release");
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==1&&!oemKeyboardConsumeBusy(),
        "expired-prefix diagnostics leave original replay and action ownership unchanged");
    setup();key('Q');key(VK_F11);key('L');key('L',true);key(VK_F11,true);key('Q',true);drain();log=rows();
    check(log.size()==4&&std::all_of(log.begin(),log.end(),[](const json& r){return r.value("hookDecisionKnown",false)&&
        r.value("hookDecision","")=="forwarded"&&!r.value("consumed",true);})&&!oemKeyboardConsumeBusy(),
        "already-visible held key produces truthful forwarded chord evidence");
    auto f13=sampledDesktopRule(json::array({{{"source","keyboard"},{"code","F13"}}}));
    sampledDesktopSetup(f13);sampledDesktopTick();key(VK_F13);key(VK_F13,true);drain();log=rows("explicit-keyboard-function");
    check(log.size()==2&&std::all_of(log.begin(),log.end(),[](const json& r){return r.value("hookDecisionKnown",false)&&
        r.value("hookDecision","")=="suppressed"&&r.value("consumed",false);})&&fixtureOwnActions.size()==1,
        "existing explicit function path receives the same actual hook decision evidence");
    auto both=sampledDesktopRule(json::array({{{"source","keyboard"},{"code","F13"}},{{"source","keyboard"},{"code","KeyL"}}}));
    sampledDesktopSetup(both);fixtureInput["buttonMapping"]["rules"]["shortcutRules"].push_back(f13);++fixtureInput["revision"].get_ref<json::number_integer_t&>();
    sampledDesktopTick(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);key(VK_F13);key('L');key('L',true);key(VK_F13,true);drain();
    const auto chord=rows();log=rows("explicit-keyboard-function");
    check(chord.size()==4&&log.size()==2&&std::all_of(chord.begin(),chord.end(),[](const json& r){return r.value("hookDecisionKnown",false);})&&
        std::all_of(log.begin(),log.end(),[](const json& r){return r.value("hookDecisionKnown",false);})&&g_desktopFunctionEdges.size()==0,
        "one hook annotates both newly published owners without touching prior FIFO rows");
    setup();fixturePostOk=false;key(VK_F11);key('L');key('L',true);key(VK_F11,true);fixturePostOk=true;drain();
    check(rows().empty()&&fixtureOwnActions.empty()&&!oemKeyboardConsumeBusy(),
        "failed wake publishes no fabricated hook-decision receipt or action");
}

static void nativeRecordedKeyboardConsumeCases() {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    const auto setup=[&](const char* trigger="press"){
        sampledDesktopSetup(sampledDesktopRule(inputs,trigger));
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWinMini";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
    };
    const auto key=[](DWORD vk,bool up=false,DWORD flags=0){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.scanCode=vk;
        k.flags=flags|(up?LLKHF_UP:0);return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto drain=[] {deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();nativeDesktopFunctionDrain();flushJobs();};
    setup();const auto a=key(VK_F11),b=key('L'),c=key('L',true),d=key(VK_F11,true);drain();
    check(fixtureOwnActions.size()==1,"retained recorded chord has one actual action before checking OS suppression");
    check(a==1&&b==1&&c==1&&d==1&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy(),"recorded keyboard chord consumes original DOWN and UP pairs without replay debt");
    setup();key(VK_F11);const auto started=g_oemKeyboardConsume.startedAt;
    oemKeyboardConsumeMaintain(started+kOemKeyboardPrefixBudgetMs);key(VK_F11,true);drain();
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==1&&!oemKeyboardConsumeBusy(),"incomplete recorded prefix repays original key within existing budget");
    setup();key(VK_F11);key('Q');key('Q',true);key(VK_F11,true);drain();
    check(fixtureOwnActions.empty()&&fixtureReplayed.size()==2&&!oemKeyboardConsumeBusy(),"unrelated typing after recorded prefix is repaid in order with ordinary UP");
    setup();fixturePostOk=false;const auto f=key(VK_F11);key('L');key('L',true);key(VK_F11,true);fixturePostOk=true;drain();
    check(fixtureOwnActions.empty()&&(f==0||!fixtureReplayed.empty())&&!oemKeyboardConsumeBusy(),"failed FIFO wake cancels action and preserves original key delivery");
    setup();key(VK_F11);key('L');nativeDesktopFunctionDrain();++fixtureNotificationSequence;flushJobs();
    check(fixtureOwnActions.empty(),"original shared power witness rejects queued recorded chord action");
    check(key('L',true)==1&&key(VK_F11,true)==1,"power cancellation retains actual consumed paired UP debt");drain();sampledDesktopTick();
    key(VK_F11);key('L');key('L',true);key(VK_F11,true);drain();
    check(fixtureOwnActions.size()==1,"fresh actual recorded pair recovers after power cancellation and neutral");
    setup("release");key(VK_F11);key('L');drain();g_shortcutRecordingActive=true;++g_oemShortcutEdgeEpoch;
    key('L',true);key(VK_F11,true);g_shortcutRecordingActive=false;++g_oemShortcutEdgeEpoch;drain();
    check(fixtureOwnActions.empty(),"recording revocation does not masquerade as recorded chord release action");
    setup("double");key(VK_F11);key('L');key('L',true);key(VK_F11,true);drain();
    check(fixtureOwnActions.empty(),"recorded chord first click waits for configured double");
    key(VK_F11);key('L');key('L',true);key(VK_F11,true);drain();check(fixtureOwnActions.size()==1,"recorded chord second click dispatches once in original double window");
    setup("hold");key(VK_F11);key('L');drain();sampledDesktopTick(501);flushJobs();
    check(fixtureOwnActions.size()==1,"recorded held chord uses original hold timer after retained admission");
    key('L',true);key(VK_F11,true);drain();check(!oemKeyboardConsumeBusy(),"held chord repays only real releases");
    setup();key(VK_F11);key('L');nativeDesktopFunctionDrain();++g_oemShortcutEdgeEpoch;flushJobs();
    check(fixtureOwnActions.empty(),"original rule epoch rejects queued recorded keyboard action");key('L',true);key(VK_F11,true);drain();
    setup();key(VK_F11,false,LLKHF_INJECTED);key('L',false,LLKHF_INJECTED);key('L',true,LLKHF_INJECTED);key(VK_F11,true,LLKHF_INJECTED);drain();
    check(fixtureOwnActions.empty()&&!oemKeyboardConsumeBusy(),"injected keyboard stream is never promoted to recorded physical input");
    setup();key(VK_F11);key('L');g_exitRequested=true;drain();
    check(fixtureOwnActions.empty()&&key('L',true)==1&&key(VK_F11,true)==1,"exit revokes queued action while preserving original paired release");
}

#ifdef YMCC_SHORTCUT_SNAPSHOT_BLOCK
static void desktopSnapshotBlockCases() {
    std::array<bool,512> saved{};
    std::copy(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),saved.begin());
    unsigned layouts=0,queries=0,goldenComparisons=0;
    const auto clear=[] {std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),false);};
    const auto validate=[&](int single=-2) {
        const auto snapshot=nativeDesktopChordPhysicalKeys();
        const auto raw=[](unsigned vk){return g_oemKeyboardConsume.physicalDown[vk]||g_oemKeyboardConsume.physicalDown[vk+256];};
        for(unsigned vk=0;vk<256;++vk){
            const bool expected=raw(vk)||(vk==VK_CONTROL&&(raw(VK_LCONTROL)||raw(VK_RCONTROL)))||
                (vk==VK_SHIFT&&(raw(VK_LSHIFT)||raw(VK_RSHIFT)))||(vk==VK_MENU&&(raw(VK_LMENU)||raw(VK_RMENU)));
            if(snapshot.has(vk)!=expected)throw std::runtime_error("snapshot/reference mismatch layout="+std::to_string(layouts)+" vk="+std::to_string(vk));
            if(single>=-1){
                const unsigned rawKey=single<0?512u:static_cast<unsigned>(single)&255u;
                const bool golden=single>=0&&(vk==rawKey||(vk==VK_CONTROL&&(rawKey==VK_LCONTROL||rawKey==VK_RCONTROL))||
                    (vk==VK_SHIFT&&(rawKey==VK_LSHIFT||rawKey==VK_RSHIFT))||(vk==VK_MENU&&(rawKey==VK_LMENU||rawKey==VK_RMENU)));
                if(snapshot.has(vk)!=golden)throw std::runtime_error("snapshot single-slot golden mismatch");
                ++goldenComparisons;
            }
            ++queries;
        }
        for(unsigned vk:{256u,511u,512u,UINT_MAX}){if(snapshot.has(vk))throw std::runtime_error("snapshot invalid key accepted");++queries;}
        ++layouts;
    };
    clear();validate(-1);
    for(unsigned slot=0;slot<512;++slot){clear();g_oemKeyboardConsume.physicalDown[slot]=true;validate(static_cast<int>(slot));}
    std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),true);validate();
    for(const auto& group:std::array<std::array<unsigned,3>,3>{{{{VK_CONTROL,VK_LCONTROL,VK_RCONTROL}},{{VK_SHIFT,VK_LSHIFT,VK_RSHIFT}},{{VK_MENU,VK_LMENU,VK_RMENU}}}})
        for(unsigned bits=0;bits<64;++bits){clear();for(unsigned i=0;i<6;++i)g_oemKeyboardConsume.physicalDown[group[i/2]+(i%2)*256]=(bits&(1u<<i))!=0;validate();}
    std::uint32_t random=0x137a6c51u;
    for(unsigned i=0;i<256;++i){for(auto& down:g_oemKeyboardConsume.physicalDown){random^=random<<13;random^=random>>17;random^=random<<5;down=(random&7u)==0;}validate();}
    std::copy(saved.begin(),saved.end(),std::begin(g_oemKeyboardConsume.physicalDown));
    check(layouts==962&&queries==250120&&goldenComparisons==131328,"snapshot blocks all-slot, modifier, extended and deterministic-layout equivalence");
    std::cout<<"SNAPSHOT_VECTORS "<<layouts<<" queries="<<queries<<" golden="<<goldenComparisons<<'\n';
}
#endif

#ifdef YMCC_SHORTCUT_CHORD_MAINTAIN_DEMAND
static void chordMaintainDemandCases(bool candidate) {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    sampledDesktopSetup(sampledDesktopRule(inputs));
    ymcc::sleeppower::SharedPowerV1 power;
    g_exitRequested=false;g_shortcutRecordingActive=false;
    if(!captureSharedPowerLifecycle(power))throw std::runtime_error("oracle source power unavailable");
    unsigned cases=0;
    for(unsigned flags=0;flags<128;++flags)for(unsigned shape=0;shape<6;++shape)for(unsigned maximum=0;maximum<2;++maximum){
        const bool wanted=(flags&1)!=0,present=(flags&2)!=0,stale=(flags&4)!=0;
        const bool exiting=(flags&8)!=0,recording=(flags&16)!=0,epochChanged=(flags&32)!=0,blocked=(flags&64)!=0;
        std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),false);
        g_desktopChordCaptureKeys={};g_desktopChordCaptureKeys.set(VK_MENU);g_desktopChordCaptureKeys.set(VK_LBUTTON);
        if(shape==1)g_oemKeyboardConsume.physicalDown[VK_LMENU]=true;
        if(shape==2)g_oemKeyboardConsume.physicalDown[VK_RMENU+256]=true;
        if(shape==3)g_oemKeyboardConsume.physicalDown[VK_LBUTTON+256]=true;
        if(shape==4)g_oemKeyboardConsume.physicalDown['P']=true;
        if(shape==5)g_oemKeyboardConsume.physicalDown[VK_LCONTROL]=true;
        const bool held=shape>=1&&shape<=3;
        g_desktopChordWanted=wanted;g_desktopChordPower=power;
        if(!present)g_desktopChordPower={};
        else if(stale)g_desktopChordPower.generation+=100;
        const auto expectedPower=g_desktopChordPower;
        g_desktopChordBlocked=blocked;g_exitRequested=exiting;g_shortcutRecordingActive=recording;
        g_oemShortcutEdgeEpoch=71;g_desktopChordEpoch=epochChanged?70:71;
        const auto oldEpoch=g_desktopChordEpoch;
        g_desktopChordKeys={};g_desktopChordKeys.set('K');const auto originalKeys=g_desktopChordKeys;
        const auto cancellation=maximum?UINT64_MAX:UINT64_C(41);g_desktopChordCancellation=cancellation;
        const bool early=!wanted&&!present;
        const bool cancel=present&&(exiting||recording||!wanted||epochChanged||stale);
        fixtureMaintainSnapshotReads=0;
        nativeDesktopChordMaintain();
        const auto expectedCancellation=cancel&&cancellation!=UINT64_MAX?cancellation+1:cancellation;
        const bool expectedBlocked=early?blocked:(cancel?held:blocked&&held);
        const auto expectedEpoch=early?oldEpoch:71;
        if(g_desktopChordCancellation.load()!=expectedCancellation||g_desktopChordBlocked!=expectedBlocked||
           g_desktopChordEpoch!=expectedEpoch||
           g_desktopChordPower.generation!=(cancel?0:expectedPower.generation)||
           g_desktopChordKeys.words!=(cancel?ymcc::input::desktopFunction::KeyboardKeys{}.words:originalKeys.words))
            throw std::runtime_error("maintain transition mismatch flags="+std::to_string(flags)+" shape="+std::to_string(shape));
        const unsigned expectedReads=early?0:candidate?(cancel||blocked?1:0):1;
        if(fixtureMaintainSnapshotReads!=expectedReads)
            throw std::runtime_error("maintain snapshot demand mismatch flags="+std::to_string(flags)+" actual="+std::to_string(fixtureMaintainSnapshotReads));
        ++cases;
    }
    g_exitRequested=false;g_shortcutRecordingActive=false;g_desktopChordPower={};g_desktopChordBlocked=false;g_desktopChordWanted=false;
    g_desktopChordCancellation=0;g_desktopChordCaptureKeys={};g_desktopChordKeys={};
    std::fill(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),false);
    check(cases==1536,"maintain transitions preserve cancel blocked aliases extended mouse and saturation; snapshot only by demand");
    std::cout<<"MAINTAIN_DEMAND_STATES "<<cases<<'\n';
}
static void chordMaintainInterleaveCases() {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    const auto setup=[&]{
        sampledDesktopSetup(sampledDesktopRule(inputs));
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWinMini";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    for(const auto reason:{"power","rule","recording","exit"}){
        setup();key(VK_F11);key('L');key('L',true);key(VK_F11,true);
        check(g_desktopFunctionEdges.size()>0,"interleave has real admitted recorded chord receipts");
        g_desktopChordBlocked=true;unsigned calls=0;
        fixtureSnapshotInterleave=[&]{
            ++calls;
            if(std::string(reason)=="power")++fixturePowerGeneration;
            if(std::string(reason)=="rule")++g_oemShortcutEdgeEpoch;
            if(std::string(reason)=="recording")g_shortcutRecordingActive=true;
            if(std::string(reason)=="exit")g_exitRequested=true;
        };
        nativeDesktopChordMaintain();fixtureSnapshotInterleave={};
        check(calls==1,"revocation arrives inside original physical snapshot observation interval");
        nativeDesktopFunctionDrain();flushJobs();
        check(fixtureOwnActions.empty(),std::string("final receipt/action guard rejects concurrent revocation: ")+reason);
        g_shortcutRecordingActive=false;g_exitRequested=false;
        nativeDesktopChordMaintain();
        check(!g_desktopChordPower.generation||sharedPowerLifecycleCurrent(g_desktopChordPower),
              "next original maintenance settles any retained stale power");
    }
    // Once drain admits serial work, its actual action must still revalidate.
    setup();key(VK_F11);key('L');key('L',true);key(VK_F11,true);
    nativeDesktopFunctionDrain();++fixturePowerGeneration;flushJobs();
    check(fixtureOwnActions.empty(),"serial action rejects revocation after real receipt drain");
}
#endif
static void nativeRecordedKeyboardChordCases() {
#ifdef YMCC_SHORTCUT_SNAPSHOT_BLOCK
    desktopSnapshotBlockCases();
#endif
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    sampledDesktopSetup(sampledDesktopRule(inputs));
    g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWinMini";
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();sampledDesktopTick();
    const auto saved=fixtureInput;
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    check(g_nativeKeyboardShortcuts.size()==1&&g_nativeKeyboardShortcuts.front().requiredMask==0&&
        g_nativeKeyboardShortcuts.front().desktopKeys==std::vector<WORD>({VK_F11,'L'}),"actual parser retains the recorded keyboard definition, not OEM L4");
    key(VK_F11);key('L');key('L',true);key(VK_F11,true);
    check(!oemKeyboardConsumeBusy()&&fixtureDesktopDown.empty(),"full physical LL chord pulse finishes before any sampled keyboard observation");
    deliverPhysicalReceipts();nativeDesktopFunctionDrain();flushJobs();sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"recorded short keyboard chord dispatches once without controller or virtual Host");
    check(fixtureInput==saved&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled&&fixtureRefreshes==0,
        "retained keyboard pulse leaves source/code and native-only intent unchanged");
#ifdef YMCC_SHORTCUT_CHORD_MAINTAIN_DEMAND
    chordMaintainDemandCases(true);
    chordMaintainInterleaveCases();
#endif
}

static void nativeDesktopChordAdmissionCases() {
    struct Chord {json inputs;std::set<WORD> keys;};
    const Chord chords[]={
        {json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}}),{VK_F11,'L'}},
        {json::array({{{"source","keyboard"},{"code","ControlLeft"}},{{"source","keyboard"},{"code","MetaLeft"}},{{"source","keyboard"},{"code","KeyG"}}}),{VK_LCONTROL,VK_LWIN,'G'}},
        {json::array({{{"source","mouse"},{"code","MouseRight"}}}),{VK_RBUTTON}},
        {json::array({{{"source","keyboard"},{"code","ShiftRight"}},{{"source","mouse"},{"code","MouseForward"}}}),{VK_RSHIFT,VK_XBUTTON2}},
        {json::array({{{"source","keyboard"},{"code","F12"}}}),{VK_F12}}
    };
    for(const auto& chord:chords){
        sampledDesktopSetup(sampledDesktopRule(chord.inputs));const auto saved=fixtureInput;
        check(g_nativeKeyboardShortcuts.size()==1&&g_nativeKeyboardShortcuts.front().requiredMask==0&&!g_nativeKeyboardShortcuts.front().desktopKeys.empty(),"actual parser keeps pure desktop source without a controller");
        fixtureDesktopDown=chord.keys;sampledDesktopTick();
        check(fixtureJobs.size()==1,"configured sampled desktop chord reaches actual serial admission with controllerAllowed=false");
        flushJobs();sampledDesktopTick();fixtureDesktopDown.clear();sampledDesktopTick();flushJobs();
        check(fixtureOwnActions.size()==1,"sampled desktop press has exactly one real native action after hold and release");
        check(fixtureInput==saved&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled&&fixtureRefreshes==0,"sampled desktop action preserves source/code, settings and no-Host intent");
    }
    auto mixed=sampledDesktopRule(json::array({{{"source","controller"},{"code","a"}},{{"source","keyboard"},{"code","KeyG"}}}));
    sampledDesktopSetup(mixed);fixtureDesktopDown={'G'};sampledDesktopTick(1,false,XINPUT_GAMEPAD_A);flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,"mixed controller/desktop rule retains original controller admission");
    fixtureDesktopDown.clear();sampledDesktopTick(1,true);fixtureDesktopDown={'G'};sampledDesktopTick(1,true,XINPUT_GAMEPAD_A);flushJobs();
    check(fixtureOwnActions.size()==1,"mixed controller binding still works after true neutral and original admission");
}
static void nativeDesktopChordGuardCases() {
    const auto inputs=json::array({{{"source","keyboard"},{"code","F11"}},{{"source","keyboard"},{"code","KeyL"}}});
    const std::set<WORD> chord={VK_F11,'L'};
    sampledDesktopSetup(sampledDesktopRule(inputs));fixtureDesktopDown=chord;sampledDesktopTick();
    check(fixtureJobs.size()==1,"sampled chord queues action before shared native publication revocation");
    ++fixtureNotificationSequence;flushJobs();
    check(fixtureOwnActions.empty(),"sampled desktop original native power witness reaches actual queued action even with vk0");
    sampledDesktopSetup(sampledDesktopRule(inputs));fixtureDesktopDown=chord;sampledDesktopTick();
    ++fixturePublicationRevision;flushJobs();
    check(fixtureOwnActions.size()==1,"healthy publication revision keeps sampled desktop action current");
    sampledDesktopSetup(sampledDesktopRule(inputs));fixtureDesktopDown=chord;sampledDesktopTick();
    g_oemShortcutEdgeEpoch.fetch_add(1);flushJobs();
    check(fixtureOwnActions.empty(),"saved desktop rule epoch cancels its already queued native action");
    sampledDesktopSetup(sampledDesktopRule(inputs,"release"));fixtureDesktopDown=chord;sampledDesktopTick();
    g_shortcutRecordingActive=true;++g_oemShortcutEdgeEpoch;g_shortcutRecordingActive=false;++g_oemShortcutEdgeEpoch;
    fixtureDesktopDown.clear();sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.empty(),"recording start-stop without an intermediate Evaluate cannot revive an old desktop release");
    fixtureDesktopDown=chord;sampledDesktopTick();fixtureDesktopDown.clear();sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"new real desktop pair restores release action after recording epoch");
    sampledDesktopSetup(sampledDesktopRule(inputs,"hold"));fixtureDesktopDown=chord;sampledDesktopTick();
    ++fixtureNotificationSequence;sampledDesktopTick(501);flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral&&g_nativeKeyboardShortcuts.front().holdStartedAt==0,"revoked sampled desktop hold waits for actual neutral in the new native power scope");
    fixtureDesktopDown.clear();sampledDesktopTick();fixtureDesktopDown=chord;sampledDesktopTick();sampledDesktopTick(501);flushJobs();
    check(fixtureOwnActions.size()==1,"fresh desktop hold recovers after actual release with original hold budget");
    sampledDesktopSetup(sampledDesktopRule(inputs),chord);sampledDesktopTick(1000);flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,"preheld sampled desktop binding cannot become a new press");
    fixtureDesktopDown.clear();sampledDesktopTick();fixtureDesktopDown=chord;sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"preheld desktop binding recovers on new real pair");
    sampledDesktopSetup(sampledDesktopRule(inputs));g_powerLifecycle=PowerLifecycle::Suspending;fixtureDesktopDown=chord;sampledDesktopTick();
    g_powerLifecycle=PowerLifecycle::Ready;sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.front().needsNeutral,"nonReady desktop input cannot be replayed as a fresh Ready press");
    fixtureDesktopDown.clear();sampledDesktopTick();fixtureDesktopDown=chord;sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"new Ready desktop press recovers after real neutral");
    sampledDesktopSetup(sampledDesktopRule(inputs,"double"));
    fixtureDesktopDown=chord;sampledDesktopTick();fixtureDesktopDown.clear();sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.empty(),"first sampled desktop click retains original double window");
    fixtureDesktopDown=chord;sampledDesktopTick();fixtureDesktopDown.clear();sampledDesktopTick();flushJobs();
    check(fixtureOwnActions.size()==1,"second sampled desktop click executes original double action");
}

static void nativeDesktopWitnessShapeCases() {
    auto tokens=oemProducerTokensFor(0);tokens.desktopFunction.vk=VK_F18;
    tokens.desktopFunction.cancellation=g_desktopFunctionCancellations[VK_F18-VK_F13].load();
    check(!oemProducerTokensCurrent(tokens),"existing F13-F24 token with absent power never gains action authority");
    check(captureSharedPowerLifecycle(tokens.desktopFunction.power)&&oemProducerTokensCurrent(tokens),"original valid F13-F24 token retains existing current-power admission");
    tokens.desktopFunction.vk=0;tokens.desktopFunction.power.size=0;
    check(!oemProducerTokensCurrent(tokens),"sampled vk0 token cannot turn malformed nonzero power into a current action");
}

static void nativeRogLlFallbackCases() {
    struct Model {YmccFamily family;const char* name;};
    const Model models[]={{YmccFamily::AsusRogAllyClassic,"ROGAlly"},{YmccFamily::AsusRogAllyXClassic,"ROGAllyX"},{YmccFamily::AsusRogAlly,"XboxROGAlly"},{YmccFamily::AsusRogAllyX,"XboxROGAllyX"}};
    const auto setup=[](const Model& model,const json& rules,bool raw=false){
        resetRoute(rules,baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=model.family;g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+model.name;
        g_oemRawPathActive=false;g_oemRawAdmitDev=nullptr;g_oemKbDownRaw=0;g_rog={};fixtureRogReferenceReady=raw;
        if(raw){g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";}
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false,DWORD flags=0){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=flags|(up?LLKHF_UP:0);return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    for(const auto& model:models)for(bool second:{false,true}){
        const DWORD vk=second?VK_F17:VK_F18;const char* id=second?"m2":"m1";
        setup(model,json::array({producerRule(id)}));const auto saved=fixtureInput;
        check(g_oemBackHook&&!g_oemRawPathActive&&g_oemBackProfileId==int(OemChordProfileId::Rog),"actual existing family LL fallback is installed without a virtual target");
        check(key(vk)==1&&key(vk)==1&&key(vk,true)==1,"configured ROG fallback DOWN/repeat/UP are consumed as one paired original gesture");
        check(fixturePhysicalReceipts.size()==2&&fixtureReplayed.empty()&&!oemKeyboardConsumeBusy(),"same LL callback retains actual short source pair without Raw dependency");
        deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.size()==1&&fixtureInput==saved&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled&&fixtureRefreshes==0,"one existing OEM fallback action preserves source/code and no-Host settings");
        tick(0,fixtureTick+2);flushJobs();check(fixtureOwnActions.size()==1,"fallback live neutral cannot replay an already handled pair");
    }
    const auto& model=models[3];
    setup(model,json::array({producerRule("m1")}));
    check(key(VK_F17)==0&&key(VK_F17,true)==0&&key('A')==0&&key('A',true)==0,"unbound ROG side and ordinary keyboard input pass through");
    setup(model,json::array({producerRule("m1")}));
    check(key(VK_F18,false,LLKHF_INJECTED)==0&&key(VK_F18,true,LLKHF_INJECTED)==0&&fixturePhysicalReceipts.empty(),"injected keyboard cannot own an OEM fallback physical receipt");
    setup(model,json::array({producerRule("m1")}));g_machineIdentity.resolved=false;
    check(key(VK_F18)==0&&key(VK_F18,true)==0,"unresolved machine never gains new ordinary fallback suppression");
    setup(model,json::array({producerRule("m1")}));g_machineIdentity.family=YmccFamily::Unknown;
    check(key(VK_F18)==0&&key(VK_F18,true)==0,"a bare Rog profile without actual family never gains suppression");
    setup(model,json::array({producerRule("m1")}));
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    setup(model,json::array({producerRule("m1"),explicitRule}),true);
    check(g_oemRawPathActive&&g_oemBackHook,"explicit unrelated F13 may keep hook while OEM Raw ownership remains qualified");
    check(key(VK_F18)==0&&key(VK_F18,true)==0&&fixturePhysicalReceipts.empty(),"Raw-owned OEM F18 is never swallowed or republished by LL fallback");
    setup(model,json::array({producerRule("m1")}));check(key(VK_F18)==1,"cancellation begins with an already swallowed original DOWN");
    g_shortcutRecordingActive=true;++g_oemShortcutEdgeEpoch;check(key(VK_F18,true)==1,"recording change retains already swallowed original UP");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.empty(),"cancelled original fallback source cannot execute a late action");
    setup(model,json::array({producerRule("m1")}));key(VK_F18);key(VK_F18,true);
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();deliverPhysicalReceipts();flushJobs();
    check(g_oemRawPathActive&&fixtureOwnActions.empty(),"actual Raw owner transition revokes a retained old LL OEM source pair");
    setup(model,json::array({producerRule("m1")}));g_shortcutRecordingActive=true;g_shortcutRecordingOemMask=kOemBitM1;nativeOemKeyboardConsumePublish();
    check(key(VK_F18)==1&&key(VK_F18,true)==1,"recording preserves original configured OEM pair suppression");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.empty(),"recording still cannot execute the configured fallback action");
    setup(model,json::array({producerRule("m1")}));fixturePostOk=false;
    const auto down=key(VK_F18),up=key(VK_F18,true);fixturePostOk=true;
    check((down==0||!fixtureReplayed.empty())&&up==0&&fixtureReplayed.size()==1&&!oemKeyboardConsumeBusy(),"failed receipt posting returns original input by pass/replay without paired debt");
    setup(model,json::array());check(key(VK_F18)==0&&key(VK_F18,true)==0,"unconfigured OEM rules do not authorize global fallback consumption");
}

static void nativeRogLlSourceTransitionCases() {
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    resetRoute(json::array({producerRule("m1"),explicitRule}),baseInput(false,"disabled"));
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
    g_oemRawPathActive=false;g_oemRawAdmitDev=nullptr;g_oemKbDownRaw=0;g_rog={};fixtureRogReferenceReady=false;
    tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    const auto key=[](bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=VK_F18;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    check(key()==1&&key(true)==1&&fixturePhysicalReceipts.size()==2,"source-transition proof starts with original configured consumed LL pair");
    const auto installs=fixtureHookInstalls,removes=fixtureHookRemoves;
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();
    check(g_oemRawPathActive&&g_oemBackHook&&fixtureHookInstalls==installs&&fixtureHookRemoves==removes,"unrelated explicit key keeps the same actual LL hook during Raw ownership change");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
    check(fixtureOwnActions.empty(),"source epoch rejects old LL action when Raw changes without any hook enable transition");
    fixtureRogReferenceReady=false;oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    check(key()==1&&key(true)==1,"new fallback owner admits a fresh real pair after neutral");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==1,"new fallback scope restores action once without replaying old source");
}

static void nativeRogHeldReturnCases() {
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    const auto setup=[&]{
        resetRoute(json::array({producerRule("m1"),producerRule("m2"),explicitRule}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=false;g_rog={};g_oemKbDownRaw=0;g_oemRawAdmitDev=nullptr;
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto raw=[&](bool wanted){fixtureRogReferenceReady=wanted;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);};
    setup();check(key(VK_F18)==1&&oemKeyboardConsumeBusy(),"held return begins with one swallowed physical LL M1 down");
    raw(true);deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();tick(0,fixtureTick+1);
    check(g_oemRawPathActive&&fixtureOwnActions.empty()&&g_oemKeyboardConsume.physicalDown[VK_F18],"actual Raw interval preserves real held debt and revokes old queued action");
    raw(false);
    check(!g_oemRawPathActive&&g_oemKeyboardConsume.physicalDown[VK_F18]&&g_oemKeyboardConsume.visibility[VK_F18]==3,"actual LL return occurs before any physical release");
    check(key(VK_F18)==1,"returned LL repeat remains swallowed as the original delivery pair");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
    check(fixtureOwnActions.empty(),"LL return must not turn a still-held old key repeat into a new configured press action");
    check(key(VK_F18,true)==1&&!oemKeyboardConsumeBusy(),"only the actual UP retires the original swallowed pair");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();tick(0,fixtureTick+1);
    check(key(VK_F18)==1&&key(VK_F18,true)==1,"fresh real down/up after neutral is available in returned LL source");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==1,"returned LL emits once only after a fresh real pair");
    setup();raw(true);
    check(key(VK_F17)==0,"unowned LL key first seen during Raw is passed to its original consumer");
    raw(false);key(VK_F17);deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
    check(fixtureOwnActions.empty(),"LL return requires real neutral even for a key first held while Raw owned actions");
    check(key(VK_F17,true)==0&&!oemKeyboardConsumeBusy(),"already passed Raw-era key receives an ordinary matching UP");
}

static void nativeRogSourceOwnerCases() {
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    const auto setup=[&]{
        resetRoute(json::array({producerRule("m1"),producerRule("m2"),explicitRule}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
        g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;
        g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
        fixtureRogReferenceReady=false;g_rog={};g_oemKbDownRaw=0;g_oemRawAdmitDev=nullptr;
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;
        return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto uiKernelTid=fixtureKernelThreadId();DWORD workerKernelTid=0;
    const auto gateWorker=[&]{std::thread worker([&]{fixtureThreadId=200;workerKernelTid=fixtureKernelThreadId();
        oemBackMappingRefreshGate(false,"disabled",false);});worker.join();};
    setup();check(key(VK_F18)==1&&g_oemKbDown==oemBackHookVkBit(VK_F18),"owner control starts with real LL consumed held M1");
    const auto held=g_oemKbDown;const auto publication=g_oemKeyMask.load();const auto installs=fixtureHookInstalls,removes=fixtureHookRemoves;
    const auto epoch=g_oemShortcutEdgeEpoch.load();
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
    gateWorker();
    check(workerKernelTid!=uiKernelTid,"production gate executed on a distinct real worker with memory OS boundaries");
    check(g_oemKbDown==held&&g_oemKeyMask==publication&&!g_oemRawPathActive&&g_oemShortcutEdgeEpoch==epoch,
        "worker gate must not mutate UI LL publication or live Raw ownership before UI reconcile");
    check(fixtureReconcilePosts>0&&fixtureHookInstalls==installs&&fixtureHookRemoves==removes,"same-hook source change posts original UI reconcile without worker hook changes");
    fixtureThreadId=200;oemBackMappingReconcileOnUi();fixtureThreadId=100;
    check(g_oemKbDown==held&&!g_oemRawPathActive,"non-owner reconcile refuses all UI private publication work");
    oemBackMappingReconcileOnUi();
    check(g_oemRawPathActive&&g_oemKbDown==0&&g_oemShortcutEdgeEpoch==epoch+1,"UI owner atomically orders actual Raw transition and old LL publication withdrawal");
    check(g_oemKeyboardConsume.physicalDown[VK_F18]&&g_oemKeyboardConsume.visibility[VK_F18]==3&&oemKeyboardConsumeBusy(),"UI source transition retains physical held and swallowed-UP debt");
    check(key(VK_F18)==1&&key(VK_F18,true)==1&&!oemKeyboardConsumeBusy(),"original repeat and actual UP remain paired after worker handoff");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();tick(0,fixtureTick+1);
    g_oemKbDownRaw=oemBackHookVkBit(VK_F17);oemBackHookRecompute();tick(0,fixtureTick+1);flushJobs();
    check(nativeOemKeyMask(true)==kOemBitM2&&fixtureOwnActions.size()==1,"new supplied qualified Raw M2 acts once after UI withdrawal without virtual target");
    const auto rawBefore=g_oemKbDownRaw.load();fixtureRogReferenceReady=false;fixturePostOk=false;gateWorker();
    check(g_oemRawPathActive&&g_oemKbDownRaw==rawBefore&&g_oemRawVkAllowed[VK_F17],"failed worker post leaves live Raw state and allow table owned by UI");
    const auto failureCount=g_oemShortcutEdgePostFailures.load();
    fixturePostOk=true;nativeOemKeyboardConsumeMaintain(++fixtureTick);
    check(failureCount>0&&!g_oemRawPathActive&&g_oemKbDownRaw==0&&!g_oemRawVkAllowed[VK_F17],"original UI maintenance settles failed post and only then withdraws Raw state/table");
    tick(0,fixtureTick+1);check(key(VK_F18)==1&&key(VK_F18,true)==1,"new LL fallback remains usable after worker requested Raw off");
    deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==2,"returned LL fallback action remains exactly once with no Host");
    setup();const auto coalescedEpoch=g_oemShortcutEdgeEpoch.load();
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";gateWorker();
    fixtureRogReferenceReady=false;gateWorker();oemBackMappingReconcileOnUi();
    check(!g_oemRawPathActive&&g_oemShortcutEdgeEpoch==coalescedEpoch,"on-off requests coalesce before UI without fabricating a source epoch");
    setup();fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";gateWorker();
    g_exitRequested=true;oemBackMappingReconcileOnUi();
    check(!g_oemRawPathActive&&g_oemKbDown==0,"exit prevents late wanted Raw source from becoming live");
    setup();fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";gateWorker();
    g_oemBackProfileId=int(OemChordProfileId::GpdWin5);oemBackMappingReconcileOnUi();
    check(!g_oemRawPathActive,"new non-ROG profile cannot borrow an older ROG Raw request");
    setup();key(VK_F17);const auto oldEpoch=g_oemShortcutEdgeEpoch.load();
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";gateWorker();
    check(key(VK_F17,true)==1&&!oemKeyboardConsumeBusy(),"actual UI UP before queued handoff repays old pair normally");
    oemBackMappingReconcileOnUi();deliverPhysicalReceipts();flushJobs();
    check(g_oemRawPathActive&&g_oemKbDown==0&&g_oemShortcutEdgeEpoch==oldEpoch+1&&fixtureOwnActions.empty(),"UI handoff revokes old queued action even when actual UP arrived before reconcile");
}

static void nativeRogHeldSourceCases() {
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    resetRoute(json::array({producerRule("m1"),producerRule("m2"),explicitRule}),baseInput(false,"disabled"));
    g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
    g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";
    fixtureRogReferenceReady=false;g_rog={};g_oemRawPathActive=false;g_oemKbDownRaw=0;g_oemRawAdmitDev=nullptr;
    tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    check(key(VK_F18)==1&&g_oemKbDown==oemBackHookVkBit(VK_F18),"old actual LL source begins with a consumed held M1 DOWN");
    const auto installs=fixtureHookInstalls,removes=fixtureHookRemoves;
    fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
    oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();
    check(g_oemRawPathActive&&g_oemBackHook&&fixtureHookInstalls==installs&&fixtureHookRemoves==removes,"Raw owner takes over while explicit F13 keeps the same LL hook");
    check(key(VK_F18,true)==1&&!oemKeyboardConsumeBusy(),"Raw-era UP repays the old swallowed LL pair without a synthetic release");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
    tick(0,fixtureTick+1);
    // Already-qualified Raw keyboard state is the explicit input boundary.
    // Actual OS hDevice admission is not invented/executed by this fixture.
    g_oemKbDownRaw=oemBackHookVkBit(VK_F17);oemBackHookRecompute();
    tick(0,fixtureTick+1);flushJobs();
    check(fixtureOwnActions.size()==1,"new qualified Raw M2 action is not blocked by a retired LL M1 held shadow");
    check(nativeOemKeyMask(true)==kOemBitM2&&g_oemKbDown==0,"Raw publication contains only its real current source, with retired LL state withdrawn");
    g_oemKbDownRaw=0;oemBackHookRecompute();tick(0,fixtureTick+1);flushJobs();
    check(nativeOemKeyMask(true)==0&&fixtureOwnActions.size()==1,"real Raw neutral does not retain or replay old LL M1 state");
    fixtureRogReferenceReady=false;oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    check(key(VK_F18)==1&&key(VK_F18,true)==1,"fresh LL source pair is usable after an actual neutral return");
    deliverPhysicalReceipts();flushJobs();
    check(fixtureOwnActions.size()==2&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,"new fallback M1 action works once with no virtual target");
}

static void nativeRogHeldSourceBoundaryCases() {
    auto explicitRule=rule("os.openTouchKeyboard","unused","press","keyboard","F13");explicitRule["params"]=json::object();
    const auto setup=[&] {
        resetRoute(json::array({producerRule("m1"),producerRule("m2"),explicitRule}),baseInput(false,"disabled"));
        g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=false;
        g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";
        fixtureRogReferenceReady=false;g_rog={};g_oemRawPathActive=false;g_oemKbDownRaw=0;g_oemRawAdmitDev=nullptr;
        tick(0,fixtureTick+1);oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick+1);
    };
    const auto key=[](DWORD vk,bool up=false){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;k.flags=up?LLKHF_UP:0;return oemBackHookProc(HC_ACTION,up?WM_KEYUP:WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));};
    const auto rawOn=[] {fixtureRogReferenceReady=true;g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();};
    setup();check(key(VK_F17)==1,"symmetric old LL source begins with a swallowed M2 DOWN");
    rawOn();
    check(g_oemKbDown==0&&g_oemKeyboardConsume.physicalDown[VK_F17]&&g_oemKeyboardConsume.visibility[VK_F17]==3&&oemKeyboardConsumeBusy(),"source withdrawal keeps real held ledger and original paired-UP debt");
    check(key(VK_F17)==1&&key(VK_F17,true)==1&&!oemKeyboardConsumeBusy(),"retained Raw-era repeat and actual UP repay only the original delivery pair");
    deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();tick(0,fixtureTick+1);
    g_oemKbDownRaw=oemBackHookVkBit(VK_F18);oemBackHookRecompute();tick(0,fixtureTick+1);flushJobs();
    check(fixtureOwnActions.size()==1&&nativeOemKeyMask(true)==kOemBitM1,"symmetric new Raw M1 action remains usable without old LL M2 contamination");
    setup();key(VK_F18);
    // Pure current Raw input is supplied at the existing shared-state boundary;
    // this checks preservation, not real OS delivery order or device proof.
    const auto rawState=oemBackHookVkBit(VK_F17);g_oemKbDownRaw=rawState;
    rawOn();
    check(g_oemKbDownRaw==rawState&&nativeOemKeyMask(true)==kOemBitM2&&g_oemKbDown==0,"entering Raw withdraws only LL publication and preserves independent Raw observed state");
    check(key(VK_F18,true)==1,"independent Raw state preservation does not release old swallowed key synthetically");
    setup();key(VK_F18);rawOn();tick(0,fixtureTick+1);
    check(fixtureOwnActions.empty(),"source change cannot turn a still-held old LL key into a new Raw action");
    g_shortcutRecordingActive=true;++g_oemShortcutEdgeEpoch;check(key(VK_F18,true)==1,"recording revocation preserves the old real-UP ownership after withdrawal");
}

static void nativeOemGateTransitionCases(unsigned firstCase=0) {
    struct Case {YmccFamily family;const char* model;const char* key;};
    const Case cases[]={
        {YmccFamily::Ayanero,"HandheldCompanion.Devices.AYANEOAIRPro","ckbig"},
        {YmccFamily::MsiClaw,"HandheldCompanion.Devices.ClawA1M","m1"},
        {YmccFamily::Gpd,"HandheldCompanion.Devices.GPDWin5","l4"},
        {YmccFamily::OneXPlayer,"HandheldCompanion.Devices.OneXPlayerMiniAMD","orange"},
        {YmccFamily::Aokzoe,"HandheldCompanion.Devices.AOKZOEA1","home"},
        {YmccFamily::ZotacGamingZone,"HandheldCompanion.Devices.GamingZone","m1"}};
    struct Observation {uint32_t matched=0,replayed=0;unsigned events=0,suppressed=0;bool paired=false;};
    const auto emit=[](const OemChordRule& chord){
        Observation observation;
        uint32_t remaining=chord.keys;std::vector<DWORD> keys;
        for(DWORD vk=1;vk<256;++vk){const auto bit=oemBackHookVkBit(vk);if(bit&&(remaining&bit)){keys.push_back(vk);remaining&=~bit;}}
        check(!remaining&&!keys.empty(),"gate transition uses only the current profile VK decoder");
        for(const auto vk:keys){++fixtureTick;KBDLLHOOKSTRUCT key{};key.vkCode=vk;++observation.events;observation.suppressed+=oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&key))!=0;}
        for(auto it=keys.rbegin();it!=keys.rend();++it){++fixtureTick;KBDLLHOOKSTRUCT key{};key.vkCode=*it;key.flags=LLKHF_UP;++observation.events;observation.suppressed+=oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&key))!=0;}
        observation.matched=g_oemConsumeMatched.load();observation.replayed=g_oemConsumeReplayed.load();
        observation.paired=!g_oemKeyboardConsume.replayCount&&!oemKeyboardConsumeBusy()&&
            std::all_of(std::begin(g_oemKeyboardConsume.visibility),std::end(g_oemKeyboardConsume.visibility),[](auto value){return value==0;})&&
            std::none_of(std::begin(g_oemKeyboardConsume.physicalDown),std::end(g_oemKeyboardConsume.physicalDown),[](bool down){return down;});
        deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();
        return observation;
    };
    for(unsigned caseIndex=firstCase;caseIndex<std::size(cases);++caseIndex){
        const auto& item=cases[caseIndex];
        resetRoute(json::array({producerRule(item.key)}),baseInput(false,"disabled"));
        g_machineIdentity.family=item.family;g_machineIdentity.deviceClass=item.model;g_machineIdentity.resolved=true;
        g_oemHooksWanted=false;g_oemKeyHooksActive=false;g_oemBackHook=g_oemMouseHook=nullptr;
        fixtureHookInstalls=fixtureHookRemoves=0;fixtureHookInstallOk=true;
        g_oemRawPathActive=false;g_oemRawAdmitDev=nullptr;fixtureRogReferenceReady=false;g_rog={};
        const auto& binding=oemUnifiedInputBindingFor(item.family,item.model);const auto& profile=oemChordProfileFor(binding.profileId);
        uint64_t bit=0;check(nativeKeyboardShortcutMaskBit(item.key,bit),"gate key keeps existing canonical identity");
        const OemChordRule* chord=nullptr;for(int i=0;i<profile.count;++i)if(profile.rules[i].bit==bit&&!profile.rules[i].mouseChord){chord=&profile.rules[i];break;}
        check(chord!=nullptr,"gate uses actual bound family chord: "+std::string(item.model));
        for(unsigned phase=0;phase<3;++phase){
            const bool virtualWanted=phase==1;const std::string persona=virtualWanted?"steamdeck":"disabled";
            fixtureInput["revision"]=20+phase;fixtureInput["outputTarget"]["persona"]=persona;
            fixtureInput["outputTarget"]["buttonMappingEnabled"]=virtualWanted;
            g_realStickTest.enabled=g_realStickTest.virtualTargetEnabled=virtualWanted;
            oemBackMappingRefreshGate(virtualWanted,persona,false);tick(0,fixtureTick+1);
            const std::string label=std::string(item.model)+"/"+(phase==0?"cold-native":phase==1?"virtual-intent":"returned-native-intent");
            check(g_oemHooksWanted&&g_oemKeyHooksActive&&g_oemBackProfileId==binding.profileId,"actual gate retains key capture: "+label);
            check(g_oemBackMappingEnabled==virtualWanted,"output rear mapping stays separate from action capture: "+label);
            const auto matched=g_oemConsumeMatched.load();const auto observed=emit(*chord);
            check(fixtureOwnActions.size()==phase+1,"bound action executes after actual gate transition: "+label);
            check(observed.matched==matched+1&&observed.replayed==0&&observed.events==observed.suppressed&&observed.paired,
                "match before normal drain, all down/up pairs suppressed without replay: "+label);
            check(!g_inputHostProcess&&!g_inputHostPrepared&&fixtureRefreshes==0,"action does not require or create Host: "+label);
            check(fixtureHookInstalls==2&&fixtureHookRemoves==0,"output intent does not reinstall capture hooks: "+label);
        }
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();fixtureInput["revision"]=24;
        fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1; // observe the original settings refresh cadence
        tick(0,fixtureTick+1);const auto actions=fixtureOwnActions.size();emit(*chord);
        check(fixtureOwnActions.size()==actions,"native unbinding does not keep an old action: "+std::string(item.model));
        g_exitRequested=true;oemBackMappingReconcileOnUi();
        check(!g_oemBackHook&&!g_oemMouseHook&&!g_oemKeyHooksActive&&fixtureHookRemoves==2,"actual hook owner retires on exit: "+std::string(item.model));
    }
    resetRoute(json::array());g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.deviceClass="HandheldCompanion.Devices.XboxROGAllyX";g_machineIdentity.resolved=true;
    g_oemHooksWanted=false;g_oemKeyHooksActive=false;g_oemBackHook=g_oemMouseHook=nullptr;
    g_rog={};g_rog.hasControl=true;fixtureRogReferenceReady=true;g_oemRawPathActive=false;
    for(const bool virtualWanted:{false,true,false}){
        oemBackMappingRefreshGate(virtualWanted,virtualWanted?"steamdeck":"disabled",false);
        check(g_oemRawPathActive&&!g_oemHooksWanted,"ROG qualified Raw Input ownership survives output intent changes");
        check(g_oemRawVkAllowed[VK_F17]&&g_oemRawVkAllowed[VK_F18]&&!g_oemRawVkAllowed['A'],"ROG keeps exact existing raw keyboard scope");
    }
    g_exitRequested=true;oemBackMappingReconcileOnUi();
}
#endif
static void oneXConsumptionCases() {
    const auto prepare=[](const std::string& cls,const char* key,const std::string& persona,bool configured=true){
        resetRoute(configured?json::array({producerRule(key)}):json::array(),baseInput(false,persona));
        g_machineIdentity.family=YmccFamily::OneXPlayer;g_machineIdentity.deviceClass=cls;
        g_oemBackProfileId=oemUnifiedInputBindingFor(g_machineIdentity.family,cls).profileId;
        g_oemHooksWanted=true;g_oemKeyHooksActive=true;
        fixtureFrontendPersona=g_inputHostBoundPersona=persona;
        g_inputHostPrepared=persona!="disabled";g_inputHostProcess=g_inputHostPrepared?reinterpret_cast<HANDLE>(1):nullptr;
        g_realStickTest.virtualTargetEnabled=persona!="disabled";
        tick(0,fixtureTick);
    };
    const auto sendKeyboard=[](uint32_t bits){
        std::vector<DWORD>keys;
        uint32_t remaining=bits;
        for(DWORD vk=1;vk<256;++vk){const auto bit=oemBackHookVkBit(vk);if(bit&&(remaining&bit)){keys.push_back(vk);remaining&=~bit;}}
        check(remaining==0&&!keys.empty(),"OneX chord is assembled from actual VK decoder");
        for(const auto vk:keys){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=vk;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&k));}
        for(auto it=keys.rbegin();it!=keys.rend();++it){++fixtureTick;KBDLLHOOKSTRUCT k{};k.vkCode=*it;k.flags=LLKHF_UP;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&k));}
        deliverPhysicalReceipts();
    };
    for(const auto& cls:fixtureOneXClasses){
        const auto& binding=oemUnifiedInputBindingFor(YmccFamily::OneXPlayer,cls);
        const auto catalog=binding.catalog;
        const std::string captureClass=binding.model?std::string(binding.model->consumerClass):cls;
        if(cls.find("OneXPlayerG1")!=std::string::npos){
            check(catalog.count==0,"OneX G1 missing OEM chords stays explicit: "+cls);continue;
        }
        check(catalog.count>0,"OneX consumer has exact real capability binding: "+cls);
        const auto profile=oemChordProfileFor(binding.profileId);
        for(int keyIndex=0;keyIndex<catalog.count;++keyIndex){
            const auto& key=catalog.keys[keyIndex];uint64_t bit=0;
            check(nativeKeyboardShortcutMaskBit(key.keyId,bit),"OneX real catalog action ID resolves: "+cls+"/"+key.keyId);
            const OemChordRule* chord=nullptr;
            for(int i=0;i<profile.count;++i)if(profile.rules[i].bit==bit&&!profile.rules[i].mouseChord){chord=&profile.rules[i];break;}
            const bool legacyHid=ymcc::capabilities::oem::onex::descriptorFor(captureClass).valid&&
                (std::string(key.keyId)=="l4"||std::string(key.keyId)=="r4"||std::string(key.keyId)=="special"||
                 (std::string(key.keyId)=="orange"&&ymcc::capabilities::oem::onex::descriptorFor(captureClass).role21Orange)||
                 std::string(key.keyId)=="keyboard");
            const bool x2Family=captureClass=="HandheldCompanion.Devices.OneXPlayerX2"||
                captureClass=="HandheldCompanion.Devices.OneXPlayer3"||captureClass=="HandheldCompanion.Devices.OneXPlayerX2MiniPro";
            const bool x2Source=x2Family&&(std::string(key.keyId)=="l4"||std::string(key.keyId)=="r4"||
                std::string(key.keyId)=="home"||std::string(key.keyId)=="gkey");
            check(chord||legacyHid||x2Source,"OneX every declared action has a tested input consumer: "+cls+"/"+key.keyId);
            const auto emit=[&](){
                if(chord){sendKeyboard(chord->keys);return;}
                if(legacyHid){
                    activateProducer(OemProducerId::OneX);g_onexBack.desc=onexCapDescriptorFor(captureClass);
                    producerSample(OemProducerId::OneX,0);nativeOemProducerDrain();
                    ++fixtureTick;producerSample(OemProducerId::OneX,bit);++fixtureTick;producerSample(OemProducerId::OneX,0);nativeOemProducerDrain();return;
                }
                // The actual device protocol/owner has its own transport tests.
                // Supply only its published-ready observation, then execute the
                // real retained publication/dispatch; do not invent a HID packet.
                g_onexX2PublishedPower=currentPowerGeneration();g_onexX2PublishedTopology=g_onexX2DeviceEpoch.load();g_onexX2PublishedReady=true;
                if(std::string(key.keyId)=="home"&&captureClass!="HandheldCompanion.Devices.OneXPlayer3"){
                    const auto generation=oemProducerBegin(OemProducerId::OneXX2Home);
                    check(oemProducerPublishOnexX2Home(generation,++fixtureTick,0x24),"OneX X2 Home uses actual click publication");
                }else{
                    const auto generation=oemProducerBegin(OemProducerId::OneXX2Rear);
                    check(oemProducerPublish(OemProducerId::OneXX2Rear,generation,0,bit),"OneX held event establishes its own neutral");nativeOemProducerDrain();
                    ++fixtureTick;check(oemProducerPublish(OemProducerId::OneXX2Rear,generation,bit,bit),"OneX held event publishes press");
                    ++fixtureTick;check(oemProducerPublish(OemProducerId::OneXX2Rear,generation,0,bit),"OneX held event publishes release");
                }
                nativeOemProducerDrain();
            };
            for(const std::string persona:{"disabled","steamdeck","elite"}){
                const std::string label=cls+"/"+key.keyId+"/"+persona;
                prepare(cls,key.keyId,persona);const json saved=fixtureInput;
                emit();
                check(fixtureOwnActions.empty()&&fixtureJobs.size()==1,"OneX action uses one existing serial job: "+label);
                flushJobs();check(fixtureOwnActions.size()==1,"OneX configured key reaches real own action once: "+label);
                check(fixtureRefreshes==0&&fixtureInput==saved,"OneX own action preserves virtual intent and durable settings: "+label);
                nativeOemProducerDrain();tick(0,fixtureTick+1);flushJobs();
                check(fixtureOwnActions.size()==1,"OneX live neutral does not replay consumed edge: "+label);
                // The same captured input with no saved action must not grow a binding.
                prepare(cls,key.keyId,persona,false);
                emit();flushJobs();check(fixtureOwnActions.empty()&&g_nativeKeyboardShortcuts.empty(),"OneX unbound key creates no automatic action: "+label);
                for(const char* cancellation:{"recording","exit","rule-epoch"}){
                    prepare(cls,key.keyId,persona);emit();
                    check(fixtureJobs.size()==1,"OneX cancellation starts with one admitted action: "+label);
                    if(std::string(cancellation)=="recording")g_shortcutRecordingActive=true;
                    else if(std::string(cancellation)=="exit")g_exitRequested=true;
                    else ++g_oemShortcutEdgeEpoch;
                    flushJobs();check(fixtureOwnActions.empty(),"OneX queued own action rejects "+std::string(cancellation)+": "+label);
                }
            }
        }
    }
}

#endif

// Shared click sources must execute configured own actions without any virtual Host.
#if defined(YMCC_WMI_CLICK_SOURCE) && defined(YMCC_ROG_CLICK_SOURCE)
static void nativeOemClickActionsCases() {
    auto noHost=[] {
        g_realStickTest.enabled=false;g_realStickTest.virtualTargetEnabled=false;
        g_inputHostPrepared=g_inputHostNeutralized=false;g_inputHostProcess=nullptr;
    };
    auto bound=[](const char* key,const char* trigger="press") {
        auto item=rule("os.openTouchKeyboard","unused",trigger,"oem",key);
        item["params"]=json::object();return item;
    };
    resetRoute(json::array({bound("claw"),bound("qs")}));noHost();tick(0,10000);
    auto generation=activateWmi();auto* sink=new MsiWmiSink(generation);
    FixtureWmiObject claw;claw.code=41;FixtureWmiObject qs;qs.code=88;
    IWbemClassObject* objects[]={&claw,&qs};sink->Indicate(2,objects);
    nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==2&&fixtureLegacyClaw==0,"native MSI bound CLAW/QS execute without Host or physical-release fallback");
    check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_realStickTest.virtualTargetEnabled,"MSI configured actions never auto-create or enable virtual output");
    nativeMsiWmiShortcutDrain();flushJobs();check(fixtureOwnActions.size()==2,"consumed WMI clicks cannot replay on idle");
    // OFF does not end the independent WMI subscription or invalidate a fresh key.
    g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetEnabled=false;
    fixtureTick+=100;sink->Indicate(2,objects);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==4,"MSI bound clicks still execute after virtual ON to OFF");
    fixtureTick+=100;sink->Indicate(1,objects);nativeMsiWmiShortcutDrain();
    g_shortcutRecordingActive=true;flushJobs();check(fixtureOwnActions.size()==4,"recording cancels queued native WMI action");g_shortcutRecordingActive=false;
    sink->Release();msiWmiWithdrawGeneration(generation);
    const struct {BYTE code;const char* key;uint64_t bit;} cases[]={{56,"ac",kOemBitAc},{147,"lib",kOemBitLib},{166,"cc",kOemBitCc}};
    for(const auto& item:cases){
        resetRoute(json::array({bound(item.key)}));noHost();tick(0,10000);
        const auto rg=activateProducer(OemProducerId::RogClick);
        const BYTE report[]={0x5A,item.code};fixtureTick=10100;rogOemKeyParse(report,sizeof(report),rg);
        nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1&&fixtureLegacyRog==0,"native ROG configured click uses common action without virtual default");
        check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_realStickTest.virtualTargetEnabled,"ROG click never creates or enables virtual output");
        g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetEnabled=false;
        fixtureTick=10200;rogOemKeyParse(report,sizeof(report),rg);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==2,"native ROG configured click survives virtual ON to OFF");
        fixtureTick=10300;rogOemKeyParse(report,sizeof(report),rg);nativeOemProducerDrain();
        rogOemKeyClose();flushJobs();check(fixtureOwnActions.size()==2,"retired native HID source cannot execute queued action");
    }
    resetRoute(json::array({bound("claw","hold")}));noHost();tick(0,10000);
    generation=activateWmi();msiWmiHandleEvent(41,generation);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureLegacyClaw==0,"click-only source reports unsupported hold without inventing action/default");
}
#endif

#if defined(YMCC_SHORTCUT_VIRTUAL_REAR_CASE)
#if defined(YMCC_SHORTCUT_VIRTUAL_UNKNOWN_CASE)
static void nativeOemVirtualUnknownCases() {
    fixtureOemVirtualAllocationFailure=false;
    auto mixed=producerRule("legionr");mixed["inputs"].push_back({{"source","controller"},{"code","a"}});
    resetRoute(json::array({mixed}));activateProducer(OemProducerId::Legion);
    producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitLegionR;
    fixtureOemVirtualAllocationFailure=true;g_oemKbDown=kVkF13;oemBackHookRecompute();
    check(!g_oemVirtualKeyboardSample.load(),"allocation control creates an actual unknown keyboard snapshot");
    producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3,"unknown keyboard snapshot cannot suppress incomplete OEM/controller binding");

    fixtureOemVirtualAllocationFailure=false;resetRoute(json::array({producerRule("m1")}));
    g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    g_oemKbDown=kVkF11;oemBackHookRecompute();
    check(fixtureVirtualRear()==0,"unknown-lifetime control establishes keyboard held reservation");
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    fixtureOemVirtualAllocationFailure=true;g_oemKbDown=kVkF11|kVkF14;oemBackHookRecompute();
    check(!g_oemVirtualKeyboardSample.load()&&fixtureVirtualRear()==0,"unknown publication does not repay the original held fence");
    fixtureOemVirtualAllocationFailure=false;g_oemKbDown=kVkF11;oemBackHookRecompute();
    check(fixtureVirtualRear()==0,"allocation recovery with no physical UP preserves the same press identity");
    g_oemKbDown=0;oemBackHookRecompute();g_oemKbDown=kVkF11;oemBackHookRecompute();
    check(fixtureVirtualRear()==3,"real UP after unknown allows the next unbound keyboard press");
    fixtureOemVirtualAllocationFailure=false;resetRoute(json::array({producerRule("m1")}));
    g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
    g_oemKbDown=kVkF11;oemBackHookRecompute();(void)fixtureVirtualRear();
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    g_oemKbDown=0;oemBackHookRecompute();tick(0,fixtureTick+501);
    check(!g_oemVirtualKeyboardSampleWanted&&!g_oemVirtualReservationsActive,
        "real keyboard UP drains the old lease on the existing UI lane while virtual Host is OFF");
}
#endif

#if defined(YMCC_SHORTCUT_VIRTUAL_GESTURE_CASE)
static void nativeOemVirtualGestureCases() {
    const auto setup=[](json item=producerRule("legionr")) {
        resetRoute(json::array({item}));fixtureVirtualAsyncInterleave={};activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitLegionR;
    };
    for(const char* trigger:{"hold","double","release","repeat"}) {
        setup(producerRule("legionr",trigger));producerSample(OemProducerId::Legion,kOemBitLegionR);
        check(fixtureVirtualRear()==0&&fixtureOwnActions.empty(),std::string("gesture reserves first frame without inventing early action: ")+trigger);
        nativeOemProducerDrain();
        if(std::string(trigger)=="double") {
            producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();
            producerSample(OemProducerId::Legion,kOemBitLegionR);nativeOemProducerDrain();
        } else if(std::string(trigger)=="release") {
            producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();
        } else tick(0,fixtureTick+600);
        flushJobs();
        check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,std::string("original gesture action still executes once: ")+trigger);
    }
    for(const char* boundary:{"recording","power","writer","exit"}) {
        setup();producerSample(OemProducerId::Legion,kOemBitLegionR);
        const auto savedPower=fixturePowerGeneration;
        if(std::string(boundary)=="recording")g_shortcutRecordingActive=true;
        if(std::string(boundary)=="power")++fixturePowerGeneration;
        if(std::string(boundary)=="writer")fixtureWriterAllowed=false;
        if(std::string(boundary)=="exit")g_exitRequested=true;
        check(fixtureVirtualRear()==3,std::string("revoked qualification cannot create new output ownership: ")+boundary);
        g_shortcutRecordingActive=false;g_exitRequested=false;fixtureWriterAllowed=true;fixturePowerGeneration=savedPower;
    }
    auto mixed=producerRule("legionr");mixed["inputs"].push_back({{"source","keyboard"},{"code","KeyQ"}});
    setup(mixed);producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3,"missing desktop member cannot reserve only the OEM subset");
    fixtureDesktopDown.insert('Q');
    check(fixtureVirtualRear()==0,"complete desktop/OEM gesture is reserved at first final projection");

    setup(mixed);producerSample(OemProducerId::Legion,kOemBitLegionR);fixtureDesktopDown.insert('Q');
    fixtureVirtualAsyncInterleave=[] {g_oemVirtualBindingPolicy.store(nullptr);};
    check(fixtureVirtualRear()==3,"policy replaced during desktop member observation cannot commit old reservation");

    setup(mixed);producerSample(OemProducerId::Legion,kOemBitLegionR);fixtureDesktopDown.insert('Q');
    fixtureVirtualAsyncInterleave=[] {++fixturePowerGeneration;};
    check(fixtureVirtualRear()==3,"power change during desktop member observation rejects mixed-version reservation");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);check(fixtureVirtualRear()==0,"held-revoke control establishes source reservation");
    ++fixturePowerGeneration;g_shortcutRecordingActive=true;
    check(fixtureVirtualRear()==0,"recording/power revocation preserves drain-only held ownership without old action permission");
    g_shortcutRecordingActive=false;producerSample(OemProducerId::Legion,0);
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    producerSample(OemProducerId::Legion,kOemBitLegionR);check(fixtureVirtualRear()==3,"true UP returns revoked gesture without transferring old action authority");

    auto controllerMixed=producerRule("legionr");controllerMixed["inputs"].push_back({{"source","controller"},{"code","a"}});
    setup(controllerMixed);producerSample(OemProducerId::Legion,kOemBitLegionR);
    fixtureFrontendPersona="steamdeck";g_oemBackMappingEnabled=true;g_curPad.wButtons=XINPUT_GAMEPAD_A;
    (void)msiBackkeyEffectiveBackBits(0,false);
    XINPUT_GAMEPAD actual{};actual.wButtons=XINPUT_GAMEPAD_B;
    check(fixtureVirtualRear("steamdeck",false,0,true,actual)==3,"diagnostic recomputation cannot reserve a gesture using stale UI controller input");
}
#endif

static void nativeOemVirtualKeyboardMsiCases(bool tail=false) {
    const char* codes[]={"m1","m2","cc","ac","lib","claw","qs","ckbig","cksmall","cktl","cktr","ayat","r4","l4","gamepad","keyboard","home","legionr","legionl","special","turbo","special2","homelp","homekb","hometb","guide","lcc","zotac","dots","screen","win","esc","orange","function","orangekb","orangetb","orangelp"};
    unsigned rows=0,profiles=0;
    for(int profile=0;!tail&&profile<static_cast<int>(OemChordProfileId::None);++profile) {
        bool visited=false;
        for(int row=0;row<kOemProfiles[profile].count;++row) {
            const auto chord=kOemProfiles[profile].rules[row];if(!chord.bit||(!chord.keys&&!chord.mouseChord))continue;
            uint64_t observed=0;int expectedBack=0,physical=0;uint32_t base=0,prefix=0;
            oemChordEvalRules(profile,chord.keys,chord.mouseChord,true,&observed,&expectedBack,&physical,&base,&prefix);
            const std::string label=std::to_string(profile)+"/"+std::to_string(row);
            check((observed&chord.bit)!=0,"keyboard declared row reaches its canonical source: "+label);
            json inputs=json::array();uint64_t bound=0;
            for(const char* code:codes){uint64_t bit=0;if(nativeKeyboardShortcutMaskBit(code,bit)&&(observed&bit)){inputs.push_back({{"source","oem"},{"code",code}});bound|=bit;}}
            check(bound==observed&&inputs.size()<=4,"keyboard actual full gesture has canonical recorded inputs: "+label);
            auto item=producerRule("m1");item["inputs"]=inputs;
            resetRoute(json::array({item}));g_oemBackProfileId=profile;
            g_inputHostProcess=nullptr;g_realStickTest.virtualTargetEnabled=false;
            const bool raw=profile==static_cast<int>(OemChordProfileId::Rog);
            if(raw) {
                g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::AsusRogAllyClassic;
                g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";
                fixtureRogReferenceReady=true;g_rog={};g_rog.hasControl=true;g_rog.controlPath=L"qualified-rog";
                oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();
                check(g_oemRawPathActive,"ROG raw sample uses the original admitted memory binding");
            }
            tick(0,fixtureTick);g_oemRearMapToL5=observed;g_oemRearMapToR5=observed;
            std::vector<DWORD> physicalKeys;uint32_t covered=0;
            const auto addPhysical=[&](DWORD vk){const auto bit=oemBackHookVkBit(vk);if(bit&&(chord.keys&bit)&&!(covered&bit)){physicalKeys.push_back(vk);covered|=bit;}};
            for(DWORD vk:{VK_LCONTROL,VK_LSHIFT,VK_LMENU,VK_LWIN})addPhysical(vk);
            for(DWORD vk=1;vk<256;++vk)addPhysical(vk);
            check(covered==chord.keys,"keyboard workload covers every declared source key: "+label);
            const auto publish=[&](bool down) {
                if(raw){g_oemKbDownRaw=down?chord.keys:0;oemBackHookRecompute();return;}
                if(chord.mouseChord){
                    MSLLHOOKSTRUCT mouse{};mouse.pt={500,300};
                    if(down){oemBackMouseHookProc(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&mouse));mouse.mouseData=static_cast<DWORD>(XBUTTON2)<<16;oemBackMouseHookProc(HC_ACTION,WM_XBUTTONDOWN,reinterpret_cast<LPARAM>(&mouse));}
                    else{mouse.mouseData=static_cast<DWORD>(XBUTTON2)<<16;oemBackMouseHookProc(HC_ACTION,WM_XBUTTONUP,reinterpret_cast<LPARAM>(&mouse));mouse.mouseData=0;oemBackMouseHookProc(HC_ACTION,WM_LBUTTONUP,reinterpret_cast<LPARAM>(&mouse));}
                } else {
                    const auto key=[&](DWORD vk){KBDLLHOOKSTRUCT event{};event.vkCode=vk;event.flags=down?0:LLKHF_UP;oemBackHookProc(HC_ACTION,down?WM_KEYDOWN:WM_KEYUP,reinterpret_cast<LPARAM>(&event));};
                    if(down)for(DWORD vk:physicalKeys)key(vk);else for(auto it=physicalKeys.rbegin();it!=physicalKeys.rend();++it)key(*it);
                }
            };
            publish(true);
            check(oemLiveBackBits()==3&&g_oemKeyMask==observed&&g_oemBackStateKb==expectedBack,"keyboard physical/editor/default mapper are retained: "+label);
            check(fixtureVirtualRear()==0&&fixtureOwnActions.empty(),"keyboard first frame filters complete gesture before action: "+label);
            tick(0,fixtureTick+1);flushJobs();
            check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,"keyboard mapped action still executes without a virtual Host: "+label);
            fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
            fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
            check(fixtureVirtualRear()==0,"keyboard rule removal keeps only the held output fence: "+label);
            publish(false);
            // Real UP and new DOWN both precede BUS: the new press cannot inherit the old lease.
            publish(true);
            check(fixtureVirtualRear()==3,"keyboard UP between frames gives the fresh unbound press its output: "+label);
            visited=true;++rows;
        }
        if(visited)++profiles;
    }
    std::cout<<"VIRTUAL_KEYBOARD_SOURCE profiles="<<profiles<<" rows="<<rows<<"\n";
    resetRoute(json::array({producerRule("m1")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
    activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    producerSample(OemProducerId::Msi,kOemBitM1);
    check(fixtureVirtualRear()==0,"MSI accepted DInput source is reserved before atomics/UI publication");
    g_oemKeyMaskMsiDinput=kOemBitM1;g_msiDinputBackBits=2;
    g_oemKbDown=kVkF11;oemBackHookRecompute();
    check(fixtureVirtualRear()==0,"MSI authoritative DInput and observed keyboard mirror remain filtered together");
    nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.size()==1,"MSI DInput binding executes once with its mirrored keyboard source");
    closeProducer(OemProducerId::Msi);g_oemKeyMaskMsiDinput=0;g_msiDinputBackBits=0;
    check(fixtureVirtualRear()==0,"MSI source termination cannot revive an already-observed held keyboard mirror"+
        std::string(" final=")+std::to_string(fixtureVirtualRear())+
        " raw="+std::to_string(oemLiveBackBits())+
        " dinput="+std::to_string(g_oemKeyMaskMsiDinput.load())+
        " kb="+std::to_string(g_oemKeyMask.load())+
        " sample="+(g_oemVirtualKeyboardSample.load()?std::to_string(g_oemVirtualKeyboardSample.load()->mask):"none")+
        " policy="+(g_oemVirtualBindingPolicy.load()&&!g_oemVirtualBindingPolicy.load()->bindings.empty()?std::to_string(g_oemVirtualBindingPolicy.load()->bindings[0].requiredMask):"none")+
        " policyMsiGen="+(g_oemVirtualBindingPolicy.load()&&!g_oemVirtualBindingPolicy.load()->bindings.empty()?std::to_string(g_oemVirtualBindingPolicy.load()->bindings[0].generations[static_cast<size_t>(OemProducerId::Msi)]):"none")+
        " currentMsiGen="+std::to_string(g_oemProducerGenerations[static_cast<size_t>(OemProducerId::Msi)].load()));
}

static void nativeOemVirtualSourceLifeCases() {
#if defined(YMCC_VIRTUAL_POLICY_SOURCE)
    resetRoute(json::array({producerRule("l4")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::GpdWin5);
    tick(0,fixtureTick);g_oemRearMapToL5=g_oemRearMapToR5=kOemBitL4;
    g_oemKbDown=kVkF14;oemBackHookRecompute();check(fixtureVirtualRear()==0,"GPD original keyboard gesture establishes output lease");
    ++g_gpdBackIfaceGen;oemBackHookRecompute();
    check(fixtureVirtualRear()==3,"new GPD interface held snapshot cannot inherit old neutral or output ownership"+
        std::string(" final=")+std::to_string(fixtureVirtualRear())+
        " raw="+std::to_string(oemLiveBackBits())+
        " kb="+std::to_string(g_oemKeyMask.load())+
        " sample="+(g_oemVirtualKeyboardSample.load()?std::to_string(g_oemVirtualKeyboardSample.load()->mask):"none"));
    g_oemKbDown=0;oemBackHookRecompute();tick(0,fixtureTick+1);
    g_oemKbDown=kVkF14;oemBackHookRecompute();check(fixtureVirtualRear()==0,"GPD new interface can reserve a fresh neutral-armed press");

    resetRoute(json::array({producerRule("l4")}));
    g_onexX2PublishedReady=true;g_onexX2PublishedPower=currentPowerGeneration();
    g_onexX2PublishedTopology=g_onexX2DeviceEpoch.load();
    const auto gen=oemProducerBegin(OemProducerId::OneXX2Rear);
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,0,kOemBitL4);nativeOemProducerDrain();tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitL4;
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,kOemBitL4,kOemBitL4);
    check(fixtureVirtualRear()==0,"OneX event-protocol observed DOWN reserves its own source");
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,0,kOemBitR4);
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,kOemBitL4,kOemBitL4);
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    check(fixtureVirtualRear()==0,"OneX unrelated observed ID was not an UP of the reserved key");
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,0,kOemBitL4);
    oemProducerPublish(OemProducerId::OneXX2Rear,gen,kOemBitL4,kOemBitL4);
    check(fixtureVirtualRear()==3,"OneX actual observed UP releases only its own old output fence");
#else
    check(false,"source lifetime variant requires the immutable binding policy consumer");
#endif
}

static void nativeOemVirtualUiFirstCases() {
    const auto setup=[](json item=producerRule("legionr")) {
        resetRoute(json::array({item}));activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitLegionR;
    };
    const auto removeBinding=[] {
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
        fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    };
    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);
    nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"UI-first control executes the original Y1 action before any BUS frame");
    producerSample(OemProducerId::Legion,kOemBitLegionR|kOemBitLegionY2);
    check(fixtureVirtualRear()==0,"HC206-C01: extra OEM key cannot return the already-consumed Y1 before first BUS");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);nativeOemProducerDrain();flushJobs();removeBinding();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"UI action then binding removal retains original press ownership before first BUS");
    activateProducer(OemProducerId::Gpd);producerSample(OemProducerId::Gpd,0);producerSample(OemProducerId::Gpd,kOemBitL4);
    check(fixtureVirtualRear()==1,"UI-first consumption preserves an independent source mapped to the same slot");
    producerSample(OemProducerId::Gpd,0);producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();
    producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3,"actual Y1 UP permits its next unbound press");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);
    producerSample(OemProducerId::Legion,0);
    producerSample(OemProducerId::Legion,kOemBitLegionR|kOemBitLegionY2);
    nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"retained DOWN1 still executes its original short action after UP and unmatched DOWN2");
    check(fixtureVirtualRear()==3,"old DOWN1 action cannot reserve the new same-generation DOWN2");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR|kOemBitLegionY2);nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureVirtualRear()==3,"initial extra OEM key is incomplete and cannot gain consumption ownership");

    auto mixed=producerRule("legionr");mixed["inputs"].push_back({{"source","controller"},{"code","a"}});
    setup(mixed);producerSample(OemProducerId::Legion,kOemBitLegionR);nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureVirtualRear()==3,"UI cannot consume an initially incomplete controller/OEM gesture");
    setup(mixed);g_curW=XINPUT_GAMEPAD_A;g_curPad.wButtons=XINPUT_GAMEPAD_A;
    producerSample(OemProducerId::Legion,kOemBitLegionR);nativeOemProducerDrain();flushJobs();removeBinding();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"UI consumes a complete controller/OEM gesture before BUS without inheriting it after edit");
    g_curW=0;g_curPad={};
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    const auto keyboardSetup=[] {
        resetRoute(json::array({producerRule("m1")}));
        g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    };
    const auto drain=[] {deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();};
    const auto slots=[] {return std::count_if(g_oemVirtualKeyboardEdges.begin(),g_oemVirtualKeyboardEdges.end(),[](const auto& edge){return edge.sequence!=0;});};
    keyboardSetup();recordedMouseKeyboard(VK_F11);drain();
    check(fixtureOwnActions.size()==1,"keyboard UI action executes from the original posted receipt before first BUS");
    recordedMouseKeyboard(VK_F14);
    check(fixtureVirtualRear()==1,"adding keyboard M2 preserves consumed M1 identity and independent M2 output");
    drain();recordedMouseKeyboard(VK_F11,true);recordedMouseKeyboard(VK_F14,true);drain();
    check(slots()==0&&!oemKeyboardConsumeBusy(),"keyboard receipt and original DOWN/UP ownership fully drain");

    keyboardSetup();recordedMouseKeyboard(VK_F11);drain();removeBinding();
    check(fixtureVirtualRear()==0,"keyboard UI action then removal cannot leak the held original press before BUS");
    recordedMouseKeyboard(VK_F11,true);recordedMouseKeyboard(VK_F11);drain();
    check(fixtureVirtualRear()==3,"real keyboard UP admits the next unbound press");

    keyboardSetup();recordedMouseKeyboard(VK_F11);recordedMouseKeyboard(VK_F11,true);
    recordedMouseKeyboard(VK_F14);recordedMouseKeyboard(VK_F11);drain();
    check(fixtureOwnActions.size()==1,"retained keyboard DOWN1 action survives its actual UP and new unmatched DOWN2");
    check(fixtureVirtualRear()==3,"old posted keyboard identity never consumes current DOWN2");
    check(slots()==0,"each retained keyboard message takes exactly its own receipt once");

    keyboardSetup();const auto serialBefore=g_oemVirtualKeyboardEdgeSequence;
    check(oemShortcutPostPhysicalEdge(g_oemKeyMask.load(),g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())&&
        serialBefore==g_oemVirtualKeyboardEdgeSequence&&slots()==0,"no canonical change succeeds without creating a receipt or WM");
    fixturePostOk=false;recordedMouseKeyboard(VK_F11);fixturePostOk=true;
    check(slots()==0&&fixturePhysicalReceipts.empty()&&g_oemShortcutEdgePostFailures==1&&
        g_oemVirtualKeyboardCapacityRefusals==0,"actual PostMessage failure rolls back its sole identity and counts only OS post failure");
    recordedMouseKeyboard(VK_F11,true);drain();
    check(!oemKeyboardConsumeBusy(),"PostMessage-refused keyboard pair returns the original replay/visibility ledger");

    keyboardSetup();for(size_t i=0;i<g_oemVirtualKeyboardEdges.size();++i)recordedMouseKeyboard(VK_F14,(i&1)!=0);
    check(slots()==static_cast<int>(g_oemVirtualKeyboardEdges.size()),"original physical hook fills the bounded receipt slots");
    const auto posted=fixturePhysicalReceipts.size();recordedMouseKeyboard(VK_F11);
    check(fixturePhysicalReceipts.size()==posted&&g_oemVirtualKeyboardCapacityRefusals==1&&
        g_oemShortcutEdgePostFailures==0,"capacity refusal posts no identity hole and remains distinct from OS failure");
    tick(0,fixtureTick);flushJobs();
    check(fixtureOwnActions.size()==1,"original sampled evaluator still consumes a complete press after retained-message capacity refusal");
    recordedMouseKeyboard(VK_F14);
    check(fixtureVirtualRear()==1,"sampled UI ownership after full capacity cannot bypass HC206 before BUS");
    drain();recordedMouseKeyboard(VK_F11,true);recordedMouseKeyboard(VK_F14,true);drain();
    check(slots()==0&&!oemKeyboardConsumeBusy(),"full-capacity keyboard path drains all accepted receipts and paired physical ownership");

    keyboardSetup();recordedMouseKeyboard(VK_F11);const auto old=fixturePhysicalReceipts.back();
    ++g_oemShortcutEdgeEpoch;drain();
    check(slots()==0&&fixtureOwnActions.empty(),"epoch-cancelled message releases its exact receipt before early return");
    const auto newId=oemVirtualRetainKeyboardEdge(kOemBitM1,oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load()));
    nativeKeyboardShortcutHandleOemEdge(old.first,old.second);
    check(newId&&slots()==1,"late duplicate old message cannot take a newer same-mask receipt");
    oemVirtualWithdrawKeyboardEdge(newId);check(slots()==0,"private receipt withdrawal returns only its own retained storage");

    keyboardSetup();g_oemVirtualKeyboardEdgeSequence=(1ull<<63)-1;recordedMouseKeyboard(VK_F11);drain();
    check(fixtureOwnActions.size()==1&&slots()==0,"actual x64 LPARAM preserves the private identity across its signed boundary");
    g_oemVirtualKeyboardEdgeSequence=UINT64_MAX;recordedMouseKeyboard(VK_F11,true);
    check(slots()==0&&g_oemVirtualKeyboardCapacityRefusals==1,"saturated private serial never wraps or aliases an old message");

    resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
    g_oemKbDown=kVkF11;oemBackHookRecompute();const auto physicalId=g_oemVirtualKeyboardWriter.presses[20];
    check(g_oemVirtualKeyboardWriter.observed&&physicalId&&!g_oemVirtualKeyboardSampleWanted,
        "physical keyboard identity is observed with no virtual publication demand");
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({producerRule("m1")});
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();tick(0,fixtureTick+500);
    check(g_oemVirtualKeyboardWriter.presses[20]==physicalId,"enabling a binding cannot invent a new held physical press");
#endif
}

static void nativeOemVirtualUiFirstTailCases(unsigned firstSource=0) {
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    const auto drain=[] {deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();};
    const auto slots=[] {return std::count_if(g_oemVirtualKeyboardEdges.begin(),g_oemVirtualKeyboardEdges.end(),[](const auto& edge){return edge.sequence!=0;});};
    const auto removeBinding=[] {
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
        ++fixtureInput["revision"].get_ref<json::number_integer_t&>();tick(0,fixtureTick+500);
    };
    const struct{OemProducerId id;const char* code;uint64_t bit;} sources[]={
        {OemProducerId::Legion,"legionr",kOemBitLegionR},{OemProducerId::Legion,"legionm3",kOemBitLegionM3},
        {OemProducerId::LegionS,"legionl",kOemBitLegionL},{OemProducerId::LegionS,"legionr",kOemBitLegionR},
        {OemProducerId::Gpd,"l4",kOemBitL4},{OemProducerId::Gpd,"r4",kOemBitR4},
        {OemProducerId::OneX,"l4",kOemBitL4},{OemProducerId::OneX,"r4",kOemBitR4},
        {OemProducerId::OneX,"special",kOemBitSpecial},{OemProducerId::Msi,"m1",kOemBitM1},
        {OemProducerId::Msi,"m2",kOemBitM2}
    };
    unsigned sourceOrdinal=0;
    for(const auto& item:sources){
        if(sourceOrdinal++<firstSource)continue;
        const auto publish=[&](uint64_t mask){
            producerSample(item.id,mask);
            if(item.id==OemProducerId::Msi){
                // Memory inputs from the same source's physical display ports;
                // the original helper above covers only the action publisher.
                g_oemKeyMaskMsiDinput=mask;
                g_msiDinputBackBits=(mask&kOemBitM1?2:0)|(mask&kOemBitM2?1:0);
            }
        };
        resetRoute(json::array({producerRule(item.code)}));activateProducer(item.id);
        publish(0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=item.bit;
        publish(item.bit);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,std::string("UI-first source action before BUS: ")+oemProducerName(item.id)+"/"+item.code);
        removeBinding();check(fixtureVirtualRear()==0,std::string("UI-first original press stays consumed after edit: ")+item.code);
        publish(0);nativeOemProducerDrain();publish(item.bit);
        check(fixtureVirtualRear()==3,std::string("UI-first source actual UP returns its next unbound press: ")+item.code);
    }

    resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
    g_machineIdentity.family=YmccFamily::MsiClaw;fixtureWindowForeground=true;
    tick(0,fixtureTick);
    g_shortcutRecordingActive=true;recordedMouseKeyboard(VK_F11);drain();
    check(!fixtureRecordedShortcutChords.empty()&&fixtureRecordedShortcutChords.back()==kOemBitM1&&fixtureOwnActions.empty(),
        "recording receives its original keyboard chord without virtual sample demand or actions");
    recordedMouseKeyboard(VK_F11,true);drain();g_shortcutRecordingActive=false;
    check(slots()==0&&!oemKeyboardConsumeBusy(),"recording neutral/UP drains original transport and physical visibility");

    resetRoute(json::array({producerRule("m1")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
    g_realStickTest.virtualTargetEnabled=false;g_inputHostProcess=nullptr;g_inputHostPrepared=g_inputHostNeutralized=false;
    recordedMouseKeyboard(VK_F11);recordedMouseKeyboard(VK_F11,true);drain();
    check(fixtureOwnActions.size()==1&&!g_inputHostProcess&&!g_realStickTest.virtualTargetEnabled,
        "opaque keyboard receipt preserves native action with virtual target OFF");

    const auto mouseSetup=[] {
        resetRoute(json::array({producerRule("guide")}));g_machineIdentity.resolved=true;
        g_machineIdentity.family=YmccFamily::Ayn;g_machineIdentity.deviceClass="HandheldCompanion.Devices.AynLoki";
        oemBackMappingRefreshGate(false,"disabled",false);oemBackMappingReconcileOnUi();tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitGuide;
    };
    mouseSetup();recordedMouse(VK_LBUTTON);recordedMouse(VK_XBUTTON2);drain();
    check(fixtureOwnActions.size()==1&&slots()==0,"original mouse chord post and handler execute their one retained action");
    removeBinding();check(fixtureVirtualRear()==0,"mouse-origin consumed Guide cannot leak its mapper after edit before BUS");
    recordedMouse(VK_LBUTTON,true);recordedMouse(VK_XBUTTON2,true);drain();
    check(!oemKeyboardConsumeBusy()&&slots()==0&&g_oemKeyMask==0,"mouse pair UP and exact message storage both return");

    mouseSetup();std::vector<uint64_t> owned;
    const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
    for(size_t i=0;i<g_oemVirtualKeyboardEdges.size();++i)owned.push_back(oemVirtualRetainKeyboardEdge(0,token));
    recordedMouse(VK_LBUTTON);recordedMouse(VK_XBUTTON2);
    check(g_oemVirtualKeyboardCapacityRefusals==1&&g_oemShortcutEdgePostFailures==0&&fixturePhysicalReceipts.empty(),
        "mouse full-capacity path refuses only the unadmitted WM and reports its capacity cause");
    tick(0,fixtureTick);flushJobs();check(fixtureOwnActions.size()==1,
        "mouse complete live sampling still runs its original action after transport refusal");
    removeBinding();check(fixtureVirtualRear()==0,"mouse sampled ownership survives edit before its first BUS frame");
    recordedMouse(VK_LBUTTON,true);recordedMouse(VK_XBUTTON2,true);drain();
    for(const auto id:owned)oemVirtualWithdrawKeyboardEdge(id);
    check(!oemKeyboardConsumeBusy()&&slots()==0&&g_oemKeyMask==0,
        "mouse capacity refusal replays/releases its original pair and all fixture-owned slots return");

    mouseSetup();fixturePostOk=false;recordedMouse(VK_LBUTTON);recordedMouse(VK_XBUTTON2);fixturePostOk=true;
    check(slots()==0&&g_oemShortcutEdgePostFailures==1&&g_oemVirtualKeyboardCapacityRefusals==0,
        "mouse actual PostMessage failure rolls back only its own receipt");
    recordedMouse(VK_LBUTTON,true);recordedMouse(VK_XBUTTON2,true);drain();
    check(!oemKeyboardConsumeBusy()&&slots()==0,"mouse OS-post failure keeps original paired release responsibility");

    resetRoute(json::array({producerRule("m1")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    g_oemVirtualKeyboardPressSequence=UINT64_MAX-1;
    recordedMouseKeyboard(VK_F11);drain();recordedMouseKeyboard(VK_F14);
    check(g_oemVirtualKeyboardWriter.presses[20]==UINT64_MAX&&g_oemVirtualKeyboardWriter.presses[21]==0&&
        g_oemVirtualKeyboardPressSequence==UINT64_MAX,"keyboard press identity saturates without wrapping and preserves the already-held ID");
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==1,"saturated new keyboard identity cannot return the original consumed held press");
    removeBinding();recordedMouseKeyboard(VK_F11,true);recordedMouseKeyboard(VK_F11);drain();
    check(g_oemVirtualKeyboardWriter.presses[20]==0&&fixtureVirtualRear()==3,
        "unknown saturated new press never inherits the old held output responsibility");
#else
    check(false,"UI-first tail requires original-press output ownership");
#endif
}

static void nativeOemVirtualUiFirstCrossCases() {
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    const auto setup=[] {
        auto composite=producerRule("m1");composite["inputs"].push_back({{"source","oem"},{"code","m2"}});
        resetRoute(json::array({composite}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
        fixtureMsiAuthority=static_cast<int>(kOemBitM1);activateProducer(OemProducerId::Msi);
        producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();tick(0,fixtureTick);
        check(g_nativeKeyboardShortcuts.size()==1&&!g_nativeKeyboardShortcuts.front().needsNeutral,
            "cross-source composite passes the original parser and source neutral qualification");
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    };
    const auto managed=[](uint64_t mask){producerSample(OemProducerId::Msi,mask);g_oemKeyMaskMsiDinput=mask;g_msiDinputBackBits=mask?2:0;};
    const auto drain=[] {deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();flushJobs();};
    setup();recordedMouseKeyboard(VK_F14);recordedMouseKeyboard(VK_F14,true);managed(kOemBitM1);drain();
    check(fixtureOwnActions.size()==1,"cross-time control preserves the original retained keyboard action semantics"+
        (fixtureOwnActions.size()==1?std::string():" actions="+std::to_string(fixtureOwnActions.size())+" logs="+json(fixtureLogs).dump()));
    check(fixtureVirtualRear()==3,"HC210: old keyboard M2 cannot borrow later managed M1 to create output ownership");

    setup();managed(kOemBitM1);recordedMouseKeyboard(VK_F14);recordedMouseKeyboard(VK_F14,true);drain();
    check(fixtureOwnActions.size()==1,"actually overlapping managed/keyboard gesture still executes its original action");
    check(fixtureVirtualRear()==0,"actual post-time complete gesture owns the same still-held managed press after keyboard UP");

    setup();managed(kOemBitM1);recordedMouseKeyboard(VK_F14);recordedMouseKeyboard(VK_F14,true);
    managed(0);managed(kOemBitM1);drain();
    check(fixtureOwnActions.size()==1,"old complete keyboard action is retained across managed UP and DOWN2");
    check(fixtureVirtualRear()==3,"old keyboard post-time witness cannot occupy managed same-generation DOWN2");
#else
    check(false,"cross-source UI identity requires original-press output ownership");
#endif
}

static void fixtureMixedController(WORD buttons,bool publish=true) {
    g_curW=buttons;g_curPad={};g_curPad.wButtons=buttons;
#if defined(YMCC_VIRTUAL_MIXED_SOURCE)
    if(publish){
        OemVirtualControllerSample sample;
        sample.source.window={fixtureTick,fixtureTick,fixtureTick,currentPowerGeneration(),g_xinputTopologyEpoch.load(),true};
        sample.source.identity=g_xinputSlotIdentityEpoch.load();sample.source.live=sample.source.routeKnown=true;
#if defined(YMCC_VIRTUAL_MSI_REVOCATION_SOURCE)
        sample.msiRevocation=g_oemVirtualMsiRevocation.load();
#endif
        captureSharedPowerLifecycle(sample.power);sample.pad=g_curPad;sample.continuity=1;sample.observed=true;sample.sourceKind=1;
        oemVirtualPublishController(sample);
    }
#endif
}
static void nativeOemVirtualUiFirstMixedCases(int part=0) {
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    const bool desktop=part==1||part==4||part==9;
    const bool click=part==2||part==5;
    const auto setup=[&](bool publish=true) {
        auto composite=producerRule("m1");
        composite["inputs"].push_back({{"source","oem"},{"code",click?"qs":"m2"}});
        composite["inputs"].push_back({{"source",desktop?"keyboard":"controller"},{"code",desktop?"KeyK":"a"}});
        resetRoute(json::array({composite}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
#if defined(YMCC_VIRTUAL_MIXED_SOURCE)
        g_oemVirtualControllerSample={};g_oemVirtualDesktopSample={};
        for(auto& value:g_keyboardSequenceInputFence)value=0;
        g_desktopKeyboardChordCancellation=g_desktopMouseChordCancellation=0;
        g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;g_msiMainSourceRevision=g_msiFeedbackModeGeneration=1;
        g_msiDinputSourceValid=g_msiDinputRouteConfirmed=g_msiDinputRouteDinputArmed=true;
        g_msiDinput.enabled=g_msiDinput.mainInterfaceMatched=g_msiDinput.canonicalValid=true;
        g_msiDinput.productId=kMsiClawCapability.pidDInput;g_msiDinput.canonicalSeq=0;
        fixtureMsiKnownBoard=true;fixtureVirtualCaptureInterleave=fixtureVirtualCommitInterleave={};
#endif
        g_keyboardModifierHookInstalled=true;g_inputSignalMouseHookInstalled=true;
        fixtureMsiAuthority=static_cast<int>(kOemBitM1);activateProducer(OemProducerId::Msi);
        if(click)activateWmi();
        producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();tick(0,fixtureTick);
        check(g_nativeKeyboardShortcuts.size()==1&&!g_nativeKeyboardShortcuts.front().needsNeutral,
            "mixed composite passes the original parser and neutral qualification");
        fixtureMixedController(0,publish);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
    };
    const auto managed=[](uint64_t mask){producerSample(OemProducerId::Msi,mask);g_oemKeyMaskMsiDinput=mask;g_msiDinputBackBits=mask?2:0;};
    const auto condition=[&](bool down,bool publish=true){
        if(desktop){recordedMouseKeyboard('K',!down);if(down)fixtureDesktopDown.insert('K');else fixtureDesktopDown.erase('K');}
        else fixtureMixedController(down?XINPUT_GAMEPAD_A:0,publish);
    };
    const auto edge=[&]{if(click)msiWmiHandleEvent(kMsiWmiEventQs,g_msiWmiActiveGeneration.load());
        else{recordedMouseKeyboard(VK_F14);recordedMouseKeyboard(VK_F14,true);}};
    const auto drain=[&]{if(click)nativeMsiWmiShortcutDrain();else{deliverPhysicalReceipts();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();}flushJobs();};
#if defined(YMCC_VIRTUAL_MIXED_SOURCE)
    if(part==5){
        for(const bool keyboard:{false,true}){
            setup();
            if(keyboard){auto saved=fixtureInput["buttonMapping"]["rules"]["shortcutRules"][0];
                saved["inputs"][2]={{"source","keyboard"},{"code","KeyK"}};
                fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({saved});
                ++fixtureInput["revision"].get_ref<json::number_integer_t&>();fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;tick(0,fixtureTick);}
            managed(kOemBitM1);ULONGLONG publishedAt=0;
            fixtureVirtualCaptureInterleave=[&]{fixtureTick+=5;
                if(keyboard){recordedMouseKeyboard('K');fixtureDesktopDown.insert('K');}else fixtureMixedController(XINPUT_GAMEPAD_A);
                publishedAt=fixtureTick;};
            edge();check(g_msiWmiClickCount==1,"original WMI admission retains one receipt across a pre-lock publication");
            const auto& witness=g_msiWmiClicks[g_msiWmiClickHead].virtualWitness.virtualObservation;
            check(witness.capturedAt>=publishedAt,"virtual cutoff is the admitted publication copy, never the earlier callback timestamp");
            drain();check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"both condition domains qualify the same actually admitted publication cutoff");
        }
        setup();auto saved=fixtureInput["buttonMapping"]["rules"]["shortcutRules"][0];
        saved["inputs"][2]={{"source","oem"},{"code","m2"}};
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({saved});
        ++fixtureInput["revision"].get_ref<json::number_integer_t&>();fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;tick(0,fixtureTick);
        g_oemRearMapToL5=kOemBitM1;g_oemRearMapToR5=kOemBitM2;
        managed(kOemBitM1);recordedMouseKeyboard(VK_F14);
        fixtureVirtualCaptureInterleave=[]{recordedMouseKeyboard(VK_F14,true);recordedMouseKeyboard(VK_F14);};
        edge();const auto& cohort=g_msiWmiClicks[g_msiWmiClickHead].virtualWitness.virtualObservation;
        check(cohort.keyboardKnown&&cohort.keyboard.presses==g_oemVirtualKeyboardWriter.presses,
            "keyboard tuple belongs to the same publication cohort as managed/controller state after lock admission");
        drain();check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"late keyboard DOWN2 is qualified as itself and both actual contributions stay consumed");
        setup();g_onexX2PublishedReady=true;g_onexX2PublishedPower=currentPowerGeneration();g_onexX2PublishedTopology=g_onexX2DeviceEpoch.load();
        const auto home=activateProducer(OemProducerId::OneXX2Home);const auto originalAt=fixtureTick-50,copyAt=fixtureTick;
        check(oemProducerPublishOnexX2Home(home,originalAt,0x24),"original OneX Home terminal publisher admits its existing event");
        OemProducerEdgeReceipt homeReceipt;bool found=false;
        while(oemProducerTryPop(homeReceipt))if(homeReceipt.source==OemProducerId::OneXX2Home){found=true;break;}
        check(found&&homeReceipt.capturedAt==originalAt&&homeReceipt.tokens.virtualObservation.capturedAt>=copyAt,
            "OneX Home preserves original action time and separately stamps its actual virtual publication cutoff");
        return;
    }
    if(part==6){
        for(const bool identity:{false,true}){
            setup();condition(true);managed(kOemBitM1);edge();
            fixtureVirtualCommitInterleave=[&]{if(identity)++g_xinputSlotIdentityEpoch;else ++g_xinputTopologyEpoch;};
            drain();check(fixtureOwnActions.size()==1,"commit-time source retirement preserves the original queued action");
            check(fixtureVirtualRear()==3,"source retirement before producer-lock admission cannot be certified by two matching old PODs");
        }
        return;
    }
    if(part==7||part==8){
        const auto ownsKeyboard=[] {return std::any_of(g_oemVirtualKeyboardLeases.begin(),g_oemVirtualKeyboardLeases.end(),[](auto value){return value!=0;});};
        setup();fixtureMsiAuthority=0;
        auto saved=producerRule("m2");saved["inputs"].push_back({{"source","controller"},{"code","a"}});
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({saved});
        ++fixtureInput["revision"].get_ref<json::number_integer_t&>();fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;tick(0,fixtureTick);
        g_oemRearMapToL5=0;g_oemRearMapToR5=kOemBitM2;
        fixtureMixedController(XINPUT_GAMEPAD_A);auto stale=g_oemVirtualControllerSample;
        stale.source.msi=true;stale.source.msiRevision=stale.source.msiMode=1;oemVirtualPublishController(stale);
        recordedMouseKeyboard(VK_F14);
#if defined(YMCC_VIRTUAL_MSI_REVOCATION_SOURCE)
        if(part==8){
            const auto unconsumed=fixtureVirtualRear();drain();
            check(unconsumed!=0&&fixtureOwnActions.size()==1&&ownsKeyboard()&&fixtureVirtualRear()==0,"MSI revocation tail starts from a real qualified original press");
            g_oemVirtualMsiRevocation=UINT64_MAX;msiDinputInvalidateCanonicalFrame();
            check(ownsKeyboard()&&fixtureVirtualRear()==0,"MSI unknown controller publication does not clear already-owned OEM responsibility");
            fixtureTick+=201;recordedMouseKeyboard(VK_F14,true);drain();fixtureMixedController(XINPUT_GAMEPAD_A);
            auto exhausted=g_oemVirtualControllerSample;exhausted.source.msi=true;exhausted.source.msiRevision=exhausted.source.msiMode=1;oemVirtualPublishController(exhausted);
            recordedMouseKeyboard(VK_F14);drain();
            check(g_oemVirtualMsiRevocation==UINT64_MAX&&!ownsKeyboard()&&fixtureVirtualRear()==unconsumed,
                "MSI revocation saturation never aliases a new press and actual OEM UP still returns its output");
            return;
        }
#endif
        g_msiDinputSourceValid=g_msiDinputRouteConfirmed=false;msiDinputInvalidateCanonicalFrame();
        const auto unconsumed=fixtureVirtualRear();
        check(!ownsKeyboard()&&unconsumed!=0,"original same-source unconsumed rear projection baseline="+std::to_string(unconsumed));
        drain();check(fixtureOwnActions.size()==1,"independent keyboard OEM receipt keeps its original action after MSI controller loss");
        check(!ownsKeyboard()&&fixtureVirtualRear()==unconsumed,"MSI local canonical loss revokes controller qualification without pretending keyboard OEM UP");
        g_msiDinputSourceValid=g_msiDinputRouteConfirmed=g_msiDinput.canonicalValid=true;
        oemVirtualPublishController(stale);
        fixtureTick+=201;recordedMouseKeyboard(VK_F14,true);drain();recordedMouseKeyboard(VK_F14);drain();
        check(fixtureOwnActions.size()==2&&!ownsKeyboard()&&fixtureVirtualRear()==unconsumed,"late pre-revocation controller publication cannot revive a new independent OEM press");
        fixtureTick+=201;recordedMouseKeyboard(VK_F14,true);drain();fixtureMixedController(XINPUT_GAMEPAD_A);
        auto fresh=g_oemVirtualControllerSample;fresh.source.msi=true;fresh.source.msiRevision=fresh.source.msiMode=1;oemVirtualPublishController(fresh);
        recordedMouseKeyboard(VK_F14);drain();
        check(fixtureOwnActions.size()==3&&ownsKeyboard()&&fixtureVirtualRear()==0,"new current MSI publication restores qualification without changing original action matching");
        return;
    }
    if(part==3){
        for(unsigned fault=0;fault<11;++fault){
            setup();condition(true);managed(kOemBitM1);
            auto sample=g_oemVirtualControllerSample;
            if(fault==0)sample.source.routeKnown=false;
            if(fault==1)sample.source.live=false;
            if(fault==2)sample.source.window.readStartedTick=fixtureTick-ymcc::kVirtualOutputSourceWindowMaxAgeMs-1;
            if(fault==3)sample.source.window.readCompletedTick=fixtureTick+100;
            if(fault==4)sample.power.flags=0;
            if(fault==7||fault==8){sample.source.msi=true;sample.source.msiRevision=sample.source.msiMode=1;}
            oemVirtualPublishController(sample);edge();
            if(fault==5)++g_xinputSlotIdentityEpoch;
            if(fault==6)++g_xinputTopologyEpoch;
            if(fault==7)++g_msiMainSourceRevision;
            if(fault==8)++g_msiFeedbackModeGeneration;
            if(fault==9){++sample.continuity;oemVirtualPublishController(sample);}
            if(fault==10){++sample.sourceKind;oemVirtualPublishController(sample);}
            drain();
            check(fixtureOwnActions.size()==1,"controller qualifier fault preserves original action "+std::to_string(fault));
            check(fixtureVirtualRear()==3,"controller qualifier never treats unknown/stale/replaced source as historical overlap "+std::to_string(fault));
        }
        return;
    }
    if(part==4||part==9){
        for(unsigned fault=0;part==4&&fault<5;++fault){
            setup();
            if(fault==4)g_desktopKeyboardChordCancellation=UINT64_MAX;
            condition(true);managed(kOemBitM1);edge();
            if(fault==0)++g_desktopKeyboardChordCancellation;
            if(fault==1){g_oemBackHook=nullptr;g_keyboardModifierHookInstalled=false;fixtureHookInstallOk=false;}
            if(fault==2)++fixtureNotificationSequence;
            if(fault==3){condition(false);condition(true);}
            drain();
            check(fixtureOwnActions.size()==1,"desktop qualifier keeps original accepted action "+std::to_string(fault));
            check(fixtureVirtualRear()==(fault==3?0:3),"desktop cancellation/power/availability qualified at original cutoff "+std::to_string(fault));
        }
        for(const bool mouse:{false,true}){
            if(part==9&&!mouse)continue;
            setup();auto saved=fixtureInput["buttonMapping"]["rules"]["shortcutRules"][0];
            saved["inputs"][2]["code"]=mouse?"MouseMiddle":"Control";
            saved["inputs"][2]["source"]=mouse?"mouse":"keyboard";
            fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array({saved});
            ++fixtureInput["revision"].get_ref<json::number_integer_t&>();fixtureTick+=NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1;tick(0,fixtureTick);
            if(mouse)check(g_nativeKeyboardShortcuts.size()==1&&g_nativeKeyboardShortcuts.front().desktopKeys==std::vector<WORD>{VK_MBUTTON},
                "mixed mouse definition retains its actual typed source through the original parser");
            if(mouse){recordedMouse(VK_MBUTTON);fixtureDesktopDown.insert(VK_MBUTTON);}
            else{recordedMouseKeyboard(VK_LCONTROL);recordedMouseKeyboard(VK_RCONTROL);recordedMouseKeyboard(VK_LCONTROL,true);fixtureDesktopDown.insert(VK_CONTROL);}
            managed(kOemBitM1);edge();drain();
            check(fixtureOwnActions.size()==1,"mixed actual mouse/modifier alias retains original action "+std::to_string(mouse));
            check(fixtureVirtualRear()==0,"mouse and remaining modifier side qualify without borrowing current UI state "+std::to_string(mouse));
        }
        return;
    }
#endif
    setup();managed(kOemBitM1);edge();condition(true);drain();
    check(fixtureOwnActions.size()==1,"mixed late condition preserves the original retained action acceptance");
    check(fixtureVirtualRear()==3,"old OEM receipt cannot borrow a controller or desktop condition first pressed after its cutoff");

    setup();condition(true);managed(kOemBitM1);edge();drain();
    check(fixtureOwnActions.size()==1,"actually overlapping mixed gesture retains original action acceptance");
    check(fixtureVirtualRear()==0,"actually overlapping mixed gesture consumes the same managed OEM press before BUS");
    condition(false);fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    ++fixtureInput["revision"].get_ref<json::number_integer_t&>();tick(0,fixtureTick+NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
    check(fixtureVirtualRear()==0,"later condition UP and unbind do not undo already owned original OEM press");
    managed(0);managed(kOemBitM1);
    check(fixtureVirtualRear()==3,"managed actual UP then DOWN2 never inherits mixed gesture ownership");
    if(desktop){
#if defined(YMCC_VIRTUAL_MIXED_SOURCE)
        setup();condition(true);g_keyboardSequenceInputFence['K']=1;managed(kOemBitM1);edge();g_keyboardSequenceInputFence['K']=0;drain();
        check(fixtureOwnActions.size()==1,"sequence-owned cutoff preserves later original action acceptance");
        check(fixtureVirtualRear()==3,"cutoff sequence-owned desktop input cannot qualify OEM output ownership");
#endif
    }else{
        setup(false);managed(kOemBitM1);edge();condition(true,false);drain();
        check(fixtureOwnActions.size()==1,"unknown physical source does not cancel the original accepted mixed action");
        check(fixtureVirtualRear()==3,"current UI controller cache cannot manufacture a missing historical physical observation");
    }
#else
    check(false,"mixed historical qualification requires original-press output ownership");
#endif
}

static void nativeOemVirtualUiFirstClickCases(bool extraOnly=false) {
#if defined(YMCC_VIRTUAL_UI_PRESS_SOURCE)
    const auto msiSetup=[](const char* trigger="press") {
        auto item=producerRule("qs",trigger);item["inputs"].push_back({{"source","oem"},{"code","m1"}});
        resetRoute(json::array({item}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
        fixtureMsiAuthority=static_cast<int>(kOemBitM1);activateProducer(OemProducerId::Msi);
        const auto wmi=activateWmi();producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;
        producerSample(OemProducerId::Msi,kOemBitM1);g_oemKeyMaskMsiDinput=kOemBitM1;g_msiDinputBackBits=2;
        nativeOemProducerDrain();return wmi;
    };
    if(!extraOnly){
    auto wmi=msiSetup();msiWmiHandleEvent(kMsiWmiEventQs,wmi);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"MSI original WMI click plus real DInput rear executes its configured action");
    check(fixtureVirtualRear()==0,"HC210-C02: WMI terminal click consumes only its captured companion rear before first BUS");

    wmi=msiSetup("double");msiWmiHandleEvent(kMsiWmiEventQs,wmi);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureVirtualRear()==0,"double first click owns its companion rear before any action emits");
    ++fixtureTick;msiWmiHandleEvent(kMsiWmiEventQs,wmi);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"second WMI click preserves original double semantics and output ownership");

    wmi=msiSetup();fixtureJobAccept=false;msiWmiHandleEvent(kMsiWmiEventQs,wmi);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureVirtualRear()==0,"WMI queue refusal cannot return the companion held output");

    wmi=msiSetup();msiWmiHandleEvent(kMsiWmiEventQs,wmi);
    producerSample(OemProducerId::Msi,0);producerSample(OemProducerId::Msi,kOemBitM1);
    nativeOemProducerDrain();nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==3,"old WMI click keeps its action without consuming new same-generation DInput DOWN2");

    auto rog=producerRule("lib");rog["inputs"].push_back({{"source","oem"},{"code","m1"}});
    resetRoute(json::array({rog}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Rog);
    activateProducer(OemProducerId::RogClick);tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitM1;g_oemRawPathActive=true;g_oemKbDownRaw=kVkF18;oemBackHookRecompute();
    const BYTE report[]={0x5A,147};rogOemKeyParse(report,sizeof(report),g_oemProducerGenerations[static_cast<size_t>(OemProducerId::RogClick)].load());
    nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"ROG terminal Library click consumes its actual held keyboard companion without inventing a held Library key");
    check(g_oemProducerStates[static_cast<size_t>(OemProducerId::RogClick)].physicalMask==0,
        "ROG click-only source remains terminal with no forged held physical state");

    auto home=producerRule("home");home["inputs"].push_back({{"source","oem"},{"code","l4"}});
    resetRoute(json::array({home}));g_onexX2PublishedReady=true;g_onexX2PublishedPower=currentPowerGeneration();
    g_onexX2PublishedTopology=g_onexX2DeviceEpoch.load();
    const auto rear=oemProducerBegin(OemProducerId::OneXX2Rear),front=oemProducerBegin(OemProducerId::OneXX2Home);
    oemProducerPublish(OemProducerId::OneXX2Rear,rear,0,kOemBitL4);nativeOemProducerDrain();tick(0,fixtureTick);
    g_oemRearMapToL5=g_oemRearMapToR5=kOemBitL4;
    oemProducerPublish(OemProducerId::OneXX2Rear,rear,kOemBitL4,kOemBitL4);nativeOemProducerDrain();
    // Vendor-ID validation is upstream; this test begins at the actual admitted Home-click producer.
    oemProducerPublishOnexX2Home(front,fixtureTick,0);nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"OneX admitted Home click consumes its captured same-family rear contribution before BUS");
    check(g_oemProducerStates[static_cast<size_t>(OemProducerId::OneXX2Home)].physicalMask==0,
        "OneX Home click remains terminal without a synthetic DOWN/UP pair");
    }
    const auto extraWmi=msiSetup();fixtureMsiAuthority=static_cast<int>(kOemBitM1|kOemBitM2);
    producerSample(OemProducerId::Msi,kOemBitM1|kOemBitM2);g_oemKeyMaskMsiDinput=kOemBitM1|kOemBitM2;g_msiDinputBackBits=3;
    nativeOemProducerDrain();msiWmiHandleEvent(kMsiWmiEventQs,extraWmi);nativeMsiWmiShortcutDrain();flushJobs();
    check(fixtureOwnActions.size()==1,"original terminal-click matcher accepts its required companion while another OEM key is held");
    check(fixtureVirtualRear()==1,"terminal click consumes only the required M1 and preserves the unrelated M2 contribution");
#else
    check(false,"click companion output ownership requires original-press consumer");
#endif
}

static void nativeOemVirtualFirstFrameCases() {
    const auto setup=[](json item=producerRule("legionr")) {
        resetRoute(json::array({item}));activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=kOemBitLegionR;g_oemRearMapToR5=kOemBitLegionR;
    };
    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureOwnActions.empty(),"first-frame control: no UI receipt or action was drained");
    check((oemBackMergedKeyMask()&kOemBitLegionR)!=0,"first-frame physical/editor canonical remains held");
    check(fixtureVirtualRear()==0,"first BUS frame reserves bound rear and both mapper slots before UI drain");
    nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.size()==1&&fixtureVirtualRear()==0,"later action executes once without restoring original output");

    setup();fixtureJobAccept=false;producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==0,"queue-refusal control reserves original gesture before dispatch");
    nativeOemProducerDrain();flushJobs();
    check(fixtureOwnActions.empty()&&fixtureVirtualRear()==0,"serial refusal reports no action and cannot revive held virtual DOWN");
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    check(fixtureVirtualRear()==0,"removing failed binding retains drain-only held output fence");
    producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();
    producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3,"real source UP permits the next unbound gesture after refusal");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR);check(fixtureVirtualRear()==0,"same-slot control establishes original reservation");
    activateProducer(OemProducerId::Gpd);producerSample(OemProducerId::Gpd,0);producerSample(OemProducerId::Gpd,kOemBitL4);
    check(fixtureVirtualRear()==1,"independent unconsumed GPD source survives in the same rear slot");
    producerSample(OemProducerId::Gpd,0);check(fixtureVirtualRear()==0,"independent source UP does not return the still-held consumed source");

    auto mixed=producerRule("legionr");mixed["inputs"].push_back({{"source","controller"},{"code","a"}});
    setup(mixed);producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3,"incomplete controller/OEM gesture preserves original rear plus mapper");
    XINPUT_GAMEPAD pad{};pad.wButtons=XINPUT_GAMEPAD_B;
    check(fixtureVirtualRear("steamdeck",false,0,true,pad)==3,"wrong controller input cannot reserve the OEM subset");
    pad.wButtons=XINPUT_GAMEPAD_A;
    check(fixtureVirtualRear("steamdeck",false,0,true,pad)==0,"complete controller/OEM gesture reserves at the actual final frame");

    setup();producerSample(OemProducerId::Legion,kOemBitLegionR|kOemBitLegionM3);
    check(fixtureVirtualRear()==3,"extra OEM canonical key preserves exact-match semantics");

#if defined(YMCC_VIRTUAL_POLICY_SOURCE)
    setup();const auto oldPolicy=g_oemVirtualBindingPolicy.load();
    fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
    fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
    producerSample(OemProducerId::Legion,kOemBitLegionR);
    {std::lock_guard<std::mutex> lock(g_oemProducerMx);nativeOemVirtualReserveFrameLocked(nullptr,oldPolicy,g_oemVirtualKeyboardSample.load());}
    check(fixtureVirtualRear()==3,"old immutable policy cannot authorize a new-epoch gesture");
    setup();closeProducer(OemProducerId::Legion);activateProducer(OemProducerId::Legion);
    producerSample(OemProducerId::Legion,kOemBitLegionR);
    check(fixtureVirtualRear()==3&&fixtureOwnActions.empty(),"new snapshot source starts held and cannot inherit old neutral qualification");
#endif
}

static void nativeOemVirtualRearConsumptionCases() {
    const struct { OemProducerId source; const char* code; uint64_t bit; int rear; } sources[]={
        {OemProducerId::Legion,"legionr",kOemBitLegionR,1},
        {OemProducerId::Legion,"legionm3",kOemBitLegionM3,2},
        {OemProducerId::LegionS,"legionl",kOemBitLegionL,1},
        {OemProducerId::LegionS,"legionr",kOemBitLegionR,2},
        {OemProducerId::Gpd,"l4",kOemBitL4,1},
        {OemProducerId::Gpd,"r4",kOemBitR4,2},
        {OemProducerId::OneX,"l4",kOemBitL4,1},
        {OemProducerId::OneX,"r4",kOemBitR4,2},
        {OemProducerId::OneX,"special",kOemBitSpecial,0}
    };
    for(const auto& item:sources) {
        const std::string label=std::string(oemProducerName(item.source))+"/"+item.code;
        resetRoute(json::array());auto generation=activateProducer(item.source);
        producerSample(item.source,0);nativeOemProducerDrain();tick(0,fixtureTick);
        producerSample(item.source,item.bit);nativeOemProducerDrain();
        check(fixtureVirtualRear()==item.rear,"unbound source retains its final rear: "+label+
            " final="+std::to_string(fixtureVirtualRear())+" raw="+std::to_string(oemLiveBackBits())+
            " canonical="+std::to_string(oemBackMergedKeyMask())+" generation="+
            std::to_string(g_oemProducerGenerations[static_cast<size_t>(item.source)].load()));
        g_oemRearMapToL5=item.bit;
        check((fixtureVirtualRear()&1)!=0,"unbound explicit mapper retains canonical source: "+label);

        resetRoute(json::array({producerRule(item.code)}));generation=activateProducer(item.source);
        producerSample(item.source,0);nativeOemProducerDrain();tick(0,fixtureTick);
        g_oemRearMapToL5=item.bit;g_oemRearMapToR5=item.bit;
        producerSample(item.source,item.bit);
        check(fixtureVirtualRear()==0,"first BUS frame cannot precede bound gesture consumption: "+label);
        nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"bound source still executes requested action: "+label);
        check((oemBackMergedKeyMask()&item.bit)!=0,"binding preserves physical/editor canonical source: "+label);
        check(fixtureVirtualRear()==0,"bound source and explicit mapper are excluded from final rear: "+label);
        check(fixtureVirtualRear("steamdeck",true)==0&&fixtureVirtualRear("xbox")==0&&fixtureVirtualRear("steamdeck",false,0,false)==0,
            "original neutral/persona/backEnabled output gates remain: "+label);
        fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=json::array();
        fixtureInput["revision"]=fixtureInput.value("revision",0ll)+1;tick(0,fixtureTick+500);
        check(fixtureVirtualRear()==0,"rule removal cannot reintroduce a consumed held source: "+label);
        producerSample(item.source,0);nativeOemProducerDrain();
        producerSample(item.source,item.bit);nativeOemProducerDrain();
        check(fixtureVirtualRear()==3,"real UP permits fresh unbound mapper output: "+label);
        closeProducer(item.source);
        check(fixtureVirtualRear()==0,"source close withdraws final rear and mapper: "+label);
        (void)generation;
    }
}
#endif

int main() {
    try {
#if defined(YMCC_SHORTCUT_FRONTEND_CASE_MODE)
        shortcutSummonFrontendCases();
        std::cout << "frontend buttons: " << passed << " cases passed\n";
        return 0;
#endif
#if defined(YMCC_SHORTCUT_ONEX_CASE_MODE)
        oneXConsumptionCases();
#if YMCC_SHORTCUT_ONEX_CASE_MODE == 1
        std::cout << "onex consumption: " << passed << " cases passed\n";return 0;
#endif
#endif
#if defined(YMCC_SHORTCUT_SHIELD_CASE_MODE)
        // Source mutations arrive through the original YMCC_SHORTCUT_SOURCE input.
        shortcutSummonActionShieldCases(YMCC_SHORTCUT_SHIELD_CASE_MODE);
        std::cout << "summon action shield: " << passed << " cases passed\n";
        return 0;
#endif
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        startF7RetirementCases();
#endif
        // Run the historical frontend suite after the shortcut consumer regressions.
#if defined(YMCC_ROG_CLICK_SOURCE)
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        check(!oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,true,"press"),"ROG LIB binding preserves official frontend services");
        check(!oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,true,"hold"),"R13 unsupported click hold does not silence default software");
        check(!oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib|kOemBitAc,true,"press") && !oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,false,"press"),"R13 unexecutable composite does not request takeover");
        check(oemVendorStackFrontMask(YmccFamily::AsusRogAlly)==(kOemBitAc|kOemBitCc|kOemBitLib),"R13 ROG vendor lease targets only front OEM actions");
        check((oemVendorStackFrontMask(YmccFamily::AsusRogAlly)&(kOemBitM1|kOemBitM2))==0,"R13 rear-only ROG rules do not stop ASUS services");
        check(oemVendorStackFrontMask(YmccFamily::Gpd)==0 && oemVendorStackFrontMask(YmccFamily::Ayanero)==0 && oemVendorStackFrontMask(YmccFamily::OneXPlayer)==0,"R13 no generic-family service takeover inferred");
        check(oemVendorServiceNamesForFamily(YmccFamily::AsusRogAlly).size()==3,"R13 HC ROG service table has three named services");
        check(oemVendorServiceNamesForFamily(YmccFamily::LenovoLegionGo).size()==1,"R13 HC Legion service table is bound");
        check(oemVendorServiceNamesForFamily(YmccFamily::MsiClaw).empty() &&
              oemVendorTaskNamesForFamily(YmccFamily::MsiClaw).size()==2 &&
              oemVendorProcessNamesForFamily(YmccFamily::MsiClaw).size()==4,
              "R13 current MSI targets keep the protected service and use explicit task/process tables");
        check(oemVendorServiceNamesForFamily(YmccFamily::ZotacGamingZone).size()==2,"R13 HC Zotac service table is bound");
        check(!oemVendorStackFamilySupported(YmccFamily::Gpd) && !oemVendorStackFamilySupported(YmccFamily::Ayanero) && !oemVendorStackFamilySupported(YmccFamily::OneXPlayer),"R13 no unsupported GPD/AYA/OneX service names invented");
#endif
        // Replay the exported settings without changing origin/defaultKey or trigger.
        // Missing an external export still exercises its exact edited default row.
        // This case is ROG/LIB-specific: do not substitute an MSI field export
        // and then assert it contains a LIB rule. Replay MSI exports below.
        const json savedInput = baseInput();
        const json savedRules = json::array({json::parse(R"({"actionId":"window.summon","defaultKey":"enabled","enabled":true,"id":"native-enabled","inputs":[{"code":"lib","source":"oem"}],"origin":"native-default","params":{},"trigger":"press","holdMs":500,"doubleWindowMs":500,"intervalMs":150})")});
        resetRoute(savedRules, savedInput); tick(0,10000);
        const auto fieldGeneration=activateProducer(OemProducerId::RogClick);
        BYTE fieldLib[]={0x5A,147};
        for (int n=0;n<2;++n) { fixtureTick+=1000; rogOemKeyParse(fieldLib,sizeof(fieldLib),fieldGeneration); nativeOemProducerDrain(); flushJobs(); }
        check(fixtureOwnActions.size()==2 && fixtureOwnActions[0]=="window.summon" && fixtureOwnActions[1]=="window.summon" && fixtureLegacyRog==0,
            "R12 exported native-default LIB click reaches summon exactly once per actual click");
#endif
#if defined(YMCC_MSI_NATIVE_ONLY_REAR_SOURCE) && defined(YMCC_PRODUCER_EDGE_SOURCE)
        if(fixtureFieldInput.is_object()) {
            const auto fieldRules=fixtureFieldInput["buttonMapping"]["rules"]["shortcutRules"];
            bool hasM1Release=false;
            for(const auto& row:fieldRules)if(row.value("enabled",true)&&row.value("actionId","")=="window.summon"&&row.value("trigger","")=="release") {
                uint64_t required=0;hasM1Release=hasM1Release||(nativeKeyboardShortcutRequiredMask(row["inputs"],required)&&required==kOemBitM1);
            }
            if(hasM1Release) {
                resetRoute(fieldRules,fixtureFieldInput);g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);
                KBDLLHOOKSTRUCT fieldRearKey{};fieldRearKey.vkCode=VK_F11;fixtureTick=10100;
                oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&fieldRearKey));deliverPhysicalReceipts();flushJobs();
                check(fixtureOwnActions.empty(),"143315 exact saved M1 release rule cannot summon on DOWN");
                fieldRearKey.flags=LLKHF_UP;fixtureTick=10200;
                oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&fieldRearKey));deliverPhysicalReceipts();flushJobs();
                check(fixtureOwnActions.size()==1&&fixtureOwnActions[0]=="window.summon","143315 exact saved settings: observed F11 UP reaches YMCC summon once");
                check(fixtureSendInputs.empty(),"143315 field replay changes no firmware, device or virtual target");
            }
        }
#endif
        for (const auto& preset : {"fps", "racing", "custom", "steam"}) {
            for (const auto& withGame : {false, true}) {
                auto input = withGame ? dedicated(baseInput(), "game-A", false) : baseInput(false, "elite");
                const auto frozen = input.dump(); ymcc::InputShortcutRuntime runtime;
                auto on = runtime.toggle(input, "memory-session", "input.gyroToggle", preset);
                check(on.ok && on.enabled && on.effective["gyroMotion"]["enabled"] == true &&
                    on.effective["outputTarget"]["gyroEnabled"] == true && on.effective["gyroMotion"]["preset"] == preset,
                    std::string(withGame ? "legacy metadata: " : "global: ") + preset + " off -> on uses selected preset");
                auto off = runtime.toggle(input, "memory-session", "input.gyroToggle", "steam");
                check(off.ok && !off.enabled && off.effective["gyroMotion"]["enabled"] == false &&
                    off.effective["outputTarget"]["gyroEnabled"] == false && off.effective["gyroMotion"]["preset"] == preset,
                    std::string(preset) + " on -> off does not switch or memorize another preset");
                check(input.dump() == frozen && off.effective["revision"] == input["revision"] &&
                    off.effective["gyroMotion"]["presets"] == input["gyroMotion"]["presets"],
                    std::string(preset) + " does not modify durable input, revision, game profile or preset definitions");
            }
        }
        for (const auto& preset : {"steamdeck", "dualsense-edge", "elite"}) {
            auto input = baseInput(); const auto frozen = input.dump(); ymcc::InputShortcutRuntime runtime;
            auto on = runtime.toggle(input, "memory-session", "input.virtualGamepadToggle", preset);
            check(on.ok && on.enabled && on.effective["outputTarget"]["persona"] == preset &&
                on.effective["outputTarget"]["buttonMappingEnabled"] == true && on.effective["gyroMotion"]["enabled"] == true,
                std::string(preset) + " virtual off -> on and existing gyro linkage applied");
            auto off = runtime.toggle(input, "memory-session", "input.virtualGamepadToggle", "elite");
            check(off.ok && !off.enabled && off.effective["outputTarget"]["persona"] == "disabled" &&
                off.effective["outputTarget"]["buttonMappingEnabled"] == false && off.effective["gyroMotion"]["enabled"] == false && input.dump() == frozen,
                std::string(preset) + " virtual on -> off stops target/gyro without writing user settings");
            auto game = dedicated(baseInput(true, preset), "game-A", true, preset); const auto frozenGame = game.dump();
            ymcc::InputShortcutRuntime gameRuntime;
            auto closed = gameRuntime.toggle(game, "memory-session", "input.virtualGamepadToggle", "steamdeck");
            auto reopened = gameRuntime.toggle(game, "memory-session", "input.virtualGamepadToggle", "dualsense-edge");
            check(!closed.enabled && reopened.enabled && reopened.effective["outputTarget"]["persona"] == "dualsense-edge" && game.dump() == frozenGame,
                std::string(preset) + " global target can close/reopen while legacy metadata remains unchanged");
        }
        auto input = baseInput(false, "elite"); ymcc::InputShortcutRuntime runtime;
        input["gyroMotion"]["presets"]["fps"] = {{"motionMode", "suppress"}, {"motionTrigger", "LT"}, {"gyroMultiplier", 1.7}, {"gyroMode", "retired"}};
        auto stored = runtime.toggle(input, "memory-session", "input.gyroToggle", "fps");
        check(stored.effective["gyroMotion"]["gyroMultiplier"] == 1.7 && stored.effective["gyroMotion"]["motionMode"] == "on" &&
            stored.effective["gyroMotion"]["motionTrigger"] == "LT" && !stored.effective["gyroMotion"].contains("gyroMode"), "saved preset params use gyro-page suppress -> on/trigger semantics");
        runtime.clear(); input["gyroMotion"]["presets"]["fps"]["motionMode"] = "on";
        auto constant = runtime.toggle(input, "memory-session", "input.gyroToggle", "fps");
        check(constant.effective["gyroMotion"]["motionTrigger"].is_null(), "always-on stored preset removes stale trigger");
        check(constant.effective["outputTarget"]["futureTarget"] == 42 && constant.effective["gyroMotion"]["futureMotion"]["keep"] == true && constant.effective["futureInput"]["keep"] == true,
            "unknown target/motion/input fields survive temporary switches");
        auto alteredRevision = input; alteredRevision["revision"] = 8; alteredRevision["outputTarget"]["revision"] = 8; alteredRevision["gyroMotion"]["revision"] = 8;
        alteredRevision["buttonMapping"]["rules"]["futureMapping"] = false;
        check(runtime.effective(alteredRevision, "memory-session")["gyroMotion"]["enabled"] == true, "rule edits and revision-only ACK do not wipe runtime state");
        auto changedSetting = alteredRevision; changedSetting["gyroMotion"]["gyroMultiplier"] = 2.1;
        check(runtime.effective(changedSetting, "memory-session")["gyroMotion"]["enabled"] == false, "explicit base motion edit clears temporary state and uses new settings");
        runtime.toggle(input, "memory-session", "input.gyroToggle", "fps");
        check(ymcc::InputShortcutRuntime{}.effective(input, "memory-session")["gyroMotion"]["enabled"] == false,
            "new process/runtime never remembers shortcut state");
        runtime.clear(); input["gyroMotion"]["virtualPadLink"] = false; input["outputTarget"]["persona"] = "disabled"; input["outputTarget"]["buttonMappingEnabled"] = false;
        check(runtime.toggle(input, "memory-session", "input.virtualGamepadToggle", "elite").effective["gyroMotion"]["enabled"] == false,
            "virtual enable respects linkage-disabled preference");
        auto gameA = dedicated(baseInput()); runtime.clear(); runtime.toggle(gameA, "memory-session", "input.gyroToggle", "fps");
        auto gameB = dedicated(baseInput(), "game-B", true);
        check(runtime.effective(gameB, "memory-session")["gyroMotion"]["enabled"] == true, "legacy game identity change preserves the global temporary toggle");
        runtime.toggle(gameA, "memory-session", "input.gyroToggle", "fps");
        check(runtime.effective(baseInput(), "memory-session")["gyroMotion"]["enabled"] == false && runtime.active(), "removing legacy metadata preserves the global temporary off state");
        check(runtime.toggle(gameB, "memory-session", "input.gyroToggle", "fps", ymcc::inputShortcutContext(gameA, "memory-session")).ok,
            "legacy game identity change does not invalidate a queued global shortcut");
        auto changedGlobal = gameB; changedGlobal["outputTarget"]["persona"] = "elite";
        check(!runtime.toggle(changedGlobal, "memory-session", "input.gyroToggle", "fps", ymcc::inputShortcutContext(gameA, "memory-session")).ok,
            "real global configuration change still rejects a stale queued shortcut");
        check(!runtime.toggle(baseInput(), "memory-session", "input.gyroToggle", "bogus").ok &&
            !runtime.toggle(baseInput(), "memory-session", "input.virtualGamepadToggle", "disabled").ok &&
            !runtime.toggle(baseInput(), "memory-session", "unknown", "fps").ok, "invalid actions/presets fail closed and cannot resurrect hidden personas");
        // Now exercise real production parser, edge evaluator, queue and action.
        resetRoute(json::array({rule("input.gyroToggle", "fps")})); const auto original = fixtureInput.dump();
        tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); tick(XINPUT_GAMEPAD_A, 1150);
        check(g_nativeKeyboardShortcuts.size() == 1 && fixtureJobs.size() == 1 && fixtureJobs[0].second == SerialJobKind::YmccOwn, "real parser admits preset-without-key; press queues one internal (not injected) action");
        flushJobs(); check(effectiveRuntimeInputSettings()["gyroMotion"]["enabled"] == true && fixtureRefreshes == 1 && fixtureInput.dump() == original,
            "real serial dispatch applies runtime state/host refresh with zero settings writes");
        tick(0, 1200); tick(XINPUT_GAMEPAD_A, 1250); flushJobs();
        check(effectiveRuntimeInputSettings()["gyroMotion"]["enabled"] == false && fixtureRefreshes == 2 && fixtureInjections.empty(), "second press turns off; held key does not double-toggle or inject keyboard events");
        resetRoute(json::array({rule("input.gyroToggle", "racing", "hold")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); tick(XINPUT_GAMEPAD_A, 1599);
        check(fixtureJobs.empty(), "hold does not execute early"); tick(XINPUT_GAMEPAD_A, 1600); tick(XINPUT_GAMEPAD_A, 1700);
        check(fixtureJobs.size() == 1, "hold executes exactly once at threshold"); flushJobs();
        check(effectiveRuntimeInputSettings()["gyroMotion"]["motionInput"] == "joystick-steering" && effectiveRuntimeInputSettings()["gyroMotion"]["outputStick"] == "left", "real racing action routes to steering/left-stick preset");
        resetRoute(json::array({rule("input.gyroToggle", "fps", "double")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); tick(0, 1200);
        check(fixtureJobs.empty(), "double press first click does not toggle"); tick(XINPUT_GAMEPAD_A, 1250); check(fixtureJobs.size() == 1, "double press second click toggles once");
        resetRoute(json::array({rule("input.gyroToggle", "fps", "release")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); check(fixtureJobs.empty(), "release trigger ignores press"); tick(0, 1200); check(fixtureJobs.size() == 1, "release trigger toggles once on release");
        auto repeat = rule("input.gyroToggle", "fps", "repeat"); repeat["holdMs"] = 0;
        resetRoute(json::array({repeat})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); tick(XINPUT_GAMEPAD_A, 1200); tick(XINPUT_GAMEPAD_A, 1250);
        check(fixtureJobs.size() == 2, "repeat uses interval and does not duplicate adjacent samples");
        resetRoute(json::array({rule("input.virtualGamepadToggle", "steamdeck", "press", "oem", "claw")})); tick(0, 1000); fixtureOemMask = 1ull << 25; tick(0, 1100); flushJobs();
        check(effectiveRuntimeInputSettings()["outputTarget"]["persona"] == "steamdeck" && fixtureJoyxoffCloses == 1, "OEM dedicated key launches selected target and uses existing SteamDeck JoyXoff close path");
        resetRoute(json::array({rule("input.virtualGamepadToggle", "elite")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); flushJobs(); fixtureAssets = false;
        tick(0, 1200); tick(XINPUT_GAMEPAD_A, 1300); flushJobs(); check(effectiveRuntimeInputSettings()["outputTarget"]["persona"] == "disabled", "closing remains possible after assets become unavailable");
        resetRoute(json::array({rule("input.virtualGamepadToggle", "elite")})); fixtureAssets = false; tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); flushJobs();
        check(effectiveRuntimeInputSettings()["outputTarget"]["persona"] == "disabled" && fixtureRefreshes == 0, "failed enable leaves runtime/settings untouched when assets missing");
        auto desktop = rule("input.gyroToggle", "steam", "press", "keyboard", "ControlLeft"); desktop["inputs"].push_back({{"source", "mouse"}, {"code", "MouseRight"}});
        resetRoute(json::array({desktop})); tick(0, 1000); tick(0, 1050); fixtureDesktopDown = {VK_LCONTROL, VK_RBUTTON}; tick(0, 1100); tick(0, 1150); flushJobs();
        check(g_nativeKeyboardShortcuts.size() == 1 && fixtureRefreshes == 1 && effectiveRuntimeInputSettings()["gyroMotion"]["preset"] == "steam", "real keyboard+mouse rule toggles selected preset exactly once");
        resetRoute(json::array({desktop})); fixtureDesktopDown = {VK_LCONTROL, VK_RBUTTON}; tick(0, 1000); tick(0, 1100);
        check(fixtureJobs.empty(), "new desktop rule cannot fire against keys already held before activation"); fixtureDesktopDown.clear(); tick(0, 1200); fixtureDesktopDown = {VK_LCONTROL, VK_RBUTTON}; tick(0, 1250); check(fixtureJobs.size() == 1, "desktop rule rearms only after neutral");
        resetRoute(json::array({rule("input.gyroToggle", "fps")})); tick(XINPUT_GAMEPAD_A, 1000); tick(XINPUT_GAMEPAD_A, 1100); check(fixtureJobs.empty(), "controller rule also requires release before first action");
        resetRoute(json::array({rule("input.gyroToggle", "fps")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); fixtureInput = dedicated(fixtureInput, "next-game", false); flushJobs();
        check(fixtureRefreshes == 1 && effectiveRuntimeInputSettings()["gyroMotion"]["enabled"] == true, "real queued job ignores legacy game metadata and applies the global temporary toggle");
        // GP-OEM-ACTION-R2 regression: frontend-advertised own actions must not
        // silently disappear in the real native parser (the before image fails here).
        for (const auto& action : {"window.summon", "window.hideToTray", "os.returnDesktop", "game.killCurrent", "os.openTouchKeyboard", "mouse.toggle", "power.tdpAdjust", "display.brightnessAdjust"}) {
            auto own = rule(action, "unused", "press", "oem", "cc"); own["params"] = json::object();
            resetRoute(json::array({own})); tick(0, 1000);
            check(g_nativeKeyboardShortcuts.size() == 1, std::string("OEM advertised action admitted: ") + action);
            fixtureOemMask = 1ull << 22; tick(0, 1100); tick(0, 1150); flushJobs();
            check(fixtureOwnActions.size() == 1 && fixtureRefreshes == 0 && fixtureInjections.empty(), std::string("OEM own action emitted once through existing boundary: ") + action);
        }
        auto own = rule("window.summon", "unused", "press", "oem", "cc"); own["params"] = json::object();
        resetRoute(json::array({own})); tick(XINPUT_GAMEPAD_A, 1000); fixtureOemMask = 1ull << 22; tick(XINPUT_GAMEPAD_A, 1100);
        check(fixtureOwnActions.size() == 1, "independent OEM rule arms and fires while gamepad A is held");
        resetRoute(json::array({own})); fixtureOemMask = 1ull << 22; tick(0, 1000); tick(0, 1100);
        check(fixtureOwnActions.empty(), "saved OEM rule never fires against a pre-held OEM key");
        fixtureOemMask = 0; tick(0, 1200); fixtureOemMask = 1ull << 22; tick(0, 1250);
        check(fixtureOwnActions.size() == 1, "OEM rule rearms after its own neutral sample");
        auto legacy = own; legacy["origin"] = "native-default"; legacy["defaultKey"] = "enabled";
        legacy["inputs"] = json::array({{{"source","controller"},{"code","lb"}},{{"source","controller"},{"code","rb"}}}); legacy["trigger"]="hold";
        resetRoute(json::array({legacy})); tick(0, 1000);
        check(g_nativeKeyboardShortcuts.empty(), "unedited legacy own-action template is not dispatched twice");
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        for (const auto& def:r12Defaults) {
            const auto key=def["defaultKey"].get<std::string>();
            if(key=="startDoubleF7")continue; // Retired row is covered above, including its saved enabled form.
            check(nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 pristine legacy slot remains available: "+key);
            auto changed=def; changed["inputs"]=json::array({{{"source","oem"},{"code","lib"}}}); changed["trigger"]="press";
            resetRoute(json::array({changed}));tick(0,10000);
            check(g_nativeKeyboardShortcuts.size()==1,"R12 saved default remap parses: "+key);
            check(!nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 edited slot disables only its original legacy owner: "+key);
            const auto gen=activateProducer(OemProducerId::RogClick);BYTE data[]={0x5A,147};fixtureTick+=20;rogOemKeyParse(data,2,gen);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==1,"R12 edited default performs its configured action exactly once: "+key);
            changed["enabled"]=false;resetRoute(json::array({changed}));tick(0,10000);
            const auto offGen=activateProducer(OemProducerId::RogClick);rogOemKeyParse(data,2,offGen);nativeOemProducerDrain();flushJobs();
            check(g_nativeKeyboardShortcuts.empty() && fixtureOwnActions.empty() && fixtureInjections.empty(),"R12 disabled remap cannot fire: "+key);
            check(!nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 disabled edited slot does not resurrect legacy binding: "+key);
            resetRoute(r12Defaults);tick(0,10000); check(nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 restoring pristine default restores its legacy owner: "+key);
        }
        // Relevant trigger timing and parameters must also select the rule owner.
        for (auto def:r12Defaults) {
            const auto key=def["defaultKey"].get<std::string>(); if(key=="startDoubleF7")continue;
            if(def["trigger"]=="hold")def["holdMs"]=750;
            else if(def["trigger"]=="double")def["doubleWindowMs"]=800;
            else if(def["trigger"]=="repeat")def["intervalMs"]=240;
            else def["trigger"]="release";
            resetRoute(json::array({def}));tick(0,10000);
            check(g_nativeKeyboardShortcuts.size()==1 && !nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 edited trigger/timing transfers ownership: "+key);
        }
        for(auto index:{3,4}) {auto def=r12Defaults[index];def["params"]["amount"]="13";def["params"]["direction"]=index==3?"down":"left";resetRoute(json::array({def}));tick(0,10000);
            check(g_nativeKeyboardShortcuts.size()==1 && g_nativeKeyboardShortcuts[0].actionDelta==-13,"R12 edited default direction/amount reaches native action");
            def["params"]["amount"]="999";resetRoute(json::array({def}));tick(0,10000);
            check(g_nativeKeyboardShortcuts.empty() && !nativeShortcutLegacyDefaultAllowed(def["defaultKey"].get<std::string>().c_str()),"R12 invalid edited default remains rejected without falling back to old chord");}
        // Exact production legacy region: a replacement must not leave an old
        // physical chord firing alongside the new OEM rule.
        for(const auto& def:r12Defaults) {
            const auto key=def["defaultKey"].get<std::string>();if(key=="startDoubleF7")continue;
            uint64_t oldMask=0;nativeKeyboardShortcutRequiredMask(def["inputs"],oldMask);
            auto runOldChord=[&](){fixtureLegacyReset();fixtureLegacyEval(0,10000);if(key=="bDoubleMinimize"){fixtureLegacyEval(static_cast<WORD>(oldMask),10100);fixtureLegacyEval(0,10150);fixtureLegacyEval(static_cast<WORD>(oldMask),10300);fixtureLegacyEval(0,10350);fixtureLegacyEval(0,10410);flushJobs();return;}fixtureLegacyEval(static_cast<WORD>(oldMask),10100);fixtureLegacyEval(static_cast<WORD>(oldMask),10700);fixtureLegacyEval(0,10800);fixtureLegacyEval(static_cast<WORD>(oldMask),10900);fixtureLegacyEval(0,11000);fixtureLegacyEval(0,11100);flushJobs();};
            resetRoute(json::array({def}));tick(0,10000);runOldChord();
            check(!fixtureOwnActions.empty(),"R12 production legacy positive control works: "+key);
            auto edited=def;edited["inputs"]=json::array({{{"source","oem"},{"code","lib"}}});edited["trigger"]="press";
            resetRoute(json::array({edited}));tick(0,10000);runOldChord();
            check(fixtureOwnActions.empty(),"R12 production legacy chord no longer competes with edited default: "+key);
        }
        auto editedB=r12Defaults[1];editedB["inputs"]=json::array({{{"source","oem"},{"code","lib"}}});editedB["trigger"]="press";
        resetRoute(json::array({editedB}));tick(0,10000);fixtureLegacyReset();g_bClosePending=true;g_bClosePendingSince=10000;g_bCloseReadyAt=10050;fixtureLegacyEval(0,10100);
        check(!g_bClosePending && fixtureOwnActions.empty(),"R12 replacing default cancels preexisting legacy double-B pending");
        for(auto code:{56,166}) {auto edited=r12Defaults[0];edited["inputs"]=json::array({{{"source","oem"},{"code",code==56?"ac":"cc"}}});edited["trigger"]="press";
            resetRoute(json::array({edited}));tick(0,10000);const auto gen=activateProducer(OemProducerId::RogClick);BYTE raw[]={0x5A,static_cast<BYTE>(code)};rogOemKeyParse(raw,2,gen);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==1 && fixtureLegacyRog==0,"R12 configured AC/CC executes remap and suppresses YMCC legacy window fallback");}
        auto editedWmi=r12Defaults[0];editedWmi["inputs"]=json::array({{{"source","oem"},{"code","claw"}}});editedWmi["trigger"]="press";
        resetRoute(json::array({editedWmi}));tick(0,10000);const auto editedWmiGen=activateWmi();msiWmiHandleEvent(41,editedWmiGen);nativeMsiWmiShortcutDrain();flushJobs();
        check(fixtureOwnActions.size()==1 && fixtureLegacyClaw==0,"R12 edited MSI WMI default also executes once without legacy CLAW fallback");
        // The shared parser fix also serves sampled vendor sources, not only ROG.
        struct R12Vendor {OemProducerId id;const char* code;uint64_t bit;};
        const R12Vendor vendors[]={{OemProducerId::Legion,"legionl",kOemBitLegionL},{OemProducerId::LegionS,"legionl",kOemBitLegionL},{OemProducerId::Gpd,"l4",kOemBitL4},{OemProducerId::OneX,"special",kOemBitSpecial},{OemProducerId::Msi,"m1",kOemBitM1}};
        for(const auto& v:vendors){auto edited=r12Defaults[0];edited["inputs"]=json::array({{{"source","oem"},{"code",v.code}}});edited["trigger"]="press";
            resetRoute(json::array({edited}));activateProducer(v.id);producerSample(v.id,0);tick(0,fixtureTick);producerSample(v.id,v.bit);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==1 && !nativeShortcutLegacyDefaultAllowed("enabled"),std::string("R12 real vendor producer dispatches edited default once: ")+v.code);
            producerSample(v.id,0);nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.size()==1,"R12 vendor release cannot repeat press action");}
#endif
        auto mixed = own; mixed["inputs"].push_back({{"source", "controller"}, {"code", "a"}});
        auto wrongOem = own; wrongOem["inputs"][0]["code"] = "a";
        auto wrongController = own; wrongController["inputs"][0]["source"] = "controller";
        resetRoute(json::array({mixed, wrongOem, wrongController})); tick(0, 1000);
        check(g_nativeKeyboardShortcuts.empty(), "native rejects mixed OEM/controller and wrong source classes");
        for (const auto& action : {"power.tdpAdjust", "display.brightnessAdjust"}) {
            auto step = rule(action, "unused", "press", "oem", "cc");
            step["params"] = {{"direction", std::string(action) == "power.tdpAdjust" ? "down" : "left"}, {"amount", "13"}};
            resetRoute(json::array({step})); tick(0, 1000); fixtureOemMask = 1ull << 22; tick(0, 1100); flushJobs();
            check(fixtureDeltas.size() == 1 && fixtureDeltas[0] == -13, std::string("custom step amount/direction preserved: ") + action);
            step["params"]["amount"] = "999"; resetRoute(json::array({step})); tick(0, 1000);
            check(g_nativeKeyboardShortcuts.empty(), "out-of-range custom step rejected");
        }
        resetRoute(json::array({own})); tick(0, 1000); g_shortcutRecordingActive = true;
        fixtureOemMask = 1ull << 22; tick(0, 1100); g_shortcutRecordingActive = false; tick(0, 1150);
        check(fixtureOwnActions.empty(), "recording cancels OEM edge and requires a new neutral");
        fixtureOemMask = 0; tick(0, 1200); fixtureOemMask = 1ull << 22; tick(0, 1250);
        check(fixtureOwnActions.size() == 1, "OEM action recovers normally after recording release");
#if defined(YMCC_SHORTCUT_SOURCE_SCOPES)
        resetRoute(json::array({own})); nativeKeyboardShortcutsEvaluate(XINPUT_GAMEPAD_A, XINPUT_GAMEPAD{}, 1000, false);
        fixtureOemMask = 1ull << 22; nativeKeyboardShortcutsEvaluate(XINPUT_GAMEPAD_A, XINPUT_GAMEPAD{}, 1100, false);
        check(fixtureOwnActions.size() == 1, "OEM lane remains available while parent controller lane is inhibited");
        resetRoute(json::array({rule("keyboard.numeric", "unused")}));
        nativeKeyboardShortcutsEvaluate(0, XINPUT_GAMEPAD{}, 1000, false);
        nativeKeyboardShortcutsEvaluate(XINPUT_GAMEPAD_A, XINPUT_GAMEPAD{}, 1100, false);
        check(fixtureJobs.empty(), "inhibited parent lane cannot inject a controller action");
#endif
        auto delayed = rule("os.openTouchKeyboard", "unused", "press", "oem", "cc"); delayed["params"] = json::object();
        resetRoute(json::array({delayed})); tick(0, 1000); fixtureOemMask = 1ull << 22; tick(0, 1100);
        g_shortcutRecordingActive = true; flushJobs(); g_shortcutRecordingActive = false;
        check(fixtureOwnActions.empty(), "queued own action rechecks recording at actual execution");
        resetRoute(json::array({delayed})); tick(0, 1000); fixtureOemMask = 1ull << 22; tick(0, 1100);
        g_exitRequested = true; flushJobs(); g_exitRequested = false;
        check(fixtureOwnActions.empty(), "queued own action rechecks terminal exit at actual execution");
        auto invalid = rule("input.gyroToggle", "invalid"); auto disabled = rule("input.virtualGamepadToggle", "elite"); disabled["enabled"] = false;
        resetRoute(json::array({invalid, disabled, rule("unknown", "fps")})); tick(0, 1000); check(g_nativeKeyboardShortcuts.empty(), "native parser rejects invalid/disabled/unknown action rules");
        auto keyboard = rule("keyboard.numeric", "unused"); keyboard["params"] = {{"key", "F7"}};
        resetRoute(json::array({keyboard})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100); check(fixtureJobs.size() == 1 && fixtureJobs[0].second == SerialJobKind::ExternalInjection, "existing keyboard action retains its original external-injection queue"); flushJobs();
        check(fixtureInjections.size() == 1 && fixtureInjections[0].first == VK_F7 && fixtureRefreshes == 0, "existing F7 action sends F7 without toggling input");
        auto gyroOnly = baseInput(true, "elite"); gyroOnly["outputTarget"]["buttonMappingEnabled"] = false;
        ymcc::InputShortcutRuntime gyroOnlyRuntime;
        check(!gyroOnlyRuntime.toggle(gyroOnly, "memory-session", "input.virtualGamepadToggle", "steamdeck").enabled,
            "gyro-only virtual target counts as currently on and switches off rather than changing persona");
        resetRoute(json::array({rule("input.gyroToggle", "fps")})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100);
        g_shortcutRecordingActive = true; flushJobs();
        check(fixtureRefreshes == 0 && !inputShortcutRuntimeSnapshot()["active"].get<bool>(), "recording suppresses queued input actions without touching runtime/config");
        g_shortcutRecordingActive = false; nativeInputShortcutToggle("input.gyroToggle", "fps", ymcc::inputShortcutContext(fixtureInput, gameInputOwnerSession()));
        auto runtimeFeedback = inputShortcutRuntimeSnapshot();
        check(runtimeFeedback["active"] == true && runtimeFeedback["gyroEnabled"] == true,
            "read-only runtime feedback reports actual temporary gyro state");
        clearInputShortcutRuntime();
        check(inputShortcutRuntimeSnapshot()["active"] == false && inputShortcutRuntimeSnapshot()["gyroEnabled"] == false,
            "manual clear restores base state without a settings write");
        // GP-OEM-EDGE-R3: both physical edges arrive between heartbeat samples.
        // Against the R2 before-image, the real hook fails the receipt assertion.
        auto shortKey = rule("os.openTouchKeyboard", "unused", "press", "oem", "l4");
        shortKey["params"] = json::object();
        resetRoute(json::array({shortKey})); tick(0, 1000);
        KBDLLHOOKSTRUCT physical{}; physical.vkCode = VK_F14;
        oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        physical.flags = LLKHF_UP;
        oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
        check(fixturePhysicalReceipts.size() == 2 && fixtureOemMask == 0,
            "real LL hook preserves short down/up receipts even when live sample is already neutral");
#if defined(YMCC_SHORTCUT_EDGE_SOURCE)
        deliverPhysicalReceipts();
        flushJobs(); check(fixtureOwnActions.size() == 1, "short physical press executes once from receipt snapshot, not the later zero mask");
        check(!g_nativeKeyboardShortcuts[0].matchedLastTick, "short key release leaves the real rule neutral");
        resetRoute(json::array({shortKey})); tick(0, 1000);
        physical.flags = 0; oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        check(fixturePhysicalReceipts.size() == 1, "physical repeat emits no duplicate LL edge receipt");
        physical.flags = LLKHF_INJECTED | LLKHF_UP; oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
        check(fixturePhysicalReceipts.size() == 1 && g_oemKbDown != 0, "injected release cannot end a physical OEM hold");
        physical.flags = LLKHF_UP; oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
        check(fixturePhysicalReceipts.size() == 2, "physical up pairs the original physical down");
        check(oemShortcutMessageTick((1ull << 32) + 40, 0xFFFFFFD0u) == (1ull << 32) - 48,
            "message timestamp unwrap preserves time across 32-bit uptime rollover");
        auto doubleKey = shortKey; doubleKey["trigger"] = "double";
        resetRoute(json::array({doubleKey})); tick(0, 1000);
        const auto delayedToken = oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(), g_gpdBackIfaceGen.load());
        const DWORD deliveryNow = static_cast<DWORD>(GetTickCount64());
        fixtureMessageTime = static_cast<LONG>(deliveryNow - 1000);
        nativeKeyboardShortcutHandleOemEdge(static_cast<WPARAM>(kOemBitL4), static_cast<LPARAM>(delayedToken));
        fixtureMessageTime = static_cast<LONG>(deliveryNow - 990);
        nativeKeyboardShortcutHandleOemEdge(0, static_cast<LPARAM>(delayedToken));
        fixtureMessageTime = static_cast<LONG>(deliveryNow - 300);
        nativeKeyboardShortcutHandleOemEdge(static_cast<WPARAM>(kOemBitL4), static_cast<LPARAM>(delayedToken));
        flushJobs(); check(fixtureOwnActions.empty(), "UI delivery delay cannot turn two physically separated presses into a double click");
        auto releaseKey = shortKey; releaseKey["trigger"] = "release";
        resetRoute(json::array({releaseKey})); tick(0, 1000); physical.flags = 0;
        oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        physical.flags = LLKHF_UP; oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
        deliverPhysicalReceipts();
        flushJobs(); check(fixtureOwnActions.size() == 1, "release action receives both edges without waiting for a heartbeat");
        for(const auto* cancellation : {"profile", "rule", "recording", "exit", "gpd-generation", "disabled", "interface-absent"}) {
            resetRoute(json::array({shortKey})); tick(0, 1000); physical.flags = 0;
            oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
            const auto receipt = fixturePhysicalReceipts.front();
            const std::string why = cancellation;
            if (why == "profile") { g_oemBackProfileId = 9; ++g_oemShortcutEdgeEpoch; }
            if (why == "rule") { ++g_oemShortcutEdgeEpoch; }
            if (why == "recording") g_shortcutRecordingActive = true;
            if (why == "exit") g_exitRequested = true;
            if (why == "gpd-generation") ++g_gpdBackIfaceGen;
            if (why == "disabled") g_oemKeyHooksActive = false;
            if (why == "interface-absent") g_gpdBackIfaceReady = false;
            nativeKeyboardShortcutHandleOemEdge(receipt.first, receipt.second); flushJobs();
            check(fixtureOwnActions.empty(), std::string("queued physical edge cancelled at ") + cancellation);
        }
        resetRoute(json::array({shortKey})); tick(0, 1000); fixturePostOk = false; physical.flags = 0;
        oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        check(fixturePhysicalReceipts.empty() && g_oemShortcutEdgePostFailures == 1, "post failure keeps live state and records bounded failure counter");
        tick(0, 1100); flushJobs(); check(fixtureOwnActions.size() == 1, "post failure retains existing live-sample fallback");
        check(std::any_of(fixtureLogs.begin(), fixtureLogs.end(), [](const json& j) { return j.value("event", "") == "oem-shortcut-edge-delivery" && j.value("reason", "") == "post-failed-live-sampling-retained"; }), "post failure reported outside hook hot path");
        auto guide = rule("os.openTouchKeyboard", "unused", "press", "oem", "guide"); guide["params"] = json::object();
        resetRoute(json::array({guide})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::Ayn); tick(0, 1000); MSLLHOOKSTRUCT mouse{};
        oemBackMouseHookProc(HC_ACTION, WM_LBUTTONDOWN, reinterpret_cast<LPARAM>(&mouse)); mouse.mouseData = XBUTTON2 << 16;
        oemBackMouseHookProc(HC_ACTION, WM_XBUTTONDOWN, reinterpret_cast<LPARAM>(&mouse));
        oemBackMouseHookProc(HC_ACTION, WM_XBUTTONUP, reinterpret_cast<LPARAM>(&mouse));
        oemBackMouseHookProc(HC_ACTION, WM_LBUTTONUP, reinterpret_cast<LPARAM>(&mouse));
        check(fixturePhysicalReceipts.size() == 2, "real mouse hook preserves the short Guide chord pair");
        deliverPhysicalReceipts();
        flushJobs(); check(fixtureOwnActions.size() == 1, "mouse chord action consumes its event pair once");
        auto controller = rule("input.gyroToggle", "fps", "hold");
        resetRoute(json::array({controller, shortKey})); tick(0, 1000); tick(XINPUT_GAMEPAD_A, 1100);
        const auto controllerBefore = g_nativeKeyboardShortcuts[0];
        physical.flags = 0; oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
        deliverPhysicalReceipts();
        check(g_nativeKeyboardShortcuts[0].matchedLastTick == controllerBefore.matchedLastTick && g_nativeKeyboardShortcuts[0].holdStartedAt == controllerBefore.holdStartedAt && g_nativeKeyboardShortcuts[0].needsNeutral == controllerBefore.needsNeutral,
            "OEM receipt never mutates an unrelated controller hold rule");
        resetRoute(json::array({releaseKey})); tick(0, 1000); physical.flags = 0;
        oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical)); deliverPhysicalReceipts();
        ++g_oemShortcutEdgeEpoch; fixtureOemMask = 0; tick(0, 1200); flushJobs();
        check(fixtureOwnActions.empty(), "source cancellation cannot masquerade as a user release action");
        for (const auto* action : {"os.openTouchKeyboard", "input.gyroToggle", "keyboard.numeric"}) {
            auto delayed = rule(action, "fps", "press", "oem", "l4");
            if (std::string(action) == "keyboard.numeric") delayed["params"] = {{"key", "Digit7"}};
            resetRoute(json::array({delayed})); tick(0, 1000); physical.flags = 0;
            oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical)); deliverPhysicalReceipts();
            check(fixtureJobs.size() == 1, std::string("OEM action accepted by existing queue: ") + action);
            ++g_oemShortcutEdgeEpoch; flushJobs();
            check(fixtureOwnActions.empty() && fixtureRefreshes == 0 && fixtureInjections.empty(),
                std::string("queued OEM job rejects a later source/rule cancellation: ") + action);
        }
        for (const auto* code : {"l4", "r4"}) {
            auto legacyRule = shortKey; legacyRule["inputs"][0]["code"] = code;
            resetRoute(json::array({legacyRule})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::GpdLegacy); tick(0, 1000);
            const bool left = std::string(code) == "l4";
            physical.flags = 0; physical.vkCode = left ? VK_F11 : VK_F12;
            oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
            physical.vkCode = left ? 'L' : 'R'; oemBackHookProc(HC_ACTION, WM_KEYDOWN, reinterpret_cast<LPARAM>(&physical));
            check(fixturePhysicalReceipts.size() == 1 && static_cast<uint64_t>(fixturePhysicalReceipts[0].first) == (left ? kOemBitL4 : kOemBitR4),
                std::string("real GPD legacy profile publishes catalog canonical bit: ") + code);
            physical.flags = LLKHF_UP; oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
            physical.vkCode = left ? VK_F11 : VK_F12; oemBackHookProc(HC_ACTION, WM_KEYUP, reinterpret_cast<LPARAM>(&physical));
            deliverPhysicalReceipts(); flushJobs();
            check(fixtureOwnActions.size() == 1 && g_oemBackStateKb == 0 && g_oemKeyMask == 0,
                std::string("real GPD legacy profile-to-rule chain fires and releases: ") + code);
        }
        auto msiKey = rule("os.openTouchKeyboard", "unused", "press", "oem", "m1"); msiKey["params"] = json::object();
        resetRoute(json::array({msiKey})); g_oemBackProfileId = 9; tick(0, 1000);
        const auto token = oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(), 1);
        fixtureMsiAuthority = static_cast<int>(kOemBitM1);
        nativeKeyboardShortcutHandleOemEdge(static_cast<WPARAM>(kOemBitM1), static_cast<LPARAM>(token)); flushJobs();
        check(fixtureOwnActions.empty(), "current MSI DInput authority rejects an older queued keyboard fallback");
#endif
#if defined(YMCC_OEM_CONSUME_SOURCE)
        // Every OS boundary is fake, including SendInput. The hook return,
        // replay ledger, matcher, parser, receipt and queued actions are real.
        const auto keyEvent = [](DWORD vk, bool up = false, DWORD extraFlags = 0) {
            KBDLLHOOKSTRUCT key{}; key.vkCode = vk; key.scanCode = vk & 255;
            key.flags = extraFlags | (up ? LLKHF_UP : 0);
            return oemBackHookProc(HC_ACTION, up ? WM_KEYUP : WM_KEYDOWN, reinterpret_cast<LPARAM>(&key));
        };
#if defined(YMCC_WMI_CLICK_SOURCE) && defined(YMCC_SHORTCUT_EDGE_SOURCE) && defined(YMCC_MSI_NATIVE_ONLY_REAR_SOURCE)
        // QS has two independent contracts: WMI owns the action; the known
        // MSI keyboard mirror must not leak the same configured key to OS.
        for(const DWORD alias:{DWORD('G'),DWORD(VK_TAB)})for(bool wmiFirst:{false,true}) {
            auto q=producerRule("qs");resetRoute(json::array({q}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);
            const auto generation=activateWmi();tick(0,10001);fixtureTick=10100;
            check(msiWmiShortcutAuthoritative(),"QS leak reproducer uses real WMI authority");
            if(wmiFirst){msiWmiHandleEvent(88,generation);nativeMsiWmiShortcutDrain();flushJobs();}
            check(keyEvent(VK_LWIN)==1&&keyEvent(alias)==1,"configured known-MSI QS consumes original Win+G/Win+Tab DOWNs with WMI active");
            deliverPhysicalReceipts();flushJobs();
            if(!wmiFirst){msiWmiHandleEvent(88,generation);nativeMsiWmiShortcutDrain();flushJobs();}
            check(keyEvent(alias,true)==1&&keyEvent(VK_LWIN,true)==1,"configured MSI QS pairs both consumed UPs");deliverPhysicalReceipts();flushJobs();
            check(fixtureReplayed.empty()&&g_oemKeyboardConsume.replayCount==0,"QS keyboard mirror cannot leak a replayed OS shortcut");
            check(fixtureOwnActions.size()==1&&fixtureOwnActions[0]=="os.openTouchKeyboard","one WMI click plus keyboard mirror emits exactly one YMCC action in either arrival order");
        }
#ifdef YMCC_MSI_QS_MIRROR_SOURCE
        for(const char* trigger:{"hold","release","repeat"}) {
            resetRoute(json::array({producerRule("qs",trigger)}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);const auto gen=activateWmi();tick(0,10001);
            check(!(oemKeyboardConsumeEligibleMask()&kOemBitQs),"unsupported click-only trigger cannot silence OS mirror");
            check(keyEvent(VK_LWIN)==0&&keyEvent('G')==0&&keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0,"unsupported QS trigger passes original keyboard pairs");
            msiWmiHandleEvent(88,gen);nativeMsiWmiShortcutDrain();flushJobs();check(fixtureOwnActions.empty()&&clickStatus("unconsumed"),"unsupported WMI rule stays visibly unconsumed, not a fake accepted action");
        }
        for(bool disabled:{false,true}) {
            auto q=producerRule("qs");if(disabled)q["enabled"]=false;else q["inputs"].push_back({{"source","oem"},{"code","m1"}});
            resetRoute(json::array({q}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);activateWmi();tick(0,10001);
            check(!(oemKeyboardConsumeEligibleMask()&kOemBitQs),"disabled or composite QS rule has no mirror ownership");
            check(keyEvent(VK_LWIN)==0&&keyEvent('G')==0&&keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0,"disabled/composite QS does not steal keyboard keys");
        }
        for(int profile=0;profile<=24;++profile) {
            if(profile==static_cast<int>(OemChordProfileId::Msi))continue;
            resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=profile;tick(0,10000);activateWmi();tick(0,10001);
            check(!g_msiQsKeyboardMirrorPolicy,"non-MSI profile cannot publish the MSI QS mirror policy");
            check(keyEvent(VK_LWIN)==0&&keyEvent('G')==0&&keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0,"non-MSI profile preserves Win+G delivery");
        }
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);fixtureMsiKnownBoard=false;tick(0,10000);activateWmi();tick(0,10001);
        check(!g_msiQsKeyboardMirrorPolicy&&keyEvent(VK_LWIN)==0&&keyEvent('G')==0,"forcing MSI profile on an unknown board does not authorise QS interception");keyEvent('G',true);keyEvent(VK_LWIN,true);
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);auto mirrorGen=activateWmi();tick(0,10001);
        check(g_msiQsKeyboardMirrorSourceGeneration==mirrorGen&&(oemKeyboardConsumeEligibleMask()&kOemBitQs),"mirror policy is bound to a current subscribed source");
        fixtureWmiTerminal=true;msiWmiSetEnabled(false);
        check(!(oemKeyboardConsumeEligibleMask()&kOemBitQs),"WMI loss immediately removes old mirror permission before UI publish");
        check(keyEvent(VK_LWIN)==0&&keyEvent('G')==0&&keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0,"lost mirror cannot silently become an unarmed fallback consumer");
        // Deliver posted transition messages once, in Win32 FIFO order, before
        // the later neutral timer; do not replay already-consumed fixture messages.
        deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.empty(),"source transition messages cannot inherit an armed fallback action");
        fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
        tick(0,11000);check((oemKeyboardConsumeEligibleMask()&kOemBitQs)&&g_msiQsKeyboardMirrorSourceGeneration==0,"neutral UI transition can republish ordinary keyboard fallback");
        fixtureTick=11100;keyEvent(VK_LWIN);keyEvent('G');keyEvent('G',true);keyEvent(VK_LWIN,true);deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.size()==1&&fixtureOwnActions[0]=="os.openTouchKeyboard","after neutral republish, WMI unavailable keyboard fallback still acts once");
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);mirrorGen=activateWmi();tick(0,10001);
        fixtureWmiTerminal=true;msiWmiSetEnabled(false);const auto newMirrorGen=activateWmi();
        check(newMirrorGen!=mirrorGen&&!(oemKeyboardConsumeEligibleMask()&kOemBitQs),"new WMI subscription cannot inherit older mirror permission");
        tick(0,11000);check(g_msiQsKeyboardMirrorSourceGeneration==newMirrorGen&&(oemKeyboardConsumeEligibleMask()&kOemBitQs),"current UI authorises only the new subscribed generation");
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);activateWmi();tick(0,10001);g_shortcutRecordingActive=true;
        check(keyEvent(VK_LWIN)==0&&keyEvent('G')==0&&keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0,"recording never consumes QS keyboard pairs");
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);activateWmi();tick(0,10001);
        keyEvent('G');keyEvent(VK_TAB);keyEvent(VK_LWIN);fixtureTick=11000;oemKeyboardConsumeMaintain(fixtureTick);
        check(fixtureReplayed.size()==3&&g_oemConsumeMatched==0,"unmatched Win+G+Tab composite replays instead of claiming QS-only suppression");keyEvent(VK_LWIN,true);keyEvent(VK_TAB,true);keyEvent('G',true);
        resetRoute(json::array({producerRule("qs")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,10000);mirrorGen=activateWmi();tick(0,10001);fixturePostOk=false;
        keyEvent(VK_LWIN);keyEvent('G');check(fixtureReplayed.size()==2,"receipt post failure repays both original DOWNs");
        check(keyEvent('G',true)==0&&keyEvent(VK_LWIN,true)==0&&!oemKeyboardConsumeBusy(),"repaid DOWNs have ordinary paired UPs with no debt");
        msiWmiHandleEvent(88,mirrorGen);nativeMsiWmiShortcutDrain();flushJobs();check(fixtureOwnActions.size()==1,"keyboard delivery failure does not duplicate the retained WMI action");
#endif
#endif
#ifdef YMCC_MSI_NATIVE_ONLY_REAR_SOURCE
        // D4462D53 SD success is preserved separately; here prove ordinary OEM
        // keyboard delivery uses real hook/parser/evaluator with target disabled.
        for(bool m1:{false,true}){
            auto ordinary=baseInput();ordinary["outputTarget"]["persona"]="disabled";
            ordinary["outputTarget"]["buttonMappingEnabled"]=false;ordinary["outputTarget"]["gyroEnabled"]=false;
            auto nativeRearRule=rule("os.openTouchKeyboard","unused","press","oem",m1?"m1":"m2");nativeRearRule["params"]=json::object();
            resetRoute(json::array({nativeRearRule}),ordinary);g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,1000);
            check(g_msiNativeRearDemand.load(),"ordinary MSI configured rear rule publishes native source demand without virtual target");
            const DWORD key=m1?VK_F11:VK_F14;check(keyEvent(key)==1&&keyEvent(key,true)==1,"ordinary MSI F11/F14 press and release consumed by exact configured OEM rule");
            deliverPhysicalReceipts();flushJobs();check(fixtureOwnActions.size()==1&&fixtureOwnActions[0]=="os.openTouchKeyboard","ordinary MSI native keyboard hook-to-receipt-to-action executes once with target disabled");
            check(g_inputHostBoundPersona=="disabled"&&fixtureSendInputs.empty(),"ordinary MSI action does not create virtual target or inject a synthetic controller");
        }
#endif
        auto rearRule = rule("os.openTouchKeyboard", "unused", "press", "oem", "l4"); rearRule["params"] = json::object();
        resetRoute(json::array({rearRule})); tick(0, 1000);
        check(g_oemConsumePolicyMask == kOemBitL4, "armed independent OEM rule publishes consumption policy");
        check(keyEvent(VK_F14) == 1 && keyEvent(VK_F14) == 1 && keyEvent(VK_F14, true) == 1,
            "GPD bare base key consumes paired DOWN/repeat/UP");
        deliverPhysicalReceipts(); flushJobs();
        check(fixtureOwnActions.size() == 1 && fixtureReplayed.empty() && !oemKeyboardConsumeBusy(),
            "consumed bare base key emits once and leaves no replay/pair debt");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        check(keyEvent(VK_LWIN, false, LLKHF_EXTENDED) == 1 && keyEvent(VK_F14) == 1,
            "GPD fixed-base with Win prefix buffers then consumes full configured chord");
        check(keyEvent(VK_F14, true) == 1 && keyEvent(VK_LWIN, true, LLKHF_EXTENDED) == 1 && fixtureReplayed.empty(),
            "matched Win prefix and base never leak an orphan release");
        deliverPhysicalReceipts(); flushJobs(); check(fixtureOwnActions.size() == 1, "prefixed GPD chord still emits one configured action");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        check(keyEvent(VK_LCONTROL) == 1 && keyEvent('C') == 1, "unmatched ordinary Ctrl+C is temporarily owned as one ordered batch");
        check(fixtureReplayed.size() == 2 && fixtureReplayed[0].ki.wVk == VK_LCONTROL && fixtureReplayed[1].ki.wVk == 'C',
            "unmatched Ctrl+C replays original DOWN order");
        check(keyEvent('C', true) == 0 && keyEvent(VK_LCONTROL, true) == 0,
            "replayed DOWNs have ordinary passed UPs");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_LWIN, false, LLKHF_EXTENDED);
        const auto prefixStart = g_oemKeyboardConsume.startedAt;
        oemKeyboardConsumeMaintain(prefixStart + kOemKeyboardPrefixBudgetMs);
        check(fixtureReplayed.size() == 1 && (fixtureReplayed[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY),
            "absolute prefix budget preserves extended Win key and replays at expiry");
        check(keyEvent(VK_F14) == 0 && keyEvent(VK_F14, true) == 0 && keyEvent(VK_LWIN, true, LLKHF_EXTENDED) == 0,
            "already-passed long prefix keeps mapping but does not claim complete OS suppression");
        deliverPhysicalReceipts(); flushJobs(); check(fixtureOwnActions.size() == 1, "long prefix does not break approved GPD C4 mapping semantics");
        resetRoute(json::array()); tick(0, 1000);
        check(keyEvent(VK_F14) == 0 && keyEvent(VK_F14, true) == 0 && keyEvent(VK_LWIN) == 0,
            "no configured action leaves every original OEM/modifier key alone");
        resetRoute(json::array({rearRule})); g_shortcutRecordingActive = true; tick(0, 1000);
        check(keyEvent(VK_F14) == 0 && keyEvent(VK_F14, true) == 0 && g_oemConsumePolicyMask == 0,
            "recording never starts new key consumption");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_F14); g_shortcutRecordingActive = true;
        check(keyEvent(VK_F14, true) == 1 && !oemKeyboardConsumeBusy(), "recording transition still pairs a previously consumed release");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_LWIN); ++g_oemShortcutEdgeEpoch;
        oemKeyboardConsumeMaintain(GetTickCount64());
        check(fixtureReplayed.size() == 1 && keyEvent(VK_LWIN, true) == 0, "source/rule cancellation replays an incomplete prefix without inventing an OEM release");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_F14); ++g_gpdBackIfaceGen; g_gpdBackIfaceReady = false;
        check(keyEvent(VK_F14, true) == 1 && fixtureReplayed.empty(), "GPD interface revocation preserves the owned UP of an accepted press");
        check(keyEvent(VK_F14) == 0, "GPD revoked interface cannot start another consumed press");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_F14); g_oemHooksWanted = false; g_oemKeyHooksActive = false;
        check(oemKeyboardConsumeBusy() && keyEvent('A') == 0 && keyEvent(VK_F14, true) == 1 && !oemKeyboardConsumeBusy(),
            "disable retains only hidden-pair ownership while ordinary typing passes");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_LWIN); fixturePostOk = false; keyEvent(VK_F14);
        check(fixtureReplayed.size() == 2 && g_oemConsumeMatched == 0,
            "failed OEM receipt cannot consume the original full chord");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        fixtureSendLimit = 0; keyEvent(VK_LCONTROL); keyEvent('C');
        check(g_oemKeyboardConsume.replayFault && g_oemKeyboardConsume.replayCount == 2 && fixtureReplayed.empty(),
            "SendInput refusal keeps replay debt and blocks new suppression");
        check(keyEvent('A') == 0 && keyEvent(VK_LCONTROL, true) == 1,
            "replay refusal leaves unrelated typing alone and retains withheld modifier UP");
        const UINT debtCount = g_oemKeyboardConsume.replayCount;
        fixtureSendLimit = 1; oemKeyboardConsumeMaintain(g_oemKeyboardConsume.retryAt);
        check(fixtureReplayed.size() == 1 && g_oemKeyboardConsume.replayCount == debtCount - 1,
            "partial SendInput advances only the accepted replay prefix");
        fixtureSendLimit = UINT_MAX; oemKeyboardConsumeMaintain(g_oemKeyboardConsume.retryAt);
        check(g_oemKeyboardConsume.replayCount == 0 && fixtureReplayed.size() == debtCount &&
              fixtureReplayed.back().ki.wVk == VK_LCONTROL && (fixtureReplayed.back().ki.dwFlags & KEYEVENTF_KEYUP),
            "replay recovery finishes ordered modifier UP without dropping debt");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        check(keyEvent(VK_F14, false, LLKHF_INJECTED) == 0 && g_oemKeyMask == 0 && fixtureReplayed.empty(),
            "own replay/other injected input is never remapped or intercepted");
        check(keyEvent(VK_F14, false, LLKHF_LOWER_IL_INJECTED) == 0 && g_oemKeyMask == 0,
            "lower integrity injected flag alone cannot enter OEM mapping/consumption");
        for (const auto* code : {"l4", "r4"}) {
            auto mapping = rearRule; mapping["inputs"][0]["code"] = code;
            resetRoute(json::array({mapping})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::GpdLegacy); tick(0, 1000);
            const bool left = std::string(code) == "l4"; const DWORD functionKey = left ? VK_F11 : VK_F12, letter = left ? 'L' : 'R';
            check(keyEvent(functionKey) == 1 && keyEvent(letter) == 1 && keyEvent(letter, true) == 1 && keyEvent(functionKey, true) == 1,
                std::string("GPD legacy full chord has paired keyboard consumption: ") + code);
            deliverPhysicalReceipts(); flushJobs();
            check(fixtureReplayed.empty() && fixtureOwnActions.size() == 1,
                std::string("GPD legacy canonical/action path still fires once: ") + code);
        }
        auto rogRear = rule("os.openTouchKeyboard", "unused", "press", "oem", "m1"); rogRear["params"] = json::object();
        resetRoute(json::array({rogRear})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::Rog); tick(0, 1000);
        check(keyEvent(VK_F18) == 0 && keyEvent(VK_F18, true) == 0 && fixtureReplayed.empty(),
            "ROG identity-qualified Raw Input owner is never replaced by global keyboard suppression");
        // R11: Raw Input is an independent owner. Exercise the real evaluator's
        // maintenance before rear consumption; a hook being absent is normal on ROG.
        struct RawRearCase { const char* name; uint32_t keys; unsigned rear; uint64_t oem; };
        const RawRearCase rawCases[]={{"M1",kVkF18,2,kOemBitM1},{"M2",kVkF17,1,kOemBitM2},
            {"both",kVkF18|kVkF17,3,kOemBitM1|kOemBitM2}};
        for (const auto& rawCase : rawCases) {
            resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Rog);
            g_oemHooksWanted=false;g_oemBackHook=g_oemMouseHook=nullptr;
            g_oemKbDownRaw=rawCase.keys;oemBackHookRecompute();
            check(oemLiveBackBits()==rawCase.rear && g_oemKeyMask==rawCase.oem,
                std::string("ROG raw rear is published before maintenance: ")+rawCase.name);
            nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick,false);
            check(oemLiveBackBits()==rawCase.rear && g_oemKeyMask==rawCase.oem && g_oemKbDownRaw==rawCase.keys,
                std::string("ROG raw rear survives real background evaluator maintenance: ")+rawCase.name);
            nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+50);
            check(oemLiveBackBits()==rawCase.rear && fixtureHookInstalls==0 && fixtureReplayed.empty(),
                std::string("ROG held raw rear survives repeat maintenance without LL interception: ")+rawCase.name);
            g_oemBackHook=reinterpret_cast<HHOOK>(1);g_oemMouseHook=reinterpret_cast<HHOOK>(2);
            g_oemKbDown=kVkF14;g_oemMouseLeft=g_oemMouseX2=true;
            oemBackMappingReconcileOnUi();
            check(oemLiveBackBits()==rawCase.rear && g_oemKeyMask==rawCase.oem && g_oemKbDown==0 &&
                  !g_oemMouseLeft && !g_oemMouseX2 && fixtureHookRemoves==2,
                std::string("removing old LL hooks withdraws only their own state, preserving raw rear: ")+rawCase.name);
            oemRawPathReset("r11-device-removal");nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+100);
            check(oemLiveBackBits()==0 && g_oemKeyMask==0 && g_oemKbDownRaw==0,
                std::string("raw owner's own device-removal reset releases rear after maintenance: ")+rawCase.name);
            g_oemKbDownRaw=rawCase.keys;oemBackHookRecompute();nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+150);
            check(oemLiveBackBits()==rawCase.rear && g_oemKeyMask==rawCase.oem,
                std::string("new qualified raw event recovers after withdrawal without a latch: ")+rawCase.name);
            g_oemKbDownRaw=0;oemBackHookRecompute();nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+200);
            check(oemLiveBackBits()==0 && g_oemKeyMask==0,
                std::string("raw hardware UP stays released through maintenance: ")+rawCase.name);
        }
        // Win5's fixed F14/F15 base key remains valid with any subset of these
        // standard modifiers; real LL events and the same evaluator run here.
        const DWORD win5Modifiers[]={VK_LCONTROL,VK_LSHIFT,VK_LMENU,VK_LWIN};
        for(unsigned prefix=0;prefix<16;++prefix)for(const bool left:{false,true}) {
            resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::GpdWin5);
            for(unsigned m=0;m<4;++m)if(prefix&(1u<<m))keyEvent(win5Modifiers[m]);
            const DWORD baseKey=left?VK_F14:VK_F15;keyEvent(baseKey);
            nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick,false);
            check(oemLiveBackBits()==(left?2:1) && fixtureReplayed.empty(),
                "Win5 real hook rear survives maintenance with modifier prefix: "+std::to_string(prefix)+(left?":L4":":R4"));
            keyEvent(baseKey,true);nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+50,false);
            check(oemLiveBackBits()==0,
                "Win5 base UP releases rear while modifier prefix is still held: "+std::to_string(prefix)+(left?":L4":":R4"));
            for(unsigned m=0;m<4;++m)if(prefix&(1u<<m))keyEvent(win5Modifiers[m],true);
        }
        // An LL replay debt can postpone unhooking but may not own raw publication.
        resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Rog);
        g_oemHooksWanted=false;g_oemKbDownRaw=kVkF18;oemBackHookRecompute();
        g_oemKeyboardConsume.visibility[VK_F14]=3;
        oemBackMappingReconcileOnUi();
        check(oemLiveBackBits()==2 && g_oemKeyMask==kOemBitM1 && fixtureHookRemoves==0,
            "pending consumed LL UP keeps its hook but cannot clear independent ROG raw rear");
        g_oemKeyboardConsume.visibility[VK_F14]=0;oemBackMappingReconcileOnUi();
        check(oemLiveBackBits()==2 && fixtureHookRemoves==2,
            "finishing consumed LL UP releases hooks while raw rear remains live");
        // Hook installation/failure owns only its channel, including a delayed
        // desired-state transition that overlaps a still-qualified raw snapshot.
        for (const bool installOk : {false,true}) {
            resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Rog);
            g_oemBackHook=g_oemMouseHook=nullptr;g_oemHooksWanted=true;fixtureHookInstallOk=installOk;
            g_oemKbDownRaw=kVkF17;oemBackHookRecompute();oemBackMappingReconcileOnUi();
            check(oemLiveBackBits()==1 && g_oemKeyMask==kOemBitM2,
                std::string("hook install result does not erase raw publication: ")+(installOk?"success":"failure"));
        }
        // Every non-ROG chord profile uses the same maintenance. Preserve its
        // real held state and ensure disabling it still withdraws LL/mouse input.
#ifdef YMCC_OEM_MOUSE_CONSUME_SOURCE
        int mouseRows=0;
        auto mouseEvent=[&](unsigned slot,bool up=false,DWORD flags=0) {
            MSLLHOOKSTRUCT event{};event.pt={500,300};event.flags=flags;
            if(slot)event.mouseData=static_cast<DWORD>(XBUTTON2)<<16;
            return oemBackMouseHookProc(HC_ACTION,slot?(up?WM_XBUTTONUP:WM_XBUTTONDOWN):(up?WM_LBUTTONUP:WM_LBUTTONDOWN),reinterpret_cast<LPARAM>(&event));
        };
        for(int profile=0;profile<static_cast<int>(OemChordProfileId::None);++profile) {
            const auto rules=oemChordProfileFor(profile);
            for(int row=0;row<rules.count;++row) {
                if(!rules.rules[row].mouseChord)continue;
                ++mouseRows;
                auto own=rule("os.openTouchKeyboard","unused","press","oem","guide");own["params"]=json::object();
                for(unsigned first:{0u,1u}) {
                    resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);
                    check(mouseEvent(first)==1&&mouseEvent(1-first)==1,"R15 configured mouse chord DOWNs consumed in either order");
                    deliverPhysicalReceipts();flushJobs();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
                    check(fixtureOwnActions.size()==1&&fixtureReplayed.empty(),"R15 mouse producer executes exactly once with no original-button replay");
                    check(mouseEvent(1-first,true)==1&&mouseEvent(first,true)==1,"R15 both original mouse UPs consumed");
                    deliverPhysicalReceipts();flushJobs();
                    check(!oemKeyboardConsumeBusy()&&g_oemKeyMask==0&&fixtureOwnActions.size()==1,"R15 mouse release leaves zero ledger/source debt");
                }
                resetRoute(json::array());g_oemBackProfileId=profile;tick(0,1000);
                check(mouseEvent(0)==0&&mouseEvent(1)==0&&mouseEvent(1,true)==0&&mouseEvent(0,true)==0,"R15 unconfigured same mouse chord passes intact");
                check(fixtureReplayed.empty()&&fixtureOwnActions.empty(),"R15 unconfigured mouse chord has no invented action/replay");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);
                check(mouseEvent(0)==1&&mouseEvent(0,true)==1,"R15 lone candidate mouse click is buffered then repaid");
                check(fixtureReplayed.size()==2&&(fixtureReplayed[0].mi.dwFlags&MOUSEEVENTF_LEFTDOWN)&&(fixtureReplayed[1].mi.dwFlags&MOUSEEVENTF_LEFTUP)&&fixtureOwnActions.empty(),"R15 ordinary click replay preserves ordered DOWN/UP and actual position");
                check(fixtureReplayed[0].mi.dx>0&&fixtureReplayed[0].mi.dy>0&&!oemKeyboardConsumeBusy(),"R15 mouse replay uses virtual-desktop absolute click coordinates");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);mouseEvent(0);fixtureTick+=40;oemMouseConsumeMaintain(fixtureTick);
                check(fixtureReplayed.size()==1&&mouseEvent(1)==0&&mouseEvent(1,true)==0&&mouseEvent(0,true)==0,"R15 expired prefix cannot retroactively half-consume an already visible button");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);
                check(mouseEvent(0,false,LLMHF_INJECTED)==0&&mouseEvent(1,false,LLMHF_LOWER_IL_INJECTED)==0&&!g_oemMouseLeft&&!g_oemMouseX2,"R15 injected replay does not recapture a synthetic OEM mouse chord");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);mouseEvent(0);mouseEvent(1);deliverPhysicalReceipts();flushJobs();
                g_oemHooksWanted=false;oemBackMappingReconcileOnUi();
                check(fixtureHookRemoves==0&&oemMouseConsumeBusy(),"R15 disabled hook retains ownership of both hidden mouse releases");
                check(mouseEvent(1,true)==1&&mouseEvent(0,true)==1,"R15 rule/source disable still consumes pending UP pairs");oemBackMappingReconcileOnUi();
                check(fixtureHookRemoves==2&&!oemMouseConsumeBusy(),"R15 paired mouse completion permits clean hook removal");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);mouseEvent(0);fixturePostOk=false;mouseEvent(1);
                check(fixtureReplayed.size()==2&&fixtureOwnActions.empty(),"R15 refused producer receipt repays original mouse DOWNs instead of claiming consumption");
                mouseEvent(1,true);mouseEvent(0,true);check(!oemMouseConsumeBusy(),"R15 receipt refusal leaves ordinary releases and no debt");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);mouseEvent(0);fixtureSendLimit=0;mouseEvent(0,true);
                check(g_oemMouseConsume.replayFault&&g_oemMouseConsume.replayCount==2&&oemMouseConsumeEligibleMask()==0,"R15 failed mouse replay disables new consumption without deleting hidden click debt");
                fixtureSendLimit=UINT_MAX;fixtureTick+=250;oemMouseConsumeMaintain(fixtureTick);
                check(fixtureReplayed.size()==2&&!oemMouseConsumeBusy()&&!g_oemMouseConsume.replayFault,"R15 replay retry resolves original DOWN/UP exactly once");
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);mouseEvent(0);fixtureSendLimit=1;mouseEvent(0,true);mouseEvent(1);
                check(g_oemMouseConsume.replayCount==2,"R15 new tracked edge stays behind partially injected old UP");fixtureSendLimit=UINT_MAX;fixtureTick+=250;oemMouseConsumeMaintain(fixtureTick);
                check(fixtureReplayed.size()==3&&(fixtureReplayed[1].mi.dwFlags&MOUSEEVENTF_LEFTUP)&&(fixtureReplayed[2].mi.dwFlags&MOUSEEVENTF_XDOWN),"R15 partial replay preserves cross-button causal order");
                mouseEvent(1,true);check(!oemMouseConsumeBusy(),"R15 partially repaid mouse stream returns to ordinary paired visibility");
            }
        }
        check(mouseRows==2,"R15 all two HC visible mouse-combination profiles checked");
        std::cout<<"R15 mouse-workload rows="<<mouseRows<<"\n";
#else
        // Execute the same physical hook workload on old production source.
        // Do not silently skip the regression when the mouse ledger is absent.
        for(int profile=0;profile<static_cast<int>(OemChordProfileId::None);++profile) {
            const auto rows=oemChordProfileFor(profile);
            for(int row=0;row<rows.count;++row) {
                if(!rows.rules[row].mouseChord)continue;
                auto own=rule("os.openTouchKeyboard","unused","press","oem","guide");own["params"]=json::object();
                resetRoute(json::array({own}));g_oemBackProfileId=profile;tick(0,1000);
                MSLLHOOKSTRUCT event{};event.pt={500,300};event.mouseData=static_cast<DWORD>(XBUTTON2)<<16;
                const auto leftDown=oemBackMouseHookProc(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&event));
                const auto xDown=oemBackMouseHookProc(HC_ACTION,WM_XBUTTONDOWN,reinterpret_cast<LPARAM>(&event));
                deliverPhysicalReceipts();flushJobs();
                check(fixtureOwnActions.size()==1,"R15 old-source control still executes configured mouse OEM action");
                check(leftDown==1&&xDown==1,"R15 old-source control must suppress original mouse DOWNs");
                const auto xUp=oemBackMouseHookProc(HC_ACTION,WM_XBUTTONUP,reinterpret_cast<LPARAM>(&event));
                const auto leftUp=oemBackMouseHookProc(HC_ACTION,WM_LBUTTONUP,reinterpret_cast<LPARAM>(&event));
                check(xUp==1&&leftUp==1,"R15 old-source control must suppress original mouse UPs");
            }
        }
#endif

        // R14: every declared keyboard route must prove event consumption,
        // configured action and paired release, not merely publish a canonical bit.
        const char* allOemCodes[] = {"a","b","x","y","lb","rb","lt","rt","back","start","leftThumb","rightThumb","dpadUp","dpadDown","dpadLeft","dpadRight","m1","m2","cc","ac","lib","claw","qs","ckbig","cksmall","cktl","cktr","ayat","r4","l4","gamepad","keyboard","home","legionr","legionl","special","turbo","special2","homelp","homekb","hometb","guide","lcc","zotac","dots","screen","win","esc","orange","function","orangekb","orangetb","orangelp","legiony2","legiony3","legionm2","legionm3","legionfrontl","legionfrontr"};
        const std::pair<uint32_t,DWORD> allPhysicalKeys[] = {
            {kVkCtrl,VK_LCONTROL},{kVkShift,VK_LSHIFT},{kVkAlt,VK_LMENU},{kVkWin,VK_LWIN},
            {kVkF3,VK_F3},{kVkF10,VK_F10},{kVkF11,VK_F11},{kVkF12,VK_F12},
            {kVkF13,VK_F13},{kVkF14,VK_F14},{kVkF15,VK_F15},{kVkF16,VK_F16},
            {kVkF17,VK_F17},{kVkF18,VK_F18},{kVkF21,VK_F21},{kVkF22,VK_F22},
            {kVkF23,VK_F23},{kVkF24,VK_F24},{kVkL,'L'},{kVkR,'R'},
            {kVkO,'O'},{kVkG,'G'},{kVkT,'T'},{kVkD,'D'},
            {kVkEsc,VK_ESCAPE},{kVkDel,VK_DELETE},{kVkSnap,VK_SNAPSHOT},{kVkTab,VK_TAB}};
        int r14Profiles=0,r14Rows=0;
        for(int profile=0;profile<static_cast<int>(OemChordProfileId::None);++profile){
            if(profile==static_cast<int>(OemChordProfileId::Rog))continue;
            bool visited=false;
            for(int row=0;row<kOemProfiles[profile].count;++row){
                const auto& chord=kOemProfiles[profile].rules[row];
                if(chord.mouseChord || !chord.keys || !chord.bit)continue;
                const char* code=nullptr;
                for(const char* candidate:allOemCodes){uint64_t bit=0;if(nativeKeyboardShortcutMaskBit(candidate,bit) && bit==chord.bit){code=candidate;break;}}
                const std::string label=std::to_string(profile)+":"+std::to_string(row)+":"+(code?code:"UNBOUND");
                check(code!=nullptr,"R14 keyboard chord has configured rule code: "+label);if(!code)continue;
                ++r14Rows;visited=true;
                auto ownRule=rule("os.openTouchKeyboard","unused","press","oem",code);ownRule["params"]=json::object();
                resetRoute(json::array({ownRule}));g_oemBackProfileId=profile;tick(0,1000);
                std::vector<DWORD> pressed;uint32_t covered=0;bool downsConsumed=true,upsConsumed=true;
                for(auto [bit,vk]:allPhysicalKeys)if(chord.keys&bit){covered|=bit;pressed.push_back(vk);downsConsumed=keyEvent(vk)==1 && downsConsumed;}
                check(covered==chord.keys,"R14 complete physical-key workload bound: "+label);
                check((g_oemKeyMask.load()&chord.bit)!=0,"R14 exact production canonical observed: "+label);
                deliverPhysicalReceipts();flushJobs();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
                check(downsConsumed && fixtureReplayed.empty(),"R14 configured chord DOWNs consumed without replay: "+label);
                check(fixtureOwnActions.size()==1,"R14 configured keyboard action executes once: "+label);
                for(auto it=pressed.rbegin();it!=pressed.rend();++it)upsConsumed=keyEvent(*it,true)==1 && upsConsumed;
                deliverPhysicalReceipts();flushJobs();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
                check(upsConsumed && !oemKeyboardConsumeBusy() && fixtureReplayed.empty(),"R14 paired UPs consumed with zero debt: "+label);
                check(g_oemKeyMask.load()==0 && oemLiveBackBits()==0 && fixtureOwnActions.size()==1,"R14 release clears publication without second action: "+label);
                // The same hardware chord must pass intact when no action owns it.
                resetRoute(json::array());g_oemBackProfileId=profile;tick(0,1000);
                bool unconfiguredPass=true;
                for(DWORD vk:pressed)unconfiguredPass=keyEvent(vk)==0 && unconfiguredPass;
                for(auto it=pressed.rbegin();it!=pressed.rend();++it)unconfiguredPass=keyEvent(*it,true)==0 && unconfiguredPass;
                deliverPhysicalReceipts();flushJobs();
                check(unconfiguredPass && fixtureReplayed.empty() && fixtureOwnActions.empty(),"R14 unconfigured same hardware chord passes intact: "+label);
                // A modifier prefix followed by unrelated typing must be repaid,
                // never swallowed as the OEM key and never run its mapped action.
                resetRoute(json::array({ownRule}));g_oemBackProfileId=profile;tick(0,1000);
                std::vector<DWORD> modifiers;
                for(auto [bit,vk]:allPhysicalKeys)if((chord.keys&bit) && (bit&(kVkCtrl|kVkShift|kVkAlt|kVkWin)))modifiers.push_back(vk);
                // Single-modifier OEM keys are complete inputs, not incomplete prefixes.
                const bool prefixOnly=!modifiers.empty() && ((chord.keys & ~(kVkCtrl|kVkShift|kVkAlt|kVkWin))!=0);
                if(prefixOnly){
                    for(DWORD vk:modifiers)keyEvent(vk);
                    keyEvent('Q');
                    deliverPhysicalReceipts();flushJobs();fixturePhysicalReceipts.clear();fixtureReceiptTimes.clear();
                    check(fixtureOwnActions.empty() && fixtureReplayed.size()==modifiers.size()+1,"R14 unrelated typing repays ordered candidate prefix: "+label);
                    bool releasesPass=keyEvent('Q',true)==0;
                    for(auto it=modifiers.rbegin();it!=modifiers.rend();++it)releasesPass=keyEvent(*it,true)==0 && releasesPass;
                    check(releasesPass && !oemKeyboardConsumeBusy(),"R14 repaid prefix has ordinary paired release: "+label);
                }

            }
            if(visited)++r14Profiles;
        }
        std::cout<<"R14 keyboard-workload profiles="<<r14Profiles<<" rows="<<r14Rows<<"\n";
        int r11Profiles=0,r11Rows=0;
        for (int profile=0;profile<static_cast<int>(OemChordProfileId::None);++profile) {
            if(profile==static_cast<int>(OemChordProfileId::Rog))continue;
            bool visited=false;
            for(int row=0;row<kOemProfiles[profile].count;++row) {
                const auto& chord=kOemProfiles[profile].rules[row];if(!chord.bit || (!chord.keys&&!chord.mouseChord))continue;
                visited=true;++r11Rows;resetRoute(json::array());g_oemBackProfileId=profile;
                g_oemKbDown=chord.keys;g_oemMouseLeft=g_oemMouseX2=chord.mouseChord;oemBackHookRecompute();
                const uint64_t expectedMask=g_oemKeyMask.load();const auto expectedRear=oemLiveBackBits();
                nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick,false);
                check(expectedMask!=0 && g_oemKeyMask==expectedMask && oemLiveBackBits()==expectedRear,
                    "non-ROG real evaluator preserves held hook chord: "+std::to_string(profile)+":"+std::to_string(row));
                g_oemRearMapToL5=expectedMask;nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+10,false);
                check((oemLiveBackBits()&1)!=0,
                    "every observed OEM chord can retain explicit left rear mapping through maintenance: "+std::to_string(profile)+":"+std::to_string(row));
                g_oemRearMapToL5=0;g_oemRearMapToR5=expectedMask;nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+20,false);
                check((oemLiveBackBits()&2)!=0,
                    "every observed OEM chord can retain explicit right rear mapping through maintenance: "+std::to_string(profile)+":"+std::to_string(row));
                oemBackMappingSetEnabled(false);nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+50,false);
                check(g_oemKeyMask==0 && oemLiveBackBits()==0 && g_oemKbDown==0 && g_oemKbDownRaw==0,
                    "non-ROG hook disable still withdraws its chord/rear: "+std::to_string(profile)+":"+std::to_string(row));
            }
            if(visited)++r11Profiles;
        }
        std::cout<<"R11_SHARED_MAINTENANCE profiles="<<r11Profiles<<" rows="<<r11Rows<<"\n";
        auto msiRear = rogRear;
        resetRoute(json::array({msiRear})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::Msi); tick(0, 1000);
        fixtureMsiAuthority = static_cast<int>(kOemBitM1);
        check(oemKeyboardConsumeEligibleMask() == 0, "authoritative MSI DInput rejects keyboard rear consumption fallback");
        auto combo = rearRule; combo["inputs"].push_back({{"source","controller"},{"code","a"}});
        resetRoute(json::array({combo})); tick(0, 1000);
        check(g_oemConsumePolicyMask == 0 && keyEvent(VK_F14) == 0, "mixed controller/OEM rules do not grant keyboard consumption");
        resetRoute(json::array({rearRule})); keyEvent(VK_F14); tick(0, 1000);
        check(g_oemConsumePolicyMask == 0 && keyEvent(VK_F14, true) == 0,
            "pre-held physical chord waits for neutral and keeps original DOWN/UP ownership");
        // Side-specific tracking keeps Ctrl active until both sides release.
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_LCONTROL); keyEvent(VK_RCONTROL, false, LLKHF_EXTENDED); keyEvent(VK_LCONTROL, true);
        check((g_oemKbDown & kVkCtrl) != 0, "left modifier release never clears the still-held right modifier");
        keyEvent(VK_RCONTROL, true, LLKHF_EXTENDED);
        check((g_oemKbDown & kVkCtrl) == 0, "modifier aggregate clears after both physical sides release");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_F14); oemBackMappingSetEnabled(false);
        check(!g_oemKeyHooksActive && g_oemKeyMask == 0 && g_oemBackStateKb == 0 && fixtureHookRemoves == 0,
            "real disable immediately withdraws input while retaining hidden pair hook");
        check(keyEvent('A') == 0 && keyEvent(VK_F14, true) == 1, "real disabled hook passes ordinary input and owns only paired OEM release");
        oemBackMappingReconcileOnUi();
        check(fixtureHookRemoves == 2 && g_oemBackHook == nullptr && g_oemMouseHook == nullptr,
            "real UI close unhooks both sources after pair/replay debt is empty");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        keyEvent(VK_LWIN); fixtureSendLimit = 0; oemBackMappingSetEnabled(false);
        check(g_oemKeyboardConsume.replayCount == 1 && fixtureHookRemoves == 0 && !g_oemKeyHooksActive,
            "real close never discards unmatched prefix when SendInput is refused");
        fixtureSendLimit = UINT_MAX; oemKeyboardConsumeMaintain(g_oemKeyboardConsume.retryAt); oemBackMappingReconcileOnUi();
        check(fixtureHookRemoves == 2 && fixtureReplayed.size() == 1,
            "real close can finish after replay acceptance without changing device state");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        fixtureThreadId = 200; oemBackMappingSetEnabled(false);
        check(fixtureReconcilePosts == 1 && fixtureHookRemoves == 0 && !g_oemHooksWanted,
            "foreign thread records latest desired hook state and marshals close to UI");
        fixtureThreadId = 100; oemBackMappingReconcileOnUi();
        check(fixtureHookRemoves == 2, "UI alone performs deferred hook removal");
        fixtureThreadId = 200; oemBackMappingSetEnabled(true);
        check(fixtureReconcilePosts == 2 && fixtureHookInstalls == 0, "foreign thread cannot install a hook without its UI message pump");
        fixtureThreadId = 100; fixtureDesktopDown = {VK_F14}; oemBackMappingReconcileOnUi();
        check(fixtureHookInstalls == 2 && g_oemKeyboardHookOwnerTid == 100 && g_oemKeyHooksActive,
            "real hook creation is owned by current UI window thread");
        tick(0, 1100);
        check(g_oemConsumePolicyMask == 0 && keyEvent(VK_F14) == 0 && keyEvent(VK_F14, true) == 0,
            "installation seeds already-passed held keys so repeats cannot steal their DOWN/UP");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        oemBackMappingSetEnabled(false); fixturePostOk = false; fixtureThreadId = 200; oemBackMappingSetEnabled(true);
        check(fixtureHookInstalls == 0 && g_oemShortcutEdgePostFailures == 1,
            "hook-control post failure keeps desired state without installing on wrong thread");
        fixtureThreadId = 100; tick(0, 1200);
        check(fixtureHookInstalls == 2 && g_oemKeyHooksActive, "existing UI evaluation repairs missed hook-control delivery");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        oemBackMappingSetEnabled(false); fixtureHookInstallOk = false; oemBackMappingSetEnabled(true);
        const int failedAttempts = fixtureHookInstalls; oemBackMappingReconcileOnUi();
        check(failedAttempts == 2 && fixtureHookInstalls == failedAttempts && !g_oemKeyHooksActive,
            "hook failure has bounded existing-tick retry and grants no input capture");
        fixtureHookInstallOk = true; g_oemHookNextInstallAttempt = 0; oemBackMappingReconcileOnUi();
        check(g_oemKeyHooksActive && fixtureHookInstalls == 4, "later UI retry can recover failed hook installation without new threads");
        g_oemMouseHook = nullptr; g_oemHookNextInstallAttempt = 0; oemBackMappingReconcileOnUi();
        check(fixtureHookInstalls == 5, "partial installation retries only the missing hook instead of leaking duplicates");
        const char* canonicalCodes[] = {"m1","m2","cc","ac","lib","claw","qs","ckbig","cksmall","cktl","cktr","ayat","r4","l4","gamepad","keyboard","home","legionr","legionl","special","turbo","special2","homelp","homekb","hometb","guide","lcc","zotac","dots","screen","win","esc","orange","function","orangekb","orangetb","orangelp"};
        int consumedProfileRows = 0, consumedProfiles = 0;
        for (int profile = 0; profile < static_cast<int>(OemChordProfileId::None); ++profile) {
            if (profile == static_cast<int>(OemChordProfileId::Rog)) continue;
            int inProfile = 0;
            for (int row = 0; row < kOemProfiles[profile].count; ++row) {
                const auto& chord = kOemProfiles[profile].rules[row];
                if (chord.mouseChord || !chord.keys || !chord.bit) continue;
                std::string code;
                for (const auto* candidateCode : canonicalCodes) {
                    uint64_t candidateBit = 0;
                    if (nativeKeyboardShortcutMaskBit(candidateCode, candidateBit) && candidateBit == chord.bit) { code = candidateCode; break; }
                }
                check(!code.empty(), "profile row canonical bound: " + std::to_string(profile) + "/" + std::to_string(row));
                auto mapping = rearRule; mapping["inputs"][0]["code"] = code;
                resetRoute(json::array({mapping})); g_oemBackProfileId = profile; tick(0, 1000);
                std::vector<DWORD> chordKeys;
                for (DWORD vk = 1; vk < 256; ++vk) {
                    if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_RSHIFT || vk == VK_RWIN) continue;
                    const uint32_t bit = oemBackHookVkBit(vk);
                    if (bit && (chord.keys & bit)) chordKeys.push_back(vk);
                }
                const auto keyOrder = [](DWORD vk) {
                    const auto bit = oemBackHookVkBit(vk);
                    return bit == kVkCtrl ? 0 : bit == kVkShift ? 1 : bit == kVkAlt ? 2 : bit == kVkWin ? 3 : 4;
                };
                std::stable_sort(chordKeys.begin(), chordKeys.end(), [&](DWORD a, DWORD b) { return keyOrder(a) < keyOrder(b); });
                bool paired = true;
                for (DWORD vk : chordKeys) paired = (keyEvent(vk) == 1) && paired;
                for (auto i = chordKeys.rbegin(); i != chordKeys.rend(); ++i) paired = (keyEvent(*i, true) == 1) && paired;
                deliverPhysicalReceipts(); flushJobs();
                check(paired && fixtureReplayed.empty() && fixtureOwnActions.size() == 1 && !oemKeyboardConsumeBusy(),
                    "real profile hook/action has complete configured paired consumption: " + std::to_string(profile) + "/" + code);
                ++consumedProfileRows; ++inProfile;
            }
            if (inProfile) ++consumedProfiles;
        }
        std::cout << "configured keyboard profile coverage: profiles=" << consumedProfiles << " rows=" << consumedProfileRows << '\n';
        auto screenAlias = rearRule; screenAlias["inputs"][0]["code"] = "screen";
        resetRoute(json::array({screenAlias})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::AyaneoFlip1S); tick(0, 1000);
        keyEvent(VK_F18); keyEvent(VK_LCONTROL); keyEvent(VK_LWIN);
        keyEvent(VK_LWIN, true); keyEvent(VK_LCONTROL, true); keyEvent(VK_F18, true);
        deliverPhysicalReceipts(); flushJobs();
        check(fixtureOwnActions.size() == 1 && g_oemKeyMask == 0 && !oemKeyboardConsumeBusy(),
            "bare/prefixed AYA Screen alias has one press when the base arrives before its modifiers");
        screenAlias["trigger"] = "release";
        resetRoute(json::array({screenAlias})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::AyaneoFlip1S); tick(0, 1000);
        keyEvent(VK_F18); keyEvent(VK_LCONTROL); keyEvent(VK_LWIN); deliverPhysicalReceipts(); flushJobs();
        check(fixtureOwnActions.empty(), "later modifier DOWN cannot invent a release of an accepted Screen stroke");
        keyEvent(VK_F18, true); deliverPhysicalReceipts(); flushJobs();
        check(fixtureOwnActions.size() == 1, "accepted Screen stroke releases when its original member actually releases");
        resetRoute(json::array({rearRule})); g_oemBackProfileId = static_cast<int>(OemChordProfileId::GpdLegacy); tick(0, 1000);
        keyEvent(VK_F11); keyEvent('L'); keyEvent(VK_LCONTROL);
        check(g_oemKeyMask == kOemBitL4 && g_oemBackStateKb == 2, "existing subset mapper keeps canonical and swapped rear output coherent across later modifier DOWN");
        keyEvent('L', true); keyEvent(VK_F11, true); keyEvent(VK_LCONTROL, true);
        check(g_oemKeyMask == 0 && g_oemBackStateKb == 0, "rear member release clears both output domains");
        resetRoute(json::array({rearRule})); tick(0, 1000);
        g_oemShortcutEdgeEpoch = 0;
        check(nativeYmccShortcutSubmit([]{ fixtureOwnActions.push_back("epoch-zero"); }, SerialJobKind::YmccOwn, 0),
            "zero wrapped epoch is an explicitly bound queue token");
        ++g_oemShortcutEdgeEpoch; flushJobs();
        check(fixtureOwnActions.empty(), "zero wrapped epoch cannot bypass queued-job cancellation guard");
#endif

#if defined(YMCC_WMI_CLICK_SOURCE)
        auto clawClick = rule("os.openTouchKeyboard", "unused", "press", "oem", "claw"); clawClick["params"] = json::object();
        auto qsClick = clawClick; qsClick["inputs"][0]["code"] = "qs";
        resetRoute(json::array({clawClick,qsClick})); tick(0,10000);
        uint64_t wmiGen = activateWmi();
        auto* sink = new MsiWmiSink(wmiGen);
        void* sinkIface = nullptr;
        check(sink->QueryInterface(IID_IWbemObjectSink,&sinkIface) == S_OK && sinkIface == sink,
            "real WMI sink QueryInterface publishes its COM identity");
        check(static_cast<IWbemObjectSink*>(sinkIface)->Release() == 1 && sink->AddRef() == 2 && sink->Release() == 1,
            "real WMI sink owns ordinary balanced references");
        FixtureWmiObject clawObject; clawObject.code = 0x129; // low byte 41
        FixtureWmiObject qsObject; qsObject.code = 88;
        IWbemClassObject* wmiObjects[] = {&clawObject,&qsObject,&clawObject};
        sink->Indicate(3,wmiObjects);
        check(g_msiWmiClickCount == 3 && fixtureWmiWakePosts == 3,
            "real WMI sink retains every separate CLAW/QS click");
        fixtureTick += 1000; msiWmiPump(); // UI did not sample the 80ms pulse
        check(g_oemKeyMaskMsi == 0 && g_msiWmiClickCount == 3,
            "WMI display pulse expiry cannot expire undelivered click receipts");
        tick(0,fixtureTick); flushJobs();
        check(fixtureOwnActions.size() == 3 && fixtureLegacyClaw == 0,
            "three WMI clicks between UI samples execute three configured actions without legacy CLAW toggle");
        tick(0,fixtureTick+100); nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == 3 && g_msiWmiClickCount == 0,
            "ordinary evaluator and redundant UI wake cannot duplicate WMI clicks");
        check(clickStatus("accepted") && clickStatus("executed"),
            "WMI action acceptance and actual queued action call have explicit receipts");
        check(sink->Release() == 0, "WMI sink releases its final local reference");

        resetRoute(json::array({qsClick})); tick(0,10000); wmiGen = activateWmi(); fixturePostOk = false;
        msiWmiHandleEvent(88,wmiGen); fixtureTick += 1000; msiWmiPump();
        check(g_msiWmiClickCount == 1 && fixturePhysicalReceipts.empty(),
            "failed WMI wake post retains click without fabricating keyboard input");
        tick(0,fixtureTick); flushJobs();
        check(fixtureOwnActions.size() == 1, "existing UI evaluation delivers a retained WMI click after post failure");

        resetRoute(json::array()); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); msiWmiHandleEvent(88,wmiGen); nativeMsiWmiShortcutDrain();
        check(fixtureLegacyClaw == 1 && fixtureJobs.empty() && clickStatus("unconsumed","no-configured-rule"),
            "unconfigured WMI clicks report unconsumed and preserve only original CLAW default");
        const auto countBeforeUnknown = g_msiWmiClickCount;
        msiWmiHandleEvent(15,wmiGen); msiWmiHandleEvent(16,wmiGen); msiWmiHandleEvent(166,wmiGen);
        check(g_msiWmiClickCount == countBeforeUnknown && g_oemKeyMaskMsiDinput == 0,
            "WMI unknown codes cannot become MSI M1/M2 or ROG events");

        for (const auto* trigger : {"hold","repeat","release"}) {
            auto unsupported = clawClick; unsupported["trigger"] = trigger;
            resetRoute(json::array({unsupported})); tick(0,10000); wmiGen = activateWmi();
            msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain(); flushJobs();
            check(fixtureOwnActions.empty() && fixtureLegacyClaw == 0 &&
                clickStatus("unconsumed","click-only-trigger-or-composite-unsupported"),
                std::string("WMI click cannot fake configured physical ") + trigger);
        }
        auto mixedWmi = clawClick; mixedWmi["inputs"].push_back({{"source","oem"},{"code","qs"}});
        resetRoute(json::array({mixedWmi})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.empty() && fixtureLegacyClaw == 0 && clickStatus("unconsumed","click-only-trigger-or-composite-unsupported"), "one WMI click never invents a simultaneous second OEM click");

        auto doubleClaw = clawClick; doubleClaw["trigger"] = "double";
        resetRoute(json::array({doubleClaw})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); fixtureTick += 100; msiWmiHandleEvent(41,wmiGen);
        fixtureTick += 1500; nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == 1 && clickStatus("waiting-double"),
            "two identical WMI clicks merge only by configured double semantics using capture times");
        fixtureTick += 600; msiWmiHandleEvent(41,wmiGen); fixtureTick += 600; msiWmiHandleEvent(41,wmiGen);
        nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == 1, "WMI double interval is not widened by UI processing delay");
        ++g_oemShortcutEdgeEpoch; fixtureTick += 50; msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == 1, "new rule/recording epoch cannot inherit WMI double history");
        fixtureWmiTerminal = true; msiWmiSetEnabled(false); wmiGen = activateWmi();
        fixtureTick += 50; msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == 1, "new WMI subscription cannot inherit old subscription double history");

        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); ++g_oemShortcutEdgeEpoch; nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.empty() && clickStatus("cancelled"), "rule change rejects a captured older WMI click");
        for (bool exiting : {false,true}) {
            resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi(); msiWmiHandleEvent(41,wmiGen);
            if (exiting) g_exitRequested = true; else g_shortcutRecordingActive = true;
            nativeMsiWmiShortcutDrain(); flushJobs();
            check(fixtureOwnActions.empty() && fixtureLegacyClaw == 0 &&
                (exiting ? clickStatus("cancelled") && fixtureRecordedShortcutClicks.empty() :
                    fixtureRecordedShortcutClicks == std::vector<uint64_t>{kOemBitClaw}),
                exiting ? "exit cancels WMI click and legacy action" : "recording captures one WMI click without action or legacy toggle");
        }
        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi(); fixtureJobAccept = false;
        msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain();
        check(fixtureJobs.empty() && clickStatus("failed","action-dispatch-refused"), "WMI action queue rejection produces explicit failure");
        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain();
        check(fixtureJobs.size() == 1, "WMI queued action carries immutable source identity");
        fixtureWmiTerminal = true; msiWmiSetEnabled(false); const auto newerWmiGen = activateWmi(); flushJobs();
        check(newerWmiGen != wmiGen && fixtureOwnActions.empty() && clickStatus("cancelled","queued-subscription-ended"),
            "old WMI action job cannot execute under a re-enabled subscription");
        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen); nativeMsiWmiShortcutDrain(); ++g_oemShortcutEdgeEpoch; flushJobs();
        check(fixtureOwnActions.empty() && clickStatus("cancelled","queued-rule-cancel"), "WMI rule changes after dispatch reject queued action");

        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(41,wmiGen);
        auto* oldSink = new MsiWmiSink(wmiGen);
        msiWmiSetEnabled(false);
        check(g_msiWmiThread != nullptr && fixtureWmiCloses == 0 && fixtureLastWaitMs == 0 && g_msiWmiClickCount == 0,
            "stop withdraws receipts and retains nonterminal watcher handle with zero wait");
        msiWmiSetEnabled(true); g_msiWmiState = 0; msiWmiPump();
        check(fixtureWmiCreates == 1 && g_msiWmiActiveGeneration == 0 && g_msiWmiStop,
            "logical stopped state cannot start a second watcher while old thread is alive");
        oldSink->Indicate(1,wmiObjects);
        check(g_msiWmiClickCount == 0 && g_oemKeyMaskMsi == 0, "retired watcher callback cannot republish after disable");
        fixtureWmiTerminal = true; msiWmiPump(); const auto rebound = g_msiWmiActiveGeneration.load();
        check(fixtureWmiCreates == 2 && fixtureWmiCloses == 1 && rebound != wmiGen,
            "actual thread terminal signal permits one new watcher generation");
        oldSink->Indicate(1,wmiObjects); msiWmiHandleEvent(41,wmiGen);
        check(g_msiWmiClickCount == 0 && g_oemKeyMaskMsi == 0 && !g_msiWmiStop,
            "immutable old sink identity rejects callbacks even after new stop flag reset");
        oldSink->Release(); fixtureWmiTerminal = false;
        g_oemKeyMaskHid = kOemBitL4; msiWmiSetEnabled(false);
        check(g_oemKeyMaskHid == kOemBitL4, "WMI source cancellation cannot clear unrelated HID state");

        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen = activateWmi();
        auto* raceSink = new MsiWmiSink(wmiGen);
        clawObject.beforeGet = [&] { msiWmiSetEnabled(false); };
        raceSink->Indicate(1,wmiObjects); clawObject.beforeGet = nullptr;
        check(g_msiWmiClickCount == 0 && g_oemKeyMaskMsi == 0,
            "stop between sink admission and WMI property read is rechecked at receipt publication");
        raceSink->Release();

        resetRoute(json::array({clawClick,qsClick})); tick(0,10000); wmiGen = activateWmi();
        for (size_t i=0; i<kMsiWmiClickCapacity; ++i) msiWmiHandleEvent(i%2 ? 88 : 41,wmiGen);
        msiWmiHandleEvent(41,wmiGen);
        check(g_msiWmiClickCount == kMsiWmiClickCapacity && fixtureLogs.back().value("reason","") == "receipt-queue-full" && !fixtureLogs.back().value("accepted",true),
            "bounded WMI queue retains admitted clicks and explicitly refuses overflow");
        nativeMsiWmiShortcutDrain(); flushJobs();
        check(fixtureOwnActions.size() == kMsiWmiClickCapacity && g_msiWmiClickCount == 0,
            "bounded WMI drain delivers every admitted click exactly once");
        bool ordered = true; uint64_t previous = 0;
        for (const auto& e : fixtureLogs) if (e.value("event","") == "oem-shortcut-click" && e.value("status","") == "accepted") {
            const uint64_t seq=e.value("sequence",0ull); ordered = ordered && seq==previous+1; previous=seq;
        }
        check(ordered && previous==kMsiWmiClickCapacity, "WMI FIFO action receipts preserve capture admission sequence");

        resetRoute(json::array({qsClick})); tick(0,10000); wmiGen = activateWmi();
        msiWmiHandleEvent(88,wmiGen); fixtureTick+=70; msiWmiHandleEvent(88,wmiGen); fixtureTick+=20; msiWmiPump();
        check(g_oemKeyMaskMsi == kOemBitQs && g_msiWmiClickCount == 2,
            "serialized pulse expiry preserves a newer callback pulse and both click receipts");
        fixtureTick+=100; msiWmiPump();
        check(g_oemKeyMaskMsi == 0 && g_msiWmiClickCount == 2, "pulse expiry is independent of retained action queue");
        g_oemKeyMask = kOemBitQs; tick(0,fixtureTick); flushJobs();
        check(fixtureOwnActions.size() == 2 && !(g_oemConsumePolicyMask.load() & kOemBitQs),
            "subscribed WMI owns QS action without duplicated or silently consumed keyboard fallback");
        const uint64_t edgeToken = oemShortcutEdgeToken(g_oemShortcutEdgeEpoch,1);
        nativeKeyboardShortcutHandleOemEdge(kOemBitQs,edgeToken); flushJobs();
        check(fixtureOwnActions.size() == 2, "queued QS keyboard receipt cannot duplicate authoritative WMI click");
        fixtureWmiTerminal = true; msiWmiSetEnabled(false); tick(0,fixtureTick+10); flushJobs();
        check(fixtureOwnActions.size() == 2, "fallback authority change cannot steal already-held keyboard DOWN");
        g_oemKeyMask = 0; tick(0,fixtureTick+20); g_oemKeyMask = kOemBitQs; tick(0,fixtureTick+30); flushJobs();
        check(fixtureOwnActions.size() == 3, "unavailable WMI restores fresh QS keyboard fallback");
        resetRoute(json::array({qsClick})); g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi); tick(0,10000);
        check((oemKeyboardConsumeEligibleMask() & kOemBitQs) != 0, "unavailable WMI publishes configured QS keyboard consumption");
        wmiGen=activateWmi();
        check((oemKeyboardConsumeEligibleMask() & kOemBitQs) == 0,
            "newly confirmed WMI immediately withdraws QS hook consumption before next UI policy publish");
        resetRoute(json::array({qsClick})); tick(0,10000); g_oemKeyMask = kOemBitQs; tick(0,10100);
        wmiGen=activateWmi(); flushJobs();
        check(fixtureOwnActions.empty(), "WMI authority gained after keyboard action queue admission cancels old fallback job");
        auto qsRelease=qsClick; qsRelease["trigger"]="release";
        resetRoute(json::array({qsRelease})); tick(0,10000); g_oemKeyMask=kOemBitQs; tick(0,10100);
        activateWmi(); tick(0,10200); flushJobs();
        check(fixtureOwnActions.empty(), "WMI authority transfer cannot fabricate keyboard release action");

        resetRoute(json::array({clawClick})); tick(0,10000); fixtureWmiCreateOk=false; msiWmiSetEnabled(true);
        check(g_msiWmiThread==nullptr && g_msiWmiActiveGeneration==0 && g_msiWmiState==3,
            "watcher creation failure withdraws source identity");
        fixtureTick+=500; msiWmiPump();
        check(fixtureWmiCreates==1, "WMI subscription retry respects existing 60-second backoff");
        fixtureTick+=kMsiWmiRetryBackoffMs; fixtureWmiCreateOk=true; msiWmiPump();
        check(fixtureWmiCreates==2 && g_msiWmiThread!=nullptr, "expired backoff permits one existing watcher retry");

        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen=activateWmi();
        auto* completedSink=new MsiWmiSink(wmiGen); msiWmiHandleEvent(41,wmiGen);
        completedSink->SetStatus(WBEM_STATUS_PROGRESS,S_OK,nullptr,nullptr);
        check(g_msiWmiClickCount==1 && msiWmiGenerationCurrent(wmiGen), "WMI progress status does not terminate current subscription");
        completedSink->SetStatus(WBEM_STATUS_COMPLETE,E_FAIL,nullptr,nullptr);
        check(g_msiWmiActiveGeneration==0 && g_msiWmiState==3 && g_msiWmiClickCount==0 && g_oemKeyMaskMsi==0,
            "async WMI query completion withdraws its own source and undelivered receipts");
        msiWmiPump();
        check(fixtureWmiCreates==1 && g_msiWmiThread!=nullptr,
            "async completion cannot bypass watcher thread terminal proof");
        fixtureWmiTerminal=true; msiWmiPump();
        check(g_msiWmiThread==nullptr && fixtureWmiCreates==1,
            "failed async query retains original retry backoff after terminal reclamation");
        fixtureTick+=kMsiWmiRetryBackoffMs; msiWmiPump(); const auto afterAsyncFailure=g_msiWmiActiveGeneration.load();
        msiWmiHandleEvent(41,afterAsyncFailure);
        completedSink->SetStatus(WBEM_STATUS_COMPLETE,E_FAIL,nullptr,nullptr);
        check(msiWmiGenerationCurrent(afterAsyncFailure) && g_msiWmiClickCount==1,
            "late old WMI SetStatus cannot terminate newer query or clear its click");
        completedSink->Release();

        resetRoute(json::array({clawClick})); tick(0,10000); wmiGen=activateWmi();
        g_msiWmiPulseMask.store(0); g_msiWmiPulseUntil.store(0); g_oemKeyMaskMsi.store(kOemBitQs);
        msiWmiPump();
        check(g_msiWmiPulseMask==0 && g_oemKeyMaskMsi==kOemBitQs,
            "pulse expiry does not clear an output-only withdrawal transition");
        std::promise<void> wmiPumpEntered, wmiPumpFinished;
        auto wmiEntered=wmiPumpEntered.get_future(), wmiFinished=wmiPumpFinished.get_future();
        std::unique_lock<std::mutex> heldWmiReceipt(g_msiWmiReceiptMx);
        auto wmiPumpReader=std::async(std::launch::async,[&]{wmiPumpEntered.set_value();msiWmiPump();wmiPumpFinished.set_value();});
        const bool wmiReaderEntered=wmiEntered.wait_for(std::chrono::seconds(2))==std::future_status::ready;
        const bool wmiReaderPassedReceipt=wmiFinished.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        g_oemKeyMaskMsi.store(0); heldWmiReceipt.unlock();
        const bool wmiReaderFinished=wmiFinished.wait_for(std::chrono::seconds(2))==std::future_status::ready;
        wmiPumpReader.get();
        check(wmiReaderEntered && !wmiReaderPassedReceipt && wmiReaderFinished,
            "output-only withdrawal keeps the receipt mutex observation before lifecycle maintenance");
        fixtureWmiTerminal=true; msiWmiSetEnabled(false);

        auto mixedDomains=rule("os.openTouchKeyboard","unused","press","oem","l4"); mixedDomains["params"]=json::object();
        mixedDomains["inputs"].push_back({{"source","controller"},{"code","a"}});
        uint64_t mixedMask=0;
        check(nativeKeyboardShortcutRequiredMask(mixedDomains["inputs"],mixedMask) && mixedMask==(kOemBitL4|XINPUT_GAMEPAD_A),
            "actual parser admits controller plus OEM with exact required bits");
        resetRoute(json::array({mixedDomains})); tick(0,10000);
        check(g_nativeKeyboardShortcuts.size()==1 && fixtureOwnActions.empty(), "mixed input rule reaches evaluator without inventing an action");
        tick(XINPUT_GAMEPAD_A,10100); flushJobs();
        check(fixtureOwnActions.empty(), "mixed input rule cannot fire without its OEM key");
        fixtureOemMask=kOemBitL4; tick(XINPUT_GAMEPAD_B,10200); flushJobs();
        check(fixtureOwnActions.empty(), "mixed input rule cannot substitute another controller key");
        tick(XINPUT_GAMEPAD_A,10300); flushJobs();
        check(fixtureOwnActions.size()==1, "mixed input rule fires exactly once when both required domains match");
        auto wrongDomain=mixedDomains; wrongDomain["inputs"][1]["source"]="oem";
        check(!nativeKeyboardShortcutRequiredMask(wrongDomain["inputs"],mixedMask), "actual parser rejects a controller code labelled as OEM");
        resetRoute(json::array({wrongDomain})); tick(0,10000);
        check(g_nativeKeyboardShortcuts.empty(), "wrong source class never reaches the sampled evaluator");
        auto independent=mixedDomains; independent["inputs"].erase(1);
        resetRoute(json::array({independent})); tick(0,10000); fixtureOemMask=kOemBitL4; tick(XINPUT_GAMEPAD_B,10100); flushJobs();
        check(fixtureOwnActions.size()==1, "independent OEM action remains usable with unrelated controller held");
#endif

#if defined(YMCC_PRODUCER_EDGE_SOURCE)
        struct ProducerCase { OemProducerId id; const char* code; uint64_t mask; bool orange; };
        const ProducerCase producerCases[]={
            {OemProducerId::Legion,"legionl",kOemBitLegionL,false},
            {OemProducerId::Legion,"legionr",kOemBitLegionR,false},
            {OemProducerId::Legion,"legiony2",kOemBitLegionY2,false},
            {OemProducerId::Legion,"legiony3",kOemBitLegionY3,false},
            {OemProducerId::Legion,"legionm2",kOemBitLegionM2,false},
            {OemProducerId::Legion,"legionm3",kOemBitLegionM3,false},
            {OemProducerId::LegionS,"legionl",kOemBitLegionL,false},
            {OemProducerId::LegionS,"legionr",kOemBitLegionR,false},
            {OemProducerId::Gpd,"l4",kOemBitL4,false},
            {OemProducerId::Gpd,"r4",kOemBitR4,false},
            {OemProducerId::OneX,"l4",kOemBitL4,false},
            {OemProducerId::OneX,"r4",kOemBitR4,false},
            {OemProducerId::OneX,"special",kOemBitSpecial,false},
            {OemProducerId::OneX,"orange",kOemBitOrange,true},
            {OemProducerId::OneX,"keyboard",kOemBitKeyboard,false},
            {OemProducerId::Msi,"m1",kOemBitM1,false},
            {OemProducerId::Msi,"m2",kOemBitM2,false}
        };
        for(const auto& item:producerCases) {
            resetRoute(json::array({producerRule(item.code),producerRule(item.code,"release")}));
            const auto generation=activateProducer(item.id); g_onexBack.desc.role21Orange=item.orange;
            producerSample(item.id,0); tick(0,fixtureTick); fixtureTick+=20;
            producerSample(item.id,item.mask); fixtureTick+=20; producerSample(item.id,0);
            const size_t pendingEdges=g_oemProducerEdgeCount;
            fixtureTick+=1000; tick(0,fixtureTick); flushJobs();
            check(pendingEdges>=2 && fixtureOwnActions.size()==2,
                std::string("real producer short DOWN/UP survives delayed UI: ")+oemProducerName(item.id)+"/"+item.code);
            tick(0,fixtureTick+100); nativeOemProducerDrain(); flushJobs();
            check(fixtureOwnActions.size()==2 && oemProducerCurrent(item.id,generation),
                std::string("retained plus live evaluation does not duplicate producer action: ")+oemProducerName(item.id)+"/"+item.code);
            producerSample(item.id,item.mask); nativeOemProducerDrain();
            closeProducer(item.id); flushJobs(); tick(0,fixtureTick+200); flushJobs();
            check(fixtureOwnActions.size()==2 && !oemProducerCurrent(item.id,generation),
                std::string("real close cancels admitted job and cannot fake physical release: ")+oemProducerName(item.id)+"/"+item.code);
        }
        // R11: exercise every implemented HID producer through its real parser,
        // current rear consumer and unrelated source cleanup. Device I/O is simulated.
        for(const auto& item:producerCases) {
            if(item.id==OemProducerId::Msi)continue;
            resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::None);
            g_oemHooksWanted=false;const auto generation=activateProducer(item.id);g_onexBack.desc.role21Orange=item.orange;
            g_oemRearMapToL5=item.mask;producerSample(item.id,0);producerSample(item.id,item.mask);
            const auto held=oemLiveBackBits();const std::string label=std::string(oemProducerName(item.id))+"/"+item.code;
            check((held&1)!=0,"qualified HID canonical bit reaches explicit rear mapper: "+label);
            nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick,false);oemHidClear();
            check(oemLiveBackBits()==held,"hook/compatibility cleanup cannot erase qualified HID rear: "+label);
            if(item.id!=OemProducerId::Gpd)gpdBackClose();else legionBackClose();
            check(oemLiveBackBits()==held && oemProducerCurrent(item.id,generation),
                "unrelated producer close preserves active rear owner: "+label);
            producerSample(item.id,0);nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+50,false);
            check(oemLiveBackBits()==0,"qualified HID UP releases mapped rear after maintenance: "+label);
            producerSample(item.id,item.mask);closeProducer(item.id);nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick+100,false);
            check(oemLiveBackBits()==0 && !oemProducerCurrent(item.id,generation),"own HID close withdraws canonical and rear state: "+label);
        }
        for(const bool left:{false,true}) {
            resetRoute(json::array());g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);
            const auto mask=left?kOemBitM1:kOemBitM2;const int display=left?2:1;
            // Boundary model of the two atomics written by an accepted MSI sample.
            // Full MSI clear/restore remains in the native capability selftest.
            g_oemKeyMaskMsiDinput=mask;g_msiDinputBackBits=display;fixtureMsiAuthority=static_cast<int>(mask);
            g_oemHooksWanted=false;nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},fixtureTick,false);
            check(oemLiveBackBits()==static_cast<unsigned>(display) && g_oemKeyMaskMsiDinput==mask,
                std::string("hook reconciliation preserves independent MSI rear sample: ")+(left?"M1":"M2"));
            oemRawPathReset("r11-unrelated-raw-reset");oemHidClear();
            check(oemLiveBackBits()==static_cast<unsigned>(display),
                std::string("unrelated raw/HID cleanup cannot clear MSI rear: ")+(left?"M1":"M2"));
        }
        for(const auto& item:producerCases) {
            resetRoute(json::array({producerRule(item.code,"hold")})); activateProducer(item.id);g_onexBack.desc.role21Orange=item.orange;
            producerSample(item.id,0); tick(0,fixtureTick); fixtureTick+=20;
            producerSample(item.id,item.mask); fixtureTick+=100; producerSample(item.id,0);
            fixtureTick+=1500; tick(0,fixtureTick);flushJobs();
            check(fixtureOwnActions.empty(),std::string("UI delay cannot convert short producer stroke into hold: ")+oemProducerName(item.id)+"/"+item.code);
        }
        resetRoute(json::array({producerRule("m1","hold")})); auto msiGeneration=activateProducer(OemProducerId::Msi);
        producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);fixtureTick+=20;producerSample(OemProducerId::Msi,kOemBitM1);
        fixtureTick+=700;tick(0,fixtureTick);flushJobs();
        check(fixtureOwnActions.size()==1,"actual retained held MSI source still triggers one existing hold action");
        tick(0,fixtureTick+200);flushJobs();
        check(fixtureOwnActions.size()==1,"held-source repeated UI sampling does not duplicate hold");

        resetRoute(json::array({producerRule("l4","double")}));auto gpdGeneration=activateProducer(OemProducerId::Gpd);
        producerSample(OemProducerId::Gpd,0);tick(0,fixtureTick);
        for(int i=0;i<2;++i) {fixtureTick+=100;producerSample(OemProducerId::Gpd,kOemBitL4);fixtureTick+=20;producerSample(OemProducerId::Gpd,0);}
        fixtureTick+=1500;tick(0,fixtureTick);flushJobs();
        check(fixtureOwnActions.size()==1,"producer double uses actual capture times despite long UI delay");
        fixtureTick+=600;producerSample(OemProducerId::Gpd,kOemBitL4);fixtureTick+=20;producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();flushJobs();
        closeProducer(OemProducerId::Gpd);gpdGeneration=activateProducer(OemProducerId::Gpd);
        producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();fixtureTick+=100;producerSample(OemProducerId::Gpd,kOemBitL4);producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"replacement source cannot inherit old double history");

        resetRoute(json::array({producerRule("legionl")}));auto oldLegion=activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Legion,0);tick(0,fixtureTick);producerSample(OemProducerId::Legion,kOemBitLegionL);
        closeProducer(OemProducerId::Legion);auto newLegion=activateProducer(OemProducerId::Legion);
        const size_t beforeOldCompletion=g_oemProducerEdgeCount;g_oemKeyMaskHid=kOemBitLegionR;
        producerSample(OemProducerId::Legion,kOemBitLegionL,oldLegion);
        check(g_oemProducerEdgeCount==beforeOldCompletion && g_oemKeyMaskHid==kOemBitLegionR,
            "captured old read identity rejects publication before touching new physical state");
        producerSample(OemProducerId::Legion,0);producerSample(OemProducerId::Legion,kOemBitLegionL);producerSample(OemProducerId::Legion,0);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1 && oemProducerCurrent(OemProducerId::Legion,newLegion),
            "source rebind admits fresh neutral/down/up but never old queued episode");
        check(!oemProducerCancel(OemProducerId::Legion,oldLegion) && oemProducerCurrent(OemProducerId::Legion,newLegion),
            "old source cleanup cannot withdraw replacement binding");

        for(OemProducerId id:{OemProducerId::Legion,OemProducerId::LegionS,OemProducerId::Gpd,OemProducerId::Msi}) {
            const uint64_t bit=id==OemProducerId::Gpd ? kOemBitL4 : id==OemProducerId::Msi ? kOemBitM1 : kOemBitLegionL;
            const char* code=id==OemProducerId::Gpd ? "l4" : id==OemProducerId::Msi ? "m1" : "legionl";
            resetRoute(json::array({producerRule(code)}));activateProducer(id);
            producerSample(id,bit);tick(0,fixtureTick);flushJobs();
            check(fixtureOwnActions.empty(),std::string("first already-held snapshot cannot impersonate fresh press: ")+oemProducerName(id));
            producerSample(id,0);nativeOemProducerDrain();producerSample(id,bit);producerSample(id,0);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==1,std::string("release arms snapshot source for fresh next press: ")+oemProducerName(id));
        }
        resetRoute(json::array({producerRule("m1")}));fixtureMsiAuthority=static_cast<int>(kOemBitM1|kOemBitM2);msiGeneration=msiDinputShortcutBeginRead();
        (void)msiDinputShortcutPublish(0,msiGeneration);nativeOemProducerDrain();
        msiDinputShortcutCancel();auto newMsi=msiDinputShortcutBeginRead();
        check(!msiDinputShortcutPublish(kOemBitM1,msiGeneration) && g_oemProducerEdgeCount==0,
            "MSI read cancelled before commit cannot open a replacement action generation");
        (void)msiDinputShortcutPublish(0,newMsi);(void)msiDinputShortcutPublish(kOemBitM1,newMsi);(void)msiDinputShortcutPublish(0,newMsi);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"MSI fresh read generation recovers after rejected stale commit");

        resetRoute(json::array({producerRule("m1"),producerRule("legionr")}));msiGeneration=activateProducer(OemProducerId::Msi);auto legionGeneration=activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Msi,0);producerSample(OemProducerId::Legion,0);tick(0,fixtureTick);
        producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);
        producerSample(OemProducerId::Legion,kOemBitLegionR);producerSample(OemProducerId::Legion,0);
        closeProducer(OemProducerId::Msi);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1 && oemProducerCurrent(OemProducerId::Legion,legionGeneration),
            "one source withdrawal retains another source FIFO and qualified action");
        resetRoute(json::array({producerRule("m1"),rule("input.gyroToggle","fps","hold")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);tick(XINPUT_GAMEPAD_A,fixtureTick+20);
        const auto unrelatedController=g_nativeKeyboardShortcuts[1];producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();
        check(g_nativeKeyboardShortcuts[1].matchedLastTick==unrelatedController.matchedLastTick && g_nativeKeyboardShortcuts[1].holdStartedAt==unrelatedController.holdStartedAt,
            "producer receipt cannot rewrite unrelated controller hold state");

        resetRoute(json::array({producerRule("l4")}));activateProducer(OemProducerId::Gpd);tick(0,fixtureTick);
        KBDLLHOOKSTRUCT gpdKey{};gpdKey.vkCode=VK_F14;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&gpdKey));gpdKey.flags=LLKHF_UP;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.size()==1 && !g_oemProducerStates[static_cast<size_t>(OemProducerId::Gpd)].sampled,
            "GPD interface bound with zero HID samples preserves real keyboard shortcut path");
        resetRoute(json::array({producerRule("l4","release")}));activateProducer(OemProducerId::Gpd);producerSample(OemProducerId::Gpd,0);tick(0,fixtureTick);
        gpdKey.flags=0;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.empty(),"zero HID state cannot invent release of still-held GPD keyboard fallback");
        gpdKey.flags=LLKHF_UP;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.empty(),"keyboard UP after verified HID ownership cannot fabricate old-source release");
        producerSample(OemProducerId::Gpd,kOemBitL4);producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"qualified HID UP releases a fresh GPD press after ownership transfer");

        resetRoute(json::array({producerRule("l4")}));gpdGeneration=activateProducer(OemProducerId::Gpd);producerSample(OemProducerId::Gpd,0);tick(0,fixtureTick);
        gpdKey.flags=0;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&gpdKey));gpdKey.flags=LLKHF_UP;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.empty(),"confirmed GPD HID owns key action and ignores duplicate keyboard stroke");
        producerSample(OemProducerId::Gpd,kOemBitL4);producerSample(OemProducerId::Gpd,0);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"confirmed GPD HID plus keyboard duplicate has exactly one configured action");
        closeProducer(OemProducerId::Gpd);tick(0,fixtureTick);g_gpdBackIfaceReady=true;
        gpdKey.flags=0;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&gpdKey));gpdKey.flags=LLKHF_UP;oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();flushJobs();
        check(fixtureOwnActions.size()==2 && oemProducerAuthorityMask()==0,"withdrawn HID authority restores a fresh keyboard fallback stroke");
        resetRoute(json::array({producerRule("l4")}));activateProducer(OemProducerId::Gpd);tick(0,fixtureTick);
        gpdKey.flags=0;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&gpdKey));deliverPhysicalReceipts();
        check(fixtureJobs.size()==1,"unconfirmed GPD keyboard action can enter queue");
        producerSample(OemProducerId::Gpd,0);flushJobs();
        check(fixtureOwnActions.empty(),"HID authority acquired after queue admission cancels old keyboard fallback job");

        resetRoute(json::array({producerRule("special"),producerRule("keyboard")}));auto oneXGeneration=activateProducer(OemProducerId::OneX);
        BYTE oneXObserved[64]{};oneXObserved[0]=kOnexBackBtnCmd;oneXObserved[1]=oneXObserved[62]=kOnexBackFrame;oneXObserved[6]=kOnexBackSpecialId;
        onexBackParse(oneXObserved,sizeof(oneXObserved),oneXGeneration);
        check(oemProducerAuthorityMask()==kOemBitSpecial,"OneX observed 0x21 acquires only that key and leaves unobserved keyboard/rear fallbacks");
        oneXObserved[6]=kOnexBackKeyboardId;onexBackParse(oneXObserved,sizeof(oneXObserved),oneXGeneration);
        check(oemProducerAuthorityMask()==(kOemBitSpecial|kOemBitKeyboard),"OneX independent observed button acquires its own side without global latch");
        resetRoute(json::array({producerRule("m1"),producerRule("m2")}));fixtureMsiAuthority=static_cast<int>(kOemBitM1);msiGeneration=activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);
        check(oemProducerAuthorityMask()==kOemBitM1,"MSI confirmed button15 never acquires unsupported button16 fallback");
        fixtureMsiAuthority=0;producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();
        check(oemProducerAuthorityMask()==0,"MSI capability withdrawal removes its own authority instead of permanently latching");

        resetRoute(json::array());activateProducer(OemProducerId::Gpd);producerSample(OemProducerId::Gpd,0);
        g_oemKeyMask=kOemBitL4;g_oemBackStateKb=2;g_gpdChordPubGen=g_gpdBackIfaceGen.load();
        producerSample(OemProducerId::Gpd,kOemBitL4);
        check(oemBackMergedDisplayState()==0 && g_oemBackState==1 && oemLiveBackBits()==1,
            "confirmed GPD HID prevents keyboard rear display from adding opposite swapped slot");
        check((fixtureRealOemBackMergedKeyMask()&kOemBitL4)!=0,
            "canonical OEM merge retains authoritative HID key after removing keyboard duplicate");
        resetRoute(json::array());activateProducer(OemProducerId::OneX);producerSample(OemProducerId::OneX,0);producerSample(OemProducerId::OneX,kOemBitSpecial);
        g_oemRearMapToL5=kOemBitSpecial;
        check(oemLiveBackBits()==1,"existing OEM-to-rear mapper can consume OneX HID Special canonical bit");
        producerSample(OemProducerId::OneX,0);
        check(oemLiveBackBits()==0,"actual OneX Special UP clears mapped rear output");
        resetRoute(json::array());activateProducer(OemProducerId::OneX);g_onexBack.desc.role21Orange=true;producerSample(OemProducerId::OneX,0);producerSample(OemProducerId::OneX,kOemBitOrange);
        g_oemRearMapToR5=kOemBitOrange;
        check(oemLiveBackBits()==2,"Apex Orange uses HC descriptor role in existing rear mapper");
        onexBackClose();
        check(oemLiveBackBits()==0 && oemProducerAuthorityMask()==0,"OneX close clears owned canonical HID state and rear mapping");

        resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);
        producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();
        fixtureMsiAuthority=0;flushJobs();
        check(fixtureOwnActions.empty(),"MSI capability revoked after action admission rejects queued action before next producer sample");
        resetRoute(json::array({producerRule("m1")}));g_oemBackProfileId=static_cast<int>(OemChordProfileId::Msi);tick(0,fixtureTick);
        KBDLLHOOKSTRUCT msiFallbackKey{};msiFallbackKey.vkCode=VK_F11;oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&msiFallbackKey));deliverPhysicalReceipts();
        check(fixtureJobs.size()==1,"unconfirmed MSI keyboard fallback can enter existing action queue");
        fixtureMsiAuthority=static_cast<int>(kOemBitM1);flushJobs();
        check(fixtureOwnActions.empty(),"new MSI capability authority cancels queued keyboard fallback even before HID receipt publish");
        resetRoute(json::array({producerRule("m2")}));fixtureMsiAuthority=static_cast<int>(kOemBitM1);msiGeneration=activateProducer(OemProducerId::Msi);
        check(!msiDinputShortcutPublish(kOemBitM2,msiGeneration),"MSI publication seam rejects raw bits outside currently confirmed capability");

        for(bool exitAfterQueue:{false,true}) {
            resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);
            producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);
            if(exitAfterQueue) g_exitRequested=true;else g_shortcutRecordingActive=true;
            nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.empty(),exitAfterQueue ? "exit cancels retained producer action" : "recording cancels retained producer action");
        }
        resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);
        producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);++g_oemShortcutEdgeEpoch;nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.empty(),"rule epoch change cancels retained source transitions");
        resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);fixturePostOk=false;
        producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick+100);flushJobs();
        check(fixtureOwnActions.size()==1,"failed producer wake post retains transitions for existing UI evaluation");

        resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);
        for(size_t n=0;n<kOemProducerEdgeCapacity/2;++n){producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);}
        const size_t beforeOverflow=g_oemProducerEdgeCount;
        producerSample(OemProducerId::Msi,kOemBitM1);
        check(beforeOverflow==kOemProducerEdgeCapacity && g_oemProducerEdgeCount==beforeOverflow && fixtureLogs.back().value("reason","")=="receipt-queue-full",
            "producer queue explicitly refuses overflow without replacing admitted transitions");
        nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==kOemProducerEdgeCapacity/2,"bounded FIFO preserves every admitted producer press in capture order");
        producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();tick(0,fixtureTick);flushJobs();
        check(oemProducerActionMask(true)==0,"queue overflow cannot latch physical pressed state after actual release");

        resetRoute(json::array({producerRule("m1")}));activateProducer(OemProducerId::Msi);producerSample(OemProducerId::Msi,0);tick(0,fixtureTick);fixtureJobAccept=false;
        producerSample(OemProducerId::Msi,kOemBitM1);producerSample(OemProducerId::Msi,0);nativeOemProducerDrain();
        check(fixtureJobs.empty() && std::any_of(fixtureLogs.begin(),fixtureLogs.end(),[](const json& e){return e.value("event","")=="oem-shortcut-edge" && !e.value("emit",true) && e.value("matched",false);}),
            "producer action queue refusal is reported at actual rule dispatch boundary");
        resetRoute(json::array({producerRule("special")}));activateProducer(OemProducerId::OneX);producerSample(OemProducerId::OneX,0);tick(0,fixtureTick);
        BYTE invalidOnex[64]{};invalidOnex[0]=kOnexBackBtnCmd;invalidOnex[6]=kOnexBackSpecialId;invalidOnex[12]=1;
        const size_t beforeBadFrame=g_oemProducerEdgeCount;onexBackParse(invalidOnex,sizeof(invalidOnex),g_onexBack.actionGeneration);
        check(g_oemProducerEdgeCount==beforeBadFrame && g_onexBack.auxKeys==0,"OneX invalid frame markers never publish shortcut event");
        invalidOnex[1]=invalidOnex[62]=kOnexBackFrame;invalidOnex[6]=0xFE;onexBackParse(invalidOnex,sizeof(invalidOnex),g_onexBack.actionGeneration);
        check(g_oemProducerEdgeCount==beforeBadFrame,"OneX unknown button ID never becomes a shortcut event");
#endif
#if defined(YMCC_PRODUCER_EDGE_SOURCE)
        for (bool readStuck : {true, false}) {
            resetRoute(json::array({producerRule("special")}));
            const auto stuckGeneration=activateProducer(OemProducerId::OneX);
            producerSample(OemProducerId::OneX,0);tick(0,fixtureTick);
            producerSample(OemProducerId::OneX,kOemBitSpecial);
            g_oemRearMapToL5=kOemBitSpecial;
            check((oemLiveBackBits()&1)!=0,"OneX actual special-to-rear mapping is active before retirement");
            g_onexBack.device=reinterpret_cast<HANDLE>(0x202);g_onexBack.ovEvent=reinterpret_cast<HANDLE>(0x203);
            g_onexBack.readPending=true;g_onexBack.writeInFlight=true;
            if (readStuck) onexBackRetireReadStuck("fixture-drain-limit"); else onexBackRetireStuck();
            check(!oemProducerCurrent(OemProducerId::OneX,stuckGeneration) && g_onexBack.actionGeneration==0,
                readStuck ? "real read-stuck retirement withdraws action source" : "real write-stuck retirement withdraws action source");
            check(oemLiveBackBits()==0 && g_oemProducerEdgeCount==0,
                readStuck ? "read-stuck retirement clears mapped rear state and queued actions" : "write-stuck retirement clears mapped rear state and queued actions");
            check(g_onexBack.readPending && g_onexBack.writeInFlight && g_onexBack.device==reinterpret_cast<HANDLE>(0x202),
                "retirement preserves in-flight shared device resources");
            nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.empty(),"retired source cannot dispatch a queued special-key action");
        }
#endif
#if defined(YMCC_ROG_CLICK_SOURCE)
        struct RogClickCase { BYTE code; const char* name; uint64_t bit; };
        const RogClickCase rogCases[]={{56,"ac",kOemBitAc},{147,"lib",kOemBitLib},{166,"cc",kOemBitCc}};
        for (const auto& item : rogCases) {
            resetRoute(json::array({producerRule(item.name)}));tick(0,10000);
            const auto rg=activateProducer(OemProducerId::RogClick);
            BYTE raw[]={0x5A,item.code};
            for (int n=0;n<3;++n) {fixtureTick+=20;rogOemKeyParse(raw,sizeof(raw),rg);}
            check(g_oemProducerEdgeCount==3 && (nativeOemKeyMask(false)&item.bit)!=0,
                std::string("ROG actual parser retains three complete vendor clicks: ")+item.name);
            fixtureTick+=1000;rogOemKeyExpirePulse(fixtureTick);
            check(g_oemProducerEdgeCount==3 && g_oemKeyMaskRog==0,
                std::string("ROG actual pulse expiry cannot erase retained clicks: ")+item.name);
            tick(0,fixtureTick);flushJobs();
            check(fixtureOwnActions.size()==3 && fixtureLegacyRog==0,
                std::string("ROG delayed UI executes every configured click without extra window toggle: ")+item.name);
            tick(0,fixtureTick+100);flushJobs();check(fixtureOwnActions.size()==3,
                std::string("ROG display/live sampling never duplicates terminal actions: ")+item.name);
            raw[0]=item.code;rogOemKeyParse(raw,1,rg);nativeOemProducerDrain();flushJobs();
            check(fixtureOwnActions.size()==4,"ROG report without 0x5A prefix uses same actual event mapping");
        }
        resetRoute(json::array({producerRule("cc","double")}));tick(0,10000);
        auto rg=activateProducer(OemProducerId::RogClick);BYTE cc[]={0x5A,166};
        fixtureTick=10100;rogOemKeyParse(cc,sizeof(cc),rg);fixtureTick=10200;rogOemKeyParse(cc,sizeof(cc),rg);
        fixtureTick=15000;rogOemKeyExpirePulse(fixtureTick);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"ROG double-click uses capture times despite delayed UI");
        fixtureTick=16000;rogOemKeyParse(cc,sizeof(cc),rg);fixtureTick=17000;rogOemKeyParse(cc,sizeof(cc),rg);
        nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.size()==1,"ROG separated clicks never become double from common UI dispatch time");
        rogOemKeyClose();rg=activateProducer(OemProducerId::RogClick);fixtureTick=17100;rogOemKeyParse(cc,sizeof(cc),rg);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==1,"ROG replacement binding cannot inherit prior double-click history");
        resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);
        fixtureTick=10100;rogOemKeyParse(cc,sizeof(cc),rg);nativeOemProducerDrain();check(fixtureJobs.size()==1,"ROG click reaches real serial action admission");
        rogOemKeyClose();flushJobs();check(fixtureOwnActions.empty() && g_oemKeyMaskRog==0 && g_rogOemKey.actionGeneration==0,
            "ROG real close cancels already-admitted action and display state");
        const auto replacement=activateProducer(OemProducerId::RogClick);rogOemKeyParse(cc,sizeof(cc),rg);
        check(g_oemProducerEdgeCount==0 && g_oemKeyMaskRog==0,"ROG stale captured read cannot change new source or display");
        rogOemKeyParse(cc,sizeof(cc),replacement);rogOemKeyClose();nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.empty(),"ROG close removes undelivered FIFO clicks");
        for (bool recording : {true,false}) {
            resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);rogOemKeyParse(cc,sizeof(cc),rg);
            if(recording)g_shortcutRecordingActive=true;else g_exitRequested=true;
            nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.empty() && fixtureLegacyRog==0,
                recording ? "ROG recording cancels click and default" : "ROG exit cancels click and default");
        }
        resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);rogOemKeyParse(cc,sizeof(cc),rg);
        ++g_oemShortcutEdgeEpoch;nativeOemProducerDrain();flushJobs();check(fixtureOwnActions.empty(),"ROG rule epoch change rejects old terminal click");
        resetRoute(json::array());tick(0,10000);rg=activateProducer(OemProducerId::RogClick);
        for(const auto& item:rogCases){BYTE raw[]={item.code};rogOemKeyParse(raw,sizeof(raw),rg);}
        check(fixtureLegacyRog==0,"ROG parser does not call blocking/default action on capture lane");
        nativeOemProducerDrain();flushJobs();check(fixtureLegacyRog==2 && fixtureOwnActions.empty(),"ROG unconfigured AC/CC preserve existing window default; LIB has none");
        resetRoute(json::array({producerRule("cc","hold"),producerRule("cc","release"),producerRule("cc","repeat")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);rogOemKeyParse(cc,sizeof(cc),rg);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.empty() && fixtureLegacyRog==0,"ROG click-only invalid trigger cannot invent hold/release/repeat or default");
        resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);fixturePostOk=false;
        rogOemKeyParse(cc,sizeof(cc),rg);fixtureTick+=1000;rogOemKeyExpirePulse(fixtureTick);tick(0,fixtureTick);flushJobs();
        check(fixtureOwnActions.size()==1,"ROG failed wake post retains actual click for existing evaluation");
        resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);
        for(size_t n=0;n<kOemProducerEdgeCapacity;++n)rogOemKeyParse(cc,sizeof(cc),rg);
        check(g_oemProducerEdgeCount==kOemProducerEdgeCapacity,"ROG FIFO bounded capacity admits each complete click");
        rogOemKeyParse(cc,sizeof(cc),rg);nativeOemProducerDrain();flushJobs();
        check(fixtureOwnActions.size()==kOemProducerEdgeCapacity,"ROG overflow never overwrites already-admitted clicks");
        resetRoute(json::array({producerRule("cc")}));tick(0,10000);rg=activateProducer(OemProducerId::RogClick);fixtureJobAccept=false;
        rogOemKeyParse(cc,sizeof(cc),rg);nativeOemProducerDrain();
        check(std::any_of(fixtureLogs.begin(),fixtureLogs.end(),[](const json& j){return j.value("source","")=="rog-hid-click" && j.value("reason","")=="action-dispatch-refused";}),"ROG action queue refusal has explicit outcome");
        resetRoute(json::array());rg=activateProducer(OemProducerId::RogClick);BYTE unknown[]={167};rogOemKeyParse(unknown,sizeof(unknown),rg);unknown[0]=168;rogOemKeyParse(unknown,sizeof(unknown),rg);unknown[0]=165;rogOemKeyParse(unknown,sizeof(unknown),rg);
        check(g_oemProducerEdgeCount==0 && g_oemKeyMaskRog==0,"ROG 165/167/168 never speculatively map OEM3/OEM4 to AC or click");
        g_oemBackState=1;check(oemProducerPhysicalBackState()==1,"ROG-only click binding cannot acquire unrelated managed rear source ownership");
#endif
#if defined(YMCC_HID_READ_SLOT_SOURCE)
        struct ReadSlotCase { OemProducerId id; const char* name; };
        const ReadSlotCase slotCases[]={{OemProducerId::Legion,"legion"},{OemProducerId::LegionS,"legion-s"},{OemProducerId::Gpd,"gpd"},{OemProducerId::RogClick,"rog"}};
        auto readSession=[](OemProducerId id)->OemHidReadSession& {
            switch(id){case OemProducerId::Legion:return g_legionBack;case OemProducerId::LegionS:return g_legionSBack;case OemProducerId::Gpd:return g_gpdBack;default:return g_rogOemKey;}
        };
        auto readSlotClose=[](OemProducerId id){switch(id){case OemProducerId::Legion:legionBackClose();break;case OemProducerId::LegionS:legionSBackClose();break;case OemProducerId::Gpd:gpdBackClose();break;default:rogOemKeyClose();}};
        auto readSlotStart=[](OemProducerId id){switch(id){case OemProducerId::Legion:return legionBackStartRead();case OemProducerId::LegionS:return legionSBackStartRead();case OemProducerId::Gpd:return gpdBackStartRead();default:return rogOemKeyStartRead();}};
        auto readSlotBind=[](OemProducerId id){switch(id){case OemProducerId::Legion:return legionBackBind();case OemProducerId::LegionS:return legionSBackBind();case OemProducerId::Gpd:return gpdBackBind();default:return rogOemKeyBind();}};
        auto readSlotPump=[](OemProducerId id){switch(id){case OemProducerId::Legion:legionBackPump();break;case OemProducerId::LegionS:legionSBackPump();break;case OemProducerId::Gpd:gpdBackPump();break;default:rogOemKeyPump();}};
        auto readSlotEnable=[](OemProducerId id,bool on){switch(id){case OemProducerId::Legion:legionBackSetEnabled(on);break;case OemProducerId::LegionS:legionSBackSetEnabled(on);break;case OemProducerId::Gpd:gpdBackSetEnabled(on);break;default:rogOemKeySetEnabled(on);}};
        auto enumerateSlot=[](OemProducerId id){fixtureIoEnumAvailable=true;switch(id){case OemProducerId::Legion:fixtureIoVendor=0x17EF;fixtureIoProduct=0x6182;fixtureIoUsagePage=0xFFA0;break;case OemProducerId::LegionS:fixtureIoVendor=0x1A86;fixtureIoProduct=0xE310;fixtureIoUsagePage=0xFFA0;break;case OemProducerId::Gpd:fixtureIoVendor=0x2F24;fixtureIoProduct=0x0137;fixtureIoUsagePage=0xFF00;break;default:break;}};
        auto activeRead=[&](OemProducerId id) {
            activateProducer(id);auto& session=readSession(id);session.enabled=true;
            session.device=reinterpret_cast<HANDLE>(0x502);session.ovEvent=reinterpret_cast<HANDLE>(0x503);
            session.buf.assign(64,0);session.ov.Internal=0x103;session.readPending=true;
            session.readActionGeneration=session.actionGeneration;
        };
        for(const auto& item:slotCases) {
            resetRoute(json::array());activeRead(item.id);auto& session=readSession(item.id);
            const auto generation=session.actionGeneration;const auto* address=session.buf.data();const auto* ovAddress=&session.ov;
            readSlotClose(item.id);
            check(session.readPending && session.closing && session.readCancelRequested && fixtureIoClosed.empty() && session.buf.data()==address && &session.ov==ovAddress,
                std::string("actual close preserves pending read buffer/event/OVERLAPPED: ")+item.name);
            check(!session.bound && session.actionGeneration==0 && !oemProducerCurrent(item.id,generation),
                std::string("actual close withdraws action source before I/O terminal: ")+item.name);
            check(fixtureCancelCalls==1 && !oemHidReadCanBegin(session) && !oemHidReadCanBind(session,item.name),
                std::string("pending cancellation forbids new read and rebinding: ")+item.name);
            readSlotClose(item.id);check(fixtureCancelCalls==1,
                std::string("repeated close sends CancelIoEx once per in-flight read: ")+item.name);
            fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultError=ERROR_IO_INCOMPLETE;
            oemHidReadServiceClosing(session,item.name);
            check(session.readPending && fixtureIoClosed.empty() && session.buf.data()==address,
                std::string("signaled event plus incomplete result retains ownership: ")+item.name);
            fixtureIoWait=WAIT_FAILED;fixtureIoResultError=ERROR_INVALID_HANDLE;fixtureTick+=2000;
            oemHidReadServiceClosing(session,item.name);
            check(session.quarantined && session.readPending && session.closing && fixtureIoClosed.empty(),
                std::string("failed observer and elapsed notice cannot free/reuse pending slot: ")+item.name);
            session.enabled=false;session.ov.Internal=0;fixtureIoWait=WAIT_TIMEOUT;fixtureIoResultError=ERROR_OPERATION_ABORTED;
            const size_t actionsBefore=fixtureOwnActions.size();oemHidReadServiceClosing(session,item.name);
            check(!session.readPending && !session.closing && fixtureIoClosed.size()==2 && session.device==INVALID_HANDLE_VALUE && session.ovEvent==nullptr && session.buf.empty(),
                std::string("late canceled terminal result releases device/event/buffer once: ")+item.name);
            oemHidReadServiceClosing(session,item.name);check(fixtureIoClosed.size()==2 && fixtureOwnActions.size()==actionsBefore,
                std::string("late completion never parses or double-closes disposed source: ")+item.name);
            check(oemHidReadCanBind(session,item.name),std::string("terminal release permits legitimate replacement binding: ")+item.name);
            check(std::all_of(fixtureIoWaitBudgets.begin(),fixtureIoWaitBudgets.end(),[](DWORD ms){return ms==0;}) &&
                std::none_of(fixtureIoResultWaitFlags.begin(),fixtureIoResultWaitFlags.end(),[](BOOL b){return b!=FALSE;}),
                std::string("actual close/service use only zero waits/nonblocking completion: ")+item.name);

            resetRoute(json::array());activeRead(item.id);auto& race=readSession(item.id);fixtureIoCancelOk=false;fixtureIoCancelError=ERROR_NOT_FOUND;
            readSlotClose(item.id);check(race.readPending && race.closing && fixtureIoClosed.empty(),
                std::string("CancelIoEx ERROR_NOT_FOUND is not completion proof: ")+item.name);
            race.ov.Internal=0;fixtureIoWait=WAIT_FAILED;fixtureIoResultOk=true;fixtureIoResultError=ERROR_SUCCESS;
            oemHidReadServiceClosing(race,item.name);
            check(!race.readPending && !race.closing && fixtureIoClosed.size()==2 && fixtureOwnActions.empty(),
                std::string("normal success racing cancellation is terminal discard even with failed event: ")+item.name);

            resetRoute(json::array());activeRead(item.id);auto& failed=readSession(item.id);readSlotClose(item.id);
            failed.ov.Internal=static_cast<ULONG_PTR>(0xC0000001);fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultError=ERROR_DEVICE_NOT_CONNECTED;
            oemHidReadServiceClosing(failed,item.name);
            check(!failed.readPending && fixtureIoClosed.size()==2,std::string("terminal device error completes cancellation ownership: ")+item.name);

            resetRoute(json::array());activeRead(item.id);auto& observer=readSession(item.id);readSlotClose(item.id);
            fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultError=ERROR_INVALID_HANDLE;
            oemHidReadServiceClosing(observer,item.name);
            check(observer.readPending && observer.closing && fixtureIoClosed.empty(),
                std::string("GetOverlappedResult API error with pending slot is not terminal: ")+item.name);

            resetRoute(json::array());activeRead(item.id);auto& immediate=readSession(item.id);immediate.readPending=false;immediate.ov.Internal=0;
            readSlotClose(item.id);check(!immediate.closing && fixtureIoClosed.size()==2 && fixtureCancelCalls==0,
                std::string("close without admitted I/O releases immediately without fake cancel: ")+item.name);
            readSlotClose(item.id);check(fixtureIoClosed.size()==2,std::string("empty repeated close never closes another device: ")+item.name);

            resetRoute(json::array());activateProducer(item.id);auto& started=readSession(item.id);started.enabled=true;
            started.device=reinterpret_cast<HANDLE>(0x502);started.ovEvent=reinterpret_cast<HANDLE>(0x503);started.buf.assign(64,0);
            check(readSlotStart(item.id) && started.readPending && started.readActionGeneration==started.actionGeneration,
                std::string("actual read start captures immutable generation at IO_PENDING: ")+item.name);
            const int readsBefore=fixtureIoReadCalls;check(!readSlotStart(item.id) && fixtureIoReadCalls==readsBefore,
                std::string("actual read admission cannot issue overlapping read into shared buffer: ")+item.name);
            readSlotClose(item.id);check(!readSlotStart(item.id) && fixtureIoReadCalls==readsBefore,
                std::string("actual start rejects closing slot without touching OVERLAPPED: ")+item.name);
            started.ov.Internal=0;fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultError=ERROR_OPERATION_ABORTED;oemHidReadServiceClosing(started,item.name);
            activateProducer(item.id);started.device=reinterpret_cast<HANDLE>(0x602);started.ovEvent=reinterpret_cast<HANDLE>(0x603);started.buf.assign(64,0);
            fixtureIoReadError=ERROR_DEVICE_NOT_CONNECTED;fixtureIoReadOk=false;
            check(!readSlotStart(item.id) && !started.bound && started.device==INVALID_HANDLE_VALUE && !started.closing,
                std::string("actual immediate read failure closes source and permits throttled recovery: ")+item.name);
        }
        for(const auto& item:slotCases) {
            resetRoute(json::array());activeRead(item.id);auto& session=readSession(item.id);
            const auto oldGeneration=session.actionGeneration;
            readSlotEnable(item.id,false);
            const int enumBefore=fixtureIoEnumCalls,openBefore=fixtureIoOpenCalls,readsBefore=fixtureIoReadCalls;
            check(!readSlotBind(item.id) && fixtureIoEnumCalls==enumBefore && fixtureIoOpenCalls==openBefore,
                std::string("actual bind refuses old pending slot before any enumeration/open: ")+item.name);
            readSlotPump(item.id);
            check(!session.enabled && session.closing && session.readPending && fixtureIoClosed.empty() && fixtureIoReadCalls==readsBefore,
                std::string("actual disabled pump preserves incomplete close without new read: ")+item.name);
            fixtureTick+=2500;readSlotEnable(item.id,true);readSlotPump(item.id);
            check(session.enabled && session.closing && session.readPending && fixtureIoEnumCalls==enumBefore && fixtureIoOpenCalls==openBefore,
                std::string("rapid re-enable keeps quarantined slot unavailable until terminal: ")+item.name);
            readSlotEnable(item.id,false);session.ov.Internal=0;fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultOk=true;
            readSlotPump(item.id);
            check(!session.readPending && !session.closing && session.device==INVALID_HANDLE_VALUE && fixtureIoClosed.size()==2 && fixtureOwnActions.empty() && fixtureIoReadCalls==readsBefore,
                std::string("actual disabled pump reaps late successful bytes without parse/rebind: ")+item.name);
            enumerateSlot(item.id);fixtureIoWait=WAIT_TIMEOUT;fixtureIoResultOk=false;fixtureIoResultError=ERROR_IO_INCOMPLETE;
            readSlotEnable(item.id,true);
            check(session.bound && session.actionGeneration!=0 && session.actionGeneration!=oldGeneration && oemProducerCurrent(item.id,session.actionGeneration),
                std::string("actual post-terminal bind admits only a new source generation: ")+item.name);
            const auto nextGeneration=session.actionGeneration;const int newEnums=fixtureIoEnumCalls,newOpens=fixtureIoOpenCalls;
            check(readSlotBind(item.id) && session.actionGeneration==nextGeneration && fixtureIoEnumCalls==newEnums && fixtureIoOpenCalls==newOpens,
                std::string("actual healthy rebind is idempotent without replacing source identity: ")+item.name);
            readSlotPump(item.id);
            check(session.readPending && session.readActionGeneration==nextGeneration && fixtureIoReadCalls==readsBefore+1,
                std::string("actual rebound pump starts exactly one current-generation read: ")+item.name);
            fixtureIoWait=WAIT_OBJECT_0;fixtureIoResultError=ERROR_INVALID_HANDLE;session.ov.Internal=0x103;readSlotPump(item.id);
            check(session.closing && session.readPending && session.actionGeneration==0 && fixtureIoReadCalls==readsBefore+1,
                std::string("actual active pump observer failure withdraws source and retains read: ")+item.name);
        }
        resetRoute(json::array());activeRead(OemProducerId::Legion);activeRead(OemProducerId::LegionS);activeRead(OemProducerId::Gpd);activeRead(OemProducerId::RogClick);
        g_gpdBackEnabledSnap=true;oemVendorHidShutdown();
        check(!g_legionBack.enabled && !g_legionSBack.enabled && !g_gpdBack.enabled && !g_rogOemKey.enabled && !g_gpdBackEnabledSnap,
            "real capture-owner shutdown disables all four collectors");
        check(g_legionBack.readPending && g_legionSBack.readPending && g_gpdBack.readPending && g_rogOemKey.readPending && fixtureIoClosed.empty() && fixtureCancelCalls==4,
            "real shutdown retains all unresolved fixed process-lifetime slots without join/free");
        check(std::all_of(std::begin(g_oemProducerGenerations),std::end(g_oemProducerGenerations),[](const auto& x){return x.load()==0;}),
            "real capture shutdown withdraws all four action sources");
#endif

        // R10: front/rear are different HC physical switches. IDs legionl/r remain
        // old rear aliases so saved actions never silently move to a front button.
        for(const auto id:{OemProducerId::Legion,OemProducerId::LegionS}) {
            for(const auto side:{0,1}) {
                const char* code=side ? "legionfrontr" : "legionfrontl";
                const uint64_t front=side ? kOemBitLegionFrontR : kOemBitLegionFrontL;
                resetRoute(json::array({producerRule(code),producerRule(code,"release")}));
                activateProducer(id);producerSample(id,0);tick(0,fixtureTick);
                producerSample(id,front);producerSample(id,0);nativeOemProducerDrain();flushJobs();
                check(fixtureOwnActions.size()==2,std::string("HC front DOWN/UP reaches configured real action: ")+oemProducerName(id)+code);
                check(oemProducerPhysicalBackState()==0,"front press/release cannot become virtual rear state");
                resetRoute(json::array({producerRule(code)}));activateProducer(id);producerSample(id,0);tick(0,fixtureTick);
                producerSample(id,front);closeProducer(id);nativeOemProducerDrain();flushJobs();
                check(fixtureOwnActions.empty(),"closing source cancels queued front action");
            }
        }
        resetRoute(json::array());activateProducer(OemProducerId::Legion);
        producerSample(OemProducerId::Legion,kOemBitLegionR);
        check((oemProducerPhysicalBackState()&3)==1,"HC Y1 is default left rear, not right");
        producerSample(OemProducerId::Legion,kOemBitLegionM3);
        check((oemProducerPhysicalBackState()&3)==2,"HC M3 is default right rear");
        producerSample(OemProducerId::Legion,kOemBitLegionL|kOemBitLegionM2);
        check((oemProducerPhysicalBackState()&3)==0,"HC B11/B5 extra rear switches do not impersonate L4/R4");
        check(oemLiveBackBits()==0,"extra HC B11/B5 bits cannot overflow Host clamp and invent supported paddles");
        g_oemRearMapToL5=kOemBitLegionL;g_oemRearMapToR5=kOemBitLegionM2;
        check(oemLiveBackBits()==3,"explicit extra rear mapping uses supported left/right slots only");
        g_oemRearMapToL5=g_oemRearMapToR5=0;
        producerSample(OemProducerId::Legion,kOemBitLegionY2|kOemBitLegionY3);
        check(oemLiveBackBits()==0,"extra L5/R5 source states remain OEM actions without unsupported virtual bits");
        for(const char* model:{"LegionGoTablet","LegionGoTablet2","LegionGoSZ1","LegionGoSZ2"}) {
            const bool isS=std::string(model).find("SZ")!=std::string::npos;
            const auto catalog=oemCatalogForFamily(YmccFamily::LenovoLegionGo,model);
            check(catalog.count==(isS?4:8),std::string("HC exact front/rear key count: ")+model);
            int frontCount=0,rearSlots=0;bool extraM1=false,extraM2=false;
            for(int i=0;i<catalog.count;i++){
                const auto& key=catalog.keys[i];uint64_t bit=0;
                check(nativeKeyboardShortcutMaskBit(key.keyId,bit),std::string("actual catalog key has consumer: ")+model+key.keyId);
                if(std::string(key.keyId).find("legionfront")==0){frontCount++;check(key.backIndex==0&&!key.clickOnly,"HC front is level state and not rear card");}
                if(key.backIndex>0)rearSlots|=1<<(key.backIndex-1);
                if(std::string(key.label).find("M1")!=std::string::npos)extraM1=true;
                if(std::string(key.label).find("M2")!=std::string::npos)extraM2=true;
            }
            check(frontCount==2 && rearSlots==(isS?3:15),"catalog separates two front buttons from actual rear slots");
            check(isS ? !extraM1&&!extraM2 : extraM1&&extraM2,"Go S does not advertise absent M1/M2 extras");
        }
        shortcutSummonFrontendCases();
        shortcutSummonActionShieldCases();
        std::cout << "input shortcut native model/real route: " << passed << " cases passed\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
