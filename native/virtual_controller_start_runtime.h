#pragma once
#include "virtual_controller_runtime_contract.h"

// VC-LIFECYCLE-20261020-R3 — production definition module.
// Actual target Start and its existing test terminals. The R3 player period is authoritative; all cleanup, admission, deadline and source receipts retain their original order.
// Shared enable/UI-output timeline: virtual_controller_runtime_contract.h.
// Navigation and capability boundaries: virtual_controller_runtime.md.
// This is compiled production code; the reference comments do not execute.
// Maintenance boundary: this is the only Start body; do not copy it for wake or self-healing.
// Sequence: original request/deadline -> retained old return -> source admission -> HELLO.
// MSI may await its exact source inside this HELLO-only epoch, before hide proof /
// PREPARE / NEUTRAL. Execution inspection still proves current-source submission.

// ═══════════════════════════════════════════════════════════════════════════
// GP-MSIG3E-6-LC-R2 §四 R1：inputHostStart 生产入口测试缝辅助。
// 生产构建下全部退化为对真实实现的一次直接调用（ymStartSeamActive() 恒 false）。
// **不复制排序逻辑**：顺序完全由 inputHostStart 自身的调用序列决定，测试只观察轨迹。
// ═══════════════════════════════════════════════════════════════════════════
static bool ymStartAssetsPresent() {
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive()) {
        ymStartSeamTrace("assets");
        return g_startEntrySeam.assetsPresent;
    }
#endif
    return inputCaptureAssetsPresent();
}

static void ymStartEnsureRoute(const char* label) {
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive()) {
        // 负控 mutation=2：抑制主路径 Ensure（必须导致顺序测试 FAIL）。
        if (g_startEntrySeam.mutation == 2 && std::strcmp(label, "main") == 0) {
            ymStartSeamTrace("ensure:main-suppressed-mutation");
            return;
        }
        msiClawEnsureDInputRoute();
        ymStartSeamTrace(std::string("ensure:") + label);
        // 模拟生产"首个有效样本 ⇒ 路线确认"的异步落点（测试用确定性替代）。
        if (g_startEntrySeam.confirmRouteAfterEnsure &&
            g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)) {
            g_msiDinputRouteConfirmed.store(true, std::memory_order_release);
            // R10-R2 窄范围 C：同一落点一并建立主源身份三项，使 admit 用例在 inputHostStopLocked
            // 撤销身份后仍能经真实 Ensure 重建 eligible（armed 门保证非 Claw / 未授权板号不伪造）。
            g_msiDinputSourceValid.store(true, std::memory_order_release);
            g_msiDinput.mainInterfaceMatched.store(true, std::memory_order_release);
            g_msiDinput.productId.store(static_cast<uint32_t>(kMsiClawCapability.pidDInput),
                                        std::memory_order_release);
        }
        return;
    }
#endif
    msiClawEnsureDInputRoute();
}

static bool ymStartAdmitSource() {
    const bool admitted = inputHostAdmitSelectedPhysicalSource();
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive()) {
        g_startEntrySeam.admitResult = admitted;
        g_startEntrySeam.armedAtAdmit = g_msiDinputRouteDinputArmed.load(std::memory_order_acquire);
        ymStartSeamTrace(admitted ? "admit:true" : "admit:false");
    }
#endif
    return admitted;
}

static bool ymStartExecutablePresent(const std::wstring& exe) {
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive()) {
        ymStartSeamTrace("exe-check");
        return g_startEntrySeam.executablePresent;
    }
#endif
    return GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static BOOL ymStartCreateProcessW(std::vector<wchar_t>& buf, const std::wstring& hostDir,
                                  STARTUPINFOW& si, PROCESS_INFORMATION& pi) {
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive()) {
        // 假子进程：两个可关闭事件句柄。production 会在 CloseHandle(pi.hThread) 与 stop 的
        // CloseHandle(g_inputHostProcess) 各自关闭一个 ⇒ 不泄漏、不双关。
        pi.hThread = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pi.hProcess = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pi.dwProcessId = GetCurrentProcessId();
        pi.dwThreadId = GetCurrentThreadId();
        ymStartSeamTrace("create-process");
        return (pi.hThread && pi.hProcess) ? TRUE : FALSE;
    }
#endif
    return ymcc::processEfficiency::createProcess(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, hostDir.c_str(), &si, &pi);
}

// A start can fail after taking physical mode/visibility ownership or creating
// a Host. Unwinding must not depend on the UI still being in startup Waiting.
// Run on the existing lifecycle owner while its Host transaction lock is held.
struct InputHostStartExceptionCleanup {
    int exceptions = std::uncaught_exceptions();
    ~InputHostStartExceptionCleanup() noexcept {
        if (std::uncaught_exceptions() <= exceptions) return;
        const auto ownsIncompleteStart = []() noexcept {
            const bool owned = g_inputHostProcess || g_inputHostPrepared ||
                g_hidHideVisibilityJournal.active || g_inputHostReleaseQuarantined ||
                g_msiDinputRouteDinputArmed.load(std::memory_order_acquire);
            const bool ready = g_inputHostProcess && g_inputHostPrepared &&
                g_inputHostNeutralized && !g_inputHostTransportFault &&
                !g_inputHostReleaseQuarantined;
            return owned && !ready;
        };
        // An optional final diagnostic failure must not tear down a target
        // whose prepare and neutral receipts were already accepted.
        if (!ownsIncompleteStart()) return;
        g_inputHostFaultHoldsStart.store(true, std::memory_order_release);
        try {
            inputHostStopLocked("fault-stop:partial-start-exception");
        } catch (...) {
            // Keep the exact owner/history, never claim a release on failure.
            g_inputHostReleaseQuarantined = true;
            g_inputHostStartBlocked = true;
        }
        if (ownsIncompleteStart()) {
            try { inputHostQueueLifecycleWork("fault-stop:partial-start-exception"); }
            catch (...) { /* retained ownership and start block remain authoritative */ }
        }
    }
};

// Normal delayed return shares the central cycle; it does not consume fallback.
#include "virtual_switch_execution_return_runtime.h"

