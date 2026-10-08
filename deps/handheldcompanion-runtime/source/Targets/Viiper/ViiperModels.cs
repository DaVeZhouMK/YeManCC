using System.Collections.Generic;

namespace HandheldCompanion.Targets.Viiper
{
    internal sealed class ViiperVirtualDevice
    {
        public uint BusId { get; set; }
        public uint DeviceId { get; set; }
        public string TypeName { get; set; } = "xbox360";
        public bool IsActive { get; set; }
    }

    internal enum PhysicalButton
    {
        A,
        B,
        X,
        Y,
        LB,
        RB,
        Back,
        Start,
        LS,
        RS,
        Guide,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        Y1,
        Y2,
        Y3,
        M3,
        M1,
        M2,
        Mode,
        Share,
    }

    internal sealed class ButtonMappingEntry
    {
        public PhysicalButton Physical { get; set; }
        public string VirtualButton { get; set; } = string.Empty;
    }

    internal sealed class ButtonMappingProfile
    {
        public string DeviceType { get; set; } = string.Empty;
        public List<ButtonMappingEntry> Mappings { get; set; } = [];
    }

    internal sealed class ViiperAppSettings
    {
        public string DeviceType { get; set; } = "xbox360";
        public string SteamSubDevice { get; set; } = string.Empty;
        public string InputSource { get; set; } = "XInput";
        public string GyroSource { get; set; } = "Left";
        public string GyroMapX { get; set; } = "X";
        public string GyroMapY { get; set; } = "Y";
        public string GyroMapZ { get; set; } = "Z";
        public string AccelMapX { get; set; } = "X";
        public string AccelMapY { get; set; } = "Y";
        public string AccelMapZ { get; set; } = "Z";
        public bool AutoStartEmulation { get; set; }
        public bool SwapRumbleSides { get; set; }
        public bool InvertRightMotor { get; set; }
        public int RumbleIntensityPercent { get; set; } = 100;
        public Dictionary<string, List<ButtonMappingEntry>> ButtonMappings { get; set; } = [];
    }
}
