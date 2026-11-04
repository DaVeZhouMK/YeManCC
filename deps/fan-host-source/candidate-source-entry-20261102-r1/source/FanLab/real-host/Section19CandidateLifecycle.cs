using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text.Json;

namespace YeManFanHost;

/**
 * 920-v1.7 §19：真实候选的有限返修验收（无硬件，OS/HID/ACPI 边界注入）。
 *
 * 原则：
 * - 管理逻辑来自候选 HC 的真实实现（ROGAlly + RogAllyDeviceLifecycle + AsusACPI 作用域）；
 * - 只在 OS/HID/ACPI 边界注入行为（DispatchProxy 边界替身），不复制状态机；
 * - 交错点用**可控停顿**（事件/闸门）而不是睡固定时间；
 * - 必需不变量失败直接抛出 ⇒ 进程非零退出；不接受带 EXPECTATION-UNMET 的 exit=0。
 *
 * A：探测归属（跨会话发布拒绝 + 弃件按实例归属释放 + 清理收据分项）
 * B：真实 I/O 四交错点（读未发起 / 读已排队未执行 / 读已返回未派发 / 设备调用已进入不返回）
 *    + 同 DevicePath 重连按句柄实例区分 + 旧循环待处理记录不被新循环覆盖
 * C：回执按操作作用域（前败→今成 / 前成→今败 / 缺记录 / 并发消费者 / 部分扇失败；
 *    IoException=未知，不得写成“写入一定未发生”；Enable/Restore 明确失败不报成功）
 * D：自动恢复（同一生产 engine：用户曲线 → 睡眠唤醒 → 只推动真实事件/时钟/协调器；
 *    OEM 交还重试单列）
 */
internal static class Section19CandidateLifecycle
{
    /** §19 组最少检查条数（低于此值视为证据不完整，验收门失败）。 */
    internal const int MinimumCheckCount = 32;

    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection19CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    /** §19 组进度跟踪（写 stderr；挂起时能定位到具体小节，不影响 stdout 结果 JSON）。 */
    private static void Trace19(string section) =>
        Console.Error.WriteLine($"[s19-progress] {section} {DateTime.Now:HH:mm:ss.fff}");

    /** 交错点闸门：Armed(point) 后在候选生命周期报告的该点位暂停，直到 Release()。 */
    private sealed class InterleaveGate
    {
        private readonly ManualResetEventSlim entered = new(false);
        private readonly ManualResetEventSlim release = new(true);
        private string? armedPoint;
        private int hits;

        public string? Armed => armedPoint;
        public int Hits => Volatile.Read(ref hits);

        public void Arm(string point)
        {
            entered.Reset();
            release.Reset();
            Volatile.Write(ref hits, 0);
            armedPoint = point;
        }

        public void OnPoint(string point)
        {
            if (armedPoint is null || !string.Equals(armedPoint, point, StringComparison.Ordinal))
                return;
            Interlocked.Increment(ref hits);
            entered.Set();
            release.Wait(30_000);
        }

        public bool WaitEntered(int timeoutMs) => entered.Wait(timeoutMs);
        public void Release() => release.Set();
        public void Disarm()
        {
            armedPoint = null;
            release.Set();
        }
    }

