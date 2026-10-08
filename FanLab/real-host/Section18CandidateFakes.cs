using System.Collections;
using System.IO;
using System.Reflection;
using System.Linq.Expressions;

namespace YeManFanHost;

/** Host 侧的风扇目标（与候选 AsusFan 枚举按名称映射，不引用候选类型）。 */
internal enum AsusFanTarget { Cpu, Gpu, Mid }

/** 注入的 ACPI 传输行为模式（只在 OS 边界模拟）。 */
internal enum CandidateAcpiMode
{
    /// <summary>全部成功且返回 16 字节。</summary>
    Complete16,
    /// <summary>全部成功但只返回 4 字节（短响应，语义 unknown）。</summary>
    ShortResponse,
    /// <summary>全部 I/O 失败（win32=5）。</summary>
    IoFailure,
    /// <summary>仅 DEVS 写失败（win32=5），DSTS 读正常。</summary>
    IoFailureWrites,
    /// <summary>§19-C 部分扇失败：仅 GPU 曲线写（0x00110025）失败，CPU/Mid 正常。</summary>
    IoFailureGpuWrite,
    /// <summary>句柄无效：调用未发起。</summary>
    InvalidHandle,
    /// <summary>传输调用抛异常：无法证明是否发起。</summary>
    TransportException,
    /// <summary>读回返回 HC 默认曲线（供生产 readback 确认），写入正常。</summary>
    MatchDefaults
}

/** 候选 DLL 的反射桥（Host 不引用候选类型，全部按名解析）。 */
internal sealed class CandidateReflection
{
    private const BindingFlags AllInstance = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    private readonly Assembly assembly;
    private readonly Type rogAllyType;
    private readonly Type boundaryInterface;
    private readonly Type boundaryResourceInterface;
    private readonly Type lifecycleType;
    private readonly Type hidDeviceInterface;
    private readonly Type hidBindingInterface;
    private readonly Type probeStruct;
    private readonly Type reportStruct;
    private readonly Type acpiType;
    private readonly Type observationsType;
    private readonly Type fanEnum;

    public CandidateReflection(Assembly assembly)
    {
        this.assembly = assembly;
        rogAllyType = Require("HandheldCompanion.Devices.ROGAlly");
        boundaryInterface = Require("HandheldCompanion.Devices.ASUS.IHcRogAllyDeviceBoundary");
        boundaryResourceInterface = Require("HandheldCompanion.Devices.ASUS.IHcRogAllyDeviceResourceBoundary");
        lifecycleType = Require("HandheldCompanion.Devices.ASUS.RogAllyDeviceLifecycle");
        hidDeviceInterface = Require("HandheldCompanion.Devices.ASUS.IHcRogAllyHidDevice");
        hidBindingInterface = Require("HandheldCompanion.Devices.ASUS.IHcRogAllyHidBinding");
        probeStruct = Require("HandheldCompanion.Devices.ASUS.HcRogAllyReadyProbe");
        reportStruct = Require("HandheldCompanion.Devices.ASUS.HcRogAllyHidReport");
        acpiType = Require("HandheldCompanion.Devices.ASUS.AsusACPI");
        observationsType = Require("HandheldCompanion.Devices.ASUS.AsusAcpiObservations");
        fanEnum = assembly.GetType("AsusFan", throwOnError: true)!;
    }

    public string ModuleVersionId => assembly.ManifestModule.ModuleVersionId.ToString("N");

    private Type Require(string fullName) =>
        assembly.GetType(fullName, throwOnError: true)
        ?? throw new InvalidOperationException($"candidate type missing: {fullName}");

    // ---------------- 注入 ----------------

    /** 安装边界替身：ROGAlly.DeviceBoundaryFactory（生产默认是真实 HidDevice/OS 边界）。 */
    public void InstallBoundaryFactory(object boundaryProxy)
    {
        var field = rogAllyType.GetField("DeviceBoundaryFactory", BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingFieldException(rogAllyType.FullName, "DeviceBoundaryFactory");
        var delegateType = typeof(Func<,>).MakeGenericType(rogAllyType, boundaryInterface);
        var parameter = Expression.Parameter(rogAllyType, "device");
        var body = Expression.Convert(Expression.Constant(boundaryProxy), boundaryInterface);
        field.SetValue(null, Expression.Lambda(delegateType, body, parameter).Compile());
    }

    // ---------------- 构造/调用 ----------------

    public object CreateRogAlly() =>
        Activator.CreateInstance(rogAllyType) ?? throw new InvalidOperationException("candidate ROGAlly could not be constructed");

    public object? CallStatic(string typeName, string methodName, params object?[] args) =>
        InvokeMatching(assembly.GetType(typeName, throwOnError: true)!, null, methodName, args);

    public object? CallInstance(object target, string methodName, params object?[] args) =>
        InvokeMatching(target.GetType(), target, methodName, args);

    private static object? InvokeMatching(Type type, object? target, string methodName, object?[] args)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
        var method = type.GetMethods(flags)
            .Where(m => m.Name == methodName && m.GetParameters().Length == args.Length)
            .Where(m => target is null ? m.IsStatic : !m.IsStatic)
            .OrderByDescending(m => m.DeclaringType == type)
            .FirstOrDefault()
            ?? throw new MissingMethodException(type.FullName, methodName);
        return method.Invoke(target, args);
    }

    public string? GetString(object target, string member) => GetMember(target, member)?.ToString();

    public bool GetBool(object target, string member) => GetMember(target, member) is true;

    public int GetInt(object target, string member) => GetMember(target, member) is int value ? value : 0;

    public long GetLong(object target, string member) => GetMember(target, member) switch
    {
        long value => value,
        int value => value,
        _ => 0L,
    };

    public int GetIntStatic(string typeName, string member) =>
        GetStaticMember(assembly.GetType(typeName, throwOnError: true)!, member) is int value ? value : 0;

    public bool GetBoolStatic(string typeName, string member) =>
        GetStaticMember(assembly.GetType(typeName, throwOnError: true)!, member) is true;

    public byte[]? GetStaticBytes(string typeName, string member) =>
        GetStaticMember(assembly.GetType(typeName, throwOnError: true)!, member) as byte[];

    private static object? GetMember(object target, string member)
    {
        for (var type = target.GetType(); type is not null; type = type.BaseType)
        {
            var property = type.GetProperty(member, AllInstance | BindingFlags.DeclaredOnly);
            if (property is not null) return property.GetValue(target);
            var field = type.GetField(member, AllInstance | BindingFlags.DeclaredOnly);
            if (field is not null) return field.GetValue(target);
        }
        // 非声明层的属性/字段（例如继承来的）兜底查询。
        var anyProperty = target.GetType().GetProperty(member, AllInstance);
        if (anyProperty is not null) return anyProperty.GetValue(target);
        var anyField = target.GetType().GetField(member, AllInstance);
        return anyField?.GetValue(target);
    }

    private static object? GetStaticMember(Type type, string member)
    {
        const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        for (var current = type; current is not null; current = current.BaseType)
        {
            var property = current.GetProperty(member, flags | BindingFlags.DeclaredOnly);
            if (property is not null) return property.GetValue(null);
            var field = current.GetField(member, flags | BindingFlags.DeclaredOnly);
            if (field is not null) return field.GetValue(null);
        }
        return null;
    }

    /** 汇总 HC 绑定表状态（诊断用）：hidId、IsOpen 与身份。 */
    public string DescribeBindingTable(object hcDevice)
    {
        var table = GetHcHidDeviceTable(hcDevice);
        var parts = new List<string>();
        foreach (DictionaryEntry entry in table)
        {
            var device = entry.Value;
            var isOpen = device?.GetType().GetProperty("IsOpen")?.GetValue(device) ?? "<no-IsOpen>";
            var path = device?.GetType().GetProperty("DevicePath")?.GetValue(device) ?? "<no-path>";
            parts.Add($"0x{(int)entry.Key:X2}:IsOpen={isOpen}:path={path}");
        }
        return parts.Count == 0 ? "<empty>" : string.Join(",", parts);
    }

    /** 汇总候选保留的全部操作收据（诊断用；不改动状态）。 */
    public string DescribeAllReceipts(object device)
    {
        var lifecycle = GetMember(device, "Lifecycle")
            ?? throw new InvalidOperationException("candidate lifecycle instance not reachable");
        var peek = InvokeMatching(lifecycle.GetType(), lifecycle, "PeekReceipts", Array.Empty<object?>());
        if (peek is not IEnumerable items)
            return "<no-receipts>";
        var parts = new List<string>();
        foreach (var item in items)
        {
            if (item is null) continue;
            parts.Add($"{GetString(item, "OperationId")}:{GetString(item, "Outcome")}:gen{GetLong(item, "SessionGeneration")}:merged={GetBool(item, "MergedDuplicateArrival")}");
        }
        return parts.Count == 0 ? "<none>" : string.Join(",", parts);
    }

    public string Describe(object receipt)
    {
        var method = receipt.GetType().GetMethod("Describe", BindingFlags.Public | BindingFlags.Instance);
        if (method?.Invoke(receipt, null) is { } text)
            return text.ToString()!;
        return GetString(receipt, "Describe") ?? receipt.ToString() ?? "<receipt>";
    }

    /** 读取任意成员值（§19 组读取 Close 收据字段/清理报告用）。 */
    public object? GetMemberValue(object target, string member) => GetMember(target, member);

    /** 候选设备内部的生命周期实例（会话归属查询的唯一入口；不复制管理逻辑）。 */
    private object LifecycleOf(object device) =>
        GetMember(device, "Lifecycle") ?? throw new InvalidOperationException("candidate lifecycle instance not reachable");

