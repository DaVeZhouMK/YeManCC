// Fixture endpoint only. No production command or actual SDK object is added.
internal static partial class Program
{
    private static readonly List<object> _gameMotorEvents = new();
    private static int _gameMotorFrames;
    private static HMController? _gameOldController;
    private static Action<HMController, HMOutputPacket>? _gameOldCallback;
    private sealed partial class HostSession
    {
        public HMController? GameMotorFixtureController => _controller;
    }
    private static void GameMotorOnDecoded(string command, ulong sequence)
    {
        if (!_managedFault.StartsWith("game-", StringComparison.Ordinal)) return;
        var controller = _managedSession?.GameMotorFixtureController;
        if (command == "QUIESCE" && _managedFault == "game-unbind-old" && controller is not null)
        {
            _gameOldController = controller;
            _gameOldCallback = controller.SnapshotFixtureOutput();
        }
        if (command != "SUBMIT_FRAME") return;
        ++_gameMotorFrames;
        if (_managedFault == "game-unbind-old" && _gameMotorFrames > 1)
        {
            var oldBytes = GameMotorPacket(0xFE, 0xDC);
            _gameOldCallback?.Invoke(_gameOldController!, new HMOutputPacket((HMOutputSource)0, 2, oldBytes, 0x11223346));
            _gameMotorEvents.Add(new { command, sequence, kind = "copied-old-callback", hadCallback = _gameOldCallback is not null });
            return;
        }
        if (controller is null) return;
        bool zero = _managedFault == "game-zero-unbind" && _gameMotorFrames == 2;
        bool replay = _managedFault == "game-replay" && _gameMotorFrames == 2;
        if (_gameMotorFrames != 1 && !zero && !replay) return;
        byte left = zero ? (byte)0 : (byte)0x12;
        byte right = zero ? (byte)0 : (byte)0xA5;
        uint sdkSequence = zero ? 0x11223345u : 0x11223344u;
        var packet = GameMotorPacket(left, right);
        _gameMotorEvents.Add(new { command, sequence, kind = "sdk-event", left, right, sdkSequence, reportId = 2, bodyBytes = packet.Length, payloadBase64 = Convert.ToBase64String(packet) });
        controller.EmitFixtureOutput(0, 2, packet, sdkSequence);
        // The SDK owns a borrowed buffer: consumers must not retain its memory.
        Array.Fill(packet, (byte)0xEE);
    }
    private static byte[] GameMotorPacket(byte left, byte right)
    {
        var bytes = new byte[63];bytes[0] = 3;bytes[2] = right;bytes[3] = left;return bytes;
    }
}