// VC-MSI-EXCLUSIVE-HANDOFF-20261024-R1. HELLO owns no virtual endpoint.
// Keep the existing epoch and source retry owner alive, but never PREPARE an
// MSI target through the generic HID-only/no-source visibility exception.
// This runs on the same serialized lifecycle owner. Only an ALREADY queued
// mode2 ticket for this epoch may progress inside the wait; its normal strict
// consumer rechecks every generation. No new writes, retries or deadlines.
static bool ymStartAwaitMsiSource(std::uint64_t ticket, bool& admitted, std::unique_lock<InputHostMutex>& hostLock) {
    const auto began=inputHostStartProtectionNow();
    const auto epoch=g_inputHostEpoch;
    const auto process=g_inputHostProcess;
    const auto runId=g_inputHostRunId;
    const auto power=currentPowerGeneration();
    const auto intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    const auto persona=inputHostBuildPersonaSnapshot();
    admitted=false;
    unsigned checks=0, serviced=0;
    const char* reason="original-request-not-current";
    for (;;) {
        const auto now=inputHostStartProtectionNow();
        const auto request=g_virtualStartProtection.view(now);
        if (request.requestId!=ticket) break;
        // The original R3 also authorizes classified fallback while verifying.
        // A true wake may restore an already-completed player's target, but only
        // its actual active, generation-bound execution grants that old ticket
        // a separate remaining power budget. A generic TLS deadline is no proof.
        const bool playerExecution=request.persona==persona &&
            g_virtualStartProtection.canContinueExecution(ticket,persona,now);
        auto remaining=playerExecution?request.remainingMs:std::uint64_t{0};
        if(!remaining)remaining=inputHostWakeMsiSourceWaitRemaining(ticket,persona,now);
        if(!remaining)remaining=inputHostMsiPowerWorkRemaining(ticket,persona,now);
        if(!remaining)break;
        if(g_inputHostSwitchRecoveryStartDeadline) {
            if(now>=g_inputHostSwitchRecoveryStartDeadline)break;
            remaining=(std::min)(remaining,g_inputHostSwitchRecoveryStartDeadline-now);
        }
        if (inputHostExitFenceRaised() || g_exitRequested.load(std::memory_order_acquire) ||
                g_inputCaptureStop.load(std::memory_order_acquire)) {reason="exit-or-capture-stop";break;}
        if (!hardwareWriteAllowed() || currentPowerGeneration()!=power ||
                g_inputHostBoundPowerGeneration!=power) {reason="power-context-changed";break;}
        if (g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) ||
                intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) ||
                inputHostConfiguredPersona()!=persona || !g_realStickTest.virtualTargetEnabled ||
                !g_inputHostProcess || g_inputHostProcess!=process || g_inputHostEpoch!=epoch ||
                g_inputHostRunId!=runId || WaitForSingleObject(process,0)!=WAIT_TIMEOUT) {reason="intent-or-host-context-changed";break;}
        if (!msiControllerRepairScope()) {reason="msi-route-retired";break;}
        ++checks;
        admitted=ymStartAdmitSource();
        if (admitted) {reason="current-msi-source-admitted";break;}
        if(checks==1)appendNativeLifecycleLog("input-host-msi-source-before-target",{
            {"stage","waiting"},{"policy","VC-MSI-EXCLUSIVE-HANDOFF-20261024-R1"},
            {"requestId",ticket},{"epoch",epoch},{"remainingMs",remaining},{"playerExecution",playerExecution},
            {"targetPrepared",false},{"newModeIntent",false}});
        std::string queuedMode;
        {
            std::lock_guard<std::mutex> queue(g_inputHostLifecycleWorkMx);
            for (auto it=g_inputHostLifecycleWorkQueue.begin();it!=g_inputHostLifecycleWorkQueue.end();++it) {
                ymcc::msi::modeTicket::Ticket mode;
                if (ymcc::msi::modeTicket::parse(*it,mode) && mode.dinput && mode.host==epoch) {
                    queuedMode=*it;
                    g_inputHostLifecycleWorkQueue.erase(it);
                    g_inputHostLifecycleWorkRequested.store(!g_inputHostLifecycleWorkQueue.empty(),std::memory_order_release);
                    break;
                }
            }
        }
        if (!queuedMode.empty()) {msiDinputHandleModeWriteIntent(queuedMode);++serviced;}
        // Source publication is pump-owned; never take its mutex here. This is
        // the existing route poll cadence, not a new high-frequency observer.
        const auto after=inputHostStartProtectionNow();
        if (after<now || after-now>=remaining) {reason="original-request-deadline";break;}
        // BUS connection edges can read the authoritative Host before reaching
        // the MSI pump. Yield only the Host mutex, retaining this serialized Start
        // owner, then revalidate the complete original context before any action.
        const auto cadence=g_inputCaptureOwnerDispatchesBus
            ? static_cast<std::uint64_t>(kInputCaptureGamepadPublishIntervalMs) : kMsiDinputRoutePollMs;
        hostLock.unlock();
        try {
            Sleep(static_cast<DWORD>(std::min<std::uint64_t>(cadence,remaining-(after-now))));
            // A waitable-timer fallback has no concurrent BUS callback. Let its
            // existing sole owner run one ordinary tick, never a second reader.
            if(g_inputCaptureOwnerDispatchesBus && !g_inputCaptureStop.load(std::memory_order_acquire) &&
                    !inputHostExitFenceRaised() && !g_exitRequested.load(std::memory_order_acquire))
                inputCaptureOwnerFallbackBusTick();
        } catch(...) {hostLock.lock();throw;}
        hostLock.lock();
    }
    appendNativeLifecycleLog("input-host-msi-source-before-target",{
        {"policy","VC-MSI-EXCLUSIVE-HANDOFF-20261024-R1"},{"requestId",ticket},
        {"epoch",epoch},{"powerGeneration",power},{"intentRevision",intent},
        {"stage","result"},{"admitted",admitted},{"reason",reason},{"checks",checks},{"existingModeTicketsServiced",serviced},
        {"elapsedMs",inputHostStartProtectionNow()-began},{"targetPrepared",false},
        {"originalDeadlineRetained",true},{"newModeIntent",false},{"sourceNeutralGateChanged",false}});
    return admitted;
}

