using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text.Json;

namespace YeManFanHost;

/**
 * 920-v1.8 §20：收尾归属闭合的确定性验证（无硬件；A 交错矩阵 + B 真实释放结果 + C 作用域生命周期）。
 *
 * 原则：
 * - 测试的是**候选真实逻辑**（生命周期/守卫/作用域都不是替身）；只在 OS/HID/ACPI 边界注入行为；
 * - B 项驱动公共类型 `HidReadIoGuard` 的**真实**管理逻辑，只把“句柄释放调用”替换为可控边界
 *   （阻塞、抛错、并发重试），不由 CandidateFake 自证；
 * - A 项覆盖 Open / 启动读取 / remap / 最终提交四个阶段的 Close 与“Close+新会话”成组交错；
 * - 失败即以异常结束 ⇒ 进程非零退出。
 */
internal static class Section20CandidateLifecycle
{
    /** §20 组最少检查条数：§18 的 20 条 + §19 的 15 条 + §20 的 8 条 = 43（低于此值视为证据不完整，验收门失败）。 */
    internal const int MinimumCheckCount = 43;

    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection20CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    /** §20.2-B 注入的“句柄释放目标”：计数、阻塞、抛错、并发可见（不改变守卫管理逻辑）。 */
    internal sealed class GuardReleaseTargetFake
    {
        private readonly object sync = new();
        public string Identity = "guard-target";
        public int Calls;
        public int Failures;
        public int Blocked;
        public int FailFirstNCalls;
        private bool block;
        private readonly ManualResetEventSlim release = new(false);
        private readonly ManualResetEventSlim entered = new(false);

        /** 设为 true 时复位放行事件（避免“上次释放留下的信号”使阻塞失效）。 */
        public bool Block
        {
            get => block;
            set
            {
                block = value;
                if (value) release.Reset();
                else release.Set();
            }
        }

        public int CallsSeen { get { lock (sync) return Calls; } }

        public bool WaitEntered(int timeoutMs) => entered.Wait(timeoutMs);

        public void Unblock() => Block = false;

        /** 等待“在途释放调用”全部结束（用于断言并发不重复释放）。 */
        public bool WaitReleaseLoopSettled(Func<bool> condition, int timeoutMs) => WaitUntil(condition, timeoutMs);

        public void Release()
        {
            lock (sync)
            {
                Calls++;
                if (FailFirstNCalls > 0)
                {
                    FailFirstNCalls--;
                    Failures++;
                    throw new IOException("injected-guard-release-failure");
                }
                if (block) Blocked++;
            }
            if (block)
            {
                entered.Set();
                release.Wait(30_000);
            }
        }
    }

    /** DispatchProxy：候选 IHcHidHandleReleaseTarget → Host 侧可控目标（DispatchProxy 基类不能是 sealed）。 */
    internal class GuardReleaseTargetProxy : DispatchProxy
    {
        public GuardReleaseTargetFake? Target { get; set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) =>
            targetMethod!.Name switch
            {
                "get_Identity" => Target!.Identity,
                "Release" => InvokeRelease(),
                _ => throw new NotSupportedException($"unexpected IHcHidHandleReleaseTarget member: {targetMethod.Name}"),
            };

        private object? InvokeRelease()
        {
            Target!.Release();
            return null;
        }
    }

