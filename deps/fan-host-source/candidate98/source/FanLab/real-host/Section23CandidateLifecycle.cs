namespace YeManFanHost;

/**
 * 920-v1.11 §23：清理完成判据统一 + 固定验收矩阵（无硬件）。
 *
 * 原则（与 §21/§22 相同，不新起架构）：
 * - 驱动**候选真实**生命周期（唯一管理逻辑），只在 OS/HID/ACPI 边界注入；
 * - 每一项同时检查**三个出口**：等待返回（TryWaitCloseCleanupCompleted）/ 当前报告（Complete）/
 *   未结算周期台账（ownComplete 或 no-cycle）——三者必须一致，不允许“一个说完成、另一个说没完成”；
 * - 观察路径只更新**被观察证据证明**的分项（pending 为空只证明延迟释放已收敛，不证明 remap 成功，
 *   也不证明失败/未知已解决）；旧观察不得覆盖同周期内的状态修订；
 * - 末位项按设计保留未结算责任（原子合同违约），必须最后执行。
 * 失败即以异常结束 ⇒ 进程非零退出。
 */
internal static class Section23CandidateLifecycle
{
    /** §23 组最少检查条数：§18–§22 的 58 条 + §23 组 12 条 = 70。 */
    internal const int MinimumCheckCount = 70;

    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection23CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    internal static IReadOnlyList<string> RunSection23CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;

        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("§23: candidate reflection bridge missing");
        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);

        var device = backend.OnStaBounded(reflection.CreateRogAlly, "s23-create-candidate-device");
        fakes.Boundary.HcDevice = device;
        reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
        checks.Add(
            $"candidate-identity-23 sha256={candidateHash} mvid={reflection.ModuleVersionId} " +
            "unifiedCleanupCriteria=single-source componentStates=success|failed|pending|unknown|not-applicable " +
            "observationUpdates=evidence-proven-only observationRevisionChecked=true");

        Run23_DetachSequenceSharedByRealAdapter(reflection, backend, checks);
        // ---- 固定矩阵（顺序固定；末位项会保留未结算责任，必须最后跑）----
        Run23_NoRemapTargetEmptyCleanup(reflection, device, checks);                          // ⑥
        Run23_NormalDeferredReleaseConverges(fakes, reflection, backend, device, checks);     // ①
        Run23_RemapFailureWithReleasedHandles(fakes, reflection, backend, device, checks);    // ②
        Run23_FailedComponentsResolvedByRetry(fakes, reflection, backend, device, checks);    // ⑤
        Run23_ObservationCannotEraseRemapFailure(fakes, reflection, backend, device, checks); // ③（本轮反例）
        Run23_RemapSuccessWithFailedHandle(fakes, reflection, backend, device, checks);       // ④a
        Run23_RemapSuccessWithUnknownHandle(fakes, reflection, backend, device, checks);      // ④b
        Run23_SameCycleRevisionDropsStaleObservation(fakes, reflection, backend, device, checks);  // ⑧b
        Run23_StaleObservationAcrossCyclesDropped(fakes, reflection, backend, device, checks);     // ⑧a
        Run23_LostInventoryResponsibilityRetained(fakes, reflection, backend, device, checks);     // ⑦
        return checks;
    }

    // ==================================================================
    // §23.3：真实适配器与测试共用的无 I/O 摘除序列
    // ==================================================================

    /**
     * 两个层次都验证：
     * 1）共享函数的结构性质——capture 抛错时 clear **不被调用**（抛错 ⇒ 完全未摘除）；
     * 2）**真实适配器**（RogAllyHidBoundary，非替身）在同一段逻辑上的故障注入——表项保持原样、
     *    重试取得完整清单并按实例去重（不依赖真机：该段无任何 I/O）。
     */
    private static void Run23_DetachSequenceSharedByRealAdapter(
        CandidateReflection reflection,
        RealHcBackend backend,
        List<string> checks)
    {
        var sequenceProbe = reflection.DetachSequenceStructuralProbe();
        if (!sequenceProbe.Contains("faultPropagated=True", StringComparison.Ordinal) ||
            !sequenceProbe.Contains("clearCallsAfterFault=0", StringComparison.Ordinal) ||
            !sequenceProbe.Contains("clearCallsAfterSuccess=1", StringComparison.Ordinal) ||
            !sequenceProbe.Contains("inventoryCount=1", StringComparison.Ordinal) ||
            !sequenceProbe.Contains("dedupIn=3 dedupOut=2", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23C: shared detach sequence lost its atomicity property: {sequenceProbe}");

        var realDevice = backend.OnStaBounded(reflection.CreateRogAlly, "s23-real-boundary-probe-device");
        var realBoundary = reflection.CreateRealBoundary(realDevice);
        var adapter = reflection.RealAdapterDetachProbe(realDevice, realBoundary);
        if (!adapter.Contains("firstInventory=1", StringComparison.Ordinal) ||
            !adapter.Contains("[2->0]", StringComparison.Ordinal) ||
            !adapter.Contains("faultPropagated=True", StringComparison.Ordinal) ||
            !adapter.Contains("tableEntriesAfterFault=2(beforeFault=2)", StringComparison.Ordinal) ||
            !adapter.Contains("survivorHeldAfterFault=True", StringComparison.Ordinal) ||
            !adapter.Contains("retryInventory=1", StringComparison.Ordinal) ||
            !adapter.Contains("tableEntriesAfterRetry=0", StringComparison.Ordinal) ||
            !adapter.Contains("survivorHeldAfterRetry=False", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23C: real adapter detach atomicity could not be demonstrated without hardware: {adapter}");
        checks.Add(
            $"candidate23-c[detach-sequence-shared-by-real-adapter] sharedLogic={sequenceProbe} " +
            $"realAdapter={adapter}");
    }

    // ==================================================================
    // 固定矩阵
    // ==================================================================

    /** ⑥ 无 remap 目标的正常空清理：明确“不适用”，不阻断完成（对照：不能把 Complete 改成无条件要求 remap 成功）。 */
    private static void Run23_NoRemapTargetEmptyCleanup(
        CandidateReflection reflection,
        object device,
        List<string> checks)
    {
        var close = reflection.CloseDeviceLifecycle(device);
        var report = reflection.DescribeReceiptReport(close);
        var wait = reflection.WaitCloseCleanupCompleted(device, 1_000, out var detail);
        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        if (!reflection.GetBool(close, "CleanupCompleted") || !wait ||
            !report.Contains("remapState=NotApplicable", StringComparison.Ordinal) ||
            !report.Contains("complete=True", StringComparison.Ordinal) ||
            !cycleState.Contains("completed=True", StringComparison.Ordinal) ||
            ledger != "no-cycle")
            throw new InvalidOperationException(
                $"§23⑥: a cleanup without a remap target must be a normal (not-applicable) completion: " +
                $"report={report} wait={wait} cycle={cycleState} ledger={ledger} detail={detail}");
        checks.Add(
            $"candidate23-a[no-remap-target-empty-cleanup] remapState=NotApplicable waitReturned=true " +
            $"reportComplete=true ledger=no-cycle detail={Truncate(detail, 90)}");
    }

    /** ① 正常 deferred → 真实释放完成：等待返回/报告/台账三出口一致为完成；已发布报告保持快照语义。 */
    private static void Run23_NormalDeferredReleaseConverges(
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
        var close = reflection.CloseDeviceLifecycle(device);
        var reportBefore = reflection.DescribeReceiptReport(close);
        if (reflection.GetBool(close, "CleanupCompleted") || !reflection.GetCleanupCycleRetained(device) ||
            !reportBefore.Contains("pending=[device-release:", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23①: deferred release must leave the cycle unsettled: {reportBefore}");

        DrainAfterDeferredClose22(fakes, reflection, device);
        var wait = reflection.WaitCloseCleanupCompleted(device, 10_000, out var detail);
        var reportAfter = reflection.DescribeReceiptReport(close);
        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        if (!wait || ledger != "no-cycle" ||
            !cycleState.Contains("completed=True", StringComparison.Ordinal) ||
            !cycleState.Contains("complete=True", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23①: legitimate observed settlement did not converge on all three outlets: " +
                $"wait={wait} cycle={cycleState} ledger={ledger}");
        if (!string.Equals(reportBefore, reportAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23①: an already-published report was rewritten:\n before={reportBefore}\n after ={reportAfter}");
        checks.Add(
            $"candidate23-a[normal-deferred-release] waitReturned=true reportComplete=true ledger=no-cycle " +
            $"historicalReportUnchanged=true detail={Truncate(detail, 110)}");
    }

    /** ② remap 失败 + 句柄已成功释放：句柄完成**不得**抹掉 remap 失败（本轮 P1 的两个分项组合）。 */
    private static void Run23_RemapFailureWithReleasedHandles(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.ThrowOnRemap = 1;
        var close = reflection.CloseDeviceLifecycle(device);
        var report = reflection.DescribeReceiptReport(close);
        var wait = reflection.WaitCloseCleanupCompleted(device, 600, out var detail);
        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        if (reflection.GetBool(close, "CleanupCompleted") || wait ||
            !report.Contains("remapState=Failed", StringComparison.Ordinal) ||
            !report.Contains("failures=[remap-release:", StringComparison.Ordinal) ||
            !report.Contains("devices=[Released|Released]", StringComparison.Ordinal) ||
            report.Contains("complete=True", StringComparison.Ordinal) ||
            !cycleState.Contains("completed=False", StringComparison.Ordinal) ||
            ledger.Contains("ownComplete=True", StringComparison.Ordinal) ||
            !ledger.Contains("remapState=Failed", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23②: released handles erased the remap failure: report={report} wait={wait} " +
                $"cycle={cycleState} ledger={ledger}");
        checks.Add(
            $"candidate23-a[remap-failure-with-released-handles] handlesReleased=1 remapState=Failed " +
            $"waitReturned=false reportComplete=false ledgerComplete=false remapFailureRetained=true " +
            $"detail={Truncate(detail, 100)}");
    }

    /** ⑤ 失败分项经**真正重试**成功：remap 与句柄释放两项都被同一周期重试解决，三出口一致转为完成。 */
    private static void Run23_FailedComponentsResolvedByRetry(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.ThrowOnRemap = 1;
        fakes.Boundary.FailDetachedReleases = 1;
        var close1 = reflection.CloseDeviceLifecycle(device);
        if (reflection.GetBool(close1, "CleanupCompleted"))
            throw new InvalidOperationException(
                $"§23⑤: both components failed but the cycle reported completion: {reflection.Describe(close1)}");
        var retriesBefore = fakes.Boundary.ReleaseDetachedCalls;
        var remapOffBefore = fakes.Boundary.RemapOffCount;

        var close2 = reflection.CloseDeviceLifecycle(device);
        var wait = reflection.WaitCloseCleanupCompleted(device, 5_000, out var detail);
        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        var report2 = reflection.DescribeReceiptReport(close2);
        if (!reflection.GetBool(close2, "CleanupCompleted") || !wait || ledger != "no-cycle" ||
            !cycleState.Contains("completed=True", StringComparison.Ordinal) ||
            !report2.Contains("remapState=Success", StringComparison.Ordinal) ||
            report2.Contains("remap-release:", StringComparison.Ordinal) ||
            fakes.Boundary.ReleaseDetachedCalls <= retriesBefore || fakes.Boundary.RemapOffCount <= remapOffBefore ||
            fakes.Boundary.DevicesReleased < 1)
            throw new InvalidOperationException(
                $"§23⑤: a real retry did not resolve the failed components: report={report2} wait={wait} " +
                $"cycle={cycleState} ledger={ledger} releaseCalls={fakes.Boundary.ReleaseDetachedCalls} " +
                $"remapOff={fakes.Boundary.RemapOffCount}");
        checks.Add(
            $"candidate23-a[failed-components-resolved-by-retry] firstCompleted=false retryCompleted=true " +
            $"remapAttemptsResolved=true handleRetried=true releaseCalls={fakes.Boundary.ReleaseDetachedCalls} " +
            $"remapOff={fakes.Boundary.RemapOffCount} waitReturned=true ledger=no-cycle");
    }

    /**
     * ③ remap 失败 + 句柄 deferred 后成功（**本轮反例**）：
     * 观察在“句柄释放已收敛（pending 为空）”之后运行，仍不得消除 remap 失败，也不得报告完成。
     * 通过停在“已捕获快照、尚未提交”的观察点保证严格顺序（不靠 sleep 竞态）。
     */
    private static void Run23_ObservationCannotEraseRemapFailure(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        ParkOnePendingRead22(fakes);
        fakes.Boundary.ThrowOnRemap = 1;
        fakes.Boundary.DeferDetachedReleases = 1;
        var close1 = reflection.CloseDeviceLifecycle(device);
        var report1 = reflection.DescribeReceiptReport(close1);
        if (reflection.GetBool(close1, "CleanupCompleted") ||
            !report1.Contains("remapState=Failed", StringComparison.Ordinal) ||
            !report1.Contains("pending=[device-release:", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23③: initial report must show the remap failure plus a deferred handle: {report1}");

        // 观察停在“快照已捕获”（其后的 pending 查询会返回旧快照）；用事件而不是 sleep 保证顺序。
        fakes.Boundary.BlockNextPendingObservation = 1;
        var waiter = Task.Run(() => reflection.WaitCloseCleanupCompleted(device, 8_000, out _));
        if (!fakes.Boundary.WaitPendingObservationEntered(8_000))
            throw new InvalidOperationException("§23③: observer did not reach its park point");
        // 最后读退出：句柄释放完成（pending 变空）——这是观察路径**唯一**被证明收敛的分项。
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        fakes.Boundary.ReleasePendingObservation();
        if (!waiter.Wait(12_000))
            throw new InvalidOperationException("§23③: parked observer did not finish");
        var waitResult = waiter.Result;

        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        if (waitResult || !reflection.GetCleanupCycleRetained(device) ||
            !cycleState.Contains("completed=False", StringComparison.Ordinal) ||
            !cycleState.Contains("remapState=Failed", StringComparison.Ordinal) ||
            !cycleState.Contains("failures=[remap-release:", StringComparison.Ordinal) ||
            !ledger.Contains("ownComplete=False", StringComparison.Ordinal) ||
            !ledger.Contains(":Released:attempts=", StringComparison.Ordinal) ||
            fakes.Boundary.RemapOffCount != 0)
            throw new InvalidOperationException(
                $"§23③: an observation erased the remap failure (or the handle did not converge): " +
                $"wait={waitResult} cycle={cycleState} ledger={ledger} remapOff={fakes.Boundary.RemapOffCount}");

        // 只有“真正重试 remap 成功”才允许清除失败并结算。
        var close2 = reflection.CloseDeviceLifecycle(device);
        var wait2 = reflection.WaitCloseCleanupCompleted(device, 5_000, out var detail2);
        var report2 = reflection.DescribeReceiptReport(close2);
        if (!reflection.GetBool(close2, "CleanupCompleted") || !wait2 || reflection.GetCleanupCycleRetained(device) ||
            !report2.Contains("remapState=Success", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23③: the failure was not cleared by a real retry: report={report2} wait={wait2} detail={detail2}");
        checks.Add(
            $"candidate23-a[observation-cannot-erase-remap-failure] observationEntered=true " +
            $"handleConvergedByEvidence=true remapFailureRetainedAfterObservation=true waitReturned=false " +
            $"cycleRetained=true retryClearsFailureOnly=true remapOffAfterRetry={fakes.Boundary.RemapOffCount}");
    }

    /** ④a remap 成功 + 句柄释放失败：句柄失败必须阻断完成；真正重试后才结算。 */
    private static void Run23_RemapSuccessWithFailedHandle(
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
        var report1 = reflection.DescribeReceiptReport(close1);
        var wait1 = reflection.WaitCloseCleanupCompleted(device, 600, out _);
        var ledger1 = reflection.GetCleanupLedger(device);
        if (reflection.GetBool(close1, "CleanupCompleted") || wait1 ||
            !report1.Contains("remapState=Success", StringComparison.Ordinal) ||
            !report1.Contains("failures=[device-release:", StringComparison.Ordinal) ||
            !ledger1.Contains(":Failed:attempts=1", StringComparison.Ordinal) ||
            ledger1.Contains("ownComplete=True", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23④a: a failed handle release was reported as complete: report={report1} wait={wait1} " +
                $"ledger={ledger1}");

        var close2 = reflection.CloseDeviceLifecycle(device);
        var wait2 = reflection.WaitCloseCleanupCompleted(device, 5_000, out var detail2);
        var ledger2 = reflection.GetCleanupLedger(device);
        if (!reflection.GetBool(close2, "CleanupCompleted") || !wait2 || ledger2 != "no-cycle" ||
            fakes.Boundary.DevicesReleased < 1)
            throw new InvalidOperationException(
                $"§23④a: retry did not resolve the failed handle: {reflection.Describe(close2)} " +
                $"ledger={ledger2} detail={detail2}");
        checks.Add(
            $"candidate23-a[remap-success-with-failed-handle] firstComplete=false waitReturned=false " +
            $"ledgerFailed=true retryComplete=true devicesReleased={fakes.Boundary.DevicesReleased}");
    }

    /** ④b remap 成功 + 句柄释放**未知**：未知不得被当作已释放（也不得被豁免），必须阻断完成。 */
    private static void Run23_RemapSuccessWithUnknownHandle(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.UnknownDetachedReleases = 1;
        var close1 = reflection.CloseDeviceLifecycle(device);
        var report1 = reflection.DescribeReceiptReport(close1);
        var wait1 = reflection.WaitCloseCleanupCompleted(device, 600, out _);
        var ledger1 = reflection.GetCleanupLedger(device);
        if (reflection.GetBool(close1, "CleanupCompleted") || wait1 ||
            !report1.Contains("unknown=[device-release:unknown:", StringComparison.Ordinal) ||
            !report1.Contains("complete=False", StringComparison.Ordinal) ||
            !ledger1.Contains(":Unknown:attempts=1", StringComparison.Ordinal) ||
            ledger1.Contains("ownComplete=True", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23④b: an unknown handle release was exempted from the completion criteria: " +
                $"report={report1} wait={wait1} ledger={ledger1}");

        var close2 = reflection.CloseDeviceLifecycle(device);
        var wait2 = reflection.WaitCloseCleanupCompleted(device, 5_000, out var detail2);
        var ledger2 = reflection.GetCleanupLedger(device);
        if (!reflection.GetBool(close2, "CleanupCompleted") || !wait2 || ledger2 != "no-cycle" ||
            fakes.Boundary.UnknownReleases != 1 || fakes.Boundary.DevicesReleased < 1)
            throw new InvalidOperationException(
                $"§23④b: retry did not resolve the unknown handle state: {reflection.Describe(close2)} " +
                $"ledger={ledger2} unknown={fakes.Boundary.UnknownReleases} detail={detail2}");
        checks.Add(
            $"candidate23-a[remap-success-with-unknown-handle] firstComplete=false unknownRelease=1 " +
            $"waitReturned=false retryComplete=true devicesReleased={fakes.Boundary.DevicesReleased}");
    }

    /**
     * ⑧b 同周期状态修订：观察在捕获快照后，同周期内发生了一次**执行重试**（分项状态更新）；
     * 旧观察不得提交，更不得把较新的失败覆盖成“观察收敛后的完成”。
     */
    private static void Run23_SameCycleRevisionDropsStaleObservation(
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
            throw new InvalidOperationException("§23⑧b: first close unexpectedly completed");
        DrainAfterDeferredClose22(fakes, reflection, device);

        fakes.Boundary.BlockNextPendingObservation = 1;
        var waiter = Task.Run(() => reflection.WaitCloseCleanupCompleted(device, 8_000, out _));
        if (!fakes.Boundary.WaitPendingObservationEntered(8_000))
            throw new InvalidOperationException("§23⑧b: observer did not reach its park point");

        // 观察已捕获快照（修订 R）；此刻同一周期内发生一次真实重试，并把释放打成失败（修订 R+1）。
        fakes.Boundary.FailDetachedReleases = 1;
        var close2 = reflection.CloseDeviceLifecycle(device);
        var reportAfterRetry = reflection.DescribeReceiptReport(close2);
        fakes.Boundary.ReleasePendingObservation();
        if (!waiter.Wait(12_000))
            throw new InvalidOperationException("§23⑧b: parked observer did not finish");
        var waitResult = waiter.Result;

        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        var reportNow = reflection.GetCleanupCycle(device);
        if (waitResult || !reflection.GetCleanupCycleRetained(device) ||
            !cycleState.Contains("completed=False", StringComparison.Ordinal) ||
            !ledger.Contains(":Failed:attempts=2", StringComparison.Ordinal) ||
            !ledger.Contains("ownComplete=False", StringComparison.Ordinal) ||
            !reportAfterRetry.Contains("failures=[device-release:", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23⑧b: a stale same-cycle observation overwrote the newer revision: wait={waitResult} " +
                $"cycle={cycleState} ledger={ledger} reportAfterRetry={reportAfterRetry}");
        if (!reportNow.Contains("complete=False", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23⑧b: current report does not reflect the newer failure: {reportNow}");

        // 第三次 Close：真正重试成功后结算（离开本项时状态干净）。
        var close3 = reflection.CloseDeviceLifecycle(device);
        var wait3 = reflection.WaitCloseCleanupCompleted(device, 5_000, out var detail3);
        var ledger3 = reflection.GetCleanupLedger(device);
        if (!reflection.GetBool(close3, "CleanupCompleted") || !wait3 || ledger3 != "no-cycle")
            throw new InvalidOperationException(
                $"§23⑧b: retry after the dropped observation did not settle: {reflection.Describe(close3)} " +
                $"ledger={ledger3} detail={detail3}");
        checks.Add(
            $"candidate23-a[same-cycle-revision-drops-stale-observation] staleObservationDropped=true " +
            $"waitReturned=false ledgerAttempts=2 cycleRetained=true newerFailurePreserved=true " +
            $"retrySettled=true releaseCalls={fakes.Boundary.ReleaseDetachedCalls}");
    }

    /** ⑧a 旧观察跨周期：观察在新周期出现后提交 ⇒ 丢弃，不写新报告、不结算新周期。 */
    private static void Run23_StaleObservationAcrossCyclesDropped(
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
            throw new InvalidOperationException("§23⑧a: first close unexpectedly completed");
        DrainAfterDeferredClose22(fakes, reflection, device);

        fakes.Boundary.BlockNextPendingObservation = 1;
        var waiter = Task.Run(() => reflection.WaitCloseCleanupCompleted(device, 8_000, out _));
        if (!fakes.Boundary.WaitPendingObservationEntered(8_000))
            throw new InvalidOperationException("§23⑧a: observer did not reach its park point");

        // 新周期出现：新会话 + 新操作 + 新 Close（其句柄同样延迟释放）。
        if (reflection.CallInstance(device, "Open") is not true)
            throw new InvalidOperationException("§23⑧a: new session publish failed");
        var nextId = reflection.StartDeviceOpen(device, true);
        var nextReceipt = reflection.WaitOpenReceipt(device, nextId, 20_000)
            ?? throw new InvalidOperationException("§23⑧a: new-session operation did not finish");
        if (reflection.GetString(nextReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"§23⑧a: new session did not complete: {reflection.Describe(nextReceipt)}");
        ParkOnePendingRead22(fakes);
        fakes.Boundary.DeferDetachedReleases = 1;
        var close2 = reflection.CloseDeviceLifecycle(device);
        var reportBefore = reflection.DescribeReceiptReport(close2);
        fakes.Boundary.ReleasePendingObservation();
        if (!waiter.Wait(12_000))
            throw new InvalidOperationException("§23⑧a: parked observer did not finish");
        var waitResult = waiter.Result;
        var reportAfter = reflection.DescribeReceiptReport(close2);
        var ledger = reflection.GetCleanupLedger(device);
        if (waitResult)
            throw new InvalidOperationException("§23⑧a: a stale observation settled the new cycle");
        if (!string.Equals(reportBefore, reportAfter, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23⑧a: a stale observation rewrote the current report:\n before={reportBefore}\n after ={reportAfter}");
        if (!ledger.Contains("ownComplete=False", StringComparison.Ordinal))
            throw new InvalidOperationException($"§23⑧a: stale observation erased the new cycle: {ledger}");

        // 排空新周期的在途读取并结算（离开本项时状态干净）。
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        var wait2 = reflection.WaitCloseCleanupCompleted(device, 10_000, out var detail2);
        var ledger2 = reflection.GetCleanupLedger(device);
        if (!wait2 || ledger2 != "no-cycle")
            throw new InvalidOperationException(
                $"§23⑧a: the new cycle did not settle after its readers drained: ledger={ledger2} detail={detail2}");
        checks.Add(
            $"candidate23-a[stale-observation-across-cycles-dropped] staleWaitReturned=false " +
            $"newReportUnchanged=true newCycleRetained=true newCycleSettledAfterDrain=true " +
            $"pendingAtBoundary={fakes.Boundary.PendingCountForDiagnostics}");
    }

    /**
     * ⑦ 清单不完整（原子合同违约）：摘除异常后重试取得的清单**不得**宣称完成；观察也不得收敛。
     * 本项按设计**保留**未结算责任（走既定有界恢复出口），因此必须最后执行。
     */
    private static void Run23_LostInventoryResponsibilityRetained(
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
                $"§23⑦: a lost inventory was hidden by a later nonempty retry: {reflection.Describe(close2)}");
        if (!contractViolated || !retained)
            throw new InvalidOperationException(
                $"§23⑦: contract violation was not detected as such (violated={contractViolated} retained={retained})");

        var observedSettle = reflection.WaitCloseCleanupCompleted(device, 400, out var observeDetail);
        var cycleState = reflection.GetCleanupCycle(device);
        var ledger = reflection.GetCleanupLedger(device);
        if (observedSettle || !reflection.GetCleanupCycleRetained(device) ||
            !cycleState.Contains("completed=False", StringComparison.Ordinal) ||
            !ledger.Contains("ownComplete=False", StringComparison.Ordinal) ||
            !ledger.Contains("detachRounds=2", StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"§23⑦: the lost-inventory responsibility was erased: settled={observedSettle} " +
                $"cycle={cycleState} ledger={ledger} detail={observeDetail}");
        checks.Add(
            $"candidate23-a[lost-inventory-responsibility-retained] firstCompleted=false secondCompleted=false " +
            $"contractViolated=true cycleRetained=true observationCannotConverge=true allOutletsIncomplete=true " +
            $"detachFailures={fakes.Boundary.DetachFailures} ledger={Truncate(ledger, 130)}");
    }
}