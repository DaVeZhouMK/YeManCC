// YeMan 灯光设置器（YeManLightSetter）——一次性灯光设置工具
// 2026-09-16（HC 机型库适配 L1 方案 B）：
// 直驱 HC 程序集（HandheldCompanion.dll）执行灯光设置，覆盖 HC 支持的全部
// 机型——各机型的 EC/HID/USB 协议由 HC 内部处理（SetLedColor 等实现）。
// 灯光是低频操作（用户改一次），故无需常驻宿主/租约/HTTP/恢复链；本进程
// 按需启动、调用完成后退出（不 Close 设备——灯光状态为硬件侧持久）。
//
// 用法：
//   YeManLightSetter.exe --hc-assembly <HandheldCompanion.dll>
//     --action color --level solid|breathing|rainbow|wave|wheel|gradient|ambilight|preset
//     [--r 255 --g 0 --b 0] [--r2 .. --g2 .. --b2 ..] [--speed 0-100]
//   YeManLightSetter.exe --hc-assembly <..> --action brightness --brightness 0-100
//   YeManLightSetter.exe --hc-assembly <..> --action status --status on|off
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
        try
        {
            var result = Run(args);
            Console.WriteLine(JsonSerializer.Serialize(new { ok = true, result }));
            // 退出码必须反映设备侧真实结果：Run 把 HC 各方法返回 false 收敛为
            // note != "ok"（setLedColor-false / setLedBrightness-false /
            // off-partial 等）而不抛异常。原实现恒返回 0，上层
            // ymccLightSetterApply 以 exitCode==0 判成功时会对失败误报 ok。
            var note = result.TryGetValue("note", out var value) ? value as string : null;
            return ExitCodeForNote(note);
        }
        catch (Exception ex)
        {
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                ok = false,
                error = ex.GetBaseException().Message,
            }));
            return 1;
        }
    }

    private static Dictionary<string, object?> Run(string[] args)
    {
        string? hcPath = null, runtimeRootOpt = null, action = null, level = null, statusArg = null;
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
                case "--hc-runtime-root": runtimeRootOpt = Next(); break;
                case "--action": action = Next(); break;
                case "--level": level = Next(); break;
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

        if (string.IsNullOrWhiteSpace(hcPath)) throw new ArgumentException("--hc-assembly is required");
        if (string.IsNullOrWhiteSpace(action)) throw new ArgumentException("--action is required");

        // ---- HC 加载（FanHost LoadAssemblyAndFactory 同构）----
        var fullPath = Path.GetFullPath(hcPath);
        if (!File.Exists(fullPath))
            throw new FileNotFoundException("HandheldCompanion.dll not found", fullPath);
        var actualHash = Sha256(fullPath);
        if (!actualHash.Equals(ExpectedHcSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException($"HC assembly SHA-256 mismatch: {actualHash}");
        var assemblyDirectory = Path.GetDirectoryName(fullPath)!;
        runtimeRoot = Path.GetFullPath(runtimeRootOpt ?? assemblyDirectory);
        AssemblyLoadContext.Default.Resolving += ResolveDependency;
        assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(fullPath);
        ConfigureHcRuntime();

        // ---- 设备获取 + 激活（FanHost OpenHcDevice/WaitForHcDeviceReadyBeforeOpen 同构）----
        var deviceType = assembly.GetType("HandheldCompanion.Devices.IDevice", throwOnError: true)!;
        var device = deviceType
            .GetMethod("GetCurrent", BindingFlags.Public | BindingFlags.Static)?
            .Invoke(null, null)
            ?? throw new InvalidOperationException("HC IDevice.GetCurrent returned null");
        Invoke(device, "Initialize", false, false);
        var ready = WaitReady(device, timeoutMs);
        if (Invoke(device, "Open") is not bool opened || !opened)
            throw new InvalidOperationException("HC device Open returned false");

        // ---- 灯光调用（IDevice.cs:1036-1041）----
        // action 本体已提为私有纯调用函数 ExecuteLedAction，使离线 fake harness
        // 能直接执行真实业务代码（而非复刻一份）。Run 只负责加载/激活与结果封装。
        var note = ExecuteLedAction(device, deviceType, action, level, statusArg,
            r, g, b, r2, g2, b2, brightness, speed);

        return new Dictionary<string, object?>
        {
            ["deviceClass"] = device.GetType().FullName,
            ["hcSha256"] = actualHash,
            ["ready"] = ready,
            ["opened"] = true,
            ["action"] = action,
            ["note"] = note,
        };
    }

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
        var note = "ok";
        switch (action)
        {
            case "color":
            {
                var levelName = string.IsNullOrWhiteSpace(level) ? "solid" : level;
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
            {
                // HC DynamicLightingManager.UpdateLED() turn-off branch 同构
                // （Managers/DynamicLightingManager.cs:305-310）：SetLedStatus(false)
                // + SetLedBrightness(0) + SetLedColor(Black, Black, SolidColor)。
                var offContract = ResolveSetLedColorContract(deviceType);
                var statusOk = (deviceType.GetMethod("SetLedStatus")
                        ?? throw new MissingMethodException(deviceType.FullName, "SetLedStatus"))
                    .Invoke(device, [false]) is bool s && s;
                var brightnessOk = (deviceType.GetMethod("SetLedBrightness")
                        ?? throw new MissingMethodException(deviceType.FullName, "SetLedBrightness"))
                    .Invoke(device, [0]) is bool bo && bo;
                var black = CreateLedColor(offContract.ColorType, 0, 0, 0);
                var colorOk = offContract.SetColor
                    .Invoke(device, [black, black, Enum.ToObject(offContract.LedLevelType, LedLevels["solid"]), 0]) is bool co && co;
                note = statusOk && brightnessOk && colorOk ? "ok" : "off-partial";
                break;
            }
            default:
                throw new ArgumentException($"unknown action {action}");
        }

        return note;
    }

    // note→退出码映射（从 Main 提取，使离线 harness 能直接测试真实映射）：
    // 仅 "ok" 记成功，其余（setLedColor-false/setLedBrightness-false/
    // setLedStatus-false/off-partial 及 null）均非零。
    private static int ExitCodeForNote(string? note) => note == "ok" ? 0 : 1;

    private static int Clamp(int value) => Math.Clamp(value, 0, 255);

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
        if (colorType == typeof(System.Windows.Media.Color))
            return System.Windows.Media.Color.FromArgb(0xFF, (byte)r, (byte)g, (byte)b);
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
        return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
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