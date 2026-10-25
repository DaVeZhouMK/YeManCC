using System.Collections.Concurrent;
using System.Reflection;

namespace YeManFanHost;

// FAN-952: CTW's fan-only sequence with the pinned HC block writer/bit receipts.
// No second writer, timer, temperature-table write or power-profile/Shift callback.
internal interface IMsiFamilyFanTransport
{
    object Gate { get; }
    int[]? ReadTemperatureAxis(byte block);
    object? WriteFanBlock(byte block, byte[] table);
    object? SetSoftwareControl();
    object? SetFullSpeed(bool enable);
}

internal sealed record MsiFamilyFanBlock(byte Block, string AxisSource, int[] TemperaturesC,
    int[] RequestedRawDuties, int[] ReadbackRawDuties, string Status, string WriteDisposition);

internal sealed class MsiFamilyFanSubmission
{
    public string Contract { get; } = "FAN-954/ctw-fan-only-and-full-blast-hc-verified";
    public bool FullSpeedRequested { get; set; }
    public List<MsiFamilyFanBlock> Blocks { get; } = [];
    public string SoftwareControlStatus { get; set; } = "not-attempted";
    public string FullSpeedStatus { get; set; } = "not-attempted";
    public string TableStatus => Blocks.Count == 2 && Blocks.All(b => b.Status == "ok") ? "ok" : Blocks.Count < 2 || Blocks.Any(b => b.Status == "unknown") ? "unknown" : "failed";
    public bool CommitAccepted { get; set; }
    public bool Success => CommitAccepted && TableStatus == "ok" && BitConfirmed(SoftwareControlStatus) && BitConfirmed(FullSpeedStatus);
    public bool PhysicalRpmConfirmed => false;
    public static bool BitConfirmed(string? status) => status is "ok" or "skipped-already-target";
}

internal static class MsiFamilyFanCurve
{
    // Exact old HC sample indices [0,2,5,6,8,9], retained only on unavailable axes.
    // This is a compatibility mapping, NOT an assertion about an AMD/Intel EC axis.
    private static readonly int[] LegacySamples = [0, 20, 50, 60, 80, 90];

    internal static int[]? DecodeTemperatureAxis(byte[]? data)
    {
        if (data is null || data.Length < 8) return null;
        int[] axis = [data[0], data[3], data[4], data[5], data[6], data[7]];
        if (axis[0] > 120) return null;
        for (int i = 1; i < axis.Length; i++)
            if (axis[i] <= axis[i - 1] || axis[i] > 120) return null;
        return axis;
    }

    internal static MsiFamilyFanSubmission Apply(IReadOnlyList<FanNode> nodes,
        IMsiFamilyFanTransport transport, Func<bool> writeAllowed,
        Action<MsiFamilyFanSubmission> observe)
    {
        // CTW distinguishes six curve sliders from the EC full-speed override (152.7).
        // An explicitly flat 100% intent selects the latter; ordinary curves clear it.
        var result = new MsiFamilyFanSubmission { FullSpeedRequested = nodes.Count > 0 && nodes.All(n => n.DutyPercent == 100) };
        void CheckWriteBoundary()
        {
            if (!writeAllowed())
                throw new FanApiException(409, "FAN_WRITE_SUPERSEDED", "风扇提交已被睡眠、关闭或超时中止，请重试");
        }
        try
        {
            lock (transport.Gate)
            {
                // Read/prepare both plans before either write. Read failures are explicit
                // compatibility fallback, not a new permanent manual-control gate.
                var plans = new List<(byte Block, string Source, int[] Axis, byte[] Table)>();
                for (byte block = 1; block <= 2; block++)
                {
                    CheckWriteBoundary();
                    int[]? axis;
                    try { axis = transport.ReadTemperatureAxis(block); }
                    catch { axis = null; }
                    bool valid = axis is { Length: 6 } && axis[0] >= 0 && axis[5] <= 120
                        && Enumerable.Range(1, 5).All(i => axis[i] > axis[i - 1]);
                    int[] selected = valid ? axis! : (int[])LegacySamples.Clone();
                    var table = new byte[8];
                    for (int i = 0; i < 6; i++)
                        table[i + 1] = (byte)Math.Clamp((int)(Interpolate(nodes, selected[i]) * 1.5), 0, 150);
                    plans.Add((block, valid ? "firmware-live" : "hc-legacy-sample-compatibility-axis-unknown", selected, table));
                }
                foreach (var plan in plans)
                {
                    CheckWriteBoundary();
                    object? receipt;
                    try { receipt = transport.WriteFanBlock(plan.Block, plan.Table); }
                    catch
                    {
                        result.Blocks.Add(new(plan.Block, plan.Source, plan.Axis,
                            plan.Table[1..7].Select(b => (int)b).ToArray(), [], "unknown", "write-unknown"));
                        throw;
                    }
                    result.Blocks.Add(new(plan.Block, plan.Source, plan.Axis,
                        plan.Table[1..7].Select(b => (int)b).ToArray(),
                        ReflectionMsiFanTransport.Member(receipt, "ReadbackDuties6") as int[] ?? [],
                        ReflectionMsiFanTransport.Member(receipt, "Status") as string ?? "unknown",
                        ReflectionMsiFanTransport.Member(receipt, "WriteDisposition") as string ?? "write-unknown"));
                }
                if (result.TableStatus != "ok")
                    throw new FanApiException(409, "FAN_CURVE_WRITE_FAILED", "MSI 风扇表未全部通过写后校验，控制尚未确认");
                CheckWriteBoundary();
                result.SoftwareControlStatus = ReflectionMsiFanTransport.Member(transport.SetSoftwareControl(), "Status") as string ?? "unknown";
                if (!MsiFamilyFanSubmission.BitConfirmed(result.SoftwareControlStatus))
                    throw new FanApiException(409, "FAN_CURVE_WRITE_FAILED", "MSI 软件控制位未通过写后校验");
                CheckWriteBoundary();
                result.FullSpeedStatus = ReflectionMsiFanTransport.Member(transport.SetFullSpeed(result.FullSpeedRequested), "Status") as string ?? "unknown";
                if (!MsiFamilyFanSubmission.BitConfirmed(result.FullSpeedStatus))
                    throw new FanApiException(409, "FAN_CURVE_WRITE_FAILED", "MSI 满速覆盖位未确认达到请求值，不能报告风扇已接管");
                CheckWriteBoundary();
                result.CommitAccepted = true;
            }
            return result;
        }
        finally { observe(result); }
    }

