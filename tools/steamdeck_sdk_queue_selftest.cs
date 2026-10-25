// Actual 2.0 private state and the shipping observer. No SDK constructors,
// pump, service, driver, device, event handles or sockets are used.
using System.Buffers.Binary;
using System.Collections;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text.Json;
using HIDMaestro;
using YeManInputHost.Diagnostics;

internal static class Program
{
    const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    const string Sha = "8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C";
    const string Profile200Sha = "14E67AC749C3802E035A3909A27912188B3F878655B590C683C4382BC6882AB2";
    static readonly List<object> Checks = new();
    static int failed, lockWorkersCreated, workersRemaining;
    static FieldInfo F(object o, string n) => o.GetType().GetField(n, Flags) ?? throw new Exception("field: " + n);
    static object? Get(object o, string n) => F(o, n).GetValue(o);
    static void Set(object o, string n, object? v) => F(o, n).SetValue(o, v);
    static object Prop(object o, string n) => o.GetType().GetProperty(n, Flags)!.GetValue(o)!;
    static void Need(bool ok, string n) { if (!ok) throw new Exception(n); }
    static void Check(string n, bool ok) { Checks.Add(new { name = n, ok }); if (!ok) failed++; }
    static byte[] Packet(uint n, int capacity = 64)
    {
        var b = new byte[capacity]; b[0] = 1; b[2] = 9; b[3] = 64;
        BinaryPrimitives.WriteUInt32LittleEndian(b.AsSpan(4), n); return b;
    }
    static bool Safe(object d) => Get(d, "_connection") is null && Get(d, "_inputThread") is null &&
        Get(d, "<Audio>k__BackingField") is null && Get(d, "_engine") is null &&
        (nint)Get(d, "_inputView")! == 0 && (nint)Get(d, "_outputView")! == 0 &&
        (nint)Get(d, "_outputEvent")! == 0 && (nint)Get(d, "_inputWaitEvent")! == 0;
    // MethodInfo.Invoke cannot box Span parameters. This BCL bridge initializes
    // stack-only empty spans and a private command value, then directly calls
    // only the hash-pinned HandleSubmit method after Safe(device) is checked.
    static Action<object, byte, uint> SubmitBridge(Type deviceType, Type commandType, MethodInfo method)
    {
        var bridge = new DynamicMethod("PrivateFixtureHandleSubmit200", typeof(void),
            new[] { typeof(object), typeof(byte), typeof(uint) }, typeof(Program).Module, skipVisibility: true);
        var il = bridge.GetILGenerator();
        var command = il.DeclareLocal(commandType);
        var payload = il.DeclareLocal(method.GetParameters()[1].ParameterType);
        var packets = il.DeclareLocal(method.GetParameters()[2].ParameterType);
        foreach (var local in new[] { command, payload, packets }) { il.Emit(OpCodes.Ldloca, local); il.Emit(OpCodes.Initobj, local.LocalType); }
        void Constant(string field, int value) { il.Emit(OpCodes.Ldloca, command); il.Emit(OpCodes.Ldc_I4, value); il.Emit(OpCodes.Stfld, commandType.GetField(field, Flags)!); }
        Constant("Command", 1); Constant("Direction", 1); Constant("TransferBufferLength", 64); Constant("NumberOfPackets", -1);
        il.Emit(OpCodes.Ldloca, command); il.Emit(OpCodes.Ldarg_2); il.Emit(OpCodes.Stfld, commandType.GetField("Seqnum", Flags)!);
        il.Emit(OpCodes.Ldloca, command); il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Ldc_I4, 0x7f); il.Emit(OpCodes.And); il.Emit(OpCodes.Stfld, commandType.GetField("Ep", Flags)!);
        il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Castclass, deviceType); il.Emit(OpCodes.Ldloca, command); il.Emit(OpCodes.Ldloc, payload); il.Emit(OpCodes.Ldloc, packets);
        il.Emit(OpCodes.Call, method); il.Emit(OpCodes.Ret);
        return bridge.CreateDelegate<Action<object, byte, uint>>();
    }
    static int Count(object queue) => ((ICollection)queue).Count;
    // These methods act on private BCL fixture collections only. The production
    // observer never reflects queue methods or examines a QueuedFrame payload.
    static object? Bcl(object queue, string method, params object?[] args) => queue.GetType().GetMethod(method)!.Invoke(queue, args);
    static UsbipTransportObservation Hooked(Assembly sdk, Action<string> probe) => new(
        new(sdk.ManifestModule.ModuleVersionId, sdk, "HIDMaestro.HMController",
            "HIDMaestro.Internal.Usbip.UsbipBackendHandle", "HIDMaestro.Internal.Usbip.UsbipEmulatedDevice",
            "HIDMaestro.Internal.Usbip.UsbipServer+Connection"), probe);

    static void ObserverChecks(Assembly sdk, object device, object frames, Dictionary<byte, Queue<uint>> pending,
        byte[][] reports, int[] lengths, int[] slots, object builder, Dictionary<string, object?> result)
    {
        var controller = RuntimeHelpers.GetUninitializedObject(typeof(HMController));
        var ht = sdk.GetType("HIDMaestro.Internal.Usbip.UsbipBackendHandle", true)!;
        object handle = RuntimeHelpers.GetUninitializedObject(ht);
        Set(handle, "<Device>k__BackingField", device); Set(controller, "<UsbipHandle>k__BackingField", handle);
        Set(controller, "_inputSeqNo", 0xfffffffeu); Set(device, "_lastSharedSeqNo", 42u);
        Set(device, "_vhciPort", 17);
        Set(device, "_builder", builder); Set(device, "_inputReports", reports);
        Set(device, "_inputReportLength", lengths); Set(device, "_inputSlot", slots);
        var observer = new UsbipTransportObservation();
        var snap = observer.Capture(controller, true);
        Check("v200-shipping-contract-binds-real-dll-and-device-port", snap.Status == "observed" && snap.VhciPort == 17 && snap.PrimaryInEndpoint == 0x83);
        Check("v200-primary-cache-uses-mapped-slot1-and-effective64-not-first-or-capacity80", snap.PrimaryInputReportId == 0 && snap.PrimaryInputReportSlot == 1 &&
            snap.LastInputReportLength == 64 && snap.LastSteamDeckPadReport?.PacketCounter == 99 && snap.InputReportCacheObservation == "observed-primary-report-cache");
        Check("v200-null-input-thread-is-absent-metadata", snap.InputThreadIsAlive is null && snap.InputThreadObservation == "absent");
        Check("v200-progress-and-cache-cannot-prove-delivery", snap.ProducerProgress?.ProducerSharedSequenceBefore == 0xfffffffeu &&
            snap.ProducerProgress.PumpAcceptedSharedSequenceBefore == 42 && !snap.ProducerProgress.TransferCompletedProven &&
            !snap.ProducerProgress.SteamConsumptionProven && !snap.TransferCompletedProven && !snap.SteamConsumptionProven &&
            snap.ConnectionClosedObservation == "unsupported-field-absent");
        result["stockObserverSnapshot"] = snap;
        uint[][] requests = pending.OrderBy(x => x.Key).Select(x => x.Value.ToArray()).ToArray();
        int frameCount = Count(frames); object? oldest = frameCount == 0 ? null : Bcl(frames, "Peek");
        byte[][] saved = reports.Select(x => (byte[])x.Clone()).ToArray();
        int[] savedLengths = (int[])lengths.Clone(), savedSlots = (int[])slots.Clone();
        observer.Capture(controller, true);
        Check("v200-capture-does-not-consume-or-write-private-sdk-state", frameCount == Count(frames) &&
            (frameCount == 0 || oldest!.Equals(Bcl(frames, "Peek"))) &&
            requests.Zip(pending.OrderBy(x => x.Key).Select(x => x.Value.ToArray())).All(x => x.First.SequenceEqual(x.Second)) &&
            reports.Zip(saved).All(x => x.First.SequenceEqual(x.Second)) && lengths.SequenceEqual(savedLengths) && slots.SequenceEqual(savedSlots) &&
            (uint)Get(device, "_lastSharedSeqNo")! == 42 && (uint)Get(controller, "_inputSeqNo")! == 0xfffffffeu && Safe(device));
        int reads = 0;
        var gated = Hooked(sdk, _ => reads++);
        Check("v200-unverified-and-disabled-gates-do-zero-reflection", gated.Capture(controller, false).Status == "disabled" && reads == 0 &&
            new UsbipTransportObservation(enabled: false).Capture(controller, true).Status == "disabled");
        Check("v200-null-controller-remains-unknown", observer.Capture(null, true).Status == "no-controller");
        Set(controller, "<UsbipHandle>k__BackingField", null);
        Check("v200-null-handle-remains-unknown", observer.Capture(controller, true).Status == "no-handle");
        Set(controller, "<UsbipHandle>k__BackingField", handle);

        // Keep the frozen original 1.12 FakeSdk field contract at the end of
        // this file. It must not be accepted or relabelled as a 2.0 layout.
        var legacyDevice = new FakeSdk.Device(Packet(112));
        var legacy = new FakeSdk.Controller(new FakeSdk.Handle(legacyDevice));
        var la = legacy.GetType().Assembly; int legacyReads = 0;
        var legacyObserver = new UsbipTransportObservation(new(la.ManifestModule.ModuleVersionId, la,
            typeof(FakeSdk.Controller).FullName!, typeof(FakeSdk.Handle).FullName!,
            typeof(FakeSdk.Device).FullName!, typeof(FakeSdk.Connection).FullName!), stage => { if (stage != "metadata") legacyReads++; });
        var rejected = legacyObserver.Capture(legacy, true);
        Check("legacy112-contract-rejected-before-field-reads-never-resigned-as200", rejected.Status == "mismatch" &&
            rejected.LastSteamDeckPadReport is null && rejected.FrameQueueCount is null && legacyReads == 0);
        result["legacy112Rejection"] = rejected;

        void CacheUnknown(string name, Action mutate, Action restore)
        {
            try
            {
                mutate(); var unknown = observer.Capture(controller, true);
                Check(name, unknown.InputReportCacheObservation.StartsWith("unknown-", StringComparison.Ordinal) &&
                    unknown.LastInputReportLength is null && unknown.LastSteamDeckPadReport is null && unknown.FrameQueueCount == frameCount);
            }
            finally { restore(); }
        }
        CacheUnknown("v200-unmapped-primary-slot-is-unknown-no-first-slot-fallback", () => slots[0] = -1, () => slots[0] = 1);
        CacheUnknown("v200-out-of-range-slot-is-unknown", () => slots[0] = 256, () => slots[0] = 1);
        CacheUnknown("v200-invalid-slot-map-shape-is-unknown", () => Set(device, "_inputSlot", new int[255]), () => Set(device, "_inputSlot", slots));
        CacheUnknown("v200-null-cache-is-unknown", () => Set(device, "_inputReports", null), () => Set(device, "_inputReports", reports));
        CacheUnknown("v200-mismatched-cache-length-array-is-unknown", () => Set(device, "_inputReportLength", new int[1]), () => Set(device, "_inputReportLength", lengths));
        CacheUnknown("v200-null-primary-cache-is-unknown", () => reports[1] = null!, () => reports[1] = Packet(99, 80));
        foreach (int length in new[] { -1, 0, 81, int.MaxValue })
            CacheUnknown("v200-invalid-effective-length-" + length + "-is-unknown", () => lengths[1] = length, () => lengths[1] = 64);
        CacheUnknown("v200-zero-primary-endpoint-is-unknown", () => Set(device, "_primaryInEndpoint", (byte)0), () => Set(device, "_primaryInEndpoint", (byte)0x83));
        CacheUnknown("v200-null-builder-is-unknown", () => Set(device, "_builder", null), () => Set(device, "_builder", builder));
        CacheUnknown("v200-report-id-mode-mismatch-is-unknown", () => Set(device, "_inputReportIds", true), () => Set(device, "_inputReportIds", false));
        lengths[1] = 63; var shortReport = observer.Capture(controller, true); lengths[1] = 64;
        Check("v200-valid-other-length-keeps-length-without-decoding-stale-tail", shortReport.LastInputReportLength == 63 && shortReport.LastSteamDeckPadReport is null);
        Set(builder, "<InputReportId>k__BackingField", (byte)7); Set(device, "_inputReportIds", true); slots[7] = 2;
        reports[2][0] = 7; var withId = observer.Capture(controller, true);
        Check("v200-nonzero-report-id-resolves-slot2", withId.PrimaryInputReportId == 7 && withId.PrimaryInputReportSlot == 2 &&
            withId.LastInputReportLength == 64 && withId.LastSteamDeckPadReport?.PacketCounter is null);
        reports[2][0] = 8; var badId = observer.Capture(controller, true);
        Check("v200-cached-id-mismatch-is-unknown", badId.InputReportCacheObservation == "unknown-cached-report-id" && badId.LastInputReportLength is null);
        Set(builder, "<InputReportId>k__BackingField", (byte)0); Set(device, "_inputReportIds", false);

        bool mutated = false;
        var copying = Hooked(sdk, stage => { if (stage == "owner-after") { Array.Clear(reports[1]); mutated = true; } });
        var copied = copying.Capture(controller, true); reports[1] = Packet(99, 80);
        Check("v200-copied64bytes-survive-source-mutation-after-lock", mutated && copied.LastSteamDeckPadReport?.PacketCounter == 99);
        UsbipTransportObservation? reentrant = null; UsbipTransportObservation.Snapshot? nested = null;
        reentrant = Hooked(sdk, stage => { if (stage == "metadata") nested = reentrant!.Capture(controller, true); });
        Check("v200-reentrant-capture-is-loss-without-wait", reentrant.Capture(controller, true).Status == "observed" && nested?.Detail == "observer-busy");

        // A finite BCL worker owns only this fixture's lock. No polling loop.
        object gate = Get(device, "_hidLock")!; using var entered = new ManualResetEventSlim(); using var release = new ManualResetEventSlim();
        lockWorkersCreated++; Interlocked.Increment(ref workersRemaining);
        var lockOwner = Task.Run(() => { try { lock (gate) { entered.Set(); Need(release.Wait(5000), "fixture-lock-release-timeout"); } } finally { Interlocked.Decrement(ref workersRemaining); } });
        Need(entered.Wait(5000), "fixture-lock-enter-timeout");
        UsbipTransportObservation.Snapshot busy; var watch = Stopwatch.StartNew(); int lockedReads = 0;
        try { busy = Hooked(sdk, stage => { if (stage == "_pendingInterruptIn" || stage == "_inputReports" || stage == "_frameQueue") lockedReads++; }).Capture(controller, true); }
        finally { watch.Stop(); release.Set(); lockOwner.GetAwaiter().GetResult(); }
        Check("v200-contended-lock-never-waits-or-reads-cache-queues", busy.Detail == "lock-busy" && busy.FrameQueueCount is null &&
            busy.LastInputReportLength is null && busy.LastSteamDeckPadReport is null && busy.InputReportCacheObservation == "unknown" && lockedReads == 0 && watch.ElapsedMilliseconds < 1000);
        result["contendedCaptureElapsedMs"] = watch.Elapsed.TotalMilliseconds;

        var connectionType = sdk.GetType("HIDMaestro.Internal.Usbip.UsbipServer+Connection", true)!;
        var owner1 = RuntimeHelpers.GetUninitializedObject(connectionType); var owner2 = RuntimeHelpers.GetUninitializedObject(connectionType);
        Set(owner1, "_device", device); Set(owner2, "_device", device); Set(device, "_connection", owner1);
        var changing = Hooked(sdk, stage => { if (stage == "owner-after") Set(device, "_connection", owner2); });
        var changed = changing.Capture(controller, true); Set(device, "_connection", null);
        Check("v200-owner-change-is-reference-metadata-not-socket-proof", changed.OwnerReferenceObservation == "changed" &&
            changed.ConnectionDeviceRelationBefore == "same-device" && changed.ConnectionIdentityBefore != changed.ConnectionIdentityAfter && !changed.TransferCompletedProven);
        for (byte key = 1; key <= 12; key++) pending[key] = new Queue<uint>(new[] { (uint)key });
        var overflow = observer.Capture(controller, true);
        Check("v200-endpoint-capture-is-bounded-to8-with-explicit-loss", overflow.Detail == "endpoint-overflow" && overflow.Endpoints.Length == 8 &&
            overflow.EndpointKeyCount == pending.Count && overflow.EndpointsOmitted == pending.Count - 8 && overflow.Endpoints[0].Address == 1);
        pending.Clear(); var many = new Queue<uint>(Enumerable.Range(1, 200000).Select(x => (uint)x)); pending[0x83] = many;
        long before = GC.GetAllocatedBytesForCurrentThread(); var large = observer.Capture(controller, true); long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Check("v200-large-request-queue-is-counted-peeked-without-consumption-or-copy", large.SampledPendingRequestCount == 200000 &&
            large.Endpoints.Single().OldestSequence == 1 && many.Count == 200000 && allocated < 32768);
        result["largeQueueCaptureAllocatedBytes"] = allocated;
        Check("v200-observer-ends-with-no-live-collaborators", Safe(device));
    }

    static int Main(string[] args)
    {
        if (args.Length != 1) return 2;
        var result = new Dictionary<string, object?>(); object? device = null, frames = null; IDictionary? endpoints = null;
        var pending = new Dictionary<byte, Queue<uint>>(); var spare = new Stack<byte[]>(); int calls = 0;
        try
        {
            Assembly sdk = typeof(HMController).Assembly;
            string hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(sdk.Location)));
            Need(hash == Sha && new FileInfo(sdk.Location).Length == 63076352 && sdk.ManifestModule.ModuleVersionId == Guid.Parse("86aed9a3-c024-4596-b02c-650082ffb852"), "SDK identity mismatch");
            result["sdkSha256"] = hash; result["sdkMvid"] = sdk.ManifestModule.ModuleVersionId; result["sdkBytes"] = 63076352;
            result["contract"] = "HIDMaestro-2.0.0-QueuedFrame-primary-report-cache";
            Check("locked200-sdk-hash-size-and-mvid", true);
            string rn = sdk.GetManifestResourceNames().Single(n => n.EndsWith("steam-deck-composite.json", StringComparison.Ordinal));
            byte[] raw; using (var stream = sdk.GetManifestResourceStream(rn)!) using (var copy = new MemoryStream()) { stream.CopyTo(copy); raw = copy.ToArray(); }
            string ph = Convert.ToHexString(SHA256.HashData(raw)); Need(ph == Profile200Sha, "2.0 profile hash mismatch");
            result["profile200Sha256"] = ph; Check("locked200-embedded-profile-identity", true);
            File.WriteAllBytes(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[0]))!, "embedded-composite200.json"), raw);
            using var doc = JsonDocument.Parse(raw); var usb = doc.RootElement.GetProperty("usbConfiguration");
            byte hid = usb.GetProperty("interfaces").EnumerateArray().Single(x => x.GetProperty("function").GetString() == "hid").GetProperty("interfaceNumber").GetByte();
            byte[] config = Convert.FromHexString(usb.GetProperty("configurationDescriptor").GetString()!);
            Need(config.Length == 150 && BinaryPrimitives.ReadUInt16LittleEndian(config.AsSpan(2)) == 150, "config framing");
            Type dt = sdk.GetType("HIDMaestro.Internal.Usbip.UsbDescriptorSet", true)!;
            Type et = dt.GetNestedType("EndpointInfo", BindingFlags.Public | BindingFlags.NonPublic)!;
            object descriptor = RuntimeHelpers.GetUninitializedObject(dt);
            endpoints = (IDictionary)Activator.CreateInstance(typeof(Dictionary<,>).MakeGenericType(typeof(byte), et))!;
            byte iface = 255, alt = 0;
            for (int i = 0; i < config.Length; i += config[i])
            {
                Need(config[i] >= 2 && i + config[i] <= config.Length, "config item");
                if (config[i + 1] == 4) { iface = config[i + 2]; alt = config[i + 3]; }
                if (config[i + 1] != 5) continue;
                object ep = RuntimeHelpers.GetUninitializedObject(et);
                Set(ep, "<Address>k__BackingField", config[i + 2]); Set(ep, "<Attributes>k__BackingField", config[i + 3]);
                Set(ep, "<MaxPacketSize>k__BackingField", BinaryPrimitives.ReadUInt16LittleEndian(config.AsSpan(i + 4)));
                Set(ep, "<Interval>k__BackingField", config[i + 6]); Set(ep, "<InterfaceNumber>k__BackingField", iface); Set(ep, "<AltSetting>k__BackingField", alt);
                endpoints.Add(config[i + 2], ep);
            }
            Set(descriptor, "_endpoints", endpoints); Set(descriptor, "<HidInterfaceNumber>k__BackingField", hid);
            byte primary = 0;
            foreach (DictionaryEntry pair in endpoints)
                if ((int)Prop(pair.Value!, "TransferType") == 3 && (bool)Prop(pair.Value!, "IsIn") && (byte)Prop(pair.Value!, "InterfaceNumber") == hid)
                { Need(primary == 0, "ambiguous primary"); primary = (byte)Prop(pair.Value!, "Address"); }
            Need(hid == 2 && primary == 0x83, "primary contract mismatch");
            Check("embedded200-primary83-mouse81-key82", endpoints.Contains((byte)0x81) && endpoints.Contains((byte)0x82) && (ushort)Prop(endpoints[(byte)0x83]!, "MaxPacketSize") == 64);
            Type bt = sdk.GetType("HIDMaestro.Internal.Usbip.UsbipEmulatedDevice", true)!;
            Need(bt.GetMethod("Finalize", Flags | BindingFlags.DeclaredOnly) is null, "backend finalizer unsafe");
            Type frameType = bt.GetNestedType("QueuedFrame", BindingFlags.NonPublic)!;
            Type queueType = typeof(Queue<>).MakeGenericType(frameType);
            frames = Activator.CreateInstance(queueType)!;
            object Frame(byte[] buffer, int length)
            {
                object frame = RuntimeHelpers.GetUninitializedObject(frameType);
                Set(frame, "Buffer", buffer); Set(frame, "Length", length); return frame;
            }
            device = RuntimeHelpers.GetUninitializedObject(bt);
            Set(device, "<Descriptors>k__BackingField", descriptor); Set(device, "_hidLock", new object());
            Set(device, "_frameQueue", frames); Set(device, "_spareFrames", spare); Set(device, "_pendingInterruptIn", pending); Set(device, "_primaryInEndpoint", primary);
            Set(device, "_readsPerEp", new Dictionary<byte, long>());
            var builder = RuntimeHelpers.GetUninitializedObject(sdk.GetType("HIDMaestro.Internal.HidReportBuilder", true)!);
            byte[][] reports = { Packet(1000, 80), Packet(99, 80), Packet(2000, 80) }; int[] lengths = { 64, 64, 64 };
            int[] slots = Enumerable.Repeat(-1, 256).ToArray(); slots[0] = 1;
            Set(device, "_builder", builder); Set(device, "_inputReports", reports); Set(device, "_inputReportLength", lengths); Set(device, "_inputSlot", slots);
            Check("constructor-bypass-null-connection-worker-audio-and-handles", Safe(device));
            Type ct = sdk.GetType("HIDMaestro.Internal.Usbip.UsbipProtocol+CommandHeader", true)!;
            MethodInfo method = bt.GetMethod("HandleSubmit", Flags)!;
            Need(method.ReturnType == typeof(void) && method.GetParameters()[0].ParameterType == ct.MakeByRefType(), "method contract");
            var submitBridge = SubmitBridge(bt, ct, method);
            void Submit(byte endpoint, uint seq)
            {
                Need(Safe(device), "unsafe fixture collaborator");
                submitBridge(device, endpoint, seq); calls++;
                Need(Safe(device), "post-submit safety contract");
            }
            Set(device, "_stop", true); Submit(0x83, 999);
            Check("v200-stopped-submit-really-is-noop", pending.Count == 0 && Count(frames) == 0);
            Set(device, "_stop", false);
            Submit(0x83, 1); Submit(0x83, 2);
            Check("v200-active-empty-primary-fifo-parks-requests-in-order", pending[(byte)0x83].SequenceEqual(new uint[] { 1, 2 }) && Count(frames) == 0);
            pending.Clear(); byte[] first = Packet(10, 80), second = Packet(11, 80);
            Bcl(frames, "Enqueue", Frame(first, 64)); Bcl(frames, "Enqueue", Frame(second, 64));
            Submit(0x81, 43); Submit(0x82, 51);
            Check("v200-secondary-pending-isolated-from-primary-QueuedFrame-fifo", Count(frames) == 2 && ReferenceEquals(Get(Bcl(frames, "Peek")!, "Buffer"), first) &&
                pending[(byte)0x81].SequenceEqual(new uint[] { 43 }) && pending[(byte)0x82].SequenceEqual(new uint[] { 51 }) && !pending.ContainsKey((byte)0x83));
            ObserverChecks(sdk, device, frames, pending, reports, lengths, slots, builder, result);
            pending.Clear();
            Submit(0x83, 3);
            Check("v200-actual-primary-dequeues-oldest-recycles-buffer-keeps-cache", Count(frames) == 1 &&
                ReferenceEquals(Get(Bcl(frames, "Peek")!, "Buffer"), second) && spare.Count == 1 && ReferenceEquals(spare.Peek(), first) &&
                BinaryPrimitives.ReadUInt32LittleEndian(reports[1].AsSpan(4)) == 99);
            Submit(0x83, 4); Submit(0x83, 5);
            Check("v200-primary-drains-fifo-then-parks", Count(frames) == 0 && spare.Count == 2 && pending[(byte)0x83].SequenceEqual(new uint[] { 5 }));
            Check("v200-void-return-and-markers-are-state-only-not-send-proof", calls == 8 && first.AsSpan().SequenceEqual(Packet(10, 80)) && second.AsSpan().SequenceEqual(Packet(11, 80)) && Safe(device));
            result["methodIlSha256"] = Convert.ToHexString(SHA256.HashData(method.GetMethodBody()!.GetILAsByteArray()!));
        }
        catch (Exception ex) { failed++; result["error"] = ex.ToString(); }
        finally
        {
            try
            {
                if (frames is not null) Bcl(frames, "Clear"); spare.Clear(); foreach (var q in pending.Values) q.Clear(); pending.Clear(); endpoints?.Clear();
                Check("cleanup-private-collections-cleared-and-workers-handles-absent", (frames is null || Count(frames) == 0) && spare.Count == 0 && pending.Count == 0 &&
                    (endpoints is null || endpoints.Count == 0) && (device is null || Safe(device)));
            }
            catch (Exception ex) { failed++; result["cleanupError"] = ex.ToString(); }
            GC.KeepAlive(device);
        }
        result["actualHandleSubmitCalls"] = calls;
        result["ok"] = failed == 0; result["checks"] = Checks.Count; result["cases"] = Checks; result["failureCount"] = failed;
        result["actualReturnType"] = "void via BCL DynamicMethod span bridge; reply status and payload unobserved";
        result["primarySelection"] = "Locked 2.0 embedded descriptor identifies endpoint83; builder primary report ID maps through _inputSlot; no backend constructor executed";
        result["skipped"] = new[] { "HandleUnlink: Audio.TryUnlink is unconditional and Audio is null; never invoked", "InputPumpLoop/capacity8 production, GET_REPORT, sockets, reply bytes/order, driver input acknowledgements and Steam consumption: not covered" };
        result["sdkConstructorsCalled"] = 0; result["inputPumpLoopCalls"] = 0; result["unlinkCalls"] = 0;
        result["privateLockWorkersCreated"] = lockWorkersCreated; result["privateWorkersRemaining"] = workersRemaining; result["pollingWorkersCreated"] = 0; result["busyWaitLoops"] = 0;
        result["socketCalls"] = 0; result["deviceDriverCalls"] = 0; result["inputCalls"] = 0; result["eventOrPhysicalHandlesCreated"] = 0; result["environmentChanges"] = 0;
        result["fieldFlickerVerified"] = false; result["socketPayloadObserved"] = false;
        File.WriteAllText(args[0], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
        return failed == 0 ? 0 : 3;
    }
}

// Original 1.12 fixture shape, frozen before this upgrade; negative control only.
namespace FakeSdk
{
 internal sealed class Controller{public Handle UsbipHandle{get;}internal Controller(Handle h){UsbipHandle=h;}}
 internal sealed class Handle{public Device Device{get;}public int VhciPort{get;}=1;internal Handle(Device d){Device=d;}}
 internal sealed class Connection{private Device _device;internal Connection(Device d){_device=d;}}
 internal sealed class Device
 {
  private readonly object _hidLock=new();private readonly Dictionary<byte,Queue<uint>> _pendingInterruptIn=new();private readonly Queue<byte[]> _frameQueue=new();
  private byte[] _lastInputReport;private readonly byte _primaryInEndpoint=131;private volatile bool _stop;private Connection _connection;private readonly Thread _inputThread=new(()=>{});
  internal object Lock=>_hidLock;internal byte[] Report{get=>_lastInputReport;set=>_lastInputReport=value;}
  internal Device(byte[] report){_lastInputReport=report;_connection=new(this);_stop=false;}
 }
}
