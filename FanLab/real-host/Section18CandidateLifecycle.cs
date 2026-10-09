using System.Collections;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Linq.Expressions;
using System.Text.Json;

namespace YeManFanHost;

/**
 * 920-v1.6 §18：隔离候选 HC 的无硬件联调。
 *
 * 原则（§18.2）：
 * - **管理逻辑**来自候选 HC 的真实实现（ROGAlly + RogAllyDeviceLifecycle / AsusACPI）；
 * - 只在 OS/HID/ACPI 边界注入行为（DispatchProxy 边界替身），不复制状态机；
 * - 未提供 `--hc-candidate-assembly` 时本组不注册；提供但缺少版本化能力时**失败**；
 * - 所有必需不变量失败都抛出 ⇒ 进程非零退出（EXPECTATION-UNMET 的 exit=0 不得供 CandidateReady 使用）。
 */
internal static class Section18CandidateLifecycle
{
    public static IReadOnlyList<string> Run(FanHostEngine engine, List<string> sink) =>
        RealHcBackend.RunSection18CandidateSelfTest(engine, sink);
}

internal sealed partial class RealHcBackend
{
    private CandidateReflection? candidateReflection;

    // ======================================================================
    // 生产侧新增：候选能力消费（旧 runtime 报告不支持，行为保持原样）
    // ======================================================================

    internal object? DeviceForSelfTest => device;
    internal Assembly? LoadedHcAssemblyForSelfTest => assembly;

    /**
     * §18.2-A：等待候选 HC 生命周期接口给出的**真实操作完成**。
     * 返回 false 表示该设备没有版本化能力（旧 runtime）——调用方不得因此宣称满足新合同。
     */
    internal bool TryWaitCandidateLifecycleCompletion(
        object? device,
        int timeoutMs,
        out string outcome,
        out string detail)
    {
        outcome = "unsupported";
        detail = "device-missing";
        if (device is null)
            return false;

        if (GetMember(device, "LifecycleApiVersion") is not int apiVersion || apiVersion < 1)
        {
            detail = "lifecycle-api-absent";
            return false;
        }

        var operationId = GetMember(device, "LastCompatEntryOperationId") as string;
        if (string.IsNullOrEmpty(operationId))
        {
            detail = "compat-entry-operation-unknown";
            return false;
        }

        var waitMethod = device.GetType()
            .GetMethods(BindingFlags.Public | BindingFlags.Instance)
            .Where(m => m.Name == "TryWaitDeviceOpenReceipt")
            .Where(m => m.GetParameters() is [{ ParameterType: var p0 }, { ParameterType: var p1 }, { ParameterType: var p2 }]
                        && p0 == typeof(string) && p1 == typeof(int) && p2.IsByRef)
            .FirstOrDefault();
        if (waitMethod is null)
        {
            detail = "lifecycle-wait-member-missing";
            return false;
        }

        var waitArgs = new object?[] { operationId, timeoutMs, null };
        var finished = waitMethod.Invoke(device, waitArgs) is true;
        if (!finished || waitArgs[2] is null)
        {
            outcome = "incomplete";
            detail = $"operation={operationId} did-not-finish-within-{timeoutMs}ms";
            return true;
        }

        var receipt = waitArgs[2]!;
        outcome = GetMember(receipt, "Outcome")?.ToString() ?? "unknown";
        detail = GetMember(receipt, "Describe")?.ToString() ?? $"operation={operationId}";
        return true;
    }

    /**
     * §19-C：ACPI 写入收据必须关联到**本次操作**。
     * 旧口径 `ConfirmAsusReleaseWriteReceipts`（全局 TakeReceipts）已撤销：历史失败会污染本次成功，
     * 本次失败也可能因记录被别的消费者取走而漏判。现在由调用方在 HC 调用前用
     * `BeginAsusWriteScope` 打开作用域、调用后用 `CompleteAsusWriteScope` 消费**本作用域**的收据。
     */

    /**
     * 一次写入操作的作用域句柄。FAN-934 §4.A：句柄在**开始时就固化本条路线的证据适用性**
     * （`Mode`），ASUS 路线才取 ASUS 操作作用域收据；非 ASUS 的已审阅 HC 路线**不取**
     * ASUS 作用域，按 HC 真实回调合同结算。二者不得混用——否则"没有 ASUS 回执"会被
     * 误判成"恢复不明"（GPD Win5／MSI 退出 FaultLocked 的根因）。
     */
    internal sealed class AsusWriteScopeHandle
    {
        public object? Scope;
        public string Kind = string.Empty;
        /** 本条路线声明的证据适用性（开始时固化，避免操作中途路线漂移）。 */
        public FanEvidenceMode Mode = FanEvidenceMode.UnsupportedNoScope;
        /** ASUS 路线但运行时读不到操作作用域 API ⇒ 记收据 API 故障（unknown），不得降成普通兼容。 */
        public bool ApiUnavailable;
        /** 非 ASUS 兼容路线的回调前提（正确实例/会话/路线、Open/资源前提）是否已成立。 */
        public bool CompatPreconditionsOk;
        public string CompatPreconditionDetail = string.Empty;
    }

    /** 本次写入操作的收据判定结果（缺失/未知/明确失败分开，不把 unknown 写成成功或“未发生”）。 */
    internal readonly record struct AsusWriteVerdict(
        string Kind,
        string Detail,
        bool ExplicitFailure,
        bool Unknown,
        bool NoneTransported,
        int ReceiptCount)
    {
        public bool Transported => Kind == "transported";
        public bool Unsupported => Kind == "unsupported";
        /** 2026-09-23：部分通道失败（至少一个通道已送达）——不算成功，也不再致命。 */
        public bool Partial => Kind == "partial";
    }

    private string asusReleaseWriteOutcome = "none";

    /**
     * Q1/Q2（2026-09-23 补充裁决）：最近一次控制写入的收据口径（enable/restore 共用
     * 同一映射）。只诊断/只对外可见，**不承担授权**：partial 通道证据不得显示成成功。
     */
    private volatile string lastControlWriteEvidence = "none";

    /** 供引擎快照读取（Q2：partial/unknown 不得读成完全成功）。 */
    internal string LastControlWriteEvidence => lastControlWriteEvidence;

    /**
     * Q1/Q2（FAN-204 s31 补充裁决 2026-09-23）：写入收据 → 对外可读口径（enable 与
     * restore 共用同一映射）。partial 通道证据**绝不可**读成"完全成功"；unknown 读成
     * "未证实也未否定"；明确失败读成"未建立控制"。
     */
    internal static string WriteEvidenceName(AsusWriteVerdict verdict) => verdict.Kind switch
    {
        "transported" => "transported",
        "partial" => "partial-channel-evidence-not-success",
        "no-receipts" => "unknown-not-verified-no-receipts",
        // `unsupported` 只保留为"旧 runtime 不支持作用域"的**诊断**名（§19 legacy 断言依赖该 kind），
        // 不再作为可放行的兼容证书（见 ClassifyReleaseAction）。
        "unsupported" => "unsupported-no-scope-capability",
        // FAN-934 R2 §3-A.1：未登记/未声明证据适用性 ⇒ 独立 unknown，绝不显示成成功或"未发生"。
        "unregistered-route-no-scope" => "unknown-not-verified-route-undeclared",
        // FAN-934 §4.A：非 ASUS HC 路线的软件回调合同证据（不是物理成功，也不是传输收据）。
        "compat-route-without-scope" => "compat-route-callback-returned-no-scope",
        "compat-route-callback-not-returned" => "failed-write-not-established",
        "compat-route-precondition-unsatisfied" => "unknown-not-verified-compat-precondition",
        // ASUS 声明支持 scope 却读不到 API：如实记 unknown，不退成普通兼容成功。
        "receipt-api-fault" => "unknown-not-verified-receipt-api-fault",
        _ => verdict.ExplicitFailure ? "failed-write-not-established" : "unknown-not-verified",
    };

    internal string AsusReleaseWriteOutcomeForSelfTest => asusReleaseWriteOutcome;

    /** FAN-934 §4.A：本条路线的证据适用性；路线缺失 ⇒ fail-closed 为 UnsupportedNoScope。 */
    internal FanEvidenceMode CurrentRouteEvidenceMode() =>
        Route?.Evidence ?? FanEvidenceMode.UnsupportedNoScope;

    /**
     * FAN-934 R2 §3-A.2：非 ASUS 兼容路线前提判定的**生产纯函数**（自测与生产共用同一实现）。
     * 只看输入参数，便于用 mock 设备/route 直接驱动，不再靠手填布尔结论。
     * 返回 false 时给出**可复核**的原因串（含真实成员值），便于真机/日志定位。
     */
    internal static (bool Ok, string Detail) EvaluateCompatPreconditionsCore(
        FanEvidenceMode evidence,
        FanRoutePrecondition precondition,
        bool sessionReady,
        string sessionReason,
        bool? deviceUseOpenLib,
        bool? deviceIsOpen,
        bool? deviceIsSupported)
    {
        if (evidence != FanEvidenceMode.CompatCallbackNoScope)
            return (false, $"route-evidence={evidence}");
        if (!sessionReady)
            return (false, $"session-not-ready:{sessionReason}");
        return precondition switch
        {
            // 依赖 OpenLib 句柄的已审阅族：设备方法在 `!UseOpenLib || !IsOpen` 时静默 return。
            FanRoutePrecondition.OpenLibHandleOpen =>
                deviceUseOpenLib is true && deviceIsOpen is true
                    ? (true, "compat-callback-preconditions-ok")
                    : (false, $"openlib-handle-not-open(useOpenLib={Fmt(deviceUseOpenLib)},isOpen={Fmt(deviceIsOpen)})"),
            // SteamDeck：`IsOpen => inpOut is not null` 且 `IsSupported => SupportedDevice!=null && Firmware!=0`。
            FanRoutePrecondition.DeviceOpenAndSupported =>
                deviceIsOpen is true && deviceIsSupported is true
                    ? (true, "compat-callback-preconditions-ok")
                    : (false, $"device-not-open-and-supported(isOpen={Fmt(deviceIsOpen)},isSupported={Fmt(deviceIsSupported)})"),
            // MSI Claw / Lenovo WMI 表：WMI.Set 静默返回 null 不抛异常 ⇒ 必须核设备 IsOpen。
            FanRoutePrecondition.DeviceOpen =>
                deviceIsOpen is true
                    ? (true, "compat-callback-preconditions-ok")
                    : (false, $"device-not-open(isOpen={Fmt(deviceIsOpen)})"),
            // fail-closed：未声明前提的路线不得凭未审阅条件通过。
            _ => (false, "compat-precondition-undeclared"),
        };
        static string Fmt(bool? value) => value?.ToString() ?? "unknown";
    }

