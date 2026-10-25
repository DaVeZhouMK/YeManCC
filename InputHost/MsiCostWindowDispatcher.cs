using System;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// One writer for this dispatcher's entire lifetime. A stalled callback retains
// that writer; callers never replace it or wait for it to finish.
internal sealed class MsiCostWindowDispatcher<T> : IDisposable
{
    private sealed class Window
    {
        internal Window(T item) => Item = item;
        internal readonly T Item;
    }

    // The closed marker occupies the same atomic slot used to publish a window.
    // Dispose cannot miss a publisher that checked admission before shutdown.
    private static readonly Window Closed = new(default!);
    private readonly object _gate = new();
    private readonly Action<T> _consume;
    private readonly AutoResetEvent _wake;
    private readonly Thread _worker;
    private Window? _pending;

    // The worker owns one reference until its final exit. Set callers acquire
    // a reference only while the count is positive, so zero cannot be revived.
    // The last reference disposes the event, without a join or a cleanup task.
    private int _wakeReferences = 1;
    private long _accepted, _dropped, _completed, _failed, _discarded;
    private long _droppedBusy, _droppedFull, _droppedClosed;

    internal MsiCostWindowDispatcher(Action<T> consume, string? threadName = null)
    {
        ArgumentNullException.ThrowIfNull(consume);
        _consume = consume;
        _worker = new Thread(WorkerLoop)
        {
            IsBackground = true,
            Name = threadName ?? "YMCC MSI cost window"
        };
        _wake = new AutoResetEvent(false);
        try
        {
            _worker.Start();
        }
        catch
        {
            Interlocked.Exchange(ref _pending, Closed);
            ReleaseWakeReference();
            throw;
        }
    }

    // Advisory rejection at request start. False does not reserve a slot;
    // the normal TryPost admission still applies after snapshot construction.
    internal bool TryRejectBeforeSnapshot()
    {
        var pending = Volatile.Read(ref _pending);
        if (ReferenceEquals(pending, Closed)) { Reject(ref _droppedClosed); return true; }
        if (pending is not null) { Reject(ref _droppedFull); return true; }
        return false;
    }

    internal bool TryPost(T item)
    {
        if (ReferenceEquals(Volatile.Read(ref _pending), Closed))
            return Reject(ref _droppedClosed);
        if (!Monitor.TryEnter(_gate, 0))
        {
            return ReferenceEquals(Volatile.Read(ref _pending), Closed)
                ? Reject(ref _droppedClosed)
                : Reject(ref _droppedBusy);
        }
        try
        {
            var pending = Volatile.Read(ref _pending);
            if (ReferenceEquals(pending, Closed)) return Reject(ref _droppedClosed);
            if (pending is not null) return Reject(ref _droppedFull);

            // Only publishers take the gate. The worker and Dispose can remove
            // this slot concurrently, and Dispose installs an irreversible marker.
            var previous = Interlocked.CompareExchange(ref _pending, new Window(item), null);
            if (previous is not null)
            {
                return ReferenceEquals(previous, Closed)
                    ? Reject(ref _droppedClosed)
                    : Reject(ref _droppedFull);
            }
            Interlocked.Increment(ref _accepted);
            SignalWorker();
            return true;
        }
        finally
        {
            Monitor.Exit(_gate);
        }
    }

    // Completed counts successful callbacks; Failed counts callbacks that threw.
    // Each field is read atomically, but the record is not a transactional snapshot.
    // Stopped reflects actual thread termination, not merely a shutdown request.
    internal readonly record struct Counters(
        long Accepted, long Dropped, long Completed, long Failed,
        long Discarded, bool Stopped,
        long DroppedBusy, long DroppedFull, long DroppedClosed);

    internal Counters SnapshotCounters() => new(
        Interlocked.Read(ref _accepted), Interlocked.Read(ref _dropped),
        Interlocked.Read(ref _completed), Interlocked.Read(ref _failed),
        Interlocked.Read(ref _discarded), !_worker.IsAlive,
        Interlocked.Read(ref _droppedBusy), Interlocked.Read(ref _droppedFull),
        Interlocked.Read(ref _droppedClosed));

    public void Dispose()
    {
        var pending = Interlocked.Exchange(ref _pending, Closed);
        if (ReferenceEquals(pending, Closed)) return;
        if (pending is not null) Interlocked.Increment(ref _discarded);
        SignalWorker();
    }

    private bool Reject(ref long reason)
    {
        Interlocked.Increment(ref reason);
        Interlocked.Increment(ref _dropped);
        return false;
    }

    private void WorkerLoop()
    {
        try
        {
            while (true)
            {
                var window = Volatile.Read(ref _pending);
                if (ReferenceEquals(window, Closed)) return;
                if (window is null)
                {
                    _wake.WaitOne();
                    continue;
                }
                if (!ReferenceEquals(Interlocked.CompareExchange(ref _pending, null, window), window))
                    continue;

                // Claiming the slot starts the sole active job. No gate is held
                // across user code, and a second window can occupy the pending slot.
                try
                {
                    _consume(window.Item);
                    Interlocked.Increment(ref _completed);
                }
                catch (Exception)
                {
                    Interlocked.Increment(ref _failed);
                }
            }
        }
        finally
        {
            var pending = Interlocked.Exchange(ref _pending, Closed);
            if (pending is not null && !ReferenceEquals(pending, Closed))
                Interlocked.Increment(ref _discarded);
            ReleaseWakeReference();
        }
    }

    private void SignalWorker()
    {
        var references = Volatile.Read(ref _wakeReferences);
        while (references != 0)
        {
            var observed = Interlocked.CompareExchange(ref _wakeReferences, references + 1, references);
            if (observed == references)
            {
                try { _wake.Set(); }
                finally { ReleaseWakeReference(); }
                return;
            }
            references = observed;
        }
    }

    private void ReleaseWakeReference()
    {
        if (Interlocked.Decrement(ref _wakeReferences) == 0) _wake.Dispose();
    }
}
