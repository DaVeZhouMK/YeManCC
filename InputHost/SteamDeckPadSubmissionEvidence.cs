using System.Buffers.Binary;

namespace YeManInputHost.Diagnostics;

// Single Host command-thread writer. No clock, reflection, I/O or allocation in
// Record; immutable copies are made only in the existing diagnostic window.
internal sealed class SteamDeckPadSubmissionEvidence
{
    // Diagnostic raw-axis reference, not a Steam deadzone or input filter.
    // Three bits per axis: 0=within +/-4096, 1=negative, 2=positive,
    // 3=-32767 full, 4=32767 full, 5=-32768 outside the Host builder domain.
    private static byte StickClass(short value) => value switch
    {
        < -32767 => 5, -32767 => 3, < -4096 => 1,
        <= 4096 => 0, < 32767 => 2, _ => 4
    };
    internal readonly record struct ExpectedPad(bool Active, bool Click, short X, short Y, ushort Pressure);
    internal readonly record struct Row(ulong CommandSequence, ulong SampleSequence, ulong SourceTick,
        uint PacketCounter, ulong ButtonBits, byte TouchMask, byte ClickMask, short LeftX, short LeftY, short RightX, short RightY,
        ushort LeftPressure, ushort RightPressure, byte ExpectedTouchMask, byte ExpectedClickMask, byte Errors,
        bool OtherFieldsNonzero, bool NeutralCommand)
    {
        public short LeftStickX { get; init; }
        public short LeftStickY { get; init; }
        public short RightStickX { get; init; }
        public short RightStickY { get; init; }
        public ushort StickClassBits => (ushort)(StickClass(LeftStickX) | (StickClass(LeftStickY) << 3) |
            (StickClass(RightStickX) << 6) | (StickClass(RightStickY) << 9));
        // Bits 0..9: accel X/Y/Z, gyro pitch/yaw/roll, quaternion W/X/Y/Z.
        // Ring presence only: nonzero magnitude/sign changes are not edges.
        // Scalar raw values are exported with every retained/latest row.
        public ushort ImuNonzeroMask { get; init; }
        public short AccelX { get; init; }
        public short AccelY { get; init; }
        public short AccelZ { get; init; }
        public short GyroPitch { get; init; }
        public short GyroYaw { get; init; }
        public short GyroRoll { get; init; }
        public short QuatW { get; init; }
        public short QuatX { get; init; }
        public short QuatY { get; init; }
        public short QuatZ { get; init; }
        public bool ImuFieldsNonzero => ImuNonzeroMask != 0;
        public byte PressureNonzeroMask => (byte)((LeftPressure != 0 ? 1 : 0) | (RightPressure != 0 ? 2 : 0));
        public byte PressureOutOfRangeMask => (byte)((LeftPressure > 32767 ? 1 : 0) | (RightPressure > 32767 ? 2 : 0));
        // Inactive wire pads with residue: bits 0..3=LX/LY/RX/RY, 4..5=LP/RP.
        // Per-field clears survive another persistent coordinate/pressure error.
        public byte InactivePadDataMask => (byte)(
            ((TouchMask & 1) == 0 ? (LeftX != 0 ? 1 : 0) | (LeftY != 0 ? 2 : 0) | (LeftPressure != 0 ? 16 : 0) : 0) |
            ((TouchMask & 2) == 0 ? (RightX != 0 ? 4 : 0) | (RightY != 0 ? 8 : 0) | (RightPressure != 0 ? 32 : 0) : 0));
        public ushort LeftTrigger { get; init; }
        public ushort RightTrigger { get; init; }
        public byte TriggerNonzeroMask => (byte)((LeftTrigger != 0 ? 1 : 0) | (RightTrigger != 0 ? 2 : 0));
        // Report-domain signature only: encodings of 127/255 and 128/255
        // bracket center. This is neither a fault verdict nor input filtering.
        public bool DualTriggerHalfRange => LeftTrigger is >= 16319 and <= 16448 &&
            RightTrigger is >= 16319 and <= 16448;
    }
    // Raw-coordinate reference only. The threshold is from the reviewed Steam
    // build's registered default; no runtime Steam policy or rejection is observed.
    private const int ReferenceXBoundary = 30500;
    internal readonly record struct GeometryPoint(ulong CommandSequence, ulong SampleSequence, ulong SourceTick,
        uint PacketCounter, short X, short Y, byte ReferenceClass);
    internal sealed record PadGeometrySnapshot(ulong ActiveReports, ulong InnerSideReferenceReports,
        ulong OutsidePadDomainReports, ulong ReferenceClassChanges, int? MinimumX, int? MaximumX,
        int? MinimumY, int? MaximumY, GeometryPoint? FirstActive, GeometryPoint? LastActive,
        GeometryPoint? LastInnerSideReference, GeometryPoint? LastReferenceClassChange);
    internal sealed record RawGeometrySnapshot(int Version, int ReferenceXBoundary,
        PadGeometrySnapshot Left, PadGeometrySnapshot Right)
    {
        public bool RecordingEnabled { get; init; }
        public bool LatestReportValid { get; init; }
        public bool SteamPolicyObserved => false;
        public bool SteamConsumptionProven => false;
        public string Scope => "Enabled successful Host raw API returns only. Inner-side reference is left X>30500 or right X<-30500; pad-domain reference is [-32767,32767]. Registered-default comparisons are not observed Steam policy or rejection. Points retain source sequence/tick; logging gaps, invalid reports and packet discontinuities break class-change comparisons. Historical points persist until target-generation reset.";
    }
    private struct PadGeometry
    {
        private ulong _activeReports, _innerSideReports, _outsideDomainReports, _classChanges;
        private short _minX, _maxX, _minY, _maxY;
        private byte _previousClass;
        private bool _havePreviousActive;
        private GeometryPoint? _first, _last, _lastInnerSide, _lastClassChange;
        internal void BreakContinuity() => _havePreviousActive = false;
        internal void Record(in Row row, bool active, short x, short y, bool left, bool continuous)
        {
            if (!active) { _havePreviousActive = false; return; }
            if (!continuous) _havePreviousActive = false;
            bool innerSide = left ? x > ReferenceXBoundary : x < -ReferenceXBoundary;
            bool outsideDomain = x < -32767 || y < -32767;
            byte referenceClass = (byte)((innerSide ? 1 : 0) | (outsideDomain ? 2 : 0));
            var point = new GeometryPoint(row.CommandSequence, row.SampleSequence, row.SourceTick,
                row.PacketCounter, x, y, referenceClass);
            if (_activeReports++ == 0)
            {
                _minX = _maxX = x; _minY = _maxY = y; _first = point;
            }
            else
            {
                if (x < _minX) _minX = x; else if (x > _maxX) _maxX = x;
                if (y < _minY) _minY = y; else if (y > _maxY) _maxY = y;
            }
            if (innerSide) { ++_innerSideReports; _lastInnerSide = point; }
            if (outsideDomain) ++_outsideDomainReports;
            if (_havePreviousActive && referenceClass != _previousClass)
            { ++_classChanges; _lastClassChange = point; }
            _last = point; _previousClass = referenceClass; _havePreviousActive = true;
        }
        internal readonly PadGeometrySnapshot Capture() => new(_activeReports, _innerSideReports,
            _outsideDomainReports, _classChanges, _activeReports == 0 ? null : (int)_minX,
            _activeReports == 0 ? null : (int)_maxX, _activeReports == 0 ? null : (int)_minY,
            _activeReports == 0 ? null : (int)_maxY, _first, _last, _lastInnerSide, _lastClassChange);
    }
    internal sealed record Snapshot(int Version, long TargetGeneration, ulong Reports, ulong Edges,
        ulong TouchChanges, ulong ClickChanges, ulong InvalidFrames, ulong TouchMismatch,
        ulong ClickMismatch, ulong CoordinateMismatch, ulong PressureMismatch, ulong CounterDiscontinuities,
        ulong EnabledSpans, ulong ObservationErrors, ulong OmittedRows, Row[] Entries)
    {
        public int ReferenceStickDeadzone => 4096;
        public Row? LastSuccessfulReport { get; init; }
        public RawGeometrySnapshot? RawGeometry { get; init; }
        public bool TransferCompletedProven => false;
        public bool SteamConsumptionProven => false;
        public string Scope => "Successful serial Host raw API returns only; digital changes, raw stick reference classes, per-field IMU presence and pad release classes are checked on each enabled report, not sampled at1Hz. Stick reference +/-4096 is diagnostic only; motion inside a class is not a ring edge. Ring omission counts edges; disabled logging and controller changes break continuity. No SDK pump, USB completion or Steam receipt is claimed.";
    }
    private const int Capacity = 32;
    private readonly Row[] _rows = new Row[Capacity];
    private ulong _reports, _edges, _touchChanges, _clickChanges, _invalid, _touchMismatch, _clickMismatch,
        _coordinateMismatch, _pressureMismatch, _counterDiscontinuities, _enabledSpans, _observationErrors;
    private long _generation;
    private bool _havePrevious, _wasEnabled;
    private Row _previous;
    private PadGeometry _leftGeometry, _rightGeometry;