    private static double Interpolate(IReadOnlyList<FanNode> nodes, double temperature)
    {
        if (temperature <= nodes[0].TempC) return nodes[0].DutyPercent;
        for (int i = 1; i < nodes.Count; i++)
        {
            if (temperature > nodes[i].TempC) continue;
            var previous = nodes[i - 1];
            var current = nodes[i];
            var ratio = (temperature - previous.TempC) / (current.TempC - previous.TempC);
            return previous.DutyPercent + (current.DutyPercent - previous.DutyPercent) * ratio;
        }
        return nodes[^1].DutyPercent;
    }
}

// Resolves private methods on ClawA1M even when HC factory returns a derived
// A2VM/BZ2EM/CG3EM. All calls remain on the Host's existing STA owner.
internal sealed class ReflectionMsiFanTransport : IMsiFamilyFanTransport
{
    // Per-runtime-Type metadata only. Gate, WMI scope/path and all receipts are
    // read from the current device on each transport/call, including after wake.
    private static readonly ConcurrentDictionary<(Type Type, string Name, int Count), MethodInfo> methodMetadata = new();
    private static readonly ConcurrentDictionary<(Type Type, string Name), MemberInfo> memberMetadata = new();
    private static readonly ConcurrentDictionary<Type, MethodInfo> structuredReaderMetadata = new();
    private readonly object device;
    private readonly MethodInfo writeBlock;
    private readonly MethodInfo control;
    private readonly MethodInfo fullSpeed;
    private readonly MethodInfo getStructured;
    private readonly string scope;
    private readonly string path;
    public object Gate { get; }

    internal ReflectionMsiFanTransport(object device)
    {
        this.device = device;
        Gate = Member(device, "msiFanGate") ?? throw new MissingMemberException("HC MSI fan gate unavailable");
        writeBlock = Method(device.GetType(), "WriteMsiFanTableBlock", 2);
        control = Method(device.GetType(), "SetFanControl", 2);
        fullSpeed = Method(device.GetType(), "SetFanFullSpeed", 1);
        var wmi = device.GetType().Assembly.GetType("HandheldCompanion.WMI")
            ?? throw new MissingMemberException("HC WMI transport unavailable");
        getStructured = structuredReaderMetadata.GetOrAdd(wmi, static type =>
            type.GetMethod("GetStructured", BindingFlags.Public | BindingFlags.Static)
                ?? throw new MissingMemberException("HC structured WMI reader unavailable"));
        scope = Member(device, "WmiScope") as string
            ?? throw new MissingMemberException("HC MSI WMI scope unavailable");
        path = Member(device, "WmiPath") as string
            ?? throw new MissingMemberException("HC MSI WMI path unavailable");
    }

    public int[]? ReadTemperatureAxis(byte block)
    {
        object? result = getStructured.Invoke(null, [scope, path, "Get_Temperature", block, 32, 8]);
        return Member(result, "IsUsable") as bool? == true
            ? MsiFamilyFanCurve.DecodeTemperatureAxis(Member(result, "Data") as byte[]) : null;
    }
    public object? WriteFanBlock(byte block, byte[] table) => writeBlock.Invoke(device, [block, table]);
    public object? SetSoftwareControl()
    {
        control.Invoke(device, [true, 0]);
        return Member(device, "LastMsiFanControlWrite");
    }
    public object? SetFullSpeed(bool enable)
    {
        fullSpeed.Invoke(device, [enable]);
        return Member(device, "LastMsiFanFullSpeedWrite");
    }
    internal static MethodInfo Method(Type type, string name, int count)
    {
        var key = (type, name, count);
        if (methodMetadata.TryGetValue(key, out var cached)) return cached;
        for (Type? current = type; current is not null; current = current.BaseType)
        {
            var found = current.GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                .FirstOrDefault(m => m.Name == name && m.GetParameters().Length == count);
            if (found is not null)
            {
                methodMetadata.TryAdd(key, found);
                return found;
            }
        }
        throw new MissingMethodException(type.FullName, name);
    }
    internal static object? Member(object? target, string name)
    {
        if (target is null) return null;
        var key = (target.GetType(), name);
        if (!memberMetadata.TryGetValue(key, out var member))
        {
            for (Type? type = key.Item1; type is not null; type = type.BaseType)
            {
                var flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
                member = (MemberInfo?)type.GetProperty(name, flags) ?? type.GetField(name, flags);
                if (member is not null)
                {
                    memberMetadata.TryAdd(key, member);
                    break;
                }
            }
        }
        return member is PropertyInfo property ? property.GetValue(target)
            : member is FieldInfo field ? field.GetValue(target) : null;
    }
}