    public bool GetSessionValid(object device) => GetMember(LifecycleOf(device), "SessionValid") is true;

    public long GetCurrentGeneration(object device) => GetLong(LifecycleOf(device), "CurrentGeneration");

    /** §19：候选是否提供结构化清理/资源归属能力（资源接口类型存在）。 */
    public string BoundaryResourceApi => boundaryResourceInterface.FullName ?? "<none>";

    public string[] GetClosePendingLoops(object closeReceipt) => StringList(closeReceipt, "PendingReadLoops");

    public string[] GetClosePendingCalls(object closeReceipt) => StringList(closeReceipt, "PendingDeviceCalls");

    public string[] GetClosePendingReleases(object closeReceipt) => StringList(closeReceipt, "PendingReleases");

    public string[] GetCloseCleanupPending(object closeReceipt) => StringList(closeReceipt, "CleanupPending");

    private static string[] StringList(object target, string member) =>
        GetMember(target, member) is IEnumerable items
            ? items.Cast<object?>().Select(item => item?.ToString() ?? string.Empty).ToArray()
            : Array.Empty<string>();

    // ---------------- 生命周期接口 ----------------

    public string StartDeviceOpen(object device, bool reScan) =>
        CallInstance(device, "StartDeviceOpen", reScan) as string
        ?? throw new InvalidOperationException("StartDeviceOpen returned no operation id");

    public object? WaitOpenReceipt(object device, string operationId, int timeoutMs)
    {
        var method = device.GetType()
            .GetMethods(AllInstance)
            .Where(m => m.Name == "TryWaitDeviceOpenReceipt")
            .Where(m => m.GetParameters() is [{ ParameterType: var p0 }, { ParameterType: var p1 }, { ParameterType: var p2 }]
                        && p0 == typeof(string) && p1 == typeof(int) && p2.IsByRef)
            .FirstOrDefault() ?? throw new MissingMethodException("TryWaitDeviceOpenReceipt");
        var args = new object?[] { operationId, timeoutMs, null };
        return method.Invoke(device, args) is true ? args[2] : null;
    }

    public object CloseDeviceLifecycle(object device) =>
        CallInstance(device, "CloseDeviceLifecycle")
        ?? throw new InvalidOperationException("CloseDeviceLifecycle returned nothing");

    public bool WaitReadLoopEnded(object device, int timeoutMs, out string detail)
    {
        var args = new object?[] { timeoutMs, null };
        var ended = CallInstance(device, "TryWaitReadLoopEnded", args) is true;
        detail = args[1]?.ToString() ?? "<none>";
        return ended;
    }

    /** 兼容事件入口（protected override Device_Inserted(bool)）——与生产派发同一入口。 */
    public void DispatchDeviceInserted(object device)
    {
        var method = device.GetType().GetMethods(AllInstance)
            .Where(m => m.Name == "Device_Inserted")
            .Where(m => m.GetParameters() is [{ ParameterType: var type }] && type == typeof(bool))
            .OrderByDescending(m => m.DeclaringType == device.GetType())
            .FirstOrDefault() ?? throw new MissingMethodException("Device_Inserted(bool)");
        method.Invoke(device, [true]);
    }

    public void HandleDeviceRemoved(object device, object hidDeviceProxy)
    {
        var lifecycle = GetMember(device, "Lifecycle")
            ?? throw new InvalidOperationException("candidate lifecycle instance not reachable");
        CallInstance(lifecycle, "HandleDeviceRemoved", hidDeviceProxy);
    }

    /**
     * 订阅候选设备的 KeyPressed（真实 HC 行为：读取循环派发按键时触发）。
     * Host 侧不复制按键分发逻辑，只观察事件是否真的发生。
     */
    public void SubscribeKeyPressed(object device, Action onKey)
    {
        var evt = device.GetType().GetEvent("KeyPressed", BindingFlags.Public | BindingFlags.Instance)
            ?? throw new MissingMemberException("IDevice.KeyPressed");
        var handlerType = evt.EventHandlerType!;
        var parameters = handlerType.GetMethod("Invoke")!.GetParameters()
            .Select(p => Expression.Parameter(p.ParameterType, p.Name))
            .ToArray();
        var callback = Expression.Call(
            Expression.Constant(onKey),
            typeof(Action).GetMethod("Invoke")!);
        var handler = Expression.Lambda(handlerType, callback, parameters).Compile();
        evt.AddEventHandler(device, handler);
    }

    /**
     * 把 HID 边界替身发布进 HC 自己的绑定表（`IDevice.hidDevices`）。
     * 说明：HID 边界被替换后，`ROGAlly.IsOpen` 仍读 HC 的绑定表，因此需要把替身设备
     * 放进同一张表（设备对象是**边界的产物**，不是管理逻辑的一部分）。
     */
    public void PublishFakeHidDevice(object hcDevice, int hidId, string identity, bool open)
    {
        var table = GetHcHidDeviceTable(hcDevice);
        var instance = CreateUninitializedHidDevice(identity, open);
        table[hidId] = instance;
    }

    public void SetFakeHidDeviceOpen(object hcDevice, int hidId, bool open)
    {
        var table = GetHcHidDeviceTable(hcDevice);
        if (table[hidId] is { } instance)
            SetHidDeviceOpen(instance, open);
    }

    public void RemoveFakeHidDevice(object hcDevice, int hidId)
    {
        GetHcHidDeviceTable(hcDevice).Remove(hidId);
    }

    private static IDictionary GetHcHidDeviceTable(object hcDevice)
    {
        for (var type = hcDevice.GetType(); type is not null; type = type.BaseType)
        {
            var field = type.GetField("hidDevices", BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
            if (field?.GetValue(hcDevice) is IDictionary table)
                return table;
        }
        throw new MissingFieldException(hcDevice.GetType().FullName, "hidDevices");
    }

    private static object CreateUninitializedHidDevice(string identity, bool open)
    {
        var hidLibrary = System.Runtime.Loader.AssemblyLoadContext.Default.Assemblies
            .FirstOrDefault(a => a.GetName().Name == "HidLibrary")
            ?? throw new InvalidOperationException("HidLibrary assembly is not loaded");
        var hidDeviceType = hidLibrary.GetType("HidLibrary.HidDevice")
            ?? throw new InvalidOperationException("HidLibrary.HidDevice not found");
        var instance = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(hidDeviceType);
        hidDeviceType.GetField("_devicePath", BindingFlags.Instance | BindingFlags.NonPublic)?.SetValue(instance, identity);
        SetHidDeviceOpen(instance, open);
        return instance;
    }

    private static void SetHidDeviceOpen(object hidDeviceInstance, bool open) =>
        hidDeviceInstance.GetType()
            .GetField("<IsOpen>k__BackingField", BindingFlags.Instance | BindingFlags.NonPublic)
            ?.SetValue(hidDeviceInstance, open);

    /** 安装 ACPI 传输替身（OS/驱动边界）；传 null 恢复真实 kernel32 传输。 */
    public void InstallAcpiTransport(object? transportProxy)
    {
        var method = acpiType.GetMethod("SetTransportForInjection", BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingMethodException("AsusACPI.SetTransportForInjection");
        method.Invoke(null, [transportProxy]);
    }

    // ---------------- AsusACPI ----------------

    public object FanEnumValue(AsusFanTarget target) =>
        Enum.Parse(fanEnum, target switch
        {
            AsusFanTarget.Cpu => "CPU",
            AsusFanTarget.Gpu => "GPU",
            _ => "Mid",
        });

    public object SetFanCurveObserved(AsusFanTarget target, byte[] curve)
    {
        var method = acpiType.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.Name == "SetFanCurveObserved")
            .Where(m => m.GetParameters() is [{ ParameterType: var p0 }, { ParameterType: var p1 }, { ParameterType: var p2 }]
                        && p0 == fanEnum && p1 == typeof(byte[]) && p2.IsByRef)
            .FirstOrDefault() ?? throw new MissingMethodException("AsusACPI.SetFanCurveObserved");
        var args = new object?[] { FanEnumValue(target), curve, null };
        method.Invoke(null, args);
        return args[2] ?? throw new InvalidOperationException("SetFanCurveObserved returned no receipt");
    }

    public object GetFanCurveObserved(AsusFanTarget target)
    {
        var method = acpiType.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.Name == "GetFanCurveObserved")
            .Where(m => m.GetParameters() is [{ ParameterType: var p0 }, { ParameterType: var p1 }, { ParameterType: var p2 }]
                        && p0 == fanEnum && p1 == typeof(int) && p2.IsByRef)
            .FirstOrDefault() ?? throw new MissingMethodException("AsusACPI.GetFanCurveObserved");
        var args = new object?[] { FanEnumValue(target), 0, null };
        method.Invoke(null, args);
        return args[2] ?? throw new InvalidOperationException("GetFanCurveObserved returned no receipt");
    }

    public List<object> TakeAcpiReceipts() => InvokeReceipts("TakeReceipts");

    public List<object> PeekAcpiReceipts() => InvokeReceipts("PeekReceipts");

