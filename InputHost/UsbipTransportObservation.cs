using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Passive, opt-in metadata reader for the approved stock SDK. The caller owns
// scheduling (the existing 1 Hz cost window and lifecycle samples). No SDK
// constructor, getter, method, socket API or control writes. The existing
// nonblocking SDK lock also permits one bounded SD pad-field copy per capture.
internal sealed class UsbipTransportObservation
{
    internal const string ApprovedSdkSha256 = "8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c";
    internal const string ApprovedSdkMvid = "86aed9a3-c024-4596-b02c-650082ffb852";
    internal const int MaximumEndpoints = 8;

    private static readonly Contract StockContract = new(
        new Guid(ApprovedSdkMvid), "HIDMaestro.Core", "HIDMaestro.HMController",
        "HIDMaestro.Internal.Usbip.UsbipBackendHandle",
        "HIDMaestro.Internal.Usbip.UsbipEmulatedDevice",
        "HIDMaestro.Internal.Usbip.UsbipServer+Connection");
    private readonly string _observerId = Guid.NewGuid().ToString("N");
    private readonly bool _enabled;
    private readonly Contract _contract;
    private Binding? _binding;
    private ProgressBinding? _progressBinding;
    private Type? _progressController;
    private string _progressBindingDetail = "not-bound";
    // Only the last device and connection are retained. IDs describe reference
    // sightings in this instance, not durable IDs or SDK/request generations.
    private object? _lastDevice, _lastConnection;
    private long _deviceOrdinal, _connectionOrdinal, _sampleOrdinal;
    private int _capturing;

    internal UsbipTransportObservation(bool enabled = true)
    {
        _enabled = enabled;
        _contract = StockContract;
    }

#if USBIP_OBSERVER_FIXTURE
    // Exists only in the isolated fixture compilation. The shipping/default
    // constructor always uses StockContract, even in a fixture build.
    internal sealed record FixtureContract(Guid Mvid, Assembly Assembly,
        string Controller, string Handle, string Device, string Connection);
    private readonly Action<string>? _fixtureProbe;
    internal UsbipTransportObservation(FixtureContract contract, Action<string>? probe = null,
        bool enabled = true)
    {
        _enabled = enabled;
        _contract = new(contract.Mvid, contract.Assembly.GetName().Name!,
            contract.Controller, contract.Handle, contract.Device, contract.Connection,
            contract.Assembly);
        _fixtureProbe = probe;
    }
#endif

