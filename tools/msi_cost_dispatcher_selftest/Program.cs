using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using YeManInputHost.Diagnostics;

internal static class Program
{
    private static int _checks;
    private static readonly List<object> Results = new();
    private static readonly TimeSpan TestBudget = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan CallerBudget = TimeSpan.FromSeconds(2);

    private static void Check(bool condition, string message)
    {
        Interlocked.Increment(ref _checks);
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void Eventually(Func<bool> condition, string message) =>
        Check(SpinWait.SpinUntil(condition, TestBudget), message);

    private static object Field<T>(MsiCostWindowDispatcher<T> dispatcher, string name) =>
        dispatcher.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(dispatcher)!;

    private static void StoppedAndReleased<T>(MsiCostWindowDispatcher<T> dispatcher)
    {
        Eventually(() => dispatcher.SnapshotCounters().Stopped, "worker actually terminated");
        Eventually(() => (int)Field(dispatcher, "_wakeReferences") == 0, "all wake references released");
        Check(!((Thread)Field(dispatcher, "_worker")).IsAlive, "Stopped is actual thread exit");
        bool disposed = false;
        try { ((AutoResetEvent)Field(dispatcher, "_wake")).WaitOne(0); }
        catch (ObjectDisposedException) { disposed = true; }
        Check(disposed, "worker event disposed");
        var counters = dispatcher.SnapshotCounters();
        Check(counters.Accepted == counters.Completed + counters.Failed + counters.Discarded,
            "settled accepted accounting");
        Check(counters.Dropped == counters.DroppedBusy + counters.DroppedFull + counters.DroppedClosed,
            "settled rejection accounting");
    }

    // A diagnostic caller thread lets the test release fake blockers on failure.
    // These are test budgets, not claims about OS scheduling or hard real time.
    private static double Promptly(Action action, Action releaseOnFailure)
    {
        using var done = new ManualResetEventSlim();
        Exception? error = null;
        double elapsed = 0;
        var caller = new Thread(() =>
        {
            var timer = Stopwatch.StartNew();
            try { action(); }
            catch (Exception ex) { error = ex; }
            finally { elapsed = timer.Elapsed.TotalMilliseconds; done.Set(); }
        }) { IsBackground = true, Name = "dispatcher-test-caller" };
        caller.Start();
        if (!done.Wait(CallerBudget))
        {
            releaseOnFailure();
            caller.Join(TestBudget);
            throw new InvalidOperationException("caller waited for a fake blocker beyond test budget");
        }
        Check(caller.Join(TestBudget), "diagnostic caller ended");
        Check(error is null, "diagnostic caller has no exception: " + error);
        Check(elapsed < CallerBudget.TotalMilliseconds, "caller returned while fake blockers stayed closed");
        return elapsed;
    }

    private static void CapacityAndOrder()
    {
        using var firstEntered = new ManualResetEventSlim();
        using var secondEntered = new ManualResetEventSlim();
        using var releaseFirst = new ManualResetEventSlim();
        using var releaseSecond = new ManualResetEventSlim();
        var calls = new ConcurrentQueue<int>();
        var threadIds = new ConcurrentDictionary<int, byte>();
        int background = 0;
        using var dispatcher = new MsiCostWindowDispatcher<int>(item =>
        {
            calls.Enqueue(item);
            threadIds.TryAdd(Environment.CurrentManagedThreadId, 0);
            if (Thread.CurrentThread.IsBackground) Interlocked.Exchange(ref background, 1);
            if (item == 1) { firstEntered.Set(); releaseFirst.Wait(); }
            if (item == 2) { secondEntered.Set(); releaseSecond.Wait(); }
        }, "test-one-cost-writer");
        try
        {
            Check(dispatcher.TryPost(1), "first job accepted");
            Check(firstEntered.Wait(TestBudget), "first callback blocked");
            Check(dispatcher.TryPost(2), "one pending job accepted while callback blocked");
            Promptly(() =>
            {
                for (int item = 3; item < 2003; item++) Check(!dispatcher.TryPost(item), "full slot rejects immediately");
            }, () => { releaseFirst.Set(); releaseSecond.Set(); });
            Check(calls.SequenceEqual(new[] { 1 }), "full rejections do not execute or replace pending");
            var held = dispatcher.SnapshotCounters();
            Check(held.Accepted == 2 && held.Completed == 0 && held.DroppedFull == 2000,
                "capacity is one active plus one pending");
            Check(!held.Stopped && !releaseFirst.IsSet, "caller returned with worker still blocked");
            releaseFirst.Set();
            Check(secondEntered.Wait(TestBudget), "original pending job executes next");
            Check(dispatcher.TryPost(2003), "slot reusable while second callback active");
            releaseSecond.Set();
            Eventually(() => dispatcher.SnapshotCounters().Completed == 3, "three accepted callbacks complete");
            Check(calls.SequenceEqual(new[] { 1, 2, 2003 }), "rejections never reorder accepted jobs");
            var worker = (Thread)Field(dispatcher, "_worker");
            Check(background == 1 && worker.IsBackground, "dedicated worker is background");
            Check(worker.Name == "test-one-cost-writer", "optional thread name used");
            Check(threadIds.Count == 1 && threadIds.ContainsKey(worker.ManagedThreadId), "only one callback writer thread");
            Check(dispatcher.GetType().GetFields(BindingFlags.Instance | BindingFlags.NonPublic)
                .Count(field => field.FieldType == typeof(Thread)) == 1, "class owns exactly one Thread");
            dispatcher.Dispose();
            StoppedAndReleased(dispatcher);
        }
        finally { releaseFirst.Set(); releaseSecond.Set(); dispatcher.Dispose(); }
    }

    private static void BusyGateAndClose()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var gateEntered = new ManualResetEventSlim();
        using var gateRelease = new ManualResetEventSlim();
        using var dispatcher = new MsiCostWindowDispatcher<int>(_ => { entered.Set(); release.Wait(); });
        var holder = new Thread(() =>
        {
            lock (Field(dispatcher, "_gate")) { gateEntered.Set(); gateRelease.Wait(); }
        }) { IsBackground = true, Name = "test-admission-gate-holder" };
        try
        {
            Check(dispatcher.TryPost(1) && entered.Wait(TestBudget), "fake consumer blocked");
            Check(dispatcher.TryPost(2), "pending installed before gate hold");
            holder.Start();
            Check(gateEntered.Wait(TestBudget), "admission gate held by different thread");
            Promptly(() => Check(!dispatcher.TryPost(3), "busy gate post rejects"), () => { gateRelease.Set(); release.Set(); });
            Check(dispatcher.SnapshotCounters().DroppedBusy == 1, "atomic busy rejection count");
            Promptly(dispatcher.Dispose, () => { gateRelease.Set(); release.Set(); });
            var closed = dispatcher.SnapshotCounters();
            Check(closed.Discarded == 1 && !closed.Stopped, "close discards pending without acquiring busy gate");
            Check(!gateRelease.IsSet && !release.IsSet, "close waited for neither gate nor consumer");
            dispatcher.Dispose();
            Check(!dispatcher.TryPost(4), "closed admission rejects even while gate busy");
            gateRelease.Set(); release.Set();
            Check(holder.Join(TestBudget), "gate holder ended");
            StoppedAndReleased(dispatcher);
        }
        finally
        {
            gateRelease.Set(); release.Set(); dispatcher.Dispose();
            if (holder.IsAlive) holder.Join(TestBudget);
        }
    }

