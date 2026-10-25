// YeMan 灯光设置器（YeManLightSetter）——一次性灯光设置工具
// 2026-09-16（HC 机型库适配 L1 方案 B）：
// 直驱 HC 程序集（HandheldCompanion.dll）执行灯光设置，覆盖 HC 支持的全部
// 机型——各机型的 EC/HID/USB 协议由 HC 内部处理（SetLedColor 等实现）。
// 灯光是低频操作（用户改一次），故无需常驻宿主/租约/HTTP/恢复链；本进程
// 按需启动、调用完成后退出。legacy 不调用 HC Close；独立 HID 核心释放自己的句柄。
//
// 用法：
//   YeManLightSetter.exe --hc-assembly <HandheldCompanion.dll>
//     --action color --level solid|breathing|rainbow|wave|wheel|gradient|ambilight|preset
//     [--r 255 --g 0 --b 0] [--r2 .. --g2 .. --b2 ..] [--speed 0-100]
//   YeManLightSetter.exe --hc-assembly <..> --action preset --preset-id <HC NameKey>
//   YeManLightSetter.exe --hc-assembly <..> --action brightness --brightness 0-100
//   YeManLightSetter.exe --hc-assembly <..> --action status --status on|off
// 独立 HID 核心：--device-core-assembly <YeManLightDeviceCore.dll>
//   --expected-device-class <精确机型类> [--device-core-sha256 <内置 pin 的一致性检查>]
//   X1Mini color solid/rainbow、brightness 在 HC 初始化前分派。
//   未覆盖能力可显式携带 --hc-assembly 走旧适配；传输失败绝不隐式重试。
// HC profile：--hc-profile legacy|ymcc-device-core-v1（新 profile 等待主控 pin）
// 可选：--hc-runtime-root <HC 运行时目录>（默认取 dll 所在目录）
//       --timeout-ms <IsReady 等待上限，默认 15000>
//
// 输出：单行 JSON（{ok:true,result:{...}} 或 {ok:false,error:"..."}），
// 退出码 0=成功 / 1=失败。调用方可据此记录 native 日志。
//
// 复用要素（与 YeManFanHost 同构）：
//   - HC 加载 + SHA-256 校验（ExpectedHcSha256，与 FanHost 同一锁定值）
//   - 依赖解析 ResolveDependency / 运行时配置 ConfigureHcRuntime
//   - 设备激活 WaitForHcDeviceReadyBeforeOpen（IsReady 轮询）+ OpenHcDevice
//   - LEDLevel 位值（HandheldCompanion.Utils.DeviceUtils.cs:20-31, [Flags]）
using YeManFanBackend.Capabilities;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Program
{
    // 与 YeManFanHost 同源的 HC 程序集锁定哈希（candidate 0.32.4.0-06c0b954；
    // 920 v1.15 §26.3 重基线到获准候选 0c5132a9…（FAN-936 单一闭包：ASUS + MSI），
    // native 硬编码的 PowerControl\handheldcompanion-runtime\HC-CANDIDATE-… 目录将携带该 DLL）。
    private const string ExpectedHcSha256 =
        "0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49";

    // Awaiting main-controller derivative identity/pin. Empty means disabled;
    // neither a caller hash nor a runtime manifest may enable this profile.
    private const string ExpectedYmccHcCoreSha256 = "08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583";
    private const string ExpectedYmccHcCoreAssemblyName = "HandheldCompanion";

    private static string HcPinForProfile(string profile) => profile switch
    {
        "legacy" => ExpectedHcSha256,
        "ymcc-device-core-v1" when ExpectedYmccHcCoreSha256.Length == 64 && ExpectedYmccHcCoreAssemblyName.Length > 0 => ExpectedYmccHcCoreSha256,
        "ymcc-device-core-v1" => throw new InvalidOperationException("YMCC HC device-core profile has no approved pin yet"),
        _ => throw new ArgumentException($"unknown HC runtime profile {profile}"),
    };

    // LEDLevel（[Flags]）：None=0, SolidColor=1, Breathing=2, Rainbow=4,
    // Wave=8, Wheel=16, Gradient=32, Ambilight=64, LEDPreset=128。
    private static readonly Dictionary<string, int> LedLevels = new(StringComparer.OrdinalIgnoreCase)
    {
        ["solid"] = 1,
        ["breathing"] = 2,
        ["rainbow"] = 4,
        ["wave"] = 8,
        ["wheel"] = 16,
        ["gradient"] = 32,
        ["ambilight"] = 64,
        ["preset"] = 128,
    };

    private static Assembly? assembly;
    private static string? runtimeRoot;

    // HC 设备初始化/灯光链路会构造 WPF 亲和对象（System.Windows/Media 等），
    // 非 STA 线程会抛"调用线程必须为 STA，因为许多 UI 组件都需要。"
    // （2026-09-16 实机复现；FanHost 用常驻 STA dispatcher 线程解决，本进程
    //  是一次性短命进程，直接让主线程 STA 即可——同 [STAThread]，零新线程。）
    [STAThread]
    private static int Main(string[] args)
    {
        YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();
        try
        {
            var result = Run(args);
            // 退出码必须反映设备侧真实结果：Run 把 HC 各方法返回 false 收敛为
            // note != "ok"（setLedColor-false / setLedBrightness-false /
            // off-partial 等）而不抛异常。原实现恒返回 0，上层
            // ymccLightSetterApply 以 exitCode==0 判成功时会对失败误报 ok。
            var note = result.TryGetValue("note", out var value) ? value as string : null;
            var exitCode = ExitCodeForNote(note);
            Console.WriteLine(JsonSerializer.Serialize(new { ok = exitCode == 0, result }));
            return exitCode;
        }
        catch (Exception ex)
        {
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                ok = false,
                error = ex.GetBaseException().Message,
                dependencyFailures = YeMan.SharedRuntime.RuntimeDependencyContract.DescribeFailure(ex, "lighting.execute"),
            }));
            return 1;
        }
    }

    private static Dictionary<string, object?> Run(string[] args)
    {
        string? hcPath = null, runtimeRootOpt = null, action = null, level = null, statusArg = null, presetId = null, expectedDeviceClass = null;
        string? corePath = null, coreCallerHash = null;
        var hcProfile = "legacy";
        int? r = null, g = null, b = null, r2 = null, g2 = null, b2 = null;
        int? brightness = null, speed = null;
        var timeoutMs = 15_000;

        for (var i = 0; i < args.Length; i++)
        {
            var current = args[i];
            string Next()
            {
                if (i + 1 >= args.Length) throw new ArgumentException($"missing value for {current}");
                return args[++i];
            }
            switch (current)
            {
                case "--hc-assembly": hcPath = Next(); break;
                case "--hc-profile": hcProfile = Next(); break;
                case "--device-core-assembly": corePath = Next(); break;
                case "--device-core-sha256": coreCallerHash = Next(); break;
                case "--hc-runtime-root": runtimeRootOpt = Next(); break;
                case "--action": action = Next(); break;
                case "--level": level = Next(); break;
                case "--preset-id": presetId = Next(); break;
                case "--expected-device-class": expectedDeviceClass = Next(); break;
                case "--r": r = int.Parse(Next()); break;
                case "--g": g = int.Parse(Next()); break;
                case "--b": b = int.Parse(Next()); break;
                case "--r2": r2 = int.Parse(Next()); break;
                case "--g2": g2 = int.Parse(Next()); break;
                case "--b2": b2 = int.Parse(Next()); break;
                case "--brightness": brightness = int.Parse(Next()); break;
                case "--speed": speed = int.Parse(Next()); break;
                case "--status": statusArg = Next(); break;
                case "--timeout-ms": timeoutMs = int.Parse(Next()); break;
                default: throw new ArgumentException($"unknown arg {current}");
            }
        }

        ValidateRequest(action, level, statusArg, presetId);
        if (timeoutMs <= 0) throw new ArgumentOutOfRangeException(nameof(timeoutMs));
        if (corePath is not null)
        {
            var coreResult = DeviceCoreConsumer.TryExecute(corePath, coreCallerHash,
                expectedDeviceClass, action!, level, new
                {
                    Action = action, Level = level, R = r, G = g, B = b,
                    R2 = r2, G2 = g2, B2 = b2, Brightness = brightness, Speed = speed, PresetId = presetId,
                });
            if (coreResult is not null) return coreResult;
            if (string.IsNullOrWhiteSpace(hcPath))
                throw new NotSupportedException("device-core capability unavailable; explicit --hc-assembly legacy adapter required");
        }
        else if (coreCallerHash is not null)
            throw new ArgumentException("--device-core-sha256 requires --device-core-assembly");
        if (string.IsNullOrWhiteSpace(hcPath)) throw new ArgumentException("--hc-assembly or --device-core-assembly is required");
        var expectedHcHash = HcPinForProfile(hcProfile);

        // ---- HC 加载（FanHost LoadAssemblyAndFactory 同构）----
        var fullPath = Path.GetFullPath(hcPath);
        if (!File.Exists(fullPath))
            throw new FileNotFoundException("HandheldCompanion.dll not found", fullPath);
        var actualHash = Sha256(fullPath);
        if (!actualHash.Equals(expectedHcHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException($"HC assembly SHA-256 mismatch: {actualHash}");
        var assemblyDirectory = Path.GetDirectoryName(fullPath)!;
        runtimeRoot = Path.GetFullPath(runtimeRootOpt ?? assemblyDirectory);
        AssemblyLoadContext.Default.Resolving += ResolveDependency;
        assembly = YeMan.SharedRuntime.RuntimeDependencyContract.LoadPinned(
            AssemblyLoadContext.Default, new AssemblyName(ExpectedYmccHcCoreAssemblyName), fullPath, expectedHcHash);
        if (hcProfile != "legacy" && assembly.GetName().Name != ExpectedYmccHcCoreAssemblyName)
            throw new InvalidOperationException("unexpected YMCC HC device-core assembly identity");
        ConfigureHcRuntime();

        // ---- 设备获取 + 激活（FanHost OpenHcDevice/WaitForHcDeviceReadyBeforeOpen 同构）----
        var deviceType = assembly.GetType("HandheldCompanion.Devices.IDevice", throwOnError: true)!;
        var device = deviceType
            .GetMethod("GetCurrent", BindingFlags.Public | BindingFlags.Static)?
            .Invoke(null, null)
            ?? throw new InvalidOperationException("HC IDevice.GetCurrent returned null");
        var capability = RequireCatalogLightingOperation(device.GetType(), expectedDeviceClass, action!, level, brightness is not null, presetId);
        Invoke(device, "Initialize", false, false);
        var ready = WaitReady(device, timeoutMs);
        using var oneXLighting = OneXLightingSession.Prepare(device);
        if (!OpenLightingDevice(device))
            throw new InvalidOperationException("HC device Open returned false");

        // ---- 灯光调用（IDevice.cs:1036-1041）----
        // action 本体已提为私有纯调用函数 ExecuteLedAction，使离线 fake harness
        // 能直接执行真实业务代码（而非复刻一份）。Run 只负责加载/激活与结果封装。
        var deviceRuntimeType = device.GetType();
        var transaction = OneXQueuedLighting.Execute(device, () => ExecuteResolvedCatalogLedAction(device, deviceRuntimeType,
            capability, statusArg, r, g, b, r2, g2, b2, brightness, speed, presetId), preset: action == "preset");
        var note = transaction.Note;

        return new Dictionary<string, object?>
        {
            ["deviceClass"] = device.GetType().FullName,
            ["capabilityId"] = capability.Device.DeviceId,
            ["capabilityOperation"] = capability.OperationId,
            ["capabilityCatalogSha256"] = HcLightingCapabilityCatalog.CatalogSha256,
            ["hcSha256"] = actualHash,
            ["sharedDependencyBindings"] = YeMan.SharedRuntime.DeviceCoreSharedDependencies.Snapshot(),
            ["backend"] = hcProfile == "legacy" ? "legacy-hc" : "ymcc-hc-device-core",
            ["hcProfile"] = hcProfile,
            ["ready"] = ready,
            ["opened"] = true,
            ["action"] = action,
            ["note"] = note,
            ["presetId"] = presetId,
            ["queuedWriteManaged"] = transaction.Managed,
            ["queuedWritesCompleted"] = transaction.Completed,
            ["queuedWriteTasks"] = transaction.WriteTasks,
            ["queuedWriteFailures"] = transaction.FailedTasks,
            ["physicalEffectVerified"] = false,
        };
    }

    // Resolve once before initialization. The immutable admitted operation flows directly to
    // the actual action; runtime overrides are checked before any device IO.
    private static LightingCapabilityOperation RequireCatalogLightingOperation(Type actualType, string? expectedClass,
        string action, string? level, bool includesBrightness, string? presetId)
    {
        var operation = HcLightingCapabilityCatalog.Require(actualType.FullName, expectedClass, action, level, includesBrightness, presetId);
        ValidateOperationImplementation(actualType, action, includesBrightness);
        if (action == "off" && (HasRealLightingMethod(actualType, "SetLedBrightness") != operation.Capabilities.Brightness ||
            HasRealLightingMethod(actualType, "SetLedStatus") != (operation.Capabilities.StatusAvailable || operation.Capabilities.StatusTransportDisabled)))
            throw new NotSupportedException("LIGHTING_OFF_RUNTIME_CONTRACT_MISMATCH");
        return operation;
    }

    private static string ExecuteCatalogLedAction(object device, Type actualType, string? expectedClass,
        string action, string? level, string? statusArg, int? r, int? g, int? b, int? r2, int? g2, int? b2,
        int? brightness, int? speed, string? presetId)
    {
        var operation = RequireCatalogLightingOperation(actualType, expectedClass, action, level, brightness is not null, presetId);
        return ExecuteResolvedCatalogLedAction(device, actualType, operation, statusArg, r, g, b, r2, g2, b2, brightness, speed, presetId);
    }

    private static string ExecuteResolvedCatalogLedAction(object device, Type actualType, LightingCapabilityOperation operation,
        string? statusArg, int? r, int? g, int? b, int? r2, int? g2, int? b2, int? brightness, int? speed, string? presetId)
    {
        var action = operation.Action;
        if (action == "off")
        {
            OneXQueuedLighting.PrepareRequestCache(device, action, brightness, statusArg);
            return ExecuteLightingOff(device, actualType, operation.Capabilities.StatusAvailable, operation.Capabilities.Brightness);
        }
        return action == "preset" ? ExecuteLedPreset(device, actualType, presetId, brightness)
            : ExecuteLedAction(device, actualType, action, operation.Level, statusArg, r, g, b, r2, g2, b2, brightness, speed);
    }

    private static string ExecuteLightingOff(object device, Type type, bool statusWriter, bool brightnessWriter)
    {
        var contract = ResolveSetLedColorContract(type);
        var statusOk = !statusWriter || type.GetMethod("SetLedStatus")!.Invoke(device, [false]) is true;
        var brightnessOk = !brightnessWriter || type.GetMethod("SetLedBrightness")!.Invoke(device, [0]) is true;
        var black = CreateLedColor(contract.ColorType, 0, 0, 0);
        var colorOk = contract.SetColor.Invoke(device, [black, black, Enum.ToObject(contract.LedLevelType, LedLevels["solid"]), 0]) is true;
        return statusOk && brightnessOk && colorOk ? "ok" : "off-partial";
    }

    private static bool HasRealLightingMethod(Type actualType, string method) =>
        actualType.GetMethod(method, BindingFlags.Public | BindingFlags.Instance) is { } candidate &&
        candidate.DeclaringType?.FullName != "HandheldCompanion.Devices.IDevice";

    // 灯光 action 真实业务体（从 Run 的 switch 原样提取，逐行不变）。传入已
    // 准备的 device/deviceType 与现有 action/数值参数，返回现有 note；由 Run 在
    // 完成加载、hash/config/init/WaitReady/Open 后调用。提取目的是让离线 fake
    // harness 直接编译/链接同一份生产代码，而不是复刻 action 顺序。
    private static string ExecuteLedAction(
        object device, Type deviceType, string? action,
        string? level, string? statusArg,
        int? r, int? g, int? b, int? r2, int? g2, int? b2,
        int? brightness, int? speed)
    {
        ValidateRequest(action, level, statusArg, null);
        ValidateOperationImplementation(deviceType, action!, brightness is not null);
        OneXQueuedLighting.PrepareRequestCache(device, action!, brightness, statusArg);
        var note = "ok";
        switch (action)
        {
            case "color":
            {
                var levelName = string.IsNullOrWhiteSpace(level) ? "solid" : level;
                if (string.Equals(levelName, "preset", StringComparison.OrdinalIgnoreCase))
                    throw new ArgumentException("preset requires --action preset and --preset-id");
                if (!LedLevels.TryGetValue(levelName, out var levelValue))
                    throw new ArgumentException($"unknown level {levelName}");
                // 签名驱动：level enum 与颜色类型都取自实际 SetLedColor 参数，
                // 不再用 "HandheldCompanion.Utils.LEDLevel" 全名字符串查找（该
                // 名称在当前 HC 中无定义，实际为 DeviceUtils+LEDLevel）。
                var contract = ResolveSetLedColorContract(deviceType);
                var enumValue = Enum.ToObject(contract.LedLevelType, levelValue);
                var main = CreateLedColor(contract.ColorType, Clamp(r ?? 255), Clamp(g ?? 0), Clamp(b ?? 0));
                var secondary = CreateLedColor(
                    contract.ColorType,
                    Clamp(r2 ?? r ?? 255), Clamp(g2 ?? g ?? 0), Clamp(b2 ?? b ?? 0));
                note = contract.SetColor.Invoke(device, [main, secondary, enumValue, speed ?? 100]) is bool ok && ok
                    ? "ok"
                    : "setLedColor-false";
                // 亮度随 color 一并设置（HC DevicePage 的亮度滑杆与效果同属一次
                // UpdateLED；0..100 映射由 HC 内部处理）。仅显式传入时写。
                if (brightness is not null)
                {
                    var setBrightness = deviceType.GetMethod("SetLedBrightness")
                        ?? throw new MissingMethodException(deviceType.FullName, "SetLedBrightness");
                    if (setBrightness.Invoke(device, [Clamp(brightness.Value)]) is not bool bOk || !bOk)
                        note = note == "ok" ? "setLedBrightness-false" : note;
                }
                break;
            }
            case "brightness":
            {
                var setBrightness = deviceType.GetMethod("SetLedBrightness")
                    ?? throw new MissingMethodException(deviceType.FullName, "SetLedBrightness");
                note = setBrightness.Invoke(device, [Clamp(brightness ?? 50)]) is bool ok && ok
                    ? "ok"
                    : "setLedBrightness-false";
                break;
            }
            case "status":
            {
                var setStatus = deviceType.GetMethod("SetLedStatus")
                    ?? throw new MissingMethodException(deviceType.FullName, "SetLedStatus");
                note = setStatus.Invoke(device, [!string.Equals(statusArg, "off", StringComparison.OrdinalIgnoreCase)]) is bool ok && ok
                    ? "ok"
                    : "setLedStatus-false";
                break;
            }
            case "off":
                // Only real optional methods are called. The catalog route further suppresses
                // inherited writers whose transport is disabled for the exact model.
                note = ExecuteLightingOff(device, deviceType, HasRealLightingMethod(deviceType, "SetLedStatus"),
                    HasRealLightingMethod(deviceType, "SetLedBrightness"));
                break;
            default:
                throw new ArgumentException($"unknown action {action}");
        }

        return note;
    }

    // MSI's general Open also changes TDP, controller mode and ROM settings.
    // Lighting needs the HID bound by IsReady, its firmware, and open-state flags.
    // Keep that narrow preparation separate from the general device lifecycle.
    private static bool OpenLightingDevice(object device)
    {
        var type = device.GetType();
        if (OneXLightingSession.IsFamily(type)) return OneXLightingSession.IsReady(device);
        Type? msi = type;
        while (msi is not null && msi.FullName != "HandheldCompanion.Devices.ClawA1M") msi = msi.BaseType;
        if (msi is null) return Invoke(device, "Open") is true;
        var open = type.GetMethod("Open", BindingFlags.Public | BindingFlags.Instance,
            null, Type.EmptyTypes, null) ?? throw new MissingMethodException(type.FullName, "Open");
        if (open.DeclaringType?.FullName is not ("HandheldCompanion.Devices.ClawA1M" or
            "HandheldCompanion.Devices.ClawA2VM" or "HandheldCompanion.Devices.MSI.ClawCG3EM"))
            throw new InvalidOperationException("HC MSI lighting Open owner changed");
        var baseOpen = open.GetBaseDefinition();
        var owner = baseOpen.DeclaringType;
        const BindingFlags fields = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        var openLib = owner?.GetField("UseOpenLib", fields);
        var devices = owner?.GetField("hidDevices", fields);
        var firmware = msi.GetField("Firmware", fields);
        var clawOpen = msi.GetField("ClawOpen", fields);
        var isOpen = type.GetProperty("IsOpen", BindingFlags.Public | BindingFlags.Instance)?.GetMethod;
        if (owner?.FullName != "HandheldCompanion.Devices.IDevice" || owner.Assembly != type.Assembly ||
            baseOpen.ReturnType != typeof(bool) || baseOpen.GetParameters().Length != 0 ||
            openLib?.FieldType != typeof(bool) || openLib.GetValue(device) is not false ||
            firmware?.FieldType != typeof(int) || clawOpen?.FieldType != typeof(bool) ||
            isOpen?.DeclaringType != msi || devices?.GetValue(device) is not System.Collections.IDictionary bindings)
            throw new InvalidOperationException("HC MSI lighting base Open contract changed");
        // Frozen HC Open and LED methods both select INPUT_HID_ID == 1.
        if (!bindings.Contains(1) || bindings[1] is not { } hid) return false;
        if (hid.GetType().GetProperty("IsConnected")?.GetValue(hid) is not true) return false;
        var attributes = hid.GetType().GetProperty("Attributes")?.GetValue(hid)
            ?? throw new InvalidOperationException("MSI lighting HID attributes missing");
        if (attributes.GetType().GetProperty("Version")?.GetValue(attributes) is not int version)
            throw new InvalidOperationException("MSI lighting firmware descriptor changed");
        var call = new System.Reflection.Emit.DynamicMethod("YmccMsiLightingOpen", typeof(bool),
            [typeof(object)], typeof(Program).Module, skipVisibility: false);
        var il = call.GetILGenerator();
        il.Emit(System.Reflection.Emit.OpCodes.Ldarg_0);
        il.Emit(System.Reflection.Emit.OpCodes.Castclass, owner);
        il.Emit(System.Reflection.Emit.OpCodes.Call, baseOpen);
        il.Emit(System.Reflection.Emit.OpCodes.Ret);
        if (!((Func<object, bool>)call.CreateDelegate(typeof(Func<object, bool>)))(device)) return false;
        firmware.SetValue(device, version);
        clawOpen.SetValue(device, true);
        return true;
    }

    // Validate before assembly load/Initialize/Open, and before direct action dispatch.
    // Keep the existing numeric clamping contract; reject ambiguous commands.
    private static void ValidateRequest(string? action, string? level, string? status, string? presetId)
    {
        if (string.IsNullOrWhiteSpace(action)) throw new ArgumentException("--action is required");
        if (action is not ("color" or "brightness" or "status" or "off" or "preset"))
            throw new ArgumentException($"unknown action {action}");
        if (action == "status" && !string.Equals(status, "on", StringComparison.OrdinalIgnoreCase) &&
            !string.Equals(status, "off", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("--status must be on or off");
        if (action == "color" && !string.IsNullOrWhiteSpace(level) &&
            (string.Equals(level, "preset", StringComparison.OrdinalIgnoreCase) || !LedLevels.ContainsKey(level)))
            throw new ArgumentException($"unsupported color level {level}; preset requires --action preset and --preset-id");
        if (action == "preset" && string.IsNullOrWhiteSpace(presetId))
            throw new ArgumentException("preset-id is required");
    }

    private static void ValidateOperationImplementation(Type deviceType, string action, bool includesBrightness)
    {
        // HC IDevice's defaults return true without issuing any operation.
        // Do not promote these inherited no-ops to successful lighting writes.
        void RequireOverride(string method)
        {
            if (!HasRealLightingMethod(deviceType, method))
                throw new NotSupportedException($"device lighting operation unavailable: {method}");
        }
        if (action is "color" or "off") RequireOverride("SetLedColor");
        if (action == "brightness" || (action is "color" or "preset" && includesBrightness)) RequireOverride("SetLedBrightness");
        if (action == "status") RequireOverride("SetLedStatus");
        if (action == "preset") RequireOverride("SetLEDPreset");
    }

    // note→退出码映射（从 Main 提取，使离线 harness 能直接测试真实映射）：
    // 仅 "ok" 记成功，其余（setLedColor-false/setLedBrightness-false/
    // setLedStatus-false/off-partial 及 null）均非零。
    private static int ExitCodeForNote(string? note) => note == "ok" ? 0 : 1;

    private static int Clamp(int value) => Math.Clamp(value, 0, 255);

    private static void ValidatePresetDeviceIdentity(Type actualType, string? expectedClass, string? action)
    {
        // This new guard belongs to the new preset route. Existing color paths
        // retain their model aliases (e.g. AynLoki -> LokiMax6600U/6800U).
        if (action == "preset" && expectedClass is not null &&
            !string.Equals(actualType.Name, expectedClass.Split('.').Last(), StringComparison.Ordinal))
            throw new InvalidOperationException("HC factory device differs from the requested preset class");
    }

    // Use the actual device list and object. A level enum (128), array index
    // or caller-supplied numeric value is not a named HC preset.
    private static string ExecuteLedPreset(object device, Type deviceType, string? presetId, int? brightness)
    {
        if (string.IsNullOrWhiteSpace(presetId)) throw new ArgumentException("preset-id is required");
        ValidateOperationImplementation(deviceType, "preset", brightness is not null);
        var presets = deviceType.GetProperty("LEDPresets", BindingFlags.Public | BindingFlags.Instance)?.GetValue(device)
            as System.Collections.IEnumerable ?? throw new MissingMemberException(deviceType.FullName, "LEDPresets");
        var matches = new List<object>();
        foreach (var preset in presets)
        {
            if (preset is not null && preset.GetType().GetProperty("NameKey")?.GetValue(preset) is string key &&
                string.Equals(key, presetId, StringComparison.Ordinal)) matches.Add(preset);
        }
        if (matches.Count != 1) throw new ArgumentException("preset-id is absent or ambiguous for this HC device");
        var setPreset = deviceType.GetMethod("SetLEDPreset", BindingFlags.Public | BindingFlags.Instance)
            ?? throw new MissingMethodException(deviceType.FullName, "SetLEDPreset");
        var parameters = setPreset.GetParameters();
        if (parameters.Length != 1 || !parameters[0].ParameterType.IsInstanceOfType(matches[0]) ||
            setPreset.ReturnType != typeof(bool)) throw new InvalidOperationException("Invalid SetLEDPreset contract");
        // Match HC DynamicLightingManager: brightness precedes the preset.
        OneXQueuedLighting.PrepareRequestCache(device, "preset", brightness, null);
        var note = "ok";
        if (brightness is not null)
        {
            var method = deviceType.GetMethod("SetLedBrightness", BindingFlags.Public | BindingFlags.Instance)
                ?? throw new MissingMethodException(deviceType.FullName, "SetLedBrightness");
            if (method.Invoke(device, [Clamp(brightness.Value)]) is not true) note = "setLedBrightness-false";
        }
        if (OneXQueuedLighting.UsesQueue(deviceType, preset: true))
            OneXQueuedLighting.InvalidatePresetCache(device);
        if (setPreset.Invoke(device, [matches[0]]) is not true) return "setLEDPreset-false";
        return note;
    }

    // 目标方法签名驱动：仅从 deviceType 的明确 public instance 方法中收集
    // 名为 SetLedColor 的候选；0 个 = 缺失，>1 个 = 歧义，二者都拒绝（不默认
    // 任选、不把类型错误转成 true）；恰好 1 个时验证四参、两色同型、第三 enum、
    // 第四 int、返回 bool。显式 BindingFlags.Public | Instance 排除 static 与
    // 非公开方法，也避免 GetMethod 在重载下抛 AmbiguousMatchException 的隐式行为。
    // 现网 HC（0C5132A9…）签名为
    //   (System.Windows.Media.Color, System.Windows.Media.Color,
    //    HandheldCompanion.Utils.DeviceUtils+LEDLevel, System.Int32) -> bool
    // 不再依赖错误的全名字符串，也不按名称模糊匹配多个重载后任选。
    private static (MethodInfo SetColor, Type ColorType, Type LedLevelType)
        ResolveSetLedColorContract(Type deviceType)
    {
        var candidates = EnumerateSetLedColorCandidates(deviceType);
        if (candidates.Count == 0)
            throw new MissingMethodException(deviceType.FullName, "SetLedColor");
        if (candidates.Count > 1)
            throw new AmbiguousMatchException(
                $"{deviceType.FullName}.SetLedColor has {candidates.Count} public instance overloads");
        var setColor = candidates[0];
        var parameters = setColor.GetParameters();
        if (parameters.Length != 4)
            throw new InvalidOperationException($"SetLedColor arity {parameters.Length} != 4");
        var colorType = parameters[0].ParameterType;
        if (parameters[1].ParameterType != colorType)
            throw new InvalidOperationException(
                $"SetLedColor color parameter mismatch: {parameters[0].ParameterType} vs {parameters[1].ParameterType}");
        if (!parameters[2].ParameterType.IsEnum)
            throw new InvalidOperationException(
                $"SetLedColor level parameter is not an enum: {parameters[2].ParameterType}");
        if (parameters[3].ParameterType != typeof(int))
            throw new InvalidOperationException(
                $"SetLedColor speed parameter is not int: {parameters[3].ParameterType}");
        if (setColor.ReturnType != typeof(bool))
            throw new InvalidOperationException(
                $"SetLedColor return type is not bool: {setColor.ReturnType}");
        return (setColor, colorType, parameters[2].ParameterType);
    }

    // 收集 public instance 的 SetLedColor 候选：含类型自身的方法；若为接口，还须
    // 收集其全部继承接口上的同名方法（.NET 的 Type.GetMethods 对接口不返回继承
    // 成员），使继承方法可解析。按参数签名去重，避免菱形继承下同一方法重复计入而
    // 误判为歧义。static 与非公开方法一律不计入（BindingFlags.Public | Instance）。
    private static List<MethodInfo> EnumerateSetLedColorCandidates(Type deviceType)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var result = new List<MethodInfo>();
        void Collect(Type type)
        {
            foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (method.Name != "SetLedColor") continue;
                var key = string.Join(",", method.GetParameters().Select(p => p.ParameterType.FullName));
                if (seen.Add(key)) result.Add(method);
            }
        }
        Collect(deviceType);
        if (deviceType.IsInterface)
            foreach (var inherited in deviceType.GetInterfaces())
                Collect(inherited);
        return result;
    }

    // 颜色对象显式按目标类型构造：当前 HC 为 System.Windows.Media.Color
    // （WPF，非 WinRT），用 ARGB 工厂 + byte 参数并显式给 alpha=0xFF。
    // 未知色类型不做强制转换、不伪成功。
    private static object CreateLedColor(Type colorType, int r, int g, int b)
    {
        if (colorType.FullName == "System.Windows.Media.Color")
        {
            var factory = colorType.GetMethod("FromArgb", BindingFlags.Public | BindingFlags.Static,
                null, [typeof(byte), typeof(byte), typeof(byte), typeof(byte)], null);
            if (factory is not null && factory.ReturnType == colorType)
                return factory.Invoke(null, [(byte)0xFF, (byte)r, (byte)g, (byte)b])!;
        }
        throw new NotSupportedException(
            $"unsupported SetLedColor color parameter type: {colorType.FullName}");
    }

    // FanHost WaitForHcDeviceReadyBeforeOpen 同构：IsReady 轮询（250ms）。
    private static bool WaitReady(object device, int timeoutMs)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            if (Invoke(device, "IsReady") is bool ready && ready) return true;
            Thread.Sleep(250);
        }
        return false; // 与 HC 一致：超时不拒绝，继续 Open
    }

    // FanHost ResolveDependency 同构。
    private static Assembly? ResolveDependency(AssemblyLoadContext _, AssemblyName name)
    {
        if (string.IsNullOrWhiteSpace(name.Name)) return null;
        var path = Path.Combine(runtimeRoot ?? AppContext.BaseDirectory, name.Name + ".dll");
        return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path)
            : YeMan.SharedRuntime.DeviceCoreSharedDependencies.Resolve(_, name, runtimeRoot ?? AppContext.BaseDirectory);
    }

    // FanHost ConfigureHcRuntime 精简版（HC 需要 SettingsPath/LogsPath 与
    // LogManager 初始化；缺失则 fail-fast，避免 HC 内部写入默认路径）。
    private static void ConfigureHcRuntime()
    {
        if (assembly is null) return;
        var root = Path.Combine(Path.GetTempPath(), "YeManLightSetter-HC");
        var logs = Path.Combine(root, "logs");
        Directory.CreateDirectory(logs);
        Environment.SetEnvironmentVariable("LOG_PATH", logs);
        var appType = assembly.GetType("HandheldCompanion.App");
        SetStaticMember(appType, "SettingsPath", root);
        SetStaticMember(appType, "LogsPath", logs);
        SetStaticMember(appType, "InstallPath", runtimeRoot ?? AppContext.BaseDirectory);
        SetStaticMember(appType, "GameControllerDbPath",
            Path.Combine(runtimeRoot ?? AppContext.BaseDirectory, "gamecontrollerdb.txt"));
        var shared = AssemblyLoadContext.Default.Assemblies
                .FirstOrDefault(item => item.GetName().Name == "Shared")
            ?? (runtimeRoot is not null && File.Exists(Path.Combine(runtimeRoot, "Shared.dll"))
                ? AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(runtimeRoot, "Shared.dll"))
                : null);
        var logType = shared?.GetType("HandheldCompanion.Shared.LogManager");
        var init = logType?.GetMethod("Initialize",
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static, [typeof(string)]);
        if (init is null)
            throw new MissingMethodException("HandheldCompanion.Shared.LogManager.Initialize not found");
        init.Invoke(null, ["YeManLightSetter"]);
    }

    private static void SetStaticMember(Type? type, string name, string value)
    {
        if (type is null) return;
        var property = type.GetProperty(name,
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
        if (property is not null && property.CanWrite)
        {
            property.SetValue(null, value);
            return;
        }
        var field = type.GetField(name,
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
        if (field is not null && field.FieldType == typeof(string)) field.SetValue(null, value);
    }

    // 反射调用（精确参数类型 + fallback 简单查找）。
    private static object? Invoke(object target, string method, params object?[] args)
    {
        var type = target.GetType();
        var exact = type.GetMethod(method,
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
            null,
            args.Select(arg => arg?.GetType() ?? typeof(object)).ToArray(),
            null);
        var resolved = exact
            ?? type.GetMethod(method, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
        if (resolved is null) throw new MissingMethodException(type.FullName, method);
        return resolved.Invoke(target, args);
    }

    private static string Sha256(string path)
    {
        using var stream = File.OpenRead(path);
        using var sha = SHA256.Create();
        return Convert.ToHexString(sha.ComputeHash(stream));
    }
}