    /**
     * FAN-934 §4.A / R2 §3-A.2：非 ASUS 兼容路线（HC 真实回调合同）在开始操作前必须成立的前提。
     * 前提不成立 ⇒ 本操作不得结算成成功，也不得据此发布 release（fail-closed）。
     * 覆盖 HC 设备可能**静默 return** 的条件：仅"反射 Invoke 未抛异常"不足以证明回调真的执行。
     * 具体前提**按机型声明**（`FanRoute.Precondition`），委托上面的生产纯函数判定。
     */
    internal (bool Ok, string Detail) EvaluateCompatCallbackPreconditions()
    {
        var route = Route;
        if (route is null)
            return (false, "route-missing");
        var sessionReady = IsHcSessionReadyForControl(out var sessionReason, requireHcReportedOpen: false);
        return EvaluateCompatPreconditionsCore(
            route.Evidence,
            route.Precondition,
            sessionReady,
            sessionReason,
            device is null ? null : GetMember(device, "UseOpenLib") as bool?,
            device is null ? null : GetMember(device, "IsOpen") as bool?,
            device is null ? null : GetMember(device, "IsSupported") as bool?);
    }

    /**
     * FAN-934 §4.A：开始一次写入操作的证据作用域。只有 ASUS 路线（`AsusScopeReceipts`）
     * 取 ASUS 操作作用域；非 ASUS 的已审阅 HC 路线**不取** ASUS 作用域，只固化证据模式并
     * 核对回调前提。ASUS 路线运行时读不到 API ⇒ `receipt-api-fault`（unknown），
     * **不得**降成普通无 scope 兼容。
     */
    internal AsusWriteScopeHandle? BeginAsusWriteScope(string kind)
    {
        var mode = CurrentRouteEvidenceMode();
        if (mode != FanEvidenceMode.AsusScopeReceipts)
        {
            var (ok, detail) = EvaluateCompatCallbackPreconditions();
            return new AsusWriteScopeHandle
            {
                Scope = null,
                Kind = kind,
                Mode = mode,
                CompatPreconditionsOk = ok,
                CompatPreconditionDetail = detail,
            };
        }

        // HC-SLIM-01 R3 §4.2：provider=backend 时取叶组件自身的 AsusAcpiObservations；
        // 否则保持 HC 程序集查找。两类型都有 public static `BeginScope(string)`，结算链不变。
        var observationsType = ResolveAsusObservationsType();
        var begin = observationsType?.GetMethod("BeginScope", BindingFlags.Public | BindingFlags.Static);
        if (begin is null)
        {
            return new AsusWriteScopeHandle
            {
                Scope = null,
                Kind = kind,
                Mode = FanEvidenceMode.AsusScopeReceipts,
                ApiUnavailable = true,
            };
        }
        return new AsusWriteScopeHandle
        {
            Scope = begin.Invoke(null, [kind]),
            Kind = kind,
            Mode = FanEvidenceMode.AsusScopeReceipts,
        };
    }

    /**
     * E-3（FAN-204 §31 R4，2026-09-24 真机取证）：逐通道收据的**唯一**摘要格式（生产与自测共用）。
     * HC 侧 `AsusAcpiCallReceipt.Win32Error` 在失败时**已有值**（成功为 null），但宿主此前只输出枚举名
     * —— 真机因此只能看到 `Mid:InitiatedFailed` 而无法判因（"Mid 通道为什么失败"成为黑盒）。
     * 形状**向后兼容**：`{fan}:{status}` 前缀保持原样（Section19 的 `GPU:InitiatedFailed` 子串断言依赖它），
     * win32 以 `/win32=N` 追加在状态之后。
     */
    internal static string FormatFanReceiptObserved(string fan, string status, object? win32Error) =>
        win32Error is null ? $"{fan}:{status}" : $"{fan}:{status}/win32={win32Error}";

    /**
     * 结算本作用域并给出判定。**只消费本作用域**的收据；缺失记 unknown（不是 verified），
     * 明确失败（未发起/已发起失败）可判失败；IoException 是“是否发起未知”，既不是成功
     * 也不得描述成“写入一定未发生”。
     */
    internal AsusWriteVerdict CompleteAsusWriteScope(AsusWriteScopeHandle? handle, string operationKind)
    {
        if (handle is null)
            return new AsusWriteVerdict("unsupported", "candidate-lifecycle-scope-unsupported", false, false, false, 0);

        // ASUS 路线声明支持 scope 却读不到 API：如实记 unknown，不退成普通无 scope 兼容。
        if (handle.ApiUnavailable)
            return new AsusWriteVerdict("receipt-api-fault",
                $"operation={operationKind} asus-scope-api-absent", false, true, true, 0);

        List<object> receipts = [];
        try
        {
            if (handle.Scope?.GetType().GetMethod("TakeReceipts", BindingFlags.Public | BindingFlags.Instance)
                ?.Invoke(handle.Scope, null) is IEnumerable taken)
            {
                foreach (var item in taken)
                    if (item is not null)
                        receipts.Add(item);
            }
        }
        catch (Exception ex)
        {
            // 收据 API 自身异常 ⇒ unknown（不得读成"写入未发生"，也不得读成成功）。
            return new AsusWriteVerdict("receipt-api-fault",
                $"operation={operationKind} take-receipts-failed: {ex.GetType().Name}", false, true, true, 0);
        }
        finally
        {
            (handle.Scope as IDisposable)?.Dispose();
        }

        var fanReceipts = receipts
            .Where(item => (GetMember(item, "CallKind") as string ?? string.Empty).StartsWith("Fan", StringComparison.Ordinal))
            .ToList();
        var observed = fanReceipts
            .Select(item => FormatFanReceiptObserved(
                GetMember(item, "Fan")?.ToString() ?? "-",
                GetMember(item, "Status")?.ToString() ?? "<unknown>",
                GetMember(item, "Win32Error")))
            .ToList();
        var explicitFailures = new List<string>();
        var unknownFresh = new List<string>();
        var transported = 0;
        foreach (var item in fanReceipts)
        {
            var fan = GetMember(item, "Fan")?.ToString() ?? "-";
            var status = GetMember(item, "Status")?.ToString() ?? "<unknown>";
            switch (status)
            {
                case "NotInitiatedHandleInvalid":
                case "InitiatedFailed":
                    explicitFailures.Add($"{fan}:{status}");
                    break;
                case "IoException":
                    unknownFresh.Add($"{fan}:IoException(是否发起未知)");
                    break;
                case "TransportedShort":
                case "TransportedComplete":
                    transported++;
                    break;
                default:
                    unknownFresh.Add($"{fan}:{status}");
                    break;
            }
        }

        var detail =
            $"operation={operationKind} scope={(handle.Scope?.GetType().GetProperty("ScopeId")?.GetValue(handle.Scope) ?? "-")} " +
            $"receipts=[{string.Join(",", observed)}] explicitFailures=[{string.Join(",", explicitFailures)}] " +
            $"unknown=[{string.Join(",", unknownFresh)}] transported={transported}";

        if (fanReceipts.Count == 0)
            return new AsusWriteVerdict("no-receipts", $"no-this-operation-fan-receipts; {detail}", false, true, true, 0);
        // 2026-09-23 架构修正（operator 裁决：风扇按 HC 能力库语义运行）：
        // 能力库把这些收据定义为**传输观察**（InitiatedFailed = 已发起但 I/O 失败，带 win32 码），
        // 不是控制成败；HC 自身也忽略逐扇返回值（三通道写同一条曲线）。
        // 因此：只要**至少一个通道送达**，部分通道失败不构成致命失败——否则 RC73XA 这类
        // Mid 通道固件不接受的机器会被永久锁死（授权后 56ms ⇒ 30 连击 ⇒ 终态 FaultLocked）。
        // 但绝不写成成功：逐扇失败仍在 detail 里点名，Partial 单独成类，未知照记 unknown。
        if (explicitFailures.Count > 0 && transported > 0)
            return new AsusWriteVerdict("partial", detail, false, unknownFresh.Count > 0, false, fanReceipts.Count);
        if (explicitFailures.Count > 0 && unknownFresh.Count > 0)
            return new AsusWriteVerdict("mixed-failed-unknown", detail, true, true, transported == 0, fanReceipts.Count);
        if (explicitFailures.Count > 0)
            return new AsusWriteVerdict("explicit-failure", detail, true, false, transported == 0, fanReceipts.Count);
        if (unknownFresh.Count > 0)
            return new AsusWriteVerdict("io-exception-unknown", detail, false, true, transported == 0, fanReceipts.Count);
        return new AsusWriteVerdict("transported", detail, false, false, false, fanReceipts.Count);
    }