    private static void ExceptionAndReentrantPost()
    {
        var calls = new ConcurrentQueue<int>();
        int reentrantAccepted = 0;
        MsiCostWindowDispatcher<int>? dispatcher = null;
        dispatcher = new MsiCostWindowDispatcher<int>(item =>
        {
            calls.Enqueue(item);
            if (item == 1) throw new InvalidOperationException("fake callback failure");
            if (item == 2 && dispatcher!.TryPost(3)) Interlocked.Exchange(ref reentrantAccepted, 1);
        });
        using (dispatcher)
        {
            Check(dispatcher.TryPost(1), "throwing callback accepted");
            Eventually(() => dispatcher.SnapshotCounters().Failed == 1, "exception counted once");
            Check(dispatcher.TryPost(2), "next job accepted after exception");
            Eventually(() => dispatcher.SnapshotCounters().Completed == 2, "worker continues after callback exception");
            Check(reentrantAccepted == 1, "callback can post without holding admission gate");
            Check(calls.SequenceEqual(new[] { 1, 2, 3 }), "exception preserves subsequent order");
            dispatcher.Dispose();
            StoppedAndReleased(dispatcher);
        }
    }

    private static void BlockedDisposeAndTerminalAdmission()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var calls = new ConcurrentQueue<int>();
        using var dispatcher = new MsiCostWindowDispatcher<int>(item => { calls.Enqueue(item); entered.Set(); release.Wait(); });
        try
        {
            Check(dispatcher.TryPost(1) && entered.Wait(TestBudget), "active callback blocked");
            Check(dispatcher.TryPost(2), "pending callback queued");
            Promptly(dispatcher.Dispose, release.Set);
            var counters = dispatcher.SnapshotCounters();
            Check(counters.Accepted == 2 && counters.Discarded == 1 && counters.Completed == 0,
                "only pending job discarded at close");
            Check(!counters.Stopped && !release.IsSet, "Dispose request does not pretend worker stopped");
            for (int i = 0; i < 512; i++)
            {
                dispatcher.Dispose();
                Check(!dispatcher.TryPost(3), "terminal state never accepts");
            }
            Check(dispatcher.SnapshotCounters().Discarded == 1, "repeated close never double counts discarded");
            Check(dispatcher.SnapshotCounters().DroppedClosed == 512, "all terminal drops counted atomically");
            Check(calls.SequenceEqual(new[] { 1 }), "no new writer or pending callback starts during stall");
            release.Set();
            StoppedAndReleased(dispatcher);
            Check(calls.SequenceEqual(new[] { 1 }), "pending job never runs after close");
        }
        finally { release.Set(); dispatcher.Dispose(); }
    }

    private static void IdleCloseAndNullItem()
    {
        using (var idle = new MsiCostWindowDispatcher<int>(_ => throw new InvalidOperationException("should not execute")))
        {
            idle.Dispose(); idle.Dispose();
            StoppedAndReleased(idle);
            Check(idle.SnapshotCounters().Accepted == 0, "idle close creates no callback");
            Check(!idle.TryPost(1), "idle terminal state rejects");
        }
        int nullCalls = 0;
        using (var nullable = new MsiCostWindowDispatcher<string?>(item => { if (item is null) Interlocked.Increment(ref nullCalls); }))
        {
            Check(nullable.TryPost(null), "null payload is distinct from empty/closed slot");
            Eventually(() => nullable.SnapshotCounters().Completed == 1, "null job consumed");
            Check(nullCalls == 1, "null payload preserved");
            nullable.Dispose();
            StoppedAndReleased(nullable);
        }
        bool rejected = false;
        try { using var invalid = new MsiCostWindowDispatcher<int>(null!); }
        catch (ArgumentNullException) { rejected = true; }
        Check(rejected, "null consumer rejected");
    }

    private static void RaceCallers(MsiCostWindowDispatcher<int> dispatcher, Action releaseOnFailure, int round)
    {
        const int posterCount = 6, closerCount = 2;
        using var start = new ManualResetEventSlim();
        using var done = new CountdownEvent(posterCount + closerCount);
        var errors = new ConcurrentQueue<Exception>();
        var threads = new List<Thread>();
        for (int index = 0; index < posterCount + closerCount; index++)
        {
            int caller = index;
            var thread = new Thread(() =>
            {
                try
                {
                    start.Wait();
                    if (caller < posterCount)
                    {
                        for (int item = 0; item < 128; item++)
                        {
                            dispatcher.TryPost(round * 10000 + caller * 128 + item);
                            if (item % 16 == 0) Thread.Yield();
                        }
                    }
                    else
                    {
                        Thread.Yield();
                        for (int i = 0; i < 16; i++) { dispatcher.Dispose(); Thread.Yield(); }
                    }
                }
                catch (Exception ex) { errors.Enqueue(ex); }
                finally { done.Signal(); }
            }) { IsBackground = true, Name = "test-post-close-racer" };
            threads.Add(thread); thread.Start();
        }
        start.Set();
        if (!done.Wait(TestBudget))
        {
            releaseOnFailure();
            foreach (var thread in threads) thread.Join(TestBudget);
            throw new InvalidOperationException("post/close callers failed to finish within fake-blocked test budget");
        }
        foreach (var thread in threads) Check(thread.Join(TestBudget), "race caller actually ended");
        Check(errors.IsEmpty, "no post/close resource race exception: " + errors.FirstOrDefault());
    }

    private static void ConcurrentCloseWithBlockedCallback()
    {
        for (int round = 0; round < 12; round++)
        {
            using var entered = new ManualResetEventSlim();
            using var release = new ManualResetEventSlim();
            int calls = 0;
            using var dispatcher = new MsiCostWindowDispatcher<int>(_ =>
            {
                Interlocked.Increment(ref calls); entered.Set(); release.Wait();
            });
            try
            {
                Check(dispatcher.TryPost(-1) && entered.Wait(TestBudget), "race callback held");
                Check(dispatcher.TryPost(-2), "race pending slot filled");
                RaceCallers(dispatcher, release.Set, round);
                var counters = dispatcher.SnapshotCounters();
                Check(counters.Accepted == 2 && counters.Discarded == 1 && !counters.Stopped,
                    "concurrent close preserves active/pending bound and actual stop state");
                Check(counters.Dropped == 6 * 128, "every rejected concurrent post counted");
                Check(calls == 1 && !release.IsSet, "all callers returned while consumer remained blocked");
                release.Set();
                StoppedAndReleased(dispatcher);
            }
            finally { release.Set(); dispatcher.Dispose(); }
        }
    }

    private static void ConcurrentPostCloseAndWorkerExit()
    {
        for (int round = 0; round < 64; round++)
        {
            int active = 0, peak = 0;
            var ids = new ConcurrentDictionary<int, byte>();
            using var dispatcher = new MsiCostWindowDispatcher<int>(_ =>
            {
                int count = Interlocked.Increment(ref active);
                int seen;
                do { seen = Volatile.Read(ref peak); }
                while (count > seen && Interlocked.CompareExchange(ref peak, count, seen) != seen);
                ids.TryAdd(Environment.CurrentManagedThreadId, 0);
                Thread.Yield();
                Interlocked.Decrement(ref active);
            });
            Check(dispatcher.TryPost(-1), "initial race window accepted");
            RaceCallers(dispatcher, dispatcher.Dispose, round);
            StoppedAndReleased(dispatcher);
            Check(peak <= 1 && ids.Count <= 1, "race never creates a second writer");
            Check(!dispatcher.TryPost(999999), "post after event release safely rejects");
            dispatcher.Dispose();
        }
    }

    private static void SnapshotAndCloseInsideCallback()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        bool stoppedInsideCallback = true;
        MsiCostWindowDispatcher<int>? dispatcher = null;
        dispatcher = new MsiCostWindowDispatcher<int>(_ =>
        {
            entered.Set(); release.Wait(); dispatcher!.Dispose();
            stoppedInsideCallback = dispatcher.SnapshotCounters().Stopped;
        });
        using (dispatcher)
        {
            try
            {
                Check(dispatcher.TryPost(1) && entered.Wait(TestBudget), "callback waiting before self close");
                Check(dispatcher.TryPost(2), "self-close has one pending job");
                for (int i = 0; i < 1000; i++)
                {
                    var counters = dispatcher.SnapshotCounters();
                    Check(counters.Accepted == 2 && !counters.Stopped, "atomic sampling while callback blocked");
                }
                release.Set();
                StoppedAndReleased(dispatcher);
                Check(!stoppedInsideCallback, "callback self-close never reports its own thread as stopped");
                var final = dispatcher.SnapshotCounters();
                Check(final.Completed == 1 && final.Discarded == 1, "self-close discards pending and completes active job");
            }
            finally { release.Set(); dispatcher.Dispose(); }
        }
    }

    private static void Run(string name, Action test)
    {
        int before = _checks;
        var timer = Stopwatch.StartNew();
        try
        {
            test();
            Results.Add(new { name, status = "PASS", assertions = _checks - before, elapsedMs = timer.Elapsed.TotalMilliseconds });
            Console.WriteLine($"PASS {name} assertions={_checks - before} elapsedMs={timer.Elapsed.TotalMilliseconds:F1}");
        }
        catch (Exception ex)
        {
            Results.Add(new { name, status = "FAIL", assertions = _checks - before, elapsedMs = timer.Elapsed.TotalMilliseconds, error = ex.ToString() });
            throw;
        }
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("usage: selftest <fresh project artifacts/run-directory>"); return 2; }
        string artifacts = Path.GetFullPath("tools/msi_cost_dispatcher_selftest/artifacts") + Path.DirectorySeparatorChar;
        string runDirectory = Path.GetFullPath(args[0]);
        if (!runDirectory.StartsWith(artifacts, StringComparison.OrdinalIgnoreCase) ||
            (Directory.Exists(runDirectory) && Directory.EnumerateFileSystemEntries(runDirectory).Any()))
        {
            Console.Error.WriteLine("run directory must be fresh and inside this test project's artifacts directory"); return 2;
        }
        Directory.CreateDirectory(runDirectory);
        string source = Path.GetFullPath("InputHost/MsiCostWindowDispatcher.cs");
        string sourceSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source)));
        string status = "PASS";
        string? failure = null;
        var total = Stopwatch.StartNew();
        try
        {
            Run("capacity-order-background-single-writer", CapacityAndOrder);
            Run("busy-gate-and-nonblocking-close", BusyGateAndClose);
            Run("exception-continuation-reentrant-post", ExceptionAndReentrantPost);
            Run("blocked-dispose-discard-terminal-admission", BlockedDisposeAndTerminalAdmission);
            Run("idle-close-null-item-null-consumer", IdleCloseAndNullItem);
            Run("concurrent-close-blocked-consumer-12-rounds", ConcurrentCloseWithBlockedCallback);
            Run("concurrent-post-close-worker-exit-64-rounds", ConcurrentPostCloseAndWorkerExit);
            Run("atomic-sampling-callback-self-close", SnapshotAndCloseInsideCallback);
        }
        catch (Exception ex) { status = "FAIL"; failure = ex.ToString(); Console.Error.WriteLine(failure); }
        var receipt = new
        {
            status, capturedAtUtc = DateTimeOffset.UtcNow, runtime = Environment.Version.ToString(),
            scope = "Pure linked dispatcher; fake consumers and test synchronization only; no InputHost/SDK/product/device/pipe/service/registry execution.",
            sourcePath = source, sourceSha256, testCount = Results.Count, assertions = _checks,
            elapsedMs = total.Elapsed.TotalMilliseconds, blockedRaceRounds = 12, exitRaceRounds = 64,
            schedulingBoundary = "Caller budgets are regression checks, not hard real-time or OS scheduling guarantees.",
            tests = Results, failure
        };
        string receiptPath = Path.Combine(runDirectory, "receipt.json");
        File.WriteAllText(receiptPath, JsonSerializer.Serialize(receipt, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"{status} tests={Results.Count} assertions={_checks} sourceSha256={sourceSha256} receipt={receiptPath}");
        return status == "PASS" ? 0 : 1;
    }
}
