using System.Reflection;

// The pinned HC OneX X1 driver returns before SerialQueue writes finish. Keep
// only this helper's requested transaction alive, and observe every queued Task.
// Does not load HC, open a port, alter its protocol or add a guessed delay.
internal static class OneXQueuedLighting
{
    internal sealed record Outcome(string Note, bool Managed, bool Completed, int WriteTasks, int FailedTasks);
    private const string OneXBase = "HandheldCompanion.Devices.OneXPlayerX1";
    private const string QueueType = "HandheldCompanion.Misc.Threading.Tasks.SerialQueue";

    internal static bool UsesQueue(Type type, bool preset = false)
    {
        // Mini disables the inherited serial port; its color/brightness use HID.
        for (Type? current = type; current is not null; current = current.BaseType)
            if (current.FullName == "HandheldCompanion.Devices.OneXPlayerX1Mini") return preset;
        for (Type? current = type; current is not null; current = current.BaseType)
            if (current.FullName == OneXBase) return true;
        return false;
    }

    private static FieldInfo Field(Type type, string name)
    {
        for (Type? current = type; current is not null; current = current.BaseType)
        {
            var field = current.GetField(name, BindingFlags.Instance | BindingFlags.Public |
                BindingFlags.NonPublic | BindingFlags.DeclaredOnly);
            if (field is not null) return field;
        }
        throw new MissingFieldException(type.FullName, name);
    }

    internal static void PrepareRequestCache(object device, string action, int? brightness, string? status)
    {
        // A new one-shot HC object starts at 0/false. Those are defaults, not
        // device readback. Force only explicitly requested serial operations;
        // the HC methods still compose and enqueue every real protocol packet.
        var type = device.GetType();
        if (!UsesQueue(type)) return; // Mini brightness uses HID, not this cache.
        var writesBrightness = action is "brightness" or "off" || brightness is not null;
        var writesStatus = writesBrightness || action == "status";
        if (!writesStatus) return;
        var enabled = action == "status" ? !string.Equals(status, "off", StringComparison.OrdinalIgnoreCase)
            : action != "off" && Math.Clamp(brightness ?? 50, 0, 100) > 0;
        var enabledField = Field(type, "LEDEnabled");
        var brightnessField = writesBrightness ? Field(type, "LEDBrightness") : null;
        if (enabledField.DeclaringType?.FullName != OneXBase || enabledField.FieldType != typeof(bool) ||
            (writesBrightness && (brightnessField?.DeclaringType?.FullName != OneXBase || brightnessField.FieldType != typeof(int))))
            throw new InvalidOperationException("Unreviewed OneX requested-state cache contract");
        if (writesBrightness) brightnessField!.SetValue(device, -1);
        enabledField.SetValue(device, !enabled);
    }

    internal static void InvalidatePresetCache(object device)
    {
        // HC's cached preset can come from settings, without a device readback.
        // This one-shot transaction must actually issue the requested preset.
        Field(device.GetType(), "LEDPreset").SetValue(device, null);
    }

    internal static Outcome Execute(object device, Func<string> action, int timeoutMs = 5000, bool preset = false)
    {
        var type = device.GetType();
        if (!UsesQueue(type, preset)) return new(action(), false, false, 0, 0);
        if (timeoutMs <= 0) throw new ArgumentOutOfRangeException(nameof(timeoutMs));
        var serial = Field(type, "_serialPort").GetValue(device);
        if (Field(type, "EnableSerialPort").GetValue(device) is not true || serial is null ||
            serial.GetType().GetProperty("IsOpen")?.GetValue(serial) is not true)
            return new("onex-serial-port-unavailable", true, false, 0, 0);
        var queue = Field(type, "_queue").GetValue(device)
            ?? throw new InvalidOperationException("OneX serial queue is null");
        if (queue.GetType().FullName != QueueType)
            throw new NotSupportedException("Unreviewed OneX serial queue type");
        var enqueue = queue.GetType().GetMethod("Enqueue", BindingFlags.Public | BindingFlags.Instance,
            null, [typeof(Action)], null);
        if (enqueue is null || !typeof(Task).IsAssignableFrom(enqueue.ReturnType))
            throw new MissingMethodException(QueueType, "Enqueue(Action) -> Task");
        var last = Field(queue.GetType(), "_lastTask").GetValue(queue) as WeakReference<Task?>
            ?? throw new InvalidOperationException("Unreviewed OneX serial queue tail");

        // Strongly retain a harmless predecessor. All writes made by the root
        // action now use HC's ContinueWith overload and this scoped scheduler.
        // It observes earlier write faults even if HC's later continuation succeeds.
        var seed = enqueue.Invoke(queue, [new Action(() => { })]) as Task
            ?? throw new InvalidOperationException("OneX queue did not return a seed Task");
        var scheduler = new WriteScheduler();
        var root = new Task<string>(action);
        scheduler.Root = root;
        root.RunSynchronously(scheduler); // reflection/action itself stays on the caller's STA
        if (!last.TryGetTarget(out var tail) || tail is null)
            throw new InvalidOperationException("OneX queue tail disappeared");
        var completed = false;
        try { completed = tail.Wait(timeoutMs); }
        catch (AggregateException) { completed = true; }
        GC.KeepAlive(seed);
        var writes = scheduler.Writes;
        if (!completed) return new("onex-write-queue-timeout", true, false, writes.Length, writes.Count(t => t.IsFaulted || t.IsCanceled));
        // Waiting for the tail proves all predecessors terminated; it does not
        // prove they succeeded, so inspect EACH captured write separately.
        var failed = writes.Count(t => t.IsFaulted || t.IsCanceled);
        foreach (var task in writes.Where(t => t.IsFaulted)) _ = task.Exception;
        if (failed != 0) return new("onex-write-queue-failed", true, true, writes.Length, failed);
        var note = root.GetAwaiter().GetResult();
        if (note == "ok" && writes.Length == 0) note = "onex-no-request-write-observed";
        return new(note, true, true, writes.Length, 0);
    }

    private sealed class WriteScheduler : TaskScheduler
    {
        private readonly object gate = new();
        private readonly HashSet<Task> writes = [];
        internal Task? Root;
        internal Task[] Writes { get { lock (gate) return writes.ToArray(); } }
        protected override IEnumerable<Task>? GetScheduledTasks() => Writes;
        protected override void QueueTask(Task task)
        {
            lock (gate) writes.Add(task);
            ThreadPool.UnsafeQueueUserWorkItem(_ => TryExecuteTask(task), null);
        }
        protected override bool TryExecuteTaskInline(Task task, bool taskWasPreviouslyQueued)
        {
            // Run only the root action inline. Port writes remain queued work,
            // preserving their FIFO dependencies without blocking the STA on a write.
            return ReferenceEquals(task, Root) && TryExecuteTask(task);
        }
    }
}
