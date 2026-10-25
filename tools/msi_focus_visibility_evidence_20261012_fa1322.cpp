// Offline-only production-TU regression. Never calls product startup, a real
// WinEvent hook, PnP/device/driver observer, InputHost, Steam or OEM framework.
#define YMCC_MSI_FOREGROUND_ISOLATION_TEST 1
#include <iostream>
#include "../native/main.cpp"
#include "../native/msi_focus_visibility_evidence_20261012_fa1322.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto output = fspath::absolute(argv[1]);
    // All logs produced by the real logger stay in the caller's scratch job.
    if (output.native().find(L"G:\\YeManCC-Work\\_scratch\\") != 0 ||
        output.native().find(L"focus-agent") == std::wstring::npos) return 2;
    g_yemanccDataDir = g_appDataDir = output.native();
    fspath::create_directories(output);
    using namespace ymcc::msi::focus_evidence_20261012_fa1322;
    unsigned cases = 0, failures = 0, reads = 0, queues = 0;
    auto check = [&](const char* name, bool ok) {
        ++cases; failures += !ok; std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
    };
    const std::string root = "USB\\VID_0DB0&PID_1901\\OFFLINE-FOCUS-BOOT";
    const std::string hid = "HID\\VID_0DB0&PID_1901&IG_00\\OFFLINE-GAME";
    const std::string xusb = "USB\\VID_0DB0&PID_1901&MI_00\\OFFLINE-XUSB";
    const std::wstring location = L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)";
    const std::wstring foreign = L"HID\\FOREIGN-OWNER\\DO-NOT-CLAIM";
    const std::vector<std::wstring> selected = {U2W(root), U2W(hid), U2W(xusb)};
    std::vector<std::wstring> hidden;
    std::vector<std::pair<std::wstring, bool>> writes;
    std::vector<bool> cycles;
    bool ui = true, cloak = true, readOk = true;
    HidHidePhysicalIdentity source;
    source.resolved = true; source.vendorId = 0x0db0; source.productId = 0x1901;
    source.usagePage = 1; source.usage = 5;
    source.deviceInstanceId = hid; source.baseContainerDeviceInstanceId = root;
    auto reset = [&] {
        g_machineIdentity.family = YmccFamily::MsiClaw;
        g_machineIdentity.manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product = "MS-1T91"; g_machineIdentity.resolved = true;
        g_machineIdentity.deviceClass = "HandheldCompanion.Devices.ClawCG3EM";
        g_realStickTest.enabled = true; g_realStickTest.virtualTargetEnabled = false;
        g_realStickTest.virtualTargetPersona = "disabled";
        g_inputHostProcess = nullptr; g_inputHostReleaseQuarantined = false; g_inputHostExitFence = false;
        g_hidHideVisibilityJournal = HidHideVisibilityJournal{};
        g_inputReady = true; g_powerGeneration = 3; g_powerLifecycle = PowerLifecycle::Ready;
        g_physicalReleaseWindowStage = 0; g_physicalReleaseWindowTransition = 0; g_physicalReleaseWindowNeutral = false;
        g_msiNativeForegroundLease = MsiNativeForegroundLease{};
        g_msiNativeForegroundEdge = 1; g_msiNativeForegroundDesiredUi = false;
        g_msiNativeForegroundSourceRetryNeeded = false; g_msiNativeForegroundSourceRetryGeneration = 0;
        g_msiNativeForegroundCyclePending = false; g_msiNativeForegroundPowerHold = false;
        g_msiNativeForegroundResumePending = false; g_msiNativeForegroundObserverActive = true;
        // IdentityReady returns without queuing/starting a real observer.
        g_msiNativeForegroundObserverRequested = true;
        hidden = {foreign}; writes.clear(); cycles.clear(); reads = queues = 0;
        ui = cloak = readOk = true;
    };
    g_msiForegroundTestIo = {}; g_msiNativeForegroundTestIo = {};
    g_msiForegroundTestIo.locations = [&](const std::string& r) {
        return r == root ? std::vector<std::wstring>{location} : std::vector<std::wstring>{};
    };
    g_msiNativeForegroundTestIo.candidates = [&] { return std::vector<HidHidePhysicalIdentity>{source}; };
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
        ++reads; if (!readOk) return false;
        s.valid = true; s.cloaking = cloak; s.inverseRaw = "off";
        s.hidden = hidden; s.applications = {hidHideCurrentModulePath()}; return true;
    };
    g_msiNativeForegroundTestIo.visibility = [&](const std::wstring& path, bool hide) {
        writes.emplace_back(path, hide);
        if (hide) hidden.push_back(path);
        else hidden.erase(std::remove(hidden.begin(), hidden.end(), path), hidden.end());
        return true;
    };
    g_msiNativeForegroundTestIo.cloak = [&](bool on) { cloak = on; return true; };
    g_msiNativeForegroundTestIo.cycle = [&](const std::string& r, const std::wstring& l, bool restoring) {
        check("cycle-is-exact-fake-root-and-location", r == root && l == location);
        cycles.push_back(restoring); return 0;
    };
    g_msiNativeForegroundTestIo.ui = [&] { return ui; };
    g_msiNativeForegroundTestIo.queue = [&] { ++queues; };
    g_msiNativeForegroundTestIo.virtualQueue = [&](const std::string&) { check("no-virtual-work", false); };
    g_msiNativeForegroundTestIo.observerQueue = [&] { check("no-real-observer-queue", false); };
    auto enter = [&] { msiNativeForegroundUiEvent(); msiNativeForegroundHandleWork(); };
    auto leave = [&] { ui = false; msiNativeForegroundUiEvent(); msiNativeForegroundHandleWork(); };
    auto evidence = [&](const std::vector<std::wstring>& owned) {
        Facts f; f.snapshotRead = f.selectedPathsComplete = f.cloakKnown = f.inverseKnown = true;
        f.readerRegistrationKnown = f.ymccReaderExempt = true; f.cloakOn = cloak;
        f.selectedPaths = selected; f.hiddenPaths = hidden; f.ownedPaths = owned;
        return decode(f);
    };
    reset(); ui = false; enter();
    check("before-ymcc-ui-ownership-no-hide-no-cycle-no-read", writes.empty() && cycles.empty() && reads == 0);
    reset(); enter();
    check("native-off-no-driver-borrow-read-hide-or-cycle", writes.empty() && cycles.empty() && reads == 0 && !g_msiNativeForegroundLease.active);
    leave();
    check("double-B-equivalent-ui-exit-keeps-native-steam-config", hidden == std::vector<std::wstring>{foreign} && writes.empty() && cycles.empty() && reads == 0);
    reset(); hidden.insert(hidden.end(), selected.begin(), selected.end()); enter(); leave(); auto e=evidence({});
    check("native-off-does-not-adopt-or-clear-unknown-inherited-selected-paths", writes.empty() && cycles.empty() && reads == 0 && hidden.size() == 4);
    check("inherited-config-is-not-misreported-as-external-input-proof", e.unownedSelectedHiddenCount == 3 && !e.externalInputDeliveryProven && !e.inheritedEntryOwnershipProven);
    reset();
    const auto seedLegacyOwned=[&] {
        auto& lease=g_msiNativeForegroundLease; lease.active=true;lease.root=root;lease.locations={location};
        lease.owned=selected;lease.power=g_powerGeneration.load();lease.edge=g_msiNativeForegroundEdge.load();
        lease.before.valid=true;lease.before.cloaking=true;lease.before.inverseRaw="off";
        lease.before.hidden={foreign};lease.before.applications={hidHideCurrentModulePath()};
        lease.diagnosticSelectedPaths=selected;lease.diagnosticSelectedPathsComplete=true;
        hidden={foreign};hidden.insert(hidden.end(),selected.begin(),selected.end());cloak=true;
    };
    seedLegacyOwned();auto owned=g_msiNativeForegroundLease.owned;leave();e=evidence(owned);
    check("retired-route-still-returns-EXACT-old-owned-paths", hidden == std::vector<std::wstring>{foreign} && !g_msiNativeForegroundLease.active && writes.size()==3 && std::all_of(writes.begin(),writes.end(),[](const auto& w){return !w.second;}));
    check("legacy-return-no-new-hide-and-only-restore-refresh", cycles==std::vector<bool>{true});
    check("returned-config-still-not-Steam-consumption-proof", e.ownedEntriesReturned==Knowledge::Yes && !e.externalInputDeliveryProven);
    reset();seedLegacyOwned();readOk=false;leave();
    check("failed-legacy-return-keeps-owned-debt-does-not-broaden", g_msiNativeForegroundLease.active && g_msiNativeForegroundLease.owned==selected && hidden.size()==4 && writes.empty());
    readOk=true;msiNativeForegroundHandleWork();
    check("legacy-return-retry-only-exact-owned-entries", hidden==std::vector<std::wstring>{foreign} && !g_msiNativeForegroundLease.active);
    reset();ui=true;enter();ui=false;enter();ui=true;enter();ui=false;enter();
    check("repeated-summon-double-B-style-boundaries-never-hide-or-cycle", writes.empty() && cycles.empty() && reads==0 && !g_msiNativeForegroundLease.active);
    reset();g_msiNativeForegroundObserverRequested=false;msiNativeForegroundIdentityReady();
    check("retired-native-off-starts-no-new-WinEvent-observer", !g_msiNativeForegroundObserverRequested.load());
    for(unsigned mask=0;mask<8;++mask){
        const bool known=(mask&1)!=0,wanted=(mask&2)!=0,native=(mask&4)!=0;
        check("resident-cloak-skip-is-only-known-MSI-native-OFF", ymcc::msi::nativeOffShouldSkipResidentCloak(known,wanted,native)==(mask==5));
    }
    reset();g_domainLogEnabled[1]=true;g_msiNativeOffFilterAuditEdge=0;g_msiNativeOffFilterAuditReads=0;
    std::vector<json> audits;
    g_msiNativeForegroundTestIo.readOnlyAuditLog=[&](const json& row){audits.push_back(row);};
    enter();leave();
    check("explicit-detail-only-audits-native-summon-and-return-filter-state", reads==2 && audits.size()==2 && audits[0]["uiOwned"]==true && audits[1]["uiOwned"]==false);
    check("filter-audit-has-real-config-and-no-fabricated-owner-or-delivery", audits[1]["readOnly"]==true && audits[1]["snapshotRead"]==true && audits[1]["cloakOn"]==true && audits[1]["inheritedOwnership"]=="unknown-do-not-adopt-or-clear");
    check("filter-audit-never-hides-clears-cloaks-or-cycles",writes.empty() && cycles.empty() && hidden==std::vector<std::wstring>{foreign});
    const auto readAfterBoundary=reads;msiNativeForegroundHandleWork();
    check("unchanged-edge-does-not-poll-filter",reads==readAfterBoundary);
    readOk=false;msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();
    check("audit-query-failure-is-explicit-unknown-and-control-unchanged",audits.back()["snapshotRead"]==false && audits.back()["cloakOn"].is_null() && writes.empty() && cycles.empty());
    readOk=true;g_msiNativeForegroundTestIo.readOnlyAuditLog=[](const json&){throw 42;};
    msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();
    check("audit-log-exception-does-not-alter-native-physical-filter",writes.empty() && cycles.empty() && hidden==std::vector<std::wstring>{foreign});
    for(unsigned i=0;i<50;++i){msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();}
    check("audit-hard-cap-is-32-boundary-snapshots-no-resident-poller",g_msiNativeOffFilterAuditReads==32 && reads==32 && writes.empty() && cycles.empty());
    g_domainLogEnabled[1]=false;g_msiNativeForegroundTestIo.readOnlyAuditLog={};
    reset(); g_machineIdentity.family = YmccFamily::AsusRogAllyClassic; enter();
    check("native-foreground-hide-is-msi-only-not-common", writes.empty() && cycles.empty() && reads == 0 && queues == 0);
    reset(); g_machineIdentity.product = "UNKNOWN"; enter();
    check("unknown-board-does-not-gain-focus-or-source-permission", writes.empty() && cycles.empty() && reads == 0);
    reset(); readOk = false; enter();
    check("failed-snapshot-does-not-hide-or-cycle", writes.empty() && cycles.empty());
    Facts f; check("missing-snapshot-remains-unknown", decode(f).ownedEntriesReturned == Knowledge::Unknown);
    f.snapshotRead = f.selectedPathsComplete = f.cloakKnown = true; f.cloakOn = true;
    f.selectedPaths = selected; f.hiddenPaths = {selected[2]};
    check("inverse-unknown-is-not-assumed-off", decode(f).selectedConfiguration == SelectedConfiguration::Unknown);
    f.inverseKnown = f.inverseOn = true;
    check("inverse-on-does-not-invent-normal-allowlist-proof", decode(f).selectedConfiguration == SelectedConfiguration::Unknown);
    f.cloakOn = false;
    check("cloak-off-means-config-not-filtering-not-hardware-delivery", decode(f).selectedConfiguration == SelectedConfiguration::NoFilterForSelectedPaths && !decode(f).externalInputDeliveryProven);
    f.cloakOn = true; f.inverseOn = false; f.selectedPathsComplete = false;
    check("partial-source-contract-remains-unknown", decode(f).selectedConfiguration == SelectedConfiguration::Unknown);
    f.selectedPathsComplete = true; f.selectedPaths = {L"hid\\case", L"HID\\CASE", L""}; f.hiddenPaths = {L"HID\\CASE"};
    check("casefold-deduplicates-only-exact-paths", decode(f).selectedHiddenCount == 1);
    f.ownedPaths = {L"HID\\CASE"};
    check("own-still-hidden-not-reported-returned", decode(f).ownedEntriesReturned == Knowledge::No && decode(f).unownedSelectedHiddenCount == 0);
    // Neutral assembly is the CURRENT production implementation, not F labels.
    auto frame = focusIsolationAssembleFrame(focusIsolationNeutralContent());
    check("semantic-neutral-zeroes-raw-stick-buttons-triggers", frame["buttons"] == 0 && frame["backButtons"] == 0 && frame["raw"]["sThumbRX"] == 0 && frame["raw"]["bLeftTrigger"] == 0);
    check("semantic-neutral-zeroes-imu-and-guide-auxiliary-output", !frame["imu"]["admitted"].get<bool>() && frame["imu"]["gyroDps"]["x"] == 0.0 && frame["imu"]["quaternion"]["w"] == 1.0 && !frame.contains("touchpads") && !frame.contains("screenButtons"));
    check("archived-common-isolation-stays-disabled", !g_focusIsolationEnabled.load() && !focusIsolationExternalInjectionBlocked());
    // Default summon behavior must remain unchanged, including MSI G3E.
    // Exercise only the real predicate; no chord, WMI or custom action is dispatched.
    const auto savedLegacyOverride = g_nativeShortcutLegacyOverrideMask;
    g_nativeShortcutLegacyOverrideMask = 0;
    reset();
    check("default-ClawCG3EM-MS1T91-legacy-shoulder-summon-preserved", nativeShortcutLegacyDefaultAllowed("enabled"));
    check("default-no-reservation-B-close-or-other-defaults", nativeShortcutLegacyDefaultAllowed("bDoubleMinimize") && nativeShortcutLegacyDefaultAllowed("tdpShortcut"));
    g_machineIdentity.product = "MS-1T52";
    check("default-preserved-for-another-known-MSI-board", nativeShortcutLegacyDefaultAllowed("enabled"));
    g_machineIdentity.product = "MS-1T91";
    g_machineIdentity.deviceClass = "HandheldCompanion.Devices.OtherClaw";
    check("default-preserved-for-another-device-class", nativeShortcutLegacyDefaultAllowed("enabled"));
    reset(); g_machineIdentity.family = YmccFamily::AsusRogAllyClassic;
    check("default-no-reservation-another-family", nativeShortcutLegacyDefaultAllowed("enabled"));
    g_nativeShortcutLegacyOverrideMask = 1u << nativeShortcutLegacyDefaultSlot("enabled");
    check("default-preserves-custom-rule-legacy-override-mask", !nativeShortcutLegacyDefaultAllowed("enabled"));
    g_nativeShortcutLegacyOverrideMask = savedLegacyOverride;
    g_msiNativeForegroundTestIo = {}; g_msiForegroundTestIo = {};
    // Parent runtime integration decodes captured config without claiming that
    // returning own entries clears inherited XUSB paths or proves Steam delivery.
    HidHideVisibilitySnapshot diagnosticSnapshot;
    diagnosticSnapshot.valid=true;diagnosticSnapshot.cloaking=true;diagnosticSnapshot.inverseRaw="off";
    diagnosticSnapshot.applications={hidHideCurrentModulePath()};
    diagnosticSnapshot.hidden={selected[2]};
    const auto integrated=msiNativeForegroundConfigurationEvidence(diagnosticSnapshot,selected,true,{});
    check("integrated-current-snapshot-inherited-XUSB-remains-explicit",integrated["unownedSelectedHiddenCount"]==1 &&
        integrated["selectedConfiguration"]=="hidden-from-unlisted-readers");
    check("integrated-config-return-never-external-input-proof",integrated["externalInputDeliveryProven"]==false &&
        integrated["inheritedEntryOwnershipProven"]==false);
    check("integrated-exempt-YMCC-read-is-separate-from-Steam",integrated["ymccExemptReaderCanBypassConfiguration"]=="yes");
    diagnosticSnapshot.valid=false;
    const auto unknownDiagnostic=msiNativeForegroundConfigurationEvidence(diagnosticSnapshot,selected,true,{});
    check("integrated-invalid-snapshot-remains-unknown",unknownDiagnostic["selectedConfiguration"]=="unknown");
    std::cout << "FOCUS_BOOT_EVIDENCE cases=" << cases << " failures=" << failures << " fake-only no-real-observers no-product-startup\n";
    return failures ? 1 : 0;
}
