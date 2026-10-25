using System;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace YeManInputHost.Diagnostics;

// Raw diagnostics and motor state have separate pending bits. A later LED or
// audio report must not erase a valid motor stop before the next live frame ACK.
internal sealed class HostGameOutputObservation
{
    internal const int MaximumPayloadBytes = 256;
    private const int RawPending = 1, MotorPending = 2;
    private readonly object _gate = new();
    private readonly byte[] _payload = new byte[MaximumPayloadBytes];
    private Binding? _current;
    private ulong _generation;
    private ulong _observed, _coalesced, _oversized;
    private int _pending;
    private byte _source, _reportId;
    private uint _sdkSequence, _motorSdkSequence;
    private int _length;
    private ulong _motorRevision;
    private HostClassicMotorProtocol.Motors _motors;
    internal bool HasPending => Volatile.Read(ref _pending) != 0;

    internal sealed class Binding
    {
        internal readonly object Controller;
        internal readonly ulong Generation;
        internal readonly HostClassicMotorProtocol.Profile Profile;
        internal Binding(object controller, ulong generation, string profileIdentity)
        {
            Controller = controller; Generation = generation;
            Profile = HostClassicMotorProtocol.Bind(profileIdentity);
        }
    }

    internal Binding Bind(object controller, string profileIdentity = "")
    {
        lock (_gate)
        {
            _current = new Binding(controller, ++_generation, profileIdentity);
            _observed = _coalesced = _oversized = _motorRevision = 0;
            _motors = default;
            Volatile.Write(ref _pending, 0);
            _length = 0;
            return _current;
        }
    }

    internal void Revoke()
    {
        lock (_gate)
        {
            _current = null;
            Volatile.Write(ref _pending, 0);
            _length = 0;
            _motors = default;
        }
    }

    internal void Observe(Binding binding, object sender, byte source, byte reportId,
        ReadOnlyMemory<byte> data, uint sdkSequence)
    {
        lock (_gate)
        {
            if (!ReferenceEquals(_current, binding) || !ReferenceEquals(binding.Controller, sender)) return;
            if (data.Length > MaximumPayloadBytes) { ++_oversized; return; }
            if ((_pending & RawPending) != 0) ++_coalesced;
            data.Span.CopyTo(_payload);
            _length = data.Length;
            _source = source;
            _reportId = reportId;
            _sdkSequence = sdkSequence;
            ++_observed;
            int pending = _pending | RawPending;
            if (HostClassicMotorProtocol.Decode(binding.Profile, source, reportId, data.Span, out var motors) ==
                HostClassicMotorProtocol.Result.Motors)
            {
                _motors = motors;
                _motorSdkSequence = sdkSequence;
                ++_motorRevision;
                pending |= MotorPending;
            }
            Volatile.Write(ref _pending, pending);
        }
    }

    private static string AppendFields(string response, string fields) =>
        response[..^1] + (response.Length == 2 ? "" : ",") + fields[1..^1] + "}";

    internal string TryAppendBinding(string response, object controller, int maximumMessageBytes)
    {
        lock (_gate)
        {
            if (_current is null || !ReferenceEquals(_current.Controller, controller) ||
                _current.Profile == HostClassicMotorProtocol.Profile.Unsupported ||
                response.Length == 0 || response[^1] != '}') return response;
            var fields = JsonSerializer.Serialize(new
            {
                gameFeedbackBindingGeneration = _current.Generation,
                gameFeedbackProtocol = (int)_current.Profile
            });
            var candidate = AppendFields(response, fields);
            return Encoding.UTF8.GetByteCount(candidate) <= maximumMessageBytes ? candidate : response;
        }
    }