    internal static IReadOnlyList<string> RunSection19CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;

        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("§19: candidate reflection bridge missing");
        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var gate = new InterleaveGate();
        reflection.SetInterleaveHook(gate.OnPoint);
        try
        {
            // 候选设备在 STA 派发线程构造（与生产一致）；本组使用独立实例，不复用 §18 的设备。
            var device = backend.OnStaBounded(reflection.CreateRogAlly, "s19-create-candidate-device");
            fakes.Boundary.HcDevice = device;
            reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
            checks.Add(
                $"candidate-identity-19 sha256={candidateHash} mvid={reflection.ModuleVersionId} " +
                $"observationsApi={reflection.GetIntStatic("HandheldCompanion.Devices.ASUS.AsusAcpiObservations", "ApiVersion")} " +
                $"resourceBoundary={reflection.BoundaryResourceApi}");

            Trace19("section-A");
            // ---- A ----
            Run19_ProbePublishCrossSession_OperationPath(fakes, reflection, backend, device, checks);
            Run19_ProbePublishCrossSession_PublicIsReady(fakes, reflection, backend, device, checks);
            Run19_PreOpenIsReadyOrderingConflict(fakes, reflection, backend, device, checks);
            Run19_CleanupReceiptHonesty(fakes, reflection, backend, device, checks);
            Trace19("section-B");
            // ---- B ----
            Run19_ReadAdmittedBeforeIssue(fakes, reflection, backend, device, gate, checks);
            Run19_ReadQueuedBeforeExecution(fakes, reflection, backend, device, checks);
            Run19_ReadReturnedBeforeDispatch(fakes, reflection, backend, device, gate, checks);
            Run19_DeviceCallInFlight(fakes, reflection, backend, device, checks);
            Run19_CleanupDeviceCallBlocked(fakes, reflection, backend, device, checks);
            Run19_ReconnectHandleInstance(fakes, reflection, backend, device, checks);
            Run19_OldLoopPendingNotOverwritten(fakes, reflection, backend, device, checks);
            Trace19("section-C");
            // ---- C ----
            Run19_ScopeOwnershipMatrix(backend, reflection, fakes, checks);
            Run19_ProductionWriteScopes(engine, candidatePath, candidateHash, fakes, reflection, checks);
            Trace19("section-D");
            // ---- D ----
            Run19_AutomaticRecovery(engine, candidatePath, candidateHash, fakes, reflection, checks);
            return checks;
        }
        finally
        {
            reflection.SetInterleaveHook(null);
            gate.Disarm();
        }
    }

    // ==================================================================
    // A
    // ==================================================================

    /** A1：旧操作的探测跨越新会话 —— 不得覆盖新会话绑定，弃件按实例归属释放。 */
    private static void Run19_ProbePublishCrossSession_OperationPath(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.ArmProbePauseOnce();   // 只暂停“旧操作”的那一次枚举
        var worker = Task.Run(() => reflection.StartDeviceOpen(device, true));
        if (!worker.Wait(5_000))
            throw new InvalidOperationException("§19A1: old operation did not register");
        var oldOperationId = worker.Result;
        if (!fakes.Boundary.WaitProbeEntered(5_000))
            throw new InvalidOperationException("§19A1: old probe did not enter its pause point");

        // Close → 新会话 → **新会话先绑定**（旧探测仍暂停）。
        reflection.CloseDeviceLifecycle(device);
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true)
                throw new InvalidOperationException("§19A1: new-session AsusACPI.Open returned false");
            if (reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("§19A1: new-session ROGAlly.Open returned false");
            return true;
        }, "s19-a1-new-session-open");
        var releasesBefore = fakes.Boundary.DevicesReleased;
        var newOperationId = reflection.StartDeviceOpen(device, true);
        var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 10_000)
            ?? throw new InvalidOperationException("§19A1: new-session operation did not complete");
        if (reflection.GetString(newReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"§19A1: new-session outcome={reflection.Describe(newReceipt)}");
        var newBinding = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);

        // 释放旧探测：它现在才返回，发布必须被拒绝（同临界区校验会话代次/操作归属）。
        fakes.Boundary.ReleaseBlockedProbe();
        if (!worker.Wait(10_000))
            throw new InvalidOperationException("§19A1: old operation worker did not return");
        var oldReceipt = reflection.WaitOpenReceipt(device, oldOperationId, 10_000)
            ?? throw new InvalidOperationException("§19A1: old operation did not reach a terminal receipt");
        var oldOutcome = reflection.GetString(oldReceipt, "Outcome")!;
        if (oldOutcome != "AbortedBySession")
            throw new InvalidOperationException($"§19A1: old operation outcome={oldOutcome} {reflection.Describe(oldReceipt)}");
        if (reflection.GetBool(oldReceipt, "DeviceBoundByThisOperation") ||
            !string.Equals(reflection.GetString(oldReceipt, "SideEffects"), "None", StringComparison.Ordinal))
            throw new InvalidOperationException($"§19A1: old receipt borrowed side effects: {reflection.Describe(oldReceipt)}");
        var bindingAfter = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);
        if (!string.Equals(newBinding, bindingAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§19A1: old probe overwrote the new session binding: new={newBinding} after={bindingAfter}");
        if (fakes.Boundary.DevicesReleased <= releasesBefore)
            throw new InvalidOperationException(
                "§19A1: discarded probe devices were not released by instance ownership");
        checks.Add(
            $"candidate19-a[old-probe-crosses-session] old=AbortedBySession oldEffects=None " +
            $"newBinding={newBinding} afterOldProbe={bindingAfter} discardedReleased=" +
            $"{fakes.Boundary.DevicesReleased - releasesBefore} probeCalls={fakes.Boundary.ProbeCalls}");
    }

    /** A2：公开 IsReady 路径同样跨代 —— 不得发布旧结果、不得覆盖新绑定。 */
    private static void Run19_ProbePublishCrossSession_PublicIsReady(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.ArmProbePauseOnce();
        var worker = Task.Run(() => reflection.CallIsReady(device));
        if (!fakes.Boundary.WaitProbeEntered(5_000))
            throw new InvalidOperationException("§19A2: public IsReady probe did not enter its pause point");

        reflection.CloseDeviceLifecycle(device);
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true ||
                reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("§19A2: new-session open failed");
            return true;
        }, "s19-a2-new-session-open");
        var releasesBefore = fakes.Boundary.DevicesReleased;
        var newOperationId = reflection.StartDeviceOpen(device, true);
        var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 10_000)
            ?? throw new InvalidOperationException("§19A2: new-session operation did not complete");
        var newBinding = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);

        fakes.Boundary.ReleaseBlockedProbe();
        if (!worker.Wait(10_000))
            throw new InvalidOperationException("§19A2: public IsReady worker did not return");
        var readyResult = worker.Result;
        var bindingAfter = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);
        if (!string.Equals(newBinding, bindingAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§19A2: public IsReady overwrote the new session binding: new={newBinding} after={bindingAfter}");
        if (fakes.Boundary.DevicesReleased <= releasesBefore)
            throw new InvalidOperationException("§19A2: discarded IsReady probe devices were not released");
        checks.Add(
            $"candidate19-a[public-isready-crosses-session] isReadyReturned={readyResult} " +
            $"newBinding={newBinding} afterProbe={bindingAfter} " +
            $"discardedReleased={fakes.Boundary.DevicesReleased - releasesBefore} " +
            $"newOutcome={reflection.GetString(newReceipt, "Outcome")}");
    }

    /**
     * A2′（2026-09-25 裁决卡 §6-C）：**关闭后重开的前置条件顺序冲突**（用**真实 HC 生命周期 + fake I/O**）。
     *
     * 复现的旧路径：`Close ⇒ 会话失效 ⇒ 前置 IsReady 恒 false ⇒ 10 s 预算必然跑满 ⇒ Open 才发布新会话`。
     * 判据（三条，均为**可判定**而非概率）：
     *   ① Close 后公开 `IsReady` **恒 false**，且**物理探测确实执行过**（`ProbeCalls` 增长）、
     *      绑定发布被拒（`RogAllyReadyBindingRejected` 入日志）⇒ 证明"物理可用性已满足、被会话准入否掉"；
     *   ② 该失效态下走**正式打开入口**得到 `AbortedBySession` / `session-invalid-at-start`
     *      且 `SideEffects=None` ⇒ 即裁决卡 §6-C 的"安全探路"在**现有公开接口内可达**；
     *   ③ 走正式 `Open()` 发布新会话后，**同一** `IsReady` 立即为真 ⇒
     *      整预算等待的成因被消除（不是把 10000 改小）。
     */
    private static void Run19_PreOpenIsReadyOrderingConflict(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);

        // ① Close ⇒ 会话失效（与睡眠/退出边界同形）。
        reflection.CloseDeviceLifecycle(device);
        var probeCallsBefore = fakes.Boundary.ProbeCalls;
        var logMarkBefore = fakes.Boundary.Logs.Count;
        var preOpenReady = reflection.CallIsReady(device);
        var probeCallsAfter = fakes.Boundary.ProbeCalls;
        var newLogs = fakes.Boundary.Logs.Skip(logMarkBefore).ToList();

        if (preOpenReady)
            throw new InvalidOperationException(
                "§19A2': public IsReady returned true on an invalidated session — ordering conflict not reproduced");
        if (probeCallsAfter <= probeCallsBefore)
            throw new InvalidOperationException(
                "§19A2': physical probe did not run during the rejected pre-open IsReady");
        var rejected = newLogs.Any(l => l.Contains("RogAllyReadyBindingRejected", StringComparison.Ordinal));
        if (!rejected)
            throw new InvalidOperationException(
                $"§19A2': expected RogAllyReadyBindingRejected, got [{string.Join("|", newLogs.TakeLast(4))}]");

        // ② 失效期"安全探路"：正式打开入口必须**无副作用**地拒绝。
        var probeOperationId = reflection.StartDeviceOpen(device, true);
        var probeReceipt = reflection.WaitOpenReceipt(device, probeOperationId, 10_000)
            ?? throw new InvalidOperationException("§19A2': invalidated-session open probe did not reach a receipt");
        var probeOutcome = reflection.GetString(probeReceipt, "Outcome");
        var probePhase = reflection.GetString(probeReceipt, "Phase");
        if (probeOutcome != "AbortedBySession")
            throw new InvalidOperationException(
                $"§19A2': invalidated-session open probe outcome={probeOutcome} {reflection.Describe(probeReceipt)}");
        if (reflection.GetBool(probeReceipt, "DeviceBoundByThisOperation") ||
            !string.Equals(reflection.GetString(probeReceipt, "SideEffects"), "None", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§19A2': invalidated-session open probe borrowed side effects: {reflection.Describe(probeReceipt)}");

        // ③ 正式 Open 发布新会话 ⇒ 同一 IsReady 立即为真（无整预算等待）。
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true ||
                reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("§19A2': new-session open failed");
            return true;
        }, "s19-a2p-new-session-open");
        var afterOpenReady = reflection.CallIsReady(device);
        if (!afterOpenReady)
            throw new InvalidOperationException(
                "§19A2': public IsReady still false after the session was published by Open()");

        checks.Add(
            $"candidate19-a2p[pre-open-isready-ordering-conflict] preOpenIsReady={preOpenReady} " +
            $"probeCalls={probeCallsAfter - probeCallsBefore} rejectedLogged={rejected} " +
            $"invalidatedOpenProbe={probeOutcome}/{probePhase} afterOpenIsReady={afterOpenReady}");
    }

    /** A3：清理收据分项诚实 —— 正例（已释放/表清空）与反例（失败不得报全清）。 */
    private static void Run19_CleanupReceiptHonesty(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        // 正例。
        OpenFreshSession(fakes, reflection, backend, device);
        var positive = reflection.CloseDeviceLifecycle(device);
        var positiveReport = reflection.GetMemberValue(positive, "CleanupReport");
        var positiveCleared = reflection.GetBool(positive, "BindingsCleared");
        var positiveComplete = reflection.GetBool(positive, "CleanupCompleted");
        if (!positiveCleared || !positiveComplete)
            throw new InvalidOperationException(
                $"§19A3: normal close cleanup not settled: {reflection.Describe(positive)}");
        var positiveDescribe = positiveReport is null ? "<none>" : reflection.Describe(positiveReport);
        if (!positiveDescribe.Contains("released=") || !positiveDescribe.Contains("tableClearVerified=True"))
            throw new InvalidOperationException($"§19A3: cleanup report missing per-device facts: {positiveDescribe}");

        // 反例：注入句柄释放失败 ⇒ 不得报“已清理/表已清空”。
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.FailDetachedReleases = 1;
        var negative = reflection.CloseDeviceLifecycle(device);
        var negativeCleared = reflection.GetBool(negative, "BindingsCleared");
        var negativeComplete = reflection.GetBool(negative, "CleanupCompleted");
        var negativeReport = reflection.GetMemberValue(negative, "CleanupReport");
        var negativeDescribe = negativeReport is null ? "<none>" : reflection.Describe(negativeReport);
        if (negativeCleared || negativeComplete)
            throw new InvalidOperationException(
                $"§19A3: release failure was reported as a clean close: {reflection.Describe(negative)}");
        if (!negativeDescribe.Contains("failures=") || negativeDescribe.Contains("failures=[]"))
            throw new InvalidOperationException($"§19A3: failure not recorded in cleanup report: {negativeDescribe}");
        var settledWithin = reflection.WaitCloseCleanupCompleted(device, 300, out var pendingDetail);
        if (settledWithin)
            throw new InvalidOperationException(
                $"§19A3: failed cleanup was reported as settled within the bounded wait: {pendingDetail}");
        checks.Add(
            $"candidate19-a[cleanup-receipt-honesty] positiveCleared=true positiveComplete=true " +
            $"negativeCleared=false negativeComplete=false negativePending=[{Truncate(pendingDetail, 160)}]");
    }

    // ==================================================================
    // B
    // ==================================================================

    /** B1（交错点①）：读取已准入、尚未发起真实读时 Close。 */
    private static void Run19_ReadAdmittedBeforeIssue(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        InterleaveGate gate,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        gate.Arm("read-admitted-before-issue");
        var operationId = reflection.StartDeviceOpen(device, true);
        var receipt = reflection.WaitOpenReceipt(device, operationId, 10_000)
            ?? throw new InvalidOperationException("§19B1: open operation did not complete");
        if (reflection.GetString(receipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"§19B1: open outcome={reflection.Describe(receipt)}");
        if (!gate.WaitEntered(5_000))
            throw new InvalidOperationException("§19B1: read loop did not reach the pre-issue point");
        // Close 落在“已准入、尚未发起真实读”的时刻（保持暂停，此时不释放）。
        var readsBeforeClose = fakes.Boundary.ReadCalls;
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        if (!reflection.GetBool(closeReceipt, "ReadLoopPendingIsolated"))
            throw new InvalidOperationException(
                $"§19B1: in-flight read was not reported as isolated pending: {reflection.Describe(closeReceipt)}");
        var pendingLoops = reflection.GetClosePendingLoops(closeReceipt);
        if (!pendingLoops.Any(loop => loop.Contains("readInFlight=True", StringComparison.Ordinal)))
            throw new InvalidOperationException(
                $"§19B1: pending read loop not described with its in-flight read: [{string.Join("|", pendingLoops)}]");

        // 现在才真正发起这次已准入的读：退役规则必须拦住它（不自动重开、不派发）。
        gate.Release();
        if (!WaitUntil(() => fakes.Boundary.ReadsBlockedByRetire >= 1, 3_000))
            throw new InvalidOperationException(
                "§19B1: the admitted read was never issued to the boundary after close");
        var loopEnded = reflection.WaitReadLoopEnded(device, 5_000, out var loopDetail);
        if (!loopEnded)
            throw new InvalidOperationException($"§19B1: read loop did not end: {loopDetail}");
        if (fakes.Boundary.KeyDispatches != 0)
            throw new InvalidOperationException("§19B1: a key was dispatched after close");
        if (fakes.Boundary.ReopenAttempts != 0)
            throw new InvalidOperationException(
                $"§19B1: the library auto-reopened the handle after close (reopenAttempts={fakes.Boundary.ReopenAttempts})");
        checks.Add(
            $"candidate19-b[read-admitted-before-issue] closePendingIsolated=true pendingReadLoops={pendingLoops.Length} " +
            $"readsBeforeClose={readsBeforeClose} blockedByRetire={fakes.Boundary.ReadsBlockedByRetire} " +
            $"reopenAttempts=0 loopEnded=true");
    }

    /** B2（交错点②）：读已被库排队、尚未真正执行时 Close ⇒ 不得自动重开（含负例证明规则承重）。 */
    private static void Run19_ReadQueuedBeforeExecution(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§19B2: read loop did not enter its first read");
        fakes.Boundary.ResetCountersKeepSession();
        var pausesBefore = fakes.Boundary.ReadDispatchPauses;
        fakes.Boundary.BlockReadDispatch = true;   // 后续读“排队后”停住
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);   // 让循环发出下一次读（排队中）
        if (!WaitUntil(() => fakes.Boundary.ReadDispatchPauses > pausesBefore, 5_000))
            throw new InvalidOperationException(
                $"§19B2: queued read did not reach its dispatch pause (pauses={fakes.Boundary.ReadDispatchPauses} " +
                $"loop={reflection.GetString(device, "ReadLoopState")} logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(6))}])");

        var hitsAtClose = fakes.Boundary.HidOpens;
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        if (!reflection.GetBool(closeReceipt, "CleanupCompleted"))
            throw new InvalidOperationException(
                $"§19B2: close cleanup must settle when no real read is executing: {reflection.Describe(closeReceipt)}");

        fakes.Boundary.BlockReadDispatch = false;   // “排队中的读”现在才真正执行
        var loopEnded = reflection.WaitReadLoopEnded(device, 5_000, out var loopDetail);
        if (!loopEnded)
            throw new InvalidOperationException($"§19B2: read loop did not end: {loopDetail}");
        if (fakes.Boundary.KeyDispatches != 0)
            throw new InvalidOperationException("§19B2: a key was dispatched after close");
        if (fakes.Boundary.ReopenAttempts != 0 || fakes.Boundary.HidOpens != hitsAtClose)
            throw new InvalidOperationException(
                $"§19B2: handle was reopened after close (reopenAttempts={fakes.Boundary.ReopenAttempts} " +
                $"hidOpens={fakes.Boundary.HidOpens} atClose={hitsAtClose})");
        if (fakes.Boundary.ReadsBlockedByRetire == 0)
            throw new InvalidOperationException("§19B2: retirement did not block entering the library read");

        // 负例（证明该规则是“不自动重开”的承重条件）：关掉退役规则，同一交错会重开句柄。
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§19B2-negative: read loop did not enter its first read");
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.HonorRetireRule = false;
        var negativePausesBefore = fakes.Boundary.ReadDispatchPauses;
        fakes.Boundary.BlockReadDispatch = true;
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        if (!WaitUntil(() => fakes.Boundary.ReadDispatchPauses > negativePausesBefore, 5_000))
            throw new InvalidOperationException("§19B2-negative: queued read did not reach its dispatch pause");
        reflection.CloseDeviceLifecycle(device);
        fakes.Boundary.BlockReadDispatch = false;
        if (!WaitUntil(() => fakes.Boundary.ReopenAttempts >= 1, 5_000))
            throw new InvalidOperationException(
                "§19B2-negative: the library-reopen emulation did not fire, the rule is not load-bearing as modeled " +
                $"(reopenAttempts={fakes.Boundary.ReopenAttempts} blockedByRetire={fakes.Boundary.ReadsBlockedByRetire} " +
                $"readCalls={fakes.Boundary.ReadCalls} hidOpens={fakes.Boundary.HidOpens} " +
                $"deviceOpen={fakes.Boundary.CurrentDeviceState.Open} deviceRetired={fakes.Boundary.CurrentDeviceState.Retired} " +
                $"loop={reflection.GetString(device, "ReadLoopState")} logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(8))}])");
        var negativeReopens = fakes.Boundary.ReopenAttempts;
        fakes.Boundary.HonorRetireRule = true;
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        reflection.WaitReadLoopEnded(device, 3_000, out _);
        checks.Add(
            $"candidate19-b[read-queued-before-execution] reopenAttempts=0 blockedByRetire={fakes.Boundary.ReadsBlockedByRetire} " +
            $"negativeReopenAttempts={negativeReopens} (rule is load-bearing)");
    }

    /** B3（交错点③）：读已返回、尚未派发时 Close ⇒ 不得派发旧事件。 */
    private static void Run19_ReadReturnedBeforeDispatch(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        InterleaveGate gate,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§19B3: read loop did not enter its read");
        gate.Arm("read-returned-before-dispatch");
        fakes.Boundary.PushReport(147);   // 读取返回带载荷报告 → 在派发前暂停
        if (!gate.WaitEntered(5_000))
            throw new InvalidOperationException(
                $"§19B3: read did not reach the post-return pause point (hits={gate.Hits} " +
                $"readCalls={fakes.Boundary.ReadCalls} keys={fakes.Boundary.KeyDispatches} " +
                $"loop={reflection.GetString(device, "ReadLoopState")} logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(6))}])");

        var keysBefore = fakes.Boundary.KeyDispatches;
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        if (!reflection.GetBool(closeReceipt, "CleanupCompleted"))
            throw new InvalidOperationException(
                $"§19B3: close cleanup must settle: {reflection.Describe(closeReceipt)}");
        // 释放暂停：报告已返回、现在才轮到派发 —— 会话已失效，必须丢弃。
        gate.Release();
        var loopEnded = reflection.WaitReadLoopEnded(device, 5_000, out var loopDetail);
        if (!loopEnded)
            throw new InvalidOperationException($"§19B3: read loop did not end: {loopDetail}");
        if (fakes.Boundary.KeyDispatches != keysBefore)
            throw new InvalidOperationException(
                $"§19B3: a report returned before close was dispatched afterwards (keys {keysBefore}->{fakes.Boundary.KeyDispatches})");
        checks.Add(
            $"candidate19-b[read-returned-before-dispatch] lateReportDropped=true keys={fakes.Boundary.KeyDispatches} " +
            $"loopEnded=true detail={Truncate(loopDetail, 80)}");
    }

    /** B4（交错点④）：Open/Release 已进入且不返回 —— Close 不得持锁无限阻塞、不得报清理完成。 */
    private static void Run19_DeviceCallInFlight(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        var callPausesBefore = fakes.Boundary.DeviceCallPauses;
        fakes.Boundary.BlockDeviceCall = true;   // 下一次设备调用（Open）进入且不返回
        var operationId = reflection.StartDeviceOpen(device, true);
        if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > callPausesBefore, 5_000))
            throw new InvalidOperationException(
                $"§19B4: device call did not enter its blocked section (pauses={fakes.Boundary.DeviceCallPauses} " +
                $"logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(5))}])");
        if (!reflection.GetSessionValid(device))
            throw new InvalidOperationException("§19B4: session invalidated before close");

        // Close 必须能进展（不持有生命周期锁等待底层 I/O），并如实报告设备调用待处理。
        var started = Stopwatch.GetTimestamp();
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        var closeMs = (Stopwatch.GetTimestamp() - started) * 1000d / Stopwatch.Frequency;
        if (closeMs > 2_000)
            throw new InvalidOperationException($"§19B4: close blocked on the in-flight device call for {closeMs:F0}ms");
        var pendingCalls = reflection.GetClosePendingCalls(closeReceipt);
        if (pendingCalls.Length == 0)
            throw new InvalidOperationException(
                $"§19B4: in-flight device call not reported: {reflection.Describe(closeReceipt)}");
        if (reflection.GetBool(closeReceipt, "CleanupCompleted"))
            throw new InvalidOperationException("§19B4: cleanup reported complete while a device call was in flight");
        // 状态查询仍可进展。
        var stateWhileBlocked = reflection.GetString(device, "ReadLoopState") ?? "<none>";
        if (reflection.WaitCloseCleanupCompleted(device, 200, out var pendingDetail))
            throw new InvalidOperationException("§19B4: cleanup settled while the device call was still blocked");

        // 释放被阻塞的调用：其收尾必须承担 Cleanup，并把已发生的副作用标为“迟到副作用”。
        fakes.Boundary.BlockDeviceCall = false;
        var receipt = reflection.WaitOpenReceipt(device, operationId, 10_000)
            ?? throw new InvalidOperationException("§19B4: operation did not reach a terminal receipt");
        var lateEffects = reflection.GetString(receipt, "LateSideEffects") ?? "None";
        if (!lateEffects.Contains("HidOpened", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§19B4: late side effect not recorded: {reflection.Describe(receipt)}");
        var settled = reflection.WaitCloseCleanupCompleted(device, 5_000, out var settledDetail);
        if (!settled)
            throw new InvalidOperationException($"§19B4: deferred cleanup never settled: {settledDetail}");
        checks.Add(
            $"candidate19-b[device-call-in-flight] closeReturnedMs={closeMs:F0} pendingCalls={pendingCalls.Length} " +
            $"cleanupCompletedAtClose=false stateQuery={stateWhileBlocked} lateEffects={lateEffects} " +
            $"deferredCleanupSettled=true op={reflection.GetString(receipt, "Outcome")}");
    }

    /**
     * B7（交错点④的清理侧）：Close 自己的 remap/Release 调用**已进入且不返回**时，
     * Close 方停在 I/O 里，但**不持有生命周期锁**——状态查询与清理结算查询仍可进展，
     * 且不得报告清理完成；释放停顿后清理必须结算（remap 交还 + 逐设备释放）。
     */
    private static void Run19_CleanupDeviceCallBlocked(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§19B7: read loop did not enter its read");
        var pausesBefore = fakes.Boundary.DeviceCallPauses;
        var releasesBefore = fakes.Boundary.DevicesReleased;
        fakes.Boundary.BlockDeviceCall = true;   // Close 清理中的 remap/释放调用进入且不返回
        object? receipt = null;
        Exception? closeError = null;
        var worker = Task.Run(() =>
        {
            try { receipt = reflection.CloseDeviceLifecycle(device); }
            catch (Exception ex) { closeError = ex; }
        });
        if (!WaitUntil(() => fakes.Boundary.DeviceCallPauses > pausesBefore, 5_000))
            throw new InvalidOperationException(
                $"§19B7: cleanup device call did not enter its blocked section (pauses={fakes.Boundary.DeviceCallPauses})");
        if (worker.Wait(200))
            throw new InvalidOperationException("§19B7: close returned although its cleanup call was still stalled");

        // 关键断言：停顿期间生命周期锁是自由的（查询可进展），且清理不得被报告为已结算。
        var stateWhileBlocked = reflection.GetString(device, "ReadLoopState");
        var sessionValidWhileBlocked = reflection.GetSessionValid(device);
        var settledWhileBlocked = reflection.WaitCloseCleanupCompleted(device, 200, out var pendingDetail);
        if (settledWhileBlocked)
            throw new InvalidOperationException(
                $"§19B7: cleanup was reported settled while the cleanup call was stalled: {pendingDetail}");

        fakes.Boundary.BlockDeviceCall = false;
        if (!worker.Wait(10_000))
            throw new InvalidOperationException("§19B7: close did not return after the stalled cleanup call was released");
        if (closeError is not null)
            throw new InvalidOperationException(
                $"§19B7: close threw {closeError.GetType().Name}: {closeError.Message}");
        var closeReceipt = receipt ?? throw new InvalidOperationException("§19B7: close returned no receipt");
        if (!reflection.GetBool(closeReceipt, "RemapReleased") || !reflection.GetBool(closeReceipt, "CleanupCompleted"))
            throw new InvalidOperationException(
                $"§19B7: cleanup not settled after the stall was released: {reflection.Describe(closeReceipt)}");
        var released = fakes.Boundary.DevicesReleased - releasesBefore;
        if (released < 1)
            throw new InvalidOperationException("§19B7: no device was released after the stall was released");
        checks.Add(
            $"candidate19-b[cleanup-call-blocked] closeBlockedOnIo=true stateQueryWhileBlocked={stateWhileBlocked} " +
            $"sessionValidWhileBlocked={sessionValidWhileBlocked} cleanupSettledWhileBlocked=false " +
            $"remapReleased=true devicesReleasedAfterRelease={released} pending=[{Truncate(pendingDetail, 90)}]");
    }

    /** B5：同 DevicePath 重连按句柄实例区分（迟到移除事件不得作用于新实例）。 */
    private static void Run19_ReconnectHandleInstance(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        var staleInstance = fakes.Boundary.BoundDeviceInstance(0x5a)
            ?? throw new InvalidOperationException("§19B5: no bound instance in the first session");
        var staleLabel = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);

        // 新会话 + 新实例（同 DevicePath）。
        OpenFreshSession(fakes, reflection, backend, device);
        var freshLabel = fakes.Boundary.BoundDeviceInstanceLabel(0x5a);
        if (string.Equals(staleLabel, freshLabel, StringComparison.Ordinal))
            throw new InvalidOperationException($"§19B5: reconnect fixture produced the same instance {freshLabel}");

        // 迟到移除事件携带**旧实例**：不得停止新循环、不得释放新绑定。
        reflection.HandleDeviceRemoved(device, staleInstance);
        if (reflection.GetString(device, "ReadLoopState") != "Running")
            throw new InvalidOperationException(
                $"§19B5: stale removal stopped the new session loop: {reflection.GetString(device, "ReadLoopState")}");
        if (!string.Equals(fakes.Boundary.BoundDeviceInstanceLabel(0x5a), freshLabel, StringComparison.Ordinal))
            throw new InvalidOperationException("§19B5: stale removal released the new binding");
        var keysBefore = fakes.Boundary.KeyDispatches;
        fakes.Boundary.PushReport(key: 147);
        if (!fakes.Boundary.WaitForKeyDispatch(3_000))
            throw new InvalidOperationException("§19B5: new session loop did not dispatch after the stale removal");

        // 正例：携带**当前实例**的移除事件必须作用于该实例。
        reflection.HandleDeviceRemoved(device, fakes.Boundary.BoundDeviceInstance(0x5a)!);
        var releasedAfterRealRemoval = fakes.Boundary.BoundDeviceInstance(0x5a) is null;
        if (!releasedAfterRealRemoval)
            throw new InvalidOperationException("§19B5: genuine removal did not release the bound instance");
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        reflection.WaitReadLoopEnded(device, 3_000, out _);
        checks.Add(
            $"candidate19-b[reconnect-handle-instance] staleInstanceIgnored=true staleLabel={staleLabel} " +
            $"freshLabel={freshLabel} keysAfterStale={fakes.Boundary.KeyDispatches - keysBefore} " +
            $"genuineRemovalReleased=true");
    }

    /** B6：旧循环的待处理记录不因新循环启动而消失（多登记）。 */
    private static void Run19_OldLoopPendingNotOverwritten(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§19B6: first loop did not enter its read");
        var firstGeneration = reflection.GetCurrentGeneration(device);

        // 释放责任延迟给在途读取 ⇒ 旧循环的读取保持未决，Close 不得宣称清理完成。
        fakes.Boundary.DeferDetachedReleases = 1;
        var firstClose = reflection.CloseDeviceLifecycle(device);
        if (reflection.GetBool(firstClose, "CleanupCompleted"))
            throw new InvalidOperationException("§19B6: deferred release was reported as settled");
        if (reflection.GetClosePendingReleases(firstClose).Length == 0)
            throw new InvalidOperationException(
                $"§19B6: pending release not reported: {reflection.Describe(firstClose)}");

        // 新会话 + 新循环（**不动旧循环的未决读取**：直接开新会话并启动新操作）。
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true ||
                reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("§19B6: new-session open failed");
            return true;
        }, "s19-b6-new-session");
        var newOperationId = reflection.StartDeviceOpen(device, true);
        var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 10_000)
            ?? throw new InvalidOperationException("§19B6: new-session operation did not complete");
        if (reflection.GetString(newReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"§19B6: new-session outcome={reflection.Describe(newReceipt)}");

        var secondClose = reflection.CloseDeviceLifecycle(device);
        var pendingLoops = reflection.GetClosePendingLoops(secondClose);
        if (pendingLoops.Length < 2)
            throw new InvalidOperationException(
                $"§19B6: old loop pending record was overwritten by the new loop slot: [{string.Join("|", pendingLoops)}]");
        if (!pendingLoops.Any(loop => loop.Contains($"gen{firstGeneration}", StringComparison.Ordinal)) ||
            !pendingLoops.Any(loop => !loop.Contains($"gen{firstGeneration}", StringComparison.Ordinal)))
            throw new InvalidOperationException(
                $"§19B6: pending list lost the old generation {firstGeneration}: [{string.Join("|", pendingLoops)}]");

        // 旧读取返回 ⇒ 延迟释放结算 ⇒ 清理可结算。
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        var settled = reflection.WaitCloseCleanupCompleted(device, 5_000, out var settledDetail);
        if (!settled)
            throw new InvalidOperationException($"§19B6: deferred cleanup never settled: {settledDetail}");
        reflection.WaitReadLoopEnded(device, 3_000, out _);
        checks.Add(
            $"candidate19-b[old-loop-pending-preserved] pendingLoops={pendingLoops.Length} " +
            $"deferredReleaseReported=true deferredCleanupSettled=true");
    }

    // ==================================================================
    // C
    // ==================================================================

    /** C1：写入收据按**操作作用域**归属（前败→今成 / 前成→今败 / 缺记录 / 并发消费者 / 部分扇失败）。 */
    private static void Run19_ScopeOwnershipMatrix(
        RealHcBackend backend,
        CandidateReflection reflection,
        CandidateFakes fakes,
        List<string> checks)
    {
        // ① 前败 → 今成：历史失败不得污染本次成功。
        fakes.Acpi.Reset(CandidateAcpiMode.IoFailureWrites);
        var failedScope = reflection.BeginAcpiScope("matrix-failed-first");
        reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve(0, 100));
        var failedSlow = backend.JudgeScope19(failedScope);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var okScope = reflection.BeginAcpiScope("matrix-ok-second");
        for (var i = 0; i < 3; i++)
            reflection.SetFanCurveObserved((AsusFanTarget)i, CandidateReflection.Curve((byte)(i * 10), 100));
        var okVerdict = backend.JudgeScope19(okScope);
        if (!failedSlow.ExplicitFailure || okVerdict.ExplicitFailure ||
            okVerdict.Kind != "transported" || okVerdict.ReceiptCount != 3)
            throw new InvalidOperationException(
                $"§19C1: previous failure leaked into this operation: failed={failedSlow} current={okVerdict}");

        // ② 前成 → 今败：历史成功不得掩盖本次失败。2026-09-23 架构修正（operator 裁决）：
        //    部分通道失败（至少一个通道已送达）**不再致命**，但必须逐扇点名、且不得报成功。
        fakes.Acpi.Reset(CandidateAcpiMode.IoFailureGpuWrite);
        var failedSecond = reflection.BeginAcpiScope("matrix-failed-second");
        for (var i = 0; i < 3; i++)
            reflection.SetFanCurveObserved((AsusFanTarget)i, CandidateReflection.Curve((byte)(i * 10), 100));
        var partialVerdict = backend.JudgeScope19(failedSecond);
        if (partialVerdict.ExplicitFailure || partialVerdict.Kind != "partial" || partialVerdict.NoneTransported ||
            partialVerdict.Transported || !partialVerdict.Detail.Contains("GPU:InitiatedFailed", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§19C1: partial fan failure must stay recorded per fan but non-fatal: {partialVerdict}");
        // ②' 零通道送达 ⇒ 仍然致命（不得把"全部失败"当部分失败放行）。
        fakes.Acpi.Reset(CandidateAcpiMode.IoFailureWrites);
        var allFailedScope = reflection.BeginAcpiScope("matrix-all-failed");
        for (var i = 0; i < 3; i++)
            reflection.SetFanCurveObserved((AsusFanTarget)i, CandidateReflection.Curve((byte)(i * 10), 100));
        var allFailedVerdict = backend.JudgeScope19(allFailedScope);
        if (!allFailedVerdict.ExplicitFailure || allFailedVerdict.Kind != "explicit-failure" ||
            !allFailedVerdict.NoneTransported)
            throw new InvalidOperationException($"§19C1: zero-transport failure must stay fatal: {allFailedVerdict}");

        // ③ 缺记录：空作用域 ⇒ unknown（不是 verified，也不是失败）。
        var emptyScope = reflection.BeginAcpiScope("matrix-empty");
        var emptyVerdict = backend.JudgeScope19(emptyScope);
        if (emptyVerdict.Kind != "no-receipts" || !emptyVerdict.Unknown || emptyVerdict.ExplicitFailure)
            throw new InvalidOperationException($"§19C1: missing receipts must be unknown: {emptyVerdict}");

        // ④ 并发消费者：两个执行上下文各自只见自己的收据。
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        object? scopeA = null;
        object? scopeB = null;
        var threadA = new Thread(() =>
        {
            scopeA = reflection.BeginAcpiScope("matrix-thread-a");
            for (var i = 0; i < 2; i++)
                reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve((byte)i, 100));
        });
        var threadB = new Thread(() =>
        {
            scopeB = reflection.BeginAcpiScope("matrix-thread-b");
            for (var i = 0; i < 5; i++)
                reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve((byte)i, 100));
        });
        threadA.Start();
        threadB.Start();
        if (!threadA.Join(10_000) || !threadB.Join(10_000))
            throw new InvalidOperationException("§19C1: concurrent scope workers did not finish");
        var verdictA = backend.JudgeScope19(scopeA);
        var verdictB = backend.JudgeScope19(scopeB);
        if (verdictA.ReceiptCount != 2 || verdictB.ReceiptCount != 5)
            throw new InvalidOperationException(
                $"§19C1: concurrent consumers crossed scopes: a={verdictA.ReceiptCount} b={verdictB.ReceiptCount}");
        var legacyVerdict = backend.JudgeScope19(null);
        if (legacyVerdict.Kind != "unsupported")
            throw new InvalidOperationException($"§19C1: legacy runtime must stay unsupported: {legacyVerdict}");
        checks.Add(
            $"candidate19-c[scope-ownership] previousFailureIsolated=true previousSuccessNotReused=true " +
            $"partialFanFailure=non-fatal-but-recorded({Truncate(partialVerdict.Detail, 96)}) " +
            $"zeroTransportFailure=fatal missingReceipts=unknown " +
            $"concurrentScopes=2/5 legacyRuntime={legacyVerdict.Kind}");
    }

    /** C2：生产 Enable/Restore 链上的作用域判定（明确失败不报成功、IoException=未知）。 */
    private static void Run19_ProductionWriteScopes(
        FanHostEngine ownerEngine,
        string candidatePath,
        string candidateHash,
        CandidateFakes fakes,
        CandidateReflection reflection,
        List<string> checks)
    {
        Trace19("c-scenarios");
        _ = ownerEngine;
        // ① 部分扇失败（至少一个通道已送达）：2026-09-23 架构修正后**不再致命**（按 HC 能力库
        //    的传输观察语义），但必须逐扇可见、绝不把失败写成成功。
        var partialFailureCode = "<none>";
        var stateAfterPartialFailure = "<none>";
        var partialRecorded = false;
        {
            var (engine, _, _) = CreateProductionEngineFor19(candidatePath, candidateHash, fakes, reflection, "s19-c1");
            try
            {
                engine.GrantSelfTestLeaseForSelfTest("s19-c1-lease");
                fakes.Acpi.Reset(CandidateAcpiMode.IoFailureGpuWrite);
                HostDiagnosticLog.ResetSelfTestCapture();
                try { _ = engine.Enable(BuildEnableBody("s19-c1-lease")); }
                catch (FanApiException ex) { partialFailureCode = ex.Code; }
                stateAfterPartialFailure = engine.Snapshot().State;
                partialRecorded = HostDiagnosticLog.SelfTestCaptured
                    .Any(e => e.Event == "enable.asus-write-receipts-partial");
                if (partialFailureCode != "<none>")
                    throw new InvalidOperationException(
                        $"§19C2: a partial-channel transport failure must not fail the necessary control write ({partialFailureCode})");
                if (!partialRecorded)
                    throw new InvalidOperationException("§19C2: partial-channel failure was not recorded per fan");
                var partialText = string.Join("|", HostDiagnosticLog.SelfTestCaptured
                    .Where(e => e.Event == "enable.asus-write-receipts-partial").Select(e => e.Details));
                if (!partialText.Contains("GPU:InitiatedFailed", StringComparison.Ordinal))
                    throw new InvalidOperationException(
                        $"§19C2: partial-channel failure lost per-fan attribution: {partialText}");
            }
            finally { SafeEngineClose(engine, fakes); }
        }

        // ①' 零通道送达：仍然致命（Enable 不得报告控制成功）。
        var zeroTransportCode = "<none>";
        {
            var (engine, _, _) = CreateProductionEngineFor19(candidatePath, candidateHash, fakes, reflection, "s19-c1z");
            try
            {
                engine.GrantSelfTestLeaseForSelfTest("s19-c1z-lease");
                fakes.Acpi.Reset(CandidateAcpiMode.IoFailureWrites);
                HostDiagnosticLog.ResetSelfTestCapture();
                try { _ = engine.Enable(BuildEnableBody("s19-c1z-lease")); }
                catch (FanApiException ex) { zeroTransportCode = ex.Code; }
                if (zeroTransportCode != "FAN_CURVE_WRITE_FAILED")
                    throw new InvalidOperationException(
                        $"§19C2: a zero-transport necessary control-write failure was reported as control success ({zeroTransportCode})");
                if (engine.Snapshot().HardwareWritesEnabled && engine.Snapshot().State == "Ready")
                    throw new InvalidOperationException("§19C2: enable reported Ready after a zero-transport write failure");
            }
            finally { SafeEngineClose(engine, fakes); }
        }

        // ② Enable 侧 IoException：unknown（不写成成功断言、也不判失败）。
        var enableUnknownRecorded = false;
        var enableThrowCode = "<none>";
        {
            var (engine, _, _) = CreateProductionEngineFor19(candidatePath, candidateHash, fakes, reflection, "s19-c2");
            try
            {
                engine.GrantSelfTestLeaseForSelfTest("s19-c2-lease");
                fakes.Acpi.Reset(CandidateAcpiMode.TransportException);
                HostDiagnosticLog.ResetSelfTestCapture();
                try { _ = engine.Enable(BuildEnableBody("s19-c2-lease")); }
                catch (FanApiException ex) { enableThrowCode = ex.Code; }
                enableUnknownRecorded = HostDiagnosticLog.SelfTestCaptured
                    .Any(e => e.Event == "enable.asus-write-receipts-unknown");
                if (!enableUnknownRecorded)
                    throw new InvalidOperationException(
                        $"§19C2: enable-side unknown write outcome was not recorded (throwCode={enableThrowCode})");
                if (enableThrowCode == "FAN_CURVE_WRITE_FAILED")
                    throw new InvalidOperationException("§19C2: IoException must not be judged as a confirmed write failure");
            }
            finally { SafeEngineClose(engine, fakes); }
        }

        // ③ Restore 侧 IoException ⇒ unknown：不得描述成“写入一定未发生”，不得据此报成功断言。
        var restoreOutcome = "<none>";
        var backendOutcome = "<none>";
        var evidence = "<none>";
        {
            var (engine, _, _) = CreateProductionEngineFor19(candidatePath, candidateHash, fakes, reflection, "s19-c3");
            try
            {
                fakes.Acpi.Reset(CandidateAcpiMode.TransportException);
                HostDiagnosticLog.ResetSelfTestCapture();
                try
                {
                    _ = engine.Restore(JsonDocument.Parse("{}").RootElement);
                    restoreOutcome = "returned";
                }
                catch (Exception ex) { restoreOutcome = "error:" + ex.GetType().Name; }
                backendOutcome = engine.RealBackendForSelfTest?.AsusReleaseWriteOutcomeForSelfTest ?? "<none>";
                evidence = engine.Snapshot().OemRestoreEvidence;
                var unknownRecorded = HostDiagnosticLog.SelfTestCaptured
                    .Any(e => e.Event == "restore.asus-write-receipts-unknown");
                var claimedTransported = HostDiagnosticLog.SelfTestCaptured
                    .Any(e => e.Event == "restore.asus-write-receipts-transported");
                if (!unknownRecorded || claimedTransported || backendOutcome != "io-exception-unknown")
                    throw new InvalidOperationException(
                        $"§19C2: IoException restore outcome was not classified as unknown " +
                        $"(outcome={backendOutcome} unknownRecorded={unknownRecorded} claimedTransported={claimedTransported})");
                if (!evidence.Contains("unknown", StringComparison.Ordinal))
                    throw new InvalidOperationException($"§19C2: restore evidence claimed more than observed: {evidence}");
                var diagnosticsText = string.Join("|", HostDiagnosticLog.SelfTestCaptured.Select(e => e.Details));
                if (diagnosticsText.Contains("did not happen", StringComparison.Ordinal))
                    throw new InvalidOperationException(
                        "§19C2: an unknown (IoException) restore outcome was described as 'write did not happen'");
            }
            finally { SafeEngineClose(engine, fakes); }
        }

        checks.Add(
            $"candidate19-c[production-write-scopes] partialFanFailure=nonFatal-recorded({partialRecorded}) " +
            $"zeroTransportFailureCode={zeroTransportCode} stateAfterPartialFailure={stateAfterPartialFailure} " +
            $"enableUnknownRecorded={enableUnknownRecorded} " +
            $"restoreIoException=unknown restoreEvidence={evidence} restoreOutcome={restoreOutcome}");
    }

    // ==================================================================
    // D
    // ==================================================================

    /** D：同一生产 engine 的自动恢复（用户曲线 → 睡眠唤醒）+ OEM 交还重试（单列）。 */
    private static void Run19_AutomaticRecovery(
        FanHostEngine ownerEngine,
        string candidatePath,
        string candidateHash,
        CandidateFakes fakes,
        CandidateReflection reflection,
        List<string> checks)
    {
        Trace19("d-scenarios");
        _ = ownerEngine;
        var (engine, backend, device) = CreateProductionEngineFor19(
            candidatePath, candidateHash, fakes, reflection, "s19-d");
        try
        {
            _ = device;
            // 用户曲线（明显区别于 OEM 默认表）。
            Trace19("d1-user-curve");
            var userNodes = new[] { new FanNode(0, 0), new FanNode(40, 0), new FanNode(70, 25), new FanNode(100, 85) };
            engine.GrantSelfTestLeaseForSelfTest("s19-d-lease");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = engine.Enable(BuildEnableBody("s19-d-lease", userNodes));
            var enableWrites = fakes.Acpi.WriteCalls.ToList();
            if (enableWrites.Count != 3)
                throw new InvalidOperationException($"§19D: user-curve enable issued {enableWrites.Count} writes");
            var userCurveBytes = enableWrites[0].InBuffer[12..];
            var defaultCpu = reflection.DefaultCurveFor(AsusFanTarget.Cpu);
            if (userCurveBytes.SequenceEqual(defaultCpu))
                throw new InvalidOperationException("§19D: fixture user curve equals the OEM default table");

            // 真实睡眠事件 → 真实唤醒协调入口；测试不授 lease、不调 Enable/Restore 帮恢复。
            Trace19("d1-suspend");
            HostDiagnosticLog.ResetSelfTestCapture();
            _ = engine.Suspend(JsonDocument.Parse("{}").RootElement);
            if (engine.Snapshot().PowerState != "Suspended")
                throw new InvalidOperationException($"§19D: suspend did not reach Suspended: {engine.Snapshot().PowerState}");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            var writesAtWake = fakes.Acpi.WriteCalls.Count;
            _ = engine.Resume();
            Trace19("d1-resume-wait");
            var wakeWait = engine.WaitForAutomaticResumeAsync(15_000);
            try { wakeWait.GetAwaiter().GetResult(); }
            catch (Exception ex)
            {
                throw new InvalidOperationException($"§19D: automatic resume did not complete: {ex.Message}");
            }
            var snapshot = engine.Snapshot();
            if (snapshot.State != "Ready")
                throw new InvalidOperationException($"§19D: automatic resume ended in {snapshot.State} (expected Ready)");
            Trace19("d1-resume-done");
            // C2（2026-09-25 裁决卡 §6-C）：重开路径的前置 `IsReady()` 轮询按 HC 会话契约
            // **结构上不可能成功**（R5B 回函 §1.3），已改为由正式 `Open()` 表达物理缺失/失败。
            // ⇒ 本段**必须不再跑满 10 s 预算**；旧路径实测 d1-resume-wait→done ≈ 10.3 s。
            // 同时要求证据里带**跳过原因**，不允许把"没轮询"静默当成"已就绪"。
            var readyWait = JsonSerializer.SerializeToElement(backend.HcReadyWaitEvidenceForSelfTest);
            var readySkipped = readyWait.GetProperty("skipped").GetString();
            var readyElapsedMs = readyWait.GetProperty("elapsedMs").GetInt32();
            if (readySkipped != "reopen-session-gated-by-hc-contract" || readyElapsedMs >= 1_000)
                throw new InvalidOperationException(
                    $"§19D: reopen readiness wait still consumed the pre-open budget " +
                    $"(skipped={readySkipped} elapsedMs={readyElapsedMs})");
            var resumeWrites = fakes.Acpi.WriteCalls.Skip(writesAtWake).ToList();
            if (resumeWrites.Count < 3)
                throw new InvalidOperationException(
                    $"§19D: automatic resume issued {resumeWrites.Count} fan writes (expected at least the 3-fan user curve)");
            var expectedIds = new[] { 0x00110024u, 0x00110025u, 0x00110032u };
            var groupKinds = new List<string>();
            for (var offset = 0; offset + 3 <= resumeWrites.Count; offset += 3)
            {
                var matchesUserCurve = true;
                for (var i = 0; i < 3; i++)
                {
                    if (resumeWrites[offset + i].DeviceId != expectedIds[i] ||
                        !resumeWrites[offset + i].InBuffer[12..].SequenceEqual(enableWrites[i].InBuffer[12..]))
                    {
                        matchesUserCurve = false;
                        break;
                    }
                }
                groupKinds.Add(matchesUserCurve
                    ? "user-curve"
                    : resumeWrites[offset].InBuffer[12..].SequenceEqual(defaultCpu)
                        ? "oem-default!"
                        : "other!");
            }
            if (resumeWrites.Count % 3 != 0)
                throw new InvalidOperationException(
                    $"§19D: automatic resume write structure is not per-fan triples: writes={resumeWrites.Count}");
            // HC 自身的打开序列可能先写默认表（其 profile manager 的启动行为），这是**先于**用户曲线写入的；
            // 恢复原意图的可证条件：本次自动恢复内确实写入过用户曲线，且**最后一次**写入就是用户曲线。
            if (!groupKinds.Contains("user-curve"))
                throw new InvalidOperationException(
                    $"§19D: automatic resume never wrote the user curve: groups=[{string.Join(",", groupKinds)}]");
            if (groupKinds[^1] != "user-curve")
                throw new InvalidOperationException(
                    $"§19D: automatic resume did not end on the user curve (final group={groupKinds[^1]}) " +
                    $"groups=[{string.Join(",", groupKinds)}]");
            // 归属精确到本 engine 实例：自动恢复期间不得触发有界 OEM 恢复重试（旧计时器不得介入）。
            var retryAttempts = engine.RecoveryRetryAttemptsForSelfTest;
            var retryTimerActive = engine.RecoveryRetryTimerActiveForSelfTest;
            var closeCleanupSettled = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "hc-close.cleanup-settled");
            var closeCleanupPending = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "hc-close.cleanup-pending");
            if (retryAttempts != 0 || retryTimerActive)
                throw new InvalidOperationException(
                    $"§19D: automatic resume used the OEM-recovery retry path (old timer interference): " +
                    $"attempts={retryAttempts} timerActive={retryTimerActive} state={engine.Snapshot().State}");
            if (!closeCleanupSettled || closeCleanupPending)
                throw new InvalidOperationException(
                    $"§19D: sleep-boundary close cleanup receipt not consumed truthfully " +
                    $"(settled={closeCleanupSettled} pending={closeCleanupPending})");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            engine.GrantSelfTestLeaseForSelfTest("s19-d-lease-2");
            _ = engine.Enable(BuildEnableBody("s19-d-lease-2", new[]
            {
                new FanNode(0, 0), new FanNode(40, 10), new FanNode(70, 40), new FanNode(100, 95)
            }));
            var adjustmentWrites = fakes.Acpi.WriteCalls.Count;
            if (adjustmentWrites < 3 ||
                !fakes.Acpi.WriteCalls.Take(3).Select(w => w.DeviceId).SequenceEqual(new[] { 0x00110024u, 0x00110025u, 0x00110032u }))
                throw new InvalidOperationException(
                    $"§19D: post-recovery user adjustment issued {adjustmentWrites} fan writes " +
                    $"ids=[{string.Join(",", fakes.Acpi.WriteCalls.Take(3).Select(w => $"0x{w.DeviceId:X8}"))}]");
            checks.Add(
                $"candidate19-d[automatic-user-curve-restore] userCurveRestored=true writeGroups=[{string.Join(",", groupKinds)}] " +
                $"finalGroupIsUserCurve=true state={snapshot.State} sleepCloseCleanupSettled=true " +
                $"retryEvents=0 nextUserAdjustmentWrites={adjustmentWrites} " +
                $"readyWaitSkipped={readySkipped} readyWaitElapsedMs={readyElapsedMs} readyWaitBudgetMs=0");

            // ---- OEM 交还重试（与“恢复原用户曲线”分开记录）----
            Trace19("d2-handback-fault");
            fakes.Acpi.Reset(CandidateAcpiMode.IoFailureWrites);
            HostDiagnosticLog.ResetSelfTestCapture();
            var restoreFaultCode = "<none>";
            try { _ = engine.Restore(JsonDocument.Parse("{}").RootElement); }
            catch (FanApiException ex) { restoreFaultCode = ex.Code; }
            if (restoreFaultCode != "OEM_RESTORE_FAILED")
                throw new InvalidOperationException(
                    $"§19D: failing OEM handback was not reported as a failure ({restoreFaultCode})");
            var retryScheduled = engine.RecoveryRetryAttemptsForSelfTest >= 1;
            if (!retryScheduled)
                throw new InvalidOperationException("§19D: OEM handback failure did not schedule the bounded retry window");

            // 解除故障：只让真实时钟/重试窗口推进（不再手工调用 Restore）。
            fakes.Acpi.Reset(CandidateAcpiMode.MatchDefaults);
            Trace19("d2-poll");
            var recovered = false;
            var recoveredWrites = new List<CandidateAcpiTransportFake.ControlCall>();
            var deadline = Stopwatch.GetTimestamp() + 20 * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < deadline)
            {
                if (engine.Snapshot().State is "AwaitingControl" or "Suspended")
                {
                    recovered = true;
                    recoveredWrites = fakes.Acpi.WriteCalls.ToList();
                    break;
                }
                Thread.Sleep(200);
            }
            if (!recovered)
                throw new InvalidOperationException(
                    $"§19D: bounded automatic retry did not settle the OEM handback (state={engine.Snapshot().State})");
            if (recoveredWrites.Count != 3)
                throw new InvalidOperationException(
                    $"§19D: automatic OEM handback retry issued {recoveredWrites.Count} fan writes");
            var defaultGpu = reflection.DefaultCurveFor(AsusFanTarget.Gpu);
            if (!recoveredWrites[0].InBuffer[12..].SequenceEqual(defaultCpu) ||
                !recoveredWrites[1].InBuffer[12..].SequenceEqual(defaultGpu))
                throw new InvalidOperationException(
                    "§19D: automatic OEM handback retry did not write HC's default tables");
            Trace19("d2-done");
            var finalSnapshot = engine.Snapshot();
            if (finalSnapshot.HcCloseCleanupPending)
                throw new InvalidOperationException("§19D: automatic OEM handback retry left close cleanup pending");
            checks.Add(
                $"candidate19-d[oem-handback-retry-separate] faultCode={restoreFaultCode} retryScheduled=true " +
                $"autoRecovered=true writes=3 defaultTables=true state={finalSnapshot.State} " +
                $"closeCleanupPending=false");
        }
        finally
        {
            SafeEngineClose(engine, fakes);
        }
    }

    // ==================================================================
    // 生产 engine 装配（§19 组各段自己的实例，与 §18 的生产流程同构）
    // ==================================================================

    private static (FanHostEngine Engine, RealHcBackend Backend, object Device) CreateProductionEngineFor19(
        string candidatePath,
        string candidateHash,
        CandidateFakes fakes,
        CandidateReflection reflection,
        string tag)
    {
        var token = Guid.NewGuid().ToString("N");
        var authorizationPath = Path.Combine(Path.GetTempPath(), $"yeman-{tag}-{token}.authorization");
        var authorizationSource = Path.Combine(AppContext.BaseDirectory, "YeManFanHost.authorization.md");
        if (!File.Exists(authorizationSource))
        {
            var fallback = Path.Combine(
                "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\FanLab\\real-host",
                "YeManFanHost.authorization.md");
            authorizationSource = File.Exists(fallback) ? fallback : throw new FileNotFoundException(
                "§19 requires the formal authorization record for the production engine flow", fallback);
        }
        File.WriteAllText(
            authorizationPath,
            "920-v1.7 §19 candidate integration (injected OS boundaries; no real device writes)\n" +
            File.ReadAllText(authorizationSource));
        HostDiagnosticLog.SelfTestCaptureEnabled = true;
        var options = HostOptions.Parse(
        [
            "--port", "8891",
            "--real-backend",
            "--hc-assembly", candidatePath,
            "--hc-runtime-root", Path.GetDirectoryName(Path.GetFullPath(candidatePath))!,
            "--hc-expected-sha256", candidateHash,
            "--hc-device-type", "HandheldCompanion.Devices.ROGAlly",
            "--allow-hardware-writes",
            "--authorization", authorizationPath,
            "--session-token", token,
            "--confirm", token,
        ]);
        var engine = new FanHostEngine(options, new HostDiagnosticLog());
        var backend = engine.RealBackendForSelfTest
            ?? throw new InvalidOperationException("§19: production engine did not create a real backend");
        _ = engine.Handshake();
        var device = backend.DeviceForSelfTest
            ?? throw new InvalidOperationException("§19: production engine did not load a device");
        fakes.Boundary.HcDevice = device;
        _ = engine.Open();
        _ = engine.OpenEvents();
        if (reflection.GetBool(device, "IsOpen") != true)
            throw new InvalidOperationException("§19: production engine did not reach the device-open state");
        return (engine, backend, device);
    }

    /**
     * 关闭 §19 组的生产 engine 并**结算故障边**：先关掉共享捕获（避免故障侧的有界重试事件
     * 污染后续场景的捕获），再释放注入故障让关闭/重试可结算，最后等待引擎离开重试状态。
     */
    private static void SafeEngineClose(FanHostEngine engine, CandidateFakes fakes)
    {
        Trace19("close-enter");
        HostDiagnosticLog.SelfTestCaptureEnabled = false;
        try { fakes.Acpi.Reset(CandidateAcpiMode.MatchDefaults); } catch { /* 注入故障释放失败不影响断言 */ }
        Trace19("close-before-engine-close");
        try { _ = engine.Close(); } catch { /* 关闭失败由状态机记录，测试不吞掉断言 */ }
        Trace19("close-after-engine-close");
        WaitUntil(() => engine.Snapshot().State != "FaultLocked", 5_000);
        HostDiagnosticLog.SelfTestCaptureEnabled = false;
        // 测试自身的 engine 不得带着未结算的有界重试进入下一个场景（否则其重试写入会污染后续证据）。
        var snapshot = engine.Snapshot();
        if (snapshot.State == "FaultLocked" || engine.RecoveryRetryTimerActiveForSelfTest)
            throw new InvalidOperationException(
                $"§19: test engine left retrying: state={snapshot.State} " +
                $"attempts={engine.RecoveryRetryAttemptsForSelfTest} timerActive={engine.RecoveryRetryTimerActiveForSelfTest} " +
                $"cleanupPending={snapshot.HcCloseCleanupPending} lastError={snapshot.LastError}");
        // 释放 engine 自有资源（含 STA 派发/诊断定时器）；否则测试进程会一直挂着不退出。
        try { engine.Dispose(); } catch { /* 释放失败由状态机记录 */ }
        Trace19("close-disposed");
    }

    /** 作用域判定的测试视图（§19-C：只暴露判定、本次收据条数与传输口径）。 */
    private readonly record struct ScopeVerdict19(
        string Kind,
        string Detail,
        bool ExplicitFailure,
        bool Unknown,
        bool NoneTransported,
        bool Transported,
        int ReceiptCount)
    {
        /** 2026-09-23：部分通道失败（至少一个通道已送达）——不算成功，也不再致命。 */
        public bool Partial => Kind == "partial";
        public override string ToString() =>
            $"kind={Kind} receipts={ReceiptCount} explicitFailure={ExplicitFailure} unknown={Unknown} " +
            $"noneTransported={NoneTransported} detail={Detail}";
    }

    private ScopeVerdict19 JudgeScope19(object? scope)
    {
        // scope 来自候选 AsusAcpiObservations.BeginScope；null = 旧 runtime（不支持作用域）。
        var handle = scope is null ? null : new AsusWriteScopeHandle { Scope = scope, Kind = "matrix" };
        var verdict = CompleteAsusWriteScope(handle, "matrix");
        return new ScopeVerdict19(
            verdict.Kind, verdict.Detail, verdict.ExplicitFailure, verdict.Unknown,
            verdict.NoneTransported, verdict.Transported, verdict.ReceiptCount);
    }

    /** 带自定义节点曲线的 Enable 请求体（§19-D 用户曲线用）。 */
    private static JsonElement BuildEnableBody(string leaseId, IReadOnlyList<FanNode> nodes) =>
        JsonDocument.Parse(JsonSerializer.Serialize(
            new
            {
                leaseId,
                nodes = nodes.Select(node => new { tempC = node.TempC, dutyPercent = node.DutyPercent }).ToArray(),
            },
            JsonDefaults.Options)).RootElement.Clone();

    private static bool WaitUntil(Func<bool> condition, int timeoutMs)
    {
        var deadline = Stopwatch.GetTimestamp() + timeoutMs * Stopwatch.Frequency / 1000d;
        while (Stopwatch.GetTimestamp() < deadline)
        {
            if (condition()) return true;
            Thread.Sleep(10);
        }
        return condition();
    }

    private static string Truncate(string text, int length) =>
        text.Length <= length ? text : text[..length] + "…";
}