    // sdkAssetVerified must come from the Host's existing approved asset check.
    // This reader does no file hashing/loading and cannot establish that claim.
    // sdkAssetVerified=false closes the gate: disabled, zero reflection, no input retained.
    internal Snapshot Capture(object? controller, bool sdkAssetVerified)
    {
        if (!_enabled) return Unknown("disabled", "opt-in-required");
        if (!sdkAssetVerified) return Unknown("disabled", "sdk-asset-unverified");
        if (Interlocked.CompareExchange(ref _capturing, 1, 0) != 0)
            return Unknown("loss", "observer-busy");
        try
        {
            _sampleOrdinal = checked(_sampleOrdinal + 1);
            if (controller is null)
            {
                _lastDevice = _lastConnection = null;
                return Unknown("no-controller", "controller-reference-null");
            }
#if USBIP_OBSERVER_FIXTURE
            _fixtureProbe?.Invoke("metadata");
#endif
            Type controllerType = controller.GetType();
            if (controllerType.FullName != _contract.Controller)
                return Unknown("unsupported", "controller-type-name");
            Assembly assembly = controllerType.Assembly;
            if (assembly.GetName().Name != _contract.AssemblyName ||
                controllerType.Module.ModuleVersionId != _contract.Mvid ||
                (_contract.RequiredAssembly is not null &&
                    !ReferenceEquals(assembly, _contract.RequiredAssembly)))
                return Unknown("mismatch", "sdk-assembly-identity");

            // Validate the full consumed field contract before the first GetValue.
            // Metadata is cached only after a successful complete binding.
            Binding binding = _binding is not null && _binding.Controller == controllerType
                ? _binding : Bind(controllerType, assembly);
            _binding = binding;
            object? handle = Read(binding.HandleField, controller);
            if (handle is null)
            {
                _lastDevice = _lastConnection = null;
                return Unknown("no-handle", "usbip-handle-reference-null");
            }
            RequireRuntimeType(handle, binding.Handle);
            object? device = Read(binding.DeviceField, handle);
            if (device is null) return Unknown("mismatch", "device-reference-null");
            RequireRuntimeType(device, binding.Device);
            int port = (int)Read(binding.PortField, device)!;
            long deviceId = DeviceId(device);

            object? ownerBefore = Read(binding.OwnerField, device, "owner-before");
            if (ownerBefore is not null) RequireRuntimeType(ownerBefore, binding.Connection);
            long? connectionBeforeId = ConnectionId(ownerBefore);
            string relation = "no-connection";
            if (ownerBefore is not null)
            {
                object? connectionDevice = Read(binding.ConnectionDeviceField, ownerBefore);
                if (connectionDevice is not null) RequireRuntimeType(connectionDevice, binding.Device);
                relation = connectionDevice is null ? "null" :
                    ReferenceEquals(device, connectionDevice) ? "same-device" : "different-device";
            }
            bool stop = (bool)Read(binding.StopField, device)!;
            byte primary = (byte)Read(binding.PrimaryField, device)!;
            object? inputThreadValue = Read(binding.ThreadField, device);
            bool? inputThreadAlive = null;
            string threadObservation = "absent";
            if (inputThreadValue is Thread inputThread)
            {
                try
                {
                    // IsAlive is metadata only. False includes unstarted and ended.
                    inputThreadAlive = inputThread.IsAlive;
                    threadObservation = "is-alive-metadata";
                }
                catch (Exception) { threadObservation = "metadata-error"; }
            }

            object? hidLock = Read(binding.LockField, device);
            if (hidLock is null) return Unknown("mismatch", "hid-lock-reference-null");
            // Fixed allocation before taking the SDK lock; no queue traversal.
            var endpoints = new Endpoint[MaximumEndpoints];
            int captured = 0;
            int? endpointCount = null, omitted = null, frames = null, reportLength = null;
            long? sampledRequests = null;
            bool acquired = false;
            string queueStatus = "lock-busy";
            Span<byte> reportCopy = stackalloc byte[64];
            bool reportCopied = false;
            byte? reportId = null;
            int? reportSlot = null;
            string cacheStatus = "unknown";
            ProducerProgressStart producerProgress = BeginProducerProgress(binding, controller, device);
            try
            {
                acquired = Monitor.TryEnter(hidLock, 0);
                if (acquired)
                {
                    var pending = (Dictionary<byte, Queue<uint>>?)Read(binding.PendingField, device)
                        ?? throw new ShapeMismatch("pending-reference-null");
                    object frameQueue = Read(binding.FramesField, device)
                        ?? throw new ShapeMismatch("frame-queue-reference-null");
                    RequireRuntimeType(frameQueue, binding.FramesType);
                    endpointCount = pending.Count;
                    // The exact Queue<QueuedFrame> type is verified at bind time.
                    // ICollection.Count executes BCL code only; no SDK getter,
                    // frame reflection, traversal, peeking or queue consumption.
                    frames = ((ICollection)frameQueue).Count;
                    cacheStatus = CopyPrimaryReport(binding, device, primary, reportCopy,
                        out reportId, out reportSlot, out reportLength, out reportCopied);
                    sampledRequests = 0;
                    // The byte key domain permits at most 256 native probes.
                    // Avoid Dictionary enumeration (including sparse tombstone
                    // scans) and never enumerate any request/frame queue.
                    for (int key = 0; key <= byte.MaxValue && captured < MaximumEndpoints; key++)
                    {
                        if (!pending.TryGetValue((byte)key, out Queue<uint>? queue)) continue;
                        if (queue is null) throw new ShapeMismatch("endpoint-queue-reference-null");
                        int count = queue.Count;
                        uint? oldest = count == 0 ? null : queue.Peek();
                        endpoints[captured++] = new Endpoint((byte)key, count, oldest);
                        sampledRequests += count;
                    }
                    omitted = endpointCount - captured;
                    queueStatus = omitted > 0 ? "endpoint-overflow" : "observed";
                }
            }
            finally { if (acquired) Monitor.Exit(hidLock); }

            // This brackets the observations, not an atomic SDK lifetime. The
            // owner can change and change back, including while _hidLock is held.
            object? ownerAfter = Read(binding.OwnerField, device, "owner-after");
            if (ownerAfter is not null) RequireRuntimeType(ownerAfter, binding.Connection);
            long? connectionAfterId = ConnectionId(ownerAfter);
            ProducerProgressObservation progress = CompleteProducerProgress(producerProgress, controller, device);
            Array.Resize(ref endpoints, captured);
            return new Snapshot
            {
                ObserverId = _observerId,
                Status = queueStatus == "observed" ? "observed" : "loss",
                Detail = queueStatus,
                SampleOrdinal = _sampleOrdinal,
                DeviceIdentity = deviceId,
                ConnectionIdentityBefore = connectionBeforeId,
                ConnectionIdentityAfter = connectionAfterId,
                OwnerPresentBefore = ownerBefore is not null,
                OwnerPresentAfter = ownerAfter is not null,
                OwnerReferenceObservation = ReferenceEquals(ownerBefore, ownerAfter) ? "stable" : "changed",
                ConnectionDeviceRelationBefore = relation,
                // The approved Connection has _server/_socket/_sendLock/_device,
                // but NO _closed field. Never infer socket or release state.
                ConnectionClosedObservation = "unsupported-field-absent",
                VhciPort = port,
                PrimaryInEndpoint = primary,
                StopFlag = stop,
                InputThreadIsAlive = inputThreadAlive,
                InputThreadObservation = threadObservation,
                QueueObservation = queueStatus,
                EndpointKeyCount = endpointCount,
                EndpointsOmitted = omitted,
                SampledPendingRequestCount = sampledRequests,
                FrameQueueCount = frames,
                LastInputReportLength = reportLength,
                InputReportCacheObservation = cacheStatus,
                PrimaryInputReportId = reportId,
                PrimaryInputReportSlot = reportSlot,
                LastSteamDeckPadReport = reportCopied ? SteamDeckPadReportObservation.Decode(reportCopy) : null,
                ProducerProgress = progress,
                Endpoints = endpoints
            };
        }
        catch (ShapeMismatch ex) { return Unknown("mismatch", ex.Message); }
        catch (Exception ex) { return Unknown("error", ex.GetType().Name); }
        finally { Volatile.Write(ref _capturing, 0); }
    }

