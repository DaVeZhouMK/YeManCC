using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Security.Cryptography;
using YeManFanHost;
using YeManFanBackend.Capabilities;

/// <summary>Runs the pinned HC software fan callback against an in-memory ACPI boundary.</summary>
public sealed class HcBinaryAdapter : IDisposable
{
    private const string ExpectedHcSha256 = "0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49";
    private const string ExpectedSentrySha256 = "d55ec4e68438fd515ae059f36c5fea4c5f5db3e610d042c7fcee318b2e0b1ee0";
    private const string ExpectedLhmSha256 = "f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf";
    private readonly object gate = new();
    private readonly IsolatedHcContext context;
    private readonly MemoryAcpiProxy transport;
    private readonly MethodInfo open;
    private readonly MethodInfo close;
    private readonly MethodInfo callback;
    private readonly Type rogType;
    private readonly Type a1Type;
    private readonly Type iDeviceType;
    private readonly Type oneXType;
    private readonly Type profileType;
    private readonly Type fanType;
    private readonly FieldInfo fanMode;
    private readonly FieldInfo fanSpeeds;
    private readonly FieldInfo profileFan;
    private readonly object software;
    private readonly object background;
    private bool disposed;

    public string AssemblyVersion { get; }
    public string Sha256 { get; }
    public IReadOnlyDictionary<string,string> LoadedImageSha256
    {
        get {var result=new Dictionary<string,string>(context.LoadedImageSha256);result["HandheldCompanion"]=Sha256;return result;}
    }
    public bool HandleReleased => !transport.HandleOpen;
    public byte[] DefaultCpuCurve => ReadDefaultCurve("defaultCPUFan");
    public byte[] DefaultGpuCurve => ReadDefaultCurve("defaultGPUFan");
    private byte[] ReadDefaultCurve(string field)
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        return (byte[])((byte[])(rogType.GetField(field, BindingFlags.Static | BindingFlags.NonPublic)!
            .GetValue(null) ?? throw new InvalidDataException("HC default curve is missing"))).Clone();
    }


    public HcBinaryAdapter(string hcDllPath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(hcDllPath);
        string fullPath = Path.GetFullPath(hcDllPath);
        string directory = Path.GetDirectoryName(fullPath)!;
        byte[] hcImage = ReadPinned(fullPath, ExpectedHcSha256);
        ReadPinned(Path.Combine(directory, "Sentry.dll"), ExpectedSentrySha256);
        Sha256 = Convert.ToHexString(SHA256.HashData(hcImage)).ToLowerInvariant();

        // The ALC denies native loading even before the per-assembly resolver can be installed.
        context = new IsolatedHcContext(directory);
        Assembly hc;
        using (var stream = new MemoryStream(hcImage, writable: false))
            hc = context.LoadFromStream(stream);
        context.BlockNativeImports(hc);
        AssemblyVersion = hc.GetName().Version?.ToString()
            ?? throw new InvalidDataException("HC has no assembly version.");

        Type acpi = RequireType(hc, "HandheldCompanion.Devices.ASUS.AsusACPI");
        Type transportInterface = RequireType(hc, "HandheldCompanion.Devices.ASUS.IAsusAcpiTransport");
        transport = (MemoryAcpiProxy)DispatchProxy.Create(transportInterface, typeof(MemoryAcpiProxy));
        Invoke(RequireMethod(acpi, "SetTransportForInjection"), null, transport);
        if (!ReferenceEquals(acpi.GetProperty("CurrentTransport")!.GetValue(null), transport))
            throw new InvalidOperationException("HC did not retain the in-memory ACPI transport.");
        context.ThrowIfNativeImportAttempted();

        // Injection precedes all HC data/device allocation, including enum boxing.
        open = RequireMethod(acpi, "Open");
        close = RequireMethod(acpi, "Close");
        rogType = RequireType(hc, "HandheldCompanion.Devices.ROGAlly");
        a1Type = RequireType(hc, "HandheldCompanion.Devices.AOKZOEA1");
        iDeviceType = RequireType(hc, "HandheldCompanion.Devices.IDevice");
        oneXType = RequireType(hc, "HandheldCompanion.Devices.OneXAOKZOE");
        profileType = RequireType(hc, "HandheldCompanion.Misc.PowerProfile");
        fanType = RequireType(hc, "HandheldCompanion.Misc.FanProfile");
        if (profileType.TypeInitializer is not null || fanType.TypeInitializer is not null)
            throw new InvalidDataException("The audited profile types must have no static constructor.");
        callback = RequireMethod(rogType, "PowerProfileManager_Applied");
        fanMode = RequireField(fanType, "fanMode");
        fanSpeeds = RequireField(fanType, "fanSpeeds");
        profileFan = RequireField(profileType, "<FanProfile>k__BackingField");
        software = Enum.Parse(fanMode.FieldType, "Software");
        background = Enum.Parse(RequireType(hc, "HandheldCompanion.UpdateSource"), "Background");
    }

    // Reflection binding only: no approved HC/LHM device or port constructor is invoked.
    public string ValidatePinnedSourcePortMetadata()
    {
        lock(gate)
        {
            ObjectDisposedException.ThrowIf(disposed,this);
            var hc=context.Assemblies.Single(x=>x.GetName().Name=="HandheldCompanion");
            var reference=hc.GetReferencedAssemblies().Single(x=>x.Name=="LibreHardwareMonitorLib");
            var library=context.LoadFromAssemblyName(reference);
            if(!context.LoadedImageSha256.TryGetValue("LibreHardwareMonitorLib",out var loadedHash)||loadedHash!=ExpectedLhmSha256)
                throw new InvalidDataException("Actual loaded pinned port bytes were not witnessed");
            if(library.GetName().FullName!=reference.FullName)throw new InvalidDataException("Pinned port library identity changed");
            using(var port=new YeManFanHost.ReflectionFanSuperIo(
                library.GetType("LibreHardwareMonitor.PawnIo.LpcIo",true)!,
                library.GetType("LibreHardwareMonitor.Hardware.Motherboard.Lpc.LpcPort",true)!))
            {
                if(port.IsReady)throw new InvalidDataException("Metadata binding unexpectedly acquired a port");
            }
            context.ThrowIfNativeImportAttempted();
            return library.GetName().FullName!;
        }
    }

    // Existing roundtrip's explicit shipping-image branch. Allocate only inert
    // uninitialized data objects; no HC constructor, GetCurrent, Open or manager runs.
    public static object RunShippingOneXContracts(string hcDllPath)
    {
        const string shippingSha = "08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583";
        var image = ReadPinned(hcDllPath, shippingSha);
        var ctx = new IsolatedHcContext(Path.GetDirectoryName(Path.GetFullPath(hcDllPath))!);
        Assembly hc; using (var stream = new MemoryStream(image, false)) hc = ctx.LoadFromStream(stream);
        ctx.BlockNativeImports(hc);
        var checks = new List<string>();
        void Check(bool ok, string name) { if (!ok) throw new InvalidDataException(name); checks.Add(name); }
        var ecWriter = "HandheldCompanion.Devices.OneXAOKZOE";
        var baseDevice = hc.GetType("HandheldCompanion.Devices.IDevice", true)!;
        var field = baseDevice.GetField("ECDetails", BindingFlags.Public | BindingFlags.Instance)!;
        var use = baseDevice.GetField("UseOpenLib", BindingFlags.NonPublic | BindingFlags.Instance)!;
        var opened = baseDevice.GetField("DeviceOpen", BindingFlags.NonPublic | BindingFlags.Instance)!;
        var oneX = hc.GetType(ecWriter, true)!;
        var mode = oneX.GetField("ACPI_FanMode_Address", BindingFlags.NonPublic | BindingFlags.Instance)!;
        var dutyAddress = oneX.GetField("ACPI_FanPWMDutyCycle_Address", BindingFlags.NonPublic | BindingFlags.Instance)!;
        object Details(int maximum, bool empty = false)
        {
            object data = Activator.CreateInstance(field.FieldType)!;
            string[] names = ["AddressFanControl", "AddressFanDuty", "AddressStatusCommandPort", "AddressDataPort", "FanValueMin", "FanValueMax"];
            int[] values = empty ? [0,0,0,0,0,0] : [0x44A,0x44B,0x4E,0x4F,0,maximum];
            for (int i=0;i<names.Length;i++) { var f=field.FieldType.GetField(names[i])!; f.SetValue(data,Convert.ChangeType(values[i],f.FieldType)); }
            return data;
        }
        object Raw(Type type, int maximum, bool empty = false)
        {
            object raw = RuntimeHelpers.GetUninitializedObject(type); field.SetValue(raw, Details(maximum,empty));
            use.SetValue(raw,true); opened.SetValue(raw,false); mode.SetValue(raw,(byte)0xC4); dutyAddress.SetValue(raw,(byte)0xC5); return raw;
        }
        int Max(object raw) => Convert.ToInt32(field.FieldType.GetField("FanValueMax")!.GetValue(field.GetValue(raw)));
        foreach (var name in new[] { "OneXPlayerG1AMD", "OneXPlayerG1Intel" })
        {
            var factory="HandheldCompanion.Devices."+name;var type=hc.GetType(factory,true)!;var expected=G1FanContractAdapter.Expected(factory)!;
            var empty=expected with { ControlAddress=0,DutyAddress=0,CommandPort=0,DataPort=0,Min=0,Max=0 };var raw=Raw(type,0,true);
            Check(G1FanContractAdapter.TryComplete(raw,true,empty,out var completed)&&completed==expected&&Max(raw)==expected.Max,"shipping-g1:actual-memory-completion-"+name);
            var before=field.GetValue(raw);Check(!G1FanContractAdapter.TryComplete(raw,true,empty,out _)&&Equals(before,field.GetValue(raw)),"shipping-g1:filled-object-not-overwritten-"+name);
            var other=Raw(type,0,true);Check(!G1FanContractAdapter.TryComplete(other,false,empty,out _)&&Max(other)==0,"shipping-g1:undeclared-rejected-"+name);
        }
        var miniType=hc.GetType(MiniAmdFanContractAdapter.Factory,true)!;var legacy=MiniAmdFanContractAdapter.Expected with { Max=255 };var mini=Raw(miniType,255);
        Check(MiniAmdFanContractAdapter.TryNormalize(mini,true,legacy,out var corrected)&&corrected==MiniAmdFanContractAdapter.Expected&&Max(mini)==100,"shipping-mini:actual-object-readback100");
        var normalized=field.GetValue(mini);Check(MiniAmdFanContractAdapter.TryNormalize(mini,true,MiniAmdFanContractAdapter.Expected,out _)&&Equals(normalized,field.GetValue(mini)),"shipping-mini:normalized-idempotent");
        var wrong=Raw(miniType,255);dutyAddress.SetValue(wrong,(byte)0x4B);Check(!MiniAmdFanContractAdapter.TryNormalize(wrong,true,legacy,out _)&&Max(wrong)==255,"shipping-mini:wrong-acpi-address-rejected");
        var late=Raw(miniType,255);opened.SetValue(late,true);Check(!MiniAmdFanContractAdapter.TryNormalize(late,true,legacy,out _)&&Max(late)==255,"shipping-mini:late-open-normalization-rejected");
        var undeclared=Raw(miniType,255);Check(!MiniAmdFanContractAdapter.TryNormalize(undeclared,false,legacy,out _)&&Max(undeclared)==255,"shipping-mini:undeclared-unchanged");
        var mismatch=Raw(miniType,255);Check(!MiniAmdFanContractAdapter.TryNormalize(mismatch,true,legacy with { DutyAddress=1 },out _)&&Max(mismatch)==255,"shipping-mini:observed-conflict-unchanged");
        var wrongData=Raw(miniType,255);var data=field.GetValue(wrongData)!;field.FieldType.GetField("AddressFanDuty")!.SetValue(data,(ushort)1);field.SetValue(wrongData,data);
        Check(!MiniAmdFanContractAdapter.TryNormalize(wrongData,true,legacy,out _)&&Max(wrongData)==255,"shipping-mini:actual-field-conflict-unchanged");
        List<int> Terminal(Type parent, object sourceDetails)
        {
            var builder=new PersistedAssemblyBuilder(new AssemblyName("OneXMiniTerminal-"+Guid.NewGuid().ToString("N")),typeof(object).Assembly);
            var module=builder.DefineDynamicModule("OneXMiniTerminal");var tagged=module.DefineType("OneXMiniTerminal.Inert",TypeAttributes.Public|TypeAttributes.Class,parent);
            var writes=tagged.DefineField("Writes",typeof(List<int>),FieldAttributes.Public|FieldAttributes.Static);
            var boundary=FindEcWriteByte(parent)??throw new InvalidDataException("Mini EC virtual terminal missing");
            var method=tagged.DefineMethod("EcWriteByte",MethodAttributes.Family|MethodAttributes.Virtual|MethodAttributes.HideBySig,typeof(void),[typeof(byte),typeof(byte)]);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldsfld,writes);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Ldc_I4_8);il.Emit(OpCodes.Shl);il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Or);il.Emit(OpCodes.Callvirt,typeof(List<int>).GetMethod("Add")!);il.Emit(OpCodes.Ret);tagged.DefineMethodOverride(method,boundary);_=tagged.CreateType();
            using var stream=new MemoryStream();builder.Save(stream);stream.Position=0;var assembly=ctx.LoadFromStream(stream);ctx.BlockNativeImports(assembly);var derived=assembly.GetType("OneXMiniTerminal.Inert",true)!;
            var terminal=new List<int>();derived.GetField("Writes")!.SetValue(null,terminal);var raw=Raw(derived,255);field.SetValue(raw,sourceDetails);opened.SetValue(raw,true);
            var setDuty=parent.GetMethod("SetFanDuty",[typeof(double)])!;foreach(var percent in new[]{0d,50d,100d})Invoke(setDuty,raw,percent);
            Check(!MiniAmdFanContractAdapter.TryNormalize(raw,true,legacy,out _),"shipping-mini:derived-terminal-never-admitted-"+parent.Name);ctx.ThrowIfNativeImportAttempted();return terminal;
        }
        var correctedWrites=Terminal(miniType,field.GetValue(mini)!);Check(correctedWrites.SequenceEqual(new[]{0xC500,0xC532,0xC564}),"shipping-mini:actual-inherited-writer-C5-0-50-100");
        var legacyWrites=Terminal(miniType,Details(255));Check(legacyWrites.SequenceEqual(new[]{0xC500,0xC57F,0xC5FF}),"shipping-mini:old255-vector-reproduced");
        foreach(var name in new[]{"OneXPlayerMiniIntel","OneXPlayerMiniPro"})
        {
            var type=hc.GetType("HandheldCompanion.Devices."+name,true)!;Check(MiniAmdFanContractAdapter.CompletionField(type) is null,"shipping-mini:other-model-metadata-rejected-"+name);
            Check(Terminal(type,Details(255)).SequenceEqual(new[]{0xC500,0xC57F,0xC5FF}),"shipping-mini:other-model255-writer-retained-"+name);
        }
        Check(!MiniAmdFanContractAdapter.IsFactory("HandheldCompanion.Devices.OneXPlayerX2MiniPro")&&FanCapabilityCatalog.FindPinnedFanSource("HandheldCompanion.Devices.OneXPlayerX2MiniPro")!.Maximum==255,"shipping-mini:X2MiniPro-independent255-retained");
        ctx.ThrowIfNativeImportAttempted();Check(!ctx.NativeImportAttempted,"shipping-onex:no-native-import");
        return new { status="passed-actual-shipping-one-x-contract",hcSha256=shippingSha,checks,correctedWrites,legacyWrites,
            loadedImageSha256=ctx.LoadedImageSha256,hcConstructorsInvoked=false,portConstructorsInvoked=false,
            instanceAllocation="uninitialized data only; actual HC constructors and lifecycle never entered",actualInheritedMiniDutyInvoked=true,
            ecTerminal="inert override; no OpenLibSys/PawnIO",hostMainInvoked=false,physicalAcceptance=false };
    }

    public IReadOnlyList<byte[]> ApplySoftware(double[] points)
    {
        ArgumentNullException.ThrowIfNull(points);
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            transport.Begin();
            try
            {
                if (Invoke(open, null) is not true)
                    throw new InvalidOperationException("The in-memory HC ACPI handle did not open.");
                object rog = RuntimeHelpers.GetUninitializedObject(rogType);
                object profile = RuntimeHelpers.GetUninitializedObject(profileType);
                object fan = RuntimeHelpers.GetUninitializedObject(fanType);
                fanMode.SetValue(fan, software);
                fanSpeeds.SetValue(fan, (double[])points.Clone());
                profileFan.SetValue(profile, fan);
                Invoke(callback, rog, profile, background);
                context.ThrowIfNativeImportAttempted();
                transport.ThrowIfFaulted();
                if (transport.Packets.Count != 3)
                    throw new InvalidDataException($"HC callback produced {transport.Packets.Count} packets; expected three.");
                return transport.Packets.Select(packet => (byte[])packet.Clone()).ToArray();
            }
            finally
            {
                try
                {
                    Invoke(close, null);
                    transport.ThrowIfFaulted();
                    context.ThrowIfNativeImportAttempted();
                    if (transport.HandleOpen)
                        throw new InvalidOperationException("The in-memory HC ACPI handle was not closed.");
                }
                finally { transport.End(); }
            }
        }
    }

    public sealed class A1TerminalReceipt
    {
        public required bool ConstructorExecuted { get; init; }
        public required string ConstructorNote { get; init; }
        public required bool BaseIsA1 { get; init; }
        public required string EcDetailsJson { get; init; }
        public required bool UseOpenLib { get; init; }
        public required byte AcpiMode { get; init; }
        public required byte AcpiDuty { get; init; }
        public required IReadOnlyList<string> WritesHex { get; init; }
        public required int WriteCountBeforeGuard { get; init; }
        public required int WriteCountAfterClosedGuard { get; init; }
        public required bool NativeImportAttempted { get; init; }
        public required bool StaticOpenLibNull { get; init; }
    }

    /// <summary>
    /// Fixture-only derived A1 type. It overrides the genuine pinned protected virtual
    /// IDevice.EcWriteByte terminal and calls the real inherited AOKZOEA1 setters. The
    /// AOKZOEA1 constructor is not executed because IDevice..ctor loads native
    /// GamepadMotion.dll; the instance is uninitialized and its constructor data is set
    /// from the pinned A1 source values. No Host route admission, no ASUS seam, no
    /// OpenLibSys/PawnIO or hardware access.
    /// </summary>
    public A1TerminalReceipt ApplyA1Terminal(double[] duties)
    {
        ArgumentNullException.ThrowIfNull(duties);
        if (duties.Any(d => !double.IsFinite(d) || d < 0 || d > 100))
            throw new ArgumentOutOfRangeException(nameof(duties), "Duties must be finite and 0..100.");
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            var builder = new PersistedAssemblyBuilder(
                new AssemblyName("A1Terminal-" + Guid.NewGuid().ToString("N")), typeof(object).Assembly);
            var module = builder.DefineDynamicModule("A1Terminal");
            var tagged = module.DefineType("A1Terminal.A1Derived", TypeAttributes.Public | TypeAttributes.Class, a1Type);
            var writes = tagged.DefineField("Writes", typeof(List<int>), FieldAttributes.Public | FieldAttributes.Static);
            var ecBoundary = FindEcWriteByte(a1Type)
                ?? throw new InvalidDataException("IDevice.EcWriteByte is unavailable");
            var method = tagged.DefineMethod("EcWriteByte",
                MethodAttributes.Family | MethodAttributes.Virtual | MethodAttributes.HideBySig,
                typeof(void), new[] { typeof(byte), typeof(byte) });
            var il = method.GetILGenerator();
            il.Emit(OpCodes.Ldsfld, writes);
            il.Emit(OpCodes.Ldarg_1);
            il.Emit(OpCodes.Ldc_I4_8);
            il.Emit(OpCodes.Shl);
            il.Emit(OpCodes.Ldarg_2);
            il.Emit(OpCodes.Or);
            il.Emit(OpCodes.Callvirt, typeof(List<int>).GetMethod("Add")!);
            il.Emit(OpCodes.Ret);
            tagged.DefineMethodOverride(method, ecBoundary);
            _ = tagged.CreateType();
            using var stream = new MemoryStream();
            builder.Save(stream);
            stream.Position = 0;
            Assembly dynamicAssembly = context.LoadFromStream(stream);
            context.BlockNativeImports(dynamicAssembly);
            Type derived = dynamicAssembly.GetType("A1Terminal.A1Derived", throwOnError: true)!;
            derived.GetField("Writes")!.SetValue(null, new List<int>());

            object instance = RuntimeHelpers.GetUninitializedObject(derived);
            var ecField = iDeviceType.GetField("ECDetails", BindingFlags.Public | BindingFlags.Instance)
                ?? throw new InvalidDataException("ECDetails field missing");
            object ec = Activator.CreateInstance(ecField.FieldType)!;
            void SetEc(string name, object value)
            {
                var field = ecField.FieldType.GetField(name) ?? throw new InvalidDataException(name + " missing");
                field.SetValue(ec, Convert.ChangeType(value, field.FieldType));
            }
            SetEc("AddressFanControl", (ushort)0x44A);
            SetEc("AddressFanDuty", (ushort)0x44B);
            SetEc("AddressStatusCommandPort", (ushort)0x4E);
            SetEc("AddressDataPort", (ushort)0x4F);
            SetEc("FanValueMin", (short)0);
            SetEc("FanValueMax", (short)255);
            ecField.SetValue(instance, ec);
            var useOpenLib = iDeviceType.GetField("UseOpenLib", BindingFlags.NonPublic | BindingFlags.Instance)
                ?? throw new InvalidDataException("UseOpenLib field missing");
            var deviceOpen = iDeviceType.GetField("DeviceOpen", BindingFlags.NonPublic | BindingFlags.Instance)
                ?? throw new InvalidDataException("DeviceOpen field missing");
            var acpiModeField = oneXType.GetField("ACPI_FanMode_Address", BindingFlags.NonPublic | BindingFlags.Instance)
                ?? throw new InvalidDataException("ACPI mode field missing");
            var acpiDutyField = oneXType.GetField("ACPI_FanPWMDutyCycle_Address", BindingFlags.NonPublic | BindingFlags.Instance)
                ?? throw new InvalidDataException("ACPI duty field missing");
            useOpenLib.SetValue(instance, true);
            deviceOpen.SetValue(instance, true);
            acpiModeField.SetValue(instance, (byte)0x4A);
            acpiDutyField.SetValue(instance, (byte)0x4B);
            MethodInfo setControl = a1Type.GetMethod("SetFanControl", new[] { typeof(bool), typeof(int) })!;
            MethodInfo setDuty = a1Type.GetMethod("SetFanDuty", new[] { typeof(double) })!;
            setControl.Invoke(instance, new object[] { true, 0 });
            setControl.Invoke(instance, new object[] { false, 0 });
            foreach (var duty in duties) setDuty.Invoke(instance, new object[] { duty });
            List<int> recorded = (List<int>)derived.GetField("Writes")!.GetValue(null)!;
            var beforeGuard = recorded.Count;
            deviceOpen.SetValue(instance, false);
            setDuty.Invoke(instance, new object[] { 50d });
            var afterGuard = recorded.Count;
            context.ThrowIfNativeImportAttempted();
            var staticOpenLib = iDeviceType.GetField("openLibSys", BindingFlags.NonPublic | BindingFlags.Static)!;
            return new A1TerminalReceipt
            {
                ConstructorExecuted = false,
                ConstructorNote = "IDevice..ctor loads native GamepadMotion.dll; uninitialized derived A1 instance with pinned A1 constructor data.",
                BaseIsA1 = ReferenceEquals(derived.BaseType, a1Type),
                EcDetailsJson = System.Text.Json.JsonSerializer.Serialize(ec, ecField.FieldType),
                UseOpenLib = (bool)useOpenLib.GetValue(instance)!,
                AcpiMode = (byte)acpiModeField.GetValue(instance)!,
                AcpiDuty = (byte)acpiDutyField.GetValue(instance)!,
                WritesHex = recorded.Select(v => $"{(v >> 8) & 0xFF:X2}:{v & 0xFF:X2}").ToArray(),
                WriteCountBeforeGuard = beforeGuard,
                WriteCountAfterClosedGuard = afterGuard,
                NativeImportAttempted = context.NativeImportAttempted,
                StaticOpenLibNull = staticOpenLib.GetValue(null) is null,
            };
        }
    }

    public sealed class A1ExactDeviceReceipt
    {
        public required bool ConstructorExecuted { get; init; }
        public required string ConstructorNote { get; init; }
        public required string TypeFullName { get; init; }
        public required bool ExactTypeIsPinnedA1 { get; init; }
        public required bool BaseIsOneXAOKZOE { get; init; }
        public required bool UseOpenLib { get; init; }
        public required bool DeviceOpen { get; init; }
        public required bool IsOpen { get; init; }
        public required byte AcpiMode { get; init; }
        public required byte AcpiDuty { get; init; }
        public required string EcDetailsJson { get; init; }
        public required bool StaticOpenLibNull { get; init; }
        public required string NativeImportTracking { get; init; }
    }

    public sealed class A1ExactPostState
    {
        public required bool UseOpenLib { get; init; }
        public required bool DeviceOpen { get; init; }
        public required bool IsOpen { get; init; }
        public required bool StaticOpenLibNull { get; init; }
    }

    /// <summary>
    /// Exact pinned AOKZOEA1 instance (no derived type, no FullName spoof) created without its
    /// constructor because IDevice..ctor loads native GamepadMotion.dll. Only source-derived
    /// constructor data is set and the instance stays closed (DeviceOpen=false), so the selected
    /// Host negative paths cannot reach an EC write even if one were called.
    /// </summary>
    public object CreateA1ExactDevice(out A1ExactDeviceReceipt receipt)
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            object instance = RuntimeHelpers.GetUninitializedObject(a1Type);
            var ecField = iDeviceType.GetField("ECDetails", BindingFlags.Public | BindingFlags.Instance)
                ?? throw new InvalidDataException("ECDetails field missing");
            object ec = Activator.CreateInstance(ecField.FieldType)!;
            void SetEc(string name, object value)
            {
                var field = ecField.FieldType.GetField(name) ?? throw new InvalidDataException(name + " missing");
                field.SetValue(ec, Convert.ChangeType(value, field.FieldType));
            }
            SetEc("AddressFanControl", (ushort)0x44A);
            SetEc("AddressFanDuty", (ushort)0x44B);
            SetEc("AddressStatusCommandPort", (ushort)0x4E);
            SetEc("AddressDataPort", (ushort)0x4F);
            SetEc("FanValueMin", (short)0);
            SetEc("FanValueMax", (short)255);
            ecField.SetValue(instance, ec);
            var useOpenLib = RequireField(iDeviceType, "UseOpenLib");
            var deviceOpen = RequireField(iDeviceType, "DeviceOpen");
            var acpiModeField = RequireField(oneXType, "ACPI_FanMode_Address");
            var acpiDutyField = RequireField(oneXType, "ACPI_FanPWMDutyCycle_Address");
            useOpenLib.SetValue(instance, true);
            deviceOpen.SetValue(instance, false);
            acpiModeField.SetValue(instance, (byte)0x4A);
            acpiDutyField.SetValue(instance, (byte)0x4B);
            var staticOpenLib = iDeviceType.GetField("openLibSys", BindingFlags.NonPublic | BindingFlags.Static)!;
            receipt = new A1ExactDeviceReceipt
            {
                ConstructorExecuted = false,
                ConstructorNote = "IDevice..ctor loads native GamepadMotion.dll; exact pinned A1 instance is uninitialized and only source-derived fields are set.",
                TypeFullName = a1Type.FullName ?? string.Empty,
                ExactTypeIsPinnedA1 = ReferenceEquals(instance.GetType(), a1Type),
                BaseIsOneXAOKZOE = ReferenceEquals(a1Type.BaseType, oneXType),
                UseOpenLib = (bool)useOpenLib.GetValue(instance)!,
                DeviceOpen = (bool)deviceOpen.GetValue(instance)!,
                IsOpen = (bool)(a1Type.GetProperty("IsOpen")!.GetValue(instance) ?? false),
                AcpiMode = (byte)acpiModeField.GetValue(instance)!,
                AcpiDuty = (byte)acpiDutyField.GetValue(instance)!,
                EcDetailsJson = System.Text.Json.JsonSerializer.Serialize(ec, ecField.FieldType),
                StaticOpenLibNull = staticOpenLib.GetValue(null) is null,
                NativeImportTracking = "not-instrumented-for-pinned-hc-assembly; no A1 EC method is invoked on the selected negative paths",
            };
            return instance;
        }
    }

    public A1ExactPostState ReadA1ExactPostState(object instance)
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(disposed, this);
            ArgumentNullException.ThrowIfNull(instance);
            if (!ReferenceEquals(instance.GetType(), a1Type))
                throw new InvalidDataException("Post-state target is not the exact pinned A1 type.");
            var useOpenLib = RequireField(iDeviceType, "UseOpenLib");
            var deviceOpen = RequireField(iDeviceType, "DeviceOpen");
            var staticOpenLib = iDeviceType.GetField("openLibSys", BindingFlags.NonPublic | BindingFlags.Static)!;
            return new A1ExactPostState
            {
                UseOpenLib = (bool)useOpenLib.GetValue(instance)!,
                DeviceOpen = (bool)deviceOpen.GetValue(instance)!,
                IsOpen = (bool)(a1Type.GetProperty("IsOpen")!.GetValue(instance) ?? false),
                StaticOpenLibNull = staticOpenLib.GetValue(null) is null,
            };
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            try { Invoke(close, null); }
            finally { transport.End(); }
            // Retain the rejecting proxy: null would restore HC's real KernelTransport.
            // This fixture uses a process-local ALC; disposing never runs HC device cleanup.
        }
    }

    private static byte[] ReadPinned(string path, string expected)
    {
        byte[] image = File.ReadAllBytes(path);
        if (!string.Equals(Convert.ToHexString(SHA256.HashData(image)), expected, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Unreviewed binary: {path}");
        return image;
    }

    private static Type RequireType(Assembly assembly, string name) => assembly.GetType(name, throwOnError: true)!;
    private static MethodInfo? FindEcWriteByte(Type type)
    {
        for (var current = type; current is not null; current = current.BaseType)
        {
            var method = current.GetMethod("EcWriteByte",
                BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly,
                null, new[] { typeof(byte), typeof(byte) }, null);
            if (method is not null) return method;
        }
        return null;
    }
    private static MethodInfo RequireMethod(Type type, string name) =>
        type.GetMethod(name, BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance)
        ?? throw new MissingMethodException(type.FullName, name);
    private static FieldInfo RequireField(Type type, string name) =>
        type.GetField(name, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
        ?? throw new MissingFieldException(type.FullName, name);
    private static object? Invoke(MethodInfo method, object? target, params object?[] arguments)
    {
        try { return method.Invoke(target, arguments); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        {
            ExceptionDispatchInfo.Capture(error.InnerException).Throw();
            throw;
        }
    }

    private sealed class IsolatedHcContext : AssemblyLoadContext
    {
        private readonly string directory;
        private readonly HashSet<string> framework;
        private Exception? nativeImportFailure;
        private readonly Dictionary<string,string> loadedImageSha256=new(StringComparer.Ordinal);
        public IReadOnlyDictionary<string,string> LoadedImageSha256=>new Dictionary<string,string>(loadedImageSha256);

        public IsolatedHcContext(string directory) : base("fan-hc-binary-" + Guid.NewGuid().ToString("N"), isCollectible: false)
        {
            this.directory = directory;
            string trusted = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? string.Empty;
            framework = trusted.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Where(path => path.StartsWith(RuntimeEnvironment.GetRuntimeDirectory(), StringComparison.OrdinalIgnoreCase)
                    || path.Contains(Path.DirectorySeparatorChar + "Microsoft.WindowsDesktop.App" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                .Select(path => Path.GetFileNameWithoutExtension(path)).ToHashSet(StringComparer.OrdinalIgnoreCase);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            if (AssemblyName.ReferenceMatchesDefinition(name, typeof(HcBinaryAdapter).Assembly.GetName()))
                return typeof(HcBinaryAdapter).Assembly;
            if (name.Name is not null && framework.Contains(name.Name))
                return Default.LoadFromAssemblyName(name);
            string simple = name.Name ?? throw new FileLoadException("Dependency has no simple name.");
            if (simple != Path.GetFileName(simple) || simple.Contains(':'))
                throw new FileLoadException("Invalid dependency name: " + simple);
            string path = Path.Combine(directory, simple + ".dll");
            if (!File.Exists(path))
                throw new FileNotFoundException("HC dependency is not in its specified directory.", path);
            byte[] image = string.Equals(simple, "Sentry", StringComparison.OrdinalIgnoreCase)
                ? ReadPinned(path, ExpectedSentrySha256) : File.ReadAllBytes(path);
            using var stream = new MemoryStream(image, writable: false);
            Assembly dependency = LoadFromStream(stream);
            loadedImageSha256[simple]=Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant();
            BlockNativeImports(dependency);
            return dependency;
        }

        public void BlockNativeImports(Assembly assembly) => NativeLibrary.SetDllImportResolver(assembly,
            (name, _, _) => DenyNative(name));
        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName) => DenyNative(unmanagedDllName);
        private IntPtr DenyNative(string name)
        {
            var error = new DllNotFoundException("Native imports are blocked in the HC fan fixture: " + name);
            nativeImportFailure ??= error;
            throw error;
        }
        public void ThrowIfNativeImportAttempted()
        {
            if (nativeImportFailure is not null) ExceptionDispatchInfo.Capture(nativeImportFailure).Throw();
        }
        public bool NativeImportAttempted => nativeImportFailure is not null;
    }

    // Public and non-sealed so DispatchProxy can derive its in-memory implementation.
    public class MemoryAcpiProxy : DispatchProxy
    {
        private static readonly IntPtr FakeHandle = new(0x5150);
        private bool active;
        private Exception? failure;
        internal bool HandleOpen { get; private set; }
        internal List<byte[]> Packets { get; } = new();
        internal void Begin()
        {
            if (active || HandleOpen) throw new InvalidOperationException("The previous fake ACPI session is still active.");
            Packets.Clear();
            failure = null;
            active = true;
        }
        internal void End() { active = false; }
        internal void ThrowIfFaulted()
        {
            if (failure is not null) ExceptionDispatchInfo.Capture(failure).Throw();
        }
        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            try
            {
                if (targetMethod is null || args is null) throw new InvalidOperationException("Invalid transport dispatch.");
                switch (targetMethod.Name)
                {
                    case "IsHandleValid":
                        return active && HandleOpen && (IntPtr)args[0]! == FakeHandle;
                    case "TryOpen":
                        if (!active || HandleOpen) throw new InvalidOperationException("Unexpected fake handle open.");
                        HandleOpen = true;
                        args[0] = FakeHandle;
                        args[1] = 0;
                        return true;
                    case "Control":
                        if (!active || !HandleOpen || (IntPtr)args[0]! != FakeHandle)
                            throw new InvalidOperationException("Control requires the current fake handle.");
                        var input = (byte[])args[2]!;
                        var output = (byte[])args[3]!;
                        if ((uint)args[1]! != 0x0022240Cu || input.Length != 28 || output.Length != 16)
                            throw new InvalidDataException("Unexpected HC fan control packet shape.");
                        Packets.Add((byte[])input.Clone());
                        Array.Clear(output, 0, output.Length);
                        args[4] = checked((uint)output.Length);
                        args[5] = 0;
                        return true;
                    case "Close":
                        if (!HandleOpen || (IntPtr)args[0]! != FakeHandle)
                            throw new InvalidOperationException("Unexpected fake handle close.");
                        HandleOpen = false;
                        return null;
                    default:
                        throw new MissingMethodException("Unreviewed ACPI transport member: " + targetMethod.Name);
                }
            }
            catch (Exception error)
            {
                failure ??= error;
                throw;
            }
        }
    }
}


