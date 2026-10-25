#pragma once
#include "virtual_controller_runtime_contract.h"
#include "input_host_process_retirement.h"

// VC-LIFECYCLE-20261020-R3 — production definition module.
// Serialized stop and physical return. Included at the original production definition site; lock order, journals, MSI adapter and return proofs remain unchanged.
// Shared enable/UI-output timeline: virtual_controller_runtime_contract.h.
// Navigation and capability boundaries: virtual_controller_runtime.md.
// This is compiled production code; the reference comments do not execute.
// Maintenance boundary: UI focus does not call this Stop path.
// Only the existing lifecycle owner retires a target; unresolved release keeps its
// original ownership debt, and only its verified return may authorize continuation.

static bool inputHostStopRestoresVisibilityWithoutProof(const char* reason) {
    if (!reason) return false;
    return std::strcmp(reason, "app-exit") == 0 ||
        std::strcmp(reason, "virtual-target-disabled") == 0 ||
        std::strcmp(reason, "coordinator-stop") == 0 ||
        std::strcmp(reason, "recovery-exhausted-physical-fallback") == 0;
}

// 批118（operator 2026-09-24 裁决「切换期本机不露」）：**人格切换期不解隐**。
// 现场（19:39 包，ms 级）：persona-changed 的停止先经 pipe 解隐（`hidhide.p-hid-restore
// {pre-release-host-api, unhide-sent}` + `hidhide-cycle`），紧接着建新塔前再隐藏 ⇒ 消费者看到
// 「旧虚拟 → 本机出现 → 本机消失 → 空 → 新虚拟」——正是 operator 报的形状；三方对比 §15 里
// CTW 的 pre-cloak（提前藏 + no re-enumeration）是同一结论的参照：最短形状 = **跨切换保持隐藏**。
// 因此 persona-changed：不做 unhide、不做端口循环；但**必须退账**（journal 清空），否则下一次
// 建塔会被 inputHostStart 的 `start-blocked{visibility-transaction-active}` 挡住。
// 解隐职责保留在三条既有路径：切回本机手柄（virtual-target-disabled）、退出（app-exit）、
// 隔离清除（quarantine-endpoint-absent）。
static bool inputHostStopHoldsPhysicalVisibility(const char* reason) {
    return reason && std::strcmp(reason, "persona-changed") == 0;
}

#include "msi_persona_mode_handoff_runtime.h"

static std::unique_lock<std::mutex> msiDinputReleasePadForLifecycle(const char* reason) {
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::defer_lock);
    if (!g_msiPersonaPumpHold.owns_lock()) pump.lock();
    msiDinputReleasePad(reason);
    // The caller retains a new lock; an existing persona hold remains its owner.
    return pump;
}

static void msiDinputStopForLifecycle(const char* reason,bool retainPhysicalMode,bool deferPhysicalReturn) {
    const bool alreadyHeld=g_msiPersonaPumpHold.owns_lock();
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::defer_lock);
    if(!alreadyHeld) pump.lock();
    // Keep the lock through release, clear, and the optional mode-1 write.
    // Otherwise a BUS pump could reopen after pad=null but before clearOwnedState.
    msiDinputReleasePad(reason);
    msiDinputClearOwnedState(reason);
    if(retainPhysicalMode) {
        g_msiDinput.routeWindowSeen=false;g_msiDinput.routeStartedAt=0;
        g_msiDinput.routeResent=false;g_msiDinput.routeReverted=false;
        g_msiModeRecoveryConfirmedEpoch.store(0,std::memory_order_release);
        g_msiModeRecoveryObservedEpoch.store(0,std::memory_order_release);
        g_msiModeRecoveryObservedTick.store(0,std::memory_order_release);
    } else if(!deferPhysicalReturn) {
        (void)msiClawRestoreXInputMode(reason);
    }
}

// Caller owns g_inputHostMx. Resource retirement is independent of saved player
// intent (app-exit may preserve Enabled). StartBlocked is not resource debt.
// Meaning: owned session cleanup incl physical duty (MSI return, endpoint and
// durable target history). This is NOT a bounded local-resource-only predicate
// and must not drive an unbounded app-exit await: physical debt can require the
// existing BUS/ownedReturn observer, whose admission may already be revoked.
// Pipe retirement must additionally include the transport owner's pending work;
// the native wrapper integrates that proof, not merely a successful close call.
// inputHostPipeRetirementProvenLocked must be declared before this header.
static bool inputHostOwnedCleanupProvenLocked() {
    const bool msiReturnProven = !msiClawKnownBoardAuthority() ||
        !g_msiDinputReturnOwed.load(std::memory_order_acquire);
    return !g_inputHostProcess && !g_inputHostPrimaryThread && !g_inputHostServerPipe && inputHostPipeRetirementProvenLocked() && !g_inputHostHello &&
        !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost && !g_inputHostNeutralized && !g_inputHostActive &&
        !g_hidHideVisibilityJournal.active && !g_inputHostReleaseQuarantined &&
        !g_msiSteamDeckReleaseDebt.required() && !g_psEndpointOwnership.retirementRequired() && msiReturnProven &&
        inputHostTargetReturnHistorySettledLocked();
}