static bool inputHostStart() {
    if (g_inputHostSwitchRecoveryStartDeadline &&
            inputHostStartProtectionNow()>=g_inputHostSwitchRecoveryStartDeadline) return false;
    // Reject retired/unknown/disabled personas before observers, routes or Host creation.
    const std::string requestedPersona = inputHostConfiguredPersona();
    if (!ymcc::controllerPersona::currentOutput(requestedPersona)) return false;
    if (const char* reason = inputHostMachineTargetRejectionReason(requestedPersona)) {
        appendNativeLifecycleLog("input-host-start-blocked", {
            {"reason", reason}, {"requestedPersona", requestedPersona},
            {"newHostCreated", false}, {"explicitIntentPreserved", true}});
        return false;
    }
    if (g_externalPriorityVirtualSuspended.load(std::memory_order_acquire)) return false;
    if (g_gamepadDriverSetup.active() || !ymcc::moduleMaintenance::available() || !inputModulePayloadCurrent()) {
        g_inputModuleReplayPending.store(true,std::memory_order_release);
        appendNativeLifecycleLog("input-host-start-blocked", {{"reason","input-module-update-pending"},{"newHostCreated",false}});
        return false;
    }
    msiControllerVisibilityObserverStart();
    // GP-PREREQ-4: keep preparation diagnostics without rejecting a target attempt.
    if (g_gamepadDriverSetup.warnsTargets()) {
        appendNativeLifecycleLog("gamepad-driver-setup", {{"phase", "target-attempt"},
            {"state", (int)g_gamepadDriverSetup.state()}, {"result", g_gamepadDriverSetup.result()},
            {"allowsTargetAttempt", true}});
    }
    // 2026-09-23 W3 批 103（D-27 终态闸门）：终态后不得新建 Host / 新 epoch / PREPARE_TARGET /
    // persona 重建（现场形状：app-exit → 新 epoch → initial-target-admission → 再次 hide）。
    // 拒绝在锁外（日志 IO 不进 g_inputHostMx 临界区）。
    if (inputHostExitFenceRaised()) {
        inputHostExitFenceLogRefusal("input-host-start", "input-host-start");
        return false;
    }
    // GP-XBOX-2（裁决 §4）：建塔全程（HELLO→hide 事务→PREPARE_TARGET→SUBMIT_NEUTRAL）
    // 属于生命周期忙段——17:44 的 PREPARE_TARGET 正是在此段被误杀。
    RecoveryStateBusyGuard recoveryBusyGuard("input-host-start");
    recoverySnapNotePhase("input-host-start");
    std::unique_lock<InputHostMutex> lock(g_inputHostMx);
    if (requestedPersona != inputHostConfiguredPersona() || !g_realStickTest.virtualTargetEnabled) return false;
    if (g_inputHostSwitchRecoveryStartRequestId &&
            (g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId!=g_inputHostSwitchRecoveryStartRequestId ||
             currentPowerGeneration()!=g_inputHostSwitchRecoveryStartPower ||
             inputHostStartProtectionNow()>=g_inputHostSwitchRecoveryStartDeadline)) return false;
    const auto entryClock=inputHostStartProtectionNow();
    const auto entryView=g_virtualStartProtection.view(entryClock);
    // Real bound power execution keeps its captured player identity, including
    // initial 0. It may not Consume a new player request to make source waiting
    // look authorized. Protected power work cannot borrow a longer wake budget.
    bool activeMsiWake=false,activeMsiPowerWork=false;
    const auto wakeRemaining=inputHostWakeMsiSourceWaitRemaining(entryView.requestId,requestedPersona,entryClock,&activeMsiWake);
    const auto powerRemaining=inputHostMsiPowerWorkRemaining(entryView.requestId,requestedPersona,entryClock,&activeMsiPowerWork);
    const bool boundMsiPower=wakeRemaining || powerRemaining;
    if((activeMsiWake || activeMsiPowerWork) && !boundMsiPower)return false;
    auto requestTicket = boundMsiPower?entryView.requestId:
        g_virtualStartProtection.existingTicket(requestedPersona,entryClock);
    if (!boundMsiPower && !requestTicket && !(g_inputHostProcess && g_inputHostPrepared && g_inputHostNeutralized &&
            g_inputHostBoundPersona == requestedPersona)) {
        const auto admission = inputHostStartProtectionConsume(requestedPersona);
        if (!admission.accepted) return false;
        requestTicket = admission.requestId;
        if (admission.created) inputHostStartProtectionLog("accepted","internal-start");
    }
    struct RequestOutcome {
        std::uint64_t ticket;
        std::uint64_t power=g_powerGeneration.load(std::memory_order_acquire);
        DWORD retryableTransportError=ERROR_SUCCESS;
        int exceptions = std::uncaught_exceptions();
        ~RequestOutcome() noexcept {
            const bool healthy = g_inputHostProcess && g_inputHostPrepared && g_inputHostNeutralized &&
                !g_inputHostReleaseQuarantined && !g_inputHostTransportFault;
            if (!healthy && retryableTransportError) {
                try { inputHostSwitchRecoveryOfferLocked(ticket,power,retryableTransportError); }
                catch (...) {} // Failed scheduling never repeats partial lifecycle effects.
            }
            const bool recovering = ticket && g_inputHostSwitchRecovery.requestId==ticket &&
                g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire);
            // A pending fallback still starts execution's one inspection clock.
            // This failure is provisional, never a final player outcome.
            if (!healthy && !inputHostSwitchExecutionAwaitingLocked(ticket) &&
                    (recovering || std::uncaught_exceptions() > exceptions || g_inputHostTransportFault ||
                    g_inputHostStartBlocked || g_inputHostFaultHoldsStart.load(std::memory_order_acquire)))
                inputHostStartProtectionFinish(ticket,"failure","start-terminal-failure");
        }
    } requestOutcome{requestTicket};
    // VC-LIFECYCLE-20261020-R3: ordinary first Start and wake/fallback Start
    // share the SAME admitted player's absolute period. Nested scopes retain
    // whichever existing deadline is shorter; this never admits or renews it.
    struct PlayerStartExecutionScope {
        bool expiredPlayerWork=false;
        std::uint64_t deadline=g_inputHostSwitchRecoveryStartDeadline;
        std::uint64_t request=g_inputHostSwitchRecoveryStartRequestId;
        std::uint64_t power=g_inputHostSwitchRecoveryStartPower;
        explicit PlayerStartExecutionScope(std::uint64_t ticket,const std::string& persona){
            const auto clock=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(clock);
            // Recheck the same immutable queue provenance at the actual Start
            // throat: dispatch may cross30s after bind/dequeue. Separate wake
            // and steady-policy callers keep their existing authority.
            if(ymcc::inputHost::queuedPlayerStart::executingPlayerStartWork &&
                    ymcc::inputHost::queuedPlayerStart::refuseExpired(view,persona,"start-execution-entry")){
                expiredPlayerWork=true;return;
            }
            if(ticket&&view.requestId==ticket&&view.locked){
                const auto original=clock+view.remainingMs;
                g_inputHostSwitchRecoveryStartDeadline=deadline&&deadline<original?deadline:original;
                g_inputHostSwitchRecoveryStartRequestId=ticket;
                g_inputHostSwitchRecoveryStartPower=currentPowerGeneration();
            }
        }
        ~PlayerStartExecutionScope(){g_inputHostSwitchRecoveryStartDeadline=deadline;
            g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}
    } playerStartExecutionScope(requestTicket,requestedPersona);
    if(playerStartExecutionScope.expiredPlayerWork)return false;
    if(inputHostSwitchExecutionAwaitingLocked(requestTicket))return false;
    if (requestTicket) g_inputHostSwitchVerificationPending.store(requestTicket,std::memory_order_release);
    g_inputHostStartProtectionBoundTicket=requestTicket;
    // Cold duties are execution, before any MSI preparation/source/mode or new
    // Host. They share this ticket and absolute period, never fallback quota.
    const auto coldReturn=inputHostTargetReturnColdPassLocked(requestTicket,currentPowerGeneration(),requestedPersona);
    if(!coldReturn.clean()){
        inputHostSwitchExecutionBeginColdLocked(requestTicket,coldReturn.reason);
        return false;
    }
    g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;
    InputHostStartExceptionCleanup exceptionCleanup;
    MsiPersonaModeStartScope msiPersonaScope;
    g_inputHostStartProtectionBoundTicket = requestTicket;
    // P0（虚拟手柄机型库对齐评估 2026-09-15）：MSI Claw 物理手柄默认处于
    // MSI 私有模式（XInput 域不可见）。GP-MSIG3E-3（Layer B）起，启动顺序改为按能力表
    // 切/等 DInput（§3.B L43）：命令接口 → SwitchMode(2) → 等 PID_1902 主游戏接口
    // （&MI_00&COL01）→ 建立 canonical 帧；有界窗口内未出现则一次重发、总窗耗尽一次回退
    // 现有 XInput T0 路径。非 Claw / 未识别机型不介入（失败仅记录，不阻塞启动）。
    //
    // GP-MSIG3E-6 LC-R1（2026-10-03 裁决 §四/§五 M1）启停顺序修正：DInput 路线**不再**
    // 在建塔入口处提前建立。旧序（此处 Ensure → 再清理旧 Host）与旧 stop 成顺序矛盾——
    // inputHostStopLocked 尾部以 routeWasArmed 撤销路线并 msiClawRestoreXInputMode("input-host-stop")
    // 归还 XInput（见 L24201-24208），会把这里刚建的路线撤掉，随后新会话在 XInput 面上准入物理源。
    // 新序：旧 Host 清理完成 → 现有阻断门通过 → MSI Ensure → 物理源准入。Ensure 改在本函数
    // 的"恒等短路"分支与物理源准入前各调用一次（幂等：已 armed 零成本早退；非 Claw/未授权
    // 板号由 msiClawEnsureDInputRoute 内部早退，零模式写）。启动被阻断早退的路径不再切模式。
    // MSI 背键（Batch 4 出路 A，2026-09-16 全机型适配）：M1/M2 → F11/F14
    // 键盘重映射（幂等；非 Claw 机型零成本早退）。hook 的 MSI VK 分支消费
    // 该键盘输出 → g_oemBackState → backButtons → Deck L5/R5。
    const bool msiRearWasCached=g_msiM12RemapDone;
    ymcc::msi::rearSdPreparation::Receipt msiRearPreparationReceipt;
    const bool msiRearPreparationReturned=msiClawPrepareForVirtualStart(&msiRearPreparationReceipt);
    if(msiClawKnownBoardAuthority())appendNativeLifecycleLog("msi-rear-sd-preparation",{
        {"route","msi-dinput-rear-preparation"},{"cachedBefore",msiRearWasCached},
        {"preparationReturned",msiRearPreparationReturned},{"cachedAfter",g_msiM12RemapDone},
        {"nativeAutomaticWritesEnabled",ymcc::msi::nativeRear::buildWritesEnabled},
        {"framing","64-byte-report-body-offset-0"},{"inputMappingVerified",false},
        {"writeOutcomeReasonAvailable",true},{"startupAdmissionChanged",false},
        {"preparationFailureReasonAvailable",true},
        {"preparationReceipt",ymcc::msi::rearSdPreparation::fields<json>(msiRearPreparationReceipt)},
        {"scope","existing preparation return and discovery/write receipts only; success is not observed keys"}});
    // 手术三 B 风暴（§27，2026-09-14）：新会话开启即重置 tuple-heal 防循环
    // 守卫（幂等；成功 tuple 匹配处也会清），防止守卫跨会话滞留。
    g_inputHostTupleHealPending.store(false, std::memory_order_release);
    // Historical owned duties are now continued before creation by the same R3
    // execution owner. Driver lists alone grant no ownership or cleanup rights.
    // Startup retains the existing service-health diagnostic below.
    // 服务健康诊断（只读，fail-closed 记录）。
    hidHideServiceHealth("start");
    if (g_inputHostReleaseQuarantined) {
        appendNativeLifecycleLog("input-host-start-blocked", {
            {"reason", "release-quarantine"},
            {"pXinput", "UNENCLOSED"}
        });
        msiQuarantineDiagnosticReceipt();
        inputHostSwitchAuditRejected("release-quarantine");   // 批130：本轮失败收口（b124 风暴形状）
        return false;
    }
    // GP-MSIG3E-6-LC-R2 §四 R1 负控 mutation=1：把 Ensure **放回旧 Host 清理（stop）之前**。
    // 生产构建下 seam 恒不活动 ⇒ 本块不存在；测试构建下顺序断言必须因此 FAIL。
