// Actual production-TU audit. All native HidHide/source/cycle/observer I/O is
// injected. Logical process restart resets ONLY production in-memory state;
// FakePersistentDriver models state outside the process. No real product,
// driver/registry/Steam/observer, global unhide, OEM action or child is run.
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#include <iostream>
#include "../native/main.cpp"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto output = fspath::absolute(argv[1]);
    if (output.native().find(L"G:\\YeManCC-Work\\_scratch\\msi-hidhide-uninstall-ab-20261012-1504\\focus-audit\\") != 0) return 2;
    g_yemanccDataDir = g_appDataDir = output.native();
    fspath::create_directories(output);
    struct FakePersistentDriver {
        bool cloak = true, inverse = false, readsOk = true, unhideOk = true;
        std::vector<std::wstring> hidden, applications;
    } driver;
    unsigned cases = 0, failures = 0, reads = 0, discoveries = 0, queues = 0;
    unsigned processIncarnation = 0;
    bool ui = true, locationCurrent = true;
    int cycleResult = 0, pollResult = -1;
    const std::string root = "USB\\VID_0DB0&PID_1901\\FAKE-PERSISTENT-OWNER";
    const std::string hid = "HID\\VID_0DB0&PID_1901&IG_00\\FAKE-GAME";
    const std::string xusb = "USB\\VID_0DB0&PID_1901&MI_00\\FAKE-XUSB";
    const std::wstring location = L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    const std::wstring foreign = L"HID\\PREEXISTING-FOREIGN\\DO-NOT-CLAIM";
    const std::vector<std::wstring> ownExpected = {U2W(root), U2W(hid), U2W(xusb)};
    std::vector<std::pair<std::wstring, bool>> writes;
    std::vector<bool> cycles;
    auto check = [&](const char* name, bool ok) {
        ++cases; failures += !ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    };
    auto stillHidden = [&](const std::vector<std::wstring>& paths) {
        return std::all_of(paths.begin(), paths.end(), [&](const auto& p) {
            return hidHideContainsPath(driver.hidden, W2U(p));
        });
    };
    auto allReturned = [&] {
        return std::none_of(ownExpected.begin(), ownExpected.end(), [&](const auto& p) {
            return hidHideContainsPath(driver.hidden, W2U(p));
        });
    };
    auto processStart = [&] {
        ++processIncarnation;
        // New process starts with empty static lease/journal/carried state.
        // Preserve the FakePersistentDriver across these logical incarnations.
        g_machineIdentity.family = YmccFamily::MsiClaw;
        g_machineIdentity.manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product = "MS-1T91";
        g_machineIdentity.deviceClass = "HandheldCompanion.Devices.ClawCG3EM";
        g_machineIdentity.resolved = true;
        g_realStickTest.enabled = true; g_realStickTest.virtualTargetEnabled = false;
        g_realStickTest.virtualTargetPersona = "disabled";
        g_inputHostProcess = nullptr; g_inputHostPrepared = g_inputHostNeutralized = false;
        g_inputHostReleaseQuarantined = false; g_inputHostExitFence = false;
        g_inputHostTransportFault = false;
        g_hidHideVisibilityJournal = HidHideVisibilityJournal{};
        g_hidHideCarriedResponsibility = HidHideCarriedResponsibility{};
        g_xinputPhysicalSourceLocked = false;
        g_xinputConnectedMask = g_xinputPhysicalSourceMask = 0;
        g_inputReady = true; g_powerGeneration = 3; g_powerLifecycle = PowerLifecycle::Ready;
        g_physicalReleaseWindowStage = 0; g_physicalReleaseWindowTransition = 0; g_physicalReleaseWindowNeutral = false;
        g_msiNativeForegroundLease = MsiNativeForegroundLease{};
        g_msiNativeForegroundEdge = 1; g_msiNativeForegroundDesiredUi = false;
        g_msiNativeForegroundSourceRetryNeeded = false; g_msiNativeForegroundSourceRetryGeneration = 0;
        g_msiNativeForegroundCyclePending = false; g_msiNativeForegroundPowerHold = false;
        g_msiNativeForegroundResumePending = false;
        g_msiNativeForegroundObserver = nullptr;
        g_msiNativeForegroundObserverActive = true;
        g_msiNativeForegroundObserverRequested = true; // no real observer request
        g_inputHostLifecycleWorkQueue.clear(); g_inputHostLifecycleWorkRequested = false;
        writes.clear(); cycles.clear(); reads = discoveries = queues = 0;
        ui = locationCurrent = true; cycleResult = 0; pollResult = -1;
    };
    auto freshScenario = [&] {
        driver = FakePersistentDriver{};
        driver.hidden = {foreign}; driver.applications = {hidHideCurrentModulePath()};
        processStart();
    };
    HidHidePhysicalIdentity source;
    source.resolved = true; source.vendorId = 0x0db0; source.productId = 0x1901;
    source.usagePage = 1; source.usage = 5;
    source.deviceInstanceId = hid; source.baseContainerDeviceInstanceId = root;
    g_msiForegroundTestIo = {}; g_msiNativeForegroundTestIo = {};
    g_msiForegroundTestIo.locations = [&](const std::string& r) {
        return locationCurrent && r == root ? std::vector<std::wstring>{location} : std::vector<std::wstring>{};
    };
    g_msiNativeForegroundTestIo.candidates = [&] {
        ++discoveries; return std::vector<HidHidePhysicalIdentity>{source};
    };
    g_msiNativeForegroundTestIo.inventory = [&](const HidHidePhysicalIdentity&) {
        HidHideInterfaceRecord r; r.instanceId = hid; r.baseContainerDeviceInstanceId = root;
        r.vendorId = 0x0db0; r.productId = 0x1901; r.usagePage = 1; r.usage = 5;
        return std::vector<HidHideInterfaceRecord>{r};
    };
    g_msiNativeForegroundTestIo.xusbInventory = [&](const HidHidePhysicalIdentity&) {
        MsiNativeForegroundXusbInventory r; r.complete = true;
        r.interfaces.push_back({true, true, false, xusb, root}); return r;
    };
    g_msiNativeForegroundTestIo.snapshot = [&](HidHideVisibilitySnapshot& s) {
        ++reads; if (!driver.readsOk) return false;
        s.valid = true; s.cloaking = driver.cloak; s.inverseRaw = driver.inverse ? "on" : "off";
        s.hidden = driver.hidden; s.applications = driver.applications; return true;
    };
    g_msiNativeForegroundTestIo.visibility = [&](const std::wstring& p, bool hide) {
        writes.emplace_back(p, hide);
        check("all-native-visibility-writes-are-exact-owned-source-never-foreign", p != foreign && std::find(ownExpected.begin(), ownExpected.end(), p) != ownExpected.end());
        if (!hide && !driver.unhideOk) return false;
        if (hide) {
            if (!hidHideContainsPath(driver.hidden, W2U(p))) driver.hidden.push_back(p);
        } else driver.hidden.erase(std::remove(driver.hidden.begin(), driver.hidden.end(), p), driver.hidden.end());
        return true;
    };
    g_msiNativeForegroundTestIo.cloak = [&](bool on) { driver.cloak = on; return true; };
    g_msiNativeForegroundTestIo.cycle = [&](const std::string& r, const std::wstring& l, bool restoring) {
        check("all-cycles-use-exact-fake-root-location", r == root && l == location);
        cycles.push_back(restoring); return cycleResult;
    };
    g_msiNativeForegroundTestIo.poll = [&] { return pollResult; };
    g_msiNativeForegroundTestIo.ui = [&] { return ui; };
    g_msiNativeForegroundTestIo.queue = [&] { ++queues; };
    g_msiNativeForegroundTestIo.virtualQueue = [&](const std::string&) { check("no-new-virtual-job-in-audit", false); };
    g_msiNativeForegroundTestIo.observerQueue = [&] { check("no-observer-start-or-queue", false); };
    auto enter = [&] { msiNativeForegroundUiEvent(); msiNativeForegroundHandleWork(); };
    auto hideUiBoundary = [&] { ui = false; msiNativeForegroundUiEvent(); msiNativeForegroundHandleWork(); };

    // A: characterize actual OFF admission. Never synthesize a missing lease.
    freshScenario(); enter();
    check("virtual-OFF-current-source-really-creates-native-owned-hide-lease", g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.owned.size() == 3 && stillHidden(ownExpected));
    if (!g_msiNativeForegroundLease.active) {
        std::cout << "ENTRY_ALREADY_CHANGED: this captured source does not reproduce the pre-fix entry; re-audit parent policy instead of inventing permission\n";
        return 1;
    }
    check("native-debt-does-not-enter-existing-virtual-journal-or-carried-responsibility", !g_hidHideVisibilityJournal.active && g_hidHideVisibilityJournal.hiddenByUs.empty() && !g_hidHideCarriedResponsibility.active && g_hidHideCarriedResponsibility.ownedTargets.empty());
    hideUiBoundary();
    check("healthy-doubleB-post-hide-boundary-returns-native-owned-paths", allReturned() && driver.hidden == std::vector<std::wstring>{foreign} && !g_msiNativeForegroundLease.active);
    const auto healthyWrites = writes.size(), healthyCycles = cycles.size();
    for (unsigned i = 0; i < 100; ++i) {
        msiNativeForegroundUiEvent(true); msiNativeForegroundHandleWork();
    }
    check("hidden-ui-force-events-do-not-rehide-after-successful-return", writes.size() == healthyWrites && cycles.size() == healthyCycles && allReturned());

    // B: exitFence drops the non-cleanup queued event, but DIRECT exact return
    // remains allowed. No product exit worker/sweep is executed by this audit.
    freshScenario(); enter();
    g_inputHostLifecycleWorkQueue = {"msi-native-foreground", "virtual-target-disabled"};
    inputHostRaiseExitFence("offline-doubleB-then-app-exit");
    check("fence-drops-native-event-but-keeps-existing-cleanup-work", g_inputHostLifecycleWorkQueue == std::deque<std::string>{"virtual-target-disabled"});
    ui = false; msiNativeForegroundExitCleanup();
    check("raised-exitFence-does-not-skip-direct-native-unhide", allReturned() && !g_msiNativeForegroundLease.active);

    // C: source/physical-location loss is checked AFTER exact old-path unhide.
    freshScenario(); enter(); locationCurrent = false; hideUiBoundary();
    check("source-loss-does-not-skip-old-owned-unhide", allReturned() && !g_msiNativeForegroundLease.active && cycles.size() == 1);

    // D: pending hide helper permits config return and retains helper debt.
    freshScenario(); cycleResult = 1; enter();
    ui = false; inputHostRaiseExitFence("offline-pending-helper-exit");
    msiNativeForegroundExitCleanup();
    check("pending-helper-does-not-strand-owned-hidden-config", allReturned() && g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.configurationReturned && g_msiNativeForegroundCyclePending);

    // E: an actual failed unhide at UI hide/exit retains exact debt. Logical
    // abrupt process termination then loses ONLY memory, not fake driver state.
    freshScenario(); enter(); driver.unhideOk = false; hideUiBoundary();
    check("failed-doubleB-boundary-return-keeps-owned-debt-and-hidden-config", g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.restorePending && !g_msiNativeForegroundLease.configurationReturned && stillHidden(ownExpected));
    inputHostRaiseExitFence("offline-unhide-fails-on-exit"); msiNativeForegroundExitCleanup();
    check("failed-direct-exit-return-is-not-cleared-as-success", g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.owned.size() == 3 && stillHidden(ownExpected));
    const auto persistentHidden = driver.hidden, persistentApplications = driver.applications;
    const auto firstIncarnation = processIncarnation;
    processStart(); ui = false; driver.unhideOk = true;
    check("second-process-has-no-native-journal-or-carried-owner", processIncarnation == firstIncarnation + 1 && !g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.owned.empty() && !g_hidHideVisibilityJournal.active && !g_hidHideCarriedResponsibility.active);
    check("second-process-keeps-fake-external-hidden-cloak-exempt-state", driver.hidden == persistentHidden && driver.applications == persistentApplications && driver.cloak && !driver.inverse);
    msiNativeForegroundUiEvent(true); msiNativeForegroundHandleWork(); msiNativeForegroundExitCleanup();
    check("background-new-process-empty-native-exit-does-not-recover-lost-owner", writes.empty() && reads == 0 && stillHidden(ownExpected));
    // With absent endpoint/source proof, this actual shared restore takes its
    // no-journal branch. It must not call the global sweep or real CLI.
    check("guarded-shared-restore-has-no-endpoint-proof-so-no-global-sweep", !hidHideVirtualEndpointAbsent(g_xinputPhysicalSourceLocked,g_xinputConnectedMask,g_xinputPhysicalSourceMask));
    const bool virtualReturn = hidHideRestorePhysicalTransaction("offline-restarted-native-no-journal");
    check("existing-virtual-restore-no-journal-returns-true-with-lost-native-paths-still-hidden", virtualReturn && stillHidden(ownExpected) && writes.empty() && !g_hidHideVisibilityJournal.active);
    ui = true; msiNativeForegroundUiEvent(); msiNativeForegroundHandleWork();
    check("second-process-reentry-does-not-reclaim-previously-owned-prehidden-paths", g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.owned.empty() && writes.empty() && stillHidden(ownExpected));
    hideUiBoundary();
    check("second-process-successful-own-restore-does-not-release-first-process-orphan", !g_msiNativeForegroundLease.active && stillHidden(ownExpected) && writes.empty());
    check("all-scenarios-preserve-unknown-foreign-preexisting-entry", hidHideContainsPath(driver.hidden,W2U(foreign)));

    // F: no failure or B callback required: abrupt termination after an entry
    // has the same owner-loss result. This is not a claim normal exit leaked.
    freshScenario(); enter(); const auto crashHidden = driver.hidden;
    processStart(); ui = false;
    msiNativeForegroundHandleWork(); msiNativeForegroundExitCleanup();
    check("abrupt-restart-after-valid-hide-loses-owner-while-config-survives", driver.hidden == crashHidden && stillHidden(ownExpected) && g_msiNativeForegroundLease.owned.empty() && writes.empty());

    // G: a previously failed B return is recoverable while THIS owner lives.
    freshScenario(); enter(); driver.unhideOk = false; hideUiBoundary();
    driver.unhideOk = true; inputHostRaiseExitFence("offline-retry-with-owner-still-live");
    msiNativeForegroundExitCleanup();
    check("failed-B-return-followed-by-working-exit-restores-this-live-owner", allReturned() && !g_msiNativeForegroundLease.active && driver.hidden == std::vector<std::wstring>{foreign});
    g_msiNativeForegroundTestIo = {}; g_msiForegroundTestIo = {};
    std::cout << "MSI_ORPHAN_OWNER_AUDIT cases=" << cases << " failures=" << failures
        << " actual-TU all-native-I/O-fake logical-two-process-restart no-real-startup no-global-sweep\n";
    return failures ? 1 : 0;
}