    private List<object> InvokeReceipts(string methodName)
    {
        var method = observationsType.GetMethod(methodName, BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingMethodException("AsusAcpiObservations." + methodName);
        var result = new List<object>();
        if (method.Invoke(null, null) is IEnumerable items)
            foreach (var item in items)
                if (item is not null)
                    result.Add(item);
        return result;
    }

    // ---------------- 代理工厂（边界替身） ----------------

    public object CreateBoundaryProxy(CandidateBoundaryFake fake)
    {
        // §19：代理按**资源接口**创建（该接口继承设备边界接口），使候选生命周期能同时
        // 拿到“基础边界 + 结构化清理”两种能力（生产真实边界同样实现两者）。
        var proxy = (BoundaryProxy)DispatchProxy.Create(AtomicBoundaryProxyInterface(), typeof(BoundaryProxy));
        proxy.Target = fake;
        return proxy;
    }

    /**
     * §22.2-B：替身边界按“原子摘除合同 + 资源接口”的**合并接口**创建（候选提供时），
     * 使替身能声明 `DetachIsAtomicOnFailure`；旧候选（无该接口）退回资源接口。
     */
    private Type AtomicBoundaryProxyInterface()
    {
        var atomic = assembly.GetType("HandheldCompanion.Devices.ASUS.IHcRogAllyAtomicResourceBoundary", throwOnError: false);
        return atomic ?? boundaryResourceInterface;
    }

    /** 候选生命周期使用的交错点钩子（内部字段；默认 null）。 */
    public void SetInterleaveHook(Action<string>? hook)
    {
        var field = lifecycleType.GetField("InterleaveHook", BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new MissingFieldException(lifecycleType.FullName, "InterleaveHook");
        field.SetValue(null, hook);
    }

    /** 公开 IsReady（走 ProbeReadyForPublicIsReady 路径）。 */
    public bool CallIsReady(object device) =>
        device.GetType().GetMethod("IsReady", BindingFlags.Public | BindingFlags.Instance)?.Invoke(device, null) is true;

    /** 等待候选 Close 清理责任结算（有界）。 */
    public bool WaitCloseCleanupCompleted(object device, int timeoutMs, out string detail)
    {
        var args = new object?[] { timeoutMs, null };
        var result = InvokeMatching(device.GetType(), device, "TryWaitCloseCleanupCompleted", args) is true;
        detail = args[1]?.ToString() ?? "<none>";
        return result;
    }

    /** 候选释放结果枚举值（DispatchProxy 必须返回真实枚举，不能返回字符串）。 */
    public object ReleaseOutcome(string name) =>
        Enum.Parse(Require("HandheldCompanion.Devices.ASUS.HcRogAllyDeviceRelease"), name);

    // ---------------- §20：真实 HidReadIoGuard / 清理周期 / 作用域生命周期 ----------------

    private Type? guardTypeCache;

    private Type GuardType => guardTypeCache ??= Require("HandheldCompanion.Devices.ASUS.HidReadIoGuard");

    public string GuardTypeName => GuardType.FullName ?? "<none>";

    public Type GuardTargetInterface => Require("HandheldCompanion.Devices.ASUS.IHcHidHandleReleaseTarget");

    public object CreateGuard(string identity, object releaseTargetProxy)
    {
        var guardType = GuardType;
        var ctor = guardType.GetConstructors()
            .Where(c => c.GetParameters().Length >= 2)
            .Where(c => c.GetParameters()[0].ParameterType == typeof(string))
            .FirstOrDefault()
            ?? throw new MissingMethodException(guardType.FullName, ".ctor(string,IHcHidHandleReleaseTarget,Action)");
        var parameters = ctor.GetParameters();
        var args = new object?[parameters.Length];
        args[0] = identity;
        args[1] = releaseTargetProxy;
        for (var i = 2; i < parameters.Length; i++)
            args[i] = null;   // 取消钩子可空（测试不需要真实 CancelIoEx）
        return ctor.Invoke(args);
    }

    public bool GuardTryEnterRead(object guard) => InvokeMatching(guard.GetType(), guard, "TryEnterRead", []) is true;

    public void GuardExitRead(object guard) => InvokeMatching(guard.GetType(), guard, "ExitRead", []);

    public string GuardRetire(object guard, int waitMs, out string detail)
    {
        var args = new object?[] { waitMs, null };
        var result = InvokeMatching(guard.GetType(), guard, "Retire", args);
        detail = args[1]?.ToString() ?? "";
        return result?.ToString() ?? "<none>";
    }

    public string? GuardPending(object guard) => InvokeMatching(guard.GetType(), guard, "DescribePending", []) as string;

    public string GuardDescribe(object guard) =>
        InvokeMatching(guard.GetType(), guard, "Describe", []) as string ?? "<none>";

    public bool GuardIsSettled(object guard) =>
        guard.GetType().GetProperty("IsSettled", BindingFlags.Public | BindingFlags.Instance)?.GetValue(guard) is true;

    public int GuardActiveReaders(object guard) =>
        guard.GetType().GetProperty("ActiveReaders", BindingFlags.Public | BindingFlags.Instance)?.GetValue(guard) is int value ? value : -1;

    /** §20：设置清理等待预算覆盖值（0 = 使用候选默认）。 */
    public void SetCleanupSettlementWaitOverride(int milliseconds)
    {
        var field = lifecycleType.GetField("CleanupSettlementWaitOverrideMs", BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new MissingFieldException(lifecycleType.FullName, "CleanupSettlementWaitOverrideMs");
        field.SetValue(null, milliseconds);
    }

    /** 候选生命周期实例（§20 清理周期/在途状态查询）。 */
    public object LifecycleInstance(object device) => LifecycleOf(device);

    public string GetCleanupCycle(object device) =>
        InvokeMatching(lifecycleType, LifecycleOf(device), "DescribeCleanupCycle", []) as string ?? "<none>";

    public bool GetCleanupCycleInFlight(object device) =>
        lifecycleType.GetProperty("CleanupCycleInFlight", BindingFlags.Public | BindingFlags.Instance)
            ?.GetValue(LifecycleOf(device)) is true;

    /** 只读读取某操作当前收据的终态（未终结时返回 null）。 */
    public string? TryPeekReceiptOutcome(object device, string operationId)
    {
        var receipt = InvokeMatching(lifecycleType, LifecycleOf(device), "TryGetReceipt", [operationId]);
        if (receipt is null) return null;
        var outcome = GetString(receipt, "Outcome");
        return outcome is null || outcome == "NotStarted" ? null : outcome;
    }

    // ---------------- §21：清理周期台账与观测冻结 ----------------

    /** §21.2：未结算清理周期台账（资源清单 + 分项结果；"no-cycle" 表示无保留周期）。 */
    public string GetCleanupLedger(object device) =>
        InvokeMatching(lifecycleType, LifecycleOf(device), "DescribeCleanupLedger", []) as string ?? "<none>";

    /** §21.2：是否仍有未结算清理周期（责任保留）。 */
    public bool GetCleanupCycleRetained(object device) =>
        lifecycleType.GetProperty("CleanupCycleRetained", BindingFlags.Public | BindingFlags.Instance)
            ?.GetValue(LifecycleOf(device)) is true;

    /**
     * §21.2：读取某 Close 收据上**已发布**的清理报告文本（验证“旧周期的观察结果迟到”时不得被改写）。
     * 报告为空 ⇒ "<none>"。
     */
    public string DescribeReceiptReport(object closeReceipt)
    {
        var report = GetMemberValue(closeReceipt, "CleanupReport");
        return report is null ? "<none>" : (Describe(report) ?? "<none>");
    }

    // ---------------- §23：统一判据探针 / 真实适配器摘除原子性 ----------------

    /**
     * §23.3：直接驱动候选共享的 `HcRogAllyDetachSequence`（无 I/O 的清单/表变更逻辑），
     * 验证结构性质：capture 抛错时 clear **不被调用**（= 抛错 ⇒ 完全未摘除），正常时清一次。
     * 用 `List<object>` 作元素类型即可（该函数只用泛型约束 class，不依赖元素语义）。
     */
    public string DetachSequenceStructuralProbe()
    {
        var sequenceType = assembly.GetType("HandheldCompanion.Devices.ASUS.HcRogAllyDetachSequence", throwOnError: true)!;
        var run = sequenceType.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.Name == "Run" && m.IsGenericMethodDefinition)
            .Where(m => m.GetParameters().Length == 2)
            .FirstOrDefault() ?? throw new MissingMethodException(sequenceType.FullName, "Run");
        var dedup = sequenceType.GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.Name == "DistinctByInstance" && m.IsGenericMethodDefinition)
            .FirstOrDefault() ?? throw new MissingMethodException(sequenceType.FullName, "DistinctByInstance");
        var closedRun = run.MakeGenericMethod(typeof(List<object>));
        var closedDedup = dedup.MakeGenericMethod(typeof(List<object>));

        var clearCalls = 0;
        var action = (Action)(() => clearCalls++);
        var faultPropagated = false;
        try
        {
            closedRun.Invoke(null, new object?[]
            {
                (Func<IReadOnlyList<List<object>>>)(() => throw new InvalidOperationException("injected-capture-failure")),
                action
            });
        }
        catch (TargetInvocationException ex)
        {
            faultPropagated = ex.InnerException is InvalidOperationException;
        }
        var clearCallsAfterFault = clearCalls;

        var inventory = new List<List<object>> { new() };
        var returned = closedRun.Invoke(null, new object?[]
        {
            (Func<IReadOnlyList<List<object>>>)(() => inventory),
            action
        }) as IReadOnlyList<List<object>>;
        var clearCallsAfterSuccess = clearCalls;

        // 按实例去重（真实适配器用它保证同一底层设备的多个逻辑绑定只释放一次）。
        var shared = new List<object>();
        var duplicate = new List<object>();
        var dedupInput = new List<List<object>> { shared, duplicate, shared };
        var dedupResult = closedDedup.Invoke(null, new object?[]
        {
            (IEnumerable<List<object>>)dedupInput,
            (Func<List<object>, object>)(item => ReferenceEquals(item, shared) ? shared : duplicate)
        }) as IReadOnlyList<List<object>>;

        return $"faultPropagated={faultPropagated} clearCallsAfterFault={clearCallsAfterFault} " +
               $"clearCallsAfterSuccess={clearCallsAfterSuccess} inventoryCount={returned?.Count ?? -1} " +
               $"dedupIn={dedupInput.Count} dedupOut={dedupResult?.Count ?? -1}";
    }

    /**
     * §23.3：直接构造**真实边界**（ROGAlly 的私有嵌套 RogAllyHidBoundary）。
     * 不读静态工厂：该字段可能已被其它测试组替换为替身，读它会把替身当真实适配器（自证失效）。
     */
    public object CreateRealBoundary(object hcDevice)
    {
        var boundaryType = assembly.GetType("HandheldCompanion.Devices.ROGAlly+RogAllyHidBoundary", throwOnError: true)!;
        var ctor = boundaryType.GetConstructor(
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            binder: null,
            [hcDevice.GetType()],
            modifiers: null) ?? throw new MissingMethodException(boundaryType.FullName, ".ctor");
        return ctor.Invoke([hcDevice]);
    }

    /** §23.3：真实适配器摘除探针——成功摘除取得完整清单；capture 阶段抛错则表项保持原样，重试可恢复。 */
    public string RealAdapterDetachProbe(object hcDevice, object realBoundary)
    {
        var detach = boundaryResourceInterface.GetMethod("DetachAllBindings")
            ?? throw new MissingMethodException(boundaryResourceInterface.FullName, "DetachAllBindings");
        var isHeld = boundaryResourceInterface.GetMethod("IsDeviceHeld")
            ?? throw new MissingMethodException(boundaryResourceInterface.FullName, "IsDeviceHeld");
        var boundInput = boundaryInterface.GetProperty("BoundInputDevice")
            ?? throw new MissingMemberException(boundaryInterface.FullName, "BoundInputDevice");
        var table = GetHcHidDeviceTable(hcDevice);

        // 两个逻辑绑定（同一底层 HidDevice 实例）+ 一个不同实例 ⇒ 清单必须按实例去重。
        PublishFakeHidDevice(hcDevice, 0x5a, "real-adapter-a", open: true);
        var shared = table[0x5a];
        table[0x5d] = shared;
        var before = table.Count;
        var firstInventory = ((IEnumerable)detach.Invoke(realBoundary, null)!).Cast<object>().ToList();
        var afterSuccess = table.Count;

        // 故障注入：capture 阶段（列举 + Canonical 规范化）抛错 ⇒ clear 不得执行，表项保持原样。
        PublishFakeHidDevice(hcDevice, 0x5a, "real-adapter-b", open: true);
        var survivor = boundInput.GetValue(realBoundary)
            ?? throw new InvalidOperationException("§23: real boundary reported no bound input device");
        table[0x5d] = null;   // Canonical(null) 在 capture 阶段抛错
        var beforeFault = table.Count;
        var faultPropagated = false;
        try { detach.Invoke(realBoundary, null); }
        catch (TargetInvocationException ex) { faultPropagated = ex.InnerException is not null; }
        var afterFault = table.Count;
        var survivorHeldAfterFault = (bool)isHeld.Invoke(realBoundary, [survivor])!;

        // 去掉坏表项后重试：应取得**完整**清单（1 个实例）并清表。
        table.Remove(0x5d);
        var retryInventory = ((IEnumerable)detach.Invoke(realBoundary, null)!).Cast<object>().ToList();
        var afterRetry = table.Count;
        var survivorHeldAfterRetry = (bool)isHeld.Invoke(realBoundary, [survivor])!;

        return $"tableEntries[{before}->{afterSuccess}] firstInventory={firstInventory.Count} " +
               $"faultPropagated={faultPropagated} tableEntriesAfterFault={afterFault}(beforeFault={beforeFault}) " +
               $"survivorHeldAfterFault={survivorHeldAfterFault} retryInventory={retryInventory.Count} " +
               $"tableEntriesAfterRetry={afterRetry} survivorHeldAfterRetry={survivorHeldAfterRetry}";
    }

    /** HC 绑定表条目数（真实适配器摘除探针核对表项数用）。 */
    public int HcBindingTableCount(object hcDevice) => GetHcHidDeviceTable(hcDevice).Count;

    // ---------------- §20：作用域生命周期 ----------------

    public long AcpiScopeLateCount(object? scope) =>
        scope?.GetType().GetProperty("LateReceiptCount", BindingFlags.Public | BindingFlags.Instance)?.GetValue(scope) is int value
            ? value
            : 0L;

    public bool AcpiScopeClosed(object? scope) =>
        scope?.GetType().GetProperty("IsClosed", BindingFlags.Public | BindingFlags.Instance)?.GetValue(scope) is true;

    /** 构造强类型 IReadOnlyList&lt;IHcRogAllyHidDevice&gt;（边界替身返回“已摘除设备”用）。 */
    public object MakeHidDeviceList(IEnumerable<object> devices)
    {
        var listType = typeof(List<>).MakeGenericType(hidDeviceInterface);
        var list = (IList)Activator.CreateInstance(listType)!;
        foreach (var device in devices) list.Add(device);
        return list;
    }

    // ---------------- ACPI 操作作用域（§19-C） ----------------

    public object? BeginAcpiScope(string kind)
    {
        var method = observationsType.GetMethod("BeginScope", BindingFlags.Public | BindingFlags.Static);
        return method?.Invoke(null, [kind]);
    }

    public List<object> TakeAcpiScopeReceipts(object? scope) => InvokeScope(scope, "TakeReceipts");

    public List<object> PeekAcpiScopeReceipts(object? scope) => InvokeScope(scope, "PeekReceipts");

    public int AcpiScopeCount(object? scope) =>
        scope?.GetType().GetProperty("Count", BindingFlags.Public | BindingFlags.Instance)?.GetValue(scope) is int value
            ? value
            : 0;

    public void DisposeAcpiScope(object? scope) =>
        (scope as IDisposable)?.Dispose();

    public string? AcpiScopeId(object? scope) => scope is null ? null : GetString(scope, "ScopeId");

    private static List<object> InvokeScope(object? scope, string member)
    {
        var result = new List<object>();
        if (scope is null) return result;
        var value = scope.GetType().GetProperty(member, BindingFlags.Public | BindingFlags.Instance)?.GetValue(scope);
        if (value is IEnumerable items)
            foreach (var item in items)
                if (item is not null)
                    result.Add(item);
        return result;
    }

    public object CreateAcpiTransportProxy(CandidateAcpiTransportFake fake)
    {
        var transportInterface = Require("HandheldCompanion.Devices.ASUS.IAsusAcpiTransport");
        var proxy = (AcpiTransportProxy)DispatchProxy.Create(transportInterface, typeof(AcpiTransportProxy));
        proxy.Target = fake;
        return proxy;
    }

    public object CreateHidDeviceProxy(FakeHidDeviceState state)
    {
        var proxy = (HidDeviceProxy)DispatchProxy.Create(hidDeviceInterface, typeof(HidDeviceProxy));
        proxy.State = state;
        return proxy;
    }

    public object CreateHidBindingProxy(int hidId, object deviceProxy)
    {
        var proxy = (HidBindingProxy)DispatchProxy.Create(hidBindingInterface, typeof(HidBindingProxy));
        proxy.HidId = hidId;
        proxy.Device = deviceProxy;
        return proxy;
    }

    public object MakeReadyProbe(bool ready, List<object> bindings)
    {
        var listType = typeof(List<>).MakeGenericType(hidBindingInterface);
        var list = (IList)Activator.CreateInstance(listType)!;
        foreach (var binding in bindings)
            list.Add(binding);
        return Activator.CreateInstance(probeStruct, ready, list)
            ?? throw new InvalidOperationException("could not build HcRogAllyReadyProbe");
    }

    public Type ReportType => reportStruct;

    /** 已完成的读取任务（返回候选的 Task<HcRogAllyHidReport>）。 */
    public object CompletedReportTask(byte key, bool success)
    {
        var fromResult = typeof(Task).GetMethod("FromResult")!.MakeGenericMethod(reportStruct);
        return fromResult.Invoke(null, [MakeReport(key, success)])!;
    }

    /** 未完成的读取任务：`complete` 由测试显式调用以释放该读取。 */
    public object PendingReportTask(out Action<byte, bool> complete)
    {
        var holderType = typeof(ReportCompletion<>).MakeGenericType(reportStruct);
        var holder = Activator.CreateInstance(holderType, nonPublic: true)!;
        var completeMethod = holderType.GetMethod("Complete")!;
        complete = (key, success) => completeMethod.Invoke(holder, [MakeReport(key, success)]);
        return holderType.GetProperty("Task")!.GetValue(holder)!;
    }

    /** 类型安全的“未完成报告”容器（用泛型方法代替对 TaskCompletionSource 的反射）。 */
    internal sealed class ReportCompletion<TReport>
    {
        private readonly TaskCompletionSource<TReport> source =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public object Task => source.Task;

        public void Complete(TReport report) => source.TrySetResult(report);
    }

    public object MakeReport(byte key, bool success)
    {
        // 注意：FromKey(byte key, int rawLength = 1) 有默认参数，反射调用必须显式给全。
        var args = success ? new object?[] { key, 1 } : [0];
        var method = reportStruct.GetMethod(success ? "FromKey" : "Failed", BindingFlags.Public | BindingFlags.Static)
            ?? throw new MissingMethodException("HcRogAllyHidReport report factory");
        return method.Invoke(null, args)
            ?? throw new InvalidOperationException("could not build HcRogAllyHidReport");
    }

    public static byte[] Curve(byte first, byte last)
    {
        var curve = new byte[16];
        for (var i = 0; i < 8; i++)
            curve[i] = (byte)(20 + i * 10);
        for (var i = 0; i < 8; i++)
            curve[8 + i] = i == 0 ? first : last;
        return curve;
    }

    public byte[] DefaultCurveFor(AsusFanTarget target)
    {
        var name = target switch
        {
            AsusFanTarget.Gpu => "defaultGPUFan",
            _ => "defaultCPUFan",
        };
        return GetStaticBytes("HandheldCompanion.Devices.ROGAlly", name)
            ?? throw new InvalidOperationException($"candidate default table {name} missing");
    }
}

/** HID 设备替身状态（唯一可变状态由测试与边界替身共享）。 */
internal sealed class FakeHidDeviceState
{
    public string Identity = "fake-hid";
    public bool Connected = true;
    public bool Open;
    /** 已被边界退役（会话关闭/清理）：此后不得再进入库读（真实库会因 !IsOpen 自动重开）。 */
    public bool Retired;
}

/** DispatchProxy：候选 IHcRogAllyDeviceBoundary → Host 侧边界替身。 */
internal class BoundaryProxy : DispatchProxy
{
    public CandidateBoundaryFake? Target { get; set; }

    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) =>
        Target!.Invoke(targetMethod!, args ?? Array.Empty<object?>());
}