// Preserve the existing OFF predicate: configured OFF plus all owned duties done.
static bool inputHostDisabledCleanupProvenLocked() {
    return !g_realStickTest.virtualTargetEnabled && inputHostOwnedCleanupProvenLocked();
}

static void inputHostStopLocked(const char* reason) {
    const auto closingTicket = inputHostConfiguredPersona() == "disabled"
        ? g_virtualStartProtection.pendingTicket("disabled",inputHostStartProtectionNow()) : 0;
    // OFF borrows only its existing closing ticket's original deadline.
    // The borrowed clock constrains pipe waits, never renews admission.
    struct StopExecutionScope {
        std::uint64_t deadline=g_inputHostSwitchRecoveryStartDeadline;
        explicit StopExecutionScope(std::uint64_t ticket) {
            const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
            if(ticket && view.requestId==ticket && view.locked) {
                const auto original=now+view.remainingMs;
                g_inputHostSwitchRecoveryStartDeadline=deadline ? (std::min)(deadline,original) : original;
            }
        }
        ~StopExecutionScope(){g_inputHostSwitchRecoveryStartDeadline=deadline;}
    } stopExecutionScope(closingTicket);
    struct CloseProtectionReceipt {
        std::uint64_t ticket;
        const char* outcome = "failure";
        ~CloseProtectionReceipt() noexcept {
            if (ticket) inputHostStartProtectionFinish(ticket,outcome,"stop-terminal-result");
        }
    } closeReceipt{closingTicket};
    ymcc::screenpads::observeNativeTransport(0,false);

    msiQuarantineDiagnosticBeginStop();
    if(msiControllerTraceEnabled())msiControllerTraceLog("host-stop-begin",{{"reason",reason?reason:"unspecified"}},true);
    // GP-MSIG3E-6-LC-R2 §四 R1：记录真实 stop 发生点（顺序断言用）。
    if (ymStartSeamActive()) ymStartSeamTrace(std::string("stop:") + (reason ? reason : ""));
    const bool hadProcess = g_inputHostProcess != nullptr;
    const bool hadPreparedTarget = g_inputHostPrepared;
    const bool prepareCreationPossible = g_inputHostPrepareMayHaveReachedHost;
    const bool msiSdPinnedOwnership = g_msiSteamDeckReleaseFence.currentPinned(g_inputHostRunId,g_inputHostEpoch);
    // SUBMIT transport loss clears publish health, not an accepted owned SD pin.
    // Other families/personas retain their existing prepared-flag semantics.
    const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership || prepareCreationPossible;
    g_psEndpointOwnership.captureRetirement(targetMayExist);
    if(g_psEndpointOwnership.capable())g_psEndpointOwnership.invalidate("target-stopping");
    const bool msiSdReleaseRequired = targetMayExist && g_msiSteamDeckReleaseFence.required();
    // R10-R2 M0：停止**起点**即快照真实 Host 进程 PID（与 release receipt 的 hostPid 同语义）。
    // 停止流程后段会 CloseHandle/置空 g_inputHostProcess，若在 stop-result 处再取会退化成 0；
    // 故同行一路携带到 stop-request 与 stop-result（owner/run/epoch 亦同行保留）。
    const unsigned long long stopHostPid = g_inputHostProcess
        ? static_cast<unsigned long long>(GetProcessId(g_inputHostProcess)) : 0ULL;
    const char* stopReason = reason ? reason : "";
    // Read-only boundary evidence: capture modifier state before QUIESCE/
    // RELEASE/SHUTDOWN so a later stuck-key observation can be placed before
    // or inside the stop transaction. This never sends or repairs a key.
    const json keyboardModifiersAtStopBegin =
        g_keyboardModifierEventGate.load(std::memory_order_acquire)
            ? ymcc::keyboardEvidence::toJson(ymcc::keyboardEvidence::read())
            : json(nullptr);
    const auto msiStopPower=ymcc::msi::rearm::buildEnabled&&msiClawKnownBoardAuthority()?g_powerGeneration.load(std::memory_order_acquire):0ULL;
    // 批131：关闭专项的计时与"拆之前的生效档"（device-removed / toggle-result 两拍要用）。
    const ULONGLONG stopStartedTick = GetTickCount64();
    // 批134：改为 value 快照（同锁读），不再直读全局字符串；批133 的语义保留：
    // 无塔停止时读**原始配置档**（不要走 inputHostPersona() 的映射，disabled/未知档保持关闭）。
    const std::string boundBefore = inputHostBoundPersonaValue();
    const std::string stopPersonaBefore = boundBefore.empty()
        ? std::string(inputHostConfiguredPersona()) : boundBefore;
    const auto stopSignalId=inputStopSignalBeginLocked(reason,stopPersonaBefore,stopHostPid);
    inputStopSignalPhaseLocked(stopSignalId,"before-publication-revoke");
    msiPersonaModeCapture(reason,targetMayExist,stopPersonaBefore);
    if (std::strcmp(stopReason, "app-exit") == 0 ||
        std::strcmp(stopReason, "virtual-target-disabled") == 0 ||
        std::strcmp(stopReason, "persona-changed") == 0 ||
        std::strcmp(stopReason, "coordinator-stop") == 0) {
        g_wakeHoldPhysicalUnhide.store(false, std::memory_order_release);
        g_wakeRescanRequested.store(false, std::memory_order_release);
    }
    // 批93（2026-09-12 回滚单）：批88 的 stopKernel0/stopUser0 CPU 采样已删
    // （温和退出整段撤销，v2 恢复）。
    // A child that never completed HELLO and never prepared a target cannot
    // own HIDMaestro state. If its bounded wait expires, terminate only this
    // startup-failed child; established target/session failures remain
    // release-unproven and are never force-killed here.
    const bool startupUnacknowledged = hadProcess && !g_inputHostHello && !targetMayExist;
    const bool transportUsable = g_inputHostHello && !g_inputHostTransportFault;
    // 批113（实机 RCA）：b112 包三次 elite 停止都在停止请求后 ~1 ms 就出
    // release-receipt{ok:false}，且**没有任何** QUIESCE/SUBMIT_NEUTRAL/RELEASE_TARGET
    // 命令回执——即有序拆除根本没发出去（唯一静默早退 = 传输身份四要素缺一）。
    // 身份位与"本次有序拆除是否真的发出"必须当场可读，否则下一轮仍只能猜。
    const bool transportIdentityReady = g_inputHostProcess != nullptr &&
        !g_inputHostPipeName.empty() && !g_inputHostRunId.empty() &&
        !g_inputHostStartupNonce.empty();
    appendNativeLifecycleLog("input-host-stop-request", {
        {"reason", reason ? reason : "unknown"},
        {"stopReason", reason ? reason : "unknown"},
        {"intentOrigin", inputHostIntentOriginForReason(reason)},
        {"intentId", std::string("stop:") + (reason ? reason : "unknown")},
        {"exitSessionId", g_exitSessionId},
        {"hadProcess", hadProcess},
        {"hadPreparedTarget", hadPreparedTarget},
        {"targetMayExist",targetMayExist},{"prepareCreationPossible",prepareCreationPossible},{"msiSdPinnedOwnership",msiSdPinnedOwnership},{"msiSdReleaseRequired",msiSdReleaseRequired},
        {"transportUsable", transportUsable},
        {"transportIdentityReady", transportIdentityReady},
        {"runId", g_inputHostRunId.empty() ? std::string("UNBOUND") : g_inputHostRunId},
        {"epoch", g_inputHostEpoch},
        {"hostPid", stopHostPid},
        {"hostInstanceId", g_inputHostHostInstanceId}
    });
    if (transportUsable && targetMayExist && !transportIdentityReady) {
        // 有序拆除在 inputHostRequestLocked 的传输身份门处**静默**失败（无命令、无回执）——
        // 这是 b112 包 elite 停止链的唯一缺口形状，必须留证（每次停止至多 1 行）。
        // 判据前提：命令回执行 `input-host-command-result` 是常开白名单项 ⇒ 该窗口里
        // **一条 QUIESCE/SUBMIT_NEUTRAL/RELEASE_TARGET 回执都没有** = 命令确实没发出去。
        appendNativeLifecycleLog("input-host-transport-unavailable", {
            {"processGone", g_inputHostProcess == nullptr},
            {"pipeNameEmpty", g_inputHostPipeName.empty()},
            {"runIdEmpty", g_inputHostRunId.empty()},
            {"nonceEmpty", g_inputHostStartupNonce.empty()},
            {"orderedTeardown", "skipped"},
            {"runId", g_inputHostRunId}, {"epoch", g_inputHostEpoch}
        });
    }
    bool localReleaseAck = false;
    bool shutdownAck = false;
    bool hostExitConfirmed = g_inputHostProcess == nullptr;
    bool processHandleClosePending = false;
    g_inputHostActive = false; // revoke publication before any pipe command.
    g_inputHostLifecycle = "quiescing";
    if (transportUsable && targetMayExist) {
        // HC only advances its connected status after the backend operation
        // succeeds. Do not continue this ordered teardown after a failed
        // QUIESCE or neutral receipt and then mislabel a later dispose as a
        // completed sequence. The Host lease will locally release on a lost
        // Coordinator connection; native must retain release-unproven.
        const bool quiesced = inputHostRequestLocked("QUIESCE", json::object(), "quiesced", 1000);
        inputStopSignalPhaseLocked(stopSignalId,"after-quiesce","QUIESCE",quiesced);
        const bool neutralized = quiesced && inputHostRequestLocked(
            "SUBMIT_NEUTRAL", json::object(), "neutral-accepted", 1000);
        inputStopSignalPhaseLocked(stopSignalId,"after-neutral","SUBMIT_NEUTRAL",quiesced?int(neutralized):-1);
        if (neutralized) {
            // 方案A（HC 同进程模型）：Host 仍存活时先经 pipe 完成 unhide/
            // cloak-off/app-unreg（InputHost 内 HidHideControlService API），
            // 再 RELEASE，避免 SHUTDOWN 后恢复只能走 CLI。
            // 批118：切换期保持隐藏（本机不露）——见 inputHostStopHoldsPhysicalVisibility。
            if (!msiSdReleaseRequired && !g_psEndpointOwnership.retirementRequired() && !g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire) &&
                !inputHostStopHoldsPhysicalVisibility(reason))
                inputHostRestoreVisibilityViaPipeLocked();
            json release;
            localReleaseAck = inputHostRequestLocked("RELEASE_TARGET", json::object(),
                "release-local-dispose-complete", static_cast<DWORD>(ymcc::inputHost::runtimeContract::releaseReceiptCeilingMs), &release);
            inputStopSignalPhaseLocked(stopSignalId,"after-target-release","RELEASE_TARGET",localReleaseAck);
            if (msiSdReleaseRequired) {
                const bool localAck = localReleaseAck;
                localReleaseAck = localAck && g_msiSteamDeckReleaseFence.acceptRelease(release);
                appendNativeLifecycleLog("msi-sd-release-fence", {
                    {"phase","release"},{"localDisposeAck",localAck},{"ownedEndpointAbsent",localReleaseAck},
                    {"reason",g_msiSteamDeckReleaseFence.reason()},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
                    // A missing/malformed release receipt must stay unproven,
                    // not throw before the quarantine/debt cleanup can continue.
                    {"releaseAttempted",true},{"receiptKind",release.type_name()},{"receiptObject",release.is_object()},
                    {"evidence",release.is_object() ? release.value("msiSteamDeckReleaseEvidence",json(nullptr)) : json(nullptr)},
                    {"scope","exact current Host-owned SD endpoints only; not external Steam input proof"}});
                // For opted MSI SD, do not cycle/expose physical input while
                // the old composite target can still exist. Host API remains
                // alive after RELEASE, before the final SHUTDOWN command.
                if (localReleaseAck && !g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire) &&
                    !inputHostStopHoldsPhysicalVisibility(reason))
                    inputHostRestoreVisibilityViaPipeLocked();
            } else if (g_psEndpointOwnership.retirementRequired() && localReleaseAck &&
                    !g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire) &&
                    !inputHostStopHoldsPhysicalVisibility(reason)) {
                // The actual Host release ACK includes its completed endpoint wait.
                // Its API is still alive after RELEASE, exactly as for MSI SD.
                inputHostRestoreVisibilityViaPipeLocked();
            }
        }
        g_inputHostReleaseDisposition = localReleaseAck
            ? "local-dispose-complete/external-release-unproven"
            : "release-unproven";
        g_inputHostLifecycle = localReleaseAck ? "released-local" : "release-unproven";
    } else if (targetMayExist) {
        g_inputHostReleaseDisposition = "release-unproven";
        g_inputHostLifecycle = "release-unproven";
    }
    // SHUTDOWN is only a final orderly command after the preceding local
    // release receipt. On any failed ordered step we leave the Host to its
    // bounded lease cleanup rather than claiming that shutdown repaired it.
    if (transportUsable && (!targetMayExist || localReleaseAck)) {
        shutdownAck = inputHostRequestLocked("SHUTDOWN", json::object(), "shutdown", 1000);
        // 批119：确认 shutdown 后管道退役（Host 立即退出）——避免紧随其后的
        // 释放可见性快照/对账轮询往断管上发 HIDHIDE_QUERY 并记假故障。
        if (shutdownAck) g_inputHostPipeRetired = true;
    }
    appendNativeLifecycleLog("input-host-release-receipt", {
        {"ok", localReleaseAck},
        {"disposition", g_inputHostReleaseDisposition},
        {"runId", g_inputHostRunId},
        {"epoch", g_inputHostEpoch},
        {"hostPid", g_inputHostProcess ? static_cast<unsigned long long>(GetProcessId(g_inputHostProcess)) : 0ULL},
        {"hostInstanceId", g_inputHostHostInstanceId},
        {"keyboardModifiersAtStopBegin", keyboardModifiersAtStopBegin}
    });
    // 2026-09-23 W3 批 99（operator 授权）：**释放终态复核**。上一行只说"本地拆完、外部未证明"，
    // 而"YMCC 不在时消费者看到什么"在产品内部此前无话可说（现场判 B1 的死结）。这里在停止
    // 时刻用既有可见性原语复核一次并落**专用常开行**（每次停止 1 行，不把整片快照拉进常开区）。
    {
        HidHideVisibilitySnapshot releaseSnap;
        const bool releaseReadback = hidHideCaptureVisibilitySnapshot(releaseSnap);
        const auto& relJournal = g_hidHideVisibilityJournal;
        const bool relPhysicalHidden = releaseReadback &&
            (hidHideContainsPath(releaseSnap.hidden, relJournal.physical.deviceInstanceId) ||
             hidHideContainsPath(releaseSnap.hidden, relJournal.physical.baseContainerDeviceInstanceId));
        appendNativeLifecycleLog("input-host-release-visibility", {
            // 批107 / Q3：**本行是恢复之前的快照**（sweep/restore 在其后执行）——现场曾把这里的
            // physicalHiddenByHidHide:true 误读成"退出后仍隐藏"。最终真值读 input-host-stop-result
            // 的 visibilityRestored 与 hidhide.p-hid-restore 的 status。
            {"snapshotPhase", "before-restore"},
            {"readback", releaseReadback},
            {"cloakOn", releaseReadback && releaseSnap.cloaking},
            {"physicalHiddenByHidHide", relPhysicalHidden},
            {"virtualEndpointAbsent", g_xinputPhysicalSourceLocked &&
                (g_xinputConnectedMask & ~g_xinputPhysicalSourceMask) == 0},
            {"hostAlive", g_inputHostProcess != nullptr},
            {"localReleaseAck", localReleaseAck}, {"shutdownAck", shutdownAck},
            {"disposition", g_inputHostReleaseDisposition},
            {"reason", reason ? reason : "unknown"}
        });
    }
    // R103: after the final ordered command has failed/been skipped, EOF is
    // the existing Host cleanup signal. Retire transport BEFORE waiting for
    // exit; retain the process handle and never manufacture a release ACK.
    if (!shutdownAck) {
        if (g_inputHostServerPipe) {
            inputHostPipeCloseOwned(g_inputHostServerPipe);
            g_inputHostServerPipe = nullptr;
        }
        g_inputHostPipeConnected = false;
        g_inputHostPipeRetired = true;
    }
    if (g_inputHostProcess) {
        // 批93（2026-09-12 回滚单）：v3 的温和退出（RELEASE 3s/SHUTDOWN 3s/
        // 2s probe/established 硬杀/graceful-stages）已整段撤销，恢复 v2：
        // No TerminateProcess fallback —— HIDMaestro teardown 有自己的驱动移除
        // 预算；超时保持 release-unproven 而非 device-gone（established 永不
        // 硬杀，仅有 startup-orphan 快速清理）。
        const auto processRetirement = ymcc::inputHost::processRetirement::retire(
            g_inputHostProcess, [&](HANDLE process) {
                DWORD wait = ymStartHostExitWait(process);
                DWORD waitError = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
                if (wait == WAIT_TIMEOUT) {
                    if (startupUnacknowledged && TerminateProcess(process, 1)) {
                        const DWORD terminatedWait = WaitForSingleObject(process, 1000);
                        wait = terminatedWait;
                        waitError = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
                        g_inputHostReleaseDisposition = terminatedWait == WAIT_OBJECT_0
                            ? "startup-failed-child-terminated"
                            : "startup-failed-child-termination-unproven";
                        appendNativeLifecycleLog("input-host-startup-orphan-cleanup", {
                            {"reason", reason ? reason : "unknown"},
                            {"terminated", terminatedWait == WAIT_OBJECT_0},
                            {"epoch", g_inputHostEpoch},
                            {"runId", g_inputHostRunId}
                        });
                        writeLog("WARN", json({{"event", "input-host.startup-orphan-terminated"},
                            {"reason", reason}, {"epoch", g_inputHostEpoch}, {"runId", g_inputHostRunId}}).dump());
                    } else {
                        g_inputHostReleaseDisposition = "release-unproven/host-exit-timeout";
                        writeLog("WARN", json({{"event", "input-host.exit-timeout-no-force-kill"},
                            {"reason", reason}, {"epoch", g_inputHostEpoch}, {"runId", g_inputHostRunId}}).dump());
                    }
                }
                return ymcc::inputHost::processRetirement::WaitResult{wait, waitError};
            });
        hostExitConfirmed = processRetirement.exitConfirmed;
        processHandleClosePending = processRetirement.closeAttempted && !processRetirement.handleClosed;
        appendNativeLifecycleLog("input-host-process-retirement", {
            {"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
            {"waitState",processRetirement.waitState},{"waitError",processRetirement.waitError},
            {"hostExitConfirmed",hostExitConfirmed},{"closeAttempted",processRetirement.closeAttempted},
            {"handleClosed",processRetirement.handleClosed},{"closeError",processRetirement.closeError},
            {"hostHandleRetained",g_inputHostProcess!=nullptr},
            {"scope","process-handle-only; endpoint-and-physical-return-independent"}});
    }
    // HC restores the selected physical controller's HidHide registration
    // after the virtual target has been disconnected. Keep this visibility
    // receipt separate from Host-local release and never infer P-XINPUT or
    // game-consumer closure from either result.
    const bool releaseProven = !targetMayExist ||
        (localReleaseAck && shutdownAck && hostExitConfirmed);
    if(hostExitConfirmed && releaseProven)
        inputHostTargetReturnTargetExitedLocked(g_inputHostRunId,g_inputHostEpoch);
    // Explicit OFF/exit may reclaim legacy visibility only after this exact
    // old child exited. Player intent never overrides a still-live target owner.
    const bool explicitRestoreAfterExit = hostExitConfirmed && inputHostStopRestoresVisibilityWithoutProof(reason);
    const bool psReturnRequired = g_psEndpointOwnership.retirementRequired();
    bool psReturnProven = !psReturnRequired;
    if (psReturnRequired && hostExitConfirmed) {
        if (releaseProven) psReturnProven=true;
        else psReturnProven=psRetiredReturnProvenLocked();
    }
    bool visibilityRestored = false;
    const char* visibilityDecision = "unknown";
    // 批118：切换期"保持隐藏"独立记账——不冒充 restored，也不进隔离（端点状态本身是好的）。
    bool visibilityHeld = false;
    if (msiSdReleaseRequired && !releaseProven) {
        visibilityDecision = "msi-sd-release-fence";
        g_inputHostReleaseQuarantined = true;
        g_inputHostStartBlocked = true;
        g_msiSteamDeckReleaseDebt = g_msiSteamDeckReleaseFence;
        appendNativeLifecycleLog("msi-sd-release-fence", {
            {"phase","return-deferred"},{"reason","SD-owned-endpoint-or-host-exit-unproven"},
            {"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
            {"physicalReturnAttempted",false},{"hadPreparedTarget",hadPreparedTarget},{"currentPinnedOwnership",msiSdPinnedOwnership},
            {"ownedTargets",g_msiSteamDeckReleaseDebt.ownedTargets()},{"scope","MSI SD only; no XInput substitute"}});
    } else if (psReturnRequired && !psReturnProven) {
        visibilityDecision = "ps-owned-return-unproven";
        g_inputHostReleaseQuarantined=true;g_inputHostStartBlocked=true;
    } else if (releaseProven && inputHostStopHoldsPhysicalVisibility(reason)) {
        visibilityDecision = "persona-held";
        visibilityHeld = hidHideHoldPhysicalTransaction(reason);
    } else if (g_wakeHoldPhysicalUnhide.load(std::memory_order_acquire)) {
        visibilityDecision = "wake-held";
        // Sleep-wake hot replace must never Unhide. Keep the journal and
        // skip quarantine/startBlocked so a dead Host can reopen.
        appendNativeLifecycleLog("hidhide.p-hid-restore-held", {
            {"reason", reason ? reason : "unknown"},
            {"releaseProven", releaseProven},
            {"pXinput", "UNENCLOSED"}
        });
    } else if (releaseProven || explicitRestoreAfterExit) {
        visibilityDecision = "restore-attempted";
        visibilityRestored = hidHideRestorePhysicalTransaction(reason);
        visibilityDecision = visibilityRestored ? "restored" : "restore-failed";
        if (!visibilityRestored) {
            g_inputHostReleaseQuarantined = true;
            g_inputHostStartBlocked = true;
        }
        else g_inputHostReleaseQuarantined = false;
    } else {
        visibilityDecision = "deferred-quarantine";
        // HC never exposes the physical source again while the old virtual
        // target is still unresolved.  This is the explicit YMCC process
        // boundary delta: no early HidHide restore after crash, pipe loss, or
        // an incomplete RELEASE/SHUTDOWN receipt.
        g_inputHostReleaseQuarantined = true;
        g_inputHostStartBlocked = true;
        appendNativeLifecycleLog("hidhide.p-hid-restore-deferred", {
            {"reason", reason ? reason : "unknown"},
            {"localReleaseAck", localReleaseAck},
            {"shutdownAck", shutdownAck},
            {"hostExitConfirmed", hostExitConfirmed},
            {"safeStop", true}, {"pXinput", "UNENCLOSED"}
        });
    }
    if (psReturnRequired && psReturnProven && (visibilityRestored || visibilityHeld ||
            std::strcmp(visibilityDecision,"wake-held")==0))
        g_psEndpointOwnership.confirmRetirement(g_inputHostRunId,g_inputHostEpoch);
    // A failed CloseHandle is still this exact old session's debt. Apply the
    // existing retry/start gate AFTER independent visibility/endpoint decisions:
    // handle-close failure alone must not suppress an otherwise proven restore.
    // Clear retains the tuple while the handle survives; quarantine maintenance
    // must use the same checked retirement helper before clearing this session.
    if (processHandleClosePending) {
        g_inputHostReleaseQuarantined = true;
        g_inputHostStartBlocked = true;
    }
    // 批 104 / X-3：XInput 平面回执（使"360 → 本机"可判：掩码/锁定/准入）。
    appendNativeLifecycleLog("input-host-xinput-plane", {
        {"phase", "stop"}, {"reason", reason ? reason : "unknown"},
        {"locked", g_xinputPhysicalSourceLocked},
        {"physicalMask", g_xinputPhysicalSourceMask},
        {"connectedMask", g_xinputConnectedMask},
        {"visibilityRestored", visibilityRestored},
        {"visibilityDecision", visibilityDecision}
    });
    writeLog("INFO", json({
        {"event", "input-host.stop"}, {"reason", reason}, {"hadProcess", hadProcess},
        {"localReleaseAck", localReleaseAck}, {"releaseDisposition", g_inputHostReleaseDisposition},
        {"visibilityRestored", visibilityRestored}, {"visibilityDecision", visibilityDecision},
        {"keyboardModifiersAtStopBegin", keyboardModifiersAtStopBegin},
        {"pXinput", "UNENCLOSED"}
    }).dump());
    if(msiControllerTraceEnabled())msiControllerTraceLog("host-stop-result",{{"reason",reason?reason:"unspecified"},{"stopHostPid",stopHostPid},{"releaseQuarantined",g_inputHostReleaseQuarantined}},true);
    msiExplicitRearmRelease(reason,g_inputHostReleaseQuarantined,localReleaseAck,shutdownAck,hostExitConfirmed,msiStopPower);
    appendNativeLifecycleLog("input-host-stop-result", {
        {"stopReason", reason ? reason : "unknown"},
        {"intentOrigin", inputHostIntentOriginForReason(reason)},
        {"intentId", std::string("stop:") + (reason ? reason : "unknown")},
        {"exitSessionId", g_exitSessionId},
        {"localReleaseAck", localReleaseAck},
        {"targetMayExist",targetMayExist},{"prepareCreationPossible",prepareCreationPossible},{"msiSdPinnedOwnership",msiSdPinnedOwnership},{"explicitRestoreAfterExit",explicitRestoreAfterExit},{"psReturnRequired",psReturnRequired},{"psReturnProven",psReturnProven},{"hostExitConfirmed",hostExitConfirmed},
        {"releaseDisposition", g_inputHostReleaseDisposition},
        {"quarantined", g_inputHostReleaseQuarantined},
        {"visibilityRestored", visibilityRestored},
        {"visibilityDecision", visibilityDecision},
        // 批118：切换期有意保持隐藏（本机不露）——与 visibilityRestored 互斥；判据按它取"有意"。
        {"visibilityHeld", visibilityHeld},
        {"holdReason", visibilityHeld ? "persona-changed" : ""},
        {"pXinput", "UNENCLOSED"},
        // R10-R2 M0：本行记录**停止起点**的真实 Host PID（句柄可能已在此前关闭 ⇒ 必须在
        // 起点快照后同行携带，不能在结果处重取）。runId/epoch 同行保留 owner token。
        {"hostPid", stopHostPid},
        {"runId", g_inputHostRunId.empty() ? std::string("UNBOUND") : g_inputHostRunId},
        {"epoch", g_inputHostEpoch},
        {"keyboardModifiersAtStopBegin", keyboardModifiersAtStopBegin},
        {"hostPidSemantics", "host-process-pid-not-native-pid"}
    });
    // 批131（照挖掘回函 §C-③ 的"关·拆塔"与"关·可见性"两拍；只加行，不改任何动作）：
    //   · device-removed = 本塔已拆（含耗时与是否隔离）；
    //   · toggle-result = **正向断言**（答 §17.5-Q4）：物理是否应可见，由三个实时事实导出。
    inputHostSwitchAuditDeviceRemoved(reason, stopPersonaBefore,
                                      GetTickCount64() - stopStartedTick,
                                      g_inputHostReleaseDisposition, g_inputHostReleaseQuarantined);
    {
        HidHideVisibilitySnapshot postSnap;
        const bool postReadback = hidHideCaptureVisibilitySnapshot(postSnap);
        inputHostSwitchAuditToggleResult(reason, !g_inputHostReleaseQuarantined,
                                         GetTickCount64() - stopStartedTick,
                                         !g_hidHideVisibilityJournal.active,
                                         postReadback && postSnap.cloaking,
                                         (unsigned long long)g_xinputConnectedMask,
                                         (unsigned long long)g_xinputPhysicalSourceMask);
    }
    // GP-MSIG3E-3（§3.B L47）：虚拟手柄生命周期终了时，若本程曾把物理手柄切到 DInput 会话
    // 模式，归还 XInput(1) 并撤路线标志（幂等；非 Claw / 未武装时零成本早退）。
    {
        const bool retainPhysicalMode = msiPersonaModeRetain(reason,releaseProven,visibilityHeld);
        const bool routeWasArmed = retainPhysicalMode
            ? g_msiDinputRouteDinputArmed.load(std::memory_order_acquire)
            : g_msiDinputRouteDinputArmed.exchange(false, std::memory_order_acq_rel);
        g_msiDinputRouteConfirmed.store(false, std::memory_order_release);
        g_msiDinputAdmissionLogged.store(false, std::memory_order_release);
        // R10-R2 窄范围 C（M3_NORMAL_STOP_CALLCHAIN）：普通 stop / virtual-target-off / persona
        // 切换必须与 disable 同序完成「释放受管主源 → 设备侧目标事实成立 → post-release mode1
        // 一次尝试」。修正前这里只发一次 restore：主源记录未撤销、sourceValid 未清 ⇒ 严格门读到
        // 的 deviceTargetAbsent() 恒 false，restore 被 deny("restore-deferred-device-facts")，
        // 仅 app-exit 第二拍 msiDinputSetEnabled(false) 才补齐——普通关闭会残留 DInput。
        // 顺序不可交换：releasePad 先撤受管主源记录（deviceTargetAbsent 的前置），clearOwnedState
        // 再清自有键态/身份（sourceValid）；两者完成后 restore 才在严格门下通过。
        // 不触碰 g_msiDinput.enabled（仍由每 tick 身份重算门控），不释放 DInput8 实例
        // g_msiDinput.di（由 app-exit disable 收口），openTried/backoffMs 不改（非 enable 生命周期）。
        // 守卫：routeWasArmed 覆盖已武装；未武装但本 episode 仍欠 mode1 归还（g_msiDinputReturnOwed，
        // 例如 route 已被前置路径撤销）时也必须走释放链，否则主源记录残留。"armed && !owed" 不可能
        // （任何 arm 必经 Request 登记债务）；"!armed && !owed" = 从未建立 DInput 会话 ⇒ 零成本早退。
        if (routeWasArmed || g_msiDinputReturnOwed.load(std::memory_order_acquire)) {
            // An exhausted-source fallback cannot expose mode1 beside an
            // unretired virtual target. Keep the existing debt on failed release.
            const bool deferPhysicalReturn=targetMayExist && !releaseProven &&
                std::strcmp(stopReason,"msi-dinput-route-window-exhausted")==0;
            msiDinputStopForLifecycle("input-host-stop",retainPhysicalMode,deferPhysicalReturn);
            if(deferPhysicalReturn)appendNativeLifecycleLog("msi-claw-gamepad-mode",{
                {"action","route-fallback-mode1-deferred"},{"reason","owned-target-retirement-unproven"},
                {"targetMayExist",targetMayExist},{"hostExitConfirmed",hostExitConfirmed},
                {"returnDebtRetained",g_msiDinputReturnOwed.load(std::memory_order_acquire)},
                {"writeAttempted",false},{"policy","VC-MSI-EXCLUSIVE-HANDOFF-20261024-R1"}});
            if(retainPhysicalMode)
                msiPersonaModeNote("old-source-released","old-frame-confirmation-and-recovery-window-cleared");
        }
        msiPersonaModeStopDone(retainPhysicalMode);
    }
    inputStopSignalPhaseLocked(stopSignalId,"after-physical-return-attempt",nullptr,-1,true);
    inputHostClearLocked();
    // Local cleanup completion is distinct from real game/device consumption.
    closeReceipt.outcome = inputHostDisabledCleanupProvenLocked() ? "success" : "failure";
    // The durable journal (or native recovery) owns the actual driver restore.
}

MsiPersonaModeStartScope::~MsiPersonaModeStartScope() noexcept {
    if(!owner)return;
    if(g_msiPersonaModeHandoff.active){
        g_msiPersonaModeHandoff.aborting=true;
        g_msiPersonaReadPause.store(true,std::memory_order_release);
        try {
            // The MSI pump never takes g_inputHostMx; its candidate try-lock
            // cannot deadlock this rollback. No other family's pump is gated.
            if(!g_msiPersonaPumpHold.owns_lock())g_msiPersonaPumpHold=std::unique_lock<std::mutex>(g_msiPersonaPumpMx);
            try {msiPersonaModeNote("aborted","start-did-not-complete-normal-stop-compensation");}catch(...){}
            inputHostStopLocked("msi-persona-handoff-aborted");
        } catch (...) {
            // Unprovable rollback is MSI-only fail-closed: do not expose an
            // unresolved virtual endpoint or fabricate a mode-return receipt.
            g_inputHostReleaseQuarantined=true;g_inputHostStartBlocked=true;
            // Keep the existing return debt for lifecycle cleanup; never leave
            // the new pause/hold latched.
        }
        msiPersonaModeReset();
    }
    g_msiPersonaStartScope=false;
}

static void inputHostStop(const char* reason) {
    // GP-XBOX-2（裁决 §4）：停 Host 全过程（QUIESCE→RELEASE→SHUTDOWN，最坏 ≈13 s）
    // 属于生命周期忙段；快照必须在此期间保持 busy，避免恢复服务在释放途中判死。
    RecoveryStateBusyGuard recoveryBusyGuard("input-host-stop");
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    inputHostStopLocked(reason);
}

