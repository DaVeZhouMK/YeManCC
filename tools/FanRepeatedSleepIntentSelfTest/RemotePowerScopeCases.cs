#if YEMAN_FAN_REMOTE_SCOPE_SELFTEST
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using YeManCC.SharedSleepPower;
using YeManFanHost;

internal static class RemotePowerScopeCases
{
    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }

    internal static void Run(string[] args)
    {
        var evidence = Path.GetFullPath(args[0]);
        Directory.CreateDirectory(evidence);
        var cases = new (string Name, Action Run)[]
        {
            ("actual-candidate-abi-layout", () =>
            {
                Require(Marshal.SizeOf<SharedPowerSnapshotV1>() == 48, "snapshot layout");
                Require(Marshal.SizeOf<RemotePowerDescriptorV1>() == 128, "descriptor layout");
                Require(Marshal.SizeOf<RemotePowerSettlementV1>() == 80, "settlement layout");
            }),
            ("accepted-borrow-outlives-http-owner", () =>
            {
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                using var worker = request.Retain();
                Require(worker is not null, "valid worker rejected");
                request.Dispose();
                Require(!request.IsCurrent && worker!.IsCurrent && source.Disposals == 0, "HTTP completion closed worker grant");
                using (var sta = worker!.Retain()) Require(sta is not null && sta.IsCurrent, "worker could not retain actual queued work");
                worker.Dispose();
                Require(source.Disposals == 1 && source.WaitHandlesClosed, "final borrower failed to close import");
                Require(source.Settlements == 0, "Dispose synthesized settlement");
            }),
            ("cancel-does-not-release-inflight-borrow", () =>
            {
                using var source = new NativeScopePort(RemotePowerPurpose.FanResume);
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanResume);
                using var worker = request.Retain();
                Require(worker is not null, "F5 precommit grant rejected");
                request.CancelLocal();
                Require(!request.IsCurrent && !worker!.IsCurrent && request.Retain() is null, "cancel admitted old work");
                request.Dispose();
                Require(source.Disposals == 0 && source.Settlements == 0, "cancel claimed in-flight cleanup");
                worker!.Dispose();
                Require(source.Disposals == 1 && source.Withdrawals == 1 && source.WaitHandlesClosed, "borrower return lost ownership");
            }),
            ("observation-is-not-fan-permission", () =>
            {
                using var source = new NativeScopePort(RemotePowerPurpose.Observation);
                Require(source.TryReadCurrent(out _), "fixture observation missing");
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(!request.IsCurrent && request.Retain() is null && source.CurrentCalls == 0, "observation promoted to writer");
            }),
            ("publication-advance-keeps-same-grant", () =>
            {
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(request.IsCurrent, "initial grant rejected");
                source.Power.PublicationRevision++;
                Require(request.IsCurrent && source.Disposals == 0 && source.Withdrawals == 0, "diagnostic publication cancelled grant");
            }),
            ("pending-and-fresh-ready-do-not-revive-old-owner", () =>
            {
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(request.IsCurrent, "initial grant rejected");
                source.Power.Flags |= 4;
                Require(!request.IsCurrent, "pending admitted writer");
                source.Power = source.Binding.Captured;
                Require(!request.IsCurrent && request.Retain() is null, "old owner revived after Ready");
                using var freshSource = new NativeScopePort();
                using var fresh = new FanRemotePowerScope(freshSource, RemotePowerPurpose.FanWrite);
                Require(fresh.IsCurrent, "new legitimate owner blocked");
            }),
            ("immutable-binding-rejects-new-request-object", () =>
            {
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(request.IsCurrent, "initial grant rejected");
                source.BindingValue.Request.Low++;
                Require(!request.IsCurrent, "new binding lent permission to old operation");
            }),
            ("returned-requires-all-evidence-and-borrowers", () =>
            {
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                using var worker = request.Retain();
                Require(worker is not null, "worker rejected");
                Require(!request.TryCreateSettlement(RemotePowerSettlementOutcome.Returned, RemotePowerSettlementFlags.AllReturned, out _), "active borrower reported returned");
                worker!.Dispose();
                Require(!request.TryCreateSettlement(RemotePowerSettlementOutcome.Returned, RemotePowerSettlementFlags.WorkerStopped, out _), "partial facts reported returned");
                Require(source.Settlements == 0, "invalid return reached receipt factory");
                Require(request.TryCreateSettlement(RemotePowerSettlementOutcome.RetainedResources, RemotePowerSettlementFlags.WorkerStopped, out var retained) && retained.Outcome == RemotePowerSettlementOutcome.RetainedResources, "honest retained receipt rejected");
                Require(request.TryCreateSettlement(RemotePowerSettlementOutcome.Returned, RemotePowerSettlementFlags.AllReturned, out _), "explicit complete return rejected");
                Require(!request.IsCurrent && request.Retain() is null && source.Disposals == 0, "prepared return authorized more work or pretended handles closed");
            }),
            ("foreign-settlement-is-rejected", () =>
            {
                using var source = new NativeScopePort { ForeignReceipt = true };
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(!request.TryCreateSettlement(RemotePowerSettlementOutcome.Returned, RemotePowerSettlementFlags.AllReturned, out _), "foreign request settled this owner");
            }),
            ("read-and-release-exceptions-keep-honest-state", () =>
            {
                using var source = new NativeScopePort { ThrowCurrent = true, ThrowDispose = true };
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                Require(!request.IsCurrent && request.LastFailure is IOException, "reader exception granted operation");
                source.ThrowCurrent = false;
                Require(!request.IsCurrent, "exception revived old operation");
                request.Dispose();
                Require(request.LastFailure is IOException && source.Disposals == 1 && source.WaitHandlesClosed && source.Settlements == 0, "release exception erased debt or invented return");
            }),
            ("current-call-and-dispose-are-serialized", () =>
            {
                using var entered = new ManualResetEventSlim();
                using var release = new ManualResetEventSlim();
                using var disposing = new ManualResetEventSlim();
                using var source = new NativeScopePort();
                using var request = new FanRemotePowerScope(source, RemotePowerPurpose.FanWrite);
                source.BeforeCurrent = () => { entered.Set(); Require(release.Wait(2000), "reader never released"); };
                var read = Task.Run(() => request.IsCurrent);
                Task? close = null;
                try
                {
                    Require(entered.Wait(2000), "reader did not enter");
                    close = Task.Run(() => { disposing.Set(); request.Dispose(); });
                    Require(disposing.Wait(2000), "dispose worker did not start");
                    Require(!close.Wait(30) && source.Disposals == 0, "import closed under active Current");
                }
                finally
                {
                    release.Set();
                    Require(read.Wait(2000), "reader did not finish");
                    if (close is not null) Require(close.Wait(2000), "dispose did not finish");
                }
                Require(source.Disposals == 1 && source.WaitHandlesClosed, "serialized release failed");
            }),
        };
        var selected = cases;
        if (args.Length > 1)
        {
            var suffix = args[1].StartsWith("from:", StringComparison.Ordinal);
            var name = suffix ? args[1][5..] : args[1];
            var index = Array.FindIndex(cases, item => item.Name == name);
            selected = index < 0 ? [] : suffix ? cases[index..] : [cases[index]];
        }
        if (selected.Length == 0) throw new ArgumentException("Unknown explicit scope case");
        var results = new List<object>();
        var passed = true;
        foreach (var item in selected)
        {
            try { item.Run(); results.Add(new { item.Name, pass = true }); }
            catch (Exception error) { results.Add(new { item.Name, pass = false, error = error.ToString() }); passed = false; break; }
        }
        File.WriteAllText(Path.Combine(evidence, "result.json"), JsonSerializer.Serialize(new
        {
            pass = passed, executed = results.Count, selected = selected.Length, results,
            scope = "Actual source-linked Fan scope borrower and public candidate ABI; Native authority/transport is a declared port. No Host engine/STA/device/physical or production importer acceptance."
        }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"remote-scope-only pass={passed} executed={results.Count}");
        if (!passed) Environment.ExitCode = 1;
    }

    // Explicit authority port: real managed wait objects, simulated Native facts,
    // process identity and receipt factory. This is not a production importer.
    private sealed class NativeScopePort : IRemoteNativePowerScope
    {
        private readonly EventWaitHandle revoked = new(false, EventResetMode.ManualReset);
        private readonly EventWaitHandle exited = new(false, EventResetMode.ManualReset);
        internal RemotePowerDescriptorV1 BindingValue;
        internal SharedPowerSnapshotV1 Power;
        internal int Disposals, Withdrawals, Settlements, CurrentCalls;
        internal bool ForeignReceipt, ThrowCurrent, ThrowDispose;
        internal Action? BeforeCurrent;
        internal NativeScopePort(RemotePowerPurpose purpose = RemotePowerPurpose.FanWrite)
        {
            Power = new SharedPowerSnapshotV1 { Size = 48, Version = 1, Generation = 9, PublicationRevision = 1, ObservedNotificationSequence = 7, SuspendSerial = 3, Phase = purpose == RemotePowerPurpose.FanResume ? 3u : 0u, Flags = purpose == RemotePowerPurpose.FanResume ? 1u : 3u };
            BindingValue = new RemotePowerDescriptorV1 { Size = 128, Version = 1, Scope = new() { Low = 11 }, Request = new() { Low = 12 }, Provider = new() { Pid = 111, Birth100ns = 20, Session = 1 }, Consumer = new() { Pid = 222, Birth100ns = 30, Session = 1 }, Purpose = purpose, Captured = Power };
        }
        public RemotePowerDescriptorV1 Binding => BindingValue;
        public WaitHandle Revoked => revoked;
        public WaitHandle ProviderExited => exited;
        internal bool WaitHandlesClosed => revoked.SafeWaitHandle.IsClosed && exited.SafeWaitHandle.IsClosed;
        public bool TryReadCurrent(out SharedPowerSnapshotV1 value) { value = Power; return true; }
        public bool Current(RemotePowerPurpose purpose)
        {
            CurrentCalls++;
            BeforeCurrent?.Invoke();
            if (ThrowCurrent) throw new IOException("explicit reader port failure");
            var captured = BindingValue.Captured;
            return Disposals == 0 && !revoked.WaitOne(0) && !exited.WaitOne(0) && purpose == BindingValue.Purpose &&
                Power.Size == 48 && Power.Version == 1 && (Power.Flags & 1) != 0 && (Power.Flags & 4) == 0 &&
                Power.Generation == captured.Generation && Power.SuspendSerial == captured.SuspendSerial &&
                Power.ObservedNotificationSequence == captured.ObservedNotificationSequence && Power.Phase == captured.Phase &&
                Power.Flags == captured.Flags && Power.PublicationRevision >= captured.PublicationRevision;
        }
        public void CancelLocal() { Withdrawals++; revoked.Set(); }
        public bool TryCreateSettlement(RemotePowerSettlementOutcome outcome, RemotePowerSettlementFlags flags, out RemotePowerSettlementV1 receipt)
        {
            Settlements++;
            receipt = new RemotePowerSettlementV1 { Size = 80, Version = 1, Scope = BindingValue.Scope, Request = BindingValue.Request, Provider = BindingValue.Provider, Consumer = BindingValue.Consumer, Outcome = outcome, Flags = flags };
            if (ForeignReceipt) receipt.Request.High++;
            return true;
        }
        public void Dispose()
        {
            if (Disposals != 0) return;
            Disposals++;
            revoked.Dispose();
            exited.Dispose();
            if (ThrowDispose) throw new IOException("explicit release port failure after local handles closed; no remote ACK");
        }
    }
}
#endif