#ifdef YMCC_FAULT_INJECT
    if (ymStartSeamActive() && g_startEntrySeam.mutation == 1 && g_inputHostProcess) {
        ymStartEnsureRoute("prearm-mutation");
    }
#endif
    if (g_inputHostProcess) {
        // A process handle remains valid after its child exits and becomes
        // signaled. Never let a stale prepared/neutralized snapshot bypass an
        // explicit rearm epoch; clear the old session first, then continue
        // through the normal HELLO → PREPARE → NEUTRAL path. Only a positive
        // exit observation permits retirement; failed/unexpected waits cannot
        // authorize close, Clear or the live-session idempotent shortcut.
        const auto processWait = ymcc::inputHost::processRetirement::waitOnce(g_inputHostProcess, 0);
        const DWORD processState = processWait.state;
        if (processState != WAIT_OBJECT_0 && processState != WAIT_TIMEOUT) {
            // Preserve this owner/tuple for the existing quarantine collector.
            // Unproven liveness must not admit a new or idempotent writer.
            g_inputHostReleaseQuarantined = true;
            g_inputHostStartBlocked = true;
            g_inputHostTransportFault = true;
            g_inputHostActive = false;
            g_inputHostHealthySinceTick.store(0, std::memory_order_release);
            appendNativeLifecycleLog("input-host-start-blocked", {
                {"reason","previous-host-wait-unproven"},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
                {"waitState",processState},{"waitError",processWait.error},
                {"hostHandleRetained",true},{"quarantined",g_inputHostReleaseQuarantined},{"newHostCreated",false}});
            return false;
        }
        if (processState == WAIT_OBJECT_0) {
            writeLog("WARN", json({
                {"event", "input-host.stale-process-handle"},
                {"state", "exited"},
                {"epoch", g_inputHostEpoch}, {"runId", g_inputHostRunId},
                {"wakeHold", g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire)}
            }).dump());
            if (g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire)) {
                appendNativeLifecycleLog("input-host-dead-host-reopen-held", {
                    {"reason", "wake-hold-skip-restore"},
                    {"epoch", g_inputHostEpoch}, {"runId", g_inputHostRunId}
                });
                // Reuse the same zero-time observation under g_inputHostMx;
                // no second wait, new budget or physical restore is introduced.
                const auto retirement = ymcc::inputHost::processRetirement::retire(
                    g_inputHostProcess, [processWait](HANDLE) { return processWait; });
                if (!retirement.exitConfirmed || !retirement.handleClosed) {
                    // The failed close remains debt of the same old session;
                    // only the existing quarantine owner may finish retirement.
                    g_inputHostReleaseQuarantined = true;
                    g_inputHostStartBlocked = true;
                    g_inputHostTransportFault = true;
                    g_inputHostActive = false;
                    g_inputHostHealthySinceTick.store(0, std::memory_order_release);
                    appendNativeLifecycleLog("input-host-process-retirement", {
                        {"phase","start-wake-held"},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
                        {"waitState",retirement.waitState},{"waitError",retirement.waitError},
                        {"hostExitConfirmed",retirement.exitConfirmed},{"closeAttempted",retirement.closeAttempted},
                        {"handleClosed",retirement.handleClosed},{"closeError",retirement.closeError},
                        {"hostHandleRetained",g_inputHostProcess!=nullptr},{"quarantined",g_inputHostReleaseQuarantined},
                        {"scope","process-handle-only;endpoint-and-physical-return-separate"}});
                    return false;
                }
                inputHostClearLocked();
            } else {
                inputHostStopLocked("host-already-exited");
            }
        } else if (g_inputHostBoundPersona != inputHostPersona()) {
            // HC SetControllerModeCore disconnects and disposes the current
            // vTarget before constructing the requested persona. Reuse the
            // existing single Host route for the same DS4/X360 transition.
            // 7.44 修订（2026-09-11）：T2b 置位——persona 转换前请求 GM Reset
            // （HC GamepadMotion.Reset，GamepadMotion.cs:79-82），capture tick
            // 消费（g_gm 独占），不等传输层、不跨线程直调。
            g_gmSessionResetReason.store("persona-transition", std::memory_order_release);
            g_gmSessionResetRequested.store(true, std::memory_order_release);
            appendNativeLifecycleLog("input-host-persona-transition", {
                {"from", g_inputHostBoundPersona}, {"to", inputHostPersona()},
                {"policy", "single-host-disconnect-dispose-recreate"},
                {"gmSessionReset", true}
            });
            const bool normalReturn=inputHostSwitchExecutionBeginReturnLocked(requestTicket);
            inputHostStopLocked("persona-changed");
            if(normalReturn)inputHostSwitchExecutionReturnStoppedLocked();
        } else {
            // 批130：塔已在请求档上 ⇒ 幂等短路（HC VirtualManager.cs:437-444 同形）。
            // 只在本轮是开关类工作取件时落一条 skipped（防每次 admission 都写）。
            inputHostSwitchAuditSkipped("already-effective");
            // GP-MSIG3E-6 LC-R1（M1）：同 persona 路径不经过任何 inputHostStopLocked，
            // 因此路线不会被 stop 撤销；仍补齐一次幂等 Ensure（已 armed 零成本早退），
            // 使本路径与主路径同序，且旧 Host 已退出等未走主路径时也能自愈。
            ymStartEnsureRoute("idempotent");
            return g_inputHostPrepared && g_inputHostNeutralized;
        }
    }
    // HC VirtualManager serializes persona changes as Disconnect -> Dispose
    // before constructing the next target.  The old target's HidHide journal
    // is intentionally still active while that release runs; checking the
    // visibility gate before handling g_inputHostProcess would deadlock the
    // transition and leave the old DS4/X360 endpoint publishing forever.
    // R103: a persona hold may retire its journal while the old child still
    // owns the endpoint. Never overwrite that retained process with a new Host.
    if (g_inputHostProcess) {
        appendNativeLifecycleLog("input-host-start-blocked", {
            {"reason","previous-host-exit-pending"},{"runId",g_inputHostRunId},
            {"epoch",g_inputHostEpoch},{"requestedPersona",inputHostPersona()},
            {"newHostCreated",false},{"explicitIntentPreserved",true}});
        return false;
    }
    if (g_inputHostReleaseQuarantined ||
        (g_hidHideVisibilityJournal.active &&
         !g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire))) {
        appendNativeLifecycleLog("input-host-start-blocked", {
            {"reason", g_inputHostReleaseQuarantined ? "release-quarantine" : "visibility-transaction-active"},
            {"pXinput", "UNENCLOSED"}
        });
        return false;
    }
    if (g_inputHostStartBlocked) {
        appendNativeLifecycleLog("input-host-start-blocked", {
            {"reason", "explicit-rearm-required"},
            {"pXinput", "UNENCLOSED"}
        });
        return false;
    }
    if(msiClawKnownBoardAuthority())g_msiVirtualReleaseRearmEligible.store(false,std::memory_order_release);
    if (!ymStartAssetsPresent()) return false;
    // HC parity（#8，VirtualManager）：target 启动不看电源状态——HC 在
    // sleep 时 target 保持连接、仅 UpdateInputs 运行时跳过（isSystemSleeping
    // 614-615）；YMCC 的等价运行时门已在 inputHostSubmitPad per-tick
    // powerAdmitted（C44.6，HC per-tick isConnected skip）。因此不在此
    // 拒绝启动：power 未 ready 时 Host 仍可创建 target，帧在 submit 环被
    // per-tick 门跳过 → 唤醒后自动恢复（与 HC 一致，无需 defer+rearm）。
    // GP-MSIG3E-6 LC-R1（M1）主路径：此处旧 Host 已清理完毕（inputHostStopLocked:
    // persona-changed / host-already-exited 等）、全部阻断门（release-quarantine /
    // visibility-transaction / explicit-rearm / capture-assets）均已通过，且后续不再有
    // stop 撤销路线。此刻建立 DInput 路线，使下面的物理源准入落在 PID_1902 主游戏接口上。
    // 幂等（已 armed 早退）；非 Claw/未授权板号由 msiClawEnsureDInputRoute 内部早退，零模式写。
    ymStartEnsureRoute("main");
    // Bind ControllerManager's one selected physical source before an
    // XInput-visible target is created.  HC's VirtualManager can nevertheless
    // construct a HID-only DS4 target while ControllerManager has no current
    // source; that target remains neutral/source-pending until the canonical
    // XInput sample is admitted.  Never apply this relaxed lifecycle to the
    // XInput persona, where a late slot could be the virtual endpoint.
    const bool msiSourceRequiredBeforeTarget = msiClawKnownBoardAuthority();
    bool physicalSourceAdmittedAtStart = ymStartAdmitSource();
    bool physicalSourcePending = !physicalSourceAdmittedAtStart;
    if (physicalSourcePending && !msiSourceRequiredBeforeTarget && !inputHostCanCreateTargetWithoutPhysicalSource()) {
        writeLog("WARN", "input-host start rejected: physical-xinput-source-missing");
        return false;
    }
    // The virtual-gamepad feature directory is the canonical Home of the Host.
    // The exe-adjacent InputHost folder is only a legacy transition fallback.
    std::wstring exe = inputHostExecutablePath();
    if (!ymStartExecutablePresent(exe)) {
        writeLog("WARN", "input-host start rejected: executable-missing");
        return false;
    }
    g_inputHostRunId = inputHostNewId();
    g_inputHostPipeName = L"YeManInputHost-" + std::wstring(g_inputHostRunId.begin(), g_inputHostRunId.end());
    g_inputHostStartupNonce = inputHostNewId();
    g_inputHostEpoch = g_inputHostEpochCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_inputHostEpochMirror.store(g_inputHostEpoch, std::memory_order_release);
    g_inputHostBoundPowerGeneration = g_powerGeneration.load(std::memory_order_acquire);
    if(!msiPersonaModeResumeOnNewTuple())return false;
    g_inputHostSequence = 0;
    g_inputHostConfigRevision = 1; // v1 canonical-frame adapter config; UI parameter apply is separately revisioned.
    g_inputHostLastKeepAliveTick = 0;
    g_inputHostReleaseDisposition = "opening";
    g_inputHostLifecycle = "opening";
    // 批130（挖掘回函 §C-①，实读 HC VirtualManager.cs:437-444/520/546）：**不再**在此写
    // bound ——旧行为=建塔早期绑定（缺陷 ② 的串档源）。本拍只登记"本轮请求档"（信封按它
    // 建 target），建塔成功（PREPARE 通过）才写 bound（= 生效档）；早退路径由 RAII 清架上档。
    // MSI ticket ownership is frozen before Host/process/pipe I/O. An ACK
    // cannot absorb a newer OFF/ON committed by the independent settings lane.
    const unsigned long long msiStartSettingSequence=msiExplicitRearmStartSettingSequence();
    inputHostBeginBuildPersona(inputHostPersona());
    struct BuildPersonaScope {
        ~BuildPersonaScope() { inputHostEndBuildPersona(); }
    } buildPersonaScope;
    // Coordinator owns the local, owner-only server before Host starts. The
    // random nonce is checked in HELLO and echoed in every receipt.
    g_inputHostServerPipe = inputHostCreateServerPipeLocked();
    if (!g_inputHostServerPipe) {
        writeLog("WARN", json({{"event", "input-host.start-failed"}, {"reason", "pipe-server-create-failed"},
            {"error", (unsigned long long)GetLastError()}}).dump());
        inputHostClearLocked();
        return false;
    }
    std::wstring startupNonce(g_inputHostStartupNonce.begin(), g_inputHostStartupNonce.end());
    std::wstring cmd = L"\"" + exe + L"\" --pipe \"" + g_inputHostPipeName + L"\" --nonce \"" + startupNonce +
        L"\" --parent-pid " + std::to_wstring(GetCurrentProcessId());
