// ── HcVerbatim: AxisFlags ───────────────────────────────────────────────
// HC verbatim 06c0b954: deps/handheldcompanion-runtime/source/Inputs/AxisFlags.cs
// 逐行提取（改 namespace，禁改枚举值/序）。
using System;

namespace YeManInputHost.HcVerbatim;

[Serializable]
public enum AxisFlags : byte
{
    None = 0,
    LeftStickX = 1,
    LeftStickY = 2,
    RightStickX = 3,
    RightStickY = 4,
    L2 = 5,
    R2 = 6,

    // Steam Deck
    LeftPadX = 7,
    RightPadX = 8,
    LeftPadY = 9,
    RightPadY = 10,

    GyroX = 11,
    GyroY = 12,

    Max = 13
}