/** DispatchProxy：候选 IAsusAcpiTransport → Host 侧 ACPI 传输替身。 */
internal class AcpiTransportProxy : DispatchProxy
{
    public CandidateAcpiTransportFake? Target { get; set; }

    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) =>
        Target!.Invoke(targetMethod!, args ?? Array.Empty<object?>());
}

/** DispatchProxy：候选 IHcRogAllyHidDevice。 */
internal class HidDeviceProxy : DispatchProxy
{
    public FakeHidDeviceState? State { get; set; }

    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) => targetMethod!.Name switch
    {
        "get_Identity" => State!.Identity,
        "get_IsConnected" => State!.Connected,
        "get_IsOpen" => State!.Open,
        _ => throw new NotSupportedException($"unexpected IHcRogAllyHidDevice member: {targetMethod.Name}"),
    };
}

/** DispatchProxy：候选 IHcRogAllyHidBinding。 */
internal class HidBindingProxy : DispatchProxy
{
    public int HidId { get; set; }
    public object? Device { get; set; }

    protected override object? Invoke(MethodInfo? targetMethod, object?[]? args) => targetMethod!.Name switch
    {
        "get_HidId" => HidId,
        "get_Device" => Device,
        _ => throw new NotSupportedException($"unexpected IHcRogAllyHidBinding member: {targetMethod.Name}"),
    };
}