    private Snapshot Unknown(string status, string detail) => new()
    {
        ObserverId = _observerId, Status = status, Detail = detail
        // All state values stay unknown on failure; no guessed zero/false.
    };

    private object? Read(FieldInfo field, object target, string? stage = null)
    {
#if USBIP_OBSERVER_FIXTURE
        _fixtureProbe?.Invoke(stage ?? field.Name);
#endif
        return field.GetValue(target);
    }

    private long DeviceId(object device)
    {
        if (!ReferenceEquals(device, _lastDevice))
        {
            _deviceOrdinal = checked(_deviceOrdinal + 1);
            _lastDevice = device;
        }
        return _deviceOrdinal;
    }

    // Optional metadata around the existing queue/cache capture. Missing fields
    // must not discard that capture. No mapping/handle dereference or SDK calls.
    private ProducerProgressStart BeginProducerProgress(Binding binding, object controller, object device)
    {
        try
        {
            if (_progressController != binding.Controller)
            {
                _progressController = binding.Controller;
                _progressBinding = null;
                try
                {
                    // Validate every optional field before reading any of them.
                    _progressBinding = new ProgressBinding(
                        Field(binding.Controller, "_inputSeqNo", typeof(uint)),
                        Field(binding.Device, "_lastSharedSeqNo", typeof(uint)),
                        Field(binding.Controller, "_idleThread", typeof(Thread)),
                        Field(binding.Device, "_inputWaitEvent", typeof(nint)));
                    _progressBindingDetail = "field-shape-verified";
                }
                catch (ShapeMismatch ex) { _progressBindingDetail = ex.Message; }
            }
            if (_progressBinding is not ProgressBinding fields)
                return new(null, null, null, null, "unsupported", _progressBindingDetail);
            long tick = Environment.TickCount64;
            uint producer = (uint)Read(fields.ProducerSequence, controller)!;
            uint pump = (uint)Read(fields.PumpSequence, device)!;
            return new(fields, tick, producer, pump, "ready", "metadata-only");
        }
        catch (Exception ex) { return new(null, null, null, null, "error", ex.GetType().Name); }
    }

