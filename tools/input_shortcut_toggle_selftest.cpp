// Included by the generated native fixture: uses real production functions.
// All storage, desktop-key reads, injection, serial jobs and host refresh are memory-only.
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
    g_inputShortcutRuntime.clear(); g_nativeKeyboardShortcutLastRefresh = 0;
    g_nativeKeyboardShortcutRevision = -1; g_nativeKeyboardShortcuts.clear();
#if !defined(YMCC_SHORTCUT_SOURCE_SCOPES)
    g_nativeKeyboardShortcutRequireRelease = true;
#endif
    fixtureOwnActions.clear(); fixtureDeltas.clear();
    fixtureWindowVisible=true;fixtureWindowIconic=fixtureWindowZoomed=fixtureWindowForeground=false;g_fullHeight=false;
    g_inputReady=true;fixtureWriterAllowed=true;fixtureFrontendPersona=g_inputHostBoundPersona="disabled";g_inputHostEpochMirror=1;
    g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostFaultHoldsStart=false;
    g_frontendButtonPulse.clear();fixtureSendInputs.clear();fixtureSendCount=UINT_MAX;
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
    g_legionBack = {}; g_legionSBack = {}; g_gpdBack = {}; g_onexBack = {}; g_msiDinput = {};
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
    g_oemKbDown = 0; g_oemMouseLeft = false; g_oemMouseX2 = false;
    g_oemKbDownRaw = 0; g_oemBackStateKb = 0;
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
            if(mask&kOemBitL4) data[8]=kGpdBackMask;
            if(mask&kOemBitR4) data[9]=kGpdBackMask;
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
int main() {
    try {
        shortcutSummonFrontendCases();
#if defined(YMCC_ROG_CLICK_SOURCE)
#if defined(YMCC_SHORTCUT_DEFAULT_OWNER_SOURCE)
        check(oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,true,"press"),"R13 executable LIB press may request service takeover");
        check(!oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,true,"hold"),"R13 unsupported click hold does not silence default software");
        check(!oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib|kOemBitAc,true,"press") && !oemVendorStackRuleCanTakeover(YmccFamily::AsusRogAlly,kOemBitLib,false,"press"),"R13 unexecutable composite does not request takeover");
        check(oemVendorStackFrontMask(YmccFamily::AsusRogAlly)==(kOemBitAc|kOemBitCc|kOemBitLib),"R13 ROG vendor lease targets only front OEM actions");
        check((oemVendorStackFrontMask(YmccFamily::AsusRogAlly)&(kOemBitM1|kOemBitM2))==0,"R13 rear-only ROG rules do not stop ASUS services");
        check(oemVendorStackFrontMask(YmccFamily::Gpd)==0 && oemVendorStackFrontMask(YmccFamily::Ayanero)==0 && oemVendorStackFrontMask(YmccFamily::OneXPlayer)==0,"R13 no generic-family service takeover inferred");
        check(oemVendorServiceNamesForFamily(YmccFamily::AsusRogAlly).size()==3,"R13 HC ROG service table has three named services");
        check(oemVendorServiceNamesForFamily(YmccFamily::LenovoLegionGo).size()==1,"R13 HC Legion service table is bound");
        check(oemVendorServiceNamesForFamily(YmccFamily::MsiClaw).size()==1,"R13 HC MSI service table is bound");
        check(oemVendorServiceNamesForFamily(YmccFamily::ZotacGamingZone).size()==2,"R13 HC Zotac service table is bound");
        check(!oemVendorStackFamilySupported(YmccFamily::Gpd) && !oemVendorStackFamilySupported(YmccFamily::Ayanero) && !oemVendorStackFamilySupported(YmccFamily::OneXPlayer),"R13 no unsupported GPD/AYA/OneX service names invented");
