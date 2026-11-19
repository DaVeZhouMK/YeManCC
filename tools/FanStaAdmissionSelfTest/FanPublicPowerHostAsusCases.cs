using System.IO;
using System.Reflection;
using System.Runtime.Loader;

internal static class FanPublicPowerHostAsusCases
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static object? Field(object target, string name) => target.GetType().GetField(name, Flags)!.GetValue(target);
    private static object? Prop(object target, string name) => target.GetType().GetProperty(name, Flags)!.GetValue(target);
    private static object? Call(object target, string name, params object?[] values)
    {
        var type = target as Type ?? target.GetType();
        var method = type.GetMethods(Flags).Single(item => item.Name == name && !item.IsGenericMethod && item.GetParameters().Length >= values.Length && item.GetParameters().Skip(values.Length).All(item => item.IsOptional));
        try { return method.Invoke(target is Type ? null : target, values.Concat(Enumerable.Repeat<object?>(Type.Missing, method.GetParameters().Length - values.Length)).ToArray()); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { throw error.InnerException; }
    }
    private static Exception? Error(Action action) { try { action(); return null; } catch (Exception error) { return error; } }
    private static string Code(Exception? error) => error?.GetType().GetProperty("Code", Flags)?.GetValue(error)?.ToString() ?? "none";

    internal static void Run(Assembly host, Func<object> create, Action<object, Action, string> legacy,
        Action<string, bool, string> check, Action<object> scenario, string selection)
    {
        var names = new[] { "host-asus-current", "host-asus-after-cpu", "host-asus-uncertain-cpu" };
        if (selection != "host-asus-all" && !names.Contains(selection, StringComparer.Ordinal)) throw new ArgumentException("Unknown Host ASUS composition case");
        var leaf = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(Path.GetDirectoryName(host.Location)!, "YeManFanBackend.dll"));
        var acpi = leaf.GetType("YeManFanBackend.Asus.AsusACPI", true)!;
        var originalTransport = acpi.GetProperty("CurrentTransport", Flags)!.GetValue(null)!;
        foreach (var name in names.Where(value => selection == "host-asus-all" || value == selection))
        {
            var backend = create();
            var transport = FanAsusTransportPort.Create(leaf);
            using var source = FanPublicPowerScopePort.Create(host);
            IDisposable? request = null;
            Thread? sta = null;
            var ok = true;
            var actionOnSta = false;
            var actionThread = 0;
            void C(string text, bool value) { ok &= value; check(name + ": " + text, value, ""); }
            try
            {
                Call(acpi, "SetTransportForInjection", transport);
                var route = Call(backend.GetType(), "ResolveRouteForSelfTest", "HandheldCompanion.Devices.ROGAlly");
                backend.GetType().GetField("routeOverrideForSelfTest", Flags)!.SetValue(backend, route);
                legacy(backend, () => { }, "host-asus-original-sta-warmup");
                sta = (Thread)Field(backend, "staThread")!;
                request = (IDisposable)Call(backend, "BindImportedPowerForRequest", source, source.Purpose)!;
                if (name != "host-asus-current")
                    transport.AfterIo = count => { if (count == 1) source.Revoke(); };
                if (name == "host-asus-uncertain-cpu") transport.ThrowAt = 1;
                var profile = new HostAsusProfilePort { FanProfile = new() { fanMode = "Software", fanSpeeds = Enumerable.Repeat(100d, 11).ToArray() } };
                Action action = () =>
                {
                    actionThread = Environment.CurrentManagedThreadId;
                    actionOnSta = Thread.CurrentThread.GetApartmentState() == ApartmentState.STA;
                    Call(backend, "ApplyAsusCurveViaBackend", profile);
                };
                var error = Error(() => Call(backend, "OnStaPowerBounded", action, name));
                request.Dispose(); request = null;
                C("actual Host action runs on original STA", actionOnSta && actionThread == sta.ManagedThreadId);
                var cancelled = name != "host-asus-current";
                C("actual Host error preserves public cancellation", cancelled ? Code(error) == "FAN_PUBLIC_POWER_SUPERSEDED" : error is null);
                var session = Field(backend, "backendFanCapability")!;
                var result = Prop(session, "LastResult")!;
                C("actual wrapper keeps completed channel prefix", transport.Packets.Count == (cancelled ? 1 : 3) && Convert.ToInt32(Prop(result, "ReceiptCount")) == (cancelled ? 1 : 3));
                C("actual session outcome is not washed by outer cancellation", Prop(result, "Outcome")!.ToString() == (name == "host-asus-current" ? "CompleteTransport" : name == "host-asus-uncertain-cpu" ? "Unknown" : "Partial"));
                C("actual STA owner returned without timeout", Field(backend, "activeStaWorkItem") is null && Field(backend, "operationTimedOut") is false);
                C("only request and STA local borrowers returned", source.Disposals == 1 && source.HandlesClosed && source.Settlements == 0);
                transport.AfterIo = null; transport.ThrowAt = 0;
                var count = transport.Packets.Count;
                profile.FanProfile.fanMode = "Hardware";
                legacy(backend, () => Call(backend, "ApplyAsusCurveViaBackend", profile), name + "-original-oem-return");
                C("original Host OEM branch survives retired public scope", transport.Packets.Count == count + 3 && Prop(Prop(session, "LastResult")!, "Outcome")!.ToString() == "CompleteTransport");
            }
            catch (Exception error) { ok = false; check(name + ": original Host composition completed", false, error.ToString()); }
            finally
            {
                request?.Dispose();
                Call(backend, "Dispose");
                C("actual backend returns owned handle and joins STA", transport.Closes == 1 && acpi.GetProperty("IsOpen", Flags)!.GetValue(null) is false && (sta is null || !sta.IsAlive));
                Call(acpi, "SetTransportForInjection", originalTransport);
            }
            scenario(new { name, ok, actualHostWrapper = true, actualSta = true, actualAsusLeaf = true, NativeImporterAndKernelArePorts = true, physicalWrites = 0 });
            if (!ok) return;
        }
    }
}

public sealed class HostAsusProfilePort { public HostAsusFanPort FanProfile { get; set; } = new(); }
public sealed class HostAsusFanPort { public string fanMode { get; set; } = "Software"; public double[] fanSpeeds { get; set; } = []; }