/** 边界替身集合：HID 边界 + ACPI 传输边界。 */
internal sealed class CandidateFakes
{
    public CandidateFakes(CandidateReflection reflection)
    {
        Reflection = reflection;
        Boundary = new CandidateBoundaryFake(reflection);
        Acpi = new CandidateAcpiTransportFake(reflection);
    }

    public CandidateReflection Reflection { get; }
    public CandidateBoundaryFake Boundary { get; }
    public CandidateAcpiTransportFake Acpi { get; }
}

/** OS/HID 边界替身：只模拟设备侧行为，管理逻辑仍在候选真实实现里。 */
internal sealed class CandidateBoundaryFake
{
    private readonly CandidateReflection reflection;
    private readonly object proxy;
    private readonly Dictionary<int, object> published = new();
    private readonly Dictionary<object, FakeHidDeviceState> deviceStates = new();
    private readonly Dictionary<object, int> deviceInstanceNumbers = new();
    private readonly List<(object Device, Action<byte, bool> Complete)> pendingReads = new();
    private readonly Queue<byte> queuedKeys = new();
    private readonly ManualResetEventSlim probeEntered = new(false);
    private readonly ManualResetEventSlim readEntered = new(false);
    private readonly ManualResetEventSlim keyDispatched = new(false);
    private readonly ManualResetEventSlim probeRelease = new(true);
    private readonly ManualResetEventSlim readDispatchRelease = new(true);
    private readonly ManualResetEventSlim deviceCallRelease = new(true);
    private readonly List<object> deferredReleaseDevices = new();
    private bool blockProbe;
    private bool blockReadDispatch;
    private bool blockDeviceCall;
    private int deviceInstanceSequence;

    public CandidateBoundaryFake(CandidateReflection reflection)
    {
        this.reflection = reflection;
        proxy = reflection.CreateBoundaryProxy(this);
    }

    public object Proxy => proxy;

    public bool BaseOpen { get; set; } = true;
    public bool AutoBindOnProbe { get; set; }
    public int ThrowOnOpen { get; set; }
    public int ThrowOnRemap { get; set; }

    /** 阻塞准备探测（代表真实 HID 枚举/OS 调用耗时）；置 false 即释放。 */
    public bool BlockProbe
    {
        get => blockProbe;
        set
        {
            blockProbe = value;
            if (value) probeRelease.Reset();
            else probeRelease.Set();
        }
    }

    /**
     * §19-B 交错点②：读已“排队”但**尚未真正读取**（库内 ReadReportAsync 用 StartNew 排队，
     * 检查与真正读取之间有窗口）。置 false 即释放。
     */
    public bool BlockReadDispatch
    {
        get => blockReadDispatch;
        set
        {
            blockReadDispatch = value;
            if (value) readDispatchRelease.Reset();
            else readDispatchRelease.Set();
        }
    }

    /**
     * §19-B 交错点④：Open/remap/Release 已进入且**不返回**。置 false 即释放。
     */
    public bool BlockDeviceCall
    {
        get => blockDeviceCall;
        set
        {
            blockDeviceCall = value;
            if (value) deviceCallRelease.Reset();
            else deviceCallRelease.Set();
        }
    }

    /**
     * 库自动重开仿真开关（负例用）：真实 HidLibrary 的 Read 在 !IsOpen 时自动 OpenDevice()。
     * true（默认）＝候选边界的退役规则生效（退役后不进入库读）；false＝按库原样重开，用于
     * 证明该规则确实是“不自动重开”的承重条件。
     */
    public bool HonorRetireRule { get; set; } = true;

    /** 注入句柄释放失败次数（负数/0 = 不注入）。 */
    public int FailDetachedReleases { get; set; }

    /** §23：注入“释放结果未知”次数（不得当作已释放，也不得当作失败）。 */
    public int UnknownDetachedReleases { get; set; }

    /** 注入“把释放责任延迟给在途读取”次数（模拟真实边界 CancelIoEx 后读取仍未退出）。 */
    public int DeferDetachedReleases { get; set; }

    /** §21.2 注入：接下来 N 次结构化摘除（DetachAllBindings）抛错（表保持原样、不做任何移除）。 */
    public int FailDetach { get; set; }

    /**
     * §21.2 注入：下一次结构化摘除**先移除一个实例**（模拟“部分摘除”）再抛错——
     * 调用方拿不到已摘除清单，用于验证“不得用后续空列表证明完成、责任必须保留”。
     */
    public bool PartialDetachThenThrow { get; set; }

    /** §21.2：结构化摘除抛错次数（诊断）。 */
    public int DetachFailures { get; private set; }

    /**
     * §22.2-B：本替身边界是否**违反**过原子摘除合同（PartialDetachThenThrow 注入就是一次违约）。
     * 违约后 `DetachIsAtomicOnFailure` 读作 false ⇒ 生命周期不再用重试清单结算（责任保留）。
     */
    public bool DetachContractViolated { get; private set; }

    /** §22.2-B：原子摘除合同声明（未违约时为 true；与真实边界 RogAllyHidBoundary 的保证一致）。 */
    public bool DetachIsAtomicOnFailure => !DetachContractViolated;

    /** §22.2-A 注入：阻塞接下来 N 次 pending 观察（在捕获快照之后停住，模拟“旧观察迟到提交”）。 */
    private int blockNextPendingObservation;
    public int BlockNextPendingObservation
    {
        get => blockNextPendingObservation;
        set
        {
            blockNextPendingObservation = value;
            if (value > 0)
            {
                pendingObservationEntered.Reset();
                pendingObservationRelease.Reset();
            }
        }
    }
    private readonly ManualResetEventSlim pendingObservationEntered = new(false);
    private readonly ManualResetEventSlim pendingObservationRelease = new(false);
    public bool WaitPendingObservationEntered(int timeoutMs) => pendingObservationEntered.Wait(timeoutMs);
    public void ReleasePendingObservation() => pendingObservationRelease.Set();

    /** §21.2：ReleaseDetachedDevice 被调用次数（含失败/延迟；用于断言“原周期按原实例重试”）。 */
    public int ReleaseDetachedCalls { get; private set; }

    /** §22 诊断：当前“延迟释放”待结算条数（只读）。 */
    public int PendingCountForDiagnostics { get { lock (pendingReads) return deferredReleaseDevices.Count; } }

    /** 只暂停接下来 N 次探测（§19-A 旧探测交错用；后续探测照常放行）。 */
    public int BlockProbeTimes { get; set; }

    /** 武装“只暂停一次探测”（复位暂停事件，避免上一次释放留下的信号让暂停失效）。 */
    public void ArmProbePauseOnce()
    {
        probeEntered.Reset();
        probeRelease.Reset();
        BlockProbeTimes = 1;
    }

    public int ProbeCalls { get; private set; }

    /** 释放被暂停的探测（配合 BlockProbe / BlockProbeTimes）。 */
    public void ReleaseBlockedProbe() => probeRelease.Set();

    private readonly ManualResetEventSlim readDispatchEntered = new(false);
    private readonly ManualResetEventSlim deviceCallEntered = new(false);
    private readonly ManualResetEventSlim reopenAttempted = new(false);