    internal void Reset()
    {
        ++_generation; _reports = _edges = _touchChanges = _clickChanges = _invalid = _touchMismatch =
            _clickMismatch = _coordinateMismatch = _pressureMismatch = _counterDiscontinuities =
            _enabledSpans = _observationErrors = 0;
        _havePrevious = _wasEnabled = false; _previous = default;
        _leftGeometry = _rightGeometry = default;
        // Old entries are outside count and never exported; no array clear work.
    }
    internal void Record(bool enabled, ReadOnlySpan<byte> bytes, ExpectedPad left, ExpectedPad right,
        ulong commandSequence, ulong sampleSequence, ulong sourceTick, bool neutralCommand = false)
    {
        if (!enabled)
        {
            _havePrevious = _wasEnabled = false;
            _leftGeometry.BreakContinuity(); _rightGeometry.BreakContinuity(); return;
        }
        try
        {
            if (!_wasEnabled) { ++_enabledSpans; _wasEnabled = true; }
            ++_reports;
            if (bytes.Length != 64 || bytes[0] != 1 || bytes[1] != 0 || bytes[2] != 9 || bytes[3] != 64)
            {
                ++_invalid; _havePrevious = false;
                _leftGeometry.BreakContinuity(); _rightGeometry.BreakContinuity(); return;
            }
            var bits = BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]);
            var touch = (byte)((bits >> 19) & 3); var click = (byte)((bits >> 17) & 3);
            byte expectedTouch = (byte)((left.Active ? 1 : 0) | (right.Active ? 2 : 0));
            byte expectedClick = (byte)((left.Active && left.Click ? 1 : 0) | (right.Active && right.Click ? 2 : 0));
            short lx = BinaryPrimitives.ReadInt16LittleEndian(bytes[16..]), ly = BinaryPrimitives.ReadInt16LittleEndian(bytes[18..]),
                rx = BinaryPrimitives.ReadInt16LittleEndian(bytes[20..]), ry = BinaryPrimitives.ReadInt16LittleEndian(bytes[22..]);
            ushort lp = BinaryPrimitives.ReadUInt16LittleEndian(bytes[56..]), rp = BinaryPrimitives.ReadUInt16LittleEndian(bytes[58..]);
            uint packet = BinaryPrimitives.ReadUInt32LittleEndian(bytes[4..]); byte errors = 0;
            if (touch != expectedTouch) { ++_touchMismatch; errors |= 1; }
            if (click != expectedClick) { ++_clickMismatch; errors |= 2; }
            if (lx != (left.Active ? left.X : 0) || ly != (left.Active ? left.Y : 0) ||
                rx != (right.Active ? right.X : 0) || ry != (right.Active ? right.Y : 0))
            { ++_coordinateMismatch; errors |= 4; }
            if (lp != (left.Active ? left.Pressure : 0) || rp != (right.Active ? right.Pressure : 0))
            { ++_pressureMismatch; errors |= 8; }
            if (_havePrevious && packet != unchecked(_previous.PacketCounter + 1))
            { ++_counterDiscontinuities; errors |= 16; }
            ushort imuNonzero = 0;
            for (int axis = 0; axis < 10; ++axis)
                if (BinaryPrimitives.ReadInt16LittleEndian(bytes[(24 + axis * 2)..]) != 0)
                    imuNonzero |= (ushort)(1 << axis);
            bool other = (bits & ~(15UL << 17)) != 0 || imuNonzero != 0 || bytes[44..56].ContainsAnyExcept((byte)0);
            var row = new Row(commandSequence, sampleSequence, sourceTick, packet, bits, touch, click, lx, ly, rx, ry, lp, rp,
                expectedTouch, expectedClick, errors, other, neutralCommand)
            {
                LeftTrigger = BinaryPrimitives.ReadUInt16LittleEndian(bytes[44..]),
                RightTrigger = BinaryPrimitives.ReadUInt16LittleEndian(bytes[46..]),
                LeftStickX = BinaryPrimitives.ReadInt16LittleEndian(bytes[48..]),
                LeftStickY = BinaryPrimitives.ReadInt16LittleEndian(bytes[50..]),
                RightStickX = BinaryPrimitives.ReadInt16LittleEndian(bytes[52..]),
                RightStickY = BinaryPrimitives.ReadInt16LittleEndian(bytes[54..]),
                ImuNonzeroMask = imuNonzero,
                AccelX = BinaryPrimitives.ReadInt16LittleEndian(bytes[24..]),
                AccelY = BinaryPrimitives.ReadInt16LittleEndian(bytes[26..]),
                AccelZ = BinaryPrimitives.ReadInt16LittleEndian(bytes[28..]),
                GyroPitch = BinaryPrimitives.ReadInt16LittleEndian(bytes[30..]),
                GyroYaw = BinaryPrimitives.ReadInt16LittleEndian(bytes[32..]),
                GyroRoll = BinaryPrimitives.ReadInt16LittleEndian(bytes[34..]),
                QuatW = BinaryPrimitives.ReadInt16LittleEndian(bytes[36..]),
                QuatX = BinaryPrimitives.ReadInt16LittleEndian(bytes[38..]),
                QuatY = BinaryPrimitives.ReadInt16LittleEndian(bytes[40..]),
                QuatZ = BinaryPrimitives.ReadInt16LittleEndian(bytes[42..])
            };
            bool geometryContinuous = _havePrevious && (errors & 16) == 0;
            _leftGeometry.Record(in row, (touch & 1) != 0, lx, ly, true, geometryContinuous);
            _rightGeometry.Record(in row, (touch & 2) != 0, rx, ry, false, geometryContinuous);
            if (_havePrevious && touch != _previous.TouchMask) ++_touchChanges;
            if (_havePrevious && click != _previous.ClickMask) ++_clickChanges;
            // Compare independent finite classes, so an unrelated held input
            // cannot hide a short pulse/release. Magnitudes within a class only
            // refresh LastSuccessfulReport and the existing pad geometry.
            if (!_havePrevious || bits != _previous.ButtonBits || touch != _previous.TouchMask || click != _previous.ClickMask ||
                row.StickClassBits != _previous.StickClassBits || row.ImuNonzeroMask != _previous.ImuNonzeroMask ||
                row.PressureNonzeroMask != _previous.PressureNonzeroMask || row.PressureOutOfRangeMask != _previous.PressureOutOfRangeMask ||
                row.InactivePadDataMask != _previous.InactivePadDataMask ||
                expectedTouch != _previous.ExpectedTouchMask || expectedClick != _previous.ExpectedClickMask ||
                errors != _previous.Errors || other != _previous.OtherFieldsNonzero || neutralCommand != _previous.NeutralCommand ||
                row.TriggerNonzeroMask != _previous.TriggerNonzeroMask || row.DualTriggerHalfRange != _previous.DualTriggerHalfRange)
                _rows[_edges++ % Capacity] = row;
            _previous = row; _havePrevious = true;
        }
        catch { ++_observationErrors; _havePrevious = false; }
    }
    internal Snapshot Capture()
    {
        int count = (int)Math.Min(_edges, (ulong)Capacity); var rows = new Row[count];
        ulong start = _edges - (ulong)count;
        for (int i = 0; i < count; ++i) rows[i] = _rows[(start + (ulong)i) % Capacity];
        return new(1, _generation, _reports, _edges, _touchChanges, _clickChanges, _invalid,
            _touchMismatch, _clickMismatch, _coordinateMismatch, _pressureMismatch, _counterDiscontinuities,
            _enabledSpans, _observationErrors, start, rows)
        {
            LastSuccessfulReport = _havePrevious ? _previous : null,
            RawGeometry = new(1, ReferenceXBoundary, _leftGeometry.Capture(), _rightGeometry.Capture())
            { RecordingEnabled = _wasEnabled, LatestReportValid = _havePrevious }
        };
    }
}
