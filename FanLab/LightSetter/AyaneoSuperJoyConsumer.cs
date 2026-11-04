using System.IO;

// One explicit request owns both fixed-source terminals. No HC factory, fan,
// battery manager, color cache, discovery, retry route or model admission here.
internal sealed record AyaneoSuperJoyRequest(string ModelClass, string Action,
    SuperJoyRgb Left, SuperJoyRgb Right, int Brightness, bool Enabled)
{
    internal static AyaneoSuperJoyRequest Create(string model, string action, string? level,
        string? status, int? r, int? g, int? b, int? r2, int? g2, int? b2, int? brightness)
    {
        if (!Program.IsSuperJoySourceModel(model)) throw new ArgumentException("SUPERJOY_MODEL_UNSUPPORTED");
        if (action is not ("color" or "brightness" or "status" or "off") ||
            (!string.IsNullOrWhiteSpace(level) && !string.Equals(level, "solid", StringComparison.OrdinalIgnoreCase)))
            throw new NotSupportedException("SUPERJOY_SOLID_ONLY");
        if (action == "off") return new(model, action, default, default, 0, false);
        if (brightness is null || r is null || g is null || b is null || r2 is null || g2 is null || b2 is null)
            throw new ArgumentException("SUPERJOY_EXPLICIT_COLORS_AND_BRIGHTNESS_REQUIRED");
        if (action == "status" && !string.Equals(status, "on", StringComparison.OrdinalIgnoreCase) && !string.Equals(status, "off", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("SUPERJOY_STATUS_REQUIRED");
        return new(model, action, new((byte)Math.Clamp(r.Value, 0, 255), (byte)Math.Clamp(g.Value, 0, 255), (byte)Math.Clamp(b.Value, 0, 255)),
            new((byte)Math.Clamp(r2.Value, 0, 255), (byte)Math.Clamp(g2.Value, 0, 255), (byte)Math.Clamp(b2.Value, 0, 255)),
            Math.Clamp(brightness.Value, 0, 100), action == "status" ? string.Equals(status, "on", StringComparison.OrdinalIgnoreCase) : brightness.Value > 0);
    }
}
internal interface IAyaneoCeLightingEc : IDisposable
{
    // True is a completed EC port protocol, NOT a physical lighting readback.
    bool TryWriteByte(byte register, byte value);
}
internal readonly record struct AyaneoSuperJoyApplyResult(bool Completed, int HidWrites, int HidReads,
    int EcWrites, bool HandbackAttempted, bool HandbackCompleted, string Reason);

internal static class AyaneoSuperJoyConsumer
{
    // EC mutex acquisition, all EC calls and disposal stay on this thread.
    // HID internally uses async cancellation; never resume EC work on a pool thread.
    internal static AyaneoSuperJoyApplyResult Apply(SuperJoyIdentity identity, AyaneoSuperJoyRequest request,
        Func<bool> ownerIsCurrent, CancellationToken stop,
        Func<ISuperJoyLightingTransport> openHid, Func<IAyaneoCeLightingEc> openEc)
    {
        if (!identity.MatchesProtocol || identity.ModelClass != request.ModelClass)
            throw new ArgumentException("SUPERJOY_COMBINED_IDENTITY_MISMATCH");
        // Requests can also be constructed by internal callers; revalidate before I/O.
        if (request != AyaneoSuperJoyRequest.Create(request.ModelClass, request.Action, "solid",
            request.Enabled ? "on" : "off", request.Left.R, request.Left.G, request.Left.B,
            request.Right.R, request.Right.G, request.Right.B, request.Brightness))
            throw new ArgumentException("SUPERJOY_COMBINED_REQUEST_INVALID");
        var hidWrites = 0; var hidReads = 0; var ecWrites = 0;
        var holdAttempted = false; var handbackAttempted = false; var handbackCompleted = false;
        var completed = false; var reason = "not-started";
        IAyaneoCeLightingEc? ec = null;
        AyaneoSuperJoyLightingSession? sticks = null;
        try
        {
            Current();
            var transport = openHid();
            try { sticks = new(transport, identity, ownerIsCurrent); }
            catch { transport.Dispose(); throw; }
            Current();
            ec = openEc();
            Current();
            // Even a reported hold failure may have reached the EC. Always attempt
            // exactly one release before returning this helper's native owner debt.
            holdAttempted = true;
            Write(0xBF, 0xFE);
            if (request.Action == "off")
            {
                SetSticks(default, default, 0, false);
                Command(4, 2, 0xC0);
            }
            // Matches fresh CEc brightness -> color -> status. Color-left writes
            // AYA B,R,G; sticks are overridden to SuperJoy on these exact models.
            SetSticks(request.Left, default, request.Brightness, false);
            Command(4, 12, Scale(request.Left.B));
            Command(4, 13, Scale(request.Left.R));
            Command(4, 14, Scale(request.Left.G));
            SetSticks(request.Left, request.Right, request.Brightness, false);
            if (request.Action != "off")
            {
                SetSticks(request.Left, request.Right, request.Brightness, request.Enabled);
                Command(4, 2, request.Enabled ? (byte)0x80 : (byte)0xC0);
            }
            Current();
            completed = true; reason = "protocol-completed-effect-unverified";
        }
        catch (OperationCanceledException) { reason = "owner-or-operation-cancelled"; }
        catch (Exception error) when (error is IOException or ObjectDisposedException or UnauthorizedAccessException)
        { reason = "terminal-failed: " + error.Message; }
        finally
        {
            if (ec is not null && holdAttempted)
            {
                handbackAttempted = true;
                try { ecWrites++; handbackCompleted = ec.TryWriteByte(0xBF, 0); }
                catch (Exception error) when (error is IOException or ObjectDisposedException or UnauthorizedAccessException)
                { handbackCompleted = false; }
                // This sole cleanup write is allowed after revocation. Native must
                // retain the exact child and exclude successors until actual exit.
                if (!handbackCompleted) { completed = false; reason += ";cec-handback-failed"; }
            }
            try { ec?.Dispose(); }
            finally { sticks?.Dispose(); }
        }
        // Release/disposal can race a final native revocation too. Successful
        // cleanup is not permission to report an operation from a retired owner.
        if (completed && (stop.IsCancellationRequested || !ownerIsCurrent()))
        { completed = false; reason = "owner-or-operation-retired-after-handback"; }
        return new(completed, hidWrites, hidReads, ecWrites, handbackAttempted, handbackCompleted, reason);

        void Current()
        {
            stop.ThrowIfCancellationRequested();
            if (!ownerIsCurrent()) throw new OperationCanceledException("SUPERJOY_OWNER_RETIRED", stop);
        }
        void Write(byte register, byte value)
        {
            Current(); ecWrites++;
            if (!ec!.TryWriteByte(register, value)) throw new IOException("CEC_WRITE_NOT_COMPLETED");
        }
        void RawCommand(byte group, byte command, byte argument)
        {
            Write(0x6D, group); Write(0xB1, command); Write(0xB2, argument); Write(0xBF, 0x10);
            if (stop.WaitHandle.WaitOne(5)) stop.ThrowIfCancellationRequested();
            Write(0xBF, 0xFE);
        }
        void Command(byte group, byte command, byte argument)
        {
            RawCommand(group, command, argument);
            if (request.ModelClass == "HandheldCompanion.Devices.AYANEONEXT2") RawCommand(3, 0, 0);
        }
        byte Scale(byte component) => (byte)(int)(((((float)component / 255) * 192) / 100) * (float)request.Brightness);
        void SetSticks(SuperJoyRgb left, SuperJoyRgb right, int brightness, bool enabled)
        {
            Current();
            var result = sticks!.SetSticksAsync(left, right, brightness, enabled, stop).GetAwaiter().GetResult();
            hidWrites += result.Writes; hidReads += result.Reads;
            if (!result.Acknowledged) throw new IOException("SUPERJOY_" + result.Reason);
            Current();
        }
    }
}
