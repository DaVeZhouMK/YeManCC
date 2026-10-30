// [AI:ENTRY:INPUTHOST] 控制器用户行为见 src/views/ButtonMappingView.vue [AI:USER:CONTROLLER]；总地图见 src/router.ts [AI:MAP]。
// 用户动作说明用于守住功能含义；Host职责、协议和内部执行读实现，具体机型能力统一查询 capabilities/ 总库。
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using HIDMaestro;
using YeManInputHost.Diagnostics;
using Nefarius.Drivers.HidHide;
using Nefarius.Utilities.DeviceManagement.Extensions;
using Nefarius.Utilities.DeviceManagement.PnP;

// The Host is deliberately a very small virtual-target adapter. It does not
// read physical input, assemble motion, or decide an owner. Those remain native
// Coordinator responsibilities. Driver preparation reports warnings without rejecting target attempts;
// only the explicitly marked standalone test lane may install
// the embedded HIDMaestro driver on first virtual-target use. The pipe tuple is
// an explicit YMCC process-boundary delta; HC parity is the target lifecycle.
internal static partial class Program
{
    private const int ProtocolVersion = 1;
    // Populated only by the explicit MSI engineering launch flag.
    private static MsiHostStageEvidence? _msiHostStageEvidence;
    // Edge wire 7 carries the full-byte report sequence.
    private static long _dualSenseFrameCounter;
    private const int MaxMessageBytes = 16 * 1024;
    // F3-B（第八十一批 B-变体，2026-09-12 用户授权"因为 HID"）：删 InputHost
    // 自杀门（lease 自杀分支/LeaseExpired/CommandLeaseTimeoutMs/KEEPALIVE 全线）。
    // 原父进程句柄退出时取消连接/读/写，随后由串行路径归还本地资源；
    // 不依赖 pipe 同时断开，无 KEEPALIVE 命令、无租约超时自杀。
    // HIDMaestro's SetupController performs its own PnP waits, but the SDK
    // does not propagate those wait results through HMController.  Keep the
    // Host-side receipt bounded within the native PREPARE_TARGET budget and
    // make the actually-present HIDMaestro topology an explicit precondition
    // for publishing the target-prepared ACK.
    private const int VirtualEndpointRemovalTimeoutMs = 3500;
    private const int VirtualEndpointArrivalTimeoutMs = 3500;
    private const int VirtualEndpointPollIntervalMs = 50;
    // 批107 / T2（2026-09-24 operator 授权"直接改 防止漂移"）：persona 切换
    // 清场的"全族等待"预算（每步各一次，最多两次）。有界性由既有预算推得：
    // native 侧 PREPARE_TARGET 的 pipe 预算 10 s（native/main.cpp:18255），
    // 其中还要容纳建塔后的到达等待 3.5 s ⇒ 2×2500 + 3500 = 8.5 s，留 1.5 s 余量。
    // 释放路径（ReleaseLocal）继续用 VirtualEndpointRemovalTimeoutMs，口径不变。
    private const int SwitchReconcileFamilyWaitMs = 2500;
    // Released Sony contacts retain their finger IDs with the inactive bit set.
    private const byte TouchpadFinger0DisabledId = 0x81;
    private const byte TouchpadFinger1DisabledId = 0x82;
    // Edge status[0]: capacity 10/10, cable bit clear; preserve the current wire byte.
    private const byte SonyWireBatteryFull = 0x0A;
    // Approved upstream HIDMaestro 2.0.0. Keep native, setup and export pins in sync.
    private const long LockedCoreBytes = 63_076_352;
    private const string LockedCoreSha256 = "8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c";
    private const string LockedCoreMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    // The virtual-gamepad feature directory is the canonical home of this Host
    // (PowerControl\feature-assets\virtual-gamepad) in both the formal deploy
    // and isolated test packages.  Older installs kept the Host beside
    // YeManCC.exe; that legacy anchor remains a fallback so marker and journal
    // resolution stay location-independent.
    private static readonly string FeatureAssetsVirtualGamepadDir = ResolveFeatureAssetsVirtualGamepadDir();
    private static readonly string StandaloneTestAutoInstallMarkerPath =
        Path.GetFullPath(Path.Combine(FeatureAssetsVirtualGamepadDir, "test-auto-install.flag"));

    private static string ResolveFeatureAssetsVirtualGamepadDir()
    {
        var baseDir = Path.GetFullPath(AppContext.BaseDirectory).TrimEnd('\\', '/');
        if (string.Equals(Path.GetFileName(baseDir), "virtual-gamepad", StringComparison.OrdinalIgnoreCase))
            return baseDir;
        return Path.GetFullPath(Path.Combine(
            baseDir,
            "..", "..",
            "PowerControl", "feature-assets", "virtual-gamepad"));
    }
    private static readonly HashSet<string> BaseFields = new(StringComparer.Ordinal)
    {
        "protocolVersion", "command", "requestId", "sequence", "runId", "epoch",
        "powerGeneration", "targetId", "ownerIdentity", "persona", "profileIdentity",
        "startupNonce", "configRevision", "configHash",
        "physicalBaseContainerInstanceId", "physicalDeviceInstanceId"
    };
    private static readonly HashSet<string> FrameFields = new(BaseFields, StringComparer.Ordinal)
    {
        "buttons", "lx", "ly", "rx", "ry", "lt", "rt", "sampleSequence",
        "monotonicTick", "normalizedInputHash", "motionPairProofId",
        "ltRaw", "rtRaw",   // G8/HC parity raw wire triggers (native sends these
                            // alongside canonical lt/rt; TrySubmitFrame consumes
                            // them at 1523-1531). Missing from the whitelist
                            // made every SUBMIT_FRAME fail with unknown-field
                            // -> Error(null) -> tuple-or-asset-mismatch, tearing
                            // down the virtual target right after start.
        "imu",              // SteamDeck persona（G1/G3）：frame 携带矩阵后
                            // gyro(dps)/accel(g)/quaternion，供 BuildSteamDeckState
                            // 编码。缺白名单会触发 unknown-field 同款撕裂。
        // 7.52 P1-T1（pipe 切点下移）：native SUBMIT_FRAME 帧新增 raw 域
        // （原生 SHORT/BYTE 透传：buttons/sThumbLX/LY/RX/RY/bLeftTrigger/
        // bRightTrigger，HC XInputController raw 直存语义）。键名与 native
        // main.cpp inputCaptureSubmit 的 frame["raw"] 逐字一致。缺白名单会让
        // 每帧 SUBMIT_FRAME 被判 unknown-field → Error(null) → 拆塔（同 107
        // 行注释旧撕裂）：2026-09-11 实测首开即现，必须补入。
        "raw"
        // 背键映射 Phase A（背键映射A任务书 2026-09-14 S4）：frame 新增可选
        // backButtons（bit0=M1→L5、bit1=M2→R5，native 仅 steamdeck 且开关 ON
        // 时非零）。不进 normalizedInputHash（独立字段，零哈希耦合）；缺省 0
        // ——老 native 不发该字段，白名单缺省解析回退 0，零污染。
        , "backButtons", "touchpads", "screenButtons"
    };
    // HIDHIDE_* 命令专用字段（HC 屏蔽层 API 化：native 经 pipe 下发 hide/
    // unhide/cloak/app-reg 操作，InputHost 内以 Nefarius.Drivers.HidHide
    // HidHideControlService 进程内 API 执行，失败回退 CLI——与 HC Media
    // Misc\HidHide.cs 的 API→CLI fallback 完全一致）。
    private static readonly HashSet<string> HidHideFields = new(BaseFields, StringComparer.Ordinal)
    {
        "path", "on", "cloak"
    };

