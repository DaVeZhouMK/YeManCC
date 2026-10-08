using HandheldCompanion.Inputs;
using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace HandheldCompanion.Controllers
{
    [Serializable]
    public class ControllerState : ICloneable, IDisposable
    {
        public ButtonState ButtonState = new();
        public AxisState AxisState = new();
        public GyroState GyroState = new();

        [JsonIgnore]
        public static readonly SortedDictionary<AxisLayoutFlags, ButtonFlags> AxisTouchButtons = new()
        {
            { AxisLayoutFlags.RightStick, ButtonFlags.RightStickTouch },
            { AxisLayoutFlags.LeftStick, ButtonFlags.LeftStickTouch },
            { AxisLayoutFlags.RightPad, ButtonFlags.RightPadTouch },
            { AxisLayoutFlags.LeftPad, ButtonFlags.LeftPadTouch },
        };

        private bool _disposed = false; // Prevent multiple disposals

        public ControllerState() { }

        public object Clone()
        {
            return new ControllerState()
            {
                ButtonState = this.ButtonState.Clone() as ButtonState ?? new ButtonState(),
                AxisState = this.AxisState.Clone() as AxisState ?? new AxisState(),
                GyroState = this.GyroState.Clone() as GyroState ?? new GyroState(),
            };
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
                // Free managed resources
                ButtonState = new ButtonState();
                AxisState = new AxisState();
                GyroState = new GyroState();
            }

            _disposed = true;
        }

        ~ControllerState()
        {
            Dispose(false);
        }
    }
}