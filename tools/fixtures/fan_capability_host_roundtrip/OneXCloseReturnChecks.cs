using System.Reflection;
using System.IO;
using System.Reflection.Emit;
using System.Runtime.ExceptionServices;
using YeManFanHost;

// Inert transport/type-shape fixture only. The actual Host and lifecycle source
// execute; this does not claim execution of HC's native EC packet writer.
public static class OneXCloseFixtureIo
{
    public static readonly List<(ushort Register, byte Value)> Writes = new();
    public static int Disposals, DisposeFailures;
    public static bool WriteResult = true;
    public static Exception? WriteFailure;
    public static Action? AfterWrite;
    public static bool Initialize(object _) => true;
    public static void Dispose(object _)
    {
        Disposals++;
        if (DisposeFailures-- > 0) throw new IOException("onex-fixture-dispose-failed");
    }
    public static bool Write(object device, ushort register, object details, byte value)
    {
        Writes.Add((register, value));
        AfterWrite?.Invoke();
        if (WriteFailure is not null) throw WriteFailure;
        return WriteResult;
    }
    public static void Forbidden() => throw new InvalidOperationException("general-device-lifecycle-forbidden");
    public static void Reset()
    { Writes.Clear(); Disposals = DisposeFailures = 0; WriteResult = true; WriteFailure = null; AfterWrite = null; }
}

public sealed class OneXCloseFixtureProfile
{
    public OneXCloseFixtureFan FanProfile = new();
}
public sealed class OneXCloseFixtureFan
{
    public object fanMode = "Hardware";
    public double[] fanSpeeds = new double[11];
}
public sealed class OneXCloseFixturePowerManager
{
    public string Status => "None";
    public object CloneProfile(object source)
    {
        var fan = ((OneXCloseFixtureProfile)source).FanProfile;
        return new OneXCloseFixtureProfile { FanProfile = new() { fanMode = fan.fanMode, fanSpeeds = (double[])fan.fanSpeeds.Clone() } };
    }
}
public sealed class OneXCloseFixtureDeviceManager { public string Status => "None"; }