    public bool WaitReadDispatchEntered(int timeoutMs) => readDispatchEntered.Wait(timeoutMs);
    public bool WaitDeviceCallEntered(int timeoutMs) => deviceCallEntered.Wait(timeoutMs);
    public bool WaitReopenAttempted(int timeoutMs) => reopenAttempted.Wait(timeoutMs);

    public int HidOpens { get; private set; }
    public int RemapOnCount { get; private set; }
    public int RemapOffCount { get; private set; }
    public int ReadCalls { get; private set; }
    public int KeyDispatches { get; private set; }
    public int BindPublications { get; private set; }
    public int TableEntriesRemoved { get; private set; }
    public int DevicesReleased { get; private set; }
    public int DeferredReleases { get; private set; }
    public int ReleaseFailures { get; private set; }
    /** §23：返回过“结果未知”的次数（诊断）。 */
    public int UnknownReleases { get; private set; }
    /** 退役后仍被拒绝进入库读的次数（证明“不自动重开”协议生效）。 */
    public int ReadsBlockedByRetire { get; private set; }
    /** “读已排队未执行”暂停点被进入的次数（单调；测试按基线比对，避免事件残留误判）。 */
    public int ReadDispatchPauses { get; private set; }
    /** “设备调用已进入且不返回”暂停点被进入的次数（单调；测试按基线比对）。 */
    public int DeviceCallPauses { get; private set; }
    /** 仿真“库自动重开”次数（负例应为 1，正例必须为 0）。 */
    public int ReopenAttempts { get; private set; }
    public string? LastDisposedIdentity { get; private set; }
    public List<string> Logs { get; } = [];
    public List<string> Violations { get; } = [];

    public FakeHidDeviceState CurrentDeviceState { get; } = new();

    /** 候选 HC 设备实例（用于把 HID 替身发布进 HC 自己的绑定表）。 */
    public object? HcDevice { get; set; }

    public void ResetForNewScenario()
    {
        CompletePendingReads(failed: true, key: 0);
        ResetCountersKeepSession();
        published.Clear();
        CurrentDeviceState.Open = false;
        CurrentDeviceState.Connected = true;
        CurrentDeviceState.Retired = false;
        BlockProbe = false;
        BlockReadDispatch = false;
        BlockDeviceCall = false;
        BlockProbeTimes = 0;
        ThrowOnOpen = 0;
        ThrowOnRemap = 0;
        FailDetachedReleases = 0;
        UnknownDetachedReleases = 0;
        DeferDetachedReleases = 0;
        FailDetach = 0;
        PartialDetachThenThrow = false;
        DetachFailures = 0;
        DetachContractViolated = false;
        BlockNextPendingObservation = 0;
        ReleaseDetachedCalls = 0;
        deferredReleaseDevices.Clear();
        AutoBindOnProbe = true;
        Logs.Clear();
        Violations.Clear();
        probeEntered.Reset();
        readEntered.Reset();
        keyDispatched.Reset();
    }

    public void ResetCountersKeepSession()
    {
        HidOpens = 0;
        RemapOnCount = 0;
        RemapOffCount = 0;
        ReadCalls = 0;
        KeyDispatches = 0;
        BindPublications = 0;
        TableEntriesRemoved = 0;
        DevicesReleased = 0;
        DeferredReleases = 0;
        ReleaseFailures = 0;
        UnknownReleases = 0;
        ReleaseDetachedCalls = 0;
        ReadsBlockedByRetire = 0;
        ReopenAttempts = 0;
    }

    public bool WaitProbeEntered(int timeoutMs) => probeEntered.Wait(timeoutMs);

    public bool WaitReadEntered(int timeoutMs) => readEntered.Wait(timeoutMs);