    private ProducerProgressObservation CompleteProducerProgress(ProducerProgressStart start,
        object controller, object device)
    {
        if (start.Fields is not ProgressBinding fields)
            return new() { Status = start.Status, Detail = start.Detail };
        try
        {
            uint producer = (uint)Read(fields.ProducerSequence, controller)!;
            uint pump = (uint)Read(fields.PumpSequence, device)!;
            object? idleValue = Read(fields.IdleThread, controller);
            bool? idleAlive = null;
            string idleObservation = "absent";
            if (idleValue is Thread idle)
            {
                try { idleAlive = idle.IsAlive; idleObservation = "is-alive-metadata"; }
                catch (Exception) { idleObservation = "metadata-error"; }
            }
            bool eventHandlePresent = (nint)Read(fields.InputWaitEvent, device)! != IntPtr.Zero;
            return new()
            {
                Status = "observed", Detail = "sequential-managed-fields",
                BeforeTick = start.Tick, AfterTick = Environment.TickCount64,
                ProducerSharedSequenceBefore = start.ProducerSequence,
                ProducerSharedSequenceAfter = producer,
                PumpAcceptedSharedSequenceBefore = start.PumpSequence,
                PumpAcceptedSharedSequenceAfter = pump,
                SequenceValuesUnchangedAcrossCapture = start.ProducerSequence == producer && start.PumpSequence == pump,
                IdleThreadIsAlive = idleAlive, IdleThreadObservation = idleObservation,
                InputWaitHandlePresent = eventHandlePresent
            };
        }
        catch (Exception ex) { return new() { Status = "error", Detail = ex.GetType().Name }; }
    }

    private sealed record ProgressBinding(FieldInfo ProducerSequence, FieldInfo PumpSequence,
        FieldInfo IdleThread, FieldInfo InputWaitEvent);
    private readonly record struct ProducerProgressStart(ProgressBinding? Fields, long? Tick,
        uint? ProducerSequence, uint? PumpSequence, string Status, string Detail);

    internal sealed record ProducerProgressObservation
    {
        public int Version => 1;
        public string Status { get; init; } = "unknown";
        public string Detail { get; init; } = "unknown";
        public long? BeforeTick { get; init; }
        public long? AfterTick { get; init; }
        public uint? ProducerSharedSequenceBefore { get; init; }
        public uint? ProducerSharedSequenceAfter { get; init; }
        public uint? PumpAcceptedSharedSequenceBefore { get; init; }
        public uint? PumpAcceptedSharedSequenceAfter { get; init; }
        public bool? SequenceValuesUnchangedAcrossCapture { get; init; }
        public bool? IdleThreadIsAlive { get; init; }
        public string IdleThreadObservation { get; init; } = "unknown";
        public bool? InputWaitHandlePresent { get; init; }
        public bool AtomicWithCachedReport => false;
        public bool TransferCompletedProven => false;
        public bool SteamConsumptionProven => false;
        public string Scope => "Sequential nonvolatile managed fields around the existing cache sample: producer sequence is assigned before final mapped commit/event; pump sequence is assigned after a stable copy and before report/cache/queue construction. Neither proves completed publication, queue insertion, socket send, driver completion or Steam receipt. Unchanged visible values do not exclude missed updates or ABA. Idle IsAlive and nonzero event handle are metadata only.";
    }

    private long? ConnectionId(object? connection)
    {
        if (connection is null) { _lastConnection = null; return null; }
        if (!ReferenceEquals(connection, _lastConnection))
        {
            _connectionOrdinal = checked(_connectionOrdinal + 1);
            _lastConnection = connection;
        }
        return _connectionOrdinal;
    }

