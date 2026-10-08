// ── HcVerbatim: AxisLayoutFlags ─────────────────────────────────────────
// HC verbatim 06c0b954: deps/handheldcompanion-runtime/source/Inputs/AxisLayout.cs
// （AxisLayoutFlags 枚举，:59-70）。仅取枚举，AxisLayout 类非 BuildReport 依赖。
using System;

namespace YeManInputHost.HcVerbatim;

[Serializable]
public enum AxisLayoutFlags : byte
{
    None = 0,
    LeftStick = 1,
    RightStick = 2,
    L2 = 3,
    R2 = 4,
    LeftPad = 5,
    RightPad = 6,
    Gyroscope = 7
}