#endif
        // Replay the exported settings without changing origin/defaultKey or trigger.
        // Missing an external export still exercises its exact edited default row.
        const json savedInput = fixtureFieldInput.is_object() ? fixtureFieldInput : baseInput();
        const json savedRules = fixtureFieldInput.is_object()
            ? fixtureFieldInput["buttonMapping"]["rules"]["shortcutRules"]
            : json::array({json::parse(R"({"actionId":"window.summon","defaultKey":"enabled","enabled":true,"id":"native-enabled","inputs":[{"code":"lib","source":"oem"}],"origin":"native-default","params":{},"trigger":"press","holdMs":500,"doubleWindowMs":500,"intervalMs":150})")});
        resetRoute(savedRules, savedInput); tick(0,10000);
        const auto fieldGeneration=activateProducer(OemProducerId::RogClick);
        BYTE fieldLib[]={0x5A,147};
        for (int n=0;n<2;++n) { fixtureTick+=1000; rogOemKeyParse(fieldLib,sizeof(fieldLib),fieldGeneration); nativeOemProducerDrain(); flushJobs(); }
        check(fixtureOwnActions.size()==2 && fixtureOwnActions[0]=="window.summon" && fixtureOwnActions[1]=="window.summon" && fixtureLegacyRog==0,
            "R12 exported native-default LIB click reaches summon exactly once per actual click");