internal static partial class FanCapabilityRoundtrip
{
    private const BindingFlags OneXFields = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
    private static Assembly? oneXFixtureAssembly;
    private static Assembly OneXFixtureAssembly()
    {
        if (oneXFixtureAssembly is not null) return oneXFixtureAssembly;
        var image = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("OneXCloseInertShape"), AssemblyBuilderAccess.Run);
        var module = image.DefineDynamicModule("types");
        TypeBuilder Class(string name, Type? parent = null)
        {
            var type = module.DefineType(name, TypeAttributes.Public | TypeAttributes.Class, parent ?? typeof(object));
            type.DefineDefaultConstructor(MethodAttributes.Public); return type;
        }
        void Method(TypeBuilder type, string name, Type result, Type[] args, Action<ILGenerator> body)
        { var il = type.DefineMethod(name, MethodAttributes.Public, result, args).GetILGenerator(); body(il); il.Emit(OpCodes.Ret); }
        var update = module.DefineEnum("HandheldCompanion.UpdateSource", TypeAttributes.Public, typeof(int)); update.DefineLiteral("Background", 0); var updateType = update.CreateTypeInfo()!.AsType();
        var fanMode = module.DefineEnum("HandheldCompanion.Misc.FanMode", TypeAttributes.Public, typeof(int));
        fanMode.DefineLiteral("Hardware", 0); fanMode.DefineLiteral("Software", 1); fanMode.CreateTypeInfo();
        var factory = Class("HandheldCompanion.Managers.ManagerFactory");
        factory.DefineField("powerProfileManager", typeof(OneXCloseFixturePowerManager), FieldAttributes.Public | FieldAttributes.Static);
        factory.DefineField("Managers", typeof(object[]), FieldAttributes.Public | FieldAttributes.Static);
        var factoryType = factory.CreateType()!;
        var powerManager = new OneXCloseFixturePowerManager();
        factoryType.GetField("powerProfileManager")!.SetValue(null, powerManager);
        factoryType.GetField("Managers")!.SetValue(null, new object[] { powerManager, new OneXCloseFixtureDeviceManager() });
        var profile = Class("HandheldCompanion.Misc.PowerProfile").CreateType()!;
        var ec = Class("HandheldCompanion.EcDetails");
        foreach (var name in new[] { "AddressFanControl", "AddressFanDuty", "AddressStatusCommandPort", "AddressDataPort", "FanValueMin", "FanValueMax" }) ec.DefineField(name, typeof(int), FieldAttributes.Public);
        var ecType = ec.CreateType()!;
        var transport = Class("HandheldCompanion.OpenLibSys");
        Method(transport, "InitializeOls", typeof(bool), [], il => { il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Initialize")!); });
        Method(transport, "Dispose", typeof(void), [], il => { il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Dispose")!); });
        var transportType = transport.CreateType()!;
        var root = Class("HandheldCompanion.Devices.IDevice");
        var opened = root.DefineField("DeviceOpen", typeof(bool), FieldAttributes.Public);
        root.DefineField("UseOpenLib", typeof(bool), FieldAttributes.Public);
        root.DefineField("ECDetails", ecType, FieldAttributes.Public);
        root.DefineField("openLibSys", transportType, FieldAttributes.Private | FieldAttributes.Static);
        var getter = root.DefineMethod("get_IsOpen", MethodAttributes.Public | MethodAttributes.SpecialName, typeof(bool), []);
        var gi = getter.GetILGenerator(); gi.Emit(OpCodes.Ldarg_0); gi.Emit(OpCodes.Ldfld, opened); gi.Emit(OpCodes.Ret);
        root.DefineProperty("IsOpen", PropertyAttributes.None, typeof(bool), []).SetGetMethod(getter);
        Method(root, "OpenEvents", typeof(void), [], _ => { });
        Method(root, "PowerProfileManager_Applied", typeof(void), [profile, updateType], il => il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Forbidden")!));
        Method(root, "ECRamDirectWriteByte", typeof(bool), [typeof(ushort), ecType, typeof(byte)], il =>
        {
            il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Ldarg_2); il.Emit(OpCodes.Ldarg_3);
            il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Write")!);
        });
        var rootType = root.CreateType()!;
        var parent = Class("HandheldCompanion.Devices.OneXAOKZOE", rootType);
        Method(parent, "SetFanControl", typeof(void), [typeof(bool), typeof(int)], il => il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Forbidden")!));
        Method(parent, "SetFanDuty", typeof(void), [typeof(double)], il => il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Forbidden")!));
        var x1 = Class("HandheldCompanion.Devices.OneXPlayerX1", parent.CreateType()!);
        x1.DefineField("_serialPort", typeof(object), FieldAttributes.Public); x1.DefineField("_vendorHidMonitor", typeof(object), FieldAttributes.Public);
        Method(x1, "Open", typeof(bool), [], il => { il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Forbidden")!); il.Emit(OpCodes.Ldc_I4_0); });
        Method(x1, "Close", typeof(void), [], il => il.Emit(OpCodes.Call, typeof(OneXCloseFixtureIo).GetMethod("Forbidden")!));
        var x1Type = x1.CreateType()!;
        var amd = Class("HandheldCompanion.Devices.OneXPlayerX1AMD", x1Type).CreateType()!;
        foreach (var name in new[] { "OneXPlayerX1Intel", "OneXPlayerX1Mini" }) Class("HandheldCompanion.Devices." + name, x1Type).CreateType();
        Class("HandheldCompanion.Devices.OneXPlayerX1Pro", amd).CreateType();
        Class("HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex", amd).CreateType();
        return oneXFixtureAssembly = image;
    }

    private static (RealHcBackend Host, OneXFanDeviceLifecycle Life, object Device) OneXCloseSession(string model)
    {
        OneXCloseFixtureIo.Reset();
        var image = OneXFixtureAssembly();
        var device = Activator.CreateInstance(image.GetType(model, true)!)!;
        var root = image.GetType("HandheldCompanion.Devices.IDevice", true)!;
        root.GetField("UseOpenLib")!.SetValue(device, true);
        var ec = Activator.CreateInstance(root.GetField("ECDetails")!.FieldType)!;
        var names = new[] { "AddressFanControl", "AddressFanDuty", "AddressStatusCommandPort", "AddressDataPort", "FanValueMin", "FanValueMax" };
        var values = new[] { 0x44a, 0x44b, 0x4e, 0x4f, 0, 184 };
        for (var i = 0; i < names.Length; i++) ec.GetType().GetField(names[i])!.SetValue(ec, values[i]);
        root.GetField("ECDetails")!.SetValue(device, ec);
        var life = new OneXFanDeviceLifecycle(device); Require(life.Open(() => true), "onex-close-owned-open:" + model);
        var host = new RealHcBackend(HostOptions.Parse(Array.Empty<string>()));
        host.InstallSelfTestRouteSessionForSelfTest(device, RealHcBackend.FanRoutesForSelfTest[model], true);
        typeof(RealHcBackend).GetField("assembly", OneXFields)!.SetValue(host, image);
        typeof(RealHcBackend).GetField("oneXFanLifecycle", OneXFields)!.SetValue(host, life);
        typeof(RealHcBackend).GetField("hcProfileTemplate", OneXFields)!.SetValue(host, new OneXCloseFixtureProfile());
        return (host, life, device);
    }
    private static void OneXApply(RealHcBackend host, bool software)
    {
        var profile = new OneXCloseFixtureProfile { FanProfile = new() { fanMode = software ? "Software" : "Hardware", fanSpeeds = (double[])Original.Clone() } };
        try { typeof(RealHcBackend).GetMethod("ApplyPowerProfile", OneXFields)!.Invoke(host, [profile, "Background", null, null]); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); }
    }
    private static bool OneXFailed(Action action)
    { try { action(); return false; } catch (Exception) { return true; } }
    private static void OneXClose(RealHcBackend host)
    { host.Close(false); }
    private static void RunOneXCloseReturnChecks()
    {
        var models = new[] { "HandheldCompanion.Devices.OneXPlayerX1AMD", "HandheldCompanion.Devices.OneXPlayerX1Intel", "HandheldCompanion.Devices.OneXPlayerX1Mini", "HandheldCompanion.Devices.OneXPlayerX1Pro", "HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex" };
        foreach (var model in models)
        {
            var (host, life, _) = OneXCloseSession(model);
            try
            {
                OneXApply(host, true); host.BlockWritesForClose();
                Require(OneXFailed(() => OneXApply(host, true)) && OneXFailed(() => life.SetDuty(100, () => false)), "onex-close-ordinary-writes-still-rejected:" + model);
                Require(OneXFailed(() => OneXApply(host, false)), "onex-close-late-unscoped-F4-still-rejected:" + model);
                host.RestoreOem();
                Require(OneXCloseFixtureIo.Writes.SequenceEqual(new[] { ((ushort)0x44a, (byte)1), ((ushort)0x44a, (byte)0) }), "onex-close-F4-after-revoke:" + model);
                OneXClose(host);
                Require(host.OemRestoreVerified && FanHostEngine.IsOemReleaseSafe(true, host.OemReleaseActionKind), "onex-close-current-bool-settles-host:" + model);
                Require(host.OemOwnershipStatus == "onex-ec-returned-physical-unknown" && !life.HasResources && OneXCloseFixtureIo.Disposals == 1, "onex-close-release-cleanup-physical-separated:" + model);
                OneXClose(host);
                Require(host.OemRestoreVerified && OneXCloseFixtureIo.Writes.Count == 3, "onex-close-duplicate-no-new-proof-or-write:" + model);
            }
            finally { host.Dispose(); }
        }
        foreach (var mode in new[] { "false", "throw", "superseded", "foreign", "dispose", "cycle", "device" })
        {
            var (host, life, device) = OneXCloseSession(models[2]);
            var root = device.GetType().Assembly.GetType("HandheldCompanion.Devices.IDevice", true)!;
            var field = root.GetField("openLibSys", OneXFields)!; var owned = field.GetValue(null);
            try
            {
                host.BlockWritesForClose();
                if (mode == "false") OneXCloseFixtureIo.WriteResult = false;
                if (mode == "throw") OneXCloseFixtureIo.WriteFailure = new IOException("onex-fixture-write-unknown");
                if (mode == "superseded") OneXCloseFixtureIo.AfterWrite = host.BlockWritesForSuspend;
                if (mode == "foreign") field.SetValue(null, Activator.CreateInstance(field.FieldType));
                if (mode == "dispose") OneXCloseFixtureIo.DisposeFailures = 1;
                if (mode == "cycle") OneXCloseFixtureIo.AfterWrite = () => host.ControlCycleId++;
                if (mode == "device") OneXCloseFixtureIo.AfterWrite = () => host.SwapDeviceForSelfTest(new object());
                Require(OneXFailed(() => OneXClose(host)), "onex-close-failure-preserved:" + mode);
                Require(life.HasResources, "onex-close-failure-retains-owned-debt:" + mode);
                if (mode == "dispose") Require(host.OemRestoreVerified, "onex-close-dispose-does-not-erase-real-return");
                else Require(!host.OemRestoreVerified && !FanHostEngine.IsOemReleaseSafe(true, host.OemReleaseActionKind), "onex-close-failure-not-success:" + mode);
                if (mode == "foreign") Require(OneXCloseFixtureIo.Writes.Count == 0 && OneXCloseFixtureIo.Disposals == 0, "onex-close-foreign-owner-no-io");
                var writes = OneXCloseFixtureIo.Writes.Count;
                OneXCloseFixtureIo.WriteResult = true; OneXCloseFixtureIo.WriteFailure = null; OneXCloseFixtureIo.AfterWrite = null;
                field.SetValue(null, owned); if (mode == "cycle") host.ControlCycleId--;
                if (mode == "device") host.SwapDeviceForSelfTest(device);
                OneXClose(host);
                Require(host.OemRestoreVerified && !life.HasResources, "onex-close-current-owner-retry-settles:" + mode);
                if (mode == "dispose") Require(OneXCloseFixtureIo.Writes.Count == writes, "onex-close-dispose-retry-never-replays-release");
            }
            finally { host.Dispose(); }
        }
        var (partial, partialLife, _) = OneXCloseSession(models[2]);
        try
        {
            typeof(RealHcBackend).GetField("sessionDeviceReference", OneXFields)!.SetValue(partial, null);
            OneXClose(partial);
            Require(!partialLife.HasResources && partial.OemRestoreVerified,
                "onex-close-partial-open-owns-its-cleanup-without-published-session-marker");
        }
        finally { partial.Dispose(); }
        var (wrong, wrongLife, wrongDevice) = OneXCloseSession(models[2]);
        try
        {
            wrong.SwapDeviceForSelfTest(new object());
            Require(OneXFailed(() => OneXClose(wrong)) && OneXCloseFixtureIo.Writes.Count == 0 && wrongLife.HasResources,
                "onex-close-wrong-device-owner-refused-before-io");
            wrong.SwapDeviceForSelfTest(wrongDevice); OneXClose(wrong);
        }
        finally { wrong.Dispose(); }
    }
}
