using System.Diagnostics;

namespace YeManFanHost;

/**
 * 920-v1.10 §22：结算必须绑定周期、摘除清单必须可证完整（无硬件）。
 *
 * 三项检查（A 组）：
 *  - 正常观察结算：最后读完成后由观察结算，且**新报告**发布、历史报告保持快照语义；
 *  - 旧观察迟到提交：观察在新周期出现后提交 ⇒ 必须丢弃（等待返回 false、不写新报告）；
 *  - 清单丢失（原子合同违约）：重试取得的清单**不得**用来宣称完成，责任保留。
 * 失败即以异常结束 ⇒ 进程非零退出。
 */
internal static class Section22CandidateLifecycle
{
    /** §22 组最少检查条数：§18–§21 的 54 条 + §22 组 4 条 = 58。 */
    internal const int MinimumCheckCount = 58;

    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection22CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    internal static IReadOnlyList<string> RunSection22CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;

        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("§22: candidate reflection bridge missing");
        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var gate = new InterleaveGate();
        reflection.SetInterleaveHook(gate.OnPoint);
        try
        {
            var device = backend.OnStaBounded(reflection.CreateRogAlly, "s22-create-candidate-device");
            fakes.Boundary.HcDevice = device;
            reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
            checks.Add(
                $"candidate-identity-22 sha256={candidateHash} mvid={reflection.ModuleVersionId} " +
                $"settlementIdentityBound=true detachAtomicityContract=present " +
                $"detachAtomic={fakes.Boundary.DetachIsAtomicOnFailure}");

            Run22_NormalObservationSettle(fakes, reflection, backend, device, checks);
            Run22_LateObservationDropped(fakes, reflection, backend, device, checks);
            // 末位：本项按设计保留未结算责任（合同违约），必须最后执行。
            Run22_LostInventoryNotHidden(fakes, reflection, backend, device, checks);
            return checks;
        }
        finally
        {
            reflection.SetInterleaveHook(null);
            gate.Disarm();
        }
    }

    /**
     * 让读取循环**保持一次未决读**（不完成它）：Close 时清理释放会按设计延迟给“最后读取者”。
     * 这与真实边界 CancelIoEx 后读取仍短暂未退出的情形同构。
     * 920-v1.13 §24.2：必须建立“现在确有已登记的未决读取”这一前提——`WaitReadEntered`
     * 是粘滞事件，新会话的读取尚未登记时也会立即返回，延迟释放随之落空，
     * 使本组“旧观察不得结算新周期”的断言在竞态下把合法结算误判为失败（本批实测一次）。
     */
    private static void ParkOnePendingRead22(CandidateFakes fakes)
    {
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("§22: read loop did not enter its first read");
        if (!fakes.Boundary.WaitPendingReadRegistered(3_000))
            throw new InvalidOperationException(
                "§22: no registered in-flight read for the current device at the release boundary");
    }

    /** 关闭后放行停住的读并排空循环（延迟释放由“最后读取者”在替身中结算）。 */
    private static void DrainAfterDeferredClose22(CandidateFakes fakes, CandidateReflection reflection, object device)
    {
        fakes.Boundary.BlockReadDispatch = false;
        for (var attempt = 0; attempt < 6 && !reflection.WaitReadLoopEnded(device, 500, out _); attempt++)
            fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        reflection.WaitReadLoopEnded(device, 5_000, out var detail);
        if (reflection.GetString(device, "ReadLoopState") == "Running")
            throw new InvalidOperationException($"§22: read loop did not end after the deferred close: {detail}");
    }

    /** A1：正常观察结算——最后读完成后由观察结算；发布新报告，历史报告保持快照。 */
    private static void Run22_NormalObservationSettle(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        ParkOnePendingRead22(fakes);
        fakes.Boundary.DeferDetachedReleases = 1;
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        var reportBefore = reflection.DescribeReceiptReport(closeReceipt);
        if (reflection.GetBool(closeReceipt, "CleanupCompleted") || !reflection.GetCleanupCycleRetained(device))
            throw new InvalidOperationException(
                $"§22A: deferred release must leave the cycle unsettled: {reflection.Describe(closeReceipt)}");

        // 最后读退出（真实边界里由最后一个读取者关闭句柄；替身在读取完成时结算延迟释放）。
        DrainAfterDeferredClose22(fakes, reflection, device);

        var settled = reflection.WaitCloseCleanupCompleted(device, 10_000, out var detail);
        var reportAfter = reflection.DescribeReceiptReport(closeReceipt);
        var retained = reflection.GetCleanupCycleRetained(device);
        if (!settled || retained)
            throw new InvalidOperationException(
                $"§22A: legitimate observation did not settle: settled={settled} retained={retained} detail={detail}");
        if (!string.Equals(reportBefore, reportAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§22A: an already-published report was rewritten:\n before={reportBefore}\n after ={reportAfter}");
        checks.Add(
            $"candidate22-a[normal-observation-settle] settled=true cycleRetained=false " +
            $"historicalReportUnchanged=true pendingAtBoundary={fakes.Boundary.PendingCountForDiagnostics} " +
            $"detail={Truncate(detail, 110)}");
    }

    /** A2：旧观察在新周期出现后迟到提交 ⇒ 丢弃（返回 false），不写新报告。 */
    private static void Run22_LateObservationDropped(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        ParkOnePendingRead22(fakes);
        fakes.Boundary.DeferDetachedReleases = 1;
        var close1 = reflection.CloseDeviceLifecycle(device);
        if (reflection.GetBool(close1, "CleanupCompleted"))
            throw new InvalidOperationException("§22A: first close unexpectedly completed");
        DrainAfterDeferredClose22(fakes, reflection, device);

        // 旧观察：捕获到“空 pending”快照后停住，此时发布新会话并 Close（新周期出现）。
        fakes.Boundary.BlockNextPendingObservation = 1;
        var waiterDetail = "<none>";
        var waiter = Task.Run(() =>
        {
            var result = reflection.WaitCloseCleanupCompleted(device, 8_000, out var detail);
            waiterDetail = detail;
            return result;
        });
        if (!fakes.Boundary.WaitPendingObservationEntered(8_000))
            throw new InvalidOperationException("§22A: observer did not reach its park point");
        if (reflection.CallInstance(device, "Open") is not true)
            throw new InvalidOperationException("§22A: new session publish failed");
        var nextId = reflection.StartDeviceOpen(device, true);
        var nextReceipt = reflection.WaitOpenReceipt(device, nextId, 20_000)
            ?? throw new InvalidOperationException("§22A: new-session operation did not finish");
        if (reflection.GetString(nextReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"§22A: new session did not complete: {reflection.Describe(nextReceipt)}");
        // 新会话的释放同样延迟给在途读取 ⇒ 边界 pending 保持非空（旧观察者的后续观察仍不能结算）。
        ParkOnePendingRead22(fakes);
        fakes.Boundary.DeferDetachedReleases = 1;
        var close2 = reflection.CloseDeviceLifecycle(device);
        var reportBefore = reflection.DescribeReceiptReport(close2);
        var waiterFinishedBeforeRelease = waiter.IsCompleted;
        fakes.Boundary.ReleasePendingObservation();
        if (!waiter.Wait(12_000))
            throw new InvalidOperationException("§22A: parked observer did not finish");
        var waiterResult = waiter.Result;
        var reportAfter = reflection.DescribeReceiptReport(close2);
        if (waiterResult)
            throw new InvalidOperationException(
                $"§22A: a stale observation settled the new cycle " +
                $"(finishedBeforeRelease={waiterFinishedBeforeRelease} waiterDetail={Truncate(waiterDetail, 200)} " +
                $"pendingAtBoundary={fakes.Boundary.PendingCountForDiagnostics} " +
                $"cycle={reflection.GetCleanupCycle(device)} ledger={reflection.GetCleanupLedger(device)})");
        if (!string.Equals(reportBefore, reportAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§22A: a stale observation rewrote the current report:\n before={reportBefore}\n after ={reportAfter}");
        checks.Add(
            $"candidate22-a[late-observation-dropped] waitReturned=false newReportUnchanged=true " +
            $"pendingAtBoundary={fakes.Boundary.PendingCountForDiagnostics} " +
            $"ledger={Truncate(reflection.GetCleanupLedger(device), 120)}");
    }

    /** A3：摘除清单丢失（原子合同违约）⇒ 重试取得的清单不得用来宣称完成，责任保留。 */
    private static void Run22_LostInventoryNotHidden(
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
        var close2 = reflection.CloseDeviceLifecycle(device);
        var contractViolated = !fakes.Boundary.DetachIsAtomicOnFailure;
        var retained = reflection.GetCleanupCycleRetained(device);
        if (reflection.GetBool(close1, "CleanupCompleted") || reflection.GetBool(close2, "CleanupCompleted"))
            throw new InvalidOperationException(
                $"§22A: a lost inventory was hidden by a later nonempty retry: " +
                $"{reflection.Describe(close2)}");
        if (!contractViolated || !retained)
            throw new InvalidOperationException(
                $"§22A: contract violation was not detected as such (violated={contractViolated} retained={retained})");
        // §22.2-A/B：空 pending 的一次观察**不得**把丢失清单/未解责任收敛掉（与 RunCycleCleanup 同规则）。
        var observedSettle = reflection.WaitCloseCleanupCompleted(device, 400, out var observeDetail);
        if (observedSettle || !reflection.GetCleanupCycleRetained(device) ||
            !reflection.GetCleanupLedger(device).Contains("ownComplete=False", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§22A: an observation erased the lost-inventory responsibility " +
                $"(settled={observedSettle} detail={observeDetail} ledger={reflection.GetCleanupLedger(device)})");
        checks.Add(
            $"candidate22-a[lost-inventory-responsibility-retained] firstCompleted=false secondCompleted=false " +
            $"contractViolated=true cycleRetained=true observationCannotConverge=true " +
            $"detachFailures={fakes.Boundary.DetachFailures} " +
            $"ledger={Truncate(reflection.GetCleanupLedger(device), 110)}");
    }
}