    // Called only while _hidLock is held. 2.0 caches by report ID, not endpoint
    // index: the primary HID report is the device builder's InputReportId.
    // Never substitute reports[0] or the first populated slot. Capacity bytes
    // beyond _inputReportLength are stale and must not reach the decoder.
    private string CopyPrimaryReport(Binding binding, object device, byte primary,
        Span<byte> copy, out byte? reportId, out int? reportSlot,
        out int? reportLength, out bool copied)
    {
        reportId = null; reportSlot = null; reportLength = null; copied = false;
        if ((primary & 0x80) == 0 || (primary & 0x7f) == 0)
            return "unknown-primary-endpoint";
        object? builder = Read(binding.BuilderField, device);
        if (builder is null) return "unknown-builder-null";
        RequireRuntimeType(builder, binding.BuilderType);
        byte id = (byte)Read(binding.ReportIdField, builder)!;
        bool hasIds = (bool)Read(binding.ReportIdsField, device)!;
        if (hasIds != (id != 0)) return "unknown-report-id-mode";
        var reports = (byte[][]?)Read(binding.ReportsField, device);
        var lengths = (int[]?)Read(binding.ReportLengthsField, device);
        var slots = (int[]?)Read(binding.ReportSlotsField, device);
        if (reports is null || lengths is null || slots is null || slots.Length != 256 ||
            reports.Length == 0 || reports.Length > 256 || lengths.Length != reports.Length)
            return "unknown-cache-array-shape";
        reportId = id;
        int slot = slots[id];
        if ((uint)slot >= (uint)reports.Length) return "unknown-report-slot";
        reportSlot = slot;
        byte[]? bytes = reports[slot];
        int length = lengths[slot];
        if (bytes is null || length <= 0 || length > bytes.Length)
            return "unknown-report-length";
        if (hasIds && bytes[0] != id) return "unknown-cached-report-id";
        reportLength = length;
        if (length == copy.Length)
        {
            bytes.AsSpan(0, length).CopyTo(copy);
            copied = true;
        }
        return "observed-primary-report-cache";
    }

    private Binding Bind(Type controller, Assembly assembly)
    {
        Type handle = ExpectedType(assembly, _contract.Handle);
        Type device = ExpectedType(assembly, _contract.Device);
        Type connection = ExpectedType(assembly, _contract.Connection);
        RequireClass(controller);
        Type builder = ExpectedType(assembly, "HIDMaestro.Internal.HidReportBuilder", requireSealed: false);
        Type frame = assembly.GetType(_contract.Device + "+QueuedFrame", throwOnError: false)
            ?? throw new ShapeMismatch("type-missing:QueuedFrame");
        if (frame.DeclaringType != device || frame.Assembly != assembly ||
            frame.Module.ModuleVersionId != _contract.Mvid || !frame.IsNestedPrivate ||
            !frame.IsValueType || !frame.IsSealed ||
            !frame.IsDefined(typeof(IsReadOnlyAttribute), inherit: false))
            throw new ShapeMismatch("type-shape:QueuedFrame");
        Type frames = typeof(Queue<>).MakeGenericType(frame);
        return new Binding(controller, handle, device, connection, builder, frames,
            Field(controller, "<UsbipHandle>k__BackingField", handle, readOnly: true, generated: true),
            Field(handle, "<Device>k__BackingField", device, readOnly: true, generated: true),
            Field(device, "_vhciPort", typeof(int)),
            Field(device, "_hidLock", typeof(object), readOnly: true),
            Field(device, "_pendingInterruptIn", typeof(Dictionary<byte, Queue<uint>>), readOnly: true),
            Field(device, "_frameQueue", frames, readOnly: true),
            Field(device, "_builder", builder, readOnly: true),
            Field(builder, "<InputReportId>k__BackingField", typeof(byte), generated: true),
            Field(device, "_inputReportIds", typeof(bool), readOnly: true),
            Field(device, "_inputReports", typeof(byte[][]), readOnly: true),
            Field(device, "_inputReportLength", typeof(int[]), readOnly: true),
            Field(device, "_inputSlot", typeof(int[]), readOnly: true),
            Field(device, "_primaryInEndpoint", typeof(byte), readOnly: true),
            Field(device, "_stop", typeof(bool), isVolatile: true),
            Field(device, "_connection", connection),
            Field(device, "_inputThread", typeof(Thread), readOnly: true),
            Field(connection, "_device", device));
    }

    private Type ExpectedType(Assembly assembly, string name, bool requireSealed = true)
    {
        Type type = assembly.GetType(name, throwOnError: false, ignoreCase: false)
            ?? throw new ShapeMismatch("type-missing:" + name);
        if (type.FullName != name || !ReferenceEquals(type.Assembly, assembly) ||
            type.Module.ModuleVersionId != _contract.Mvid)
            throw new ShapeMismatch("type-identity:" + name);
        if (requireSealed) RequireClass(type);
        else if (!type.IsClass || type.IsSealed || type.BaseType != typeof(object))
            throw new ShapeMismatch("type-shape:" + type.Name);
        return type;
    }

    private static void RequireClass(Type type)
    {
        if (!type.IsClass || !type.IsSealed || type.BaseType != typeof(object))
            throw new ShapeMismatch("type-shape:" + type.Name);
    }