#endif
        for (const auto& preset : {"fps", "racing", "custom", "steam"}) {
            for (const auto& withGame : {false, true}) {
                auto input = withGame ? dedicated(baseInput(), "game-A", false) : baseInput(false, "elite");
                const auto frozen = input.dump(); ymcc::InputShortcutRuntime runtime;
                auto on = runtime.toggle(input, "memory-session", "input.gyroToggle", preset);
                check(on.ok && on.enabled && on.effective["gyroMotion"]["enabled"] == true &&
                    on.effective["outputTarget"]["gyroEnabled"] == true && on.effective["gyroMotion"]["preset"] == preset,
                    std::string(withGame ? "dedicated: " : "global: ") + preset + " off -> on uses selected preset");
                auto off = runtime.toggle(input, "memory-session", "input.gyroToggle", "steam");
                check(off.ok && !off.enabled && off.effective["gyroMotion"]["enabled"] == false &&
                    off.effective["outputTarget"]["gyroEnabled"] == false && off.effective["gyroMotion"]["preset"] == preset,
                    std::string(preset) + " on -> off does not switch or memorize another preset");
                check(input.dump() == frozen && off.effective["revision"] == input["revision"] &&
                    off.effective["gyroMotion"]["presets"] == (withGame ? input["gameOverride"]["gyroMotion"]["presets"] : input["gyroMotion"]["presets"]),
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
            auto game = dedicated(baseInput(), "game-A", true, preset); const auto frozenGame = game.dump();
            ymcc::InputShortcutRuntime gameRuntime;
            auto closed = gameRuntime.toggle(game, "memory-session", "input.virtualGamepadToggle", "steamdeck");
            auto reopened = gameRuntime.toggle(game, "memory-session", "input.virtualGamepadToggle", "dualsense-edge");
            check(!closed.enabled && reopened.enabled && reopened.effective["outputTarget"]["persona"] == "dualsense-edge" && game.dump() == frozenGame,
                std::string(preset) + " dedicated target can close/reopen with selected persona while profile remains unchanged");
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
        check(runtime.effective(gameB, "memory-session")["gyroMotion"]["enabled"] == true, "game A temporary off cannot override game B");
        runtime.toggle(gameA, "memory-session", "input.gyroToggle", "fps");
        check(runtime.effective(baseInput(), "memory-session")["outputTarget"]["persona"] == "disabled", "leaving dedicated game restores global configuration without writes");
        check(!runtime.toggle(gameB, "memory-session", "input.gyroToggle", "fps", ymcc::inputShortcutContext(gameA, "memory-session")).ok,
            "stale queued key from game A is rejected after owner changed");
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
        check(fixtureRefreshes == 0 && effectiveRuntimeInputSettings()["gyroMotion"]["enabled"] == false, "real queued job rejects stale context before touching next game's runtime");
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
        const json r12Defaults = json::parse(R"DEFAULTS([{"actionId":"window.summon","defaultKey":"enabled","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-enabled","inputs":[{"source":"controller","code":"lb"},{"source":"controller","code":"rb"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"window.hideToTray","defaultKey":"bDoubleMinimize","doubleWindowMs":500,"enabled":true,"holdMs":2000,"id":"native-bDoubleMinimize","inputs":[{"code":"b","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"double"},{"actionId":"keyboard.numeric","defaultKey":"startDoubleF7","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-startDoubleF7","inputs":[{"code":"start","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"key":"F7"},"trigger":"double"},{"actionId":"power.tdpAdjust","defaultKey":"tdpShortcut","doubleWindowMs":500,"enabled":true,"holdMs":0,"id":"native-tdpShortcut","inputs":[{"code":"start","source":"controller"},{"code":"dpadUp","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"amount":"1","direction":"up","linearAcceleration":true},"trigger":"repeat"},{"actionId":"display.brightnessAdjust","defaultKey":"fpsShortcut","doubleWindowMs":500,"enabled":true,"holdMs":0,"id":"native-fpsShortcut","inputs":[{"code":"start","source":"controller"},{"code":"dpadRight","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{"amount":"5","direction":"right","linearAcceleration":true},"trigger":"repeat"},{"actionId":"game.killCurrent","defaultKey":"killGame","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-killGame","inputs":[{"code":"back","source":"controller"},{"code":"b","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"os.openTouchKeyboard","defaultKey":"openKeyboard","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-openKeyboard","inputs":[{"code":"back","source":"controller"},{"code":"x","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"},{"actionId":"os.returnDesktop","defaultKey":"returnDesktop","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-returnDesktop","inputs":[{"code":"back","source":"controller"},{"code":"a","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"press"},{"actionId":"mouse.toggle","defaultKey":"mouseToggle","doubleWindowMs":500,"enabled":true,"holdMs":500,"id":"native-mouseToggle","inputs":[{"code":"back","source":"controller"},{"code":"y","source":"controller"}],"intervalMs":150,"origin":"native-default","params":{},"trigger":"hold"}])DEFAULTS");
        resetRoute(r12Defaults); tick(0,10000);
        check(g_nativeKeyboardShortcuts.size()==1 && g_nativeShortcutLegacyOverrideMask==0,"R12 nine pristine defaults retain eight legacy owners and one F7 rule owner");
        for (const auto& def:r12Defaults) {
            const auto key=def["defaultKey"].get<std::string>(); const bool hasLegacy=key!="startDoubleF7";
            if (hasLegacy) check(nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 pristine legacy slot remains available: "+key);
            auto changed=def; changed["inputs"]=json::array({{{"source","oem"},{"code","lib"}}}); changed["trigger"]="press";
            resetRoute(json::array({changed}));tick(0,10000);
            check(g_nativeKeyboardShortcuts.size()==1,"R12 saved default remap parses: "+key);
            if (hasLegacy) check(!nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 edited slot disables only its original legacy owner: "+key);
            const auto gen=activateProducer(OemProducerId::RogClick);BYTE data[]={0x5A,147};fixtureTick+=20;rogOemKeyParse(data,2,gen);nativeOemProducerDrain();flushJobs();
            check(key=="startDoubleF7" ? fixtureInjections.size()==1 : fixtureOwnActions.size()==1,"R12 edited default performs its configured action exactly once: "+key);
            changed["enabled"]=false;resetRoute(json::array({changed}));tick(0,10000);
            const auto offGen=activateProducer(OemProducerId::RogClick);rogOemKeyParse(data,2,offGen);nativeOemProducerDrain();flushJobs();
            check(g_nativeKeyboardShortcuts.empty() && fixtureOwnActions.empty() && fixtureInjections.empty(),"R12 disabled remap cannot fire: "+key);
            if (hasLegacy) check(!nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 disabled edited slot does not resurrect legacy binding: "+key);
            resetRoute(r12Defaults);tick(0,10000); if(hasLegacy)check(nativeShortcutLegacyDefaultAllowed(key.c_str()),"R12 restoring pristine default restores its legacy owner: "+key);
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
            check(fixtureOwnActions.empty() && fixtureLegacyClaw == 0 && clickStatus("cancelled"),
                exiting ? "exit cancels WMI click and legacy action" : "recording cancels WMI click and legacy action");
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

        auto mixedDomains=rule("os.openTouchKeyboard","unused","press","oem","l4"); mixedDomains["params"]=json::object();
        mixedDomains["inputs"].push_back({{"source","controller"},{"code","a"}});
        uint64_t mixedMask=0;
        check(!nativeKeyboardShortcutRequiredMask(mixedDomains["inputs"],mixedMask),
            "actual parser deliberately rejects controller plus OEM mixed domains");
        resetRoute(json::array({mixedDomains})); tick(0,10000);
        check(g_nativeKeyboardShortcuts.empty(), "unsupported mixed input rule never reaches the sampled evaluator");
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
        std::cout << "input shortcut native model/real route: " << passed << " cases passed\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
