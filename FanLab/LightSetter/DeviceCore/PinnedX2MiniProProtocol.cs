namespace YeManCC.Lighting;

// Adapted only from fixed f2e58468 OneXPlayerX2MiniPro and OneXAOKZOE.
// SOURCE-NOTICE.md records exact file/symbol identities. No OXP3 sequence,
// aggregate zone, serial status/preset, controller setup or cached LED state.
internal sealed class PinnedX2MiniProProtocol
{
    private readonly ILightingHidTransport transport;
    private readonly CancellationToken cancellationToken;
    private readonly Action ensureOwnerCurrent;
    internal int WriteAttempts { get; private set; }

    internal PinnedX2MiniProProtocol(ILightingHidTransport transport,
        CancellationToken cancellationToken, Action ensureOwnerCurrent)
    {
        this.transport = transport;
        this.cancellationToken = cancellationToken;
        this.ensureOwnerCurrent = ensureOwnerCurrent;
    }

    internal bool SetLedBrightness(int brightness)
    {
        bool result = true;
        foreach (byte side in new byte[] { 0x01, 0x02, 0x07, 0x05, 0x06 })
        {
            result &= SendZoneBrightness(brightness, side);
            ZoneDelay();
        }
        return result;
    }

    internal bool SetLedColor(RgbColor mainColor, RgbColor secondaryColor, LEDLevel level)
    {
        if (level is not (LEDLevel.SolidColor or LEDLevel.Breathing)) return false;
        bool breathing = level == LEDLevel.Breathing;
        bool result = true;
        foreach (byte side in new byte[] { 0x01, 0x02, 0x07 })
        {
            result &= SendZoneColor(mainColor, side, breathing);
            ZoneDelay();
        }
        foreach (byte side in new byte[] { 0x05, 0x06 })
        {
            result &= SendZoneColor(secondaryColor, side, breathing);
            ZoneDelay();
        }
        return result;
    }

    private bool SendZoneBrightness(int brightness, byte side)
    {
        bool enabled = brightness > 0;
        byte level = !enabled || brightness <= 33 ? (byte)0x01
            : brightness <= 66 ? (byte)0x03 : (byte)0x04;
        return Write(new byte[] { 0xFD, side, 0x02, (byte)(enabled ? 1 : 0), 0x05, level });
    }

    private bool SendZoneColor(RgbColor color, byte side, bool breathing)
    {
        List<byte> command = new() { breathing ? (byte)0xF0 : (byte)0xFE, side, 0x02 };
        for (int i = 0; i < 18; i++)
        {
            command.Add(color.R);
            command.Add(color.G);
            command.Add(color.B);
        }
        command.Add(color.R);
        command.Add(color.G);
        return Write(command);
    }

    private bool Write(IList<byte> command)
    {
        CheckOwner();
        if (!transport.IsConnected) return false;
        // These two existing builders are byte-identical in the fixed newer
        // source. The color/brightness helpers above use the newer semantics.
        var report = PinnedOneXProtocol.WithReportID(PinnedOneXProtocol.BuildV1Command(0xB8, 0x01, command));
        WriteAttempts++;
        var accepted = transport.Write(report);
        CheckOwner();
        return accepted;
    }

    private void CheckOwner()
    {
        cancellationToken.ThrowIfCancellationRequested();
        ensureOwnerCurrent();
    }

    private void ZoneDelay()
    {
        // Keep the source's 200 ms after EVERY zone, including the last.
        // Short slices retain the existing owner's heartbeat/generation checks.
        for (int elapsed = 0; elapsed < 200; elapsed += 20)
        {
            CheckOwner();
            if (cancellationToken.WaitHandle.WaitOne(20)) cancellationToken.ThrowIfCancellationRequested();
        }
        CheckOwner();
    }
}
