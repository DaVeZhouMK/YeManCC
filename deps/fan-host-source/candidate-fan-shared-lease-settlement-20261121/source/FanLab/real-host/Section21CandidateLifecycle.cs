using System.Diagnostics;
using System.Reflection;
using System.Text.Json;

namespace YeManFanHost;

/**
 * 920-v1.9 §21：清理周期统一规则 + 真实自动恢复入口（无硬件）。
 *
 * 原则：
 * - 驱动**候选真实**生命周期与 `HidReadIoGuard`（不是替身状态机）；只在 OS/HID/ACPI 边界注入；
 * - A 项：一个周期的资源清单与分项结果（摘除失败/部分摘除/失败重试/稳定终态/观测冻结/新会话准入）；
 * - B 项：守卫层“已释放 = 稳定终态”与“最后读退出 vs Retire 竞争”；
 * - C 项：**同一生产 Host engine 的自动恢复入口**——注入一次清理失败 → 解除 → 只推动原有事件/协调器，
 *   证明原用户曲线自动恢复且下一次调整可执行（不手工连续 Close/Enable 帮恢复；沿用原 60s 重试预算）。
 * 失败即以异常结束 ⇒ 进程非零退出。
 */
internal static class Section21CandidateLifecycle
{
    /** §21 组最少检查条数：§18/§19/§20 的 43 条 + §21 的 11 条 = 54。 */
    internal const int MinimumCheckCount = 54;

    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection21CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    internal static IReadOnlyList<string> RunSection21CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;

        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("§21: candidate reflection bridge missing");
        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var gate = new InterleaveGate();
        reflection.SetInterleaveHook(gate.OnPoint);
        try
        {
            var device = backend.OnStaBounded(reflection.CreateRogAlly, "s21-create-candidate-device");
            fakes.Boundary.HcDevice = device;
            reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
            checks.Add(
                $"candidate-identity-21 sha256={candidateHash} mvid={reflection.ModuleVersionId} " +
                "cleanupLedger=per-generation-cycles detachFailureHonest=true stableReleaseTerminal=true");

            // ---- A：清理周期规则（顺序固定：末位项会保留未结算责任，必须最后跑）----
            Run21_DetachFailureNotCompleted(fakes, reflection, backend, device, checks);
            Run21_FailedReleaseRetriedInCycle(fakes, reflection, backend, device, checks);
            Run21_RepeatCloseAfterSuccess(fakes, reflection, backend, device, checks);
            Run21_LateObserverAndNewSession(fakes, reflection, backend, device, checks);
            Run21_PartialDetachThenThrow(fakes, reflection, backend, device, checks);
            // ---- B：守卫稳定终态与最后读竞争 ----
            Run21_GuardStableTerminal(backend, reflection, checks);
            Run21_LastReaderVsRetire(backend, reflection, checks);
            // ---- C：同一生产 engine 的自动恢复（清理失败注入）----
            Run21_AutoRecoveryAfterCleanupFailure(engine, candidatePath, candidateHash, fakes, reflection, checks);
            return checks;
        }
        finally
        {
            reflection.SetInterleaveHook(null);
            gate.Disarm();
        }
    }

    // ==================================================================
    // A：一个清理周期的规则
    // ==================================================================

    /** 摘除异常 ⇒ 不得报完成；随后同一代次 Close 接续周期重试摘除并完成（不丢责任、不误报）。 */
    private static void Run21_DetachFailureNotCompleted(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.FailDetach = 1;

        var close1 = reflection.CloseDeviceLifecycle(device);
        var completed1 = reflection.GetBool(close1, "CleanupCompleted");
        var cleared1 = reflection.GetBool(close1, "BindingsCleared");
        var ledger1 = reflection.GetCleanupLedger(device);
        var retained1 = reflection.GetCleanupCycleRetained(device);
        if (completed1 || cleared1)
            throw new InvalidOperationException(
                $"§21A: detach-failure close claimed completion: {reflection.Describe(close1)}");
        if (!ledger1.Contains("detachFailed=True", StringComparison.Ordinal) ||
            !ledger1.Contains("detachRounds=1", StringComparison.Ordinal))
            throw new InvalidOperationException($"§21A: detach failure not recorded in the cycle ledger: {ledger1}");
        if (!retained1 || fakes.Boundary.ReleaseDetachedCalls != 0)
            throw new InvalidOperationException(
                $"§21A: detach failure lost its responsibility (retained={retained1} releaseCalls={fakes.Boundary.ReleaseDetachedCalls})");
        checks.Add(
            $"candidate21-a[detach-failure-not-completed] completed={completed1} bindingsCleared={cleared1} " +
            $"cycleRetained={retained1} releaseCalls=0 ledger={Truncate(ledger1, 150)}");

        // 同一代次 Close：接续同一周期重试摘除 → 取得实例并释放。
        var close2 = reflection.CloseDeviceLifecycle(device);
        var completed2 = reflection.GetBool(close2, "CleanupCompleted");
        var retained2 = reflection.GetCleanupCycleRetained(device);
        if (!completed2 || retained2 || fakes.Boundary.DevicesReleased < 1)
            throw new InvalidOperationException(
                $"§21A: detach-failure resume did not complete in the original cycle: " +
                $"{reflection.Describe(close2)} retained={retained2} released={fakes.Boundary.DevicesReleased}");
        checks.Add(
            $"candidate21-a[detach-failure-resume] completed={completed2} releaseCalls={fakes.Boundary.ReleaseDetachedCalls} " +
            $"devicesReleased={fakes.Boundary.DevicesReleased} cycleRetained={retained2}");
    }

    /** 释放失败 ⇒ 原周期按原实例重试成功（不用新空表覆盖旧失败资源）。 */
    private static void Run21_FailedReleaseRetriedInCycle(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.FailDetachedReleases = 1;

        var close1 = reflection.CloseDeviceLifecycle(device);
        var ledger1 = reflection.GetCleanupLedger(device);
        if (reflection.GetBool(close1, "CleanupCompleted") ||
            !reflection.GetCleanupCycleRetained(device) ||
            fakes.Boundary.ReleaseFailures != 1 ||
            !ledger1.Contains(":Failed:attempts=1", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§21A: failed release was not kept in the cycle: {reflection.Describe(close1)} ledger={ledger1}");

        var close2 = reflection.CloseDeviceLifecycle(device);
        var releaseCallsAfterRetry = fakes.Boundary.ReleaseDetachedCalls;
        if (!reflection.GetBool(close2, "CleanupCompleted") ||
            reflection.GetCleanupCycleRetained(device) ||
            releaseCallsAfterRetry < 2 ||
            fakes.Boundary.DevicesReleased < 1)
            throw new InvalidOperationException(
                $"§21A: same-cycle retry did not resolve the failed resource: {reflection.Describe(close2)} " +
                $"releaseCalls={releaseCallsAfterRetry} released={fakes.Boundary.DevicesReleased}");
        checks.Add(
            $"candidate21-a[failed-release-retried-in-cycle] firstComplete=false retryComplete=true " +
            $"releaseCalls={releaseCallsAfterRetry} devicesReleased={fakes.Boundary.DevicesReleased} " +
            $"retained=false");
    }

    /** 已成功释放为稳定终态：重复 Close 不再次释放、不退回失败。 */
    private static void Run21_RepeatCloseAfterSuccess(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();

        var close1 = reflection.CloseDeviceLifecycle(device);
        var callsAfterFirst = fakes.Boundary.ReleaseDetachedCalls;
        if (!reflection.GetBool(close1, "CleanupCompleted") || callsAfterFirst < 1)
            throw new InvalidOperationException($"§21A: normal close did not complete: {reflection.Describe(close1)}");

        var close2 = reflection.CloseDeviceLifecycle(device);
        if (!reflection.GetBool(close2, "CleanupCompleted") ||
            fakes.Boundary.ReleaseDetachedCalls != callsAfterFirst ||
            reflection.GetCleanupCycleRetained(device))
            throw new InvalidOperationException(
                $"§21A: repeat close after success released again or lost its result: {reflection.Describe(close2)} " +
                $"releaseCalls={fakes.Boundary.ReleaseDetachedCalls} (first={callsAfterFirst})");
        checks.Add(
            $"candidate21-a[repeat-close-after-success] secondCompleted=true releaseCalls={fakes.Boundary.ReleaseDetachedCalls} " +
            $"releaseSuccessIsTerminal=true");
    }

    /** 旧资源待处理时新会话准入；旧收据的报告文本不被后续周期改写（观测冻结）。 */
    private static void Run21_LateObserverAndNewSession(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.FailDetachedReleases = 1;
        var close1 = reflection.CloseDeviceLifecycle(device);
        if (reflection.GetBool(close1, "CleanupCompleted"))
            throw new InvalidOperationException("§21A: first close unexpectedly completed");
        var report1Before = reflection.DescribeReceiptReport(close1);

        // 新会话准入（不手工 Close；旧周期“已结算但有未完成资源”不得阻塞新会话）。
        if (reflection.CallInstance(device, "Open") is not true)
            throw new InvalidOperationException("§21A: new session publish failed");
        var nextId = reflection.StartDeviceOpen(device, true);
        var nextReceipt = reflection.WaitOpenReceipt(device, nextId, 20_000)
            ?? throw new InvalidOperationException("§21A: new-session operation did not finish");
        var nextOutcome = reflection.GetString(nextReceipt, "Outcome");
        if (nextOutcome != "Completed")
            throw new InvalidOperationException(
                $"§21A: new session was blocked by retained (settled) old resources: {reflection.Describe(nextReceipt)}");
        checks.Add(
            $"candidate21-a[new-session-admission-while-old-pending] newOutcome={nextOutcome} " +
            $"oldCycleRetained={reflection.GetCleanupCycleRetained(device)} " +
            $"ledger={Truncate(reflection.GetCleanupLedger(device), 120)}");

        // 新会话自己的 Close：旧周期在自己的代次条目里被接续重试；旧收据报告保持不变。
        var close2 = reflection.CloseDeviceLifecycle(device);
        var report1After = reflection.DescribeReceiptReport(close1);
        var retained2 = reflection.GetCleanupCycleRetained(device);
        if (!reflection.GetBool(close2, "CleanupCompleted") || retained2)
            throw new InvalidOperationException(
                $"§21A: close after new session did not settle every cycle: {reflection.Describe(close2)} " +
                $"ledger={reflection.GetCleanupLedger(device)}");
        if (!string.Equals(report1Before, report1After, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§21A: late observer rewrote a published report:\n before={report1Before}\n after ={report1After}");
        checks.Add(
            $"candidate21-a[late-observer-frozen-report] reportUnchanged={string.Equals(report1Before, report1After, StringComparison.Ordinal)} " +
            $"secondCloseCompleted=true releaseCalls={fakes.Boundary.ReleaseDetachedCalls}");
    }

    /**
     * 部分摘除后抛错：调用方从未拿到资源清单 ⇒ 后续 Close（即使摘除返回空表）也不得声称“已清表”。
     * 本项会**保留**未结算责任（按 §21.2“无法安全重试则保留责任并走既定恢复出口”），因此必须最后执行。
     */
    private static void Run21_PartialDetachThenThrow(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.PartialDetachThenThrow = true;

        var close1 = reflection.CloseDeviceLifecycle(device);
        var ledger1 = reflection.GetCleanupLedger(device);
        if (reflection.GetBool(close1, "CleanupCompleted") ||
            reflection.GetBool(close1, "BindingsCleared") ||
            !ledger1.Contains("detachFailed=True", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§21A: partial-detach failure was not honest: {reflection.Describe(close1)} ledger={ledger1}");

        var close2 = reflection.CloseDeviceLifecycle(device);
        var ledger2 = reflection.GetCleanupLedger(device);
        var completed2 = reflection.GetBool(close2, "CleanupCompleted");
        if (completed2 || !reflection.GetCleanupCycleRetained(device) ||
            !ledger2.Contains("detachRounds=2", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§21A: cycle claimed completion from an empty detach after a partial-detach failure: " +
                $"{reflection.Describe(close2)} ledger={ledger2}");
        checks.Add(
            $"candidate21-a[partial-detach-then-throw-honest] firstCompleted=false secondCompleted={completed2} " +
            $"detachFailures={fakes.Boundary.DetachFailures} responsibilityRetained=true " +
            $"ledger={Truncate(ledger2, 150)}");
    }

    // ==================================================================
    // B：守卫层
    // ==================================================================

    /** 已成功释放的同一实例是稳定终态：重复 Retire 返回既有结果，不再 Dispose。 */
    private static void Run21_GuardStableTerminal(RealHcBackend backend, CandidateReflection reflection, List<string> checks)
    {
        var (guard, target) = CreateGuard(backend, reflection);
        var first = reflection.GuardRetire(guard, 0, out _);
        var second = reflection.GuardRetire(guard, 0, out var detail2);
        if (first != "Released" || second != "Released" ||
            target.CallsSeen != 1 || !reflection.GuardIsSettled(guard))
            throw new InvalidOperationException(
                $"§21B: released handle was not a stable terminal state: first={first} second={second} " +
                $"calls={target.CallsSeen} detail={detail2}");
        checks.Add(
            $"candidate21-b[guard-release-stable-terminal] first={first} second={second} " +
            $"releaseCalls={target.CallsSeen} settled={reflection.GuardIsSettled(guard)}");
    }

    /** 最后读退出与 Retire 竞争：只有一个责任方释放；此后 Retire 只返回既有结果。 */
    private static void Run21_LastReaderVsRetire(RealHcBackend backend, CandidateReflection reflection, List<string> checks)
    {
        var (guard, target) = CreateGuard(backend, reflection);
        if (!reflection.GuardTryEnterRead(guard))
            throw new InvalidOperationException("§21B: in-flight read was not admitted");
        var deferred = reflection.GuardRetire(guard, 0, out var detail);
        if (deferred != "DeferredToInFlightRead")
            throw new InvalidOperationException(
                $"§21B: retire with an in-flight read must defer (got {deferred} detail={detail})");
        reflection.GuardExitRead(guard);
        if (!WaitUntil(() => target.CallsSeen == 1, 10_000))
            throw new InvalidOperationException(
                $"§21B: last reader did not take over the release (calls={target.CallsSeen})");
        var after = reflection.GuardRetire(guard, 0, out var detailAfter);
        if (after != "Released" || target.CallsSeen != 1 || reflection.GuardPending(guard) is not null)
            throw new InvalidOperationException(
                $"§21B: post-reader retire must return the existing result: after={after} calls={target.CallsSeen} " +
                $"detail={detailAfter} pending={reflection.GuardPending(guard)}");
        checks.Add(
            $"candidate21-b[last-reader-vs-retire] retireDeferred=true lastReaderReleased=true " +
            $"releaseCalls={target.CallsSeen} pendingAfter=-");
    }

    // ==================================================================
    // C：同一生产 engine 的自动恢复入口（清理失败注入）
    // ==================================================================

    /**
     * §21.2 真实入口验收：注入**一次**清理失败（睡眠边界释放失败）→ 解除 → 只推动原有事件（Suspend/Resume）
     * 与既有协调器（有界恢复重试窗口）→ 原用户曲线自动恢复 + 下一次调整可执行。
     * 不得手工连续 Close/Enable 帮恢复；沿用原 60s 预算（2s × 30 次）。
     */
    private static void Run21_AutoRecoveryAfterCleanupFailure(
        FanHostEngine ownerEngine,
        string candidatePath,
        string candidateHash,
        CandidateFakes fakes,
        CandidateReflection reflection,
        List<string> checks)
    {
        _ = ownerEngine;
        var (engine, backend, device) = CreateProductionEngineFor19(
            candidatePath, candidateHash, fakes, reflection, "s21-auto-recovery");
        try
        {
            // 用户曲线（明显区别于 OEM 默认表），作为“恢复原意图”的比对基准。
            var userNodes = new[] { new FanNode(0, 0), new FanNode(40, 0), new FanNode(70, 25), new FanNode(100, 85) };
            engine.GrantSelfTestLeaseForSelfTest("s21-lease");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = engine.Enable(BuildEnableBody("s21-lease", userNodes));
            var enableWrites = fakes.Acpi.WriteCalls.ToList();
            if (enableWrites.Count != 3)
                throw new InvalidOperationException($"§21C: user-curve enable issued {enableWrites.Count} writes");
            var defaultCpu = reflection.DefaultCurveFor(AsusFanTarget.Cpu);
            if (enableWrites[0].InBuffer[12..].SequenceEqual(defaultCpu))
                throw new InvalidOperationException("§21C: fixture user curve equals the OEM default table");

            // 注入一次清理失败：睡眠边界的句柄释放失败（表项摘除正常，仅释放失败）。
            fakes.Boundary.ResetCountersKeepSession();
            fakes.Boundary.FailDetachedReleases = 1;
            HostDiagnosticLog.ResetSelfTestCapture();
            // §21.2：恢复窗口内**测试自身**发起的 engine 调用清单（唯一入口是本助手；新增手工 Close/Enable
            // 必须经过它并会改变断言——不用静态字面量自证）。
            var recoveryWindowEngineCalls = new List<string>();
            _ = engine.Suspend(JsonDocument.Parse("{}").RootElement);
            var pendingSeen = engine.Snapshot().HcCloseCleanupPending ||
                              HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "hc-close.cleanup-pending");
            var failureExercised = fakes.Boundary.ReleaseFailures >= 1;
            var ledgerAtFault = reflection.GetCleanupLedger(device);
            if (!pendingSeen || !failureExercised)
                throw new InvalidOperationException(
                    $"§21C: injected cleanup failure was not observed truthfully " +
                    $"(pending={pendingSeen} releaseFailures={fakes.Boundary.ReleaseFailures} " +
                    $"state={engine.Snapshot().State} ledger={ledgerAtFault})");
            if (!ledgerAtFault.Contains(":Failed:attempts=1", StringComparison.Ordinal))
                throw new InvalidOperationException($"§21C: failed resource not kept in the cycle ledger: {ledgerAtFault}");

            // 解除故障（一次性注入已消费；不再调用任何 engine API 帮恢复）。
            fakes.Boundary.FailDetachedReleases = 0;

            // 只推原有协调器：有界恢复重试窗口（2s 间隔）应接续旧周期并结算清理。
            var settleDeadline = Stopwatch.GetTimestamp() + 30 * Stopwatch.Frequency;
            var settled = false;
            while (Stopwatch.GetTimestamp() < settleDeadline)
            {
                if (!engine.Snapshot().HcCloseCleanupPending && !reflection.GetCleanupCycleRetained(device))
                {
                    settled = true;
                    break;
                }
                Thread.Sleep(200);
            }
            var retryAttempts = engine.RecoveryRetryAttemptsForSelfTest;
            var releaseCallsAfterRetry = fakes.Boundary.ReleaseDetachedCalls;
            if (!settled)
                throw new InvalidOperationException(
                    $"§21C: the ordinary coordinator did not settle the cleanup failure " +
                    $"(state={engine.Snapshot().State} pending={engine.Snapshot().HcCloseCleanupPending} " +
                    $"retryAttempts={retryAttempts} ledger={reflection.GetCleanupLedger(device)})");
            // 同周期重试证据：注入一次失败后仍发生了第二次释放调用，且并非测试发起。
            if (releaseCallsAfterRetry < 2 || fakes.Boundary.DevicesReleased < 1)
                throw new InvalidOperationException(
                    $"§21C: the failed resource was not retried in the original cycle by the coordinator: " +
                    $"releaseCalls={releaseCallsAfterRetry} released={fakes.Boundary.DevicesReleased}");
            // 预算证据：绝不超出原 60s/30 次窗口（局部等待不重置、不新增）。
            if (retryAttempts > 30)
                throw new InvalidOperationException(
                    $"§21C: recovery retry budget left the original bounded window: attempts={retryAttempts}");
            var wakeState = engine.Snapshot();

            // 真实唤醒：只推原事件，等待自动恢复完成。
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            var writesAtWake = fakes.Acpi.WriteCalls.Count;
            recoveryWindowEngineCalls.Add("Resume");
            _ = engine.Resume();
            var wakeWait = engine.WaitForAutomaticResumeAsync(60_000);
            try { wakeWait.GetAwaiter().GetResult(); }
            catch (Exception ex)
            {
                throw new InvalidOperationException($"§21C: automatic resume did not complete: {ex.Message}");
            }
            var snapshot = engine.Snapshot();
            if (snapshot.State != "Ready")
                throw new InvalidOperationException(
                    $"§21C: automatic resume ended in {snapshot.State} (expected Ready)");
            var resumeWrites = fakes.Acpi.WriteCalls.Skip(writesAtWake).ToList();
            if (resumeWrites.Count < 3 || resumeWrites.Count % 3 != 0)
                throw new InvalidOperationException(
                    $"§21C: automatic resume write structure is not per-fan triples: writes={resumeWrites.Count}");
            var groupKinds = new List<string>();
            for (var offset = 0; offset + 3 <= resumeWrites.Count; offset += 3)
            {
                var matchesUserCurve = true;
                for (var i = 0; i < 3; i++)
                {
                    if (!resumeWrites[offset + i].InBuffer[12..].SequenceEqual(enableWrites[i].InBuffer[12..]))
                    {
                        matchesUserCurve = false;
                        break;
                    }
                }
                groupKinds.Add(matchesUserCurve
                    ? "user-curve"
                    : resumeWrites[offset].InBuffer[12..].SequenceEqual(defaultCpu)
                        ? "oem-default"
                        : "other");
            }
            if (!groupKinds.Contains("user-curve") || groupKinds[^1] != "user-curve")
                throw new InvalidOperationException(
                    $"§21C: automatic resume did not restore the user curve (groups=[{string.Join(",", groupKinds)}])");

            // 恢复后仍接受下一次用户调整。
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            engine.GrantSelfTestLeaseForSelfTest("s21-lease-2");
            _ = engine.Enable(BuildEnableBody("s21-lease-2", new[]
            {
                new FanNode(0, 0), new FanNode(40, 10), new FanNode(70, 40), new FanNode(100, 95)
            }));
            var adjustmentWrites = fakes.Acpi.WriteCalls.Count;
            if (adjustmentWrites < 3)
                throw new InvalidOperationException(
                    $"§21C: post-recovery user adjustment issued {adjustmentWrites} fan writes");

            // 恢复窗口内测试自身发起的 engine 调用清单（派生计数，不用静态字面量自证）：
            // 清单只应包含推动原事件的 `Resume`；任何手工 Close/Enable 都会出现在清单里并使断言失败。
            var manualCloseCalls = recoveryWindowEngineCalls.Count(call => call == "Close");
            var manualEnableCalls = recoveryWindowEngineCalls.Count(call => call == "Enable");
            if (manualCloseCalls != 0 || manualEnableCalls != 0)
                throw new InvalidOperationException(
                    $"§21C: the test helped the recovery with manual engine calls: " +
                    $"[{string.Join(",", recoveryWindowEngineCalls)}]");
            checks.Add(
                $"candidate21-c[automatic-recovery-after-cleanup-failure] injectedCleanupFailure=true " +
                $"cleanupPendingObserved={pendingSeen} retryAttempts={retryAttempts} " +
                $"releaseCalls={releaseCallsAfterRetry} cleanupSettledByCoordinator={settled} " +
                $"stateAfterRetry={wakeState.State} userCurveRestored=true finalGroupIsUserCurve=true " +
                $"state={snapshot.State} nextUserAdjustmentWrites={adjustmentWrites} " +
                $"recoveryWindowEngineCalls=[{string.Join(",", recoveryWindowEngineCalls)}] " +
                $"manualCloseCalls={manualCloseCalls} manualEnableCalls={manualEnableCalls}");
        }
        finally
        {
            SafeEngineClose(engine, fakes);
        }
    }
}