    /**
     * FAN-934 §4.A：按**路线声明的证据模式**决定本次操作的结算口径（生产与自测共用，纯函数）。
     * - ASUS：原样返回操作作用域收据判定（保留 partial／有据回退等既有语义）。
     * - 非 ASUS 已审阅 HC 路线：在会话/实例/路线/Open 前提成立且**真实回调已返回**后，
     *   产出 `compat-route-without-scope`（软件回调合同证据，**不是**物理成功或传输收据）。
     *   前提不成立 ⇒ `compat-route-precondition-unsatisfied`（unknown）；回调未返回 ⇒ 失败。
     * - 未审阅/不支持路线：**独立** unknown（`unregistered-route-no-scope`，FAN-934 R2 §3-A.1）——
     *   不复用历史 `unsupported` 兼容证书。旧映射把本默认分支的 `unsupported` 当"已审阅无 scope
     *   兼容"放行（`ClassifyReleaseAction` → CompatRouteWithoutScope → IsOemReleaseSafe=true），
     *   等于把"没有本次操作作用域的合格证据"判成安全。新默认结果必须 fail-closed 为 unknown。
     */
    internal static AsusWriteVerdict DecideOperationEvidence(
        FanEvidenceMode mode,
        string operationKind,
        bool callbackReturned,
        bool compatPreconditionsOk,
        string compatPreconditionDetail,
        AsusWriteVerdict asusVerdict)
    {
        switch (mode)
        {
            case FanEvidenceMode.AsusScopeReceipts:
                return asusVerdict;
            case FanEvidenceMode.CompatCallbackNoScope:
                if (!compatPreconditionsOk)
                    return new AsusWriteVerdict(
                        "compat-route-precondition-unsatisfied",
                        $"operation={operationKind} compat-preconditions-unsatisfied: {compatPreconditionDetail}",
                        false, true, false, 0);
                if (!callbackReturned)
                    return new AsusWriteVerdict(
                        "compat-route-callback-not-returned",
                        $"operation={operationKind} hc-callback-did-not-return",
                        true, false, true, 0);
                return new AsusWriteVerdict(
                    "compat-route-without-scope",
                    $"operation={operationKind} hc-compat-callback-returned-no-asus-scope-required",
                    false, false, false, 0);
            default:
                // FAN-934 R2 §3-A.1：未登记/未声明证据适用性的路线不得取得任何"合格证据"——
                // 独立 unknown kind，**不**复用历史 `unsupported` 兼容证书（后者曾把默认分支放行）。
                return new AsusWriteVerdict(
                    "unregistered-route-no-scope",
                    $"operation={operationKind} unregistered-route-no-scope",
                    false, true, false, 0);
        }
    }

    /**
     * FAN-934 §4.A：结算一次写入操作。ASUS 路线消费操作作用域收据；非 ASUS 路线按回调合同
     * 结算（不取 ASUS 作用域）。`callbackReturned` 由调用方在 HC 调用返回后置真——
     * 反射 Invoke 未抛异常**不等于**设备方法真的执行（GPD `UseOpenLib && IsOpen` 会静默 return），
     * 故非 ASUS 路线的前提核对在 `BeginAsusWriteScope` 已固化。
     */
    internal AsusWriteVerdict SettleOperationEvidence(
        AsusWriteScopeHandle? handle, string operationKind, bool callbackReturned)
    {
        var mode = handle?.Mode ?? FanEvidenceMode.UnsupportedNoScope;
        var asusVerdict = mode == FanEvidenceMode.AsusScopeReceipts
            ? CompleteAsusWriteScope(handle, operationKind)
            : new AsusWriteVerdict("unsupported", $"{operationKind}:non-asus-route-no-scope", false, false, false, 0);
        return DecideOperationEvidence(
            mode,
            operationKind,
            callbackReturned,
            handle?.CompatPreconditionsOk ?? false,
            handle?.CompatPreconditionDetail ?? "no-handle",
            asusVerdict);
    }

    /**
     * §19-A/B：候选 Close 的清理收据必须被 Host 消费——清理责任未结算（例如句柄释放被
     * 延迟给在途读取）时不得报告“关闭清理完成”。返回 false 时调用方保持
     * HC_CLOSE_PENDING，由既有有界重试窗口继续处理。旧 runtime（无该接口）返回 true。
     */
    internal bool ConfirmCandidateCloseCleanup(int timeoutMs, out string detail)
    {
        detail = "no-candidate-lifecycle-api";
        if (device is null)
            return true;
        var wait = device.GetType()
            .GetMethods(BindingFlags.Public | BindingFlags.Instance)
            .Where(m => m.Name == "TryWaitCloseCleanupCompleted")
            .Where(m => m.GetParameters() is [{ ParameterType: var p0 }, { ParameterType: var p1 }]
                        && p0 == typeof(int) && p1.IsByRef)
            .FirstOrDefault();
        if (wait is null)
            return true;
        var args = new object?[] { timeoutMs, null };
        var settled = wait.Invoke(device, args) is true;
        detail = args[1]?.ToString() ?? "<none>";
        // G9 / M5（HC 收据字段全集）：`detail` 是 HC 生命周期收据的 Describe()，其
        // `released` / `tableClearVerified` / `remapReleased` / `complete` 等字段的语义
        // **只限 HC 生命周期资源**（绑定表项、HID 句柄实例、controller remap），源码原文即
        // "调用方不得据此报告『资源已清理』"。这些字段名极易被读成"风扇已交还"，而风扇交还
        // 证据只存在于 `OemReleaseActionKind`。⇒ 同拍显式给出范围，不靠读者推断。
        Diagnostic?.Invoke(settled ? "hc-close.cleanup-settled" : "hc-close.cleanup-pending",
            new
            {
                detail,
                releasedScope = "hc-lifecycle-only",
                fanReleaseEvidence = "not-carried-by-this-event; see OemReleaseActionKind",
            });
        return settled;
    }

    /** 生产 Enable 请求体（不带 lease，由自测夹具授予）。 */
    private static JsonElement BuildEnableBody(string leaseId) =>
        JsonDocument.Parse(
            $$"""
            {
              "leaseId": "{{leaseId}}",
              "nodes": [
                { "tempC": 0,   "dutyPercent": 0 },
                { "tempC": 40,  "dutyPercent": 35 },
                { "tempC": 70,  "dutyPercent": 70 },
                { "tempC": 100, "dutyPercent": 100 }
              ]
            }
            """).RootElement.Clone();

    /** 从（可能被 STA 派发包装的）异常里取出可复核的故障标识。 */
    private static string FaultCodeOf(Exception exception)
    {
        for (Exception? current = exception; current is not null; current = current.InnerException)
        {
            if (current is FanApiException api) return api.Code;
            if (current.Message.StartsWith("HC OEM restore write did not happen", StringComparison.Ordinal))
                return "OEM_RESTORE_WRITE_NOT_HAPPENED";
        }
        return $"<exception:{exception.GetType().Name}>";
    }

    // ======================================================================
    // 自测钩子（只被 §18 组使用；不改变生产行为）
    // ======================================================================