#if defined(YMCC_MSI_HOST_STAGE_DIAGNOSTIC)
    // MSI-only opt-in evidence; no flag on ordinary/non-MSI Host launches.
    g_inputHostMsiStageRequested = msiClawKnownBoardAuthority() && domainLogEnabled("virtual");
    if (g_inputHostMsiStageRequested) cmd += L" --msi-host-stage-evidence-v1";
#endif
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    std::wstring hostDir = exe;
    const auto hostSlash = hostDir.find_last_of(L"\\/");
    if (hostSlash != std::wstring::npos) hostDir.resize(hostSlash);
    if (!ymStartCreateProcessW(buf, hostDir, si, pi)) {
        writeLog("WARN", json({{"event", "input-host.start-failed"}, {"error", (unsigned long long)GetLastError()}}).dump());
        inputHostClearLocked();
        return false;
    }
    g_inputHostPrimaryThread = pi.hThread;
    g_inputHostProcess = pi.hProcess;
    pi.hThread = nullptr; pi.hProcess = nullptr;
    (void)inputHostPrimaryThreadRetirementProvenLocked("created");
    json helloReceipt;
    g_inputHostCreatedIdentity = ymcc::inputHost::exitRetirement::identity(g_inputHostProcess);
    g_inputHostExitForceSpent = false;
    // GP-XBOX-5：冷启动现场 3 s HELLO 到时后 Host 才写回，造成可避免的断管重开。
    const DWORD helloBudget = inputHostBuildPersonaSnapshot() == "elite" ? 5000 : 3000;
    const bool hello = inputHostRequestLocked("HELLO", json::object(), "host-hello", helloBudget, &helloReceipt);
    if (hello && domainLogEnabled("virtual")) {
        try {
            // R4: bind the controller Host itself; FanHost export hashes have a different scope.
            const auto identity = helloReceipt.find("hostRuntimeBinaryIdentity");
            const bool present = identity != helloReceipt.end() && identity->is_object();
            const bool bounded = present && identity->dump().size() <= 8192;
            json evidence = {{"state", bounded ? "observed" : (present ? "oversized" : "not-advertised")},
                {"hostPid", g_inputHostProcess ? GetProcessId(g_inputHostProcess) : 0},
                {"hostInstanceId", g_inputHostHostInstanceId}, {"runId", g_inputHostRunId},
                {"epoch", g_inputHostEpoch}, {"requestId", helloReceipt.value("requestId", std::string{})},
                {"sequence", helloReceipt.value("sequence", 0ULL)},
                {"assetVerified", helloReceipt.value("assetVerified", false)},
                {"scope", "Host HELLO identity observation; no loaded-byte or Steam consumption proof"}};
            if (bounded) evidence["identity"] = *identity;
            appendNativeLifecycleLog("input-host-runtime-binary-identity", evidence);
        } catch (...) { /* diagnostic failure never changes HELLO success */ }
    }
    g_inputHostHello = hello;
    if (hello) inputHostApplyHelloCapabilities(helloReceipt);
    if (hello) g_inputHostLifecycle = "host-acknowledged";
