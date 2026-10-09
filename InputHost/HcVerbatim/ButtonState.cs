// ── HcVerbatim: ButtonState ─────────────────────────────────────────────
// HC verbatim 06c0b954: deps/handheldcompanion-runtime/source/Inputs/ButtonState.cs
// 逐行提取（改 namespace）。剥离 Newtonsoft.Json JSON glue（[JsonIgnore]/
// [OnSerializing]/[OnDeserialized]/State 字典）——仅用于 HC 本地 JSON 兼容，
// InputHost 不序列化该状态；核心语义（pressed 数组/索引器/Clone/Equals/
// Contains/AddRange/Overwrite/Inject）逐行保留。
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace YeManInputHost.HcVerbatim;

[Serializable]
public partial class ButtonState : ICloneable, IDisposable
{
    public static readonly ButtonFlags[] AllButtons = Enum.GetValues<ButtonFlags>();

    public static readonly ButtonFlags[] UIButtons = new[] {
        ButtonFlags.LeftPadClickDown, ButtonFlags.LeftPadClickLeft, ButtonFlags.LeftPadClickRight, ButtonFlags.LeftPadClickUp,
        ButtonFlags.RightPadClickDown, ButtonFlags.RightPadClickLeft, ButtonFlags.RightPadClickRight, ButtonFlags.RightPadClickUp,
        ButtonFlags.L2Soft, ButtonFlags.L2Full,
        ButtonFlags.R2Soft, ButtonFlags.R2Full,
        ButtonFlags.LeftStickUp, ButtonFlags.LeftStickDown, ButtonFlags.LeftStickLeft, ButtonFlags.LeftStickRight,
        ButtonFlags.RightStickUp, ButtonFlags.RightStickDown, ButtonFlags.RightStickLeft, ButtonFlags.RightStickRight,
    };

    public static readonly ButtonFlags[] OEMButtons = new[] { ButtonFlags.OEM1, ButtonFlags.OEM2, ButtonFlags.OEM3, ButtonFlags.OEM4, ButtonFlags.OEM5, ButtonFlags.OEM6, ButtonFlags.OEM7, ButtonFlags.OEM8, ButtonFlags.OEM9, ButtonFlags.OEM10 };

    private const int MaxValue = (int)ButtonFlags.Max;
    private const int MaxButton = (int)ButtonFlags.B15;

    private bool[] _pressed = new bool[MaxValue];

    private bool _disposed = false;

    public ButtonState(Dictionary<ButtonFlags, bool> state)
    {
        // initialize array from incoming dictionary
        _pressed = new bool[MaxValue];
        if (state != null)
        {
            for (int i = 0; i < MaxValue; i++)
                _pressed[i] = state.TryGetValue((ButtonFlags)i, out var v) && v;
        }
    }

    public ButtonState()
    {
        _pressed = new bool[MaxValue];
    }

    ~ButtonState() => Dispose(false);

    public bool this[ButtonFlags button]
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            int idx = (int)button;
            return (idx < _pressed.Length && idx >= 0) ? _pressed[idx] : false;
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        set
        {
            int idx = (int)button;
            if (idx < _pressed.Length && idx >= 0)
                _pressed[idx] = value;
        }
    }

    public IEnumerable<ButtonFlags> Buttons
    {
        get
        {
            // snapshot of currently pressed buttons
            List<ButtonFlags> list = new List<ButtonFlags>();
            for (int i = 0; i < MaxValue; i++)
                if (_pressed[i]) list.Add((ButtonFlags)i);
            return list;
        }
    }

    public object Clone()
    {
        var clone = new ButtonState();
        Array.Copy(_pressed, clone._pressed, _pressed.Length);
        return clone;
    }

    public bool IsEmpty()
    {
        for (int i = 0; i < _pressed.Length; i++)
            if (_pressed[i]) return false;
        return true;
    }

    public void Clear()
    {
        Array.Clear(_pressed, 0, _pressed.Length);
    }

    public bool Contains(ButtonState other)
    {
        if (other is null) return false;
        // Equivalent to: for every button, this[b] == other[b]
        for (int i = 0; i < MaxValue; i++)
            if (_pressed[i] != other._pressed[i]) return false;
        return true;
    }

    public bool ContainsTrue(ButtonState other)
    {
        if (other is null) return false;

        bool emptyThis = true, emptyOther = true;
        for (int i = 0; i < _pressed.Length; i++)
        {
            if (_pressed[i]) emptyThis = false;
            if (other._pressed[i]) emptyOther = false;
        }
        if (emptyThis || emptyOther) return false;

        // every true in other must be true in this
        for (int i = 0; i < _pressed.Length; i++)
            if (other._pressed[i] && !_pressed[i]) return false;

        return true;
    }

    public void AddRange(ButtonState other)
    {
        if (other is null) return;
        for (int i = 0; i < _pressed.Length; i++)
            if (other._pressed[i]) _pressed[i] = true;
    }

    public static void Overwrite(ButtonState origin, ButtonState target)
    {
        if (origin is null || target is null) return;
        if (target._pressed.Length != origin._pressed.Length)
            target._pressed = new bool[origin._pressed.Length];

        Array.Copy(origin._pressed, target._pressed, origin._pressed.Length);
    }

    public static void Inject(ButtonState origin, ButtonState target)
    {
        if (origin is null || target is null) return;
        for (int i = 0; i < origin._pressed.Length; i++)
        {
            if (origin._pressed[i] != target._pressed[i])
                target._pressed[i] = true;
        }
    }

    public override bool Equals(object? obj)
    {
        if (obj is not ButtonState other) return false;
        if (ReferenceEquals(this, other)) return true;
        if (other._pressed.Length != _pressed.Length) return false;

        for (int i = 0; i < _pressed.Length; i++)
            if (_pressed[i] != other._pressed[i]) return false;

        return true;
    }

    public override int GetHashCode()
    {
        // simple hash on pressed bits
        int h = 17;
        for (int i = 0; i < _pressed.Length; i++)
            if (_pressed[i]) h = (h * 31) ^ (i + 1);
        return h;
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (_disposed) return;
        if (disposing)
        {
            Array.Clear(_pressed, 0, _pressed.Length);
            _pressed = Array.Empty<bool>();
        }
        _disposed = true;
    }
}