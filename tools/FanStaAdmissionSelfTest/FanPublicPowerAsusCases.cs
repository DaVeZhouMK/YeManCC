using System.IO;
using System.Reflection;
using System.Runtime.Loader;

internal static class FanPublicPowerAsusCases
{
    private const BindingFlags Flags = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Call(object target, string name, params object?[] arguments)
    {
        var type = target as Type ?? target.GetType();
        var method = type.GetMethods(Flags).Single(method => method.Name == name && method.GetParameters().Length == arguments.Length);
        try { return method.Invoke(target is Type ? null : target, arguments); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    private static object? Prop(object target, string name) => target.GetType().GetProperty(name, Flags)!.GetValue(target);
    private static Exception? Error(Action action) { try { action(); return null; } catch (Exception error) { return error; } }

    internal static void Run(Assembly host, Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        var names = new[] { "asus-before-first-409", "asus-false-callback", "asus-held-handle-lock", "asus-after-cpu", "asus-after-gpu", "asus-after-mid", "asus-unknown-prefix", "asus-short-prefix", "asus-legacy-entrypoints" };
        if (selection != "asus-all" && !names.Contains(selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown explicit ASUS current case");
        var leaf = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(Path.GetDirectoryName(host.Location)!, "YeManFanBackend.dll"));
        var acpi = leaf.GetType("YeManFanBackend.Asus.AsusACPI", true)!;
        var observations = leaf.GetType("YeManFanBackend.Asus.AsusAcpiObservations", true)!;
        var catalog = leaf.GetType("YeManFanBackend.Capabilities.FanCapabilityCatalog", true)!;
        var originalTransport = acpi.GetProperty("CurrentTransport", Flags)!.GetValue(null)!;
        foreach (var name in names.Where(name => selection == "asus-all" || name == selection))
        {
            var transport = FanAsusTransportPort.Create(leaf);
            object? session = null;
            IDisposable? receiptScope = null;
            Task<Exception?>? pending = null;
            var handleGate = acpi.GetField("handleLock", Flags)!.GetValue(null)!;
            var ownsGate = false;
            using var beforeLock = new ManualResetEventSlim();
            var ok = true;
            void C(string text, bool passed) { ok &= passed; check(name + ": " + text, passed, ""); }
            try
            {
                Call(acpi, "SetTransportForInjection", transport);
                C("only declared transport opens", Call(acpi, "Open") is true && transport.Opens == 1);
                Call(observations, "ClearReceipts");
                session = Call(catalog, "AcquireAsusCurve", "HandheldCompanion.Devices.ROGAlly")!;
                receiptScope = (IDisposable)Call(observations, "BeginScope", name)!;
                var points = Enumerable.Repeat(100d, 11).ToArray();
                var allowed = name is not ("asus-before-first-409" or "asus-false-callback");
                var calls = 0;
                var marker = (Exception)Activator.CreateInstance(host.GetType("YeManFanHost.FanApiException", true)!, Flags, null,
                    new object[] { 409, "FAN_PUBLIC_POWER_SUPERSEDED", "explicit original Host cancellation" }, null)!;
                Func<bool> current = () =>
                {
                    var count = Interlocked.Increment(ref calls);
                    if (name == "asus-held-handle-lock" && count == 2) beforeLock.Set();
                    if (Volatile.Read(ref allowed)) return true;
                    if (name == "asus-false-callback") return false;
                    throw marker;
                };
                var expectedPrefix = name switch { "asus-after-cpu" or "asus-unknown-prefix" or "asus-short-prefix" => 1, "asus-after-gpu" => 2, "asus-after-mid" => 3, "asus-legacy-entrypoints" => 3, _ => 0 };
                if (expectedPrefix > 0 && name != "asus-legacy-entrypoints")
                    transport.AfterIo = count => { if (count == expectedPrefix) Volatile.Write(ref allowed, false); };
                if (name == "asus-unknown-prefix") transport.ThrowAt = 1;
                if (name == "asus-short-prefix") transport.ReturnBytes = 8;
                Exception? error;
                if (name == "asus-held-handle-lock")
                {
                    Monitor.Enter(handleGate); ownsGate = true;
                    var acceptedSession = session;
                    pending = Task.Run(() => Error(() => Call(acceptedSession, "ApplySoftwareCurve", points, current)));
                    if (!beforeLock.Wait(2000)) throw new TimeoutException("outer current checks did not finish");
                    C("actual handle lock prevents I/O after outer checks", !pending.IsCompleted && transport.Packets.Count == 0);
                    Volatile.Write(ref allowed, false);
                    Monitor.Exit(handleGate); ownsGate = false;
                    if (!pending.Wait(2000)) throw new TimeoutException("actual lock waiter did not return");
                    error = pending.Result;
                }
                else if (name == "asus-legacy-entrypoints") error = Error(() => Call(session, "ApplySoftwareCurve", points));
                else error = Error(() => Call(session, "ApplySoftwareCurve", points, current));
                if (name == "asus-legacy-entrypoints")
                    C("original CLR entry still dispatches three channels", error is null && calls == 0);
                else if (name == "asus-false-callback")
                    C("false callback is cancellation before I/O", error is OperationCanceledException);
                else
                    C("original Host 409 exception propagates unchanged", ReferenceEquals(error, marker));
                var receipts = (Array)Call(receiptScope, "PeekReceipts")!;
                C("cancelled suffix never issues another channel", transport.Packets.Count == expectedPrefix && receipts.Length == expectedPrefix);
                var last = Prop(session, "LastResult")!;
                C("session keeps actual receipt prefix", Convert.ToInt32(Prop(last, "ReceiptCount")) == expectedPrefix);
                if (expectedPrefix > 0)
                {
                    var status = Prop(receipts.GetValue(0)!, "Status")!.ToString();
                    C("actual first receipt status remains honest", status == (name == "asus-unknown-prefix" ? "IoException" : name == "asus-short-prefix" ? "TransportedShort" : "TransportedComplete"));
                    if (name == "asus-unknown-prefix") C("uncertain initiation survives public cancellation", Prop(receipts.GetValue(0)!, "InitiatedUncertain") is true && Prop(last, "Outcome")!.ToString() == "Unknown");
                    if (expectedPrefix == 3) C("three returned transports are evidence, not permission", Prop(last, "Outcome")!.ToString() == "CompleteTransport");
                }
                else C("zero I/O stays unknown rather than failure receipt", Prop(last, "Outcome")!.ToString() == "Unknown");
                // Return uses the original no-callback overload even when current
                // is now false. It is no grant to issue a new software command.
                transport.AfterIo = null; transport.ThrowAt = 0; transport.ReturnBytes = 16;
                var beforeReturn = transport.Packets.Count;
                Call(session, "RestoreDefaultCurves");
                C("OEM return is not blocked by cancelled software grant", transport.Packets.Count == beforeReturn + 3 && Prop(Prop(session, "LastResult")!, "Outcome")!.ToString() == "CompleteTransport");
                Call(session, "CloseOwnedHandle");
                C("original owned handle and catalog owner return", transport.Closes == 1 && acpi.GetProperty("IsOpen", Flags)!.GetValue(null) is false && Prop(session, "HandleReleased") is true);
            }
            catch (Exception error) { ok = false; check(name + ": original scenario completed", false, error.ToString()); }
            finally
            {
                if (ownsGate) Monitor.Exit(handleGate);
                var returned = pending is null || pending.Wait(2000);
                C("actual owned waiter returned", returned);
                if (returned)
                {
                    if (session is not null && Prop(session, "HandleReleased") is not true) Call(session, "CloseOwnedHandle");
                    else Call(acpi, "Close");
                    receiptScope?.Dispose();
                    Call(acpi, "SetTransportForInjection", originalTransport);
                }
            }
            scenario(new { name, ok, actualControllerHandleLock = true, transport = "declared fake kernel edge", packets = transport.Packets.Count, physicalWrites = 0 });
            if (!ok) return;
        }
    }
}

public class FanAsusTransportPort : DispatchProxy
{
    private static readonly IntPtr Handle = new(61347);
    public List<byte[]> Packets { get; } = [];
    public int Opens { get; private set; }
    public int Closes { get; private set; }
    public int ThrowAt { get; set; }
    public uint ReturnBytes { get; set; } = 16;
    public Action<int>? AfterIo { get; set; }
    public static FanAsusTransportPort Create(Assembly backend) => (FanAsusTransportPort)DispatchProxy.Create(
        backend.GetType("YeManFanBackend.Asus.IAsusAcpiTransport", true)!, typeof(FanAsusTransportPort));
    protected override object? Invoke(MethodInfo? method, object?[]? arguments)
    {
        switch (method!.Name)
        {
            case "IsHandleValid": return Equals(arguments![0], Handle);
            case "TryOpen": Opens++; arguments![0] = Handle; arguments[1] = 0; return true;
            case "Control":
                if (!Equals(arguments![0], Handle)) throw new InvalidOperationException("foreign kernel handle in test port");
                Packets.Add((byte[])((byte[])arguments[2]!).Clone());
                Array.Clear((byte[])arguments[3]!);
                arguments[4] = ReturnBytes; arguments[5] = 0;
                AfterIo?.Invoke(Packets.Count);
                if (Packets.Count == ThrowAt) throw new IOException("explicit uncertain device-call port");
                return true;
            case "Close":
                if (!Equals(arguments![0], Handle)) throw new InvalidOperationException("foreign close in test port");
                Closes++; return null;
            default: throw new NotSupportedException(method.Name);
        }
    }
}