    private static void RequireRuntimeType(object value, Type expected)
    {
        if (value.GetType() != expected) throw new ShapeMismatch("runtime-type:" + expected.Name);
    }

    private static FieldInfo Field(Type type, string name, Type valueType,
        bool readOnly = false, bool isVolatile = false, bool generated = false)
    {
        FieldInfo? field = type.GetField(name,
            BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly);
        if (field is null || field.DeclaringType != type || field.FieldType != valueType ||
            !field.IsPrivate || field.IsStatic || field.IsLiteral || field.IsInitOnly != readOnly ||
            field.GetOptionalCustomModifiers().Length != 0)
            throw new ShapeMismatch("field-shape:" + type.Name + "." + name);
        Type[] modifiers = field.GetRequiredCustomModifiers();
        if (isVolatile ? modifiers.Length != 1 || modifiers[0] != typeof(IsVolatile) : modifiers.Length != 0)
            throw new ShapeMismatch("field-volatility:" + type.Name + "." + name);
        // IsDefined inspects attribute metadata; it does not construct attributes.
        if (field.IsDefined(typeof(CompilerGeneratedAttribute), inherit: false) != generated)
            throw new ShapeMismatch("field-generated:" + type.Name + "." + name);
        return field;
    }

    private sealed record Contract(Guid Mvid, string AssemblyName, string Controller,
        string Handle, string Device, string Connection, Assembly? RequiredAssembly = null);
    private sealed record Binding(Type Controller, Type Handle, Type Device, Type Connection,
        Type BuilderType, Type FramesType,
        FieldInfo HandleField, FieldInfo DeviceField, FieldInfo PortField, FieldInfo LockField,
        FieldInfo PendingField, FieldInfo FramesField, FieldInfo BuilderField, FieldInfo ReportIdField,
        FieldInfo ReportIdsField, FieldInfo ReportsField, FieldInfo ReportLengthsField, FieldInfo ReportSlotsField,
        FieldInfo PrimaryField, FieldInfo StopField, FieldInfo OwnerField, FieldInfo ThreadField,
        FieldInfo ConnectionDeviceField);
    private sealed class ShapeMismatch(string detail) : Exception(detail);

    internal readonly record struct Endpoint(byte Address, int PendingCount, uint? OldestSequence);

    // Serialize this DTO in the caller if desired. No live SDK references escape.
    internal sealed record Snapshot
    {
        public string ObserverId { get; init; } = "unknown";
        public string Status { get; init; } = "unknown";
        public string Detail { get; init; } = "unknown";
        public long? SampleOrdinal { get; init; }
        public long? DeviceIdentity { get; init; }
        public long? ConnectionIdentityBefore { get; init; }
        public long? ConnectionIdentityAfter { get; init; }
        public bool? OwnerPresentBefore { get; init; }
        public bool? OwnerPresentAfter { get; init; }
        public string OwnerReferenceObservation { get; init; } = "unknown";
        public string ConnectionDeviceRelationBefore { get; init; } = "unknown";
        public string ConnectionClosedObservation { get; init; } = "unknown";
        public int? VhciPort { get; init; }
        public byte? PrimaryInEndpoint { get; init; }
        public bool? StopFlag { get; init; }
        public bool? InputThreadIsAlive { get; init; }
        public string InputThreadObservation { get; init; } = "unknown";
        public string QueueObservation { get; init; } = "unknown";
        public int? EndpointKeyCount { get; init; }
        public int? EndpointsOmitted { get; init; }
        public long? SampledPendingRequestCount { get; init; }
        public int? FrameQueueCount { get; init; }
        public int? LastInputReportLength { get; init; }
        public string InputReportCacheObservation { get; init; } = "unknown";
        public byte? PrimaryInputReportId { get; init; }
        public int? PrimaryInputReportSlot { get; init; }
        public bool TransferCompletedProven => false;
        public bool SteamConsumptionProven => false;
        public string CachedReportScope => "Primary HID builder report ID cache under _hidLock; not necessarily the latest across other report IDs. SDK initial cache may precede any publication. No transfer or consumer receipt is observed.";
        public SteamDeckPadReportObservation? LastSteamDeckPadReport { get; init; }
        public ProducerProgressObservation? ProducerProgress { get; init; }
        public Endpoint[] Endpoints { get; init; } = Array.Empty<Endpoint>();
    }
}