    /**
     * 920-v1.13 §24.2：等待**当前确实已登记的未决读取**（当前设备的读取任务）。
     * `readEntered` 是粘滞事件（只在场景复位时清除）：早先周期的读取也会把它置位，
     * 因此“进入过读取”不能证明“释放调用当时有在途读取”。而延迟释放
     * （`DeferDetachedReleases`）只在释放调用**当时** `PendingReadCount > 0` 才生效，
     * 所以“旧观察不得结算新周期”这类夹具的前提必须用本等待建立，不能靠粘滞事件。
     */
    public bool WaitPendingReadRegistered(int timeoutMs)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (true)
        {
            lock (pendingReads)
            {
                if (pendingReads.Any(entry =>
                        entry.Device is HidDeviceProxy proxy && ReferenceEquals(proxy.State, CurrentDeviceState)))
                    return true;
            }
            if (Environment.TickCount64 >= deadline) return false;
            Thread.Sleep(5);
        }
    }

    public bool WaitForKeyDispatch(int timeoutMs) => keyDispatched.Wait(timeoutMs);

    /** 完成全部未决读取（失败报告 = 循环退出；成功报告 = 迟到报告路径）。 */
    public void CompletePendingReads(bool failed, byte key)
    {
        List<(object Device, Action<byte, bool> Complete)> pending;
        lock (pendingReads)
        {
            pending = new List<(object, Action<byte, bool>)>(pendingReads);
            pendingReads.Clear();
        }
        Logs.Add($"complete-pending count={pending.Count} failed={failed}");
        foreach (var entry in pending)
        {
            try { entry.Complete(key, !failed); }
            catch (Exception ex)
            {
                var site = ex.TargetSite is null ? "-" : $"{ex.TargetSite.DeclaringType?.Name}.{ex.TargetSite.Name}";
                var frames = ex.StackTrace?.Split('\n').Take(2).Select(f => f.Trim()).ToArray() ?? [];
                Logs.Add($"complete-failed:{ex.GetType().Name}:{ex.Message} site={site} frames=[{string.Join(";", frames)}]");
            }
        }
        // 在途读取已返回：结算“延迟给在途读取”的句柄释放（真实边界里由最后一个读取者关闭句柄）。
        SettleDeferredReleases();
    }

    /** 只完成**该句柄实例**的未决读取（真实 CancelIoEx 作用于该句柄的 I/O）。 */
    private void CompleteDeviceReads(object deviceProxy)
    {
        List<Action<byte, bool>> pending;
        lock (pendingReads)
        {
            pending = pendingReads
                .Where(entry => ReferenceEquals(entry.Device, deviceProxy))
                .Select(entry => entry.Complete)
                .ToList();
            pendingReads.RemoveAll(entry => ReferenceEquals(entry.Device, deviceProxy));
        }
        Logs.Add($"complete-device-reads device={(deviceProxy as HidDeviceProxy)?.State?.Identity ?? "?"} count={pending.Count}");
        foreach (var complete in pending)
        {
            try { complete(0, false); }
            catch (Exception ex) { Logs.Add($"complete-device-read-failed:{ex.GetType().Name}:{ex.Message}"); }
        }
        SettleDeferredReleases();
    }

    /** 派发一个报告：若循环正阻塞在未决读取上，则直接完成该读取；否则排队给下一次读取。 */
    public void PushReport(byte key)
    {
        (object Device, Action<byte, bool> Complete)? pending = null;
        int remaining;
        lock (pendingReads)
        {
            if (pendingReads.Count > 0)
            {
                pending = pendingReads[^1];
                pendingReads.RemoveAt(pendingReads.Count - 1);
            }
            remaining = pendingReads.Count;
        }
        Logs.Add($"push-report key={key} delivered={pending is not null} pendingLeft={remaining} readCalls={ReadCalls}");
        if (pending is { } entry)
        {
            entry.Complete(key, true);
            SettleDeferredReleases();
            return;
        }
        lock (queuedKeys) queuedKeys.Enqueue(key);
    }

    public object Invoke(MethodInfo method, object?[] args) => method.Name switch
    {
        "get_IsBaseOpen" => BaseOpen,
        "IsHidBound" => published.ContainsKey((int)args[0]!),
        "get_BoundInputDevice" => published.TryGetValue(0x5a, out var bound) ? bound : null!,
        "ProbeReady" => ProbeReady(),
        "PublishBinding" => PublishBinding(args[0]!),
        "ReleaseBinding" => ReleaseBinding(args[0]!),
        "ReleaseAllBindings" => ReleaseAll(),
        "OpenDevice" => OpenDevice(args[0]!),
        "ReadReportAsync" => ReadReport(args[0]!),
        "ApplyControllerRemap" => ApplyControllerRemap((bool)args[0]!),
        "LogInformation" => Log(args[0]?.ToString(), error: false),
        "LogError" => Log(args[0]?.ToString(), error: true),
        // ---- IHcRogAllyDeviceResourceBoundary（§19 结构化清理）----
        "DetachAllBindings" => DetachAllBindings(),
        "TryClaimUnpublishedBinding" => TryClaimUnpublishedBinding(args[0]!),
        "IsDeviceHeld" => IsDeviceHeld(args[0]!),
        "ReleaseDetachedDevice" => ReleaseDetachedDevice(args[0]!, args),
        "DescribePendingReleases" => DescribePendingReleases(),
        "get_DetachIsAtomicOnFailure" => DetachIsAtomicOnFailure,
        _ => throw new NotSupportedException($"unexpected boundary member: {method.Name}"),
    };

    /** gate 内调用：只改表（不做 I/O），返回被摘除的句柄实例（同一实例只返回一次）。 */
    private object DetachAllBindings()
    {
        // §21.2 注入：整体失败（不做任何移除）。
        if (FailDetach > 0)
        {
            FailDetach--;
            DetachFailures++;
            Logs.Add("detach-all injected-failure");
            throw new InvalidOperationException("injected-detach-failure");
        }
        // §21.2 注入：摘除已发生（表项被移除）但**清单丢失**（抛错）——调用方从未取得已摘除实例。
        if (PartialDetachThenThrow)
        {
            PartialDetachThenThrow = false;
            DetachFailures++;
            DetachContractViolated = true;   // §22.2-B：这是一次原子合同违约（真实边界不会这样）。
            var removed = 0;
            foreach (var key in published.Keys.ToList())
            {
                published.Remove(key);
                TableEntriesRemoved++;
                removed++;
                if (HcDevice is { } hcDevice)
                    reflection.RemoveFakeHidDevice(hcDevice, key);
            }
            Logs.Add($"detach-all partial-then-throw removed={removed} listLost=true");
            throw new InvalidOperationException("injected-partial-detach-failure");
        }
        var devices = new List<object>();
        foreach (var key in published.Keys.ToList())
        {
            var device = published[key];
            if (!devices.Any(existing => ReferenceEquals(existing, device)))
                devices.Add(device);
            published.Remove(key);
            TableEntriesRemoved++;
            if (HcDevice is { } hcDevice)
                reflection.RemoveFakeHidDevice(hcDevice, key);
        }
        Logs.Add($"detach-all removed={TableEntriesRemoved} devices={devices.Count}");
        return reflection.MakeHidDeviceList(devices);
    }

    /** gate 内调用：该探测绑定是否未被任何表项持有（是 ⇒ 认领，由调用方在 gate 外释放）。 */
    private object TryClaimUnpublishedBinding(object bindingProxy)
    {
        if (bindingProxy is not HidBindingProxy binding) return false;
        bool held = published.Values.Any(bound => ReferenceEquals(bound, binding.Device));
        if (!held)
            Logs.Add($"claim-unpublished hidId=0x{binding.HidId:X2} claimed=true");
        return !held;
    }

    /** gate 内按**实例**查询该句柄是否仍被绑定表持有（§20.2-A 清理收据核对用）。 */
    private object IsDeviceHeld(object deviceProxy)
    {
        var held = published.Values.Any(bound => ReferenceEquals(bound, deviceProxy));
        Logs.Add($"is-device-held identity={(deviceProxy as HidDeviceProxy)?.State?.Identity ?? "?"} held={held}");
        return held;
    }

    /**
     * gate 外调用：释放一个已摘除/已认领的句柄实例。
     * 语义与真实边界一致：退役（禁止再进入库读，仿真库的自动重开窗口）→ 取消在途读
     * （此处以“完成未决读”仿真 CancelIoEx）→ 释放；可注入失败与“延迟给在途读取”。
     */
    private object ReleaseDetachedDevice(object deviceProxy, object?[] args)
    {
        ReleaseDetachedCalls++;
        var state = (deviceProxy as HidDeviceProxy)?.State;
        var identity = state?.Identity ?? "<unknown>";
        void SetDetail(string text)
        {
            if (args.Length > 2) args[2] = text;
        }
        if (BlockDeviceCall)
        {
            Logs.Add($"release-detached-entered identity={identity}");
            DeviceCallPauses++;
            deviceCallEntered.Set();
            deviceCallRelease.Wait(30_000);
        }
        if (state is not null) state.Retired = true;
        if (DeferDetachedReleases > 0 && PendingReadCount(deviceProxy) > 0)
        {
            DeferDetachedReleases--;
            DeferredReleases++;
            deferredReleaseDevices.Add(deviceProxy!);
            Logs.Add($"release-deferred identity={identity} readers={PendingReadCount(deviceProxy)}");
            SetDetail($"release-deferred-to-inflight-read readers={PendingReadCount(deviceProxy)}");
            return reflection.ReleaseOutcome("DeferredToInFlightRead");
        }
        if (FailDetachedReleases > 0)
        {
            FailDetachedReleases--;
            ReleaseFailures++;
            Logs.Add($"release-failed identity={identity}");
            SetDetail("injected-release-failure");
            return reflection.ReleaseOutcome("Failed");
        }
        // §23：结果未知（既不能证明已释放，也不能证明失败）——不得被空 pending 或一次观察消除。
        if (UnknownDetachedReleases > 0)
        {
            UnknownDetachedReleases--;
            UnknownReleases++;
            Logs.Add($"release-unknown identity={identity}");
            SetDetail("injected-release-unknown");
            return reflection.ReleaseOutcome("Unknown");
        }
        CompleteDeviceReads(deviceProxy!);
        if (state is not null) state.Open = false;
        DevicesReleased++;
        LastDisposedIdentity = identity;
        Logs.Add($"release-released identity={identity}");
        SetDetail("released-after-retire");
        return reflection.ReleaseOutcome("Released");
    }

    private object DescribePendingReleases()
    {
        var pending = deferredReleaseDevices
            .Select(device => $"{(device as HidDeviceProxy)?.State?.Identity ?? "<unknown>"}:release-deferred-to-inflight-read")
            .ToArray();
        // §22.2-A 注入：快照已捕获后停住（旧观察在新周期出现后“迟到提交”）。
        if (Interlocked.Exchange(ref blockNextPendingObservation, 0) == 1)
        {
            pendingObservationEntered.Set();
            if (!pendingObservationRelease.Wait(10_000)) throw new TimeoutException("pending-observation-release timeout");
        }
        return pending;
    }

    private int PendingReadCount(object deviceProxy)
    {
        lock (pendingReads) return pendingReads.Count(entry => ReferenceEquals(entry.Device, deviceProxy));
    }

    /** 在途读取结束时结算“延迟释放”的句柄（真实边界里由最后一个读取者关闭句柄）。 */
    private void SettleDeferredReleases()
    {
        if (deferredReleaseDevices.Count == 0) return;
        foreach (var device in deferredReleaseDevices.ToList())
        {
            // 只有该句柄已无未决读取时才由“最后一个读取者”完成释放。
            if (PendingReadCount(device) > 0) continue;
            deferredReleaseDevices.Remove(device);
            if (device is HidDeviceProxy proxy2 && proxy2.State is { } state)
            {
                state.Open = false;
                state.Retired = true;
            }
            DevicesReleased++;
            LastDisposedIdentity = (device as HidDeviceProxy)?.State?.Identity;
            Logs.Add($"release-deferred-completed identity={LastDisposedIdentity}");
        }
    }

    private object ProbeReady()
    {
        ProbeCalls++;
        // 先枚举（真实探测的枚举/特征读结果在耗时期间就已确定），再进入可控暂停：
        // §19-A 的“旧探测跨越新会话”交错依赖“暂停前已拿到待发布绑定”。
        var discovered = new List<object>();
        if (AutoBindOnProbe)
        {
            if (!published.ContainsKey(0x5a))
                discovered.Add(MakeBinding(0x5a, CurrentDeviceState));
            if (!published.ContainsKey(0x5d))
                discovered.Add(MakeBinding(0x5d, new FakeHidDeviceState { Identity = "fake-aura" }));
        }
        var ready = BaseOpen && (published.ContainsKey(0x5a) || discovered.Count > 0);

        // 单次阻塞（§19-A：只暂停“旧探测”的那一次枚举，后续探测照常放行）。
        if (BlockProbeTimes > 0)
        {
            BlockProbeTimes--;
            probeEntered.Set();
            probeRelease.Wait(30_000);
        }
        else if (BlockProbe)
        {
            probeEntered.Set();
            // 等待测试释放（阻塞代表真实的 HID 枚举/OS 调用耗时）。
            probeRelease.Wait(30_000);
        }

        return reflection.MakeReadyProbe(ready, discovered);
    }

    private object MakeBinding(int hidId, FakeHidDeviceState state)
    {
        var device = reflection.CreateHidDeviceProxy(state);
        deviceStates[device] = state;
        // 同一 DevicePath 重连会产生**新的句柄实例**：序号用于区分实例归属（§19-B）。
        deviceInstanceNumbers[device] = ++deviceInstanceSequence;
        return reflection.CreateHidBindingProxy(hidId, device);
    }

    /** 当前绑定实例标签（identity#instance）；用于“同 DevicePath 重连按实例区分”的核对。 */
    public string BoundDeviceInstanceLabel(int hidId)
    {
        if (!published.TryGetValue(hidId, out var device) || device is not HidDeviceProxy proxy || proxy.State is null)
            return "<none>";
        var number = deviceInstanceNumbers.TryGetValue(device, out var value) ? value : 0;
        return $"{proxy.State.Identity}#{number}";
    }

    /** 当前绑定实例对象（引用同一性核对）。 */
    public object? BoundDeviceInstance(int hidId) =>
        published.TryGetValue(hidId, out var device) ? device : null;

    /** 供测试构造“被移除设备”的边界对象（按 Identity 归属比对，不复制管理逻辑）。 */
    public object CreateRemovedDeviceProxy(string identity)
    {
        var state = new FakeHidDeviceState { Identity = identity, Connected = false, Open = false };
        var device = reflection.CreateHidDeviceProxy(state);
        deviceStates[device] = state;
        return device;
    }

    private object PublishBinding(object bindingProxy)
    {
        var binding = (HidBindingProxy)bindingProxy;
        published[binding.HidId] = binding.Device!;
        var state = (binding.Device as HidDeviceProxy)!.State!;
        deviceStates.TryAdd(binding.Device!, state);
        // 同一绑定也写进 HC 自己的表（ROGAlly.IsOpen / remap 目标依赖该表）。
        if (HcDevice is { } hcDevice)
            reflection.PublishFakeHidDevice(hcDevice, binding.HidId, state.Identity, open: false);
        BindPublications++;
        return null!;
    }

    private static string IdentityOf(object deviceProxy) =>
        deviceProxy is HidDeviceProxy proxy && proxy.State is { } state
            ? state.Identity
            : "<unknown>";

    private object ReleaseBinding(object deviceProxy)
    {
        var identity = IdentityOf(deviceProxy);
        foreach (var key in published.Where(p => IdentityOf(p.Value) == identity).Select(p => p.Key).ToList())
        {
            published.Remove(key);
            if (HcDevice is { } hcDevice)
                reflection.RemoveFakeHidDevice(hcDevice, key);
        }
        LastDisposedIdentity = identity;
        return null!;
    }

    private object ReleaseAll()
    {
        LastDisposedIdentity ??= published.Values.Select(IdentityOf).FirstOrDefault();
        if (HcDevice is { } hcDevice)
        {
            foreach (var key in published.Keys.ToList())
                reflection.RemoveFakeHidDevice(hcDevice, key);
        }
        published.Clear();
        CurrentDeviceState.Open = false;
        return null!;
    }

    private object OpenDevice(object deviceProxy)
    {
        if (BlockDeviceCall)
        {
            // §19-B 交错点④：Open 已进入且不返回（调用方必须据此报告“设备调用待处理”，不得宣称完成）。
            Logs.Add("open-entered");
            DeviceCallPauses++;
            deviceCallEntered.Set();
            deviceCallRelease.Wait(30_000);
        }
        if (ThrowOnOpen > 0)
        {
            ThrowOnOpen--;
            throw new IOException("injected-open-failure");
        }
        HidOpens++;
        CurrentDeviceState.Open = true;
        CurrentDeviceState.Retired = false;
        if (HcDevice is { } hcDevice)
        {
            // 打开动作本身保证 HC 绑定表里有一个"已打开"的输入设备（不依赖发布时序）。
            reflection.PublishFakeHidDevice(hcDevice, 0x5a, CurrentDeviceState.Identity, open: true);
        }
        return null!;
    }

    private object ReadReport(object deviceProxy)
    {
        if (BlockReadDispatch)
        {
            // §19-B 交错点②：读已由库排队（真实库用 TaskFactory.StartNew），尚未真正读取。
            Logs.Add("read-dispatch-blocked");
            ReadDispatchPauses++;
            readDispatchEntered.Set();
            readDispatchRelease.Wait(30_000);
        }

        var state = (deviceProxy as HidDeviceProxy)?.State;
        if (state is { Retired: true } && HonorRetireRule)
        {
            // 候选边界读取适配的规则：退役后不进入库读 ⇒ 不会触发库的自动 OpenDevice()。
            ReadsBlockedByRetire++;
            Logs.Add("read-blocked-by-retire");
            return reflection.CompletedReportTask(0, success: false);
        }
        if (state is { Open: false })
        {
            // 库语义仿真：Read() 在 !IsOpen 时自动 OpenDevice()（真实 HidLibrary 3.2.49）。
            ReopenAttempts++;
            state.Open = true;
            HidOpens++;
            reopenAttempted.Set();
            Logs.Add("library-auto-reopen");
        }

        ReadCalls++;
        byte queued;
        lock (queuedKeys)
        {
            if (queuedKeys.Count > 0)
            {
                var key = queuedKeys.Dequeue();
                readEntered.Set();
                Logs.Add($"read#{ReadCalls}-queued-delivered key={key}");
                return reflection.CompletedReportTask(key, success: true);
            }
            queued = 0;
        }
        _ = queued;

        var task = reflection.PendingReportTask(out var complete);
        lock (pendingReads) pendingReads.Add((deviceProxy, complete));
        Logs.Add($"read#{ReadCalls}-pending-registered");
        // 事件必须在**登记之后**置位：等待方醒来时未决读取一定可被 PushReport 命中。
        readEntered.Set();
        return task;
    }

    private object ApplyControllerRemap(bool enable)
    {
        if (BlockDeviceCall)
        {
            Logs.Add($"remap-entered enable={enable}");
            DeviceCallPauses++;
            deviceCallEntered.Set();
            deviceCallRelease.Wait(30_000);
        }
        if (ThrowOnRemap > 0)
        {
            ThrowOnRemap--;
            throw new IOException("injected-remap-failure");
        }
        if (enable) RemapOnCount++;
        else RemapOffCount++;
        return null!;
    }

    private object Log(string? message, bool error)
    {
        Logs.Add($"{(error ? "ERROR" : "INFO")}:{message}");
        return null!;
    }

    /** 由 KeyPressed 事件订阅计数：候选读取循环真正派发按键事件时才递增。 */
    public void NoteKeyDispatch()
    {
        KeyDispatches++;
        keyDispatched.Set();
    }
}