    internal static IReadOnlyList<string> RunSection20CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;

        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("§20: candidate reflection bridge missing");
        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var gate = new InterleaveGate();
        reflection.SetInterleaveHook(gate.OnPoint);
        try
        {
            var device = backend.OnStaBounded(reflection.CreateRogAlly, "s20-create-candidate-device");
            fakes.Boundary.HcDevice = device;
            reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
            checks.Add(
                $"candidate-identity-20 sha256={candidateHash} mvid={reflection.ModuleVersionId} " +
                $"observationsApi={reflection.GetIntStatic("HandheldCompanion.Devices.ASUS.AsusAcpiObservations", "ApiVersion")} " +
                $"guardType={reflection.GuardTypeName} scopeLifecycle=closed-tracking");

            // ---- A：四阶段 ×（Close / Close+新会话）成组交错 ----
            Run20_CloseInterleaveMatrix(fakes, reflection, backend, device, gate, checks);
            Run20_NewSessionWaitsForCleanup(fakes, reflection, backend, device, checks);
            Run20_CleanupWaitBudget(fakes, reflection, backend, device, checks);
            // ---- B：真实 guard 的释放结果 ----
            Run20_GuardReleaseBlocking(backend, reflection, checks);
            Run20_GuardReleaseFailureAndRetry(backend, reflection, checks);
            Run20_GuardConcurrentReleaseOwnership(backend, reflection, checks);
            // ---- C：作用域生命周期 ----
            Run20_ScopeLifecycle(backend, reflection, fakes, checks);
            return checks;
        }
        finally
        {
            reflection.SetInterleaveHook(null);
            reflection.SetCleanupSettlementWaitOverride(0);
            gate.Disarm();
        }
    }

    // ==================================================================
    // A：旧操作结束 / 清理责任 / 新会话准入
    // ==================================================================

    private readonly record struct PhaseCase(string Phase, string Mode, bool PublishNewSession);

    /**
     * 每个阶段：①在阶段点暂停；②Close（可选同时发布新会话）；③释放暂停后核对终态。
     * 断言：绝不 Completed；副作用按“未发生/已发生/迟到”如实保留；清理可结算。
     */
    private static void Run20_CloseInterleaveMatrix(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        InterleaveGate gate,
        List<string> checks)
    {
        var cases = new[]
        {
            new PhaseCase("before-open", "probe", false),
            new PhaseCase("before-open", "probe", true),
            new PhaseCase("during-open", "blocked-open", false),
            new PhaseCase("during-read-start", "hook-read-start", false),
            new PhaseCase("during-remap", "blocked-remap", false),
            new PhaseCase("during-remap", "blocked-remap", true),
            new PhaseCase("before-commit", "hook-commit", false),
            new PhaseCase("before-commit", "hook-commit", true)
        };

        var summaries = new List<string>();
        foreach (var item in cases)
        {
            var label = $"{item.Phase}{(item.PublishNewSession ? "+new-session" : "")}";
            OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
            fakes.Boundary.ResetCountersKeepSession();
            string operationId;

            switch (item.Mode)
            {
                case "probe":
                {
                    fakes.Boundary.ArmProbePauseOnce();
                    var worker = Task.Run(() => reflection.StartDeviceOpen(device, true));
                    if (!worker.Wait(8_000))
                        throw new InvalidOperationException($"§20A[{label}]: operation did not register");
                    operationId = worker.Result;
                    if (!fakes.Boundary.WaitProbeEntered(8_000))
                        throw new InvalidOperationException($"§20A[{label}]: probe did not reach its pause");
                    ApplyCloseAndOptionalNewSession(fakes, reflection, device, item.PublishNewSession, label);
                    fakes.Boundary.ReleaseBlockedProbe();
                    if (!worker.Wait(15_000))
                        throw new InvalidOperationException($"§20A[{label}]: operation worker did not return");
                    break;
                }
                case "blocked-open":
                {
                    fakes.Boundary.BlockDeviceCall = true;
                    var pausesBefore = fakes.Boundary.DeviceCallPauses;
                    operationId = reflection.StartDeviceOpen(device, true);
                    if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > pausesBefore, 8_000))
                        throw new InvalidOperationException($"§20A[{label}]: open device call never entered its pause");
                    ApplyCloseAndOptionalNewSession(fakes, reflection, device, item.PublishNewSession, label);
                    fakes.Boundary.BlockDeviceCall = false;
                    break;
                }
                case "blocked-remap":
                {
                    // 确定性进入“remap 已发起且不返回”：先在钩子处暂停，武装阻塞，再放行到 remap 调用。
                    gate.Arm("device-call-before-remap-on");
                    operationId = reflection.StartDeviceOpen(device, true);
                    if (!gate.WaitEntered(10_000))
                        throw new InvalidOperationException($"§20A[{label}]: operation did not reach the remap step");
                    fakes.Boundary.BlockDeviceCall = true;
                    var pausesBefore = fakes.Boundary.DeviceCallPauses;
                    gate.Release();
                    if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > pausesBefore, 8_000))
                        throw new InvalidOperationException($"§20A[{label}]: remap call never entered its pause");
                    ApplyCloseAndOptionalNewSession(fakes, reflection, device, item.PublishNewSession, label);
                    fakes.Boundary.BlockDeviceCall = false;
                    break;
                }
                default:
                {
                    var pausePoint = item.Mode == "hook-read-start"
                        ? "device-call-before-start-read-loop"
                        : "commit-before";
                    gate.Arm(pausePoint);
                    operationId = reflection.StartDeviceOpen(device, true);
                    if (!gate.WaitEntered(10_000))
                        throw new InvalidOperationException($"§20A[{label}]: operation did not reach {pausePoint}");
                    ApplyCloseAndOptionalNewSession(fakes, reflection, device, item.PublishNewSession, label);
                    gate.Release();
                    break;
                }
            }

            var receipt = reflection.WaitOpenReceipt(device, operationId, 20_000)
                ?? throw new InvalidOperationException($"§20A[{label}]: operation did not reach a terminal receipt");
            var outcome = reflection.GetString(receipt, "Outcome")!;
            var late = reflection.GetString(receipt, "LateSideEffects") ?? "None";
            var effects = reflection.GetString(receipt, "SideEffects") ?? "None";
            if (outcome == "Completed")
                throw new InvalidOperationException(
                    $"§20A[{label}]: operation reported Completed after close/replacement: {reflection.Describe(receipt)}");

            var expected = item.Mode switch
            {
                "probe" => "none",
                "blocked-open" => "late:HidOpened",
                "hook-read-start" => "late:ReadLoopStarted",
                "blocked-remap" => "late:RemapApplied",
                _ => "effects-all"
            };
            if (expected == "none")
            {
                if (!effects.Contains("None", StringComparison.Ordinal))
                    throw new InvalidOperationException(
                        $"§20A[{label}]: rejected-before-side-effect operation must not own effects: {reflection.Describe(receipt)}");
            }
            else if (expected.StartsWith("late:", StringComparison.Ordinal))
            {
                var flag = expected["late:".Length..];
                if (!late.Contains(flag, StringComparison.Ordinal))
                    throw new InvalidOperationException(
                        $"§20A[{label}]: late side effect {flag} was not preserved: {reflection.Describe(receipt)}");
            }
            else
            {
                foreach (var flag in new[] { "DeviceBound", "HidOpened", "ReadLoopStarted", "RemapApplied" })
                    if (!effects.Contains(flag, StringComparison.Ordinal))
                        throw new InvalidOperationException(
                            $"§20A[{label}]: pre-close effects lost ({flag}): {reflection.Describe(receipt)}");
            }

            // 清理必须可结算；新会话（若已发布）必须仍可再次打开
            fakes.Boundary.CompletePendingReads(failed: true, key: 0);
            reflection.WaitReadLoopEnded(device, 5_000, out _);
            var settled = reflection.WaitCloseCleanupCompleted(device, 10_000, out var settleDetail);
            if (!settled)
                throw new InvalidOperationException(
                    $"§20A[{label}]: cleanup never settled: {settleDetail} | {reflection.GetCleanupCycle(device)}");

            if (item.PublishNewSession)
            {
                if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true)
                    throw new InvalidOperationException($"§20A[{label}]: AsusACPI.Open for the new session failed");
                var nextId = reflection.StartDeviceOpen(device, true);
                var nextReceipt = reflection.WaitOpenReceipt(device, nextId, 20_000)
                    ?? throw new InvalidOperationException($"§20A[{label}]: next-session operation did not finish");
                if (reflection.GetString(nextReceipt, "Outcome") != "Completed")
                    throw new InvalidOperationException(
                        $"§20A[{label}]: next-session operation must complete after cleanup settled: " +
                        $"{reflection.Describe(nextReceipt)} | {reflection.GetCleanupCycle(device)}");
                fakes.Boundary.CompletePendingReads(failed: true, key: 0);
                reflection.WaitReadLoopEnded(device, 5_000, out _);
            }

            summaries.Add($"{label}:{outcome}/{(late == "None" ? "-" : late)}");
        }

        checks.Add(
            $"candidate20-a[close-interleave-matrix] cases={summaries.Count} allRejected=true " +
            $"[{string.Join(" ", summaries)}]");
    }

    private static void ApplyCloseAndOptionalNewSession(
        CandidateFakes fakes,
        CandidateReflection reflection,
        object device,
        bool publishNewSession,
        string label)
    {
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        if (!reflection.GetBool(closeReceipt, "SessionInvalidated"))
            throw new InvalidOperationException($"§20A[{label}]: close did not invalidate the session");
        if (publishNewSession && reflection.CallInstance(device, "Open") is not true)
            throw new InvalidOperationException($"§20A[{label}]: new session publish failed");
    }

    /** 新会话登记等待旧清理，结算后**自动继续**并完成（不是拒绝、不是并发）。 */
    private static void Run20_NewSessionWaitsForCleanup(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.BlockDeviceCall = true;             // 阻塞 Close 清理中的 remap 调用
        var pausesBefore = fakes.Boundary.DeviceCallPauses;
        object? closeReceipt = null;
        var closeWorker = Task.Run(() => closeReceipt = reflection.CloseDeviceLifecycle(device));
        if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > pausesBefore, 8_000))
            throw new InvalidOperationException("§20A-wait: old cleanup did not enter its blocked call");
        if (closeWorker.IsCompleted)
            throw new InvalidOperationException("§20A-wait: close returned although its cleanup call was still stalled");

        // 新会话：发布 + 新操作（旧清理仍在途）
        if (reflection.CallInstance(device, "Open") is not true)
            throw new InvalidOperationException("§20A-wait: new session publish failed");
        var startedAt = Stopwatch.GetTimestamp();
        var newOperationId = reflection.StartDeviceOpen(device, true);
        var blockedWhileCleaning = !WaitUntil(
            () => reflection.TryPeekReceiptOutcome(device, newOperationId) is not null, 800);
        var stateWhileWaiting = reflection.GetCleanupCycle(device);

        // 释放旧清理：新操作应自动继续并完成
        fakes.Boundary.BlockDeviceCall = false;
        if (!closeWorker.Wait(15_000))
            throw new InvalidOperationException("§20A-wait: close did not return after its cleanup was released");
        var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 15_000)
            ?? throw new InvalidOperationException("§20A-wait: new-session operation did not reach a terminal receipt after cleanup settled");
        var waitedMs = (Stopwatch.GetTimestamp() - startedAt) * 1000d / Stopwatch.Frequency;
        var outcome = reflection.GetString(newReceipt, "Outcome");
        if (outcome != "Completed")
            throw new InvalidOperationException(
                $"§20A-wait: new-session operation must continue automatically after old cleanup settled: " +
                $"{reflection.Describe(newReceipt)}");
        if (!reflection.GetBool(newReceipt, "HidOpenedByThisOperation") ||
            !reflection.GetBool(newReceipt, "RemapAppliedByThisOperation"))
            throw new InvalidOperationException($"§20A-wait: new-session receipt lost its own effects: {reflection.Describe(newReceipt)}");
        if (closeReceipt is not null && !reflection.GetBool(closeReceipt, "CleanupCompleted"))
            throw new InvalidOperationException($"§20A-wait: old close cleanup did not settle: {reflection.Describe(closeReceipt!)}");
        checks.Add(
            $"candidate20-a[new-session-waits-for-cleanup] blockedWhileCleaning={blockedWhileCleaning} " +
            $"waitedMs={waitedMs:F0} outcome={outcome} newOwnedEffects=true oldCleanupCompleted=true " +
            $"stateWhileWaiting=[{Truncate(stateWhileWaiting, 110)}]");
    }

    /** 旧清理超出等待预算：新会话以 RejectedCleanupPending 结束（无副作用、不并发硬件调用）。 */
    private static void Run20_CleanupWaitBudget(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        reflection.SetCleanupSettlementWaitOverride(300);   // 有界预算：300ms
        try
        {
            fakes.Boundary.BlockDeviceCall = true;
            var pausesBefore = fakes.Boundary.DeviceCallPauses;
            var closeWorker = Task.Run(() => reflection.CloseDeviceLifecycle(device));
            if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > pausesBefore, 8_000))
                throw new InvalidOperationException("§20A-budget: old cleanup did not enter its blocked call");
            if (reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("§20A-budget: new session publish failed");
            var deviceCallsBefore = fakes.Boundary.HidOpens + fakes.Boundary.RemapOnCount;
            var newOperationId = reflection.StartDeviceOpen(device, true);
            var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 10_000)
                ?? throw new InvalidOperationException("§20A-budget: new-session operation did not reach a terminal receipt");
            var outcome = reflection.GetString(newReceipt, "Outcome")!;
            var effects = reflection.GetString(newReceipt, "SideEffects") ?? "None";
            var phase = reflection.GetString(newReceipt, "Phase") ?? "";
            var deviceCallsAfter = fakes.Boundary.HidOpens + fakes.Boundary.RemapOnCount;
            if (outcome == "Completed")
                throw new InvalidOperationException(
                    $"§20A-budget: operation must not complete while old cleanup exceeded the budget: {reflection.Describe(newReceipt)}");
            if (!phase.Contains("cleanup-pending-timeout", StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"§20A-budget: expected the bounded wait to time out at a device-call step, got phase={phase}");
            if (outcome == "RejectedCleanupPending")
            {
                if (!effects.Contains("None", StringComparison.Ordinal))
                    throw new InvalidOperationException(
                        $"§20A-budget: rejection must not own side effects: {reflection.Describe(newReceipt)}");
            }
            else if (outcome == "PartiallyApplied")
            {
                // 允许“只发布了绑定（表项操作）后才被清理预算挡住”，但不得有任何硬件调用副作用。
                foreach (var forbidden in new[] { "HidOpened", "ReadLoopStarted", "RemapApplied" })
                    if (effects.Contains(forbidden, StringComparison.Ordinal))
                        throw new InvalidOperationException(
                            $"§20A-budget: rejected operation must not touch hardware ({forbidden}): {reflection.Describe(newReceipt)}");
            }
            else
            {
                throw new InvalidOperationException($"§20A-budget: unexpected outcome {outcome}: {reflection.Describe(newReceipt)}");
            }
            if (deviceCallsAfter != deviceCallsBefore)
                throw new InvalidOperationException(
                    $"§20A-budget: rejected operation must not touch the device: before={deviceCallsBefore} after={deviceCallsAfter}");
            fakes.Boundary.BlockDeviceCall = false;
            if (!closeWorker.Wait(15_000))
                throw new InvalidOperationException("§20A-budget: close did not return after its cleanup was released");
            checks.Add(
                $"candidate20-a[cleanup-wait-budget] outcome={outcome} phase={phase} deviceCallsStable=true " +
                $"effects={effects} oldCloseReturned=true");
        }
        finally
        {
            reflection.SetCleanupSettlementWaitOverride(0);
        }
    }

    // ==================================================================
    // B：真实 guard 的释放结果（只替换句柄释放调用）
    // ==================================================================

    private static (object Guard, GuardReleaseTargetFake Target) CreateGuard(RealHcBackend backend, CandidateReflection reflection)
    {
        var target = new GuardReleaseTargetFake();
        var proxy = (GuardReleaseTargetProxy)DispatchProxy.Create(reflection.GuardTargetInterface, typeof(GuardReleaseTargetProxy));
        proxy.Target = target;
        var guard = backend.OnStaBounded(
            () => reflection.CreateGuard("guard-fake-device", proxy),
            "s20-create-guard");
        return (guard, target);
    }

    /** 最后读取者退出 → 释放调用阻塞：在释放返回之前，待释放列表必须继续列出该句柄。 */
    private static void Run20_GuardReleaseBlocking(RealHcBackend backend, CandidateReflection reflection, List<string> checks)
    {
        var (guard, target) = CreateGuard(backend, reflection);
        if (!reflection.GuardTryEnterRead(guard))
            throw new InvalidOperationException("§20B1: guard refused the first read");
        target.Block = true;
        var retireOutcome = reflection.GuardRetire(guard, 0, out var retireDetail);
        if (retireOutcome != "DeferredToInFlightRead")
            throw new InvalidOperationException($"§20B1: retire with an active reader must defer, got {retireOutcome} ({retireDetail})");

        var releaseWorker = Task.Run(() => reflection.GuardExitRead(guard));
        if (!target.WaitEntered(5_000))
            throw new InvalidOperationException("§20B1: last-reader release never entered the injected target");

        // 关键断言：读计数已归零，但释放尚未返回 ⇒ 必须仍在待释放列表、且 IsSettled=false
        var pendingWhileReleasing = reflection.GuardPending(guard);
        var settledWhileReleasing = reflection.GuardIsSettled(guard);
        var readersWhileReleasing = reflection.GuardActiveReaders(guard);
        if (readersWhileReleasing != 0)
            throw new InvalidOperationException($"§20B1: expected readers==0 while releasing, got {readersWhileReleasing}");
        if (settledWhileReleasing)
            throw new InvalidOperationException("§20B1: guard reported settled although the release call had not returned");
        if (string.IsNullOrEmpty(pendingWhileReleasing) || !pendingWhileReleasing!.Contains("releasing", StringComparison.Ordinal))
            throw new InvalidOperationException($"§20B1: pending list lost the in-flight release: pending=[{pendingWhileReleasing}]");

        target.Unblock();
        if (!releaseWorker.Wait(10_000))
            throw new InvalidOperationException("§20B1: last-reader release did not return after unblocking");
        var settledAfter = WaitUntil(() => reflection.GuardIsSettled(guard), 5_000);
        if (!settledAfter)
            throw new InvalidOperationException($"§20B1: guard never settled: {reflection.GuardDescribe(guard)}");
        if (!string.IsNullOrEmpty(reflection.GuardPending(guard)))
            throw new InvalidOperationException("§20B1: settled guard must not stay in the pending list");
        if (target.CallsSeen != 1)
            throw new InvalidOperationException($"§20B1: expected exactly one release call, got {target.CallsSeen}");
        checks.Add(
            $"candidate20-b[release-blocking] readersZeroWhileReleasing=true settledBeforeReturn=false " +
            $"pendingWhileReleasing=[{Truncate(pendingWhileReleasing!, 90)}] releaseCalls={target.CallsSeen} settledAfterUnblock=true");
    }

    /** 释放失败：必须记录为 ReleaseFailed 并**继续出现在待释放列表**；顺序重试成功后才消失。 */
    private static void Run20_GuardReleaseFailureAndRetry(RealHcBackend backend, CandidateReflection reflection, List<string> checks)
    {
        var (guard, target) = CreateGuard(backend, reflection);
        target.FailFirstNCalls = 2;   // 两次顺序失败：Retire 一次 + 顺序重试一次
        var first = reflection.GuardRetire(guard, 0, out var firstDetail);
        if (first != "Failed")
            throw new InvalidOperationException($"§20B2: failing release must be reported as Failed, got {first} ({firstDetail})");
        var pendingAfterFailure = reflection.GuardPending(guard);
        var settledAfterFailure = reflection.GuardIsSettled(guard);
        if (settledAfterFailure)
            throw new InvalidOperationException("§20B2: failed release must not be reported as settled");
        if (string.IsNullOrEmpty(pendingAfterFailure) || !pendingAfterFailure!.Contains("release-failed", StringComparison.Ordinal))
            throw new InvalidOperationException($"§20B2: failed release dropped from the pending list: pending=[{pendingAfterFailure}]");

        // 顺序重试（非并发）：第二次仍失败 → 仍为 Failed
        var second = reflection.GuardRetire(guard, 0, out _);
        if (second != "Failed")
            throw new InvalidOperationException($"§20B2: sequential retry result={second}");
        // 第三次成功 → 结算并从列表消失
        var third = reflection.GuardRetire(guard, 0, out _);
        if (third != "Released")
            throw new InvalidOperationException($"§20B2: retried release result={third}");
        if (!reflection.GuardIsSettled(guard) || !string.IsNullOrEmpty(reflection.GuardPending(guard)))
            throw new InvalidOperationException(
                $"§20B2: settled-after-retry invariant broken: {reflection.GuardDescribe(guard)}");
        if (target.CallsSeen != 3)
            throw new InvalidOperationException($"§20B2: expected three sequential release attempts, got {target.CallsSeen}");
        checks.Add(
            $"candidate20-b[release-failure-retry] first={first} pendingAfterFailure=[{Truncate(pendingAfterFailure!, 70)}] " +
            $"sequentialRetryFailed={second} final={third} releaseCalls={target.CallsSeen} settled=true");
    }

    /** 并发重试释放：同一句柄只允许一个责任方调用底层释放（不重复 CloseHandle）。 */
    private static void Run20_GuardConcurrentReleaseOwnership(RealHcBackend backend, CandidateReflection reflection, List<string> checks)
    {
        var (guard, target) = CreateGuard(backend, reflection);
        if (!reflection.GuardTryEnterRead(guard))
            throw new InvalidOperationException("§20B3: guard refused the first read");
        target.Block = true;
        var retire = reflection.GuardRetire(guard, 0, out _);
        if (retire != "DeferredToInFlightRead")
            throw new InvalidOperationException($"§20B3: expected deferral, got {retire}");

        var exitWorker = Task.Run(() => reflection.GuardExitRead(guard));
        if (!target.WaitEntered(5_000))
            throw new InvalidOperationException("§20B3: last-reader release never entered the injected target");
        // 并发第二个消费者：必须被拒绝为“已有责任方在释放中”，不得再调用底层释放
        var concurrent = reflection.GuardRetire(guard, 0, out var concurrentDetail);
        var callsDuring = target.CallsSeen;
        target.Unblock();
        if (!exitWorker.Wait(10_000))
            throw new InvalidOperationException("§20B3: last-reader release did not return");
        WaitUntil(() => reflection.GuardIsSettled(guard), 5_000);
        if (callsDuring != 1 || target.CallsSeen != 1)
            throw new InvalidOperationException(
                $"§20B3: duplicate release happened: callsDuring={callsDuring} total={target.CallsSeen}");
        if (concurrent != "DeferredToInFlightRead")
            throw new InvalidOperationException($"§20B3: concurrent consumer result={concurrent} ({concurrentDetail})");
        checks.Add(
            $"candidate20-b[concurrent-release-ownership] concurrentConsumerResult={concurrent} " +
            $"underlyingReleaseCalls=1 totalCalls={target.CallsSeen} settled=true");
    }

    // ==================================================================
    // C：作用域生命周期（编号唯一 / 结束状态 / 迟到收据 / 嵌套 / 正常写对照）
    // ==================================================================

    private static void Run20_ScopeLifecycle(
        RealHcBackend backend,
        CandidateReflection reflection,
        CandidateFakes fakes,
        List<string> checks)
    {
        // ① 并发唯一编号
        var ids = new System.Collections.Concurrent.ConcurrentBag<string>();
        var threads = new List<Thread>();
        for (var t = 0; t < 4; t++)
        {
            var worker = new Thread(() =>
            {
                for (var i = 0; i < 25; i++)
                {
                    var scope = reflection.BeginAcpiScope($"concurrent-{i}");
                    var id = reflection.AcpiScopeId(scope);
                    if (!string.IsNullOrEmpty(id)) ids.Add(id!);
                    reflection.DisposeAcpiScope(scope);
                }
            });
            worker.IsBackground = true;
            threads.Add(worker);
            worker.Start();
        }
        foreach (var worker in threads)
            if (!worker.Join(15_000))
                throw new InvalidOperationException("§20C1: concurrent scope worker did not finish");
        var unique = ids.Distinct().Count();
        if (ids.Count != 100 || unique != 100)
            throw new InvalidOperationException(
                $"§20C1: scope ids are not unique under concurrency: total={ids.Count} unique={unique}");

        // ② 作用域关闭后迟到发布：子工作在作用域内捕获执行上下文，却在 Dispose 之后才发布
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var lateScope = reflection.BeginAcpiScope("late-publish");
        reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        var childStarted = new ManualResetEventSlim(false);
        var childRelease = new ManualResetEventSlim(false);
        var child = Task.Run(() =>
        {
            childStarted.Set();
            childRelease.Wait(10_000);
            // §20.2-C：AsyncLocal 随 ExecutionContext 流入 Task.Run；此发布发生在作用域 Dispose 之后
            reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve(10, 100));
        });
        if (!childStarted.Wait(5_000))
            throw new InvalidOperationException("§20C2: child work did not start");
        reflection.DisposeAcpiScope(lateScope);
        var closed = reflection.AcpiScopeClosed(lateScope);
        var countBeforeLate = reflection.AcpiScopeCount(lateScope);
        childRelease.Set();
        if (!child.Wait(10_000))
            throw new InvalidOperationException("§20C2: child work did not finish after the scope was closed");
        var lateCount = reflection.AcpiScopeLateCount(lateScope);
        var countAfterLate = reflection.AcpiScopeCount(lateScope);
        if (!closed)
            throw new InvalidOperationException("§20C2: scope disposed flag was never set");
        if (lateCount < 1)
            throw new InvalidOperationException(
                $"§20C2: post-dispose receipt was silently dropped (late={lateCount} consumed={countAfterLate})");
        if (countAfterLate != countBeforeLate || countAfterLate != 1)
            throw new InvalidOperationException(
                $"§20C2: consumed receipts were polluted by late publishes: before={countBeforeLate} after={countAfterLate}");

        // ③ 嵌套作用域：内层结束后，本线程发布回到外层
        var outer = reflection.BeginAcpiScope("outer");
        reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        var inner = reflection.BeginAcpiScope("inner");
        reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve(10, 100));
        reflection.DisposeAcpiScope(inner);
        reflection.SetFanCurveObserved(AsusFanTarget.Mid, CandidateReflection.Curve(20, 100));
        var outerCount = reflection.AcpiScopeCount(outer);
        var innerCount = reflection.AcpiScopeCount(inner);
        reflection.DisposeAcpiScope(outer);
        if (innerCount != 1 || outerCount != 2)
            throw new InvalidOperationException($"§20C3: nested scope attribution wrong: inner={innerCount} outer={outerCount}");

        // ④ 正常同步写对照：逐扇分类保持（不因作用域改造而改变）
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var normal = reflection.BeginAcpiScope("normal-write");
        for (var i = 0; i < 3; i++)
            reflection.SetFanCurveObserved((AsusFanTarget)i, CandidateReflection.Curve((byte)(i * 10), 100));
        var verdict = backend.JudgeScope19(normal);
        reflection.DisposeAcpiScope(normal);
        if (verdict.Kind != "transported" || verdict.ReceiptCount != 3 || verdict.ExplicitFailure)
            throw new InvalidOperationException($"§20C4: normal sync write classification changed: {verdict}");
        checks.Add(
            $"candidate20-c[scope-lifecycle] uniqueIds={unique}/100 lateReceipts={lateCount} consumedUnaffected={countAfterLate == 1} " +
            $"nested=inner1/outer2 normalWrite={verdict.Kind}({verdict.ReceiptCount}) " +
            $"observationsApi={reflection.GetIntStatic("HandheldCompanion.Devices.ASUS.AsusAcpiObservations", "ApiVersion")} " +
            $"childContextFlowed={closed}");
    }
}