#if defined(YMCC_MSI_HOST_STAGE_DIAGNOSTIC)
    if (hello && g_inputHostMsiStageRequested && msiClawKnownBoardAuthority()) {
        try {
            ymcc::msi::hostStageEvidence::Expected expected;
            expected.parentPid = GetCurrentProcessId();
            expected.hostPid = g_inputHostProcess ? GetProcessId(g_inputHostProcess) : 0;
            expected.hostInstance = g_inputHostHostInstanceId;
            expected.requestId = helloReceipt.value("requestId", std::string{});
            expected.sequence = helloReceipt.value("sequence", 0ULL);
            expected.runId = g_inputHostRunId; expected.epoch = g_inputHostEpoch; expected.command = "HELLO";
            appendNativeLifecycleLog("input-host-msi-stage-evidence", {{"state", "hello-arm"},
                {"evidence", ymcc::msi::hostStageEvidence::read(expected)}});
        } catch (...) { /* missing evidence never rejects HELLO or target preparation */ }
    }
#endif
    // MSI source readiness must precede visibility proof and PREPARE. The
    // HELLO-only epoch preserves the original pump/resend ownership while waiting.
    if (hello && msiSourceRequiredBeforeTarget) {
        physicalSourceAdmittedAtStart=false;
        const auto sourceWaitEpoch=g_inputHostEpoch;
        const auto sourceWaitRunId=g_inputHostRunId;
        const auto sourceWaitPower=g_inputHostBoundPowerGeneration;
        const auto sourceWaitIntent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
        if (!ymStartAwaitMsiSource(requestTicket,physicalSourceAdmittedAtStart,lock)) {
            // The wait yields the mutex for BUS. Never retire a replacement owner
            // if an independent exit/cleanup changed the tuple in that interval.
            if(g_inputHostEpoch==sourceWaitEpoch && g_inputHostRunId==sourceWaitRunId) {
                inputHostStopLocked("msi-source-admission-unproven");
                const auto failed=g_virtualStartProtection.view(inputHostStartProtectionNow());
                if(failed.requestId==requestTicket && failed.persona==requestedPersona &&
                        inputHostConfiguredPersona()==requestedPersona && g_realStickTest.virtualTargetEnabled &&
                        hardwareWriteAllowed() && currentPowerGeneration()==sourceWaitPower &&
                        !inputHostExitFenceRaised() && !g_inputCaptureStop.load(std::memory_order_acquire) &&
                        !g_exitRequested.load(std::memory_order_acquire) &&
                        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
                        sourceWaitIntent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire))
                    g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
            }
            inputHostStartProtectionFinish(requestTicket,"failure","msi-source-admission-unproven");
            return false;
        }
        physicalSourcePending=false;
    }
    // P1-T11（R7，2026-09-11）：hide 先于虚拟目标创建（顺序对齐 HC）。
    // HC IController.cs:536-545：选中目标 → Hide(HideHID + CyclePort) →
    // 才创建虚拟目标；YMCC 旧序（PREPARE → hide）反转。现重排为：
    //   HELLO → hide 事务（app-reg 幂等/cloak 幂等/dev-hide/readback/CyclePort）
    //   → PREPARE_TARGET → SUBMIT_NEUTRAL。
    // 失败语义（KG-R2 更正）：hide/readback 失败本身不直接阻断 PREPARE，但并非无条件放行。
    //   · 无在线物理源需要隐藏时：hide 不准入不构成拒绝理由，PREPARE 照常继续。
    //   · 在线物理源存在且隐藏未准入时（hello && physicalSourceOnlineAtStart && !physicalHidAdmitted）：
    //     先做**一次**有界 reset/重试（hidHideRestorePhysicalTransaction("pre-target-hide-retry-reset")
    //     → Sleep(100) → hidHideApplyPhysicalTransaction），重试后仍未准入则
    //     inputHostStopLocked("physical-hide-not-admitted") + return false **拒绝 PREPARE**。
    //     见下方 input-host-hide-not-admitted 的 initial/retry-result/refused-pre-target 分支。
    //     （HC hide 后创建失败同样状态，靠断开/关闭路径解隐；双端点护栏优先于放行。）
    // HC 的 selected-controller 路径在虚拟目标创建前隐藏物理 instance/container。
    // physical-source-pending 特殊分支（physicalVisibilitySkip）：无在线物理源 = 无需要隐藏的对象
    // → 跳过 hide 事务、也不进入上述拒绝门（HC 无此循环，0910-20 identity-unresolved
    // 重试循环不回归）。
    const bool physicalVisibilitySkip =
        !msiSourceRequiredBeforeTarget && physicalSourcePending && inputHostCanCreateTargetWithoutPhysicalSource();
    const ULONGLONG hideTxnStartedTick = GetTickCount64();
    bool physicalHidAdmitted = hello &&
        (kVirtualTestKeepPhysicalVisible || physicalVisibilitySkip ||
         hidHideApplyPhysicalTransaction(g_inputHostRunId));
    // 批131（照挖掘回函 §C-③"开·隐藏"拍）：一条汇总行（子步骤明细仍在 hidhide.* 行族）。
    // 批133（照 CTW 口径 §B3）：`HidHide pre-cloak armed for '{0}' (blocked={1}, cloak={2}, no re-enumeration)`