/**
 * ACPI 传输替身（OS/驱动边界）。真实行为模式由测试配置；
 * 记录每次调用的原始封包与长度，供“真实封包/曲线变换/每扇一次写”核对。
 */
internal sealed class CandidateAcpiTransportFake
{
    private readonly CandidateReflection reflection;
    private readonly object proxy;
    private readonly ManualResetEventSlim blockedReadEntered = new(false);
    private readonly ManualResetEventSlim blockedReadReleased = new(false);
    private readonly object sync = new();

    public CandidateAcpiTransportFake(CandidateReflection reflection)
    {
        this.reflection = reflection;
        proxy = reflection.CreateAcpiTransportProxy(this);
    }

    public sealed record ControlCall(uint MethodId, uint DeviceId, int InputLength, int BufferSize, byte[] InBuffer);

    public object Proxy => proxy;
    public CandidateAcpiMode Mode { get; private set; } = CandidateAcpiMode.Complete16;
    public bool BlockReads { get; set; }
    public int ControlCalls { get; private set; }
    public int ReadCalls { get; private set; }
    public int FailedWriteCount { get; private set; }
    public List<ControlCall> WriteCalls { get; } = [];
    public List<ControlCall> ReadCallRecords { get; } = [];

    private const uint DevsMethod = 0x53564544;
    private const uint DstsMethod = 0x53545344;

    public void Reset(CandidateAcpiMode mode)
    {
        lock (sync)
        {
            Mode = mode;
            ControlCalls = 0;
            ReadCalls = 0;
            FailedWriteCount = 0;
            WriteCalls.Clear();
            ReadCallRecords.Clear();
            BlockReads = false;
            blockedReadEntered.Reset();
            blockedReadReleased.Reset();
        }
    }

    public bool WaitBlockedReadEntered(int timeoutMs) => blockedReadEntered.Wait(timeoutMs);

    public void ReleaseBlockedReads(bool shortResponse)
    {
        if (shortResponse)
            Mode = CandidateAcpiMode.ShortResponse;
        blockedReadReleased.Set();
    }

    public object Invoke(MethodInfo method, object?[] args) => method.Name switch
    {
        "IsHandleValid" => Mode != CandidateAcpiMode.InvalidHandle,
        "TryOpen" => TryOpen(args),
        "Control" => Control(args),
        "Close" => null!,
        _ => throw new NotSupportedException($"unexpected IAsusAcpiTransport member: {method.Name}"),
    };

    private object TryOpen(object?[] args)
    {
        if (Mode == CandidateAcpiMode.InvalidHandle)
        {
            args[0] = IntPtr.Zero;
            args[1] = 2;   // ERROR_FILE_NOT_FOUND
            return false;
        }
        args[0] = new IntPtr(0x920);
        args[1] = 0;
        return true;
    }

    private object Control(object?[] args)
    {
        var inBuffer = (byte[])args[2]!;
        var outBuffer = (byte[])args[3]!;
        var methodId = BitConverter.ToUInt32(inBuffer, 0);
        var deviceId = inBuffer.Length >= 12 ? BitConverter.ToUInt32(inBuffer, 8) : 0u;
        var call = new ControlCall(methodId, deviceId, inBuffer.Length, outBuffer.Length, inBuffer);

        lock (sync)
        {
            ControlCalls++;
            if (methodId == DevsMethod)
            {
                WriteCalls.Add(call);
            }
            else if (methodId == DstsMethod)
            {
                ReadCallRecords.Add(call);
                ReadCalls++;
            }
        }

        if (methodId == DstsMethod && BlockReads)
        {
            blockedReadEntered.Set();
            blockedReadReleased.Wait(30_000);
        }

        var mode = Mode;
        if (mode == CandidateAcpiMode.TransportException)
            throw new IOException("injected-transport-exception");

        if (mode == CandidateAcpiMode.IoFailure ||
            (mode == CandidateAcpiMode.IoFailureWrites && methodId == DevsMethod) ||
            (mode == CandidateAcpiMode.IoFailureGpuWrite && methodId == DevsMethod && deviceId == 0x00110025))
        {
            if (methodId == DevsMethod)
                lock (sync) FailedWriteCount++;
            args[4] = 0u;
            args[5] = 5;   // ERROR_ACCESS_DENIED
            return false;
        }

        if (mode == CandidateAcpiMode.ShortResponse)
        {
            Array.Clear(outBuffer);
            args[4] = 4u;
            args[5] = 0;
            return true;
        }

        if (mode == CandidateAcpiMode.MatchDefaults && methodId == DstsMethod)
        {
            var target = deviceId switch
            {
                0x00110025 => AsusFanTarget.Gpu,
                _ => AsusFanTarget.Cpu,
            };
            var curve = reflection.DefaultCurveFor(target);
            Array.Clear(outBuffer);
            Array.Copy(curve, outBuffer, Math.Min(curve.Length, outBuffer.Length));
            args[4] = (uint)outBuffer.Length;
            args[5] = 0;
            return true;
        }

        // Complete16：成功且返回整个输出容量（厂商语义仍由候选标 unknown）。
        Array.Clear(outBuffer);
        args[4] = (uint)outBuffer.Length;
        args[5] = 0;
        return true;
    }
}