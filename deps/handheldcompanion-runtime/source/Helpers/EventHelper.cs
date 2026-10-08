using HandheldCompanion.Controllers;
using System.Threading;
using static HandheldCompanion.Managers.ControllerManager;

namespace HandheldCompanion.Helpers
{
    public static class EventHelper
    {
        private readonly struct InputsUpdatedDispatchState
        {
            public readonly InputsUpdatedEventHandler Handler;
            public readonly ControllerState State;
            public readonly bool IsMapped;

            public InputsUpdatedDispatchState(InputsUpdatedEventHandler handler, ControllerState state, bool isMapped)
            {
                Handler = handler;
                State = state;
                IsMapped = isMapped;
            }
        }

        public static void RaiseInputsUpdatedAsync(InputsUpdatedEventHandler? handlers, ControllerState state, bool isMapped)
        {
            if (handlers is null) return;

            ThreadPool.UnsafeQueueUserWorkItem(
                static s =>
                {
                    try { s.Handler(s.State, s.IsMapped); } catch { }
                },
                new InputsUpdatedDispatchState(handlers, state, isMapped),
                preferLocal: true);
        }
    }

}