// 的同形常开行 —— 三条件一次说清：**屏蔽条数 / cloak 位 / 不触发重枚举**（我方开启路径本来就 cycle=0）。
    {
        HidHideVisibilitySnapshot preSnap;
        const bool preRead = hidHideCaptureVisibilitySnapshot(preSnap);
        appendNativeLifecycleLog("hidhide.pre-cloak", {
            {"armed", !(kVirtualTestKeepPhysicalVisible || physicalVisibilitySkip)},
            {"skipReason", kVirtualTestKeepPhysicalVisible ? "test-keep-physical-visible"
                : (physicalVisibilitySkip ? "physical-source-pending-hid-only-target" : "")},
            {"blocked", (unsigned long long)g_hidHideVisibilityJournal.hiddenByUs.size()},
            {"cloak", preRead && preSnap.cloaking},
            {"reEnumeration", false},
            {"transactionId", g_hidHideVisibilityJournal.transactionId},
            {"pXinput", "UNENCLOSED"}
        });
    }
    inputHostSwitchAuditHideTransaction(
        kVirtualTestKeepPhysicalVisible || physicalVisibilitySkip,
        kVirtualTestKeepPhysicalVisible ? "test-keep-physical-visible"
            : (physicalVisibilitySkip ? "physical-source-pending-hid-only-target" : nullptr),
        physicalHidAdmitted, GetTickCount64() - hideTxnStartedTick);
    // Double-endpoint guard: if a physical source is online, never publish a
    // virtual target while physical hide admission is unproven.
    const bool physicalSourceOnlineAtStart =
        physicalSourceAdmittedAtStart && !physicalSourcePending;
    if (hello && physicalSourceOnlineAtStart && !physicalHidAdmitted) {
        appendNativeLifecycleLog("input-host-hide-not-admitted", {
            {"phase", "initial"}, {"sourcePresent", true},
            {"physicalHidAdmitted", false}, {"runId", g_inputHostRunId},
            {"retry", true}, {"safeStop", true}
        });
        // Clear any partially-owned hide journal before one bounded retry.
        const bool resetOk = hidHideRestorePhysicalTransaction("pre-target-hide-retry-reset");
        if (resetOk) {
            Sleep(100);
            physicalHidAdmitted = hidHideApplyPhysicalTransaction(g_inputHostRunId);
        }
        appendNativeLifecycleLog("input-host-hide-not-admitted", {
            {"phase", "retry-result"}, {"sourcePresent", true},
            {"physicalHidAdmitted", physicalHidAdmitted},
            {"resetOk", resetOk}, {"retry", true}, {"safeStop", true}
        });
    }
    if (hello && physicalSourceOnlineAtStart && !physicalHidAdmitted) {
        appendNativeLifecycleLog("input-host-hide-not-admitted", {
            {"phase", "refused-pre-target"}, {"sourcePresent", true},
            {"physicalHidAdmitted", false}, {"retry", true},
            {"safeStop", true}
        });
        inputHostStopLocked("physical-hide-not-admitted");
        return false;
    }
    // 虚拟目标创建（HC 顺序：hide 之后才建虚拟）。PREPARE 失败仍走既有
    // quarantine 流程（护栏不变）；hide 阶段不新建 epoch/多目标（epoch 已在
    // 上方 ++g_inputHostEpoch 一次，hide 只复用该 epoch）。
    const ULONGLONG prepareStartedTick = GetTickCount64();
    const auto preparePowerGeneration = g_powerGeneration.load(std::memory_order_acquire);
    json prepareReceipt;
    if(msiControllerTraceEnabled())msiControllerTraceLog("virtual-prepare-begin",{{"runId",g_inputHostRunId},{"persona",inputHostBuildPersonaSnapshot()}},true);
    const bool msiSdFenceAdmission = g_msiSteamDeckReleaseFence.begin(msiClawKnownBoardAuthority(),
        inputHostBuildPersonaSnapshot(), g_inputHostRunId, g_inputHostEpoch);
    json preparePayload = json::object();
    if (msiSdFenceAdmission && g_msiSteamDeckReleaseFence.required()) preparePayload["msiSteamDeckReleaseFence"] = true;
    const bool prepareAttempted = hello && msiSdFenceAdmission;
    const bool prepareSent = prepareAttempted && inputHostRequestLocked("PREPARE_TARGET", preparePayload, "target-prepared", static_cast<DWORD>(ymcc::inputHost::runtimeContract::prepareReceiptCeilingMs), &prepareReceipt);
    const bool prepared = prepareSent && g_msiSteamDeckReleaseFence.acceptPrepare(prepareReceipt);
    if(prepareSent && g_psEndpointOwnership.capable()) {
        g_psEndpointOwnership.accept(prepareReceipt);
        inputHostTargetReturnAcceptPsLocked(prepareReceipt);
        try {const auto pin=g_psEndpointOwnership.snapshot();
            appendNativeLifecycleLog("ps-endpoint-ownership",
                ymcc::inputStartupDiagnostics::acceptedPsPrepare(prepareReceipt, pin));
        } catch(...) {}
    }
    if (g_msiSteamDeckReleaseFence.required()) appendNativeLifecycleLog("msi-sd-release-fence", {
        {"phase","prepare"},{"admitted",prepared},{"reason",g_msiSteamDeckReleaseFence.reason()},
        {"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
        // A denied/failed PREPARE may have no receipt or a non-object response.
        // Logging must not throw or coerce it into valid release evidence.
        {"prepareAttempted",prepareAttempted},{"prepareSent",prepareSent},
        {"receiptKind",prepareReceipt.type_name()},{"receiptObject",prepareReceipt.is_object()},
        {"evidence",prepareReceipt.is_object() ? prepareReceipt.value("msiSteamDeckReleaseEvidence",json(nullptr)) : json(nullptr)},
        {"scope","MSI-SD owned endpoint lifetime; not Steam consumption"}});
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
    inputHostMsiReportDiagnosticAcceptPrepare(prepared, prepareReceipt);
#endif
    if(msiControllerTraceEnabled())msiControllerTraceLog("virtual-prepare-end",{{"runId",g_inputHostRunId},{"prepared",prepared},{"persona",inputHostBuildPersonaSnapshot()}},true);
    // A successful Host PREPARE can own a target even if the new pin receipt
    // is malformed. Preserve ownership for safe teardown; never publish it.
    g_inputHostPrepared = prepareSent;
    if (prepared) {
        // R14-4（评审14）：目标创建会改变可见设备集合（虚拟端点上线）⇒ 显式失效解析缓存，
        // 不依赖设备树事件能否按时到达。
        hidHideInvalidateRawInputIdentityCache();
        g_inputHostLifecycle = "target-prepared";
        // 批130「bind 收敛到建塔成功」：塔建成这一刻才写生效档（HC 的 HIDmode 写点 = target
        // 构造完成；只有 HIDstatus 是 connect 成功才写）。失败路径不写 ⇒ 回执/UI/重绑判据
        // （bound != live）从此读到真值，不再报"从未建出的档"。
        const std::string builtPersona = inputHostBuildPersonaSnapshot();
        if (!builtPersona.empty()) inputHostSetBoundPersona(builtPersona);   // 批134：同锁写入口
        // 批131（照挖掘回函 §C-③"开·挂载"拍）：挂载回执（含 vid/pid 与耗时）。
        {
            const char* mountedLabel = nullptr; const char* mountedVidPid = nullptr;
            inputHostSwitchAuditTargetIds(builtPersona, &mountedLabel, &mountedVidPid);
            inputHostSwitchAuditDeviceMounted(mountedLabel, mountedVidPid,
                                              GetTickCount64() - prepareStartedTick);
        }
    }
    const bool neutralized = prepared && inputHostRequestLocked("SUBMIT_NEUTRAL", json::object(), "neutral-accepted", 2000);
    g_inputHostNeutralized = neutralized;
    if (neutralized) g_inputHostLifecycle = "neutralized";
    // GP-XBOX-5：只限定已建出的 Elite；PS/Deck 原准入与帧流程不进入此分支。
    if (prepared && inputHostBuildPersonaSnapshot() == "elite" &&
        (!neutralized || !inputHostXboxConfirmStartupLocked(prepareReceipt, prepareStartedTick, preparePowerGeneration))) {
        // 电源撤准入/换代由原电源生命周期接管；此处禁止再 Stop/Unhide，保留驻留语义。
        const bool samePower = g_powerGeneration.load(std::memory_order_acquire) == preparePowerGeneration &&
            g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
            g_inputReady.load(std::memory_order_acquire) &&
            !g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire);
        if (!samePower) {
            appendNativeLifecycleLog("input-host-xbox-startup", {
                {"phase", "power-deferred"}, {"deviceWrites", false},
                {"owner", "existing-power-lifecycle"}
            });
            return false;
        }
        const bool sameIntent = std::strcmp(inputHostPersona(), "elite") == 0;
        inputHostStopLocked("xbox-startup-xinput-unavailable");
        if (sameIntent) g_inputHostFaultHoldsStart.store(true, std::memory_order_release);
        appendNativeLifecycleLog("input-host-xbox-startup", {
            {"phase", "physical-fallback"}, {"automaticRetryStopped", true},
            {"cleanupReleased", !g_hidHideVisibilityJournal.active && g_inputHostProcess == nullptr},
            {"physicalUsability", "see-release-readback-and-device-test"}
        });
        inputHostSwitchAuditRejected("xbox-startup-xinput-unavailable");
        return false;
    }
    // P0-3（2026-09-13 用户裁决，HC 严格）：删除 SUSPEND/journal/recovery
    // 原创机制。HC 无 journal、无 recovery 子进程（App.xaml.cs:648-716 仅
    // Sentry；残留靠 --uninstall-restore）；InstallNullDriver 仅 HC 插槽自愈
    // FixVirtualSlot（ControllerManager.cs:2001）内联使用，YMCC 无槽位监视器，
    // 故整机制连同命令面一并退役。物理 XInput 保持在线且仅被 HidHide 白名单
    // 隐藏（0912-18 基线 pXinput=UNENCLOSED）；publication 由
    // inputHostSubmitPad 的 per-tick physical-source 门（HC per-tick
    // isConnected skip）管控。
    const bool visibilityAdmitted = physicalHidAdmitted;
    const char* xinputPlane = physicalSourcePending
        ? "UNENCLOSED-source-pending"
        : "UNENCLOSED";
    if (visibilityAdmitted)
        g_inputHostLifecycle = physicalSourcePending ? "admitted-source-pending" : "admitted";
    appendNativeLifecycleLog("input-host-open-result", {
        {"hello", hello}, {"prepared", prepared}, {"neutralized", neutralized},
        {"visibilityAdmitted", visibilityAdmitted}, {"pXinput", xinputPlane},
        {"physicalSourceAdmittedAtStart", physicalSourceAdmittedAtStart},
        {"physicalSourcePending", physicalSourcePending},
        {"physicalHidAdmitted", physicalHidAdmitted},
        {"physicalHideGate", physicalSourceOnlineAtStart
            ? (physicalHidAdmitted ? "admitted" : "refused")
            : "not-required"},
        {"physicalVisibility", kVirtualTestKeepPhysicalVisible ? "kept-visible-test" : "hidhide-admitted"},
        {"transportFault", g_inputHostTransportFault},
        {"hostInstanceId", g_inputHostHostInstanceId}, {"runId", g_inputHostRunId},
        {"epoch", g_inputHostEpoch}
    });
    g_inputHostOpenReceiptEpoch.store(g_inputHostEpoch, std::memory_order_release);
    // P1-T11（R7）／KG-R2 更正：hide/readback 失败本身不写 journal、不改目标集，
    // visibilityAdmitted 仅保留为审计字段（publication 由 inputHostSubmitPad 的
    // per-tick 物理源门管控）。但启动成败**并非**"只看 PREPARE 结果"：在线物理源
    // 隐藏未准入（hello && physicalSourceOnlineAtStart && !physicalHidAdmitted）已在
    // 上方 refused-pre-target 分支 `inputHostStopLocked("physical-hide-not-admitted") +
    // return false` 拒绝，根本走不到本行。能到本行的只有 hide 已准入、或无需隐藏
    // （physical-source-pending）的情形，此时启动成败才只由 PREPARE 结果决定；
    // PREPARE 失败仍走下方 quarantine（label 复用既有流程）。
    if (!prepared) {
        requestOutcome.retryableTransportError=g_inputHostSwitchRecoveryTransportError;
        g_inputHostReleaseDisposition = "opening-failed/release-unproven";
        inputHostStopLocked("opening-failed");
        inputHostSwitchAuditRejected("opening-failed");   // 批130：本轮失败收口（throwKind 列）
        if (prepareReceipt.is_object() && prepareReceipt.contains("status") && prepareReceipt["status"].is_string() &&
            prepareReceipt.contains("detail") && prepareReceipt["detail"].is_string() &&
            ymcc::moduleMaintenance::transientPrepareRejection(prepareReceipt["status"].get<std::string>(),prepareReceipt["detail"].get<std::string>())) {
            g_inputModuleReplayPending.store(true,std::memory_order_release);
            appendNativeLifecycleLog("input-host-module-maintenance-deferred",{{"newTargetPublished",false},{"automaticRetryDeferred",true}});
            return false;
        }
        // A target-admission failure is a failed open, not a transient wake
        // condition. Hold automatic reopen until explicit user toggle, persona
        // transition, or power rearm; this prevents a 5–6 second open/stop
        // storm when a PnP probe rejects the target.
        g_inputHostFaultHoldsStart.store(true, std::memory_order_release);
        appendNativeLifecycleLog("input-host-fault-cooldown", {
            {"reason", "open-target-admission"},
            {"automaticallyRearmed", false},
            {"hint", "explicit-user-rearm-or-power-rearm-required"}
        });
        return false;
    }
    writeLog("INFO", json({
        {"event", "input-host.target-neutralized"}, {"persona", inputHostPersona()},
        {"epoch", g_inputHostEpoch}, {"runId", g_inputHostRunId},
        {"hostInstanceId", g_inputHostHostInstanceId}, {"active", false},
        {"physicalVisibility", kVirtualTestKeepPhysicalVisible ? "kept-visible-test" : "admitted"},
        {"physicalSourcePending", physicalSourcePending},
        {"pXinput", xinputPlane}
    }).dump());
    // Defensive open receipt: HC surfaces every Connect/status transition in
    // its log; YMCC must always leave an audit line once the target session is
    // admitted, independent of the earlier input-host-open-result projection.
    // Observed sessions reached SUBMIT_FRAME without an open-result line, so
    // this receipt is written unconditionally at the successful return.
    // 批129（诚实回执）：`persona` 读 live 配置，但本次塔实际按**信封取值**（bound）
    // 建出——实测两值可不同（08:57:44.274：persona:elite 伴 PREPARE
    // steam-deck-composite）。判"切换成功"用 builtPersona/builtProfile（实物）+
    // personaDrift；`persona` 保留原语义（现请求档），既有判据/P16 取用不受影响。
    const std::string builtPersonaNow = g_inputHostBoundPersona.empty()
        ? std::string(inputHostPersona()) : g_inputHostBoundPersona;
    const std::string builtProfileNow = inputHostBoundOrCurrentProfileIdentity();
    const std::string livePersonaNow = inputHostPersona();
    // 批131（照挖掘回函 §C-③"开·呈递"拍）：呈递行带 targetLabel + vid/pid（与 CT `now presenting …` 对表）。
    const char* builtTargetLabel = nullptr; const char* builtVidPid = nullptr;
    inputHostSwitchAuditTargetIds(builtPersonaNow, &builtTargetLabel, &builtVidPid);
    msiExplicitRearmStarted(msiStartSettingSequence);
    appendNativeLifecycleLog("input-host-started", {
        {"intentOrigin", inputHostIntentOriginForReason(g_inputHostReconcileReason.c_str())},
        {"intentId", std::string("start:") +
            (g_inputHostReconcileReason.empty() ? "unknown" : g_inputHostReconcileReason)},
        {"runId", g_inputHostRunId}, {"epoch", g_inputHostEpoch},
        {"persona", livePersonaNow},
        {"builtPersona", builtPersonaNow}, {"builtProfile", builtProfileNow},
        {"boundPersona", builtPersonaNow}, {"appliedPersona", builtPersonaNow},
        {"targetLabel", builtTargetLabel ? builtTargetLabel : "unknown"},
        {"vidPid", builtVidPid ? builtVidPid : "unknown"},
        {"builtBound", !g_inputHostBoundPersona.empty()},
        {"personaDrift", builtPersonaNow != livePersonaNow},
        {"physicalSourcePending", physicalSourcePending},
        {"pXinput", xinputPlane},
        {"transportUsable", true}
    });
    // 批129「落定拍」：五值同行（请求 / 入队 / 出队 / 下发 profile / 实物 persona）。
    inputHostSwitchAuditSettled("started", builtPersonaNow, builtProfileNow, livePersonaNow,
                                g_inputHostEpoch, g_inputHostHostInstanceId,
                                g_inputHostProcess
                                    ? static_cast<unsigned long long>(GetProcessId(g_inputHostProcess))
                                    : 0ULL,
                                g_inputHostRunId);
    // 批131（照挖掘回函 §C-③"就绪"拍 = CT `[ClawReady]` 三条件断言）：塔已就绪一条行，
    // 并把 enabled / mounted 两个状态位分开落盘（照 CT `_viiperEnabled` / `_viiperMounted` 分离）。
    inputHostSwitchAuditVirtualReady(kVirtualTestKeepPhysicalVisible || physicalVisibilitySkip,
                                     physicalHidAdmitted, physicalSourcePending,
                                     g_realStickTest.virtualTargetEnabled);
    // HC parity (CyclePort re-enumeration): after a successful hide+restart the
    // device re-enumerates hidden-but-present and the XInput slot returns; the
    // live (connectedMask & sourceMask) gate resumes publication then.  If the
    // slot has not returned yet at open completion, diagnostic only — never a
    // session failure (the 0910-17 probe traced exactly this awaiting state).
    if (visibilityAdmitted && !physicalSourcePending &&
        (g_xinputConnectedMask & g_xinputPhysicalSourceMask) == 0) {
        appendNativeLifecycleLog("input-host-open-source-awaiting-cycle", {
            {"sourceMask", g_xinputPhysicalSourceMask},
            {"connectedMask", g_xinputConnectedMask},
            {"hint", "cycle-re-enumeration-pending; live-gate-resumes"},
            {"pXinput", xinputPlane}
        });
    }
    if(!msiPersonaModeFinish())return false;
    inputHostStampBuildSettle();   // 批125
    return true;
}