    /** 把隔离候选 DLL 装配到本 backend（与生产同一条 LoadFromAssemblyPath 路径）。 */
    internal string InstallCandidateAssemblyForSelfTest(string candidateAssemblyPath)
    {
        var fullPath = Path.GetFullPath(candidateAssemblyPath);
        if (!File.Exists(fullPath))
            throw new FileNotFoundException("candidate HC assembly not found", fullPath);

        var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(fullPath))).ToLowerInvariant();
        assemblyDirectory = Path.GetDirectoryName(fullPath);
        hcRuntimeDirectory = assemblyDirectory!;
        AssemblyLoadContext.Default.Resolving += ResolveDependency;
        assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(fullPath);
        deviceType = assembly.GetType("HandheldCompanion.Devices.IDevice", throwOnError: true);
        // 与生产加载同一步：初始化 HC LogManager/路径（候选设备构造会触达 ManagerFactory/LogManager）。
        ConfigureHcRuntime();
        candidateReflection = new CandidateReflection(assembly);
        return hash;
    }

    /** 把候选设备实例接到生产 backend 字段上（生产切换点：device / sessionDeviceReference）。 */
    internal void InstallCandidateDeviceForSelfTest(object candidateDevice, bool markSessionOpen)
    {
        device = candidateDevice;
        sessionDeviceReference = candidateDevice;
        hcCoreInitialized = true;
        openAttempted = true;
        // Route 由 FactoryType(=设备类型全名) 解析；未登记的候选设备类型必须显式失败。
        if (!FanRoutes.ContainsKey(candidateDevice.GetType().FullName ?? string.Empty))
            throw new InvalidOperationException($"no Host route for candidate device {candidateDevice.GetType().FullName}");
        CaptureOemBaseline();
        sessionRouteMarker = BuildSessionRouteMarker();
        if (markSessionOpen)
            IsOpen = true;
    }

    /**
     * FAN-934 R2 §3-A.2：仅自测使用——把一条**已审阅路线**与一个 mock 设备显式接给 backend，
     * 让正反例驱动**真实**的生产函数链 `EvaluateCompatCallbackPreconditions` →
     * `BeginAsusWriteScope` → `SettleOperationEvidence`（不再手填判定布尔值/结论串）。
     * 直接按生产 `LoadAndHandshakeCore` 完成后的会话字段形态铺设身份，**不**调用
     * `CaptureOemBaseline`（那会触达裸 mock 上并不存在的 HC profile/EC 接口）。
     * 生产路径恒不使用本方法（只有 `routeOverrideForSelfTest` 的 seam 会被打开）。
     */
    internal void InstallSelfTestRouteSessionForSelfTest(object mockDevice, FanRoute route, bool backendOpen)
    {
        device = mockDevice;
        sessionDeviceReference = mockDevice;
        routeOverrideForSelfTest = route;
        hcCoreInitialized = true;
        openAttempted = true;
        oemBaselineCaptured = true;
        baselineStrategy = route.RestoreStrategy;
        baselineRestoreRoute = mockDevice.GetType().FullName;
        sessionRouteMarker = BuildSessionRouteMarker();
        IsOpen = backendOpen;
        // FAN-936 §5：自测会话同样要有一个**有效基线绑定**（生产在 `CaptureOemBaseline()` 末尾
        // 捕获）。这里直接按同一四项（设备/实例/周期/boot identity）落定，使 `RestoreOem()` /
        // `ApplyCurve()` 的绑定门在自测里也真实生效，而不是被"未捕获"一律拦掉。
        CaptureBaselineBinding();
    }

    /**
     * FAN-934 R2 §3-B：唯一自测 HC 边界（默认关闭）。把三处不可能在自测环境复现的 HC 依赖
     * （ManagerFactory 图观测、FanMode/FanProfile 构造、UpdateSource/PowerProfileManager 回调）
     * 收敛到一个计数器式实现，使 GPD/MSI 的**生产恢复入口**（`RestoreOem()` / `Close()`）能在
     * 无 HC 程序集、无真实 EC/WMI 的前提下被状态化驱动，并如实记录回调调用计数与调用方。
     * 生产路径恒为 null（`OpenCore`/`ApplyCurve`/`RestoreOem` 都不写入本字段）。
     */
    internal SelfTestHcBoundary? hcBoundaryForSelfTest;

    internal sealed class SelfTestHcBoundary
    {
        /** 生产代码经过 HC 边界构造 power-profile 的次数（`BuildPowerProfile` seam）。 */
        public int ProfileBuildCount { get; private set; }
        /** 生产代码经过**唯一** HC 回调边界的次数（被路线守卫拦下的操作不会递增）。 */
        public int CallbackInvocationCount { get; private set; }
        /** 每次回调实际收到的 source（生产 `NormalizeUpdateSourceName` 之后的取值）。 */
        public List<string> CallbackSources { get; } = new();
        /** 置位后模拟 HC 回调抛出；`ApplyPowerProfile` 会原样 rethrow（不洗成成功）。 */
        public Exception? CallbackFailure { get; set; }
        /** false 时模拟 ManagerFactory 图不可读 ⇒ Close 清理判定必须 fail-closed。 */
        public bool ManagerObservationAvailable { get; set; } = true;

        /**
         * FAN-936 R4：释放收据的结论状态（负例可置 failed/unknown 复现真实回调结论）。R2 section 2
         * 后 MSI 交还走窄接口，收据由 mock 的 `ReleaseMsiFanControl` 工厂按这三项产出。
         */
        public string ReleaseReceiptStatus { get; set; } = "ok";
        public string ReleaseReceiptFullSpeedStatus { get; set; } = "ok";
        public string ReleaseReceiptSoftwareStatus { get; set; } = "ok";

        public object BuildProfile(double[] duties, bool software)
        {
            ProfileBuildCount++;
            return new SelfTestHcProfile(new SelfTestHcFanProfile
            {
                fanSpeeds = duties.Length == 0 ? Array.Empty<double>() : (double[])duties.Clone(),
                fanMode = software ? "Software" : "Hardware",
            });
        }

        public void Apply(object profile, string sourceName)
        {
            CallbackInvocationCount++;
            CallbackSources.Add(sourceName);
            if (CallbackFailure is not null) throw CallbackFailure;
        }
    }

    /** HC `PowerProfile` 反射形状替身（`GetMember` 只按名字读 `FanProfile`）。 */
    internal sealed class SelfTestHcProfile
    {
        public SelfTestHcProfile(SelfTestHcFanProfile fanProfile) => FanProfile = fanProfile;
        public SelfTestHcFanProfile FanProfile { get; }
    }

    /** HC `FanProfile` 反射形状替身（`GetMember` 只按名字读 `fanSpeeds`/`fanMode`）。 */
    internal sealed class SelfTestHcFanProfile
    {
        public double[] fanSpeeds = Array.Empty<double>();
        public string fanMode = "Hardware";
    }

    /** 仅自测：把 HC 边界接给 backend（生产路径恒不调用）。 */
    internal void InstallHcBoundaryForSelfTest(SelfTestHcBoundary boundary) =>
        hcBoundaryForSelfTest = boundary;

    /** 仅自测：替换当前设备引用，复现"错会话/设备身份漂移"。 */
    internal void SwapDeviceForSelfTest(object otherDevice) => device = otherDevice;

    /** 仅自测：Close 之后重新打开同一会话（生产由 `Open` 完成）。 */
    internal void ReopenSelfTestSessionForSelfTest(object mockDevice, FanRoute route, bool backendOpen)
    {
        InstallSelfTestRouteSessionForSelfTest(mockDevice, route, backendOpen);
        closed = false;
    }

    /** 仅自测：直驱生产 `BuildPowerProfile`（`public ApplyCurve` 的真实授权门自测不可过）。 */
    internal object BuildPowerProfileForSelfTest(double[] duties, bool software) =>
        BuildPowerProfile(duties, software);

    /**
     * FAN-936 §5：暴露本进程 boot identity 供自测构造"绑定本次开机"的 MSI 结构化释放夹具
     * （生产侧 `machineBootIdentity` 是 private static，自测类读不到）。生产路径不使用。
     */
    internal static string MachineBootIdentityForSelfTest => machineBootIdentity;

    /**
     * FAN-936 §5：仅自测——把**已捕获基线**的 boot identity 打旧，复现"上一开机的快照不得再被
     * 用于写入/交还"。生产路径不调用（`baselineBootIdentity` 只在 `CaptureBaselineBinding()`
     * 中按 `machineBootIdentity` 落定）。
     */
    internal void ForceStaleBaselineBootIdentityForSelfTest() =>
        baselineBootIdentity = "1999-01-01T00:00:00Z";

    /** FAN-936 §5：仅自测——把基线绑定的 Host 实例打旧（复现"上一个 Host 实例的基线被复用"）。 */
    internal void ForceStaleBaselineHostInstanceForSelfTest() =>
        baselineHostInstanceId = "stale-host-instance";

    /**
     * FAN-936 R2 §5：仅自测——接上路线会话后把**基线捕获标志**退回未捕获，复现"MSI 路线已准入但
     * 尚未落定基线绑定"的写入/交还准入。生产路径不调用（`oemBaselineCaptured` 只在
     * `CaptureOemBaseline()` 成功末尾置真）。
     */
    internal void ClearBaselineCaptureForSelfTest() => oemBaselineCaptured = false;

    /** FAN-936 §5：仅自测——复现"上一次 HC 调用已超时未返回"，断言延迟 WMI 期间不得并发恢复。 */
    internal void MarkOperationTimedOutForSelfTest() => operationTimedOut = true;

    /** 仅自测：直驱生产 `ApplyPowerProfile`（走同一条唯一 HC 回调边界）。 */
    internal void ApplyPowerProfileForSelfTest(object profile, string sourceName = "Background") =>
        ApplyPowerProfile(profile, sourceName);

    // ======================================================================
    // §18 矩阵
    // ======================================================================

    internal static IReadOnlyList<string> RunSection18CandidateSelfTest(FanHostEngine engine, List<string> checks)
    {
        var candidatePath = engine.Options.HcCandidateAssembly;
        if (string.IsNullOrWhiteSpace(candidatePath))
            return checks;
        using var backend = new RealHcBackend(engine.Options);
        var candidateHash = backend.InstallCandidateAssemblyForSelfTest(candidatePath);
        var reflection = backend.candidateReflection
            ?? throw new InvalidOperationException("candidate reflection bridge missing");
        checks.Add($"candidate-identity sha256={candidateHash} mvid={reflection.ModuleVersionId} path={Path.GetFullPath(candidatePath)}");

        // 旧 runtime 如实报告不支持（负例）：没有版本化能力的对象不得被当成候选。
        if (backend.TryWaitCandidateLifecycleCompletion(new object(), 10, out _, out var unsupportedDetail))
            throw new InvalidOperationException("lifecycle completion must be reported unsupported for a plain object");
        checks.Add($"candidate-lifecycle-unsupported-detected detail={unsupportedDetail}");

        var fakes = new CandidateFakes(reflection);
        reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
        reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        // 候选设备在 **STA 派发线程**上构造（与生产 HC 的设备创建/事件派发线程一致；
        // WPF 相关依赖在非 STA 线程上会初始化失败）。
        var device = backend.OnStaBounded(reflection.CreateRogAlly, "s18-create-candidate-device");
        fakes.Boundary.HcDevice = device;
        // 只观察真实 HC 的按键派发（不复制分发逻辑）：迟到报告若被派发，计数会变。
        reflection.SubscribeKeyPressed(device, fakes.Boundary.NoteKeyDispatch);
        checks.Add($"candidate-lifecycle-api version={reflection.GetInt(device, "LifecycleApiVersion")} " +
                   $"observationsApi={reflection.GetIntStatic("HandheldCompanion.Devices.ASUS.AsusAcpiObservations", "ApiVersion")}");
        if (reflection.GetInt(device, "LifecycleApiVersion") != 1)
            throw new InvalidOperationException("candidate LifecycleApiVersion must be 1");

        // ---- S0：正常打开（真实完成 + 收据归属 + 兼容事件入口观察完成）----
        OpenFreshSession(fakes, reflection, backend, device);
        var compatOperationId = reflection.GetString(device, "LastCompatEntryOperationId")
            ?? throw new InvalidOperationException("compat entry did not record an operation id");
        var receipt = reflection.WaitOpenReceipt(device, compatOperationId, 10_000)
            ?? throw new InvalidOperationException("normal open operation did not reach a terminal receipt");
        if (reflection.GetString(receipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"normal open outcome={reflection.Describe(receipt)}");
        if (!reflection.GetBool(receipt, "HidOpenedByThisOperation") ||
            !reflection.GetBool(receipt, "ReadLoopStartedByThisOperation") ||
            !reflection.GetBool(receipt, "RemapAppliedByThisOperation") ||
            !reflection.GetBool(receipt, "DeviceBoundByThisOperation"))
            throw new InvalidOperationException($"normal open receipt did not own its side effects: {reflection.Describe(receipt)}");
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("read loop did not enter the pending read after a normal open");
        if (fakes.Boundary.HidOpens != 1 || fakes.Boundary.RemapOnCount != 1 || fakes.Boundary.ReadCalls != 1)
            throw new InvalidOperationException(
                $"normal open side-effect facts hidOpens={fakes.Boundary.HidOpens} remapOn={fakes.Boundary.RemapOnCount} reads={fakes.Boundary.ReadCalls}");
        if (reflection.GetBool(device, "IsOpen") != true || reflection.GetString(device, "ReadLoopState") != "Running")
            throw new InvalidOperationException(
                $"candidate device did not become open with a running read loop " +
                $"isOpen={reflection.GetBool(device, "IsOpen")} readLoop={reflection.GetString(device, "ReadLoopState")} " +
                $"bindPublications={fakes.Boundary.BindPublications} hidOpens={fakes.Boundary.HidOpens} reads={fakes.Boundary.ReadCalls} " +
                $"acpiOpen={reflection.GetBoolStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "IsOpen")} " +
                $"logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(4))}]");
        checks.Add(
            $"candidate-open[normal] outcome=Completed effectsOwned=true hidOpens=1 remapOn=1 reads=1 " +
            $"deviceIsOpen=true readLoop=Running op={compatOperationId}");

        // ---- S1：重复到达合并（不重复副作用）----
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.BlockProbe = true;
        // 该交错只驱动候选生命周期本体（HID/ACPI 均为替身），不占 STA 队列。
        var duplicateWorker = Task.Run(() => reflection.StartDeviceOpen(device, true));
        if (!duplicateWorker.Wait(5_000))
            throw new InvalidOperationException("duplicate-arrival fixture did not register its operation");
        var duplicateFirst = duplicateWorker.Result;
        if (!fakes.Boundary.WaitProbeEntered(5_000))
            throw new InvalidOperationException("duplicate-arrival fixture did not block inside the ready probe");
        var duplicateSecond = reflection.StartDeviceOpen(device, true);
        fakes.Boundary.BlockProbe = false;
        if (!duplicateWorker.Wait(10_000))
            throw new InvalidOperationException("duplicate-arrival worker did not return");
        var mergedReceipt = reflection.WaitOpenReceipt(device, duplicateFirst, 10_000)
            ?? throw new InvalidOperationException("first duplicate-arrival operation did not complete");
        if (!fakes.Boundary.WaitReadEntered(5_000))
            throw new InvalidOperationException("duplicate-arrival fixture did not reach a live read loop");

        // 合同允许两种形态：合并到在途操作，或受控拒绝（AlreadyOpen）。两者都必须**不重复副作用**。
        string shape;
        if (duplicateSecond == duplicateFirst)
        {
            if (!reflection.GetBool(mergedReceipt, "MergedDuplicateArrival"))
                throw new InvalidOperationException(
                    $"duplicate arrival reused the operation id but was not recorded as merged: {reflection.Describe(mergedReceipt)}");
            shape = "merged-into-in-flight-operation";
        }
        else
        {
            var rejected = reflection.WaitOpenReceipt(device, duplicateSecond, 10_000)
                ?? throw new InvalidOperationException("second duplicate-arrival operation did not complete");
            shape = "controlled-rejected:" + reflection.GetString(rejected, "Outcome");
            if (reflection.GetString(rejected, "Outcome") != "AlreadyOpen")
                throw new InvalidOperationException(
                    $"duplicate arrival was neither merged nor controlled-rejected: first={duplicateFirst} " +
                    $"second={duplicateSecond} firstReceipt=[{reflection.Describe(mergedReceipt)}] secondReceipt=[{reflection.Describe(rejected)}] " +
                    $"bound={fakes.Boundary.BindPublications} hidOpens={fakes.Boundary.HidOpens} reads={fakes.Boundary.ReadCalls}");
        }

        if (fakes.Boundary.HidOpens != 1 || fakes.Boundary.RemapOnCount != 1 || fakes.Boundary.ReadCalls != 1)
            throw new InvalidOperationException(
                $"duplicate arrival duplicated side effects hidOpens={fakes.Boundary.HidOpens} remapOn={fakes.Boundary.RemapOnCount} reads={fakes.Boundary.ReadCalls}");
        if (reflection.GetString(device, "ReadLoopState") != "Running")
            throw new InvalidOperationException(
                $"duplicate arrival left the read loop in state {reflection.GetString(device, "ReadLoopState")} " +
                $"reads={fakes.Boundary.ReadCalls} logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(3))}]");
        checks.Add(
            $"candidate-open[duplicate-arrival] shape={shape} op={duplicateFirst} hidOpens=1 remapOn=1 readLoops=1 " +
            $"effects={reflection.Describe(mergedReceipt)}");

        // ---- S2：裁决方交错① —— 准入通过、副作用未执行时 Close ----
        RunCloseBetweenAdmissionAndSideEffect(fakes, reflection, backend, device, checks);

        // ---- S3：裁决方交错② —— 旧请求不得借用新会话状态 ----
        RunOldRequestAfterNewSession(fakes, reflection, backend, device, checks);

        // ---- S4：未决 HID I/O 与迟到报告 ----
        RunPendingHidIo(fakes, reflection, backend, device, checks);

        // ---- S5：部分副作用后异常（归属 + 下一轮仍可用）----
        RunPartialSideEffectFault(fakes, reflection, backend, device, checks);

        // ---- S6：旧清理不得停止新任务 ----
        RunOldCleanupKeepsNewSession(fakes, reflection, backend, device, checks);

        // ---- S7：AsusACPI 逐调用回执矩阵 ----
        RunAcpiReceiptMatrix(fakes, reflection, checks);

        // ---- S8：生产 engine 的故障→恢复有效配置 + 阻塞读链 ----
        RunProductionEngineFlow(engine, candidatePath, candidateHash, reflection, checks);

        // 收尾：真实 Close（会话失效、remap 交还、设备释放）并等待读取循环真实结束。
        fakes.Boundary.CurrentDeviceState.Connected = false;
        fakes.Boundary.CurrentDeviceState.Open = false;
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        backend.OnStaBounded(() => { reflection.CallInstance(device, "Close"); return true; }, "s18-final-close");
        for (var attempt = 0; attempt < 3 && !reflection.WaitReadLoopEnded(device, 500, out _); attempt++)
            fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        var loopEnded = reflection.WaitReadLoopEnded(device, 5_000, out var loopDetail);
        if (!loopEnded)
            throw new InvalidOperationException(
                $"read loop did not end after close: {loopDetail} state={reflection.GetString(device, "ReadLoopState")} " +
                $"logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(3))}]");
        checks.Add($"candidate-close[takeover-ready] readLoopEnded=true detail={loopDetail}");
        return checks;
    }

    /** 新会话 + 正常打开（每个场景的确定性起点；HC 调用走 STA 派发线程，与生产同路径）。 */
    private static void OpenFreshSession(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        bool dispatchCompatibilityEntry = true)
    {
        // 先把设备标记为“已拔出”，再完成未决读取：旧读取循环会因设备不可用而结束
// （不会退避重读），随后 Close 才会真正释放它。
        fakes.Boundary.CurrentDeviceState.Connected = false;
        fakes.Boundary.CurrentDeviceState.Open = false;
        fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        backend.OnStaBounded(() => { reflection.CallInstance(device, "Close"); return true; }, "s18-scenario-close");
        for (var attempt = 0; attempt < 3 && !reflection.WaitReadLoopEnded(device, 500, out _); attempt++)
            fakes.Boundary.CompletePendingReads(failed: true, key: 0);
        reflection.WaitReadLoopEnded(device, 2_000, out var drainDetail);
        if (reflection.GetString(device, "ReadLoopState") == "Running")
            throw new InvalidOperationException($"old read loop did not end before the next scenario: {drainDetail}");
        fakes.Boundary.ResetForNewScenario();
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        fakes.Boundary.AutoBindOnProbe = true;
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true)
                throw new InvalidOperationException("candidate AsusACPI.Open returned false");
            if (reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("candidate ROGAlly.Open returned false");
            if (dispatchCompatibilityEntry)
                reflection.DispatchDeviceInserted(device);
            return true;
        }, "s18-scenario-open");

        if (!dispatchCompatibilityEntry)
            return;

        // 兼容事件入口派发后，等待该操作**真实完成**再进入下一步
        // （否则计数器复位会与操作的最后几个副作用步骤竞争）。
        var operationId = reflection.GetString(device, "LastCompatEntryOperationId")
            ?? throw new InvalidOperationException("compat entry did not record an operation id");
        var receipt = reflection.WaitOpenReceipt(device, operationId, 10_000)
            ?? throw new InvalidOperationException("scenario open operation did not reach a terminal receipt");
        if (reflection.GetString(receipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"scenario open outcome={reflection.Describe(receipt)}");
    }

    // ---- S2 ----
    private static void RunCloseBetweenAdmissionAndSideEffect(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.BlockProbe = true;
        // 操作在后台线程进入“已准入、副作用未执行”的状态，测试线程在此期间执行 Close。
        var worker = Task.Run(() => reflection.StartDeviceOpen(device, true));
        if (!worker.Wait(5_000))
            throw new InvalidOperationException("close-interleave fixture did not register its operation");
        var operationId = worker.Result;
        if (!fakes.Boundary.WaitProbeEntered(5_000))
            throw new InvalidOperationException("close-interleave fixture did not block inside the ready probe");
        var receiptsBeforeClose = reflection.DescribeAllReceipts(device);

        // Close 落在“准入已通过、副作用尚未开始”的窗口内。
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        if (reflection.GetBool(closeReceipt, "SessionInvalidated") != true)
            throw new InvalidOperationException("close did not invalidate the session");
        if (fakes.Boundary.HidOpens != 0 || fakes.Boundary.RemapOnCount != 0 || fakes.Boundary.ReadCalls != 0)
            throw new InvalidOperationException(
                $"close observed side effects that must not exist hudOpens={fakes.Boundary.HidOpens} remapOn={fakes.Boundary.RemapOnCount} reads={fakes.Boundary.ReadCalls}");

        fakes.Boundary.BlockProbe = false;
        if (!worker.Wait(10_000))
            throw new InvalidOperationException("interleaved operation worker did not return");
        var receipt = reflection.WaitOpenReceipt(device, operationId, 10_000)
            ?? throw new InvalidOperationException("interleaved operation did not reach a terminal receipt");
        var outcome = reflection.GetString(receipt, "Outcome")!;
        if (outcome != "AbortedBySession")
            throw new InvalidOperationException(
                $"interleaved operation outcome={outcome} {reflection.Describe(receipt)} receipts(before close)=[{receiptsBeforeClose}] " +
                $"receipts(now)=[{reflection.DescribeAllReceipts(device)}]");
        if (fakes.Boundary.HidOpens != 0 || fakes.Boundary.RemapOnCount != 0 || fakes.Boundary.ReadCalls != 0 ||
            reflection.GetBool(receipt, "HidOpenedByThisOperation"))
            throw new InvalidOperationException(
                $"post-Close side effects leaked hidOpens={fakes.Boundary.HidOpens} remapOn={fakes.Boundary.RemapOnCount} " +
                $"reads={fakes.Boundary.ReadCalls} receipt={reflection.Describe(receipt)}");
        checks.Add(
            $"candidate-open[close-between-admission-and-mutation] sessionInvalidated=true postCloseSideEffects=0 " +
            $"outcome=AbortedBySession op={operationId}");
    }

    // ---- S3 ----
    private static void RunOldRequestAfterNewSession(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.BlockProbe = true;

        // 旧请求在后台线程进入“已准入、副作用未执行”的状态。
        var worker = Task.Run(() => reflection.StartDeviceOpen(device, true));
        if (!worker.Wait(5_000))
            throw new InvalidOperationException("old-session fixture did not register its operation");
        var oldOperationId = worker.Result;
        if (!fakes.Boundary.WaitProbeEntered(5_000))
            throw new InvalidOperationException("old-session fixture did not block inside the ready probe");

        reflection.CloseDeviceLifecycle(device);
        // 新会话：重新 Open（新代次，STA 派发线程上，与生产一致）并由新操作打开设备。
        backend.OnStaBounded(() =>
        {
            if (reflection.CallStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "Open") is not true)
                throw new InvalidOperationException("candidate AsusACPI.Open (new session) returned false");
            if (reflection.CallInstance(device, "Open") is not true)
                throw new InvalidOperationException("candidate ROGAlly.Open (new session) returned false");
            return true;
        }, "s18-new-session-open");
        fakes.Boundary.BlockProbe = false;
        var newOperationId = reflection.StartDeviceOpen(device, true);
        var newReceipt = reflection.WaitOpenReceipt(device, newOperationId, 10_000)
            ?? throw new InvalidOperationException("new-session operation did not complete");
        if (reflection.GetString(newReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"new-session open outcome={reflection.Describe(newReceipt)}");
        if (!worker.Wait(10_000))
            throw new InvalidOperationException("old-session worker did not return");

        var oldReceipt = reflection.WaitOpenReceipt(device, oldOperationId, 10_000)
            ?? throw new InvalidOperationException("old operation did not reach a terminal receipt");
        var oldOutcome = reflection.GetString(oldReceipt, "Outcome")!;
        if (oldOutcome != "AbortedBySession")
            throw new InvalidOperationException($"old operation outcome={oldOutcome} {reflection.Describe(oldReceipt)}");
        if (reflection.GetBool(oldReceipt, "HidOpenedByThisOperation") ||
            reflection.GetBool(oldReceipt, "ReadLoopStartedByThisOperation") ||
            reflection.GetBool(oldReceipt, "RemapAppliedByThisOperation"))
            throw new InvalidOperationException($"old receipt borrowed the new session state: {reflection.Describe(oldReceipt)}");

        var newSessionGeneration = reflection.GetLong(newReceipt, "SessionGeneration");
        var oldSessionGeneration = reflection.GetLong(oldReceipt, "SessionGeneration");
        if (newSessionGeneration <= oldSessionGeneration)
            throw new InvalidOperationException(
                $"new session generation {newSessionGeneration} must exceed old {oldSessionGeneration}");
        checks.Add(
            $"candidate-open[old-request-after-new-session] old=AbortedBySession oldEffects=0 " +
            $"oldGen={oldSessionGeneration} newGen={newSessionGeneration} new=Completed newOwned=true");
    }

    // ---- S4 ----
    private static void RunPendingHidIo(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        if (!fakes.Boundary.WaitReadEntered(3_000))
            throw new InvalidOperationException("read loop did not enter the pending read");
        var readsBeforeClose = fakes.Boundary.ReadCalls;
        if (readsBeforeClose < 1)
            throw new InvalidOperationException($"expected a live pending read after open, got {readsBeforeClose}");
        if (reflection.GetString(device, "ReadLoopState") != "Running")
            throw new InvalidOperationException(
                $"read loop not running before close: {reflection.GetString(device, "ReadLoopState")}");

        var hidOpensAtClose = fakes.Boundary.HidOpens;
        var closeReceipt = reflection.CloseDeviceLifecycle(device);
        var pendingIsolated = reflection.GetBool(closeReceipt, "ReadLoopPendingIsolated");
        if (!pendingIsolated)
            throw new InvalidOperationException($"pending read must be reported as isolated pending: {reflection.Describe(closeReceipt)}");
        if (fakes.Boundary.ReadCalls != readsBeforeClose)
            throw new InvalidOperationException(
                $"read loop issued another read after close: before={readsBeforeClose} after={fakes.Boundary.ReadCalls}");

        // 迟到报告：会话已失效，报告不得被派发为按键事件；循环必须结束；不得重新打开设备。
        var keysBefore = fakes.Boundary.KeyDispatches;
        fakes.Boundary.CompletePendingReads(failed: false, key: 166);
        var loopEnded = reflection.WaitReadLoopEnded(device, 5_000, out var loopDetail);
        if (!loopEnded)
            throw new InvalidOperationException(
                $"read loop did not end after the pending read returned: {loopDetail} reads={fakes.Boundary.ReadCalls} " +
                $"keys={fakes.Boundary.KeyDispatches} loopState={reflection.GetString(device, "ReadLoopState")} " +
                $"logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(4))}]");
        if (fakes.Boundary.KeyDispatches != keysBefore)
            throw new InvalidOperationException("late report after session invalidation was dispatched as a key event");
        if (fakes.Boundary.HidOpens != hidOpensAtClose)
            throw new InvalidOperationException(
                $"read loop re-opened the device after close: hidOpens={fakes.Boundary.HidOpens} atClose={hidOpensAtClose}");
        checks.Add(
            $"candidate-open[pending-hid-io] closePendingIsolated=true loopEnded=true lateReportDropped=true " +
            $"readsStable={fakes.Boundary.ReadCalls} hidOpensAfterClose=0 detail={loopDetail}");
    }

    // ---- S5 ----
    private static void RunPartialSideEffectFault(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        fakes.Boundary.ResetCountersKeepSession();
        fakes.Boundary.ThrowOnRemap = 1;

        var operationId = reflection.StartDeviceOpen(device, true);
        var receipt = reflection.WaitOpenReceipt(device, operationId, 10_000)
            ?? throw new InvalidOperationException("fault operation did not reach a terminal receipt");
        var outcome = reflection.GetString(receipt, "Outcome")!;
        if (outcome != "PartiallyApplied")
            throw new InvalidOperationException($"fault operation outcome={outcome} {reflection.Describe(receipt)}");
        if (!reflection.GetBool(receipt, "HidOpenedByThisOperation") ||
            !reflection.GetBool(receipt, "ReadLoopStartedByThisOperation"))
            throw new InvalidOperationException($"fault receipt lost the side effects that did happen: {reflection.Describe(receipt)}");
        var faultType = reflection.GetString(receipt, "FaultType");
        if (string.IsNullOrEmpty(faultType))
            throw new InvalidOperationException("fault receipt did not record the fault type");
        var uncertain = reflection.GetString(receipt, "UncertainSideEffects");
        if (string.IsNullOrEmpty(uncertain) || uncertain == "None")
            throw new InvalidOperationException($"fault receipt must mark the uncertain side effect: {reflection.Describe(receipt)}");

        // 下一轮：新会话 + 新操作仍可完成（兼容事件入口没有把异常逃逸成进程级失败）。
        OpenFreshSession(fakes, reflection, backend, device, dispatchCompatibilityEntry: false);
        var nextId = reflection.StartDeviceOpen(device, true);
        var nextReceipt = reflection.WaitOpenReceipt(device, nextId, 10_000)
            ?? throw new InvalidOperationException("post-fault operation did not complete");
        if (reflection.GetString(nextReceipt, "Outcome") != "Completed")
            throw new InvalidOperationException($"post-fault outcome={reflection.Describe(nextReceipt)}");
        checks.Add(
            $"candidate-open[partial-side-effect-fault] outcome=PartiallyApplied fault={faultType} " +
            $"uncertain={uncertain} effects={reflection.GetString(receipt, "SideEffects")} next=Completed");
    }

    // ---- S6 ----
    private static void RunOldCleanupKeepsNewSession(
        CandidateFakes fakes,
        CandidateReflection reflection,
        RealHcBackend backend,
        object device,
        List<string> checks)
    {
        OpenFreshSession(fakes, reflection, backend, device);
        // “旧设备”是与当前会话无关的身份（旧清理不得作用于新会话的设备/任务）。
        var staleIdentity = "stale-hid-device" + Guid.NewGuid().ToString("N")[..8];
        // 旧设备（不存在的身份）移除事件：不得停止当前会话的读取循环。
        reflection.HandleDeviceRemoved(device, fakes.Boundary.CreateRemovedDeviceProxy(staleIdentity));
        var loopStateAfterStaleRemoval = reflection.GetString(device, "ReadLoopState");
        if (loopStateAfterStaleRemoval is not ("Running" or "Ended"))
            throw new InvalidOperationException($"unexpected loop state after stale removal: {loopStateAfterStaleRemoval}");

        var keysBefore = fakes.Boundary.KeyDispatches;
        fakes.Boundary.PushReport(key: 147);
        if (!fakes.Boundary.WaitForKeyDispatch(3_000))
            throw new InvalidOperationException("new session read loop did not dispatch a report after stale-device cleanup");
        var loopStateAfterReport = reflection.GetString(device, "ReadLoopState");
        checks.Add(
            $"candidate-open[old-cleanup-new-session] staleIdentity={staleIdentity} " +
            $"loopStateAfterStaleRemoval={loopStateAfterStaleRemoval} newReportDispatched=true " +
            $"keysBefore={keysBefore} keysAfter={fakes.Boundary.KeyDispatches} loopState={loopStateAfterReport}");
    }

    // ---- S7 ----
    private static void RunAcpiReceiptMatrix(
        CandidateFakes fakes,
        CandidateReflection reflection,
        List<string> checks)
    {
        // 句柄无效 = 未发起（不是成功）。
        fakes.Acpi.Reset(CandidateAcpiMode.InvalidHandle);
        var invalid = reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        if (reflection.GetString(invalid, "Status") != "NotInitiatedHandleInvalid" ||
            reflection.GetBool(invalid, "Initiated"))
            throw new InvalidOperationException($"invalid-handle receipt wrong: {reflection.Describe(invalid)}");
        if (fakes.Acpi.ControlCalls != 0)
            throw new InvalidOperationException("invalid handle must not reach the OS transport");

        // I/O 失败 + Win32 错误码。
        fakes.Acpi.Reset(CandidateAcpiMode.IoFailure);
        var failed = reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve(0, 100));
        if (reflection.GetString(failed, "Status") != "InitiatedFailed" ||
            reflection.GetBool(failed, "Initiated") != true ||
            reflection.GetInt(failed, "Win32Error") != 5)
            throw new InvalidOperationException($"io-failure receipt wrong: {reflection.Describe(failed)}");

        // 传输调用抛异常：无法证明是否发起。
        fakes.Acpi.Reset(CandidateAcpiMode.TransportException);
        var uncertain = reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        if (reflection.GetString(uncertain, "Status") != "IoException" ||
            !reflection.GetBool(uncertain, "InitiatedUncertain"))
            throw new InvalidOperationException($"exception receipt wrong: {reflection.Describe(uncertain)}");

        // 短响应（语义 unknown）。
        fakes.Acpi.Reset(CandidateAcpiMode.ShortResponse);
        var shortReceipt = reflection.SetFanCurveObserved(AsusFanTarget.Mid, CandidateReflection.Curve(0, 100));
        if (reflection.GetString(shortReceipt, "Status") != "TransportedShort" ||
            reflection.GetInt(shortReceipt, "ValidReturnedLength") != 4 ||
            reflection.GetInt(shortReceipt, "OutputCapacity") != 16 ||
            reflection.GetString(shortReceipt, "VendorSemantics") != "Unknown")
            throw new InvalidOperationException($"short-response receipt wrong: {reflection.Describe(shortReceipt)}");

        // 正常传输：逐扇一次写、真实封包与曲线钳制。
        fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
        var preWrites = fakes.Acpi.WriteCalls.Count;
        var cpu = reflection.SetFanCurveObserved(AsusFanTarget.Cpu, CandidateReflection.Curve(0, 100));
        var gpu = reflection.SetFanCurveObserved(AsusFanTarget.Gpu, CandidateReflection.Curve(10, 100));
        var mid = reflection.SetFanCurveObserved(AsusFanTarget.Mid, CandidateReflection.Curve(20, 100));
        var writes = fakes.Acpi.WriteCalls.Skip(preWrites).ToList();
        if (writes.Count != 3)
            throw new InvalidOperationException($"expected exactly one write per fan, got {writes.Count}");
        var expectedIds = new[] { 0x00110024u, 0x00110025u, 0x00110032u };
        for (var i = 0; i < expectedIds.Length; i++)
            if (writes[i].DeviceId != expectedIds[i] || writes[i].MethodId != 0x53564544u)
                throw new InvalidOperationException(
                    $"fan write[{i}] packet mismatch device=0x{writes[i].DeviceId:X8} method=0x{writes[i].MethodId:X8}");
        foreach (var (fanReceipt, fanName) in new[] { (cpu, "CPU"), (gpu, "GPU"), (mid, "Mid") })
        {
            if (reflection.GetString(fanReceipt, "Status") != "TransportedComplete" ||
                reflection.GetInt(fanReceipt, "InputLength") != 28 ||
                reflection.GetInt(fanReceipt, "ValidReturnedLength") != 16 ||
                reflection.GetString(fanReceipt, "Fan") != fanName)
                throw new InvalidOperationException($"{fanName} receipt wrong: {reflection.Describe(fanReceipt)}");
        }

        // 曲线变换/钳制 + 输入长度/输出容量分开记录。
        var cpuWrite = writes[0];
        if (cpuWrite.InputLength != 28 || cpuWrite.BufferSize != 16)
            throw new InvalidOperationException(
                $"fan write framing wrong input={cpuWrite.InputLength} capacity={cpuWrite.BufferSize}");
        var clamped = cpuWrite.InBuffer[12..];
        if (clamped.Length != 16)
            throw new InvalidOperationException("fan write curve payload truncated");
        for (var i = 0; i < 8; i++)
            if (clamped[8 + i] > 99)
                throw new InvalidOperationException($"duty {clamped[8 + i]} was not clamped to 99");

        // 读回（诊断用）同样逐调用留收据，且不产生额外写。
        var writesBeforeRead = fakes.Acpi.WriteCalls.Count;
        var readReceipt = reflection.GetFanCurveObserved(AsusFanTarget.Cpu);
        if (reflection.GetString(readReceipt, "Status") != "TransportedComplete" ||
            fakes.Acpi.WriteCalls.Count != writesBeforeRead)
            throw new InvalidOperationException($"fan read receipt wrong: {reflection.Describe(readReceipt)}");

        // 收据注册表可被 Host 取走（新接口给 Host 取得本操作结果）。
        var taken = reflection.TakeAcpiReceipts();
        if (taken.Count < 8)
            throw new InvalidOperationException($"expected the per-call receipts to be retrievable, got {taken.Count}");
        if (reflection.PeekAcpiReceipts().Count != 0)
            throw new InvalidOperationException("TakeReceipts did not clear the registry");
        checks.Add(
            $"candidate-acpi-receipts invalid=NotInitiatedHandleInvalid failed=InitiatedFailed(win32=5) " +
            $"exception=IoException(uncertain=true) short=TransportedShort(4/16) complete=TransportedComplete " +
            $"perFanWrites=3 ids=[0x00110024,0x00110025,0x00110032] method=0x53564544 clampedDuty<=99 taken={taken.Count}");
    }

    // ---- S8：生产 engine 的故障→恢复有效配置 + 阻塞读 ----
    private static void RunProductionEngineFlow(
        FanHostEngine ownerEngine,
        string candidatePath,
        string candidateHash,
        CandidateReflection reflection,
        List<string> checks)
    {
        var checksLocal = checks;
        var token = Guid.NewGuid().ToString("N");
        var authorizationPath = Path.Combine(Path.GetTempPath(), $"yeman-s18-{token}.authorization");
        // 内容与仓库内正式授权记录逐字相同（仅路径不同）；本组所有硬件访问都落在注入的
        // ACPI/HID 边界替身上，不产生真实设备写入。
        var authorizationSource = Path.Combine(AppContext.BaseDirectory, "YeManFanHost.authorization.md");
        if (!File.Exists(authorizationSource))
        {
            var fallback = Path.Combine(
                "G:\\YeManCC-Work\\Mainline\\YeManCC-source\\YeManCC\\FanLab\\real-host",
                "YeManFanHost.authorization.md");
            authorizationSource = File.Exists(fallback) ? fallback : throw new FileNotFoundException(
                "§18 requires the formal authorization record for the production engine flow", fallback);
        }
        File.WriteAllText(
            authorizationPath,
            "920-v1.6 §18 candidate integration (injected OS boundaries; no real device writes)\n" +
            File.ReadAllText(authorizationSource));
        HostDiagnosticLog.ResetSelfTestCapture();
        HostDiagnosticLog.SelfTestCaptureEnabled = true;
        try
        {
            _ = ownerEngine;   // 该组使用自己的生产 engine 实例（与主 engine 相同的类与入口）
            var options = HostOptions.Parse(
            [
                "--port", "8891",
                "--real-backend",
                "--hc-assembly", candidatePath,
                "--hc-runtime-root", Path.GetDirectoryName(Path.GetFullPath(candidatePath))!,
                "--hc-expected-sha256", candidateHash,
                "--hc-device-type", "HandheldCompanion.Devices.ROGAlly",
                "--asus-readback",
                "--allow-hardware-writes",
                "--authorization", authorizationPath,
                "--session-token", token,
                "--confirm", token,
            ]);

            var fakes = new CandidateFakes(reflection);
            reflection.InstallBoundaryFactory(fakes.Boundary.Proxy);
            reflection.InstallAcpiTransport(fakes.Acpi.Proxy);
            fakes.Boundary.AutoBindOnProbe = true;
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);

            using var engine = new FanHostEngine(options, new HostDiagnosticLog());
            var backend = engine.RealBackendForSelfTest
                ?? throw new InvalidOperationException("§18: production engine did not create a real backend");

            // 920-v1.13 §24.2：这一失败入口必须自带完整诊断。加载被拒绝时
            // Handshake 会吞掉异常并把它放进 state.LastError（reason）；只报
            // "did not load a device" 会丢掉"为什么没有设备实例"的唯一现场。
            var handshake = engine.Handshake();
            var handshakeState = engine.Snapshot();
            var device = backend.DeviceForSelfTest
                ?? throw new InvalidOperationException(
                    $"§18: production engine did not load a device " +
                    $"state={handshakeState.State} hostMode={handshakeState.HostMode} " +
                    $"lastError={handshakeState.LastError ?? "-"} " +
                    $"assemblyLoaded={backend.LoadedHcAssemblyForSelfTest is not null} " +
                    $"handshake={JsonSerializer.Serialize(handshake, JsonDefaults.Options)} " +
                    $"capturedCodes=[{string.Join(",", HostDiagnosticLog.SelfTestCaptured.Select(entry => entry.Event))}]");
            // 设备在 Handshake 的装配体加载里创建：在其后的就绪探测/打开前完成边界接线。
            fakes.Boundary.HcDevice = device;
            _ = engine.Open();
            _ = engine.OpenEvents();
            if (reflection.GetBool(device, "IsOpen") != true)
                throw new InvalidOperationException(
                    $"§18: production engine did not reach the ASUS device-open state " +
                    $"route={backend.Route?.RestoreStrategy} isOpen={reflection.GetBool(device, "IsOpen")} " +
                    $"hcDeviceIsOpen={HcDeviceIsOpen(device)} bindPublications={fakes.Boundary.BindPublications} " +
                    $"hidOpens={fakes.Boundary.HidOpens} lastOp={reflection.GetString(device, "LastCompatEntryOperationId") ?? "-"} " +
                    $"readLoop={reflection.GetString(device, "ReadLoopState")} state={engine.Snapshot().State} " +
                    $"hcDeviceOpen={reflection.GetBool(device, "DeviceOpen")} acpiOpen={reflection.GetBoolStatic("HandheldCompanion.Devices.ASUS.AsusACPI", "IsOpen")} " +
                    $"bindings=[{reflection.DescribeBindingTable(device)}] " +
                    $"logs=[{string.Join("|", fakes.Boundary.Logs.TakeLast(4))}]");
            if (backend.Route?.RestoreStrategy != FanRestoreStrategy.AsusAcpiCurves)
                throw new InvalidOperationException($"§18: production engine selected route {backend.Route?.RestoreStrategy}");
            checksLocal.Add("candidate-engine[open] hcDeviceIsOpen=true route=AsusAcpiCurves");

            // 生产写路径（Enable + 软件曲线）：必须先真的写过，OEM 交还才会走写回分支。
            engine.GrantSelfTestLeaseForSelfTest("s18-lease");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = engine.Enable(BuildEnableBody("s18-lease"));
            var curveWrites = fakes.Acpi.WriteCalls.Count;
            var snapshotAfterEnable = engine.Snapshot();
            if (curveWrites != 3)
                throw new InvalidOperationException($"§18: production Enable issued {curveWrites} fan writes instead of 3");
            if (!snapshotAfterEnable.HardwareWritesObserved)
                throw new InvalidOperationException("§18: production Enable did not record observed hardware writes");
            checksLocal.Add(
                $"candidate-engine[control] enableWrites=3 hardwareWritesObserved=true state={snapshotAfterEnable.State}");

            // 阶段 1：写入失败 ⇒ 不得把“未发生”的 OEM 交还降成成功（I-08）。
            fakes.Acpi.Reset(CandidateAcpiMode.IoFailureWrites);
            var faultCode = "<none>";
            try { _ = engine.Restore(JsonDocument.Parse("{}").RootElement); }
            catch (Exception ex)
            {
                faultCode = FaultCodeOf(ex);
            }
            if (faultCode == "<none>")
                throw new InvalidOperationException("§18: failing fan writes were downgraded to restore success (I-08 violation)");
            checksLocal.Add(
                $"candidate-engine[fault-not-downgraded] code={faultCode} state={engine.Snapshot().State} " +
                $"failedWrites={fakes.Acpi.FailedWriteCount}");

            // 阶段 2：释放故障 ⇒ 同一生产 engine 恢复原有效配置（逐扇默认表写入）。
            fakes.Acpi.Reset(CandidateAcpiMode.MatchDefaults);
            _ = engine.Restore(JsonDocument.Parse("{}").RootElement);
            var writes = fakes.Acpi.WriteCalls.ToList();
            var expectedIds = new[] { 0x00110024u, 0x00110025u, 0x00110032u };
            if (writes.Count != 3 || !writes.Select(w => w.DeviceId).SequenceEqual(expectedIds))
                throw new InvalidOperationException(
                    $"§18: restored configuration writes mismatch count={writes.Count} ids=[{string.Join(",", writes.Select(w => $"0x{w.DeviceId:X8}"))}]");
            var defaultCpu = reflection.GetStaticBytes("HandheldCompanion.Devices.ROGAlly", "defaultCPUFan");
            var defaultGpu = reflection.GetStaticBytes("HandheldCompanion.Devices.ROGAlly", "defaultGPUFan");
            if (defaultCpu is null || defaultGpu is null)
                throw new InvalidOperationException("§18: HC default fan tables are not readable");
            if (!writes[0].InBuffer[12..].SequenceEqual(defaultCpu) || !writes[1].InBuffer[12..].SequenceEqual(defaultGpu))
                throw new InvalidOperationException("§18: restored fan table bytes do not match HC's default tables");
            var confirmed = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "restore.asus-default-readback-confirmed");
            checksLocal.Add(
                $"candidate-engine[recovery-restores-config] fanWrites=3 ids=[0x00110024,0x00110025,0x00110032] " +
                $"defaultTableBytes=match readbackConfirmed={confirmed} state={engine.Snapshot().State}");

            // 阶段 3：生产 Restore 链上的阻塞读（未决 I/O 保持到显式释放）。
            // 先重新接管一次软件曲线（清除已确认标记），否则生产 Restore 会跳过 OEM 写回分支。
            engine.GrantSelfTestLeaseForSelfTest("s18-lease-2");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = engine.Enable(BuildEnableBody("s18-lease-2"));
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            fakes.Acpi.BlockReads = true;
            var blockedStarted = Stopwatch.GetTimestamp();
            var blockedTask = Task.Run(() =>
            {
                try { _ = engine.Restore(JsonDocument.Parse("{}").RootElement); return "returned"; }
                catch (Exception ex) { return "error:" + ex.GetType().Name; }
            });
            if (!fakes.Acpi.WaitBlockedReadEntered(5_000))
                throw new InvalidOperationException(
                    $"§18: production restore never reached the ACPI readback reads={fakes.Acpi.ReadCalls} " +
                    $"writes={fakes.Acpi.WriteCalls.Count} blocked={fakes.Acpi.BlockReads} taskCompleted={blockedTask.IsCompleted} " +
                    $"result={(blockedTask.IsCompleted ? blockedTask.Result : "<running>")}");
            var writesWhileBlocked = fakes.Acpi.WriteCalls.Count;
            Thread.Sleep(1_200);
            var stillPending = !blockedTask.IsCompleted;
            var blockedMs = (Stopwatch.GetTimestamp() - blockedStarted) * 1000d / Stopwatch.Frequency;
            var readsWhileBlocked = fakes.Acpi.ReadCalls;
            if (!stillPending)
                throw new InvalidOperationException(
                    $"§18: production restore fabricated a completion while the read was pending ({blockedTask.Result})");
            if (fakes.Acpi.WriteCalls.Count != writesWhileBlocked)
                throw new InvalidOperationException(
                    $"§18: a second hardware write was issued while the read was pending before={writesWhileBlocked} after={fakes.Acpi.WriteCalls.Count}");
            if (readsWhileBlocked != 1)
                throw new InvalidOperationException($"§18: duplicate reads while pending: {readsWhileBlocked}");

            fakes.Acpi.ReleaseBlockedReads(shortResponse: true);
            if (!blockedTask.Wait(10_000))
                throw new InvalidOperationException("§18: production restore did not return after the pending read was released");
            var unconfirmed = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "restore.asus-default-readback-unconfirmed");
            if (!unconfirmed)
                throw new InvalidOperationException("§18: a short readback response was not reported as unconfirmed");
            checksLocal.Add(
                $"candidate-engine[pending-read] blockedMs={blockedMs:F0} pendingObserved=true secondWrite=false " +
                $"readAttemptsWhileBlocked={readsWhileBlocked} released=true readbackUnconfirmed=true");

            // 阶段 4：释放后可重新接管（同一 engine 再次生产 restore，读回匹配即确认）。
            engine.GrantSelfTestLeaseForSelfTest("s18-lease-3");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = engine.Enable(BuildEnableBody("s18-lease-3"));
            fakes.Acpi.Reset(CandidateAcpiMode.MatchDefaults);
            HostDiagnosticLog.ResetSelfTestCapture();
            _ = engine.Restore(JsonDocument.Parse("{}").RootElement);
            var takeoverWrites = fakes.Acpi.WriteCalls.Count;
            var takeoverConfirmed = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "restore.asus-default-readback-confirmed");
            if (takeoverWrites != 3)
                throw new InvalidOperationException($"§18: re-takeover issued {takeoverWrites} fan writes instead of 3");
            if (!takeoverConfirmed)
                throw new InvalidOperationException("§18: re-takeover did not confirm the restored configuration");
            checksLocal.Add(
                $"candidate-engine[retakeover-after-release] fanWrites=3 readbackConfirmed=true state={engine.Snapshot().State}");

            // 阶段 5：Close 之后不得再有设备副作用。
            var hidOpensBeforeClose = fakes.Boundary.HidOpens;
            _ = engine.Close();
            if (fakes.Boundary.HidOpens != hidOpensBeforeClose)
                throw new InvalidOperationException("§18: device was re-opened after the engine close");
            checksLocal.Add(
                $"candidate-engine[close] hidOpensStable={fakes.Boundary.HidOpens} readLoop={reflection.GetString(device, "ReadLoopState")}");

            // 阶段 6（§18.2-C）：不带 --asus-readback 时，自动 restore 路径**不发起**可选读回，
            // 仍完成逐扇默认表写回；读回如实记录 not-attempted/unknown。
            var defaultOptions = HostOptions.Parse(
            [
                "--port", "8892",
                "--real-backend",
                "--hc-assembly", candidatePath,
                "--hc-runtime-root", Path.GetDirectoryName(Path.GetFullPath(candidatePath))!,
                "--hc-expected-sha256", candidateHash,
                "--hc-device-type", "HandheldCompanion.Devices.ROGAlly",
                "--allow-hardware-writes",
                "--authorization", authorizationPath,
                "--session-token", token,
                "--confirm", token,
            ]);
            if (defaultOptions.AsusReadbackEnabled)
                throw new InvalidOperationException("§18: optional ASUS readback must be off by default");
            using var defaultEngine = new FanHostEngine(defaultOptions, new HostDiagnosticLog());
            var defaultBackend = defaultEngine.RealBackendForSelfTest
                ?? throw new InvalidOperationException("§18: default engine did not create a real backend");
            _ = defaultEngine.Handshake();
            var defaultDevice = defaultBackend.DeviceForSelfTest
                ?? throw new InvalidOperationException("§18: default engine did not load a device");
            fakes.Boundary.HcDevice = defaultDevice;
            _ = defaultEngine.Open();
            _ = defaultEngine.OpenEvents();
            defaultEngine.GrantSelfTestLeaseForSelfTest("s18-default-lease");
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            _ = defaultEngine.Enable(BuildEnableBody("s18-default-lease"));
            fakes.Acpi.Reset(CandidateAcpiMode.Complete16);
            HostDiagnosticLog.ResetSelfTestCapture();
            _ = defaultEngine.Restore(JsonDocument.Parse("{}").RootElement);
            var defaultWrites = fakes.Acpi.WriteCalls.Count;
            var defaultReads = fakes.Acpi.ReadCalls;
            var notAttempted = HostDiagnosticLog.SelfTestCaptured.Any(e => e.Event == "restore.asus-default-readback-not-attempted");
            if (defaultWrites != 3 || defaultReads != 0 || !notAttempted)
                throw new InvalidOperationException(
                    $"§18: default restore must write the default table without the optional readback " +
                    $"writes={defaultWrites} readbackReads={defaultReads} notAttempted={notAttempted}");
            checksLocal.Add(
                $"candidate-engine[readback-default-off] fanWrites=3 readbackReads=0 notAttempted=true state={defaultEngine.Snapshot().State}");
            _ = defaultEngine.Close();
        }
        finally
        {
            HostDiagnosticLog.SelfTestCaptureEnabled = false;
            HostDiagnosticLog.ResetSelfTestCapture();
            try { File.Delete(authorizationPath); } catch { }
        }
    }
}