    private static async Task<int> Main(string[] args)
    {
        YeManCC.ProcessEfficiency.ApplyCurrentProcessOnce();
        // Experimental physical reader has no virtual-target/SDK lifecycle.
        if (args.Length > 0 && args[0] == PhysicalSourceReader.Command)
            return await PhysicalSourceReader.RunAsync(args);
        if (args.Length == 1 && args[0] == "--selftest-physical-source-reader-v1")
            return PhysicalSourceReader.RunSelfTest();
        if (args.Length == 1 && args[0] == "--selftest-external-controller-priority-cycle")
            return ExternalControllerPortCycle.SelfTest() ? 0 : 3;
        if (args.Length > 0 && args[0] == ExternalControllerPortCycle.Command)
            return ExternalControllerPortCycle.Run(args);
        if (args.Length == 1 && args[0] == "--selftest-msi-native-cycle")
            return MsiNativePortCycle.SelfTest() ? 0 : 3;
        if (args.Length > 0 && args[0] == MsiNativePortCycle.Command)
            return MsiNativePortCycle.Run(args);
        if (args.Length == 1 && args[0] == "--selftest-shared-runtime")
            return RuntimeSharedDependencies.RunSelfTest();
        if (args.Length == 1 && args[0] == "--selftest-protocol")
            return RunProtocolSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-virtual-output-admission")
            return RunVirtualOutputAdmissionSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-elite-state")
            return RunEliteStateSelfTest() ? 0 : 3;
        if (args.Length == 2 && args[0] == "--selftest-native-touchpad-fixture")
            return RunNativeScreenPadFixtureTest(args[1]) ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-edge-touchpad")
            return RunEdgeScreenPadSelfTest() ? 0 : 3;
        if(args.Length==1 && args[0]=="--selftest-screen-buttons")return RunScreenButtonsSelfTest()?0:3;
        if (args.Length == 1 && args[0] == "--selftest-steamdeck-touchpads")
            return RunSteamDeckScreenPadSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-steamdeck-state")
            return RunSteamDeckStateSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-edge-wire")
            return RunEdgeWireSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0].StartsWith("--selftest-alloc-probe", StringComparison.Ordinal))
            return RunAllocProbeAsync().GetAwaiter().GetResult();
        if (args.Length == 1 && args[0] == "--selftest-endpoint-fence")
            return RunEndpointFenceSelfTest() ? 0 : 3;
        if (args.Length == 1 && args[0] == "--selftest-backend-advisory")
            return RunBackendAdvisorySelfTest() ? 0 : 3;

        if (!TryParseArguments(args, out var pipeName, out var startupNonce, out var parentPid, out var msiStageRequested, out var argumentError))
        {
            // T0 (2026-09-10): never surface a console/usage popup.  A Host
            // double-clicked from Explorer (or a stale shortcut) previously
            // flashed the console "usage: YeManInputHost.exe ..." window — the
            // reported "Invalid command line" regression.  Exit silently with
            // code 2; the native Coordinator is the only intended launcher and
            // surfaces the same failure through pipe timeouts instead.
            return 2;
        }

        var session = new HostSession(startupNonce!, parentPid, msiStageRequested);
        IDisposable? msiCostLifetime = null;
        MsiHostStageEvidence? msiStage = null;
        try
        {
            return await HostTerminalCleanup.RunAsync(
                async () =>
                {
                    // Once session is owned, initialization failures must settle it too.
                    msiCostLifetime = session.CostDiagnosticLifetime;
                    msiStage = msiStageRequested
                        ? MsiHostStageEvidence.TryCreate(parentPid, _fatalContextHostInstance) : null;
                    _msiHostStageEvidence = msiStage;
                    return await RunPipeClientAsync(pipeName!, parentPid, session).ConfigureAwait(false);
                },
                (out string detail) => session.ReleaseLocal(out detail),
                LogFatalExit).ConfigureAwait(false);
        }
        finally
        {
            _msiHostStageEvidence = null;
            // Keep diagnostics alive through settlement; both owned scopes must retire.
            try { msiStage?.Dispose(); }
            finally { msiCostLifetime?.Dispose(); }
        }
    }

    // GP-PREREQ-4: an unavailable or throwing dependency probe cannot prevent the factory call.
    // Factory failures propagate to the existing PREPARE_TARGET exception/error receipt.
    private static T CreateTargetWithBackendWarning<T>(Func<(bool Ready, string Detail)> prepareBackend,
        Func<T> createTarget, out string backendReceipt)
    {
        try
        {
            var probe = prepareBackend();
            backendReceipt = (probe.Ready ? "backend-ready:" : "backend-warning:") + probe.Detail;
        }
        catch (Exception ex)
        {
            backendReceipt = "backend-warning:backend-check-exception:" + ex.GetType().Name;
        }
        return createTarget();
    }

    private static bool RunBackendAdvisorySelfTest()
    {
        int checks = 0, creates = 0;
        void Check(bool value) { if (!value) throw new InvalidOperationException("backend-advisory-selftest"); checks++; }
        foreach (var backend in new[] { "usbip-backend-unavailable", "driver-not-installed" })
        {
            foreach (var ready in new[] { false, true })
            {
                var previous = creates;
                var target = CreateTargetWithBackendWarning(() => (ready, backend), () => ++creates, out var receipt);
                Check(target == previous + 1 && creates == previous + 1);
                Check(receipt == (ready ? "backend-ready:" : "backend-warning:") + backend);
            }
        }
        var beforeThrow = creates;
        CreateTargetWithBackendWarning(() => throw new IOException("probe"), () => ++creates, out var warning);
        Check(creates == beforeThrow + 1 && warning == "backend-warning:backend-check-exception:IOException");
        var failure = new InvalidOperationException("factory");
        var failureReceipt = "";
        try
        {
            CreateTargetWithBackendWarning<int>(() => (false, "driver-not-installed"), () => throw failure, out failureReceipt);
            return false;
        }
        catch (InvalidOperationException ex)
        {
            Check(ReferenceEquals(ex, failure));
            Check(failureReceipt == "backend-warning:driver-not-installed");
        }
        Console.WriteLine($"BACKEND_ADVISORY_PASS checks={checks} creates={creates} deviceOperations=0");
        return true;
    }

    // Preserve the current endpoint counts and reject every retired virtual persona.
    private static bool RunEndpointFenceSelfTest()
    {
        static VirtualEndpointObservation O(int primary, int companion, int hid, int steam, int steamHid) =>
            new(true, primary, companion, hid, steam, steamHid, true, "selftest");

        var absent = O(0, 0, 0, 0, 0);
        var edge = O(1, 0, 1, 0, 0);
        var steamDeck = O(0, 0, 0, 1, 1);
        // L-2 回归位：复合根 + MI_00 接口 = root 计数 2（老 `== 1` 永不可满足的死锁形状）。
        var steamDeckTwoRoots = O(0, 0, 0, 2, 1);
        // L-1 回归位：HIDMAESTRO 本族唯一 + usbip 残留（旧语义下必须被拒绝）。
        var mixed = O(1, 0, 1, 1, 1);
        var elite = O(1, 1, 1, 0, 0);
        var eliteWithStaleUsbip = O(1, 1, 1, 1, 1);
        // 本族严格性回归位：同族重复端点。
        var duplicateEdge = O(2, 0, 2, 0, 0);
        var duplicateElite = O(2, 1, 2, 0, 0);
        var compositeRootId = IsSteamDeckCompositeHardwareId("USB\\VID_28DE&PID_1205");
        var compositeRevisionId = IsSteamDeckCompositeHardwareId("USB\\VID_28DE&PID_1205&REV_0300");
        var interfaceId = IsSteamDeckCompositeHardwareId("USB\\VID_28DE&PID_1205&MI_00");
        var cases = new[]
        {
            absent.IsAbsent,                                            // 全空 = absent
            steamDeck.IsAbsent,                                         // L-3：仅 usbip 在场 = HIDMAESTRO 平面已清空（旧语义 false）
            edge.IsUniqueStartedTarget("dualsense-edge"),
            !steamDeck.IsUniqueStartedTarget("dualsense-edge"),             // 无本族端点 ⇒ 不通过
            steamDeck.IsUniqueStartedTarget("steamdeck"),
            steamDeckTwoRoots.IsUniqueStartedTarget("steamdeck"),       // L-2：root 计数 2 也通过（老 `== 1` 会假）
            mixed.IsUniqueStartedTarget("dualsense-edge"),                  // L-1：他族残留不阻塞本族建塔（旧语义 false）
            elite.IsUniqueStartedTarget("elite"),
            eliteWithStaleUsbip.IsUniqueStartedTarget("elite"),     // L-1：360 同样不再被他族残留阻塞
            !duplicateEdge.IsUniqueStartedTarget("dualsense-edge"),          // 同族重复仍拒绝
            !duplicateElite.IsUniqueStartedTarget("elite"),         // 同族重复仍拒绝
            compositeRootId,
            compositeRevisionId,
            !interfaceId,
            // 批107 / T2：全族判空（切换清场专用）——与 IsAbsent（L-3）的口径差异
            // 显式成对断言：仅 usbip 在场时 IsAbsent 真而 IsFullyAbsent 假。
            absent.IsFullyAbsent,                                       // 全族空 = fully absent
            !edge.IsFullyAbsent,                                         // HIDMAESTRO 在场 ⇒ 非全族空
            !steamDeck.IsFullyAbsent,                                   // 仅 usbip 在场：IsAbsent 真 / IsFullyAbsent 假
            !mixed.IsFullyAbsent,                                       // 两族都在场
        };
        // 自检（项目纪律）：用例数≠预期即判 FAIL——避免"断言被删/漏写"造成假绿。
        const int expectedCaseCount = 18;
        if (cases.Length != expectedCaseCount) return false;
        foreach (var passed in cases)
        {
            if (!passed) return false;
        }
        foreach (var retired in new[] { "xbox360", "dualshock4", "dualsense", "disabled", "unknown" })
            if (edge.IsUniqueStartedTarget(retired) || elite.IsUniqueStartedTarget(retired) || steamDeck.IsUniqueStartedTarget(retired)) return false;
        return true;
    }

    private static bool TryParseArguments(string[] args, out string? pipeName, out string? startupNonce, out int parentPid, out bool msiStageRequested, out string error)
    {
        pipeName = null;
        startupNonce = null;
        parentPid = 0;
        msiStageRequested = false;
        error = "usage: YeManInputHost.exe --pipe <name> --nonce <nonce> --parent-pid <pid>";
        if (!(args.Length == 6 || (args.Length == 7 && args[6] == "--msi-host-stage-evidence-v1")) ||
            args[0] != "--pipe" || args[2] != "--nonce" || args[4] != "--parent-pid") return false;
        if (string.IsNullOrWhiteSpace(args[1]) || args[1].Length > 180 || args[1].Any(char.IsControl))
        {
            error = "invalid-pipe-name";
            return false;
        }
        if (string.IsNullOrWhiteSpace(args[3]) || args[3].Length is < 32 or > 160 || args[3].Any(char.IsControl))
        {
            error = "invalid-startup-nonce";
            return false;
        }
        if (!int.TryParse(args[5], out parentPid) || parentPid <= 0)
        {
            error = "invalid-parent-pid";
            return false;
        }
        pipeName = args[1];
        startupNonce = args[3];
        msiStageRequested = args.Length == 7;
        return true;
    }

    private static async Task<HostTerminalCleanup.LoopResult> RunPipeClientAsync(string pipeName, int parentPid, HostSession session)
    {
        // Native Coordinator owns the server; this process is a bounded client
        // that can never originate a target command. Keep one message-mode
        // connection for the whole Host session: reconnecting for every frame
        // adds a named-pipe handshake to the HID->virtual-output path.
        // Bind the original parent once; a recycled PID cannot revive this Host.
        using var parentLifetime = ParentProcessLifetime.TryOpen(parentPid);
        if (parentLifetime is null || !parentLifetime.IsAlive)
        {
            LogFatalExit("parent-exited", "parent-process-gone");
            return new(0, "parent-exited");
        }
        var codec = new MessageCodec();   // GP-CPU2 P2/P3：会话级缓冲（每 Host 会话一份）
        await using var pipe = new NamedPipeClientStream(
            ".",
            pipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous);
        try
        {
            await pipe.ConnectAsync(3000, parentLifetime.ExitToken).ConfigureAwait(false);
            pipe.ReadMode = PipeTransmissionMode.Message;
        }
        catch (OperationCanceledException) when (!parentLifetime.IsAlive)
        {
            LogFatalExit("parent-exited", "parent-process-gone");
            return new(0, "parent-exited");
        }
        catch (Exception ex) when (ex is TimeoutException or IOException)
        {
            LogFatalExit("connect-failed", ex.GetType().Name);
            return new(4, "connect-failed");
        }

        while (parentLifetime.IsAlive)
        {
            // The bound parent exit cancels even a pipe held open by another
            // process. Release remains serialized after the pending IO ends.
            string? request;
            // R3 补充：管道读侧计时（门控内；含等待，见 RecordCostPipeRead 注释）。
            var costReadStarted = session.CostEnabledForRead ? Stopwatch.GetTimestamp() : 0L;
            try
            {
                _msiHostStageEvidence?.BeginRead();
                request = await codec.ReadAsync(pipe, parentLifetime.ExitToken).ConfigureAwait(false);
                _msiHostStageEvidence?.EndRead(request?.Length ?? 0);
            }
            catch (OperationCanceledException) when (!parentLifetime.IsAlive) { break; }
            catch (Exception ex)
            {
                LogFatalExit("read-failed", ex.GetType().Name);
                return new(5, "read-failed");
            }

            if (!parentLifetime.IsAlive) break;
            if (request is null)
            {
                LogFatalExit("read-failed", "null-message");
                return new(5, "read-failed");
            }
            if (costReadStarted != 0)
                session.RecordCostPipeRead((long)(Stopwatch.GetElapsedTime(costReadStarted).TotalMilliseconds * 1000.0));

            _msiHostStageEvidence?.BeginHandle();
            var response = session.Handle(request, out var shutdown);
            var shutdownResult = shutdown || HostTerminalCleanup.IsFailedShutdownResponse(response)
                ? HostTerminalCleanup.ShutdownResult(response) : (HostTerminalCleanup.LoopResult?)null;
            _msiHostStageEvidence?.EndHandle();
            try
            {
                _msiHostStageEvidence?.BeginWrite();
                await codec.WriteAsync(pipe, response, parentLifetime.ExitToken).ConfigureAwait(false);
                _msiHostStageEvidence?.EndWrite();
            }
            catch (OperationCanceledException) when (!parentLifetime.IsAlive)
            {
                LogFatalExit("parent-exited", "parent-process-gone");
                return new(0, "parent-exited", shutdownResult?.ShutdownRelease);
            }
            catch (Exception ex)
            {
                // A native timeout/disconnect makes the owner unprovable.
                // Release locally and exit; never keep a background writer.
                // X1：宿主死因落盘（CREATE_NO_WINDOW 丢弃 stderr，见 LogFatalExit 头注释）。
                LogFatalExit("write-failed", ex.GetType().Name + ": " + ex.Message);
                return new(6, "write-failed", shutdownResult?.ShutdownRelease);
            }

            if (shutdownResult is { } terminal) { LogFatalExit("shutdown-ordered", "shutdown-command"); return terminal; }   // 2026-09-23 W3 批 100（跨线共报 #2）：有序 SHUTDOWN 与"父进程消失"必须可区分；旧文案让 5 次正常收尾看起来像 5 次父进程死亡（实测 20:19/20:21/20:22 三次 App 并未退出）。
        }

        // Parent crash/exit is terminal, never an automatic rearm condition.
        LogFatalExit("parent-exited", "parent-process-gone");
        return new(0, "parent-exited");
    }



    // ── GP-CPU2 P2/P3（裁决：窄版）——复用会话级读/写缓冲 ──
    // 原实现每条消息：`new MemoryStream()` + `new byte[4096]`（读）与 `Encoding.UTF8.GetBytes`（写）。
    // 现在：每会话一份读缓冲 + 复用 MemoryStream；写侧复用 UTF-8 输出缓冲并**只写实际长度**。
    // 语义不变（裁决边界）：消息模式分片循环 `IsMessageComplete`、EOF→null、
    // 超限→message-too-large/response-too-large、**一次响应一次消息写入**（无 Flush 变化）、
    // 单消费者串行 await（缓冲在 await 期间不被第二条消息复用）。
    private sealed class MessageCodec
    {
        private readonly byte[] _readBuffer = new byte[MaxMessageBytes];
        private readonly MemoryStream _readBody = new MemoryStream(MaxMessageBytes);
        private byte[] _writeBuffer = new byte[4096];

        public async Task<string?> ReadAsync(PipeStream pipe, CancellationToken cancellationToken)
        {
            _readBody.SetLength(0);
            do
            {
                var count = await pipe.ReadAsync(_readBuffer, cancellationToken).ConfigureAwait(false);
                if (count == 0) return null;
                _readBody.Write(_readBuffer, 0, count);
                if (_readBody.Length > MaxMessageBytes) throw new InvalidDataException("message-too-large");
            } while (!pipe.IsMessageComplete);
            return Encoding.UTF8.GetString(_readBody.GetBuffer(), 0, checked((int)_readBody.Length));
        }

        public async Task WriteAsync(PipeStream pipe, string message, CancellationToken cancellationToken)
        {
            var required = Encoding.UTF8.GetByteCount(message);
            if (required > MaxMessageBytes) throw new InvalidDataException("response-too-large");
            if (_writeBuffer.Length < required) _writeBuffer = new byte[required];
            Encoding.UTF8.GetBytes(message, 0, message.Length, _writeBuffer, 0);
            // NamedPipe message mode preserves this write as one message. The
            // native Coordinator reads the response immediately; an explicit
            // FlushAsync adds a second synchronous wait to every HC-style tick
            // without changing the message boundary or delivery contract.
            await pipe.WriteAsync(_writeBuffer.AsMemory(0, required), cancellationToken).ConfigureAwait(false);
        }
    }

    // ── GP-CPU2（S0/验收）：非常驻探针 —— 同机同场景的「分配 / 尾延迟 / 协议差分」三合一 ──
    // 只调用现有生产入口（HostSession.Handle / ReadMessageAsync / WriteMessageAsync），
    // 不新增任何生产路径、不接触设备、非常驻。旧/新两次构建各跑一次即得对照表。
    private static async Task<int> RunAllocProbeAsync()
    {
        const string hash = "a3d10d49211a38883c647eea27268b7639b414bb377d63f9419f2e54ce42e281";
        const string nonce = "alloc-probe-nonce-0123456789";
        var nArg = Environment.GetCommandLineArgs().FirstOrDefault(a => a.StartsWith("--selftest-alloc-probe=", StringComparison.Ordinal));
        var N = nArg is null ? 2000 : Math.Max(10, int.Parse(nArg.Split('=')[1], System.Globalization.CultureInfo.InvariantCulture));
        Console.WriteLine("PROBE-START n=" + N);
        var frameHash = CanonicalFrameHash(0, 0, 0, 0, 0, 0, 0);
        string Hello() =>
            $$"""{"protocolVersion":1,"command":"HELLO","requestId":"r0","sequence":1,"runId":"run-1","epoch":1,"powerGeneration":1,"targetId":"target-1","ownerIdentity":"coordinator","persona":"dualsense-edge","profileIdentity":"dualsense-edge","startupNonce":"{{nonce}}","configRevision":1,"configHash":"{{hash}}"}""";
        string Frame(int i) =>
            $$"""{"protocolVersion":1,"command":"SUBMIT_FRAME","requestId":"f{{i}}","sequence":{{i + 2}},"runId":"run-1","epoch":1,"powerGeneration":1,"targetId":"target-1","ownerIdentity":"coordinator","persona":"dualsense-edge","profileIdentity":"dualsense-edge","startupNonce":"{{nonce}}","configRevision":1,"configHash":"{{hash}}","buttons":0,"lx":0,"ly":0,"rx":0,"ry":0,"lt":0,"rt":0,"sampleSequence":{{i + 1}},"monotonicTick":{{i + 1}},"normalizedInputHash":"{{frameHash}}","motionPairProofId":"pair-unproven-safe-zero"}""";

        // A) 解析 + 处理（合法帧路径）
        var session = new HostSession(nonce);
        Console.WriteLine("PROBE-STAGE parse-begin");
        var helloResp = session.Handle(Hello(), out _);
        Console.WriteLine("PROBE-STAGE hello " + (helloResp.Length > 90 ? helloResp[..90] : helloResp));
        for (var i = 0; i < 200; ++i) session.Handle(Frame(i), out _);
        var lat = new double[N];
        var alloc0 = GC.GetTotalAllocatedBytes(precise: false);   // 进程内单调；异步续体跨线程不受影响
        for (var i = 0; i < N; ++i)
        {
            var t0 = System.Diagnostics.Stopwatch.GetTimestamp();
            session.Handle(Frame(i), out _);
            lat[i] = System.Diagnostics.Stopwatch.GetElapsedTime(t0).TotalMicroseconds;
        }
        var parseAlloc = (GC.GetTotalAllocatedBytes(precise: false) - alloc0) / (double)N;
        Array.Sort(lat);
        double P(double q) => lat[(int)Math.Min(N - 1, Math.Max(0, Math.Round(q * (N - 1))))];

        Console.WriteLine("PARSE-DONE allocPerMsg=" + parseAlloc.ToString("F1", System.Globalization.CultureInfo.InvariantCulture));
        // B) 传输（本地消息模式命名管道对，构造口径与生产同源；**有界**：超时只报告不阻塞）
        double txAlloc = -1, txP50 = -1, txP99 = -1;
        try
        {
            using var txCts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            var clientCodec = new MessageCodec();
            var serverCodec = new MessageCodec();
            var pipeName = "ymcc-cpu2-probe-" + Guid.NewGuid().ToString("N");
            using var server = new System.IO.Pipes.NamedPipeServerStream(
                pipeName, System.IO.Pipes.PipeDirection.InOut, 1,
                System.IO.Pipes.PipeTransmissionMode.Message, System.IO.Pipes.PipeOptions.Asynchronous);
            using var client = new System.IO.Pipes.NamedPipeClientStream(
                ".", pipeName, System.IO.Pipes.PipeDirection.InOut, System.IO.Pipes.PipeOptions.Asynchronous);
            var accept = server.WaitForConnectionAsync();
            client.Connect(3000);
            accept.Wait(3000);
            client.ReadMode = System.IO.Pipes.PipeTransmissionMode.Message;
            var txLat = new double[N];
            var alloc1 = GC.GetTotalAllocatedBytes(precise: false);   // 同上（跨线程有效）
            for (var i = 0; i < N && !txCts.IsCancellationRequested; ++i)
            {
                var t0 = System.Diagnostics.Stopwatch.GetTimestamp();
                // 0 缓冲消息模式管道：写会阻塞到对端读取 ⇒ 写与读必须**并发**（与生产的
                // "Coordinator 侧读 + Host 侧读"并发拓扑同源），否则自身死锁。
                var writeTask = clientCodec.WriteAsync(client, Frame(i), txCts.Token);
                var readTask = serverCodec.ReadAsync(server, txCts.Token);
                await Task.WhenAll(writeTask, readTask).ConfigureAwait(false);
                var onServer = readTask.Result;
                var respTask = serverCodec.WriteAsync(server, onServer ?? "", txCts.Token);
                var backTask = clientCodec.ReadAsync(client, txCts.Token);
                await Task.WhenAll(respTask, backTask).ConfigureAwait(false);
                var onClient = backTask.Result;
                if (onClient is null || onClient.Length != (onServer?.Length ?? -1))
                    throw new InvalidDataException("transport-probe-mismatch");
                txLat[i] = System.Diagnostics.Stopwatch.GetElapsedTime(t0).TotalMicroseconds;
            }
            txAlloc = (GC.GetTotalAllocatedBytes(precise: false) - alloc1) / (double)N;
            Array.Sort(txLat);
            txP50 = txLat[(int)Math.Round(0.50 * (N - 1))];
            txP99 = txLat[(int)Math.Round(0.99 * (N - 1))];
        }
        catch (Exception ex)
        {
            Console.WriteLine("transport-probe-error " + ex.GetType().Name + ":" + ex.Message);
        }

        Console.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "ALLOC-PROBE parseAllocPerMsg={0:F1}B parseP50us={1:F1} parseP95us={2:F1} parseP99us={3:F1} " +
            "txAllocPerMsg={4:F1}B txP50us={5:F1} txP99us={6:F1} n={7}",
            parseAlloc, P(0.50), P(0.95), P(0.99), txAlloc, txP50, txP99, N));

        // C) 协议差分（逐字节回执；旧/新构建输出必须一致）
        var cases = new (string Name, string Message)[]
        {
            ("legal-frame", Frame(0)),
            ("unknown-field", Hello()[..^1] + ",\"unknown\":1}"),
            ("duplicate-field", Hello()[..^1] + ",\"epoch\":1}"),
            ("unknown-field+duplicate", Hello()[..^1] + ",\"unknown\":1,\"epoch\":1}"),
            ("unknown-command", Hello().Replace("\"HELLO\"", "\"BOGUS\"", StringComparison.Ordinal)),
            ("negative-zero", Frame(0).Replace("\"lx\":0", "\"lx\":-0", StringComparison.Ordinal)),
            ("frame-before-prepare", Frame(0))
        };
        foreach (var (name, message) in cases)
        {
            var s = new HostSession(nonce);
            s.Handle(Hello(), out _);
            var preview = name == "legal-frame" ? null : s.Handle(message, out _);
            if (name == "legal-frame") preview = s.Handle(message, out _);
            Console.WriteLine("DIFF " + name + " " + preview);
        }
        return 0;
    }

    private static bool RunVirtualOutputAdmissionSelfTest()
    {
        var current = new[] { ("steamdeck", "steam-deck-composite"), ("dualsense-edge", "dualsense-edge"), ("elite", "xbox-elite-v2") };
        var retired = new[] { ("xbox360", "xbox-360-wired"), ("dualshock4", "dualshock-4-v2"), ("dualsense", "dualsense"), ("unknown", "dualsense-edge"), ("disabled", "disabled") };
        var commands = new[] { "HELLO", "PREPARE_TARGET", "RECREATE_TARGET", "SUBMIT_NEUTRAL", "SUBMIT_FRAME" };
        int checks = 0;
        bool Check(string persona, string profile, string command, bool expected)
        {
            var values = new Dictionary<string, object>
            {
                ["protocolVersion"] = 1, ["command"] = command, ["requestId"] = "admission", ["sequence"] = 1,
                ["runId"] = "admission", ["epoch"] = 1, ["powerGeneration"] = 1, ["targetId"] = "target",
                ["ownerIdentity"] = "owner", ["persona"] = persona, ["profileIdentity"] = profile,
                ["startupNonce"] = "admission-nonce", ["configRevision"] = 1, ["configHash"] = new string('a', 64)
            };
            if (command == "SUBMIT_FRAME")
            {
                foreach (var key in new[] { "buttons", "lx", "ly", "rx", "ry", "lt", "rt" }) values[key] = 0;
                values["sampleSequence"] = 1; values["monotonicTick"] = 1;
                values["normalizedInputHash"] = CanonicalFrameHash(0, 0, 0, 0, 0, 0, 0);
                values["motionPairProofId"] = "pair-unproven-safe-zero";
            }
            var accepted = TryParseEnvelope(JsonSerializer.Serialize(values), out var envelope, out var error);
            checks++;
            return accepted == expected && (expected ? envelope is not null : envelope is null && error == "persona-not-admitted");
        }
        foreach (var command in commands)
        {
            foreach (var (persona, profile) in current)
            {
                if (!Check(persona, profile, command, true)) return false;
                foreach (var (_, otherProfile) in current)
                    if (otherProfile != profile && !Check(persona, otherProfile, command, false)) return false;
            }
            foreach (var (persona, profile) in retired)
                if (!Check(persona, profile, command, false)) return false;
        }
        Console.WriteLine($"virtual-output-admission: OK {checks} strict current profile-pair and retired/unknown wire checks; no Host or hardware constructed");
        return checks == 70;
    }

    private static bool RunProtocolSelfTest()
    {
        const string hash = "a3d10d49211a38883c647eea27268b7639b414bb377d63f9419f2e54ce42e281";
        const string nonce = "t10-selftest-startup-nonce-0123456789";
        var hello = $$"""{"protocolVersion":1,"command":"HELLO","requestId":"r1","sequence":1,"runId":"run-1","epoch":1,"powerGeneration":1,"targetId":"target-1","ownerIdentity":"coordinator","persona":"dualsense-edge","profileIdentity":"dualsense-edge","startupNonce":"{{nonce}}","configRevision":1,"configHash":"{{hash}}"}""";
        var frameHash = CanonicalFrameHash(0, 0, 0, 0, 0, 0, 0);
        // F3-B：keepAliveBeforePrepare 定义已删除（KEEPALIVE 全线移除）。
        var frameBeforePrepare = $$"""{"protocolVersion":1,"command":"SUBMIT_FRAME","requestId":"r2","sequence":2,"runId":"run-1","epoch":1,"powerGeneration":1,"targetId":"target-1","ownerIdentity":"coordinator","persona":"dualsense-edge","profileIdentity":"dualsense-edge","startupNonce":"{{nonce}}","configRevision":1,"configHash":"{{hash}}","buttons":0,"lx":0,"ly":0,"rx":0,"ry":0,"lt":0,"rt":0,"sampleSequence":1,"monotonicTick":1,"normalizedInputHash":"{{frameHash}}","motionPairProofId":"pair-unproven-safe-zero"}""";
        var unknownField = hello[..^1] + ",\"unknown\":1}";
        var nonceMismatch = hello.Replace(nonce, nonce + "-mismatch", StringComparison.Ordinal);
        var negativeZero = frameBeforePrepare.Replace("\"lx\":0", "\"lx\":-0", StringComparison.Ordinal);

        var session = new HostSession(nonce);
        var helloResponse = session.Handle(hello, out var helloShutdown);
        var invalidResponse = session.Handle(unknownField, out var invalidShutdown);
        var nonceResponse = session.Handle(nonceMismatch, out var nonceShutdown);
        var negativeZeroResponse = session.Handle(negativeZero, out var negativeZeroShutdown);
        // F3-B（第八十一批 B-变体）：KEEPALIVE 已从协议删除，selftest 相应段移除。
        var frameResponse = session.Handle(frameBeforePrepare, out var frameShutdown);
        // 批次A（评审10 §3）：精简状态查询协议门。帧（sequence 2）已被消费，
        // 故这两条用 sequence 3/4；断言关注协议语义而非本机是否装有 HidHide：
        //   · 精简回执 detail 是可解析 JSON 且**不含 applications**；
        //   · 完整 HIDHIDE_QUERY 语义不变（status 仍为 hidhide-query）。
        var slimQuery = hello
            .Replace("\"command\":\"HELLO\"", "\"command\":\"HIDHIDE_QUERY_STATE\"", StringComparison.Ordinal)
            .Replace("\"requestId\":\"r1\"", "\"requestId\":\"r3\"", StringComparison.Ordinal)
            .Replace("\"sequence\":1", "\"sequence\":3", StringComparison.Ordinal);
        var fullQuery = hello
            .Replace("\"command\":\"HELLO\"", "\"command\":\"HIDHIDE_QUERY\"", StringComparison.Ordinal)
            .Replace("\"requestId\":\"r1\"", "\"requestId\":\"r4\"", StringComparison.Ordinal)
            .Replace("\"sequence\":1", "\"sequence\":4", StringComparison.Ordinal);
        var slimResponse = session.Handle(slimQuery, out var slimShutdown);
        var fullResponse = session.Handle(fullQuery, out var fullShutdown);
        // GP-926R17 §4：聚合回执协议门（frame 已被消费，故用 sequence 5；只读、绑当前 tuple）。
        var statsQuery = hello
            .Replace("\"command\":\"HELLO\"", "\"command\":\"TUPLE_SUBMIT_STATS\"", StringComparison.Ordinal)
            .Replace("\"requestId\":\"r1\"", "\"requestId\":\"r5\"", StringComparison.Ordinal)
            .Replace("\"sequence\":1", "\"sequence\":5", StringComparison.Ordinal);
        var statsResponse = session.Handle(statsQuery, out var statsShutdown);
        // GP-GBRETURN-1：关闭归还刷新协议门（sequence 6/7）。**空 path** ⇒ 只断言受理判据，
        // 不触发任何设备操作（本自测无设备、无驱动、无真实端口）：BEGIN 必须 accepted=false
        // 且 reason=false-empty-path；再接一次 POLL 必须仍为 idle（空路径没占用在途门）。
        var cycleBeginQuery = hello
            .Replace("\"command\":\"HELLO\"", "\"command\":\"HIDHIDE_CYCLE_BEGIN\"", StringComparison.Ordinal)
            .Replace("\"requestId\":\"r1\"", "\"requestId\":\"r6\"", StringComparison.Ordinal)
            .Replace("\"sequence\":1", "\"sequence\":6", StringComparison.Ordinal);
        var cycleBeginResponse = session.Handle(cycleBeginQuery, out var cycleBeginShutdown);
        var cyclePollQuery = hello
            .Replace("\"command\":\"HELLO\"", "\"command\":\"HIDHIDE_CYCLE_POLL\"", StringComparison.Ordinal)
            .Replace("\"requestId\":\"r1\"", "\"requestId\":\"r7\"", StringComparison.Ordinal)
            .Replace("\"sequence\":1", "\"sequence\":7", StringComparison.Ordinal);
        var cyclePollResponse = session.Handle(cyclePollQuery, out var cyclePollShutdown);
        // GP-GBRETURN-1：**不存在设备**路径的短受理→后台执行→有界查询全链自测（不触碰任何真实
        // 端口/驱动/设备）：BEGIN 必须 accepted=true 并立即返回；后台查不到设备 ⇒ 终态 failed；
        // 有界轮询必须读到终态且 inFlight 复位。轮询次数不定 ⇒ 序列号动态推进（每拍唯一 requestId）。
        long closeRefreshSeq = 8;
        string CloseRefreshMsg(string command, string requestId, string? path)
        {
            var message = hello
                .Replace("\"command\":\"HELLO\"", "\"command\":\"" + command + "\"", StringComparison.Ordinal)
                .Replace("\"requestId\":\"r1\"", "\"requestId\":\"" + requestId + "\"", StringComparison.Ordinal)
                .Replace("\"sequence\":1", "\"sequence\":" + closeRefreshSeq.ToString(), StringComparison.Ordinal);
            return path is null
                ? message
                : message[..^1] + ",\"path\":\"" + path.Replace("\\", "\\\\") + "\"}";   // JSON 转义（wire 由 nlohmann 生成，自测这里手工拼）
        }
        var bogusBeginResponse = session.Handle(
            CloseRefreshMsg("HIDHIDE_CYCLE_BEGIN", "r8", "USB\\VID_0000&PID_0000\\SELFTEST-NO-DEVICE"), out var bogusShutdown);
        closeRefreshSeq++;
        var bogusBeginDetail = JsonDocument.Parse(
            JsonDocument.Parse(bogusBeginResponse).RootElement.GetProperty("detail").GetString() ?? "").RootElement;
        var bogusAccepted = bogusBeginDetail.GetProperty("accepted").ValueKind == JsonValueKind.True;
        var closeRefreshTerminal = "";
        var closeRefreshInFlightAtEnd = true;
        for (var pollIndex = 0; pollIndex < 40; ++pollIndex)
        {
            var pollResponse = session.Handle(
                CloseRefreshMsg("HIDHIDE_CYCLE_POLL", "rp" + pollIndex.ToString(), null), out _);
            closeRefreshSeq++;
            using var pollDoc = JsonDocument.Parse(pollResponse);
            using var pollDetailDoc = JsonDocument.Parse(pollDoc.RootElement.GetProperty("detail").GetString() ?? "");
            closeRefreshTerminal = pollDetailDoc.RootElement.GetProperty("state").GetString() ?? "";
            closeRefreshInFlightAtEnd = pollDetailDoc.RootElement.GetProperty("inFlight").ValueKind == JsonValueKind.True;
            if (closeRefreshTerminal is "done" or "failed") break;
            System.Threading.Thread.Sleep(25);
        }
        var recreateSession = new HostSession(nonce);
        var recreateBeforeHello = hello.Replace("\"command\":\"HELLO\"", "\"command\":\"RECREATE_TARGET\"", StringComparison.Ordinal);
        var recreateResponse = recreateSession.Handle(recreateBeforeHello, out var recreateShutdown);
        using var helloDoc = JsonDocument.Parse(helloResponse);
        using var invalidDoc = JsonDocument.Parse(invalidResponse);
        using var nonceDoc = JsonDocument.Parse(nonceResponse);
        using var negativeZeroDoc = JsonDocument.Parse(negativeZeroResponse);
        using var frameDoc = JsonDocument.Parse(frameResponse);
        using var slimDoc = JsonDocument.Parse(slimResponse);
        using var fullDoc = JsonDocument.Parse(fullResponse);
        using var statsDoc = JsonDocument.Parse(statsResponse);
        using var cycleBeginDoc = JsonDocument.Parse(cycleBeginResponse);
        using var cyclePollDoc = JsonDocument.Parse(cyclePollResponse);
        using var recreateDoc = JsonDocument.Parse(recreateResponse);
        var slimDetail = slimDoc.RootElement.GetProperty("detail").GetString() ?? "";
        using var slimDetailDoc = JsonDocument.Parse(slimDetail);
        // 批次A v2（评审12 R3）：精简回执形状的**可控成功/失败用例**——不依赖本机是否
        // 安装 HidHide，直接断言两个形状（成功=ok/cloakOn/hidden 齐备且无 applications；
        // 失败=ok:false 且不带 hidden/cloakOn，native 严格校验会判无效并进入回退阶梯）。
        var okShape = HostSession.ComposeHidHidePeriodicStateDetail(true, true, new[] { "USB\\VID_045E&PID_028E\\SELFTEST" });
        var failShape = HostSession.ComposeHidHidePeriodicStateDetail(false, false, null);
        using var okShapeDoc = JsonDocument.Parse(okShape);
        using var failShapeDoc = JsonDocument.Parse(failShape);
        var okShapeValid =
            okShapeDoc.RootElement.GetProperty("ok").ValueKind == JsonValueKind.True &&
            okShapeDoc.RootElement.GetProperty("cloakOn").ValueKind == JsonValueKind.True &&
            okShapeDoc.RootElement.GetProperty("hidden").ValueKind == JsonValueKind.Array &&
            okShapeDoc.RootElement.GetProperty("hidden").GetArrayLength() == 1 &&
            !okShape.Contains("applications", StringComparison.Ordinal);
        var failShapeValid =
            failShapeDoc.RootElement.GetProperty("ok").ValueKind == JsonValueKind.False &&
            !failShapeDoc.RootElement.TryGetProperty("hidden", out _) &&
            !failShapeDoc.RootElement.TryGetProperty("cloakOn", out _);
        // GP-MSIG3E-6-LC-R2 §四 R4：完整 HIDHIDE_QUERY 的可选只读 inverse 三态形状——不依赖本机
        // 是否安装 HidHide，直接断言生产者：readOk=false ⇒ unknown（读异常不影响其它字段）。
        var inverseShapeValid =
            HostSession.ComposeHidHideInverseState(true, true) == "on" &&
            HostSession.ComposeHidHideInverseState(true, false) == "off" &&
            HostSession.ComposeHidHideInverseState(false, false) == "unknown" &&
            HostSession.ComposeHidHideInverseState(false, true) == "unknown";
        // GP-GBRETURN-1：BEGIN（空 path 拒绝）+ POLL（idle）协议门——detail 必须是可解析 JSON。
        var cycleBeginDetail = cycleBeginDoc.RootElement.GetProperty("detail").GetString() ?? "";
        var cyclePollDetail = cyclePollDoc.RootElement.GetProperty("detail").GetString() ?? "";
        bool cycleBeginValid;
        using (var cycleBeginDetailDoc = JsonDocument.Parse(cycleBeginDetail))
            cycleBeginValid =
                cycleBeginDoc.RootElement.GetProperty("ok").GetBoolean() &&
                cycleBeginDoc.RootElement.GetProperty("status").GetString() == "hidhide-cycle-begin" &&
                cycleBeginDetailDoc.RootElement.GetProperty("accepted").ValueKind == JsonValueKind.False &&
                cycleBeginDetailDoc.RootElement.GetProperty("reason").GetString() == "false-empty-path";
        bool cyclePollValid;
        using (var cyclePollDetailDoc = JsonDocument.Parse(cyclePollDetail))
            cyclePollValid =
                cyclePollDoc.RootElement.GetProperty("ok").GetBoolean() &&
                cyclePollDoc.RootElement.GetProperty("status").GetString() == "hidhide-cycle-poll" &&
                cyclePollDetailDoc.RootElement.GetProperty("state").GetString() == "idle" &&
                cyclePollDetailDoc.RootElement.GetProperty("inFlight").ValueKind == JsonValueKind.False;
        return !helloShutdown && !invalidShutdown && !nonceShutdown && !negativeZeroShutdown && !frameShutdown &&
               !slimShutdown && !fullShutdown && !statsShutdown &&
               !cycleBeginShutdown && !cyclePollShutdown && !bogusShutdown &&
               cycleBeginValid && cyclePollValid &&
               bogusAccepted && closeRefreshTerminal == "failed" && !closeRefreshInFlightAtEnd &&
               !recreateShutdown &&
               okShapeValid && failShapeValid && inverseShapeValid &&
               helloDoc.RootElement.GetProperty("ok").GetBoolean() &&
               (helloDoc.RootElement.GetProperty("detail").GetString() ?? "").Contains("hidhideStateQuery", StringComparison.Ordinal) &&
               !invalidDoc.RootElement.GetProperty("ok").GetBoolean() &&
               !nonceDoc.RootElement.GetProperty("ok").GetBoolean() &&
               !negativeZeroDoc.RootElement.GetProperty("ok").GetBoolean() &&
               !frameDoc.RootElement.GetProperty("ok").GetBoolean() &&
               slimDoc.RootElement.GetProperty("ok").GetBoolean() &&
               slimDoc.RootElement.GetProperty("status").GetString() == "hidhide-query-state" &&
               slimDetailDoc.RootElement.TryGetProperty("ok", out _) &&
               !slimDetail.Contains("applications", StringComparison.Ordinal) &&
               fullDoc.RootElement.GetProperty("status").GetString() == "hidhide-query" &&
               statsDoc.RootElement.GetProperty("ok").GetBoolean() &&
               statsDoc.RootElement.GetProperty("status").GetString() == "submit-stats" &&
               (statsDoc.RootElement.GetProperty("detail").GetString() ?? "").Contains("frameOk", StringComparison.Ordinal) &&
               (statsDoc.RootElement.GetProperty("detail").GetString() ?? "").Contains("runId", StringComparison.Ordinal) &&
               !recreateDoc.RootElement.GetProperty("ok").GetBoolean() &&
               recreateDoc.RootElement.GetProperty("status").GetString() == "protocol-error" &&
               recreateDoc.RootElement.GetProperty("detail").GetString() == "hello-required";
    }

    // Preserve Edge's axis-usage swap without changing report bytes.
    // Unrecognized descriptor bytes retain the existing fail-closed behavior.
    private static HMProfile? ApplySonyAxisDeclarationSwap(HMProfile? profile)
    {
        if (profile is null) return null;
        var hex = profile.DescriptorHex;
        if (string.IsNullOrEmpty(hex)) return profile;
        if (profile.Id != "dualsense-edge") return profile;
        const string fromA = "0932093509330934", toA = "0932093309350934";
        if (!hex.Contains(fromA, StringComparison.Ordinal)) return profile;
        var swapped = hex.Replace(fromA, toA);
        var map = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["0x32"] = "rightStickX",
            ["0x33"] = "rightStickY",
            ["0x35"] = "leftTrigger",
            ["0x34"] = "rightTrigger",
        };
        // SDK 2.0 resets the inferred report size when DescriptorHex changes.
        // Keep this existing declaration-only patch on its original 64-byte wire.
        return new HMProfileBuilder().FromProfile(profile).DescriptorHex(swapped)
            .InputReportSize(profile.InputReportSize).AxisMap(map).Build();
    }

    private static bool RunEdgeWireSelfTest()
    {
        using var context = new SdkDataOnlyProfiles();
        context.LoadDefaultProfiles();
        var dsProfile = ApplySonyAxisDeclarationSwap(context.GetProfile("dualsense-edge"));
        Console.WriteLine("edge-profile: present="+(dsProfile is not null)+" size="+dsProfile?.InputReportSize);
        if (dsProfile is null || dsProfile.InputReportSize != 64) { Console.Error.WriteLine("edge-wire-check-1-failed"); return false; }

        var env = new CommandEnvelope(
            "SUBMIT_FRAME", "selftest", 1, "selftest", 1, 1, "target", "owner",
            "dualsense-edge", "dualsense-edge", "selftest-nonce", 1, LockedCoreSha256)
        {
            Buttons = 0x0400 | 0x0100,   // Guide + L1
            LtRaw = 0.5f,
            RtRaw = 1.0f,
            ImuAdmitted = true,
            ImuGyroX = 100.0, ImuGyroY = 200.0, ImuGyroZ = 300.0,
            ImuAccelX = 1.0, ImuAccelY = 2.0, ImuAccelZ = 3.0,
        };

        var ds = BuildDualSenseDataOnlyReport(dsProfile, env, 5);
        if ((short)(ds[15] | (ds[16] << 8)) != 1600) { Console.Error.WriteLine("edge-wire-check-2-failed"); return false; }   // gyro wire 16
        if ((short)(ds[17] | (ds[18] << 8)) != 3200) { Console.Error.WriteLine("edge-wire-check-3-failed"); return false; }   // gyro wire 18
        if ((short)(ds[19] | (ds[20] << 8)) != 4800) { Console.Error.WriteLine("edge-wire-check-4-failed"); return false; }   // gyro wire 20
        if ((short)(ds[21] | (ds[22] << 8)) != 8192) { Console.Error.WriteLine("edge-wire-check-5-failed"); return false; }   // accel wire 22
        if ((short)(ds[23] | (ds[24] << 8)) != 16384) { Console.Error.WriteLine("edge-wire-check-6-failed"); return false; }  // accel wire 24
        if ((short)(ds[25] | (ds[26] << 8)) != 24576) { Console.Error.WriteLine("edge-wire-check-7-failed"); return false; }  // accel wire 26
        if (ds[6] != 5) { Console.Error.WriteLine("edge-wire-check-8-failed"); return false; }                                // seq_number wire 7
        if (ds[4] != 128 || ds[5] != 255) { Console.Error.WriteLine("edge-wire-check-9-failed"); return false; }              // z / rz
        if (ds[7] != 8) { Console.Error.WriteLine("edge-wire-check-10-failed"); return false; }                                // hat 8
        if (ds[8] != 0x0D) { Console.Error.WriteLine("edge-wire-check-11-failed"); return false; }                             // L1 + L2 + R2
        if (ds[9] != 0x01) { Console.Error.WriteLine("edge-wire-check-12-failed"); return false; }                             // PS（buttons[2] bit0）
        var ts = (uint)(ds[27] | (ds[28] << 8) | (ds[29] << 16) | (ds[30] << 24));
        if (ts != 5u * 24024u) { Console.Error.WriteLine("edge-wire-check-13-failed"); return false; }                         // sensor_timestamp（8ms 节拍折算）
        if (ds[32] != TouchpadFinger0DisabledId || ds[36] != TouchpadFinger1DisabledId) { Console.Error.WriteLine("edge-wire-check-14-failed"); return false; }
        if (ds[52] != SonyWireBatteryFull) { Console.Error.WriteLine("edge-wire-check-15-failed"); return false; }               // status[0] wire 53

        // Preserve Edge back-button bits and their independent wire positions.
        var dsEdgeProfile = ApplySonyAxisDeclarationSwap(context.GetProfile("dualsense-edge"));
        if (dsEdgeProfile is null || dsEdgeProfile.InputReportSize != 64) { Console.Error.WriteLine("edge-wire-check-16-failed"); return false; }
        var edgeEnv = env with { Persona = "dualsense-edge", ProfileIdentity = "dualsense-edge" };
        var e0 = BuildDualSenseDataOnlyReport(dsEdgeProfile, edgeEnv with { BackButtons = 0x0 }, 5, MapDsEdgePaddles(0x0));
        var e1 = BuildDualSenseDataOnlyReport(dsEdgeProfile, edgeEnv with { BackButtons = 0x1 }, 5, MapDsEdgePaddles(0x1));
        var e2 = BuildDualSenseDataOnlyReport(dsEdgeProfile, edgeEnv with { BackButtons = 0x2 }, 5, MapDsEdgePaddles(0x2));
        var e3 = BuildDualSenseDataOnlyReport(dsEdgeProfile, edgeEnv with { BackButtons = 0x3 }, 5, MapDsEdgePaddles(0x3));
        if ((e0[9] & 0xF0) != 0 ||
            (e1[9] & 0x40) == 0 || (e1[9] & 0x80) != 0 ||
            (e2[9] & 0x80) == 0 || (e2[9] & 0x40) != 0 ||
            (e3[9] & 0xC0) != 0xC0) return false;
        return true;
    }

    private static bool RunEliteStateSelfTest()
    {
        using var context = new SdkDataOnlyProfiles();
        context.LoadDefaultProfiles();
        var eliteProfile = context.GetProfile("xbox-elite-v2");
        if (eliteProfile is null)
        {
            Console.WriteLine("elite-state: FAIL profile-missing xbox-elite-v2");
            return false;
        }
        Console.WriteLine("elite-state: profile=xbox-elite-v2 declaredInputReportSize=" + eliteProfile.InputReportSize);
        var eliteNeutral = new CommandEnvelope(
            "SUBMIT_FRAME", "selftest", 1, "selftest", 1, 1, "target", "owner",
            "elite", "xbox-elite-v2", "selftest-nonce", 1, LockedCoreSha256);
        HMGamepadState eliteState;
        try
        {
            eliteState = BuildEliteState(eliteProfile, eliteNeutral);
        }
        catch (Exception ex)
        {
            Console.WriteLine("elite-state: FAIL BUILD THREW " + ex.GetType().Name + ": " + ex.Message);
            return false;
        }
        var eliteAxes = eliteState.Axes;
        var eliteCentered =
            Math.Abs(eliteAxes[HMAxis.X] - 0.5f) < 0.0001f &&
            Math.Abs(eliteAxes[HMAxis.Y] - 0.5f) < 0.0001f &&
            Math.Abs(eliteAxes[HMAxis.Rx] - 0.5f) < 0.0001f &&
            Math.Abs(eliteAxes[HMAxis.Ry] - 0.5f) < 0.0001f;
        // elite（separate）的扳机必须落在 Z/Rz：中性 0；且**不得**落 Vx/Vy（老形状）。
        var eliteTriggersCorrect =
            eliteAxes.TryGetValue(HMAxis.Z, out var eliteLt) && Math.Abs(eliteLt) < 0.0001f &&
            eliteAxes.TryGetValue(HMAxis.Rz, out var eliteRt) && Math.Abs(eliteRt) < 0.0001f &&
            !eliteAxes.ContainsKey(HMAxis.Vx) && !eliteAxes.ContainsKey(HMAxis.Vy);
        var eliteGuide = BuildEliteState(eliteProfile, eliteNeutral with { Buttons = 0x0400 });
        var eliteFull = BuildEliteState(eliteProfile,
            eliteNeutral with { Lx = -1f, Ly = 1f, Rx = 1f, Ry = -1f, LtRaw = 1f, RtRaw = 1f });
        var eliteFullOk =
            eliteFull.Axes[HMAxis.X] == 0f && eliteFull.Axes[HMAxis.Y] == 0f &&
            eliteFull.Axes[HMAxis.Rx] == 1f && eliteFull.Axes[HMAxis.Ry] == 1f &&
            eliteFull.Axes[HMAxis.Z] == 1f && eliteFull.Axes[HMAxis.Rz] == 1f;
        if (!eliteCentered || !eliteTriggersCorrect || (eliteGuide.Buttons & HMButton.Guide) == 0 || !eliteFullOk)
        {
            Console.WriteLine("elite-state: FAIL centered=" + eliteCentered + " triggers=" + eliteTriggersCorrect +
                " guide=" + ((eliteGuide.Buttons & HMButton.Guide) != 0) + " full=" + eliteFullOk);
            return false;
        }
        // Elite does not consume OEM BackButtons as SDK paddle buttons.
        var eliteBack = BuildEliteState(eliteProfile, eliteNeutral with { BackButtons = 0x3 });
        if ((eliteBack.Buttons & (HMButton.LeftPaddle | HMButton.RightPaddle)) != 0)
        {
            Console.WriteLine("elite-state: FAIL backbuttons-consumed-by-xbox-builder");
            return false;
        }
        Console.WriteLine("elite-state: OK centered + guide + Z/Rz triggers (axes=" + eliteAxes.Count + ")");
        return true;
    }

    // SteamDeck persona（G2/G1，2026-09-11）：BuildSteamDeckState 数值断言。
    // 轴 0.5 中心（extendedReport int16-axis 域）、按钮映射、IMU 编码 HC
    // SteamDeckTarget 系数（accel 16384/g、gyro 16/dps、轴序 X,-Z,Y）。
    private static bool RunSteamDeckStateSelfTest()
    {
        using var context = new SdkDataOnlyProfiles();
        context.LoadDefaultProfiles();
        var profile = context.GetProfile("steam-deck-composite");
        if (profile is null || profile.InputReportSize != 64) return false;

        var neutral = new CommandEnvelope(
            "SUBMIT_FRAME", "selftest", 1, "selftest", 1, 1, "target", "owner",
            "steamdeck", "steam-deck-composite", "selftest-nonce", 1, LockedCoreSha256);
        var state = BuildSteamDeckState(profile, neutral);
        var axes = state.Axes;
        var centered =
            Math.Abs(axes[HMAxis.X] - 0.5f) < 0.0001f &&
            Math.Abs(axes[HMAxis.Y] - 0.5f) < 0.0001f &&
            Math.Abs(axes[HMAxis.Rx] - 0.5f) < 0.0001f &&
            Math.Abs(axes[HMAxis.Ry] - 0.5f) < 0.0001f &&
            axes[HMAxis.Z] == 0f && axes[HMAxis.Rz] == 0f;
        if (!centered) return false;
        // S54：steam-deck-composite 的扳机轴角色是 Z/Rz（StandardAxes 实证），
        // Vx/Vy 不被编码；中性态 Z/Rz 必须为 0，且 Hat 无方向。
        if (state.Hat != HMHat.None) return false;
        // fail-zero：未 admit IMU 时全部为零。
        if (state.AccelX != 0 || state.AccelY != 0 || state.AccelZ != 0 ||
            state.GyroPitch != 0 || state.GyroYaw != 0 || state.GyroRoll != 0) return false;

        var guide = neutral with { Buttons = 0x0400 };
        if ((BuildSteamDeckState(profile, guide).Buttons & HMButton.Guide) == 0) return false;

        var abxy = neutral with { Buttons = 0x1000 | 0x2000 | 0x4000 | 0x8000 };
        var abxyButtons = BuildSteamDeckState(profile, abxy).Buttons;
        if ((abxyButtons & (HMButton.Cross | HMButton.Circle | HMButton.Square | HMButton.Y)) !=
            (HMButton.Cross | HMButton.Circle | HMButton.Square | HMButton.Y)) return false;

        // DPad（S54）：frame XInput dpad 低位 -> HMHat -> Neptune DPAD 位。
        var dpadUp = BuildSteamDeckState(profile, neutral with { Buttons = 0x0001 }).Hat;
        var dpadRight = BuildSteamDeckState(profile, neutral with { Buttons = 0x0008 }).Hat;
        var dpadDown = BuildSteamDeckState(profile, neutral with { Buttons = 0x0002 }).Hat;
        var dpadLeft = BuildSteamDeckState(profile, neutral with { Buttons = 0x0004 }).Hat;
        var dpadDiag = BuildSteamDeckState(profile, neutral with { Buttons = 0x0001 | 0x0008 }).Hat;
        if (dpadUp != HMHat.North || dpadRight != HMHat.East || dpadDown != HMHat.South ||
            dpadLeft != HMHat.West || dpadDiag != HMHat.NorthEast) return false;

        // 扳机（S54 + G8）：frame raw lt/rt -> Z/Rz（codec 由此编码 byte44-46 +
        // RT/LT_DIGITAL；HC SteamDeckTarget 消费原始 AxisState，无 ≤30 死区）。
        var trig = BuildSteamDeckState(profile, neutral with { LtRaw = 1.0f, RtRaw = 1.0f });
        if (trig.Axes[HMAxis.Z] != 1f || trig.Axes[HMAxis.Rz] != 1f) return false;
        var trigDead = BuildSteamDeckState(profile, neutral with { Lt = 0f, Rt = 0f, LtRaw = 0.1f, RtRaw = 0.1f });
        if (trigDead.Axes[HMAxis.Z] != 0.1f || trigDead.Axes[HMAxis.Rz] != 0.1f) return false;

        // IMU 编码：gyro.X=1 dps -> GyroPitch=16；accel.X=1g -> AccelX=16384；
        // accelY = -accelZ*16384、accelZ = accelY*16384（HC accel 轴序 X,-Z,Y）。
        // Signed-16 saturation: -3g -> -32768; +2g -> +32767, without sign wrap.
        var imu = neutral with
        {
            ImuAdmitted = true,
            ImuGyroX = 1.0, ImuGyroY = 2.0, ImuGyroZ = 3.0,
            ImuAccelX = 1.0, ImuAccelY = 2.0, ImuAccelZ = 3.0,
        };
        var imuState = BuildSteamDeckState(profile, imu);
        var imuOk = imuState.GyroPitch == 16 && imuState.GyroYaw == -48 && imuState.GyroRoll == 32 &&
                    imuState.AccelX == 16384 && imuState.AccelY == short.MinValue && imuState.AccelZ == short.MaxValue;

        // GP-MSIG3E-6-LC-R3（§四 C1 规则4）：既有 Deck 生产 builder 的背键位序**只核对不改**。
        // 进入帧的 BackButtons(bit0=M1 / bit1=M2) 必须被 BuildSteamDeckState 消费为
        // LeftPaddle(M1→L5) / RightPaddle(M2→R5)；0 位不得落入任何 paddle 位。
        var m1 = BuildSteamDeckState(profile, neutral with { BackButtons = 0x1 }).Buttons;
        var m2 = BuildSteamDeckState(profile, neutral with { BackButtons = 0x2 }).Buttons;
        var m12 = BuildSteamDeckState(profile, neutral with { BackButtons = 0x3 }).Buttons;
        var m0 = BuildSteamDeckState(profile, neutral with { BackButtons = 0x0 }).Buttons;
        var backOk =
            (m1 & HMButton.LeftPaddle2) != 0 && (m1 & HMButton.RightPaddle2) == 0 &&
            (m2 & HMButton.RightPaddle2) != 0 && (m2 & HMButton.LeftPaddle2) == 0 &&
            (m12 & (HMButton.LeftPaddle2 | HMButton.RightPaddle2)) == (HMButton.LeftPaddle2 | HMButton.RightPaddle2) &&
            (m0 & (HMButton.LeftPaddle2 | HMButton.RightPaddle2)) == 0;
        return imuOk && backOk;
    }

    // A HIDMaestro device deliberately presents the profile's USB descriptor
    // identity while PnP instance IDs can remain HIDCLASS.  Therefore VID/PID
    // alone cannot distinguish a physical DS4/Xbox device from this Host's
    // target.  The stable identity is the HIDMaestro-marked root (the current
    // driver reports ROOT\\HIDCLASS + ROOT\\HIDMAESTRO) and exactly one started
    // HID descendant beneath it.
    private static VirtualEndpointObservation WaitForVirtualEndpointRemoval(string? persona = null)
    {
        var deadline = Environment.TickCount64 + VirtualEndpointRemovalTimeoutMs;
        VirtualEndpointObservation observation;
        do
        {
            observation = ObserveVirtualEndpoint(persona);
            if (observation.IsAbsent) return observation;
            Thread.Sleep(VirtualEndpointPollIntervalMs);
        }
        while (Environment.TickCount64 < deadline);
        return observation;
    }

    // 批107 / T2：切换清场专用——按"全族"（HIDMAESTRO 三计数 + usbip/SteamDeck 族）
    // 等待旧端点消失。与释放路径的 WaitForVirtualEndpointRemoval 刻意分开：后者的
    // IsAbsent 口径（批 104 / L-3，只看 HIDMAESTRO 三计数）是"本族建塔门"，不得改动
    // （否则 SteamDeck 死锁回归）。探针失败 ⇒ IsFullyAbsent=false ⇒ 走有界等待。
    private static VirtualEndpointObservation WaitForEndpointFamilyRemoval(int timeoutMs, bool requireInstanceIds = false)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        VirtualEndpointObservation observation;
        do
        {
            observation = ObserveVirtualEndpoint(requireInstanceIds: requireInstanceIds);
            if (observation.IsFullyAbsent) return observation;
            Thread.Sleep(VirtualEndpointPollIntervalMs);
        }
        while (Environment.TickCount64 < deadline);
        return observation;
    }

    private static VirtualEndpointObservation WaitForVirtualEndpointArrival(string persona, bool requireInstanceIds = false)
    {
        var deadline = Environment.TickCount64 + VirtualEndpointArrivalTimeoutMs;
        VirtualEndpointObservation observation;
        do
        {
            observation = ObserveVirtualEndpoint(persona, requireInstanceIds);
            if (observation.IsUniqueStartedTarget(persona) && (!requireInstanceIds || observation.SteamDeckRootCount == 1)) return observation;
            Thread.Sleep(VirtualEndpointPollIntervalMs);
        }
        while (Environment.TickCount64 < deadline);
        return observation;
    }

    private static VirtualEndpointObservation ObserveVirtualEndpoint(string? persona = null, bool requireInstanceIds = false)
    {
        using var diagnosticScope = new MsiPnpCensusDiagnostic.ObservationScope(
            MsiPnpCensusDiagnostic.Requested ? Environment.TickCount64 : 0);
        var diagnostic = diagnosticScope.Capture;
        try
        {
            var nodes = EnumeratePresentPnpNodes(requireInstanceIds);
            // The removal/rebuild fence must be persona-agnostic. An Edge/Elite
            // probe used to ignore a stale SteamDeck USBIP root (and vice versa),
            // so the next persona could be published while the old endpoint was
            // still present.  Count every supported virtual family on every poll;
            // persona is used only to decide which single family is admissible.
            var hidMaestroRoots = nodes.Where(IsHidMaestroPrimaryRoot).ToArray();
            var xboxCompanionRoots = nodes.Where(IsHidMaestroXboxCompanionRoot).ToArray();
            var steamDeckRoots = nodes.Where(IsSteamDeckUsbRoot).ToArray();
            var hidRootIds = new HashSet<string>(hidMaestroRoots.Select(node => node.InstanceId), StringComparer.OrdinalIgnoreCase);
            var steamDeckRootIds = new HashSet<string>(steamDeckRoots.Select(node => node.InstanceId), StringComparer.OrdinalIgnoreCase);
            var hidChildren = nodes.Where(node =>
                node.InstanceId.StartsWith("HID\\", StringComparison.OrdinalIgnoreCase) &&
                GetPnpAncestors(node.DevInst).Any(hidRootIds.Contains))
                .ToArray();
            var steamDeckHidChildren = nodes.Where(node =>
                node.InstanceId.StartsWith("HID\\", StringComparison.OrdinalIgnoreCase) &&
                GetPnpAncestors(node.DevInst).Any(steamDeckRootIds.Contains))
                .ToArray();
            var allStarted = hidMaestroRoots.All(node => node.Started) &&
                xboxCompanionRoots.All(node => node.Started) &&
                hidChildren.All(node => node.Started) &&
                steamDeckRoots.All(node => node.Started) &&
                steamDeckHidChildren.All(node => node.Started);
            return new VirtualEndpointObservation(
                ProbeSucceeded: true,
                PrimaryRootCount: hidMaestroRoots.Length,
                XboxCompanionRootCount: xboxCompanionRoots.Length,
                HidChildCount: hidChildren.Length,
                SteamDeckRootCount: steamDeckRoots.Length,
                SteamDeckHidChildCount: steamDeckHidChildren.Length,
                AllStarted: allStarted,
                Detail: DescribeVirtualEndpoint(hidMaestroRoots, xboxCompanionRoots, hidChildren,
                    steamDeckRoots, steamDeckHidChildren, allStarted))
            {
                SteamDeckRootInstanceIds = requireInstanceIds ? Array.AsReadOnly(steamDeckRoots.Select(node => node.InstanceId).ToArray()) : null,
                SteamDeckHidChildInstanceIds = requireInstanceIds ? Array.AsReadOnly(steamDeckHidChildren.Select(node => node.InstanceId).ToArray()) : null,
                PresentInstanceIds = requireInstanceIds ? Array.AsReadOnly(nodes.Select(node => node.InstanceId).ToArray()) : null,
                MsiPnpCensus = CaptureExistingMsiPnpCensus(diagnostic, hidMaestroRoots, xboxCompanionRoots,
                    hidChildren, steamDeckRoots, steamDeckHidChildren),
            };
        }
        catch (Exception ex)
        {
            return new VirtualEndpointObservation(
                ProbeSucceeded: false,
                PrimaryRootCount: 0,
                XboxCompanionRootCount: 0,
                HidChildCount: 0,
                SteamDeckRootCount: 0,
                SteamDeckHidChildCount: 0,
                AllStarted: false,
                Detail: "pnp-probe-exception:" + ex.GetType().Name)
                { MsiPnpCensus = diagnostic?.TryComplete(false, Environment.TickCount64) };
        }
    }

    private static MsiPnpCensusDiagnostic.Receipt? CaptureExistingMsiPnpCensus(MsiPnpCensusDiagnostic? diagnostic,
        PnpNode[] primary, PnpNode[] companion, PnpNode[] hid, PnpNode[] steamDeck, PnpNode[] steamDeckHid)
    {
        if(diagnostic is null) return null;
        try {
            diagnostic.TryCandidates("primary-root", primary.Select(node => node.InstanceId));
            diagnostic.TryCandidates("xbox-companion-root", companion.Select(node => node.InstanceId));
            diagnostic.TryCandidates("hid-descendant", hid.Select(node => node.InstanceId));
            diagnostic.TryCandidates("steamdeck-root", steamDeck.Select(node => node.InstanceId));
            diagnostic.TryCandidates("steamdeck-hid-descendant", steamDeckHid.Select(node => node.InstanceId));
            return diagnostic.TryComplete(true, Environment.TickCount64);
        } catch { return null; }
    }

    private static IReadOnlyList<PnpNode> EnumeratePresentPnpNodes(bool requireInstanceIds = false)
    {
        const uint digcfPresent = 0x00000002;
        const uint digcfAllClasses = 0x00000004;
        var deviceSet = SetupDiGetClassDevsW(IntPtr.Zero, null, IntPtr.Zero, digcfPresent | digcfAllClasses);
        if (deviceSet == InvalidDeviceInfoSet)
            throw new InvalidOperationException("setupdi-open:" + Marshal.GetLastWin32Error());
        try
        {
            var nodes = new List<PnpNode>();
            for (uint index = 0; ; index++)
            {
                var data = new SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf<SP_DEVINFO_DATA>() };
                if (!SetupDiEnumDeviceInfo(deviceSet, index, ref data))
                {
                    var error = Marshal.GetLastWin32Error();
                    if (error == 259) { MsiPnpCensusDiagnostic.Current?.TryEnumerationEnd(); break; } // ERROR_NO_MORE_ITEMS
                    throw new InvalidOperationException("setupdi-enumerate:" + error);
                }
                MsiPnpCensusDiagnostic.Current?.TryNodeEnumerated();
                var instanceId = GetPnpInstanceId(deviceSet, ref data);
                if (string.IsNullOrWhiteSpace(instanceId))
                {
                    MsiPnpCensusDiagnostic.Current?.TryMissingInstanceId();
                    if (requireInstanceIds) throw new InvalidOperationException("setupdi-instance-id-unavailable");
                    continue;
                }
                MsiPnpCensusDiagnostic.Current?.TryNamedNode();
                nodes.Add(new PnpNode(instanceId, data.DevInst, IsPnpNodeStarted(data.DevInst), GetPnpHardwareIds(deviceSet, ref data)));
            }
            return nodes;
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(deviceSet);
        }
    }

    private static string GetPnpInstanceId(IntPtr deviceSet, ref SP_DEVINFO_DATA data)
    {
        SetupDiGetDeviceInstanceIdW(deviceSet, ref data, null, 0, out var required);
        if (required == 0) return string.Empty;
        var buffer = new StringBuilder((int)required + 1);
        return SetupDiGetDeviceInstanceIdW(deviceSet, ref data, buffer, buffer.Capacity, out _)
            ? buffer.ToString()
            : string.Empty;
    }

    private static IReadOnlyList<string> GetPnpHardwareIds(IntPtr deviceSet, ref SP_DEVINFO_DATA data)
    {
        const uint spdrpHardwareId = 0x00000001;
        SetupDiGetDeviceRegistryPropertyW(deviceSet, ref data, spdrpHardwareId, out _, null, 0, out var required);
        if (required == 0) {
            MsiPnpCensusDiagnostic.Current?.TryHardwareIdsUnavailable(Marshal.GetLastWin32Error(), required);
            return Array.Empty<string>();
        }
        var buffer = new byte[required];
        if (!SetupDiGetDeviceRegistryPropertyW(deviceSet, ref data, spdrpHardwareId, out var returnedPropertyType, buffer, (uint)buffer.Length, out var returnedSize)) {
            MsiPnpCensusDiagnostic.Current?.TryHardwareIdsReadFailed(Marshal.GetLastWin32Error(), required);
            return Array.Empty<string>();
        }
        if(MsiPnpCensusDiagnostic.Current is { } census) {
            try {
                var length = (int)Math.Min(returnedSize, (uint)buffer.Length);
                var terminated = returnedSize >= 4 && returnedSize <= buffer.Length &&
                    buffer[length - 1] == 0 && buffer[length - 2] == 0 && buffer[length - 3] == 0 && buffer[length - 4] == 0;
                census.TryHardwareIdsReadShape(required, returnedSize, returnedPropertyType, terminated);
            } catch { /* optional read-shape diagnostic only */ }
        }
        return Encoding.Unicode.GetString(buffer)
            .Split('\0', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    }

    private static bool IsHidMaestroPrimaryRoot(PnpNode node) =>
        node.HardwareIds.Any(id => id.Equals("ROOT\\HIDMAESTRO", StringComparison.OrdinalIgnoreCase));

    // SteamDeck composite（G2）：只识别 usbip 虚拟 USB 复合根。
    // MI_00~MI_03 是同一复合设备的接口节点，不能再次计为 root；HID
    // 子设备沿复合根的后代链匹配。
    private static bool IsSteamDeckCompositeHardwareId(string id)
    {
        var normalized = id.Trim();
        return normalized.Equals("USB\\VID_28DE&PID_1205", StringComparison.OrdinalIgnoreCase) ||
            (normalized.StartsWith("USB\\VID_28DE&PID_1205&", StringComparison.OrdinalIgnoreCase) &&
             !normalized.Contains("&MI_", StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsSteamDeckUsbRoot(PnpNode node) =>
        node.HardwareIds.Any(IsSteamDeckCompositeHardwareId);

    private static bool IsHidMaestroXboxCompanionRoot(PnpNode node) =>
        node.HardwareIds.Any(id => id.Equals("ROOT\\HIDMAESTROXUSB", StringComparison.OrdinalIgnoreCase));

    private static IEnumerable<string> GetPnpAncestors(uint devInst)
    {
        var current = devInst;
        for (var depth = 0; depth < 16; depth++)
        {
            var parentResult = CM_Get_Parent(out var parent, current, 0);
            if (parentResult != ConfigurationManagerSuccess) {
                MsiPnpCensusDiagnostic.Current?.TryAncestorParentNonSuccess(parentResult); yield break;
            }
            var buffer = new StringBuilder(512);
            var ancestorIdResult = CM_Get_Device_IDW(parent, buffer, buffer.Capacity, 0);
            if (ancestorIdResult != ConfigurationManagerSuccess) {
                MsiPnpCensusDiagnostic.Current?.TryAncestorIdNonSuccess(ancestorIdResult); yield break;
            }
            yield return buffer.ToString();
            current = parent;
        }
        MsiPnpCensusDiagnostic.Current?.TryAncestorDepthLimit();
    }

    private static bool IsPnpNodeStarted(uint devInst) =>
        CM_Get_DevNode_Status(out var status, out _, devInst, 0) == ConfigurationManagerSuccess &&
        (status & DeviceNodeStarted) != 0;

    private static string DescribeVirtualEndpoint(IReadOnlyList<PnpNode> primaryRoots, IReadOnlyList<PnpNode> xboxCompanionRoots,
        IReadOnlyList<PnpNode> hidChildren, IReadOnlyList<PnpNode> steamDeckRoots,
        IReadOnlyList<PnpNode> steamDeckHidChildren, bool allStarted)
    {
        static string Compact(IReadOnlyList<PnpNode> nodes) => string.Join(",", nodes.Select(node =>
            node.InstanceId + "[" + string.Join("|", node.HardwareIds) + "]"));
        return "pnp-primary-roots=" + primaryRoots.Count +
            ";pnp-xbox-companion-roots=" + xboxCompanionRoots.Count +
            ";pnp-hid-children=" + hidChildren.Count +
            ";pnp-steamdeck-roots=" + steamDeckRoots.Count +
            ";pnp-steamdeck-hid-children=" + steamDeckHidChildren.Count +
            ";pnp-started=" + allStarted.ToString().ToLowerInvariant() +
            ";pnp-primary-root-ids=" + Compact(primaryRoots) +
            ";pnp-xbox-companion-root-ids=" + Compact(xboxCompanionRoots) +
            ";pnp-hid-ids=" + Compact(hidChildren) +
            ";pnp-steamdeck-root-ids=" + Compact(steamDeckRoots) +
            ";pnp-steamdeck-hid-ids=" + Compact(steamDeckHidChildren);
    }

    // 批107 / T2：切换清场回执的紧凑计数。词汇表与 DescribeVirtualEndpoint 一致，
    // 便于判据工具用同一组正则读（before/after 成对，可判定"旧族是否真的走干净"）。
    private static string DescribeEndpointCounts(VirtualEndpointObservation observation) =>
        "pnp-primary-roots:" + observation.PrimaryRootCount +
        ",pnp-xbox-companion-roots:" + observation.XboxCompanionRootCount +
        ",pnp-hid-children:" + observation.HidChildCount +
        ",pnp-steamdeck-roots:" + observation.SteamDeckRootCount +
        ",pnp-steamdeck-hid-children:" + observation.SteamDeckHidChildCount;

    private const int ConfigurationManagerSuccess = 0;
    private const uint DeviceNodeStarted = 0x00000008;
    private static readonly IntPtr InvalidDeviceInfoSet = new(-1);

    [StructLayout(LayoutKind.Sequential)]
    private struct SP_DEVINFO_DATA
    {
        public uint cbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SetupDiGetClassDevsW(IntPtr classGuid, string? enumerator, IntPtr hwndParent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiEnumDeviceInfo(IntPtr deviceInfoSet, uint memberIndex, ref SP_DEVINFO_DATA deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiGetDeviceInstanceIdW(IntPtr deviceInfoSet, ref SP_DEVINFO_DATA deviceInfoData,
        StringBuilder? deviceInstanceId, int deviceInstanceIdSize, out uint requiredSize);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr deviceInfoSet, ref SP_DEVINFO_DATA deviceInfoData,
        uint property, out uint propertyRegDataType, byte[]? propertyBuffer, uint propertyBufferSize, out uint requiredSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("cfgmgr32.dll")]
    private static extern int CM_Get_Parent(out uint parentDevInst, uint devInst, uint flags);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    private static extern int CM_Get_Device_IDW(uint devInst, StringBuilder buffer, int bufferLen, uint flags);

    [DllImport("cfgmgr32.dll")]
    private static extern int CM_Get_DevNode_Status(out uint status, out uint problemNumber, uint devInst, uint flags);

    // Read-only CM leaf boundary. The helper owns all graph traversal and checks.
    private sealed class PsOwnershipCmReader : PsEndpointOwnership.ICmReader
    {
        internal static readonly PsOwnershipCmReader Instance = new();
        public int Locate(string id, out uint node) => CM_Locate_DevNodeW(out node, id, 0);
        public int GetChild(uint node, out uint child) => CM_Get_Child(out child, node, 0);
        public int GetSibling(uint node, out uint sibling) => CM_Get_Sibling(out sibling, node, 0);
        public int GetDeviceId(uint node, out string id)
        {
            var buffer = new StringBuilder(PsEndpointOwnership.MaximumIdLength + 1);
            var result = CM_Get_Device_IDW(node, buffer, buffer.Capacity, 0);
            id = result == PsEndpointOwnership.Success ? buffer.ToString() : "";
            return result;
        }
        public int GetStatus(uint node, out uint status, out uint problem) => CM_Get_DevNode_Status(out status, out problem, node, 0);
    }

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
    private static extern int CM_Locate_DevNodeW(out uint node, string instanceId, uint flags);
    [DllImport("cfgmgr32.dll")]
    private static extern int CM_Get_Child(out uint child, uint node, uint flags);
    [DllImport("cfgmgr32.dll")]
    private static extern int CM_Get_Sibling(out uint sibling, uint node, uint flags);

    private readonly record struct PnpNode(string InstanceId, uint DevInst, bool Started, IReadOnlyList<string> HardwareIds);

    private readonly record struct VirtualEndpointObservation(
        bool ProbeSucceeded,
        int PrimaryRootCount,
        int XboxCompanionRootCount,
        int HidChildCount,
        int SteamDeckRootCount,
        int SteamDeckHidChildCount,
        bool AllStarted,
        string Detail)
    {
        // Optional identity snapshots preserve existing count-only callers.
        public IReadOnlyList<string>? SteamDeckRootInstanceIds { get; init; }
        public IReadOnlyList<string>? SteamDeckHidChildInstanceIds { get; init; }
        public IReadOnlyList<string>? PresentInstanceIds { get; init; }
        public MsiPnpCensusDiagnostic.Receipt? MsiPnpCensus { get; init; }

        // 批 104 / L-3（回归老线口径，2026-09-24）：IsAbsent 只看 HIDMAESTRO 三计数；
        // usbip（SteamDeck）族不再参与"必须清空"的阻塞判定（老线 main.cpp L781 同形）。
        public bool IsAbsent => ProbeSucceeded &&
            PrimaryRootCount == 0 && XboxCompanionRootCount == 0 && HidChildCount == 0;

        // 批107 / T2：**全族判空**——在 IsAbsent（L-3 口径）之上再要求 usbip/SteamDeck 族为空。
        // 只用于 persona 切换的清场决策与回执（"上一代 persona 是不是真的走干净了"）；
        // 建塔门仍用 IsAbsent。探针失败 ⇒ false（保守：走有界等待，必要时一次 fallback 清扫）。
        public bool IsFullyAbsent => IsAbsent &&
            SteamDeckRootCount == 0 && SteamDeckHidChildCount == 0;

        public bool IsUniqueStartedTarget(string persona) =>
            ProbeSucceeded && AllStarted &&
            (persona == "steamdeck" ? IsUniqueSteamDeck() :
             // Each current persona retains its existing endpoint-count contract.
             persona == "elite" ? PrimaryRootCount == 1 && HidChildCount == 1 && XboxCompanionRootCount <= 1 :
             persona == "dualsense-edge" ? PrimaryRootCount == 1 && HidChildCount == 1 &&
                                      XboxCompanionRootCount == 0 :
             false);

        // SteamDeck composite（G2）：usbip 虚拟 USB（USB\VID_28DE&PID_1205 复合根，
        // MI_00~03 多接口 + HID\VID_28DE&PID_1205 子设备）。要求恰好一个
        // 复合根，且不能同时存在另一类 HIDMaestro 端点。
        // 批 104 / L-2（老线 L792 口径）：root 计数含"复合根 + 其 MI_00 接口"两份，
        // `== 1` 永不可满足（死锁根因）⇒ 改为 `>= 1`。
        private bool IsUniqueSteamDeck() =>
            SteamDeckRootCount >= 1 && SteamDeckHidChildCount >= 1 &&
            PrimaryRootCount == 0 && XboxCompanionRootCount == 0 && HidChildCount == 0;
    }

        // X1（YS-9122 整合修复，2026-09-12）：宿主死因落盘。
        // Do NOT rely on stderr; CREATE_NO_WINDOW discards it（三十八批次 S1 教训）。
        // 生产环境宿主 stderr 被 CREATE_NO_WINDOW 丢弃，S1 死因（FileNotFoundException:
        // Nefarius.Drivers.HidHide）花了一整轮探针才复现。所有致命/退出路径在此落一行
        // JSON 到 %LOCALAPPDATA%\YeManCC\input-host-fatal.log（与 input-host-rejects.log
        // 同目录同风格，参考 HostSession Error() 落盘实现）。日志写入失败绝不能影响退出路径。
        // 注意：必须位于 Program 类（调用点在 Main/RunPipeClientAsync）；不得放回 HostSession。
        // 批129（板面 batch 128）：死因行判因上下文 +「有序收尾 / 非预期死亡」一眼可分。
        // 现场（2026-09-25 本机取证）：该文件当时 5 行只有 reason/detail，全部是有序
        // 收尾（shutdown-ordered），却曾被读成 5 次"父进程死亡"；kind 消除该歧义。
        private static readonly DateTime ProcessStartedUtc = DateTime.UtcNow;
        private static volatile string _fatalContextLastCommand = "none";
        private static volatile string _fatalContextHostInstance = "none";
        private static void NoteCommand(string command)
        {
            if (!string.IsNullOrEmpty(command)) _fatalContextLastCommand = command;
        }
        private static void NoteHostInstance(string instanceId)
        {
            if (!string.IsNullOrEmpty(instanceId)) _fatalContextHostInstance = instanceId;
        }

        private static void LogFatalExit(string reason, string detail)
        {
            try
            {
                // kind：orderly = 收到 SHUTDOWN 的有序收尾；parent-lost = 父进程消失
                // （App 关闭与 App 崩溃在此不可区分，note 已标注）；fatal = 异常/读写失败。
                var kind = reason == "shutdown-ordered" ? "orderly"
                    : (reason == "parent-exited" ? "parent-lost"
                    : reason is "terminal-cleanup-complete" or "terminal-cleanup-failed" ? "resource-settlement" : "fatal");
                var line = JsonSerializer.Serialize(new
                {
                    t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    kind,
                    reason,
                    detail,
                    note = reason == "parent-exited" ? "app-exit-vs-app-crash-indistinguishable-here" : "",
                    uptimeMs = (long)(DateTime.UtcNow - ProcessStartedUtc).TotalMilliseconds,
                    pid = Environment.ProcessId,
                    hostInstanceId = _fatalContextHostInstance,
                    lastCommand = _fatalContextLastCommand
                });
                if (_msiHostStageEvidence is { } stage)
                {
                    // Enrich this existing fatal event only. Ordinary launches
                    // retain the original line and have no stage channel.
                    try
                    {
                        var node = System.Text.Json.Nodes.JsonNode.Parse(line)!;
                        node["msiHostStage"] = JsonSerializer.SerializeToNode(stage.FatalSnapshot());
                        line = node.ToJsonString();
                    }
                    catch { }
                }
                var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
                try { Directory.CreateDirectory(dir); } catch { }
                File.AppendAllText(Path.Combine(dir, "input-host-fatal.log"), line + Environment.NewLine, System.Text.Encoding.UTF8);
            }
            catch
            {
                // 日志写入失败绝不能影响退出路径。
            }
        }

    private sealed class HostSession
    {
        private readonly object _lifecycleGate = new();
        private HMContext? _context;
        private HMController? _controller;
        private readonly PsEndpointOwnership _psEndpointOwnership = new();
        private readonly HostGameOutputObservation _gameOutputObservation = new();
        private HMController? _gameOutputController;
        private Action<HMController, HMOutputPacket>? _gameOutputCallback;
        private CommandEnvelope? _gameOutputTuple;
        // A failed SD writer barrier retains the target for a later release retry.
        private bool _steamDeckReleasePending;
        private bool _outputReleasePending;
        private bool _idleCtsReleasePending;
        private HostControllerIdleCtsRelease.Captured? _idleCtsRelease;
        // Pinned SDK Dispose sets its flag before the final context callback.
        // After an exception a second no-op Dispose is not completion evidence.
        private string _sdkDisposeFailure = "";
        private bool LocalReleasePending => _steamDeckReleasePending || _outputReleasePending || _idleCtsReleasePending ||
            _sdkDisposeFailure.Length != 0;
        // R39: host-local SDK object lifetime diagnostics; no OS or frame reads.
        private readonly TargetCreationLifetimeEvidence? _targetCreationLifetime = TargetCreationLifetimeEvidence.TryCreate();
        private long _lastSubmitElapsedUs;
        // GP-926R17 §4：后端实际 submit 的最小聚合回执——只绑当前 tuple/会话（HELLO 与每次
        // PREPARE/RECREATE 重置）；native 在唤醒与借用结算各取一次。不改判定/幂等/后端架构。
        private long _backendSubmitOk;
        private long _backendSubmitFail;
        private long _backendNeutralOk;
        private long _backendNeutralFail;
        private string _lastBackendDetail = "";
        private ulong _lastBackendSequence;
        private CommandEnvelope? _tuple;
        private HostReleaseTimingEvidence? _releaseTimingEvidence;
        private CommandEnvelope? _releaseTimingBinding;
        private readonly string _startupNonce;
        private readonly int _reportChainParentPid1410;
        private MsiReportChainRuntime1410? _reportChain1410;
        private HostVirtualSignalEvidence? _virtualSignalEvidence;
        private bool _reportChainCapabilityAdvertised1410;
        private readonly Dictionary<string, IdempotentReply> _idempotentReplies = new(StringComparer.Ordinal);
        private Phase _phase = Phase.AwaitHello;
        private ulong _lastSequence;
        private bool _assetVerified;
        // 批107 / T2：语义收紧——只有**发生过机器级 fallback 清扫**才为 true。
        // 旧形状每次 prepare 都无条件做机器级 RemoveAll ⇒ 恒为 true（无信息量）。
        private bool _virtualEndpointReconciled;
        private VirtualEndpointObservation _virtualEndpoint = new(false, 0, 0, 0, 0, 0, false, "pnp-not-observed");
        private bool _targetPnpAdmitted;
        private MsiSteamDeckReleaseFence1726? _msiSteamDeckReleaseFence1726;
        private readonly bool _msiNonSdReleaseDiagnosticRequested;
        // R13: existing known-MSI detailed-log launch authority only. Failure to
        // start diagnostics must not fall back to synchronous work in SUBMIT_FRAME.
        private readonly bool _msiCostDeferredRequested;
        private readonly MsiCostWindowDiagnostics? _msiCostDiagnostics;
        internal IDisposable? CostDiagnosticLifetime => _msiCostDiagnostics;
        private MsiNonSdReleaseDiagnostic? _msiNonSdReleaseDiagnostic;
        private CommandEnvelope? _msiNonSdReleaseDiagnosticBinding;
        private readonly long _startedAt = Environment.TickCount64;
        // R3（2026-09-17 tick 成本需求单）：宿主侧成本聚合（默认关门控；
        // 门控 = native 写的 input-host-cost-enabled.flag，窗口级刷新）。
        private readonly long[] _costSubmitUsSamples = new long[2048];
        private int _costSubmitUsCount;
        private long _costFrames;
        private long _costJsonParseUsSum;
        private long _costJsonParseCount;
        private long _costHidSubmitUsSum;
        private long _costHidSubmitCount;
        private long _costPipeReadUsSum;
        private long _costPipeReadCount;
        private long _costFramesRejected;
        // 批次A（评审10 §3）：HidHide 查询模式计数——证明周期核对改走精简查询
        // （HIDHIDE_QUERY_STATE）且完整查询调用点数量未变；门同上，关时零累加。
        private long _costHidHideQueryFull;
        private long _costHidHideQueryState;
        private double _costLastGcPauseMs;
        // R3b″（2026-09-18 用户授权）：分配量与线程角色归因。规则与既有成本字段一致：
        // 门 = input-host-cost-enabled.flag；关时零累加零写入（各调用点先查门）。
        private long _costLastAllocBytes = GC.GetTotalAllocatedBytes(precise: false);
        private HostCpuCostWindow? _costCpuWindow; // Allocated only inside the enabled diagnostic window.
        // 角色自报：在既有热路径埋点处记录"当前线程 OS tid → 角色"（kernel32
        // GetCurrentThreadId 为 TEB 读取，ns 级；池线程无法用 Thread.Name 命名）。
        private readonly Dictionary<string, int> _costRoleOsTid = new(StringComparer.Ordinal);
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        private void RecordCostRole(string role)
        {
            if (!_costFlagEnabled) return;
            _costRoleOsTid[role] = (int)GetCurrentThreadId();
        }
        private string RoleOfCostThread(int osTid)
        {
            foreach (var kv in _costRoleOsTid)
                if (kv.Value == osTid) return kv.Key;
            return "unlabeled";
        }
        private long _costWindowStartTicks = Environment.TickCount64;
        private bool _costFlagEnabled = CostLoggingRequestedByFlag();
        private long _lastActivityAt = Environment.TickCount64;
        private readonly string _hostInstanceId = Guid.NewGuid().ToString("N");
        private HostRuntimeBinaryIdentity.Snapshot? _runtimeBinaryIdentity;
        private UsbipTransportObservation? _usbipTransportObservation;
        // ── GP-GBRETURN-1：关闭归还刷新的"短受理—后台执行—有界查询"支持（**只服务 T1 关闭归还**）──
        // 既有同步 HIDHIDE_CYCLE 在消息循环里执行 CyclePort（可达数秒）⇒ 帧命令堵在循环后面
        // （R17-4 因此撤回关闭侧对称 cycle）。本组命令只为此新增：BEGIN 立即受理（一个在途），
        // 后台任务只做端口刷新、**绝不写管道**；POLL 从内存读状态（不取 HidHideLock、不阻塞循环）。
        // 既有同步 HIDHIDE_CYCLE / CPU2 codec 语义与判据不变；正常开启路径的调用点不改。
        private readonly object _closeRefreshGate = new();
        private int _closeRefreshInFlight;                 // 0/1（Interlocked；全局一个在途动作）
        private string _closeRefreshState = "idle";        // idle | in-flight | done | failed
        private string _closeRefreshDetail = "";
        private long _closeRefreshStartedTick;
        private long _closeRefreshElapsedMs;
        private string _closeRefreshPath = "";
        private string _closeRefreshRunId = "";
        private ulong _closeRefreshEpoch;
        private ulong _closeRefreshPowerGeneration;

        public HostSession(string startupNonce, int parentPid = 0, bool msiReleaseDiagnosticRequested = false)
        {
            _startupNonce = startupNonce;
            _reportChainParentPid1410 = parentPid;
            _msiNonSdReleaseDiagnosticRequested = msiReleaseDiagnosticRequested;
            _msiCostDeferredRequested = msiReleaseDiagnosticRequested;
            if (_msiCostDeferredRequested)
            {
                try { _msiCostDiagnostics = new(_costFlagEnabled, CostStateDir); }
                catch { /* optional diagnostics unavailable; never block frame receipts */ }
            }
            // 批129：宿主身份进死因上下文（可与 native 侧回执的 hostInstanceId 对齐）。
            Program.NoteHostInstance(_hostInstanceId);
        }

        // F3-B（第八十一批 B-变体）：LeaseExpired 已删除——宿主不因租约超时
        // 自杀；存活 = 已绑定的原父进程句柄仍存活。

        public string Handle(string message, out bool shutdown)
        {
            lock (_lifecycleGate)
                return HandleLocked(message, out shutdown);
        }

        private string HandleLocked(string message, out bool shutdown)
        {
            shutdown = false;
            // R3（2026-09-17 需求单）：命令解析耗时（门控缓存；关时零累加）。
            var costParseStarted = _costFlagEnabled ? Stopwatch.GetTimestamp() : 0L;
            if (!TryParseEnvelope(message, out var envelope, out var parseError))
                return Error(null, "protocol-error", parseError);
            // Parsed metadata is diagnostic evidence, never tuple/admission proof.
            var stageEnvelope = envelope!;
            _msiHostStageEvidence?.Decode(stageEnvelope.RequestId, stageEnvelope.Sequence,
                stageEnvelope.RunId, stageEnvelope.Epoch, stageEnvelope.PowerGeneration, stageEnvelope.Command);
            if (_costFlagEnabled)
            {
                _costJsonParseUsSum += (long)(Stopwatch.GetElapsedTime(costParseStarted).TotalMilliseconds * 1000.0);
                _costJsonParseCount++;
                RecordCostRole("json-parse");
            }
            if (envelope!.StartupNonce != _startupNonce)
                return Error(envelope, "protocol-error", "startup-nonce-mismatch");
            // 批129：死因上下文——最后一条被接纳的命令（异常退出时的判因锚点）。
            Program.NoteCommand(envelope.Command);
            // GP-CPU-1（operator 2026-09-27：验证/检测类开销尽量降低）：原实现在**每条命令**上
            // 都做一次 `JsonSerializer.Serialize(envelope)`（含 125/s 的 SUBMIT_FRAME），但该指纹
            // 只在「同 requestId 重放」分支做比较、或在存表时写入 ⇒ 改为**惰性**计算（重放/存表时）。
            if (_idempotentReplies.TryGetValue(envelope.RequestId, out var prior))
            {
                var fingerprint = JsonSerializer.Serialize(envelope);
                if (prior.Fingerprint != fingerprint || prior.Command != envelope.Command)
                    return Error(envelope, "protocol-error", "request-id-content-mismatch");
                return prior.Response;
            }

            if (_phase == Phase.AwaitHello)
            {
                if (envelope.Command != "HELLO") return Error(envelope, "protocol-error", "hello-required");
                if (!TryVerifyLockedAsset(out var assetDetail)) return Error(envelope, "asset-mismatch", assetDetail);
                _assetVerified = true;
                // R4: own Host identity, observed only after the existing SDK check.
                // A diagnostic failure never changes HELLO or controller behavior.
                try
                {
                    var businessAssembly = typeof(Program).Assembly;
                    var coreAssembly = typeof(HMContext).Assembly;
                    _runtimeBinaryIdentity = HostRuntimeBinaryIdentity.Capture(
                        Environment.ProcessPath ?? "", businessAssembly.Location,
                        businessAssembly.ManifestModule.ModuleVersionId.ToString("D"),
                        coreAssembly.Location, coreAssembly.ManifestModule.ModuleVersionId.ToString("D"),
                        LockedCoreBytes, LockedCoreSha256, DateTimeOffset.UtcNow.ToString("O"));
                }
                catch { _runtimeBinaryIdentity = null; }
                ResetVirtualSignalEvidenceSafely();
                _tuple = envelope;
                _lastSequence = envelope.Sequence;
                _lastActivityAt = Environment.TickCount64;
                ResetBackendSubmitStats();
                _phase = Phase.Hello;
                // 批次A（评审10 §3）：能力协商——HELLO 回执 detail 广告本宿主支持
                // 精简状态查询。旧 native 不解析 detail（无行为差）；新 native 据此
                // 决定周期核对用 HIDHIDE_QUERY_STATE，否则回退完整 HIDHIDE_QUERY。
                return Ack(envelope, "host-hello", _msiCostDeferredRequested
                    ? "{\"hidhideStateQuery\":true,\"msiCostWindowDeferred\":true,\"msiCostWindowDeferredAvailable\":" + (_msiCostDiagnostics is not null ? "true" : "false") + "}"
                    : "{\"hidhideStateQuery\":true}");
            }

            if (envelope!.Command == "HELLO") return Error(envelope, "protocol-error", "hello-already-received");
            if (_tuple is null || !SameTuple(_tuple, envelope)) return Error(envelope, "protocol-error", "tuple-mismatch");
            if (envelope.Sequence != _lastSequence + 1) return Error(envelope, "protocol-error", "sequence-stale-or-gap");
            _lastSequence = envelope.Sequence;
            _lastActivityAt = Environment.TickCount64;

            switch (envelope.Command)
            {
                case "PREPARE_TARGET":
                    if (_phase != Phase.Hello) return Error(envelope, "state-rejected", "prepare-requires-hello");
                    if (!TryPrepare(envelope, out var prepareDetail))
                    {
                        var response = Error(envelope, "prepare-rejected", prepareDetail);
                        RememberIdempotent(envelope, response);
                        return response;
                    }
                    ResetBackendSubmitStats();   // GP-926R17：目标代次重置（只绑当前 tuple）
                    _phase = Phase.Prepared;
                    ArmReportChain1410Safely(envelope);
                    {
                        var response = Ack(envelope, "target-prepared", prepareDetail);
                        RememberIdempotent(envelope, response);
                        return response;
                    }

                case "RECREATE_TARGET":
                    // Sleep-wake hot replace: same Host process, Dispose+Create.
                    // PREPARE_TARGET stays Hello-only so the first-open path is
                    // unchanged. Recreate may run from Hello (retry after a
                    // failed replace already released the old target) or from
                    // a live prepared/neutral/active/quiesced target.
                    if (_phase is Phase.AwaitHello or Phase.Released)
                        return Error(envelope, "state-rejected", "recreate-requires-hello-or-prepared-target");
                    if (_phase != Phase.Hello)
                    {
                        if (!ReleaseLocalLocked(out var recreateReleaseDetail))
                        {
                            _phase = LocalReleasePending ? Phase.Quiesced : Phase.Hello;
                            var response = Error(envelope, "recreate-release-failed", recreateReleaseDetail,
                                "local-dispose-failed");
                            RememberIdempotent(envelope, response);
                            return response;
                        }
                        _phase = Phase.Hello;
                    }
                    if (!TryPrepare(envelope, out var recreateDetail))
                    {
                        _phase = LocalReleasePending ? Phase.Quiesced : Phase.Hello;
                        var response = Error(envelope, "recreate-rejected", recreateDetail);
                        RememberIdempotent(envelope, response);
                        return response;
                    }
                    ResetBackendSubmitStats();   // GP-926R17：目标代次重置（只绑当前 tuple）
                    _phase = Phase.Prepared;
                    {
                        var response = Ack(envelope, "target-prepared", recreateDetail);
                        RememberIdempotent(envelope, response);
                        return response;
                    }

                case "QUERY_TARGET_OWNERSHIP":
                    if (envelope.Persona != PsEndpointOwnership.Persona)
                        return Error(envelope, "state-rejected", "ownership-persona-unavailable");
                    if (_psEndpointOwnership.BindingGeneration == 0)
                        return Error(envelope, "state-rejected", "ownership-target-not-created");
                    CapturePsEndpointOwnership();
                    {
                        // Incomplete is an observation, not a transport failure.
                        var response = Ack(envelope, "target-ownership");
                        RememberIdempotent(envelope, response);
                        return response;
                    }

                case "SUBMIT_NEUTRAL":
                    if (_phase is not (Phase.Prepared or Phase.Neutralized or Phase.Active or Phase.Quiesced))
                        return Error(envelope, "state-rejected", "neutral-requires-prepared-target");
                    if (_phase == Phase.Quiesced && !TryStopSteamDeckIdleForRelease(out var idleStopDetail))
                        return Error(envelope, "neutral-rejected", idleStopDetail);
                    if (!TrySubmitNeutral(out var neutralDetail, envelope, "submit-neutral-command")) return Error(envelope, "neutral-rejected", neutralDetail);
                    if (_phase != Phase.Quiesced) _phase = Phase.Neutralized;
                    return Ack(envelope, "neutral-accepted");

                case "SUBMIT_FRAME":
                    if (_phase is not (Phase.Neutralized or Phase.Active))
                        return Error(envelope, "state-rejected", "frame-requires-neutral");
                    if (!TrySubmitFrame(envelope, out var frameDetail))
                    {
                        // Match HC's target-failure boundary: a backend send
                        // failure cannot leave a logically connected writer
                        // available for a later frame. This is still only a
                        // local dispose result, never device-gone proof.
                        var frameReleaseOk = ReleaseLocalLocked(out var frameReleaseDetail);
                        _phase = LocalReleasePending ? Phase.Quiesced : Phase.Released;
                        var response = Error(
                            envelope,
                            "frame-rejected",
                            frameDetail + ";" + frameReleaseDetail,
                            frameReleaseOk ? "local-dispose-complete" : "local-dispose-failed");
                        RememberIdempotent(envelope, response);
                        return response;
                    }
                    var firstFrame = _phase == Phase.Neutralized;
                    _phase = Phase.Active;
                    return Ack(envelope, "frame-accepted", firstFrame: firstFrame);

                case "TUPLE_SUBMIT_STATS":
                    // GP-926R17 §4：只读聚合回执（绑当前 tuple；native 唤醒/借用结算低频调用，非周期）。
                    return Ack(envelope, "submit-stats", ComposeBackendSubmitStatsDetail());

                case "QUIESCE":
                    if (_phase is not (Phase.Prepared or Phase.Neutralized or Phase.Active or Phase.Quiesced))
                        return Error(envelope, "state-rejected", "quiesce-requires-prepared-target");
                    var quiesceOutputDetail = DetachGameOutputObservation();
                    _phase = Phase.Quiesced;
                    return Ack(envelope, "quiesced", quiesceOutputDetail);

                case "RELEASE_TARGET":
                    if (_phase is Phase.AwaitHello or Phase.Hello or Phase.Released)
                        return Error(envelope, "state-rejected", "release-requires-prepared-target");
                    var releaseOk = ReleaseLocal(out var releaseDetail, envelope);
                    _phase = LocalReleasePending ? Phase.Quiesced : Phase.Released;
                    {
                        var response = releaseOk
                        ? Ack(envelope, "release-local-dispose-complete", releaseDisposition: "local-dispose-complete")
                        : Error(envelope, "release-local-dispose-failed", releaseDetail, releaseDisposition: "local-dispose-failed");
                        RememberIdempotent(envelope, response);
                        return response;
                    }

                case "SHUTDOWN":
                    var shutdownReleaseOk = ReleaseLocal(out var shutdownDetail, envelope);
                    _phase = LocalReleasePending ? Phase.Quiesced : Phase.Released;
                    shutdown = !LocalReleasePending;
                    return shutdownReleaseOk
                        ? Ack(envelope, "shutdown", releaseDisposition: "local-dispose-complete")
                        : Error(envelope, "shutdown-local-dispose-failed", shutdownDetail, releaseDisposition: "local-dispose-failed");

                // GP-GBRETURN-1：关闭归还专用（**不**走 HidHideLock/tbox 同步 cycle，避免把
                // 消息循环堵住 ⇒ 帧命令不被拖住）。既有同步 HIDHIDE_CYCLE 语义完全不变。
                case "HIDHIDE_CYCLE_BEGIN":
                    return HandleCloseRefreshBegin(envelope);
                case "HIDHIDE_CYCLE_POLL":
                    return HandleCloseRefreshPoll(envelope);

                default:
                    if (envelope.Command.StartsWith("HIDHIDE_", StringComparison.Ordinal))
                    {
                        // HC 屏蔽层 API 化（对齐 Misc\HidHide.cs 的 API→CLI
                        // fallback + lock(hidLock) 串行化）：native 不再直接
                        // spawn HidHideCLI，改经 InputHost 进程内调用
                        // Nefarius.Drivers.HidHide HidHideControlService。
                        return HandleHidHideCommand(envelope, out shutdown);
                    }
                    return Error(envelope, "protocol-error", "unknown-command");
            }
        }

        // ── HIDHIDE 屏蔽命令：进程内 API（主）+ CLI fallback，对齐 HC ──
        // HC Misc\HidHide.cs 的操作统一 lock(hidLock) + HidHideControlService
        // API（Add/RemoveBlockedInstanceId、IsActive、ApplicationPaths），API
        // 抛异常才走 CLI。此处完全照搬该语义，并把结果塞进 response.detail
        // 的 JSON：native 不再需要 spawn HidHideCLI 就能 hide/unhide/cloak。
        private static readonly object HidHideLock = new();

        // GP-GBRETURN-1：关闭刷新**短受理**——回执立即返回；实际端口刷新在后台任务执行，结果只写
        // 内存状态（后台线程绝不写管道；回执一律由本串行循环写出）。一个在途动作：重入即拒绝。
        // 绑定 path + runId/epoch/powerGeneration（回读时随状态返回，供 native 归属比对）。
        private string HandleCloseRefreshBegin(CommandEnvelope envelope)
        {
            var path = envelope.HidHidePath ?? "";
            if (string.IsNullOrWhiteSpace(path))
                return Ack(envelope, "hidhide-cycle-begin",
                    "{\"accepted\":false,\"reason\":\"false-empty-path\"}");
            if (Interlocked.CompareExchange(ref _closeRefreshInFlight, 1, 0) != 0)
                return Ack(envelope, "hidhide-cycle-begin",
                    "{\"accepted\":false,\"reason\":\"in-flight\"}");
            lock (_closeRefreshGate)
            {
                _closeRefreshState = "in-flight";
                _closeRefreshDetail = "";
                _closeRefreshStartedTick = Environment.TickCount64;
                _closeRefreshElapsedMs = 0;
                _closeRefreshPath = path;
                _closeRefreshRunId = envelope.RunId;
                _closeRefreshEpoch = envelope.Epoch;
                _closeRefreshPowerGeneration = envelope.PowerGeneration;
            }
            var bindingPath = path;
            var bindingRunId = envelope.RunId;
            var bindingEpoch = envelope.Epoch;
            var bindingPowerGen = envelope.PowerGeneration;
            _ = Task.Run(() =>
            {
                // 后台只做该物理刷新（与同步 HIDHIDE_CYCLE 同一 Nefarius CyclePort 语义）；
                // 不碰虚拟目标/控制器，不写管道，不取 HidHideLock（HIDHIDE_* 命令不被拖住）。
                var t0 = Environment.TickCount64;
                string state;
                string detail;
                try
                {
                    var device = FindPhysicalDevice(bindingPath);
                    if (device is null)
                    {
                        state = "failed";
                        detail = "false-device-not-found";
                    }
                    else
                    {
                        var usb = device.ToUsbPnPDevice();
                        usb.CyclePort();   // void；不抛 = 成功（HC PnPDetails.cs:143-144）
                        state = "done";
                        detail = "true";
                    }
                }
                catch (Exception ex)
                {
                    state = "failed";
                    detail = "false:" + ex.GetType().Name;
                }
                lock (_closeRefreshGate)
                {
                    _closeRefreshState = state;
                    _closeRefreshDetail = detail;
                    _closeRefreshElapsedMs = Environment.TickCount64 - t0;
                }
                Interlocked.Exchange(ref _closeRefreshInFlight, 0);   // 动作已结算：允许下一次
            });
            return Ack(envelope, "hidhide-cycle-begin",
                JsonSerializer.Serialize(new
                {
                    accepted = true,
                    mode = "background-execute",
                    runId = bindingRunId,
                    epoch = bindingEpoch,
                    powerGeneration = bindingPowerGen
                }));
        }

        // GP-GBRETURN-1：**有界查询**——内存读，不取 HidHideLock、不阻塞循环（不堵帧通道）。
        private string HandleCloseRefreshPoll(CommandEnvelope envelope)
        {
            string payload;
            lock (_closeRefreshGate)
            {
                payload = JsonSerializer.Serialize(new
                {
                    state = _closeRefreshState,
                    ok = _closeRefreshState == "done",
                    inFlight = Volatile.Read(ref _closeRefreshInFlight) != 0,
                    detail = _closeRefreshDetail,
                    elapsedMs = _closeRefreshElapsedMs,
                    totalMs = _closeRefreshStartedTick == 0
                        ? 0 : Environment.TickCount64 - _closeRefreshStartedTick,
                    path = _closeRefreshPath,
                    runId = _closeRefreshRunId,
                    epoch = _closeRefreshEpoch,
                    powerGeneration = _closeRefreshPowerGeneration
                });
            }
            return Ack(envelope, "hidhide-cycle-poll", payload);
        }

        private static HidHideControlService? TryNewHidHideService()
        {
            try { return new HidHideControlService(); }
            catch { return null; }
        }

        // GP-MSIG3E-6-LC-R2 §四 R4：inverse application list 只读三态的**唯一生产者**。读取
        // 失败（getter 抛异常 / 服务不可用）⇒ `unknown`，只使该字段 UNKNOWN，不使其它已完整
        // 读取的字段失效。只消费 DLL 只读 getter `IsAppListInverted`，禁止 setter / `--inv-off`。
        // 旧 native 回执缺 `inverse` ⇒ 兼容 UNKNOWN（native 侧解析留空即 unknown）。
        internal static string ComposeHidHideInverseState(bool readOk, bool inverted)
        {
            if (!readOk) return "unknown";
            return inverted ? "on" : "off";
        }

        private static string ReadHidHideInverseState(HidHideControlService svc)
        {
            try { return ComposeHidHideInverseState(true, svc.IsAppListInverted); }
            catch { return ComposeHidHideInverseState(false, false); }
        }

        // 批次A v2（评审12 R3）：精简回执形状的**唯一生产者**——成功/失败两条路径都由
        // 它产出，selftest 可对两种形状直接断言（成功：ok/cloakOn/hidden 齐备且类型正确、
        // 无 applications；失败：`{"ok":false}"`，native 侧严格校验会判无效并走回退阶梯）。
        internal static string ComposeHidHidePeriodicStateDetail(bool ok, bool cloakOn, IEnumerable<string>? hidden)
        {
            if (!ok) return "{\"ok\":false}";
            return JsonSerializer.Serialize(new
            {
                ok = true,
                cloakOn,
                hidden = (hidden ?? Array.Empty<string>()).ToArray()
            });
        }

        private static bool HidHideCliFallback(string arguments, out string detail)
        {
            detail = "";
            try
            {
                // 与 HC fallback 一样：找不到 CLI 就静默失败（HidHide.cs:35-49
                // 静态 ctor 已用服务校验安装，CLI 仅备份路径）。
                var cli = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                    "Nefarius Software Solutions", "HidHide", "x64", "HidHideCLI.exe");
                if (!File.Exists(cli)) { detail = "cli-missing"; return false; }
                using var p = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = cli,
                        Arguments = arguments,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true
                    }
                };
                if (!p.Start()) { detail = "cli-start-failed"; return false; }
                // HC fallback 用 3s（HidHide.cs WaitForExit(TimeSpan.FromSeconds(3))）；
                // native 已删周期 CLI 探测，此处只需覆盖单条操作。
                if (!p.WaitForExit(3000)) { try { p.Kill(); } catch { } detail = "cli-timeout"; return false; }
                detail = (p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd()).Trim();
                return p.ExitCode == 0;
            }
            catch (Exception ex)
            {
                detail = "cli-exception:" + ex.GetType().Name;
                return false;
            }
        }

        private string HandleHidHideCommand(CommandEnvelope envelope, out bool shutdown)
        {
            shutdown = false;
            lock (HidHideLock)
            {
                switch (envelope.Command)
                {
                    case "HIDHIDE_HIDE":
                    {
                        string apiDetail;
                        var svc = TryNewHidHideService();
                        if (svc is not null)
                        {
                            try
                            {
                                if (!svc.BlockedInstanceIds.Contains(envelope.HidHidePath))
                                    svc.AddBlockedInstanceId(envelope.HidHidePath);
                                return Ack(envelope, "hidhide-hidden", "api-ok");
                            }
                            catch { apiDetail = "api-failed"; }
                        }
                        return HidHideCliFallback($"--dev-hide \"{envelope.HidHidePath}\"", out apiDetail)
                            ? Ack(envelope, "hidhide-hidden", "cli-fallback:" + apiDetail)
                            : Error(envelope, "hidhide-rejected", apiDetail);
                    }
                    case "HIDHIDE_UNHIDE":
                    {
                        var svc = TryNewHidHideService();
                        if (svc is not null)
                        {
                            try
                            {
                                if (svc.BlockedInstanceIds.Contains(envelope.HidHidePath))
                                    svc.RemoveBlockedInstanceId(envelope.HidHidePath);
                                return Ack(envelope, "hidhide-unhidden", "api-ok");
                            }
                            catch { }
                        }
                        return HidHideCliFallback($"--dev-unhide \"{envelope.HidHidePath}\"", out var uDetail)
                            ? Ack(envelope, "hidhide-unhidden", "cli-fallback:" + uDetail)
                            : Error(envelope, "hidhide-rejected", uDetail);
                    }
                    case "HIDHIDE_CLOAK":
                    {
                        var svc = TryNewHidHideService();
                        if (svc is not null)
                        {
                            try
                            {
                                svc.IsActive = envelope.HidHideCloak;
                                return Ack(envelope, envelope.HidHideCloak ? "hidhide-cloak-on" : "hidhide-cloak-off", "api-ok");
                            }
                            catch { }
                        }
                        return HidHideCliFallback(envelope.HidHideCloak ? "--cloak-on" : "--cloak-off", out var cDetail)
                            ? Ack(envelope, envelope.HidHideCloak ? "hidhide-cloak-on" : "hidhide-cloak-off", "cli-fallback:" + cDetail)
                            : Error(envelope, "hidhide-rejected", cDetail);
                    }
                    case "HIDHIDE_APP_REG":
                    case "HIDHIDE_APP_UNREG":
                    {
                        var svc = TryNewHidHideService();
                        if (svc is not null)
                        {
                            try
                            {
                                bool isReg = envelope.Command == "HIDHIDE_APP_REG";
                                var contains = svc.ApplicationPaths.Contains(envelope.HidHidePath);
                                if (isReg && !contains) svc.AddApplicationPath(envelope.HidHidePath);
                                if (!isReg && contains) svc.RemoveApplicationPath(envelope.HidHidePath);
                                return Ack(envelope, isReg ? "hidhide-app-registered" : "hidhide-app-unregistered", "api-ok");
                            }
                            catch { }
                        }
                        var arg = envelope.Command == "HIDHIDE_APP_REG"
                            ? $"--app-reg \"{envelope.HidHidePath}\""
                            : $"--app-unreg \"{envelope.HidHidePath}\"";
                        return HidHideCliFallback(arg, out var aDetail)
                            ? Ack(envelope, envelope.Command == "HIDHIDE_APP_REG" ? "hidhide-app-registered" : "hidhide-app-unregistered", "cli-fallback:" + aDetail)
                            : Error(envelope, "hidhide-rejected", aDetail);
                    }
                    case "HIDHIDE_QUERY":
                    {
                        if (_costFlagEnabled) _costHidHideQueryFull++;
                        var svc = TryNewHidHideService();
                        string payload = "{\"ok\":false}";
                        if (svc is not null)
                        {
                            try
                            {
                                var blocked = svc.BlockedInstanceIds.Select(x => x.ToUpperInvariant()).ToList();
                                var applications = svc.ApplicationPaths.ToList();
                                // R4：可选只读 inverse，独立 try（内部）⇒ 读异常只使该字段
                                // unknown，绝不使上面已完整读取的 hidden/applications/cloakOn 失效。
                                var inverse = ReadHidHideInverseState(svc);
                                payload = JsonSerializer.Serialize(new
                                {
                                    ok = true,
                                    cloakOn = svc.IsActive,
                                    hidden = blocked,
                                    applications,
                                    inverse
                                });
                            }
                            catch { }
                        }
                        return Ack(envelope, "hidhide-query", payload);
                    }
                    case "HIDHIDE_QUERY_STATE":
                    {
                        // 批次A（评审10 §3，2026-09-18）：周期核对（约 500ms，只读
                        // hidden/cloak）专用精简查询。刻意不触碰 ApplicationPaths：
                        // 依赖库 GetApplications 会为每个白名单路径重跑
                        // VolumeHelper.GetVolumeMappings（每次 3×char[65535] + string，
                        // 实测 637 MB/45s、202 次 AllocLarge Gen2）。完整
                        // HIDHIDE_QUERY 语义与调用点保持不变；本回执不含
                        // applications，禁止被需要白名单的完整快照逻辑消费。
                        if (_costFlagEnabled) _costHidHideQueryState++;
                        var svc = TryNewHidHideService();
                        string payload = ComposeHidHidePeriodicStateDetail(false, false, null);
                        if (svc is not null)
                        {
                            try
                            {
                                var blocked = svc.BlockedInstanceIds.Select(x => x.ToUpperInvariant()).ToList();
                                payload = ComposeHidHidePeriodicStateDetail(true, svc.IsActive, blocked);
                            }
                            catch { }
                        }
                        return Ack(envelope, "hidhide-query-state", payload);
                    }
                    case "HIDHIDE_CYCLE":
                    {
                        // 7.52：CyclePort 迁移到 InputHost 复用 HC 捆绑 Nefarius DLL
                        // （HC PnPDetails.CyclePort -> GetUsbPnPDevice().CyclePort()），
                        // 消除 native 手写 hub-cycle 原创传输层（“已调用 HC DLL 零原创”）。
                        // 失败仅 Ack 带错误（HC PnPDetails.cs:141-146 物理分支：Nefarius
                        // UsbPnPDevice.CyclePort() 返回 void，不抛 = 成功→true，抛异常→
                        // false；native 侧已有 cycle 失败不阻塞 admit/restore 的语义）。
                        if (string.IsNullOrWhiteSpace(envelope.HidHidePath))
                            return Ack(envelope, "hidhide-cycle", "false-empty-path");
                        try
                        {
                            var device = FindPhysicalDevice(envelope.HidHidePath);
                            if (device is null)
                                return Ack(envelope, "hidhide-cycle", "false-device-not-found");
                            var usb = device.ToUsbPnPDevice();
                            usb.CyclePort(); // void；不抛 = 成功（HC PnPDetails.cs:143-144）
                            return Ack(envelope, "hidhide-cycle", "true");
                        }
                        catch (Exception ex)
                        {
                            return Ack(envelope, "hidhide-cycle", "false:" + ex.GetType().Name);
                        }
                    }
                    default:
                        return Error(envelope, "protocol-error", "unsupported-hidhide-command");
                }
            }
        }

        public bool ReleaseLocal(out string detail, CommandEnvelope? releaseRequest = null)
        {
            // R101: bounded release-only evidence. Internal cleanup and frame
            // paths do not allocate a collector or inherit a previous receipt.
            var timing = releaseRequest?.Command is ("RELEASE_TARGET" or "SHUTDOWN")
                ? HostReleaseTimingEvidence.TryCreate() : null;
            timing?.Begin(HostReleaseTimingEvidence.Stage.LifecycleGate);
            lock (_lifecycleGate)
            {
                timing?.End(HostReleaseTimingEvidence.Stage.LifecycleGate);
                _releaseTimingEvidence = timing;
                _releaseTimingBinding = timing is null ? null : releaseRequest;
                try
                {
                    var ok = ReleaseLocalLocked(out detail, timing);
                    timing?.Complete(ok);
                    return ok;
                }
                finally { timing?.Finish(); }
            }
        }

        private static MsiNonSdReleaseDiagnostic.Snapshot MsiNonSdSnapshot(VirtualEndpointObservation observation) =>
            new(observation.ProbeSucceeded, observation.PrimaryRootCount, observation.XboxCompanionRootCount,
                observation.HidChildCount, observation.SteamDeckRootCount, observation.SteamDeckHidChildCount,
                observation.AllStarted, observation.Detail) { ExistingPnpCensus = observation.MsiPnpCensus };

        private void CaptureMsiSdkDeclaredIdentityDiagnostic()
        {
            if (_msiNonSdReleaseDiagnostic is null) return;
            try { _msiNonSdReleaseDiagnostic.TrySetSdkDeclaredIdentity(
                MsiSdkDeclaredIdentityDiagnostic.TryCapture(_controller, _assetVerified, LockedCoreMvid, Environment.TickCount64)); }
            catch { /* optional identity diagnostic never changes target lifetime */ }
        }

        private void BeginMsiNonSdReleaseDiagnostic()
        {
            if (_msiNonSdReleaseDiagnostic is null) return;
            try { _msiNonSdReleaseDiagnostic.TryBegin(MsiNonSdSnapshot(_virtualEndpoint), Environment.TickCount64,
                _controller is not null, _context is not null); } catch { /* optional diagnostic loss */ }
        }
        private void ObserveMsiNonSdReleaseDiagnostic(VirtualEndpointObservation observation, long beginTick)
        {
            if (_msiNonSdReleaseDiagnostic is null) return;
            try { _msiNonSdReleaseDiagnostic.TryObserveExistingPoll(MsiNonSdSnapshot(observation), beginTick, Environment.TickCount64); }
            catch { /* no effect on the existing removal/admission decision */ }
        }
        private void CompleteMsiNonSdReleaseDiagnostic(bool localDisposalReportedOk)
        {
            if (_msiNonSdReleaseDiagnostic is null) return;
            try { _msiNonSdReleaseDiagnostic.TryComplete(localDisposalReportedOk, Environment.TickCount64); } catch { }
        }

        private bool TryStopSteamDeckIdleForRelease(out string detail, HostReleaseTimingEvidence? releaseTiming = null)
        {
            detail = "";
            if (_tuple?.Persona != "steamdeck" || _controller is null) return true;
            releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.SteamDeckIdleStop);
            try
            {
                StopSteamDeckIdleForRelease(_controller);
                _steamDeckReleasePending = false;
                return true;
            }
            catch (Exception ex)
            {
                // Keep the objects and phase retryable. Never let SDK Dispose's
                // unchecked Join or context-wide cleanup race this live writer.
                _steamDeckReleasePending = true;
                _phase = Phase.Quiesced;
                detail = "steamdeck-idle-stop-failed:" + ex.GetType().Name + ":" + ex.Message;
                return false;
            }
            finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.SteamDeckIdleStop); }
        }

        private bool TryStopControllerOutputForRelease(out string detail, HostReleaseTimingEvidence? releaseTiming = null)
        {
            detail = "";
            if (_controller is null) { _outputReleasePending = false; return true; }
            releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.OutputReaderStop);
            try
            {
                var stopped = HostControllerOutputStop.TryStop(_controller, out detail);
                _outputReleasePending = !stopped;
                if (!stopped) _phase = Phase.Quiesced;
                return stopped;
            }
            finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.OutputReaderStop); }
        }

        private bool TryCompleteIdleCtsRelease(out string detail)
        {
            detail = "";
            if (_idleCtsRelease is null) return true;
            var completed = _idleCtsRelease.TryRelease(out detail);
            _idleCtsReleasePending = !completed;
            if (completed) _idleCtsRelease = null;
            else _phase = Phase.Quiesced;
            return completed;
        }

        private bool ReleaseLocalLocked(out string detail, HostReleaseTimingEvidence? releaseTiming = null)
        {
            _psEndpointOwnership.Invalidate("release-started");
            var gameOutputDetachDetail = DetachGameOutputObservation();
            if (_sdkDisposeFailure.Length != 0)
            {
                _phase = Phase.Quiesced;
                // Keep the original failure detail and read-only MSI observation
                // semantics on SHUTDOWN; an absent endpoint cannot prove SDK disposal.
                if (_msiSteamDeckReleaseFence1726?.HasReleaseReceipt == true)
                    _msiSteamDeckReleaseFence1726.ConfirmUnresolvedRelease(MsiSteamDeckReleaseFence1726.Read(
                        () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true)));
                detail = _sdkDisposeFailure;
                return false;
            }
            _targetCreationLifetime?.ReleaseAttempt();
            // A successful SDK Dispose can leave only our captured idle CTS
            // pending. Finish that exact resource before any context cleanup.
            if (!TryCompleteIdleCtsRelease(out detail)) return false;
            if (!TryStopSteamDeckIdleForRelease(out detail, releaseTiming)) return false;
            _msiCostDiagnostics?.InvalidateController();
            using var pnpDiagnosticScope = new MsiPnpCensusDiagnostic.Scope(_msiNonSdReleaseDiagnostic is not null);
            BeginMsiNonSdReleaseDiagnostic();
            var msiFence = _msiSteamDeckReleaseFence1726;
            // SHUTDOWN after RELEASE must retain both success and unresolved
            // errors; null SDK objects are not fresh proof of endpoint removal.
            if (_controller is null && _context is null && msiFence?.HasReleaseReceipt == true)
            {
                releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.FinalObservation);
                try
                {
                    if (!msiFence.ReleaseSucceeded)
                    {
                        _virtualEndpoint = MsiSteamDeckReleaseFence1726.Read(
                            () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true));
                        msiFence.ConfirmUnresolvedRelease(_virtualEndpoint);
                    }
                    detail = msiFence.ReleaseDetail;
                    return msiFence.ReleaseSucceeded;
                }
                finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.FinalObservation); }
            }
            VirtualEndpointObservation? ownedBefore = msiFence is null ? null : MsiSteamDeckReleaseFence1726.Read(
                () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true));
            VirtualEndpointObservation? ownedAfter = null;
            using var xboxDiagnostics = _context is not null ? XboxSdkDiagnostics.TryBegin(
                _tuple?.Persona ?? "", _hostInstanceId, _tuple?.RunId ?? "", _tuple?.Epoch ?? 0UL,
                "RELEASE_LOCAL", LockedCoreSha256, LockedCoreMvid) : null;
            xboxDiagnostics?.SetEndpoint(_virtualEndpoint.Detail);
            using var usbipDiagnostics = _context is not null ? UsbipSetupEvidence.TryBegin(
                _tuple?.Persona ?? "", _hostInstanceId, _tuple?.RunId ?? "", _tuple?.Epoch ?? 0UL,
                "RELEASE_LOCAL", LockedCoreSha256, LockedCoreMvid) : null;
            usbipDiagnostics?.Stage("release-begin", _virtualEndpoint.Detail);
            _targetPnpAdmitted = false;
            var errors = new List<string>();
            if (gameOutputDetachDetail.Length != 0) errors.Add(gameOutputDetachDetail);
            LogUsbipTransportLifecycleSafely("release-before-neutral");
            if (_controller is not null)
            {
                releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.Neutral);
                try
                {
                    // A neutral call can fail without throwing because
                    // TrySubmitNeutral deliberately converts SubmitState
                    // failures into a false result.  Treat that result as an
                    // incomplete safe-zero boundary; otherwise RELEASE_TARGET
                    // could acknowledge local-dispose-complete even though
                    // the required neutral was never accepted by the backend.
                    if (!TrySubmitNeutral(out var neutralDetail))
                        errors.Add("neutral:" + (string.IsNullOrWhiteSpace(neutralDetail) ? "failed" : neutralDetail));
                }
                catch (Exception ex) { errors.Add("neutral:" + ex.GetType().Name); }
                finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.Neutral); }
                // SDK Dispose ignores a failed output-reader Join. Retain all
                // SDK resources until that reader has actually terminated.
                if (!TryStopControllerOutputForRelease(out var outputStopDetail, releaseTiming))
                {
                    errors.Add("controller-output-stop:" + outputStopDetail);
                    detail = string.Join(";", errors);
                    CompleteMsiNonSdReleaseDiagnostic(false);
                    return false;
                }
                // Capture the pinned readonly idle CTS only after both worker
                // barriers have passed. Never release it before SDK Dispose.
                if (!HostControllerIdleCtsRelease.TryCapture(_controller, out var idleCtsRelease, out var captureDetail) ||
                    idleCtsRelease is null)
                {
                    _idleCtsReleasePending = true;
                    _phase = Phase.Quiesced;
                    errors.Add("controller-idle-cts-capture:" + captureDetail);
                    detail = string.Join(";", errors);
                    CompleteMsiNonSdReleaseDiagnostic(false);
                    return false;
                }
                releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.ControllerDispose);
                try
                {
                    usbipDiagnostics?.Stage("controller-dispose-begin");
                    _controller.Dispose();
                    usbipDiagnostics?.Stage("controller-dispose-returned");
                    idleCtsRelease.MarkControllerDisposeReturned();
                    _idleCtsRelease = idleCtsRelease;
                    _idleCtsReleasePending = true;
                }
                catch (Exception ex)
                {
                    // A set SDK _disposed flag is not proof that Dispose
                    // returned. Do not release the captured CTS on this path.
                    _idleCtsRelease = idleCtsRelease;
                    _idleCtsReleasePending = true;
                    _phase = Phase.Quiesced;
                    errors.Add("controller-dispose:" + ex.GetType().Name);
                    return RetainSdkDisposeFailure(errors, ownedBefore, out detail);
                }
                finally
                {
                    releaseTiming?.End(HostReleaseTimingEvidence.Stage.ControllerDispose);
                    LogUsbipTransportLifecycleSafely("controller-dispose-finished");
                }
                _controller = null;
                ResetVirtualSignalEvidenceSafely();
                if (!TryCompleteIdleCtsRelease(out var idleCtsDetail))
                {
                    errors.Add("controller-idle-cts-release:" + idleCtsDetail);
                    detail = string.Join(";", errors);
                    CompleteMsiNonSdReleaseDiagnostic(false);
                    return false;
                }
            }
            releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.ReportChainClose);
            try { CloseReportChain1410Safely("release-local"); }
            finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.ReportChainClose); }
            if (_context is not null)
            {
                try
                {
                    // HC's VirtualManager removes the old vTarget from its
                    // backend during Disconnect -> Dispose.  The YMCC
                    // adapter is a process boundary, so a controller from a
                    // previous Host process can survive that local object
                    // lifetime.  HIDMaestro exposes the corresponding
                    // backend-wide reconciliation API; use it only after the
                    // current controller has been neutralized and disposed.
                    // This is the single-public-endpoint close boundary, not
                    // a second writer or an input-source operation.
                    // Stock SDK 2.0.0 maps this bool overload to Orphaned,
                    // preserveInstall=true. Live controllers leased by another
                    // process are outside this sweep. Never replace it with
                    // HMCleanupScope.Everything to force a local release.
                    // Exact endpoint absence remains the release proof below.
                    releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.RemoveAllAttempt1);
                    try { HMContext.RemoveAllVirtualControllers(true); }
                    finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.RemoveAllAttempt1); }
                    // Do not acknowledge local release while a HIDMaestro
                    // endpoint is still present in the PnP tree. Dispose
                    // can return before the backend removal notification;
                    // one bounded retry closes that race.
                    var diagnosticPollBegin = _msiNonSdReleaseDiagnostic is null ? 0 : Environment.TickCount64;
                    VirtualEndpointObservation endpointRemoval;
                    releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.EndpointWaitAttempt1);
                    try
                    {
                        endpointRemoval = msiFence is null ? WaitForVirtualEndpointRemoval() : msiFence.Wait(
                            () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true),
                            () => Environment.TickCount64, Thread.Sleep, VirtualEndpointRemovalTimeoutMs, VirtualEndpointPollIntervalMs);
                    }
                    finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.EndpointWaitAttempt1); }
                    ObserveMsiNonSdReleaseDiagnostic(endpointRemoval, diagnosticPollBegin);
                    if (msiFence is null ? !endpointRemoval.IsAbsent : !msiFence.IsOwnedAbsent(endpointRemoval))
                    {
                        releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.RemoveAllAttempt2);
                        try { HMContext.RemoveAllVirtualControllers(true); }
                        finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.RemoveAllAttempt2); }
                        diagnosticPollBegin = _msiNonSdReleaseDiagnostic is null ? 0 : Environment.TickCount64;
                        releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.EndpointWaitAttempt2);
                        try
                        {
                            endpointRemoval = msiFence is null ? WaitForVirtualEndpointRemoval() : msiFence.Wait(
                                () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true),
                                () => Environment.TickCount64, Thread.Sleep, VirtualEndpointRemovalTimeoutMs, VirtualEndpointPollIntervalMs);
                        }
                        finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.EndpointWaitAttempt2); }
                        ObserveMsiNonSdReleaseDiagnostic(endpointRemoval, diagnosticPollBegin);
                    }
                    if (msiFence is not null) ownedAfter = _virtualEndpoint = endpointRemoval;
                    if (msiFence is null && !endpointRemoval.IsAbsent)
                        errors.Add("virtual-endpoint-removal-timeout:" + endpointRemoval.Detail);
                }
                catch (Exception ex) { errors.Add("virtual-endpoint-reconcile:" + ex.GetType().Name); }
                releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.ContextDispose);
                try { _context.Dispose(); }
                catch (Exception ex)
                {
                    errors.Add("context-dispose:" + ex.GetType().Name);
                    return RetainSdkDisposeFailure(errors, ownedBefore, out detail);
                }
                finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.ContextDispose); }
                _context = null;
            }
            releaseTiming?.Begin(HostReleaseTimingEvidence.Stage.FinalObservation);
            try
            {
                if (msiFence is not null)
                {
                    // Even exceptional cleanup/unknown ownership gets a real final
                    // observation and an explicit failure receipt, never prepare cache.
                    ownedAfter ??= MsiSteamDeckReleaseFence1726.Read(
                        () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true));
                    _virtualEndpoint = ownedAfter.Value;
                    return msiFence.CompleteRelease(ownedBefore!.Value, ownedAfter.Value, errors, out detail);
                }
                detail = errors.Count == 0 ? "local-dispose-complete" : string.Join(";", errors);
                CompleteMsiNonSdReleaseDiagnostic(errors.Count == 0);
                return errors.Count == 0;
            }
            finally { releaseTiming?.End(HostReleaseTimingEvidence.Stage.FinalObservation); }
        }

        private bool RetainSdkDisposeFailure(List<string> errors, VirtualEndpointObservation? ownedBefore, out string detail)
        {
            _sdkDisposeFailure = detail = string.Join(";", errors);
            _phase = Phase.Quiesced;
            var fence = _msiSteamDeckReleaseFence1726;
            if (fence is not null && ownedBefore.HasValue)
            {
                var after = MsiSteamDeckReleaseFence1726.Read(
                    () => ObserveVirtualEndpoint("steamdeck", requireInstanceIds: true));
                fence.CompleteRelease(ownedBefore.Value, after, errors, out detail);
                _sdkDisposeFailure = detail;
            }
            CompleteMsiNonSdReleaseDiagnostic(false);
            return false;
        }

        private void RememberIdempotent(CommandEnvelope envelope, string response) =>
            _idempotentReplies[envelope.RequestId] =
                new IdempotentReply(JsonSerializer.Serialize(envelope), envelope.Command, response);

        private bool TryPrepare(CommandEnvelope envelope, out string detail)
        {
            using var usbipPreparationGate = YeManInputHost.UsbipMaintenanceGate.TryEnter(envelope.Persona == "steamdeck");
            if (usbipPreparationGate is null) {
                detail = envelope.Persona == "steamdeck" && YeManCC.UsbipMaintenanceState.Pending
                    ? "usbip-reboot-required" : "usbip-version-maintenance-in-progress";
                return false;
            }
            if (_sdkDisposeFailure.Length != 0)
            {
                detail = "prior-sdk-dispose-unconfirmed:" + _sdkDisposeFailure;
                return false;
            }
            if (_idleCtsReleasePending || _idleCtsRelease is not null)
            {
                detail = "prior-idle-cts-release-pending";
                return false;
            }
            if (!TryGetVirtualOutputProfile(envelope.Persona, envelope.ProfileIdentity, out var profileName))
            {
                detail = "persona-not-admitted";
                return false;
            }
            var targetCreationAttempt = _targetCreationLifetime?.BeginAttempt() ?? default;
            var targetCreationPrepared = false;
            _msiCostDiagnostics?.InvalidateController();
            using var pnpDiagnosticScope = new MsiPnpCensusDiagnostic.Scope(
                MsiNonSdReleaseDiagnostic.ShouldObserve(_msiNonSdReleaseDiagnosticRequested, envelope.Persona));
            CloseReportChain1410Safely("target-prepare");
            _msiNonSdReleaseDiagnostic = null; _msiNonSdReleaseDiagnosticBinding = null;
            try { if (MsiNonSdReleaseDiagnostic.ShouldObserve(_msiNonSdReleaseDiagnosticRequested, envelope.Persona))
                { _msiNonSdReleaseDiagnostic = new(envelope.Persona); _msiNonSdReleaseDiagnosticBinding = envelope; } }
            catch { /* diagnostic initialization never rejects target prepare */ }
            // PREPARE is per-epoch explicit opt-in. RECREATE in that same epoch
            // retains the requirement but resets pins/receipts before SDK create.
            _msiSteamDeckReleaseFence1726 = (envelope.Command == "PREPARE_TARGET"
                ? envelope.MsiSteamDeckReleaseFence : _msiSteamDeckReleaseFence1726 is not null)
                ? new MsiSteamDeckReleaseFence1726(envelope) : null;
            // GP-XBOX-7: enable stock SDK setup timing before its static/prewarm initialization.
            using var xboxDiagnostics = XboxSdkDiagnostics.TryBegin(envelope.Persona, _hostInstanceId,
                envelope.RunId, envelope.Epoch, envelope.Command, LockedCoreSha256, LockedCoreMvid);
            // USBIP evidence is scoped to SD lifecycle transitions. It never enables SDK
            // packet diagnostics or changes preparation, migration, retry or timeout policy.
            using var usbipDiagnostics = UsbipSetupEvidence.TryBegin(envelope.Persona, _hostInstanceId,
                envelope.RunId, envelope.Epoch, envelope.Command, LockedCoreSha256, LockedCoreMvid);
            // GP-XBOX-5：PREPARE 的数秒停顿拆开记录；非 Elite 不启用，不进入帧路径。
            var xboxTimings = envelope.Persona == "elite" ? new List<string>() : null;
            var xboxClock = xboxTimings is null ? null : Stopwatch.StartNew();
            long xboxStageStart = 0;
            // GP-MSIG3E-3-R2（E3）：全 persona 的**阶段水位**（与 elite 计时解耦）——创建失败时
            // 定位黑盒失败发生在哪一步（context-profiles / old-endpoint-reconcile /
            // profile-backend / sdk-create / pnp-arrival）。此前阶段仅对 elite 记录，
            // steam-deck-composite 被 Win32Exception 拒绝时只剩异常类型，无法判断失败步骤。
            var lastStage = "enter";
            var backendReceipt = "backend-check-not-reached";
            void XboxStage(string stage)
            {
                lastStage = stage;
                usbipDiagnostics?.Stage(stage);
                if (xboxClock is null) return;
                var elapsed = xboxClock.ElapsedMilliseconds;
                xboxTimings!.Add(stage + "=" + (elapsed - xboxStageStart));
                xboxStageStart = elapsed;
            }
            string XboxTimingDetail() => xboxTimings is null ? "" :
                ";xbox-startup-ms:" + string.Join(",", xboxTimings) + ",total=" + xboxClock!.ElapsedMilliseconds;
            try
            {
                usbipDiagnostics?.Stage("context-create-begin");
                _context = new HMContext();
                _context.LoadDefaultProfiles();
                XboxStage("context-profiles");
                // HC parity（批107 / T2，2026-09-24）：HC SetControllerModeCore
                // （deps\handheldcompanion-runtime\source\Managers\VirtualManager.cs:446-457）
                // 换型 = **只对当前 vTarget** Disconnect → Dispose → vTarget=null，
                // 全程没有机器级清扫。进入本方法时本 Host 的当前 vTarget 必已拆除：
                // PREPARE_TARGET 只在 Phase.Hello 放行（本方法唯一入口之一），
                // RECREATE_TARGET 先走 ReleaseLocalLocked（Dispose 后 phase=Hello）。
                // 因此这里要处理的只有**跨进程残留**：前一代 Host 的端点处在 PnP
                // 移除瞬态，或崩溃后残留。旧形状在每次 prepare 上无条件调用机器级
                // RemoveAllVirtualControllers(true)：比 HC 多出一次全机边界动作，且
                // 发生在**新目标建立之前**（§73：直切 ~1 s 内完成、不等旧端点真消失，
                // 建塔后旧 persona 节点被内核带回来 ⇒ 双柄）。新形状（清场纪律对齐
                // HC + 有界，回执行可判）：
                //   ① 先观察：全族已空 ⇒ **一次机器级调用都不做**（HC parity）；
                //   ② 否则有界等待旧族自行走完（含 usbip 族，旧形状不看这一族）；
                //   ③ 仍有他族/幽灵残留 ⇒ **恰好一次**有界 fallback 清扫 + 再次有界等待。
                _targetPnpAdmitted = false;
                var reconcileStarted = Environment.TickCount64;
                var reconcileBefore = ObserveVirtualEndpoint(requireInstanceIds: _msiSteamDeckReleaseFence1726 is not null);
                var reconcilePath = "hc-parity";
                _virtualEndpoint = reconcileBefore;
                if (!reconcileBefore.IsFullyAbsent)
                {
                    _virtualEndpoint = WaitForEndpointFamilyRemoval(SwitchReconcileFamilyWaitMs, requireInstanceIds: _msiSteamDeckReleaseFence1726 is not null);
                    if (!_virtualEndpoint.IsFullyAbsent)
                    {
                        HMContext.RemoveAllVirtualControllers(true);
                        _virtualEndpointReconciled = true;
                        reconcilePath = "fallback-sweep";
                        _virtualEndpoint = WaitForEndpointFamilyRemoval(SwitchReconcileFamilyWaitMs, requireInstanceIds: _msiSteamDeckReleaseFence1726 is not null);
                    }
                }
                var reconcileReceipt = "switch-reconcile:path=" + reconcilePath +
                    ";waitedMs=" + (Environment.TickCount64 - reconcileStarted) +
                    ";before=" + DescribeEndpointCounts(reconcileBefore) +
                    ";after=" + DescribeEndpointCounts(_virtualEndpoint);
                if (_msiSteamDeckReleaseFence1726 is null ? !_virtualEndpoint.IsAbsent : !_virtualEndpoint.IsFullyAbsent)
                {
                    ReleaseLocal(out _);
                    detail = "stale-virtual-endpoint:" + _virtualEndpoint.Detail + ";" + reconcileReceipt;
                    return false;
                }
                var zeroBeforeCreate = _virtualEndpoint;
                _msiNonSdReleaseDiagnostic?.TrySetBeforeCreateCensus(zeroBeforeCreate.MsiPnpCensus);
                XboxStage("old-endpoint-reconcile");
                // S0'''（2026-09-16 用户批准）：Sony persona 走"声明序置换"profile（数据字节不变）。
                var profile = ApplySonyAxisDeclarationSwap(_context.GetProfile(profileName));
                if (profile is null)
                {
                    ReleaseLocal(out _);
                    detail = "profile-missing:" + profileName;
                    return false;
                }
                // GP-PREREQ-4: prepare only the selected profile's backend; probe failure is a warning.
                // USBIP/local auto-install paths remain unchanged, and the SDK factory owns creation success.
                _controller = CreateTargetWithBackendWarning(() =>
                {
                    var ready = TryEnsureDriverInstalled(_context, out var backendDetail,
                        requireUsbip: profile.RequiresUsbipBackend, usbipDiagnostics: usbipDiagnostics);
                    XboxStage("profile-backend");
                    return (ready, backendDetail);
                }, () =>
                {
                    lastStage = "sdk-create";
                    usbipDiagnostics?.Stage("sdk-create-begin", backendReceipt);
                    if (envelope.Persona == PsEndpointOwnership.Persona)
                        _psEndpointOwnership.BeginCreate(PsOwnershipBinding(envelope));
                    return _context.CreateController(profile);
                }, out backendReceipt);
                if (envelope.Persona == PsEndpointOwnership.Persona)
                    _psEndpointOwnership.Created(_controller);
                _targetCreationLifetime?.Created(targetCreationAttempt, _controller);
                CaptureMsiSdkDeclaredIdentityDiagnostic();
                LogUsbipTransportLifecycleSafely("target-created");
                _screenPadOwnsReport = false;
                _screenPadPacketCounter = 0;
                ScreenPadSubmissionEvidence.Reset();
                _steamDeckReleasePending = false;
                _outputReleasePending = false;
                XboxStage("sdk-create");
                _virtualEndpoint = WaitForVirtualEndpointArrival(envelope.Persona, requireInstanceIds: _msiSteamDeckReleaseFence1726 is not null);
                XboxStage("pnp-arrival");
                xboxDiagnostics?.SetEndpoint(_virtualEndpoint.Detail);
                if (!_virtualEndpoint.IsUniqueStartedTarget(envelope.Persona))
                {
                    var observationDetail = _virtualEndpoint.Detail;
                    ReleaseLocal(out var releaseDetail);
                    detail = "target-pnp-not-admitted:" + observationDetail + ";" + releaseDetail +
                        ";" + reconcileReceipt + ";" + backendReceipt;
                    return false;
                }
                if (_msiSteamDeckReleaseFence1726 is not null &&
                    !_msiSteamDeckReleaseFence1726.TryPin(zeroBeforeCreate, _virtualEndpoint, out var pinDetail))
                {
                    ReleaseLocal(out var releaseDetail);
                    detail = pinDetail + ";" + releaseDetail;
                    return false;
                }
                detail = "target-pnp-admitted;profile=" + profileName +
                    ";descriptor-vid=" + profile.VendorId.ToString("X4") +
                    ";descriptor-pid=" + profile.ProductId.ToString("X4") +
                    ";" + _virtualEndpoint.Detail + ";" + reconcileReceipt + ";" + backendReceipt + XboxTimingDetail();
                _targetPnpAdmitted = true;
                CapturePsEndpointOwnership();
                BindGameOutputObservation(envelope);
                targetCreationPrepared = true;
                return true;
            }
            catch (Exception ex)
            {
                // GP-MSIG3E-3-R2（E3）：先采集失败阶段（在覆盖为 "exception" 之前），再补充黑盒诊断
                // （类型全名 / HResult / Win32 NativeErrorCode / 内层异常链 / 截断栈顶帧），最后才标记
                // exception 阶段。仅加诊断，不改创建策略或描述符。
                var failedStage = lastStage;
                usbipDiagnostics?.Stage("prepare-exception", "stage=" + failedStage + DescribePrepareException(ex));
                ReleaseLocal(out _);
                XboxStage("exception");
                detail = "prepare-exception:" + ex.GetType().Name
                    + ";stage=" + failedStage
                    + DescribePrepareException(ex)
                    + ";" + backendReceipt
                    + XboxTimingDetail();
                return false;
            }
            finally
            {
                usbipDiagnostics?.Stage("prepare-result", targetCreationPrepared ? "target-pnp-admitted" : "not-prepared");
                if (!targetCreationPrepared) _targetCreationLifetime?.Failure(targetCreationAttempt);
            }
        }

        // GP-MSIG3E-3-R2（E3）：创建失败黑盒诊断。此前 PREPARE_TARGET 的 catch 只回
        // ex.GetType().Name，无法解释"6 次 steam-deck-composite 创建全部在 PREPARE_TARGET 被
        // Win32Exception 拒绝"的具体步骤与错误码。这里输出：异常类型全名、HResult（十六进制）、
        // Win32 NativeErrorCode（若是 Win32Exception）、最多 4 层内层异常链、以及截断后的栈顶。
        // 全字段单行化并限长，避免污染 pipe 回执（回执是分号分隔的单行）。
        private static string DescribePrepareException(Exception ex)
        {
            var sb = new StringBuilder();
            var current = ex;
            var depth = 0;
            while (current is not null && depth < 4)
            {
                sb.Append(";ex").Append(depth).Append('=').Append(current.GetType().FullName);
                sb.Append(";hr").Append(depth).Append("=0x")
                    .Append((current.HResult & 0xFFFFFFFFL).ToString("X8"));
                if (current is System.ComponentModel.Win32Exception win32)
                    sb.Append(";native").Append(depth).Append('=').Append(win32.NativeErrorCode);
                if (!string.IsNullOrWhiteSpace(current.Message))
                    sb.Append(";msg").Append(depth).Append('=').Append(SanitizeReceiptField(current.Message, 160));
                current = current.InnerException;
                depth++;
            }
            if (!string.IsNullOrWhiteSpace(ex.StackTrace))
                sb.Append(";stack=").Append(SanitizeReceiptField(ex.StackTrace!, 400));
            return sb.ToString();
        }

        // 把任意诊断文本压成单行、限长，供分号分隔的 pipe 回执安全承载。
        private static string SanitizeReceiptField(string value, int maxLen)
        {
            var flat = value.Replace('\r', ' ').Replace('\n', ' ').Replace(';', ',');
            return flat.Length <= maxLen ? flat : flat.Substring(0, maxLen) + "...";
        }

        // Shared PnP lookup for the HidHide lane (HIDHIDE_CYCLE by device
        // path) and, historically, the removed physical-xinput suspension
        // mechanism. Keep: HIDHIDE_CYCLE still resolves the target device
        // through the same Nefarius GetDeviceByInstanceId API.
        private static PnPDevice? FindPhysicalDevice(string baseInstanceId)
        {
            try { return PnPDevice.GetDeviceByInstanceId(baseInstanceId, DeviceLocationFlags.Normal); }
            catch { return null; }
        }

        private static bool TryEnsureDriverInstalled(HMContext context, out string detail, bool requireUsbip = false,
            UsbipSetupEvidence? usbipDiagnostics = null)
        {
            if (requireUsbip)
                return TryEnsureUsbipBackend(out detail, usbipDiagnostics);

            if (context.IsDriverInstalled)
            {
                detail = "driver-already-installed";
                return true;
            }

            if (!File.Exists(StandaloneTestAutoInstallMarkerPath))
            {
                detail = "driver-not-installed";
                return false;
            }

            try
            {
                context.InstallDriver();
            }
            catch (Exception ex)
            {
                detail = "driver-install-exception:" + ex.GetType().Name;
                return false;
            }

            if (!context.IsDriverInstalled)
            {
                detail = "driver-install-failed";
                return false;
            }

            detail = "driver-auto-installed-for-standalone-test";
            return true;
        }

        // Stock HIDMaestro 2.0.0 owns its bundled usbip-win2 0.9.8.1 installation.
        // No local transport fork or external-installer fallback.
        private static bool TryEnsureUsbipBackend(out string detail, UsbipSetupEvidence? usbipDiagnostics = null)
        {
            usbipDiagnostics?.Stage("backend-probe-begin");
            if (HMContext.IsUsbipBackendAvailable)
            {
                detail = "usbip-backend-available";
                usbipDiagnostics?.Stage("backend-probe-result", detail, true);
                return true;
            }
            usbipDiagnostics?.Stage("backend-probe-result", "usbip-backend-unavailable", false);

            try
            {
                // SDK 内建安装：提权由 SDK 内部完成（RunSilentInstall + pnputil），
                // 与 HC 侧 UsbipDriverInstaller 同构；输入 Host 自身以非管理员
                // 模式运行时亦可经由 SDK 例外抛出，不在此造轮子拉安装器。
                usbipDiagnostics?.Stage("sdk-install-begin");
                Action<string>? progress = usbipDiagnostics is null ? null : usbipDiagnostics.Report;
                HMContext.InstallUsbipBackend(progress);
                if (HMContext.IsUsbipBackendAvailable)
                {
                    detail = "usbip-backend-installed-by-sdk";
                    usbipDiagnostics?.Stage("sdk-install-returned", detail, true);
                    return true;
                }
                detail = "usbip-backend-install-failed";
                usbipDiagnostics?.Stage("sdk-install-returned", detail, false);
                return false;
            }
            catch (Exception ex)
            {
                // 采用 install-failed 前缀以命中 native 侧 E4 分支的
                // usbip-backend-install-failed 判定（prompt 2 重试/官网）；
                // installer-missing（prompt 1 下载安装包）不再出现：SDK 内建
                // 安装不依赖外部安装包，无需用户手放 redist。
                detail = "usbip-backend-install-failed:" + ex.GetType().Name + ":" + ex.Message;
                usbipDiagnostics?.Stage("sdk-install-exception", detail, false);
                return false;
            }
        }

        private void ArmReportChain1410Safely(CommandEnvelope envelope)
        {
            try
            {
                CloseReportChain1410Safely("prepare-arm");
                if (!_reportChainCapabilityAdvertised1410 || envelope.ReportChainPrepare1410 is null || !_targetPnpAdmitted || _controller is null) return;
                var runtime = new MsiReportChainRuntime1410();
                if (runtime.ArmSafely(envelope.ReportChainPrepare1410, _reportChainParentPid1410, Environment.ProcessId,
                    _hostInstanceId, envelope.Persona, envelope.ProfileIdentity, envelope.RunId, envelope.Epoch,
                    envelope.PowerGeneration, _virtualEndpoint.Detail, _targetPnpAdmitted)) _reportChain1410 = runtime;
            }
            catch { _reportChain1410 = null; }
        }

        private void CloseReportChain1410Safely(string reason)
        {
            ResetVirtualSignalEvidenceSafely();
            var runtime = _reportChain1410; _reportChain1410 = null;
            try { runtime?.CloseSafely(reason); } catch { }
        }

        private void PrepareReportChainInvocation1410(CommandEnvelope? cause, long? encoderIndex, string reason,
            out MsiPersonaReportChain20261012? tracker, out MsiReportChainFrame20261012? frame)
        {
            tracker = null; frame = null;
            try
            {
                var runtime = _reportChain1410;
                if (runtime is null || !runtime.Armed) return;
                var binding = cause ?? _tuple;
                if (binding is null) return;
                // binding.Buttons is the FINAL submitted native canonical frame, not raw
                // physical-source bits. Optional source metadata supplies IDs only.
                runtime.TryInvocationSafely(binding.RunId, binding.Epoch, binding.PowerGeneration, binding.Persona,
                    binding.ProfileIdentity, cause?.Sequence ?? _lastSequence, reason, encoderIndex,
                    cause?.ReportChainSource1410, reason == "live-frame" ? (uint)binding.Buttons : null, out tracker, out frame);
            }
            catch { tracker = null; frame = null; }
        }

        private void ResetVirtualSignalEvidenceSafely()
        {
            try { _virtualSignalEvidence?.ResetSafely(_costFlagEnabled); } catch { }
        }

        internal HostVirtualSignalEvidence.Attempt BeginVirtualSignalEvidenceSafely(HMController controller,
            CommandEnvelope? cause, CommandEnvelope? canonical, string reason, long? encoderIndex,
            byte[]? report, HMGamepadState? state)
        {
            try
            {
                if (!_costFlagEnabled) { ResetVirtualSignalEvidenceSafely(); return default; }
                var binding = cause ?? _tuple;
                // Never substitute a trigger/prepare envelope for actual internal controls.
                if (binding is null || canonical is null) return default;
                var evidence = _virtualSignalEvidence ??= new HostVirtualSignalEvidence(CostStateDir);
                return evidence.BeginSafely(_hostInstanceId, controller, binding, canonical,
                    cause?.Sequence ?? _lastSequence, cause is not null, reason, encoderIndex, report, state);
            }
            catch { return default; }
        }

        internal void CompleteVirtualSignalEvidenceSafely(HostVirtualSignalEvidence.Attempt attempt, Exception? exception)
        {
            try
            {
                if (_costFlagEnabled) _virtualSignalEvidence?.CompleteSafely(attempt, exception);
                else ResetVirtualSignalEvidenceSafely();
            }
            catch { }
        }

        private void SubmitSonyReportChain1410(HMController controller, CommandEnvelope? cause, byte[] report,
            long encoderIndex, string reason, CommandEnvelope? canonical = null)
        {
            var attempt = BeginVirtualSignalEvidenceSafely(controller, cause, canonical ?? cause, reason, encoderIndex, report, null);
            MsiPersonaReportChain20261012? tracker = null; MsiReportChainFrame20261012? frame = null;
            try { PrepareReportChainInvocation1410(cause, encoderIndex, reason, out tracker, out frame); }
            catch { tracker = null; frame = null; }
            // Keep the existing opt-in tracker. Its delegate is allocated only when armed.
            static void Tracked(MsiPersonaReportChain20261012 tracker, MsiReportChainFrame20261012 frame,
                HMController controller, byte[] report) => tracker.InvokeRaw(frame, report, bytes => controller.SubmitRawReport(bytes));
            try
            {
                try
                {
                    if (tracker is not null && frame is not null) Tracked(tracker, frame, controller, report);
                    else controller.SubmitRawReport(report);
                }
                catch (Exception ex) { CompleteVirtualSignalEvidenceSafely(attempt, ex); throw; }
                CompleteVirtualSignalEvidenceSafely(attempt, null);
            }
            finally { try { _reportChain1410?.AfterBackendSafely(); } catch { } }
        }

        private void SubmitEliteReportChain1410(HMController controller, HMGamepadState state, CommandEnvelope? cause,
            string reason, CommandEnvelope? canonical = null)
        {
            var attempt = BeginVirtualSignalEvidenceSafely(controller, cause, canonical ?? cause, reason, null, null, state);
            MsiPersonaReportChain20261012? tracker = null; MsiReportChainFrame20261012? frame = null;
            try { PrepareReportChainInvocation1410(cause, null, reason, out tracker, out frame); }
            catch { tracker = null; frame = null; }
            static void Tracked(MsiPersonaReportChain20261012 tracker, MsiReportChainFrame20261012 frame,
                HMController controller, HMGamepadState state) => tracker.InvokeUnencodedState(frame, () => controller.SubmitState(state));
            try
            {
                try
                {
                    if (tracker is not null && frame is not null) Tracked(tracker, frame, controller, state);
                    else controller.SubmitState(state);
                }
                catch (Exception ex) { CompleteVirtualSignalEvidenceSafely(attempt, ex); throw; }
                CompleteVirtualSignalEvidenceSafely(attempt, null);
            }
            finally { try { _reportChain1410?.AfterBackendSafely(); } catch { } }
        }

        private bool TrySubmitFrame(CommandEnvelope envelope, out string detail)
        {
            if (!TryGetVirtualOutputProfile(envelope.Persona, envelope.ProfileIdentity, out var profileName))
            {
                detail = "persona-not-admitted";
                return false;
            }
            if (_controller is null || _context is null)
            {
                detail = "target-not-ready";
                return false;
            }
            var started = Stopwatch.GetTimestamp();
            _msiHostStageEvidence?.BeginSubmit();

            try
            {
                var profile = _context.GetProfile(profileName) ?? throw new InvalidOperationException("profile-missing:" + profileName);
                // R3（2026-09-17 需求单）：HID 提交段计时（门控缓存；关时零累加）。
                var costHidStarted = _costFlagEnabled ? Stopwatch.GetTimestamp() : 0L;
                _msiHostStageEvidence?.BeginBackend();
                try
                {
                    switch (envelope.Persona)
                    {
                        case "steamdeck": SubmitSteamDeckWithScreenPads(_controller, profile, envelope, _costFlagEnabled, signalSession: this, signalCause: envelope); break;
                        case "elite": SubmitEliteReportChain1410(_controller, BuildEliteState(profile, envelope), envelope, "live-frame"); break;
                        case "dualsense-edge":
                        {
                            var frameIndex = _dualSenseFrameCounter++;
                            var report = BuildDualSenseDataOnlyReport(profile, envelope, frameIndex, MapDsEdgePaddles(envelope.BackButtons));
                            SubmitSonyReportChain1410(_controller, envelope, report, frameIndex, "live-frame");
                            break;
                        }
                        default: throw new InvalidOperationException("persona-not-admitted");
                    }
                }
                finally
                {
                    _msiHostStageEvidence?.EndBackend();
                }
                if (_costFlagEnabled)
                {
                    _costHidSubmitUsSum += (long)(Stopwatch.GetElapsedTime(costHidStarted).TotalMilliseconds * 1000.0);
                    _costHidSubmitCount++;
                    RecordCostRole("hid-submit");
                }
                _lastSubmitElapsedUs = (long)(Stopwatch.GetElapsedTime(started).TotalMilliseconds * 1000.0);
                _backendSubmitOk++;
                _lastBackendDetail = "report-submitted";
                _lastBackendSequence = envelope.Sequence;
                detail = "report-submitted";
                RecordCostFrame(_lastSubmitElapsedUs, rejected: false);
                return true;
            }
            catch (Exception ex)
            {
                _lastSubmitElapsedUs = (long)(Stopwatch.GetElapsedTime(started).TotalMilliseconds * 1000.0);
                _backendSubmitFail++;
                _lastBackendDetail = "submit-exception:" + ex.GetType().Name;
                _lastBackendSequence = envelope.Sequence;
                detail = "submit-exception:" + ex.GetType().Name;
                RecordCostFrame(_lastSubmitElapsedUs, rejected: true);
                return false;
            }
            finally
            {
                _msiHostStageEvidence?.EndSubmit();
            }
        }

        // ── R3（2026-09-17 tick 成本需求单）：宿主侧 1Hz 成本聚合 ──
        // 门控 = native 写的 input-host-cost-enabled.flag（与 fan-logging-enabled.flag
        // 同构）；关时零累加零写入；仅 1Hz 一行为输入宿主成本事件，写
        // %LOCALAPPDATA%\YeManCC\input-host-cost.log。不触碰协议/回执字段。
        private static string CostStateDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");

        private static bool CostLoggingRequestedByFlag()
        {
            try
            {
                var flag = Path.Combine(CostStateDir, "input-host-cost-enabled.flag");
                if (!File.Exists(flag)) return false;
                var value = File.ReadAllText(flag).Trim();
                return value.Equals("enabled", StringComparison.OrdinalIgnoreCase) || value == "1";
            }
            catch { return false; }
        }

        internal bool CostEnabledForRead => _costFlagEnabled;

        // R3 补充（2026-09-17）：管道读侧耗时（含等待——用于区分"读等待"与真实
        // 处理，均值≈帧间隔属正常；非纯 CPU 指标）。
        internal void RecordCostPipeRead(long readUs)
        {
            if (!_costFlagEnabled) return;
            _costPipeReadUsSum += readUs;
            _costPipeReadCount++;
            RecordCostRole("pipe-read");
        }

        private void RecordCostFrame(long submitUs, bool rejected)
        {
            if (!_costFlagEnabled)
            {
                // MSI's background reader may notice logging re-enabled. No
                // flag file or process query is performed by this polling call.
                if (_msiCostDeferredRequested) MaybeEmitCostWindow();
                return;
            }
            _costFrames++;
            if (rejected) _costFramesRejected++;
            if (_costSubmitUsCount < _costSubmitUsSamples.Length)
                _costSubmitUsSamples[_costSubmitUsCount++] = submitUs;
            MaybeEmitCostWindow();
        }

        // R3 补充：GC 暂停时长（GetTotalPauseDuration 差值 → 每秒毫秒）。
        private double ComputeGcPauseMsPerSec(long windowMs)
        {
            var totalMs = GC.GetTotalPauseDuration().TotalMilliseconds;
            var delta = totalMs - _costLastGcPauseMs;
            _costLastGcPauseMs = totalMs;
            if (delta < 0) delta = 0;
            return windowMs > 0 ? delta * 1000.0 / windowMs : 0;
        }

        private void MaybeEmitCostWindow()
        {
            var now = Environment.TickCount64;
            var windowMs = now - _costWindowStartTicks;
            if (windowMs < 1000) return;
            _msiHostStageEvidence?.BeginCost();
            try
            {
                _costWindowStartTicks = now;
                if (_msiCostDeferredRequested)
                {
                    QueueMsiCostWindow(now, windowMs);
                    return;
                }
                _costFlagEnabled = CostLoggingRequestedByFlag();  // 窗口级刷新（≤1 次 IO/s）
                if (!_costFlagEnabled) ResetVirtualSignalEvidenceSafely();
                if (!_costFlagEnabled)
                {
                    _costCpuWindow?.Reset();
                    ResetCostWindow();
                    return;
                }
                try
                {
                    var count = _costSubmitUsCount;
                    var samples = new long[count];
                    Array.Copy(_costSubmitUsSamples, samples, count);
                    Array.Sort(samples);
                    long Percentile(double p) => count == 0
                        ? 0L
                        : samples[Math.Min(count - 1, (int)(count * p))];
                    using var proc = System.Diagnostics.Process.GetCurrentProcess();
                    // R3b″（2026-09-18 授权）：分配率（窗口增量；allocPerFrame 供"每帧
                    // 分配"对拍）+ 线程级 CPU top-4（含埋点自报角色；未标注 = 池/运行时线程）。
                    // GP-CPU2 P5（裁决：纠正统计口径）：byThread 只列最忙 4 条线程、不是进程总和 ⇒
                    // 增记**进程累计 CPU 增量**（procCpuMsPerSec）；byThread 保留作热点提示。
                    var cpuWindow = _costCpuWindow ??= new();
                    double? procCpuMs = null;
                    try { procCpuMs = proc.TotalProcessorTime.TotalMilliseconds; } catch { }
                    var processCpu = cpuWindow.ObserveProcess(procCpuMs, Environment.TickCount64);
                    var allocTotal = GC.GetTotalAllocatedBytes(precise: false);
                    var allocDelta = allocTotal - _costLastAllocBytes;
                    if (allocDelta < 0) allocDelta = 0;
                    _costLastAllocBytes = allocTotal;
                    var threadRows = new List<(int tid, long msPerSec, string role)>();
                    var observedThreads = proc.Threads;
                    var threadRatesKnown = 0;
                    var threadReadFailures = 0;
                    var threadBaselines = 0;
                    var threadOtherUnknown = 0;
                    cpuWindow.BeginThreadObservation();
                    try
                    {
                        foreach (System.Diagnostics.ProcessThread th in observedThreads)
                        {
                            using (th)
                            {
                                double? cpuMs = null;
                                try { cpuMs = th.TotalProcessorTime.TotalMilliseconds; } catch { }
                                var measurement = cpuWindow.ObserveThread(th.Id, cpuMs, Environment.TickCount64);
                                if (measurement.MsPerSec is long rate)
                                {
                                    threadRatesKnown++;
                                    if (rate > 0) threadRows.Add((th.Id, rate, RoleOfCostThread(th.Id)));
                                }
                                else if (measurement.Status == "read-unavailable") threadReadFailures++;
                                else if (measurement.Status == "baseline") threadBaselines++;
                                else threadOtherUnknown++;
                            }
                        }
                    }
                    finally { cpuWindow.FinishThreadObservation(); }
                    threadRows.Sort((x, y) => y.msPerSec.CompareTo(x.msPerSec));
                    var byThread = new List<object>();
                    for (var ti = 0; ti < threadRows.Count && ti < 4; ti++)
                        byThread.Add(new { tid = threadRows[ti].tid, cpuMsPerSec = threadRows[ti].msPerSec, role = threadRows[ti].role });
                    var line = JsonSerializer.Serialize(new
                    {
                        t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                        // Writer/session identity remains available when optional USBIP evidence is null.
                        writerPid = Environment.ProcessId, hostInstanceId = _hostInstanceId,
                        runId = _tuple?.RunId ?? "", epoch = _tuple?.Epoch ?? 0UL,
                        powerGeneration = _tuple?.PowerGeneration ?? 0UL,
                        windowMs,
                        frames = _costFrames,
                        submitElapsedUs = new
                        {
                            p50 = Percentile(0.50),
                            p95 = Percentile(0.95),
                            max = count == 0 ? 0L : samples[count - 1]
                        },
                        jsonParseUs = new
                        {
                            n = _costJsonParseCount,
                            mean = _costJsonParseCount == 0 ? 0L : _costJsonParseUsSum / _costJsonParseCount
                        },
                        hidSubmitUs = new
                        {
                            n = _costHidSubmitCount,
                            mean = _costHidSubmitCount == 0 ? 0L : _costHidSubmitUsSum / _costHidSubmitCount
                        },
                        pipeReadUs = new
                        {
                            n = _costPipeReadCount,
                            mean = _costPipeReadCount == 0 ? 0L : _costPipeReadUsSum / _costPipeReadCount
                        },
                        framesRejected = _costFramesRejected,
                        gcPauseMsPerSec = ComputeGcPauseMsPerSec(windowMs),
                        allocBytesPerSec = windowMs > 0 ? (long)(allocDelta * 1000.0 / windowMs) : 0,
                        // GP-CPU2 P5：进程累计 CPU（含所有线程；与 byThread 的 top-4 不同口径）。
                        procCpuMsPerSec = processCpu.MsPerSec,
                        cpuObservation = new
                        {
                            version = 1, state = processCpu.Status,
                            observationTick = processCpu.ObservationTick,
                            runtimeWindowMs = processCpu.WindowMs,
                            threadRatesKnown, threadReadFailures, threadBaselines, threadOtherUnknown
                        },
                        allocPerFrame = _costFrames > 0 ? allocDelta / _costFrames : 0,
                        hidhideQuery = new { full = _costHidHideQueryFull, state = _costHidHideQueryState },
                        byThread,
                        threads = observedThreads.Count,
                        logRetention = HostCostLog.Snapshot(),
                        gcHeapBytes = GC.GetTotalMemory(false),
                        gen0 = GC.CollectionCount(0),
                        gen1 = GC.CollectionCount(1),
                        gen2 = GC.CollectionCount(2),
                        phase = _phase.ToString(),
                        steamDeckPadSubmissions = _tuple?.Persona == "steamdeck" ? ScreenPadSubmissionEvidence.Capture() : null,
                        usbipTransport = CaptureUsbipTransportSafely("cost-window", _costFlagEnabled)
                    });
                    HostCostLog.Append(CostStateDir, line);
                }
                catch { _costCpuWindow?.Reset(); /* 诊断永不阻塞或改变控制路径 */ }
                ResetCostWindow();
            }
            finally
            {
                _msiHostStageEvidence?.EndCost();
            }
        }

        // Only bounded in-memory copies happen here. The worker never reads
        // HostSession, takes its lifecycle lock or writes a target/pipe response.
        private void QueueMsiCostWindow(long now, long windowMs)
        {
            try
            {
                var diagnostic = _msiCostDiagnostics;
                _costFlagEnabled = diagnostic?.LoggingEnabled ?? false;
                if (!_costFlagEnabled) ResetVirtualSignalEvidenceSafely();
                if (diagnostic is null) return;
                if (diagnostic.TryRejectBeforeSnapshot()) return;
                var samples = _costSubmitUsCount == 0 ? Array.Empty<long>() : new long[_costSubmitUsCount];
                Array.Copy(_costSubmitUsSamples, samples, samples.Length);
                Func<object?, bool, object?>? reader = null;
                WeakReference<object>? controller = null;
                if (_assetVerified && _tuple?.Persona == "steamdeck" && _controller is not null)
                {
                    var observer = _usbipTransportObservation ??= new UsbipTransportObservation();
                    reader = (target, verified) => observer.Capture(target, verified);
                    controller = new WeakReference<object>(_controller);
                }
                diagnostic.TryRecord(new(
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), now, windowMs,
                    _costFrames, samples, _costJsonParseUsSum, _costJsonParseCount,
                    _costHidSubmitUsSum, _costHidSubmitCount, _costPipeReadUsSum, _costPipeReadCount,
                    _costFramesRejected, _costHidHideQueryFull, _costHidHideQueryState,
                    _costRoleOsTid.ToArray(), _phase.ToString(), _hostInstanceId,
                    _tuple?.RunId, _tuple?.Epoch ?? 0UL, _tuple?.PowerGeneration ?? 0UL,
                    _tuple?.Persona, _tuple?.ProfileIdentity, _lastSequence,
                    diagnostic.ControllerGeneration, controller, reader, _assetVerified)
                {
                    SteamDeckPadSubmissions = _costFlagEnabled && _tuple?.Persona == "steamdeck"
                        ? ScreenPadSubmissionEvidence.Capture() : null
                });
            }
            catch { /* snapshot loss is diagnostic-only; never change frame success */ }
            finally { ResetCostWindow(); }
        }

        // R5: snapshot only at the existing cost window/lifecycle calls. Stock
        // SDK queues are sampled without waiting or changing their contents.
        // A snapshot is neither a wire receipt nor proof of external release.
        private object? CaptureUsbipTransportSafely(string phase, bool loggingRequested)
        {
            if (!loggingRequested || !_assetVerified || _tuple?.Persona != "steamdeck") return null;
            try
            {
                _usbipTransportObservation ??= new UsbipTransportObservation();
                return new
                {
                    version = 1, phase, capturedAtUtc = DateTimeOffset.UtcNow.ToString("O"),
                    hostPid = Environment.ProcessId, hostInstanceId = _hostInstanceId,
                    runId = _tuple.RunId, epoch = _tuple.Epoch, powerGeneration = _tuple.PowerGeneration,
                    persona = _tuple.Persona, profileIdentity = _tuple.ProfileIdentity,
                    lastHostCommandSequence = _lastSequence,
                    scope = "Sampled SDK managed state; no original request owner, wire, driver or Steam acceptance proof.",
                    snapshot = _usbipTransportObservation.Capture(_controller, _assetVerified)
                };
            }
            catch (Exception ex)
            {
                return new { version = 1, phase, state = "observation-error", error = ex.GetType().Name };
            }
        }

        private void LogUsbipTransportLifecycleSafely(string phase)
        {
            try
            {
                // Refresh only at these sparse lifecycle boundaries. Detailed-log
                // flag authority stays with Native; logging disabled means no reflection.
                var evidence = CaptureUsbipTransportSafely(phase, CostLoggingRequestedByFlag());
                if (evidence is null) return;
                var line = JsonSerializer.Serialize(new
                {
                    t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    kind = "usbip-transport-lifecycle", usbipTransport = evidence
                });
                if (Encoding.UTF8.GetByteCount(line) > 8192) return;
                HostCostLog.Append(CostStateDir, line);
            }
            catch { /* diagnostic failure never changes target create/neutral/dispose */ }
        }

        private void ResetCostWindow()
        {
            _costFrames = 0;
            _costSubmitUsCount = 0;
            _costJsonParseUsSum = 0;
            _costJsonParseCount = 0;
            _costHidSubmitUsSum = 0;
            _costHidSubmitCount = 0;
            _costPipeReadUsSum = 0;
            _costPipeReadCount = 0;
            _costFramesRejected = 0;
            _costHidHideQueryFull = 0;
            _costHidHideQueryState = 0;
        }

        private bool TrySubmitNeutral(out string detail, CommandEnvelope? diagnosticEnvelope = null, string diagnosticReason = "release-neutral")
        {
            if (_tuple is null || !TryGetVirtualOutputProfile(_tuple.Persona, _tuple.ProfileIdentity, out var profileName))
            {
                detail = "persona-not-admitted";
                return false;
            }
            if (_controller is null || _context is null)
            {
                detail = "target-not-ready";
                return false;
            }
            try
            {
                var profile = _context.GetProfile(profileName) ?? throw new InvalidOperationException("profile-missing:" + profileName);
                var neutral = new CommandEnvelope(
                    "SUBMIT_NEUTRAL", "neutral", 1, "neutral", 1, 1, "target", "coordinator",
                    _tuple.Persona, profileName, _startupNonce, 1, new string('0', 64));
                switch (neutral.Persona)
                {
                    case "steamdeck": SubmitSteamDeckWithScreenPads(_controller, profile, neutral, _costFlagEnabled, isNeutralCommand: true,
                        signalSession: this, signalCause: diagnosticEnvelope, signalReason: diagnosticReason); break;
                    case "elite": SubmitEliteReportChain1410(_controller, BuildEliteState(profile, neutral), diagnosticEnvelope, diagnosticReason, neutral); break;
                    case "dualsense-edge":
                    {
                        var frameIndex = _dualSenseFrameCounter++;
                        var report = BuildDualSenseDataOnlyReport(profile, neutral, frameIndex);
                        SubmitSonyReportChain1410(_controller, diagnosticEnvelope, report, frameIndex, diagnosticReason, neutral);
                        break;
                    }
                    default: throw new InvalidOperationException("persona-not-admitted");
                }
                _backendNeutralOk++;
                _lastBackendDetail = "neutral-report-submitted";
                detail = "neutral-report-submitted";
                return true;
            }
            catch (Exception ex)
            {
                _backendNeutralFail++;
                _lastBackendDetail = "neutral-exception:" + ex.GetType().Name;
                detail = "neutral-exception:" + ex.GetType().Name;
                return false;
            }
        }

        // GP-926R17 §4：后端 submit 聚合（HELLO 与每次 PREPARE/RECREATE 重置 ⇒ 只绑当前 tuple）。
        // The callback takes only the observation lock; it never takes the lifecycle
        // lock, performs pipe/device IO, decodes motors, or changes target ownership.
        private void BindGameOutputObservation(CommandEnvelope envelope)
        {
            var detachDetail = DetachGameOutputObservation();
            if (detachDetail.Length != 0) throw new InvalidOperationException(detachDetail);
            if (_controller is null || !_targetPnpAdmitted) return;
            var controller = _controller;
            var observation = _gameOutputObservation;
            var binding = observation.Bind(controller, envelope.ProfileIdentity);
            Action<HMController, HMOutputPacket> callback = (sender, packet) =>
                observation.Observe(binding, sender, (byte)packet.Source, packet.ReportId, packet.Data, packet.SeqNo);
            _gameOutputController = controller;
            _gameOutputCallback = callback;
            _gameOutputTuple = envelope;
            controller.OutputReceived += callback;
        }

        private string DetachGameOutputObservation()
        {
            // Invalidate first: an event invocation may already hold the old delegate.
            _gameOutputObservation.Revoke();
            var controller = _gameOutputController;
            var callback = _gameOutputCallback;
            _gameOutputTuple = null;
            if (controller is not null && callback is not null)
            {
                try { controller.OutputReceived -= callback; }
                catch (Exception ex)
                {
                    // Keep the exact delegate for retry. Observation authority is
                    // already revoked; SDK stop/disposal must still run on release.
                    return "game-output-detach:" + ex.GetType().Name;
                }
            }
            _gameOutputController = null;
            _gameOutputCallback = null;
            return "";
        }

        private string AppendGameOutputObservation(string response, CommandEnvelope? envelope, bool ok)
        {
            var bindingReply = envelope?.Command is "PREPARE_TARGET" or "RECREATE_TARGET";
            if ((!_gameOutputObservation.HasPending && !bindingReply) || !ok || envelope is null || _gameOutputTuple is null || _controller is null ||
                !ReferenceEquals(_controller, _gameOutputController) || !SameTuple(_gameOutputTuple, envelope) ||
                _phase is not (Phase.Prepared or Phase.Neutralized or Phase.Active)) return response;
            // Reserve space for the existing bounded optional evidence appenders.
            if (bindingReply) response = _gameOutputObservation.TryAppendBinding(response, _controller, MaxMessageBytes - 2048);
            return _gameOutputObservation.TryAppend(response, _controller, MaxMessageBytes - 2048,
                includeMotors: envelope.Command == "SUBMIT_FRAME");
        }

        internal void ResetBackendSubmitStats()
        {
            _backendSubmitOk = 0;
            _backendSubmitFail = 0;
            _backendNeutralOk = 0;
            _backendNeutralFail = 0;
            _lastBackendDetail = "";
            _lastBackendSequence = 0;
        }

        internal string ComposeBackendSubmitStatsDetail()
        {
            return JsonSerializer.Serialize(new
            {
                ok = true,
                runId = _tuple?.RunId ?? "",
                epoch = _tuple?.Epoch ?? 0,
                persona = _tuple?.Persona ?? "",
                frameOk = _backendSubmitOk,
                frameFail = _backendSubmitFail,
                neutralOk = _backendNeutralOk,
                neutralFail = _backendNeutralFail,
                lastDetail = _lastBackendDetail,
                lastSequence = _lastBackendSequence
            });
        }

        private string Ack(CommandEnvelope envelope, string status, string detail = "", bool? firstFrame = null, string? releaseDisposition = null) =>
            SerializeResponse(envelope, true, status, detail, firstFrame, releaseDisposition);

        private string Error(CommandEnvelope? envelope, string status, string detail, string? releaseDisposition = null)
        {
            // L1（2026-09-11 十二批次，审计补课）：所有拒绝路径（parseError /
            // unknown-field / sequence-stale-or-gap / tuple-mismatch / 状态拒绝）
            // 在返回前写一行文件日志，native 侧 tuple-mismatch 审计可直接对读。
            // 文件与 native 日志同目录：%LOCALAPPDATA%\YeManCC\input-host-rejects.log。
            try
            {
                var line = JsonSerializer.Serialize(new
                {
                    t = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    command = envelope?.Command ?? "",
                    requestId = envelope?.RequestId ?? "",
                    status,
                    detail,
                    protocolError = status == "protocol-error" || status.StartsWith("protocol-", StringComparison.Ordinal)
                });
                File.AppendAllText(LogRejectsPath, line + Environment.NewLine, System.Text.Encoding.UTF8);
            }
            catch
            {
                // 日志写入失败绝不能影响命令处理路径。
            }
            return SerializeResponse(envelope, false, status, detail, null, releaseDisposition);
        }

        private static string LogRejectsPath
        {
            get
            {
                var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");
                try { Directory.CreateDirectory(dir); } catch { }
                return Path.Combine(dir, "input-host-rejects.log");
            }
        }

        private PsEndpointOwnership.Binding PsOwnershipBinding(CommandEnvelope envelope)
        {
            return new(_hostInstanceId, envelope.RunId, envelope.Epoch, envelope.TargetId, envelope.Persona);
        }

        private void CapturePsEndpointOwnership()
        {
            if (_tuple?.Persona == PsEndpointOwnership.Persona)
                _psEndpointOwnership.Capture(_controller, _assetVerified, PsOwnershipCmReader.Instance);
        }

        private string AppendPsEndpointOwnership(string response, CommandEnvelope? envelope)
        {
            if (envelope?.Persona != PsEndpointOwnership.Persona || envelope.Command is not
                ("PREPARE_TARGET" or "RECREATE_TARGET" or "QUERY_TARGET_OWNERSHIP")) return response;
            return _psEndpointOwnership.Append(response, PsOwnershipBinding(envelope), MaxMessageBytes);
        }

        private string SerializeResponse(CommandEnvelope? envelope, bool ok, string status, string detail, bool? firstFrame, string? releaseDisposition)
        {
            var response = JsonSerializer.Serialize(new HostResponseFields(
                ProtocolVersion,
                ok,
                envelope?.Command ?? "",
                envelope?.RequestId ?? "",
                envelope?.Sequence ?? 0UL,
                _hostInstanceId,
                envelope?.RunId ?? "",
                envelope?.Epoch ?? 0UL,
                envelope?.PowerGeneration ?? 0UL,
                envelope?.TargetId ?? "",
                envelope?.OwnerIdentity ?? "",
                envelope?.Persona ?? "",
                envelope?.ProfileIdentity ?? "",
                envelope?.StartupNonce ?? "",
                envelope?.ConfigRevision ?? 0UL,
                envelope?.ConfigHash ?? "",
                status,
                detail,
                _assetVerified,
                LockedCoreSha256,
                LockedCoreMvid,
                _virtualEndpointReconciled,
                _targetPnpAdmitted,
                _virtualEndpoint.PrimaryRootCount + _virtualEndpoint.XboxCompanionRootCount,
                _virtualEndpoint.PrimaryRootCount,
                _virtualEndpoint.XboxCompanionRootCount,
                _virtualEndpoint.HidChildCount,
                _virtualEndpoint.AllStarted,
                _virtualEndpoint.Detail,
                firstFrame,
                releaseDisposition,
                _lastSubmitElapsedUs));
            response = AppendGameOutputObservation(response, envelope, ok);
            if (envelope is not null && envelope.Command is ("PREPARE_TARGET" or "RECREATE_TARGET" or "RELEASE_TARGET" or "SHUTDOWN") &&
                _msiSteamDeckReleaseFence1726?.Evidence is not null && _msiSteamDeckReleaseFence1726.Matches(envelope))
                response = response[..^1] + ",\"msiSteamDeckReleaseEvidence\":" +
                    JsonSerializer.Serialize(_msiSteamDeckReleaseFence1726.Evidence) + "}";
            if (envelope is not null && envelope.Command is ("RELEASE_TARGET" or "SHUTDOWN") &&
                _msiNonSdReleaseDiagnosticBinding is { } diagnosticBinding && SameTuple(diagnosticBinding, envelope))
                response = _msiNonSdReleaseDiagnostic?.TryAppend(response, MaxMessageBytes) ?? response;
            if (envelope is not null)
                response = _targetCreationLifetime?.TryAppend(response, envelope.Command, MaxMessageBytes) ?? response;
            if (envelope?.Command is ("RELEASE_TARGET" or "SHUTDOWN") &&
                _releaseTimingBinding is { } releaseBinding && SameTuple(releaseBinding, envelope) &&
                releaseBinding.Command == envelope.Command && releaseBinding.RequestId == envelope.RequestId &&
                releaseBinding.Sequence == envelope.Sequence)
                response = _releaseTimingEvidence?.TryAppend(response, MaxMessageBytes) ?? response;
            try
            {
                if (ok && envelope?.Command == "HELLO" && status == "host-hello")
                {
                    var capable = response[..^1] + ",\"msiReportChainDiagnosticVersion\":" + MsiReportChainProtocol1410.DiagnosticVersion + ",\"msiSteamDeckReleaseFenceVersion\":1,\"psEndpointOwnershipVersion\":1}";
                    if (_msiNonSdReleaseDiagnosticRequested) capable = capable[..^1] + ",\"msiNonSdReleaseEvidenceVersion\":1}";
                    capable = HostRuntimeBinaryIdentity.AppendToHello(capable, _runtimeBinaryIdentity, MaxMessageBytes);
                    _reportChainCapabilityAdvertised1410 = true;
                    return capable;
                }
                if (envelope?.Command == "PREPARE_TARGET" && envelope.ReportChainPrepareSpecified1410)
                {
                    var armed = ok && status == "target-prepared" && _reportChain1410?.Armed == true;
                    var suffix = ",\"msiReportChainDiagnosticArmed\":" + (armed ? "true" : "false");
                    if (armed) suffix += ",\"msiReportChainDiagnosticSessionId\":" + JsonSerializer.Serialize(_reportChain1410!.SessionId);
                    return AppendPsEndpointOwnership(response[..^1] + suffix + "}", envelope);
                }
            }
            catch { }
            return AppendPsEndpointOwnership(response, envelope);
        }

        private static bool TryVerifyLockedAsset(out string detail)
        {
            try
            {
                var assembly = typeof(HMContext).Assembly;
                var path = assembly.Location;
                if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) { detail = "core-location-missing"; return false; }
                var file = new FileInfo(path);
                if (file.Length != LockedCoreBytes) { detail = "core-bytes-mismatch"; return false; }
                using var stream = File.OpenRead(path);
                var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
                if (hash != LockedCoreSha256) { detail = "core-sha256-mismatch"; return false; }
                if (assembly.ManifestModule.ModuleVersionId.ToString("D") != LockedCoreMvid) { detail = "core-mvid-mismatch"; return false; }
                detail = "locked-core-verified";
                return true;
            }
            catch (Exception ex)
            {
                detail = "core-identity-exception:" + ex.GetType().Name;
                return false;
            }
        }
    }

    // GP-CPU2 P1：字段表**按线程复用**（原实现每条消息新建 Dictionary）。解析是同步、
    // 单消费者调用（会话锁内），ThreadStatic 足够且不跨会话串用；判据与优先级逐字不变。
    [ThreadStatic] private static Dictionary<string, JsonElement>? t_fieldCache;

    private static bool TryGetVirtualOutputProfile(string persona, string profileIdentity, out string profileName)
    {
        profileName = persona switch
        {
            "steamdeck" => "steam-deck-composite",
            "dualsense-edge" => "dualsense-edge",
            "elite" => "xbox-elite-v2",
            _ => ""
        };
        return profileName.Length != 0 && profileIdentity == profileName;
    }

    private static bool TryParseEnvelope(string message, out CommandEnvelope? envelope, out string error)
    {
        envelope = null;
        error = "invalid-json";
        if (string.IsNullOrWhiteSpace(message) || (message.Length > MaxMessageBytes / 3 && Encoding.UTF8.GetByteCount(message) > MaxMessageBytes)) return false;
        try
        {
            using var document = JsonDocument.Parse(message, new JsonDocumentOptions { AllowTrailingCommas = false, CommentHandling = JsonCommentHandling.Disallow });
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) { error = "root-not-object"; return false; }
            // GP-CPU2 P1（裁决：保守方案——保留 JsonDocument，只优化合法帧路径的字典/LINQ）。
            // 原实现每条消息新建 Dictionary + `properties.Keys.Any(...)` LINQ。改为**每会话复用**字典
            // （Clear 后重建）+ 等价普通循环。判据与优先级**逐字不变**：
            //   duplicate-field 仍在此处按同一顺序失败；unknown-field 仍在（且只在）
            //   duplicate → invalid-command → unknown-command 之后判定，**绝不提前拒绝**
            //   （裁决方实跑确认：提前拒绝会改变反例的判定优先级）。
            var properties = t_fieldCache ??= new Dictionary<string, JsonElement>(StringComparer.Ordinal);
            properties.Clear();
            var prepareMetadataDuplicate1410 = false;
            var sourceMetadataDuplicate1410 = false;
            foreach (var property in root.EnumerateObject())
            {
                if (!properties.TryAdd(property.Name, property.Value))
                {
                    if (property.Name == MsiReportChainProtocol1410.PrepareField) { prepareMetadataDuplicate1410 = true; continue; }
                    if (property.Name == MsiReportChainProtocol1410.SourceField) { sourceMetadataDuplicate1410 = true; continue; }
                    error = "duplicate-field:" + property.Name; return false;
                }
            }
            if (!TryReadString(properties, "command", 32, out var command)) { error = "invalid-command"; return false; }
            if (command is not ("HELLO" or "PREPARE_TARGET" or "RECREATE_TARGET" or "SUBMIT_NEUTRAL" or "SUBMIT_FRAME"
                // F3-B：KEEPALIVE 已从协议白名单移除（第八十一批 B-变体）。
                // GP-926R17 §4：只读聚合回执命令（绑当前 tuple；native 唤醒/借用结算低频调用）。
                or "TUPLE_SUBMIT_STATS" or "QUERY_TARGET_OWNERSHIP"
                or "QUIESCE" or "RELEASE_TARGET" or "SHUTDOWN"
                or "HIDHIDE_HIDE" or "HIDHIDE_UNHIDE" or "HIDHIDE_CLOAK" or "HIDHIDE_APP_REG" or "HIDHIDE_APP_UNREG" or "HIDHIDE_QUERY" or "HIDHIDE_QUERY_STATE" or "HIDHIDE_CYCLE"
                // GP-GBRETURN-1：关闭归还专用（短受理/有界查询）——只在关闭刷新用，不是第二写者。
                or "HIDHIDE_CYCLE_BEGIN" or "HIDHIDE_CYCLE_POLL"))
            {
                error = "unknown-command";
                return false;
            }
            var allowed = command == "SUBMIT_FRAME" ? FrameFields :
                (command.StartsWith("HIDHIDE_", StringComparison.Ordinal) ? HidHideFields : BaseFields);
            var unknownFieldSeen = false;
            foreach (var key in properties.Keys)
            {
                if (!allowed.Contains(key) && !MsiReportChainProtocol1410.IsMetadataField(key) && key != MsiSteamDeckReleaseFence1726.PrepareField) { unknownFieldSeen = true; break; }
            }
            if (unknownFieldSeen) { error = "unknown-field"; return false; }
            if (!TryReadUInt64(properties, "protocolVersion", out var version) || version != ProtocolVersion) { error = "protocol-version"; return false; }
            if (!TryReadString(properties, "requestId", 160, out var requestId) ||
                !TryReadUInt64(properties, "sequence", out var sequence) || sequence == 0 ||
                !TryReadString(properties, "runId", 160, out var runId) ||
                !TryReadUInt64(properties, "epoch", out var epoch) || epoch == 0 ||
                !TryReadUInt64(properties, "powerGeneration", out var powerGeneration) || powerGeneration == 0 ||
                !TryReadString(properties, "targetId", 160, out var targetId) ||
                !TryReadString(properties, "ownerIdentity", 160, out var ownerIdentity) ||
                !TryReadString(properties, "persona", 64, out var persona) ||
                !TryReadString(properties, "profileIdentity", 96, out var profileIdentity) ||
                !TryReadString(properties, "startupNonce", 160, out var startupNonce) ||
                !TryReadUInt64(properties, "configRevision", out var configRevision) || configRevision == 0 ||
                !TryReadString(properties, "configHash", 64, out var configHash) || !IsSha256(configHash))
            {
                error = "invalid-base-field";
                return false;
            }

            if (!TryGetVirtualOutputProfile(persona, profileIdentity, out _))
            {
                error = "persona-not-admitted";
                return false;
            }

            var parsed = new CommandEnvelope(command, requestId, sequence, runId, epoch, powerGeneration, targetId, ownerIdentity, persona, profileIdentity, startupNonce, configRevision, configHash);
            if (properties.TryGetValue(MsiSteamDeckReleaseFence1726.PrepareField, out var releaseFence))
            {
                if (command != "PREPARE_TARGET" || persona != "steamdeck" || profileIdentity != "steam-deck-composite" ||
                    releaseFence.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
                {
                    error = "invalid-msi-steamdeck-release-fence";
                    return false;
                }
                parsed = parsed with { MsiSteamDeckReleaseFence = releaseFence.GetBoolean() };
            }
            var physicalBaseContainerInstanceId = "";
            var physicalDeviceInstanceId = "";
            if (properties.ContainsKey("physicalBaseContainerInstanceId") &&
                !TryReadString(properties, "physicalBaseContainerInstanceId", 160, out physicalBaseContainerInstanceId))
            {
                error = "invalid-physical-base-instance-id";
                return false;
            }
            if (properties.ContainsKey("physicalDeviceInstanceId") &&
                !TryReadString(properties, "physicalDeviceInstanceId", 160, out physicalDeviceInstanceId))
            {
                error = "invalid-physical-device-instance-id";
                return false;
            }
            // Optional physical/HidHide fields keep their validated values.
            // With no such values the fresh base record already has these defaults.
            if (physicalBaseContainerInstanceId.Length != 0 || physicalDeviceInstanceId.Length != 0 ||
                command.StartsWith("HIDHIDE_", StringComparison.Ordinal))
            {
                parsed = parsed with
                {
                    PhysicalBaseContainerInstanceId = properties.TryGetValue("physicalBaseContainerInstanceId", out _)
                        ? physicalBaseContainerInstanceId : "",
                    PhysicalDeviceInstanceId = properties.TryGetValue("physicalDeviceInstanceId", out _)
                        ? physicalDeviceInstanceId : "",
                    HidHidePath = properties.TryGetValue("path", out _)
                        ? (properties["path"].ValueKind == JsonValueKind.String ? properties["path"].GetString() ?? "" : "")
                        : "",
                    HidHideOn = properties.TryGetValue("on", out _)
                        ? properties["on"].ValueKind == JsonValueKind.True : false,
                    HidHideCloak = properties.TryGetValue("cloak", out _)
                        ? properties["cloak"].ValueKind == JsonValueKind.True : false
                };
            }
            if (command == "SUBMIT_FRAME")
            {
                if (!TryReadInt32(properties, "buttons", out var buttons) || buttons < 0 || buttons > 0xffff ||
                    !TryReadAxis(properties, "lx", out var lx) || !TryReadAxis(properties, "ly", out var ly) ||
                    !TryReadAxis(properties, "rx", out var rx) || !TryReadAxis(properties, "ry", out var ry) ||
                    !TryReadTrigger(properties, "lt", out var lt) || !TryReadTrigger(properties, "rt", out var rt) ||
                    !TryReadUInt64(properties, "sampleSequence", out var sampleSequence) || sampleSequence == 0 ||
                    !TryReadUInt64(properties, "monotonicTick", out var monotonicTick) || monotonicTick == 0 ||
                    !TryReadString(properties, "normalizedInputHash", 64, out var normalizedInputHash) || !IsSha256(normalizedInputHash) ||
                    !TryReadString(properties, "motionPairProofId", 160, out var motionPairProofId))
                {
                    error = "invalid-frame";
                    return false;
                }
                if (normalizedInputHash != CanonicalFrameHash(buttons, lx, ly, rx, ry, lt, rt))
                {
                    error = "normalized-input-hash-mismatch";
                    return false;
                }
                // G8/HC parity: raw pre-canonicalized triggers, falling back to
                // the canonical value when a legacy coordinator omits them.
                var ltRaw = lt;
                var rtRaw = rt;
                if ((properties.ContainsKey("ltRaw") && !TryReadTrigger(properties, "ltRaw", out ltRaw)) ||
                    (properties.ContainsKey("rtRaw") && !TryReadTrigger(properties, "rtRaw", out rtRaw)))
                {
                    error = "invalid-frame";
                    return false;
                }
                // 7.52 P1-T1：raw 域（原生 SHORT/BYTE）可选解析；缺省
                // HasRawDomain=false，编码链回退 canonical float 域（Q1）。
                var hasRaw = properties.TryGetValue("raw", out var rawElement) &&
                             rawElement.ValueKind == JsonValueKind.Object;
                var rawButtons = buttons;
                var rawLx = (short)0; var rawLy = (short)0;
                var rawRx = (short)0; var rawRy = (short)0;
                byte rawLt = 0; byte rawRt = 0;
                if (hasRaw)
                {
                    if (rawElement.TryGetProperty("buttons", out var rawButtonsEl) &&
                        rawButtonsEl.ValueKind == JsonValueKind.Number)
                        rawButtons = rawButtonsEl.GetInt32();
                    TryReadShort(rawElement, "sThumbLX", out rawLx);
                    TryReadShort(rawElement, "sThumbLY", out rawLy);
                    TryReadShort(rawElement, "sThumbRX", out rawRx);
                    TryReadShort(rawElement, "sThumbRY", out rawRy);
                    TryReadByte(rawElement, "bLeftTrigger", out rawLt);
                    TryReadByte(rawElement, "bRightTrigger", out rawRt);
                }
                // SteamDeck IMU（G1/G3）：可选解析，缺省 fail-zero。
                var imuAdmitted = false;
                var imuGyroX = 0.0; var imuGyroY = 0.0; var imuGyroZ = 0.0;
                var imuAccelX = 0.0; var imuAccelY = 0.0; var imuAccelZ = 0.0;
                var imuQuatW = 1.0; var imuQuatX = 0.0; var imuQuatY = 0.0; var imuQuatZ = 0.0;
                var imuQuaternionPresent = false;
                if (properties.TryGetValue("imu", out var imuElement) && imuElement.ValueKind == JsonValueKind.Object)
                {
                    if (imuElement.TryGetProperty("admitted", out var admittedEl) && admittedEl.ValueKind == JsonValueKind.True)
                        imuAdmitted = true;
                    TryReadImuAxis(imuElement, "gyroDps", ref imuGyroX, ref imuGyroY, ref imuGyroZ);
                    TryReadImuAxis(imuElement, "accelG", ref imuAccelX, ref imuAccelY, ref imuAccelZ);
                    imuQuaternionPresent = TryReadImuQuaternion(imuElement, out imuQuatW, out imuQuatX, out imuQuatY, out imuQuatZ);
                }
                // 背键映射 Phase A（背键映射A任务书 2026-09-14 S4）：可选字段，
                // 缺省 0（老 native 不发→零污染）；clamp 0..15（bit0=M1、bit1=M2）。
                var backButtons = 0;
                if (properties.TryGetValue("backButtons", out var backEl) &&
                    backEl.ValueKind == JsonValueKind.Number && backEl.TryGetInt32(out var backRaw))
                    backButtons = Math.Clamp(backRaw, 0, 15);
                var screenButtons=0;
                if(properties.TryGetValue("screenButtons",out var screenButtonsElement) &&
                    !TryReadScreenButtons(screenButtonsElement,persona,out screenButtons))
                {error="invalid-screen-buttons";return false;}
                var padsPresent = properties.TryGetValue("touchpads", out var padsElement);
                ScreenPad padLeft = default, padRight = default;
                if (padsPresent && ((persona is not ("steamdeck" or "dualsense-edge")) || !TryReadScreenPads(padsElement, out padLeft, out padRight)))
                { error = "invalid-touchpads"; return false; }
                parsed = parsed with
                {
                    ScreenButtons=screenButtons,
                    ScreenPadsPresent = padsPresent, ScreenPadLeft = padLeft, ScreenPadRight = padRight,
                    Buttons = buttons, Lx = lx, Ly = ly, Rx = rx, Ry = ry, Lt = lt, Rt = rt,
                    LtRaw = ltRaw, RtRaw = rtRaw,
                    RawButtons = rawButtons,
                    RawThumbLX = rawLx, RawThumbLY = rawLy,
                    RawThumbRX = rawRx, RawThumbRY = rawRy,
                    RawLeftTrigger = rawLt, RawRightTrigger = rawRt,
                    HasRawDomain = hasRaw,
                    SampleSequence = sampleSequence, MonotonicTick = monotonicTick,
                    NormalizedInputHash = normalizedInputHash, MotionPairProofId = motionPairProofId,
                    ImuAdmitted = imuAdmitted,
                    ImuGyroX = imuGyroX, ImuGyroY = imuGyroY, ImuGyroZ = imuGyroZ,
                    ImuAccelX = imuAccelX, ImuAccelY = imuAccelY, ImuAccelZ = imuAccelZ,
                    ImuQuatW = imuQuatW, ImuQuatX = imuQuatX, ImuQuatY = imuQuatY, ImuQuatZ = imuQuatZ,
                    ImuQuaternionPresent = imuQuaternionPresent,
                    BackButtons = backButtons,
                };
            }
            envelope = AttachReportChainMetadata1410Safely(parsed, properties, prepareMetadataDuplicate1410, sourceMetadataDuplicate1410);
            return true;
        }
        catch (JsonException)
        {
            error = "invalid-json";
            return false;
        }
    }

    private static CommandEnvelope AttachReportChainMetadata1410Safely(CommandEnvelope parsed,
        IReadOnlyDictionary<string, JsonElement> properties, bool prepareDuplicate, bool sourceDuplicate)
    {
        try
        {
            var prepareSpecified = parsed.Command == "PREPARE_TARGET" && properties.TryGetValue(MsiReportChainProtocol1410.PrepareField, out _);
            MsiReportChainPrepare1410? prepare = null; MsiReportChainSource1410? source = null;
            if (prepareSpecified && !prepareDuplicate && properties.TryGetValue(MsiReportChainProtocol1410.PrepareField, out var p))
                prepare = MsiReportChainProtocol1410.ParsePrepareSafely(p, parsed.Command, parsed.RunId, parsed.Epoch, parsed.PowerGeneration);
            if (!sourceDuplicate && properties.TryGetValue(MsiReportChainProtocol1410.SourceField, out var f))
                source = MsiReportChainProtocol1410.ParseSourceSafely(f, parsed.Command);
            if (!prepareSpecified && prepare is null && source is null) return parsed;
            return parsed with { ReportChainPrepareSpecified1410 = prepareSpecified, ReportChainPrepare1410 = prepare, ReportChainSource1410 = source };
        }
        catch { return parsed; }
    }

    private static bool TryReadString(IReadOnlyDictionary<string, JsonElement> properties, string name, int maxLength, out string value)
    {
        value = "";
        if (!properties.TryGetValue(name, out var element) || element.ValueKind != JsonValueKind.String) return false;
        value = element.GetString() ?? "";
        if (value.Length is not (> 0 and <= 160) || value.Length > maxLength) return false;
        for (var i = 0; i < value.Length; i++)
            if (char.IsControl(value[i])) return false;
        return true;
    }

    private static bool TryReadUInt64(IReadOnlyDictionary<string, JsonElement> properties, string name, out ulong value)
    {
        value = 0;
        return properties.TryGetValue(name, out var element) && element.ValueKind == JsonValueKind.Number && element.TryGetUInt64(out value);
    }

    private static bool TryReadInt32(IReadOnlyDictionary<string, JsonElement> properties, string name, out int value)
    {
        value = 0;
        return properties.TryGetValue(name, out var element) && element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out value);
    }

    // 7.52 P1-T1（raw 域）：原生 SHORT 轴值（-32768..32767，HC raw 透传域）。
    // 从 frame.raw 对象（JsonElement）读取；缺字段失败。
    private static bool TryReadShort(JsonElement source, string name, out short value)
    {
        value = 0;
        if (!source.TryGetProperty(name, out var element) || element.ValueKind != JsonValueKind.Number ||
            !element.TryGetInt32(out var raw))
            return false;
        value = (short)Math.Clamp(raw, short.MinValue, short.MaxValue);
        return true;
    }

    // 7.52 P1-T1（raw 域）：原生 BYTE 扳机值（0..255，HC raw 透传域）。
    private static bool TryReadByte(JsonElement source, string name, out byte value)
    {
        value = 0;
        if (!source.TryGetProperty(name, out var element) || element.ValueKind != JsonValueKind.Number ||
            !element.TryGetInt32(out var raw))
            return false;
        value = (byte)Math.Clamp(raw, 0, byte.MaxValue);
        return true;
    }

    private static bool TryReadAxis(IReadOnlyDictionary<string, JsonElement> properties, string name, out float value)
    {
        value = 0;
        return properties.TryGetValue(name, out var element) && element.ValueKind == JsonValueKind.Number &&
               element.TryGetSingle(out value) && float.IsFinite(value) &&
               !(value == 0f && element.GetRawText().StartsWith("-", StringComparison.Ordinal)) &&
               value is >= -1f and <= 1f;
    }

    private static bool TryReadTrigger(IReadOnlyDictionary<string, JsonElement> properties, string name, out float value)
    {
        value = 0;
        return properties.TryGetValue(name, out var element) && element.ValueKind == JsonValueKind.Number &&
               element.TryGetSingle(out value) && float.IsFinite(value) &&
               !(value == 0f && element.GetRawText().StartsWith("-", StringComparison.Ordinal)) &&
               value is >= 0f and <= 1f;
    }

    // SteamDeck IMU（G1/G3）：解析 frame 内 imu.{axis}.{x,y,z} 三元组。
    private static void TryReadImuAxis(JsonElement imu, string axisName,
        ref double x, ref double y, ref double z)
    {
        if (!imu.TryGetProperty(axisName, out var axis) || axis.ValueKind != JsonValueKind.Object)
            return;
        if (axis.TryGetProperty("x", out var ex) && ex.ValueKind == JsonValueKind.Number) x = ex.GetDouble();
        if (axis.TryGetProperty("y", out var ey) && ey.ValueKind == JsonValueKind.Number) y = ey.GetDouble();
        if (axis.TryGetProperty("z", out var ez) && ez.ValueKind == JsonValueKind.Number) z = ez.GetDouble();
    }

    private static bool TryReadImuQuaternion(JsonElement imu, out double w, out double x, out double y, out double z)
    {
        w=1.0; x=y=z=0.0;
        if (!imu.TryGetProperty("quaternion", out var q) || q.ValueKind != JsonValueKind.Object) return false;
        static bool Component(JsonElement q, string key, out double value)
        {
            value=0;
            return q.TryGetProperty(key,out var e) && e.ValueKind==JsonValueKind.Number &&
                e.TryGetDouble(out value) && double.IsFinite(value);
        }
        if (!Component(q,"w",out var qw) || !Component(q,"x",out var qx) ||
            !Component(q,"y",out var qy) || !Component(q,"z",out var qz)) return false;
        w=qw; x=qx; y=qy; z=qz;
        return true;
    }

    private static bool IsSha256(string value)
    {
        // Keep the length/null gate and the runtime hex predicate unchanged.
        // Scan in order without allocating a LINQ string enumerator.
        if (value.Length != 64) return false;
        for (var i = 0; i < value.Length; i++)
        {
            if (!Uri.IsHexDigit(value[i])) return false;
        }
        return true;
    }

    private static string CanonicalFrameHash(int buttons, float lx, float ly, float rx, float ry, float lt, float rt)
    {
        // Keep the existing decimal-buttons + six uppercase IEEE-754 X8 words.
        // Write the same UTF-8 bytes directly; do not normalize signed zero or
        // quantize input. Byte-identical frames reuse only a successful hash.
        // 256 also covers the legacy 79-char limit with a non-ASCII minus sign.
        Span<byte> canonical = stackalloc byte[256];
        if (!buttons.TryFormat(canonical, out var count))
            throw new InvalidOperationException("canonical-frame-buttons-format-failed");
        AppendFrameHashBits(canonical, ref count, lx);
        AppendFrameHashBits(canonical, ref count, ly);
        AppendFrameHashBits(canonical, ref count, rx);
        AppendFrameHashBits(canonical, ref count, ry);
        AppendFrameHashBits(canonical, ref count, lt);
        AppendFrameHashBits(canonical, ref count, rt);
        return CanonicalFrameFingerprintMemo.GetOrCompute(canonical[..count]);
    }

    private static void AppendFrameHashBits(Span<byte> destination, ref int count, float value)
    {
        const string hex = "0123456789ABCDEF";
        destination[count++] = (byte)'|';
        var bits = unchecked((uint)BitConverter.SingleToInt32Bits(value));
        for (var shift = 28; shift >= 0; shift -= 4)
            destination[count++] = (byte)hex[(int)((bits >> shift) & 0xf)];
    }

    private static bool SameTuple(CommandEnvelope left, CommandEnvelope right) =>
        left.RunId == right.RunId && left.Epoch == right.Epoch && left.PowerGeneration == right.PowerGeneration &&
        left.TargetId == right.TargetId && left.OwnerIdentity == right.OwnerIdentity && left.Persona == right.Persona &&
        left.ProfileIdentity == right.ProfileIdentity && left.StartupNonce == right.StartupNonce &&
        left.ConfigRevision == right.ConfigRevision && left.ConfigHash == right.ConfigHash;



    // Shared signed-16 motion quantization for the current report builders.
    // A signed-16 report saturates at its endpoints. Preserve each target's
    // existing in-range rounding/truncation; never wrap a positive IMU value.
    private static short QuantizeMotionAxis(double value, double scale, bool roundToNearest = false)
    {
        if (!double.IsFinite(value)) return 0;
        var counts = value * scale;
        if (counts >= short.MaxValue) return short.MaxValue;
        if (counts <= short.MinValue) return short.MinValue;
        return (short)(roundToNearest ? Math.Round(counts) : counts);
    }

    private static void WriteSonyImuLe16(byte[] data, int index, double raw, double scale, double clampAbs)
    {
        var value = QuantizeMotionAxis(Math.Clamp(raw, -clampAbs, clampAbs), scale, roundToNearest: true);
        data[index] = (byte)(value & 0xFF);
        data[index + 1] = (byte)((value >> 8) & 0xFF);
    }

    // Edge uses the current 63-byte Sony data-only layout; the driver prepends 0x01.
    // Sticks 0..3, triggers 4..5, sequence 6, buttons 7..10, gyro 15..20,
    // accel 21..26, timestamp 27..30, contacts 32..39, status 52..54.
    private static byte[] BuildDualSenseDataOnlyReport(HMProfile profile, CommandEnvelope envelope, long frameIndex,
        int edgePaddles = 0)
    {
        if (profile.InputReportSize != 64)
            throw new InvalidOperationException("dualsense-wire-size-mismatch:" + profile.InputReportSize);
        var data = new byte[63];
        data[0] = AxisToByte(envelope.Lx);
        data[1] = AxisToByte(-envelope.Ly);
        data[2] = AxisToByte(envelope.Rx);
        data[3] = AxisToByte(-envelope.Ry);
        data[4] = TriggerToByte(envelope.LtRaw);   // wire 5 = z (L2)
        data[5] = TriggerToByte(envelope.RtRaw);   // wire 6 = rz (R2)
        // seq_number（wire 7）：真实 DualSense 每帧递增；写死会让部分消费者把连续
        // 帧视为同一包。
        data[6] = (byte)(frameIndex & 0xFF);
        data[7] = (byte)(MapHatOctant(envelope.Buttons) | MapFaceButtons(envelope.Buttons));
        data[8] = MapDigitalButtons(envelope.Buttons, envelope.LtRaw > 0f, envelope.RtRaw > 0f);
        // buttons[2]（wire 10）：PS(bit0) + 触摸板(bit1) + Mute(bit2)。产品当前无
        // 屏幕单触摸板 click 来源；不映射 Mute。
        data[9] = (byte)((((envelope.Buttons & 0x0400) != 0 || (envelope.ScreenButtons&1)!=0) ? 0x01 : 0x00) | (edgePaddles & 0xF0));
        if((envelope.ScreenButtons&2)!=0)data[9]|=0x04; // Real DualSense Mute.
        if(envelope.Persona=="dualsense-edge") {
            if((envelope.ScreenButtons&4)!=0)data[9]|=0x10; // LFn
            if((envelope.ScreenButtons&16)!=0)data[9]|=0x20; // RFn
            if((envelope.ScreenButtons&8)!=0)data[9]|=0x40; // LB
            if((envelope.ScreenButtons&32)!=0)data[9]|=0x80; // RB
        }
        if (envelope.ImuAdmitted)
        {
            WriteSonyImuLe16(data, 15, envelope.ImuGyroX, 16.0, 2048.0);
            WriteSonyImuLe16(data, 17, envelope.ImuGyroY, 16.0, 2048.0);
            WriteSonyImuLe16(data, 19, envelope.ImuGyroZ, 16.0, 2048.0);
            WriteSonyImuLe16(data, 21, envelope.ImuAccelX, 8192.0, 4.0);
            WriteSonyImuLe16(data, 23, envelope.ImuAccelY, 8192.0, 4.0);
            WriteSonyImuLe16(data, 25, envelope.ImuAccelZ, 8192.0, 4.0);
        }
        // sensor_timestamp（wire 28-31 = data[27..30]，uint32 LE，单位 0.333µs）：真机 250Hz
        // 下为 12012 单位/4ms；本产品节拍是 8ms（HC 默认档），要表达相同的"经过时间"
        // 须按单位速率折算 = 3,000,000/s × 0.008 = 24024 单位/帧；单调递增（24 位回绕）。
        var ts = (uint)((frameIndex & 0xFFFFFFL) * 24024L);
        data[27] = (byte)(ts & 0xFF);
        data[28] = (byte)((ts >> 8) & 0xFF);
        data[29] = (byte)((ts >> 16) & 0xFF);
        data[30] = (byte)((ts >> 24) & 0xFF);
        // 触摸点 0/1（wire 33-36 / 37-40 = data[32..35] / [36..39]）：contact 字节最高
        // 位 = 无效触点（DS_TOUCH_POINT_INACTIVE）；全 0 会被消费
        // 者读成"在 (0,0) 的活跃触点"。
        WriteDualSenseScreenPads(data,envelope);

        // status[0]（wire 53 = data[52]）低半字节 = 电量 0..10、bit4 = 有线位。
        // 保留已批准的满电、无有线位状态，避免 Steam
        // 显示"电量不足 5%"。
        data[52] = SonyWireBatteryFull;
        return data;
    }

    // Keep Elite's SDK State encoding, unsigned stick domain and separate Z/Rz triggers.
    // The USB profile's report size remains SDK-derived.
    private static HMGamepadState BuildEliteState(HMProfile profile, CommandEnvelope envelope)
    {
        if (profile.Id != "xbox-elite-v2")
            throw new InvalidOperationException("elite-profile-not-admitted:" + profile.Id);
        var buttons = HMButton.None;
        if ((envelope.Buttons & 0x1000) != 0) buttons |= HMButton.A;
        if ((envelope.Buttons & 0x2000) != 0) buttons |= HMButton.B;
        if ((envelope.Buttons & 0x4000) != 0) buttons |= HMButton.X;
        if ((envelope.Buttons & 0x8000) != 0) buttons |= HMButton.Y;
        if ((envelope.Buttons & 0x0100) != 0) buttons |= HMButton.LeftBumper;
        if ((envelope.Buttons & 0x0200) != 0) buttons |= HMButton.RightBumper;
        if ((envelope.Buttons & 0x0020) != 0) buttons |= HMButton.Back;
        if ((envelope.Buttons & 0x0010) != 0) buttons |= HMButton.Start;
        if ((envelope.Buttons & 0x0040) != 0) buttons |= HMButton.LeftStick;
        if ((envelope.Buttons & 0x0080) != 0) buttons |= HMButton.RightStick;
        if ((envelope.Buttons & 0x0400) != 0) buttons |= HMButton.Guide;
        if((envelope.ScreenButtons&1)!=0)buttons|=HMButton.Guide;
        var octant = MapHatOctant(envelope.Buttons);
        return new HMGamepadState
        {
            Buttons = buttons,
            Hat = octant == 8 ? HMHat.None : (HMHat)(octant + 1),
            Axes = new Dictionary<HMAxis, float>(6)
            {
                // The current SDK State encoder consumes its
                // standard stick values in the unsigned 0..1 report domain,
                // even though the canonical B frame is signed -1..1. Empirical
                // XInput readback is the contract here: 0.5 is center; passing
                // signed zero produces (-32768,+32767) full-scale output.
                // Preserve HC's Y direction convention before conversion.
                [HMAxis.X] = UnitStickAxis(envelope.Lx),
                [HMAxis.Y] = UnitStickAxis(-envelope.Ly),
                [HMAxis.Rx] = UnitStickAxis(envelope.Rx),
                [HMAxis.Ry] = UnitStickAxis(-envelope.Ry),
                // Preserve the raw trigger values without a canonical deadzone.
                [HMAxis.Z] = envelope.LtRaw,
                [HMAxis.Rz] = envelope.RtRaw,
            }
        };
    }

    private static float UnitStickAxis(float value) =>
        Math.Clamp((value + 1f) * 0.5f, 0f, 1f);

    // SteamDeck persona（G2）：steam-deck-composite 走 SubmitState + alwaysArmed
    // 64B Neptune frame（extendedReport 自动打包）。按钮映射 HC SteamDeckTarget
    // 按键位 ↔ HMButton 枚举；轴经 extendedReport 解析 sticks/triggers；IMU 由
    // frame imu 字段编码（HC SteamDeckTarget.BuildReport:104-116：accel*16384/g、
    // gyro*16/dps）。quaternion（byte36-42）HIDMaestro composite 无字段，首期
    // 缺省（G3/M6 待真机验证，不阻塞 persona+陀螺仪双闭口）。
    private static HMGamepadState BuildSteamDeckState(HMProfile profile, CommandEnvelope envelope)
    {
        var fields = BuildSteamDeckStateFields(profile, envelope);
        // SubmitState keeps a fresh dictionary owned by this state; raw reports use fields directly.
        return new HMGamepadState
        {
            Buttons = fields.Buttons,
            Hat = fields.Hat,
            Axes = new Dictionary<HMAxis, float>(6)
            {
                [HMAxis.X] = fields.X,
                [HMAxis.Y] = fields.Y,
                [HMAxis.Rx] = fields.Rx,
                [HMAxis.Ry] = fields.Ry,
                [HMAxis.Z] = fields.Z,
                [HMAxis.Rz] = fields.Rz,
            },
            AccelX = fields.AccelX,
            AccelY = fields.AccelY,
            AccelZ = fields.AccelZ,
            GyroPitch = fields.GyroPitch,
            GyroYaw = fields.GyroYaw,
            GyroRoll = fields.GyroRoll,
        };
    }

    // Value-only conversion shared by the SDK state path and the raw SteamDeck encoder.
    private static SteamDeckStateFields BuildSteamDeckStateFields(HMProfile profile, CommandEnvelope envelope)
    {
        if (profile.InputReportSize != 64) throw new InvalidOperationException("steam-deck-wire-size-mismatch:" + profile.InputReportSize);
        var buttons = HMButton.None;
        if ((envelope.Buttons & 0x1000) != 0) buttons |= HMButton.Cross;
        if ((envelope.Buttons & 0x2000) != 0) buttons |= HMButton.Circle;
        if ((envelope.Buttons & 0x4000) != 0) buttons |= HMButton.Square;
        if ((envelope.Buttons & 0x8000) != 0) buttons |= HMButton.Y;
        if ((envelope.Buttons & 0x0100) != 0) buttons |= HMButton.LeftBumper;
        if ((envelope.Buttons & 0x0200) != 0) buttons |= HMButton.RightBumper;
        if ((envelope.Buttons & 0x0020) != 0) buttons |= HMButton.Back;
        if ((envelope.Buttons & 0x0010) != 0) buttons |= HMButton.Start;
        if ((envelope.Buttons & 0x0040) != 0) buttons |= HMButton.LeftStick;
        if ((envelope.Buttons & 0x0080) != 0) buttons |= HMButton.RightStick;
        if ((envelope.Buttons & 0x0400) != 0) buttons |= HMButton.Guide;
        // HIDMaestro 1.12 names wire bits 15/16 LeftPaddle2/RightPaddle2.
        // Keep the existing M1 -> L5 and M2 -> R5 wire behavior.
        if ((envelope.BackButtons & 0x1) != 0) buttons |= HMButton.LeftPaddle2;
        if ((envelope.BackButtons & 0x2) != 0) buttons |= HMButton.RightPaddle2;
        // L4/R4 screen buttons remain on their existing explicit raw-report bits.
        var hat = HMHat.None;
        var dpadUp = (envelope.Buttons & 0x0001) != 0;
        var dpadDown = (envelope.Buttons & 0x0002) != 0;
        var dpadLeft = (envelope.Buttons & 0x0004) != 0;
        var dpadRight = (envelope.Buttons & 0x0008) != 0;
        if (dpadUp && dpadRight) hat = HMHat.NorthEast;
        else if (dpadDown && dpadRight) hat = HMHat.SouthEast;
        else if (dpadDown && dpadLeft) hat = HMHat.SouthWest;
        else if (dpadUp && dpadLeft) hat = HMHat.NorthWest;
        else if (dpadUp) hat = HMHat.North;
        else if (dpadRight) hat = HMHat.East;
        else if (dpadDown) hat = HMHat.South;
        else if (dpadLeft) hat = HMHat.West;
        return new SteamDeckStateFields
        {
            Buttons = buttons,
            Hat = hat,
            // 共享 0..1 report 域（0.5=center），extendedReport
            // 自动解析到 Neptune int16-axis 字段（byte48-54）。
            X = UnitStickAxis(envelope.Lx),
            Y = UnitStickAxis(-envelope.Ly),
            Rx = UnitStickAxis(envelope.Rx),
            Ry = UnitStickAxis(-envelope.Ry),
            // HC SteamDeckTarget:127-128 写 uint16-trigger（byte44-46），
            // 消费 0..1 域（extendedReport uint16-trigger 自动编码）。
            // S54 线缆探针实证：steam-deck-composite 的 StandardAxes 为
            // X/Y/Rx/Ry/Z/Rz——扳机角色是 Z/Rz，Vx/Vy 不被该 profile 编码
            // （Vx/Vy 提交实测 triggers=0）。必须用 Z/Rz；codec 会在
            // Z/Rz>0 时自动置 RT/LT_DIGITAL 按钮位（HC data[8] 0x02/0x01）。
            // G8/HC parity：HC SteamDeckTarget 消费原始 AxisState（无 canonical
            // ≤30 死区），当前输出同取 LtRaw/RtRaw；canonical Lt/Rt
            // 仅用于 gate/merge/score。旧协调器缺 LtRaw 时由 TryConsumeFrame
            // 回退 canonical 值。
            Z = envelope.LtRaw,
            Rz = envelope.RtRaw,
            // G1：IMU 字段（HC SteamDeckTarget.BuildReport:104-116 编码系数）：
            //   accelX = accel.X * 16384/g；accelY = -accel.Z * 16384/g；
            //   accelZ = accel.Y * 16384/g（HC accel 轴序 X,-Z,Y）
            //   gyroPitch = gyro.X * 16/dps；gyroYaw = -gyro.Z * 16/dps；
            //   gyroRoll = gyro.Y * 16/dps（HC gyro 轴序 X,-Z,Y）
            // 仅 imuAdmitted 时写入；fail-zero（HC 无传感器时 DSU 全 0）。
            AccelX = QuantizeMotionAxis(envelope.ImuAdmitted ? envelope.ImuAccelX : 0.0, 16384.0),
            AccelY = QuantizeMotionAxis(envelope.ImuAdmitted ? -envelope.ImuAccelZ : 0.0, 16384.0),
            AccelZ = QuantizeMotionAxis(envelope.ImuAdmitted ? envelope.ImuAccelY : 0.0, 16384.0),
            GyroPitch = QuantizeMotionAxis(envelope.ImuAdmitted ? envelope.ImuGyroX : 0.0, 16.0),
            GyroYaw = QuantizeMotionAxis(envelope.ImuAdmitted ? -envelope.ImuGyroZ : 0.0, 16.0),
            GyroRoll = QuantizeMotionAxis(envelope.ImuAdmitted ? envelope.ImuGyroY : 0.0, 16.0),
            // HMGamepadState has no quaternion properties. The existing raw
            // SteamDeck report writes admitted w/x/y/z at bytes 36..43.
        };
    }

    private static byte AxisToByte(float value)
    {
        var normalized = Math.Clamp((value + 1f) * 127.5f, 0f, 255f);
        return (byte)Math.Clamp((int)MathF.Round(normalized), 0, 255);
    }

    private static byte TriggerToByte(float value) => (byte)Math.Clamp((int)MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0, 255);

    private static byte MapHatOctant(int buttons)
    {
        var up = (buttons & 0x0001) != 0;
        var down = (buttons & 0x0002) != 0;
        var left = (buttons & 0x0004) != 0;
        var right = (buttons & 0x0008) != 0;
        if (up && right) return 1;
        if (down && right) return 3;
        if (down && left) return 5;
        if (up && left) return 7;
        if (up) return 0;
        if (right) return 2;
        if (down) return 4;
        if (left) return 6;
        return 8;
    }

    private static byte MapFaceButtons(int buttons)
    {
        byte result = 0;
        if ((buttons & 0x4000) != 0) result |= 0x10; // X
        if ((buttons & 0x1000) != 0) result |= 0x20; // A
        if ((buttons & 0x2000) != 0) result |= 0x40; // B
        if ((buttons & 0x8000) != 0) result |= 0x80; // Y
        return result;
    }

    private static byte MapDigitalButtons(int buttons, bool leftTrigger, bool rightTrigger)
    {
        byte result = 0;
        if ((buttons & 0x0100) != 0) result |= 0x01;
        if ((buttons & 0x0200) != 0) result |= 0x02;
        if (leftTrigger) result |= 0x04;
        if (rightTrigger) result |= 0x08;
        if ((buttons & 0x0020) != 0) result |= 0x10;
        if ((buttons & 0x0010) != 0) result |= 0x20;
        if ((buttons & 0x0040) != 0) result |= 0x40;
        if ((buttons & 0x0080) != 0) result |= 0x80;
        return result;
    }

    // 批112（DualSense Edge，054C:0DF2）：stock profile dualsense-edge 的
    // extendedReport/fields[10]（byte 10）逐位实证 = Guide(0)/Touchpad(1)/Misc1(2)/
    // _ /LeftPaddle2(4)/RightPaddle2(5)/LeftPaddle(6)/RightPaddle(7)。我方 DS 帧是
    // **63 B data-only**（report ID 单独给）⇒ profile 的 byte 10 就是 data[9]，
    // 故这 4 位直接落在 data[9] bit4..7（唯一置位点 = 上方那一行）。
    // 映射：M1(左,bit0)→LeftPaddle(bit6)、M2(右,bit1)→RightPaddle(bit7)，与 SteamDeck
    // 的 L5/R5 同"主对"语义；bit4/5（Paddle2 对）留空。
    // ⚠ 真机需确认物理 M1/M2 ↔ Edge 四个背键的对应序；本表是唯一真源，改一处即可。
    private const int DsEdgePaddleLeft = 0x40;   // bit6 = LeftPaddle
    private const int DsEdgePaddleRight = 0x80;  // bit7 = RightPaddle
    private static int MapDsEdgePaddles(int backButtons) =>
        ((backButtons & 0x1) != 0 ? DsEdgePaddleLeft : 0) |
        ((backButtons & 0x2) != 0 ? DsEdgePaddleRight : 0);

    private enum Phase { AwaitHello, Hello, Prepared, Neutralized, Active, Quiesced, Released }
    private sealed record IdempotentReply(string Fingerprint, string Command, string Response);

    private sealed record CommandEnvelope(
        string Command,
        string RequestId,
        ulong Sequence,
        string RunId,
        ulong Epoch,
        ulong PowerGeneration,
        string TargetId,
        string OwnerIdentity,
        string Persona,
        string ProfileIdentity,
        string StartupNonce,
        ulong ConfigRevision,
        string ConfigHash)
    {
        [System.Text.Json.Serialization.JsonIgnore] public bool ReportChainPrepareSpecified1410 { get; init; }
        [System.Text.Json.Serialization.JsonIgnore] public MsiReportChainPrepare1410? ReportChainPrepare1410 { get; init; }
        [System.Text.Json.Serialization.JsonIgnore] public MsiReportChainSource1410? ReportChainSource1410 { get; init; }
        [System.Text.Json.Serialization.JsonPropertyName("msiSteamDeckReleaseFence")]
        public bool MsiSteamDeckReleaseFence { get; init; }
        public int Buttons { get; init; }
        public float Lx { get; init; }
        public float Ly { get; init; }
        public float Rx { get; init; }
        public float Ry { get; init; }
        public float Lt { get; init; }
        public float Rt { get; init; }
        // G8/HC parity: raw pre-canonicalized trigger values carried by modern
                // frames. Older coordinators omit them; TryConsumeFrame falls back to
                // the canonical value, which on the zeroed domain is equivalent.
                public float LtRaw { get; init; }
                public float RtRaw { get; init; }
                // 7.52 P1-T1（pipe 切点下移）：raw 域原生 SHORT/BYTE 值（HC
                // XInputController.cs:128-133 直存 AxisState；Xbox360Target.cs:50-57
                // wire 直写）。优先消费 raw 域编码 wire；旧协调器缺 raw 时回退
                // canonical float 域（Q1 双域并存，破坏面最小）。
                public int RawButtons { get; init; }
                public short RawThumbLX { get; init; }
                public short RawThumbLY { get; init; }
                public short RawThumbRX { get; init; }
                public short RawThumbRY { get; init; }
                public byte RawLeftTrigger { get; init; }
                public byte RawRightTrigger { get; init; }
                public bool HasRawDomain { get; init; }
        public ulong SampleSequence { get; init; }
        public ulong MonotonicTick { get; init; }
        public string NormalizedInputHash { get; init; } = "";
        public string MotionPairProofId { get; init; } = "";
        public string PhysicalBaseContainerInstanceId { get; init; } = "";
        public string PhysicalDeviceInstanceId { get; init; } = "";
        // HIDHIDE 屏蔽命令参数（native 经 pipe 下发，API→CLI fallback）。
        public string HidHidePath { get; init; } = "";
        public bool HidHideOn { get; init; }
        public bool HidHideCloak { get; init; }
        // SteamDeck persona IMU（G1/G3）：矩阵后 gyro(dps)/accel(g) + quaternion，
        // 供 BuildSteamDeckState 编码为 SteamDeckTarget wire 字段。非 steamdeck
        // 帧缺省全 0（HC 无传感器时 DSU 全 0）。
        public bool ImuAdmitted { get; init; }
        public double ImuGyroX { get; init; }
        public double ImuGyroY { get; init; }
        public double ImuGyroZ { get; init; }
        public double ImuAccelX { get; init; }
        public double ImuAccelY { get; init; }
        public double ImuAccelZ { get; init; }
        [System.Text.Json.Serialization.JsonIgnore] public bool ImuQuaternionPresent { get; init; }
        public double ImuQuatW { get; init; } = 1.0;
        public double ImuQuatX { get; init; }
        public double ImuQuatY { get; init; }
        public double ImuQuatZ { get; init; }
        // 背键映射 Phase A（背键映射A任务书 2026-09-14 S4）：bit0=M1(左)、
        // bit1=M2(右)。缺省 0（老 native 不发该字段零污染）；由 steamdeck 与 dualsense-edge
        // 分支消费；Elite 不读。不进 normalizedInputHash。
        public int BackButtons { get; init; }
        public int ScreenButtons { get; init; }
        public bool ScreenPadsPresent { get; init; }
        public ScreenPad ScreenPadLeft { get; init; }
        public ScreenPad ScreenPadRight { get; init; }
    }
}