    internal string TryAppend(string response, object controller, int maximumMessageBytes, bool includeMotors = false)
    {
        int selected = RawPending | (includeMotors ? MotorPending : 0);
        if ((Volatile.Read(ref _pending) & selected) == 0) return response;
        lock (_gate)
        {
            if (_current is null || !ReferenceEquals(_current.Controller, controller) || (_pending & selected) == 0 ||
                response.Length == 0 || response[^1] != '}') return response;
            var candidate = response;
            int delivered = 0;
            if ((_pending & RawPending) != 0)
            {
                var fields = JsonSerializer.Serialize(new
                {
                    gameOutputObservation = new
                    {
                        version = 1,
                        binding = "enclosing-validated-host-response",
                        generation = _current.Generation,
                        observed = _observed,
                        coalesced = _coalesced,
                        oversized = _oversized,
                        source = _source,
                        reportId = _reportId,
                        sdkSequence = _sdkSequence,
                        payloadBytes = _length,
                        payloadBase64 = Convert.ToBase64String(_payload, 0, _length),
                        protocolDecoded = false,
                        gameFeedbackForwardingProven = false,
                        physicalAcceptance = false
                    }
                });
                candidate = AppendFields(candidate, fields);
                delivered |= RawPending;
            }
            if (includeMotors && (_pending & MotorPending) != 0)
            {
                var fields = JsonSerializer.Serialize(new
                {
                    gameFeedbackGeneration = _current.Generation,
                    gameFeedbackRevision = _motorRevision,
                    gameFeedbackLeft = (int)_motors.Left,
                    gameFeedbackRight = (int)_motors.Right,
                    gameFeedbackCompatibility = (int)_motors.Compatibility,
                    gameFeedbackSdkSequence = _motorSdkSequence
                });
                candidate = AppendFields(candidate, fields);
                delivered |= MotorPending;
            }
            if (Encoding.UTF8.GetByteCount(candidate) > maximumMessageBytes) return response;
            Volatile.Write(ref _pending, _pending & ~delivered);
            return candidate;
        }
    }
}

// Protocol facts: locked DualSense Edge USB profile and Linux hid-playstation
// f24ba334afafc70c3149e9db9c0cf8ecc6d52a09. No upstream implementation is copied.
internal static class HostClassicMotorProtocol
{
    internal enum Profile { Unsupported, DualSenseEdgeUsb, SteamDeckClassicRumble, XboxEliteXInputClassic }
    internal enum Result { NotApplicable, Malformed, NoMotorUpdate, UnsupportedHapticMode, Motors }
    internal readonly record struct Motors(byte Left, byte Right, byte Compatibility)
    {
        internal bool IsStop => Left == 0 && Right == 0;
    }

    internal static Profile Bind(string profileIdentity) => global::YeManCC.Capabilities.HcControllerGameFeedbackCapabilityCatalog.ResolveToken(profileIdentity) switch
    {
        1 => Profile.DualSenseEdgeUsb,
        2 => Profile.SteamDeckClassicRumble,
        3 => Profile.XboxEliteXInputClassic,
        _ => Profile.Unsupported
    };

    internal static Result Decode(Profile profile, byte source, byte reportId,
        ReadOnlySpan<byte> data, out Motors motors)
    {
        motors = default;
        if (profile == Profile.XboxEliteXInputClassic)
        {
            if (source != 2 || reportId != 0) return Result.NotApplicable;
            // The locked companion forwards XUSB IOCTL input unchanged.
            // Four bytes are raw XINPUT_VIBRATION WORDs, little-endian.
            if (data.Length == 4)
            {
                motors = new(data[1], data[3], 4);
                return Result.Motors;
            }
            // Packed InSetState: virtual index, LED, left8, right8, flags.
            // Its virtual index never chooses the physical source slot.
            if (data.Length != 5) return Result.Malformed;
            byte flags = data[4];
            if ((flags & ~0x03) != 0) return Result.Malformed;
            if ((flags & 0x02) == 0) return Result.NoMotorUpdate;
            motors = new(data[2], data[3], 4);
            return Result.Motors;
        }
        if (profile == Profile.SteamDeckClassicRumble)
        {
            // The no-report-ID Valve command's first byte is in ReportId.
            // USBIP HidFeature Data starts with length, then packed rumble fields.
            if (source != 1 || reportId != 0xEB) return Result.NotApplicable;
            if (data.Length != 63 || (data[0] != 0 && data[0] != 9)) return Result.Malformed;
            if (data[1] != 0) return Result.UnsupportedHapticMode;
            // Preserve frozen HC's speed/256 adapter. Intensity/gain are not
            // extra amplitudes. Both normalized zeros explicitly settle game rumble.
            motors = new(data[5], data[7], 3);
            return Result.Motors;
        }
        if (profile != Profile.DualSenseEdgeUsb || source != 0 || reportId != 0x02)
            return Result.NotApplicable;
        // HMOutputPacket.ReportId is separate. The locked full report is 64 bytes.
        if (data.Length != 63) return Result.Malformed;
        byte flags0 = data[0];
        bool compatibility1 = (flags0 & 0x01) != 0;
        bool compatibility2 = (data[38] & 0x04) != 0;
        if (!compatibility1 && !compatibility2) return Result.NoMotorUpdate;
        if ((flags0 & 0x02) == 0) return Result.UnsupportedHapticMode;
        // The SDK profile uses full-report byte4 left and byte3 right. Preserve
        // separate amplitudes; zero/zero is an explicit motor stop update.
        motors = new(data[3], data[2], compatibility2 ? (byte)2 : (byte)1);
        return Result.Motors;
    }
}
