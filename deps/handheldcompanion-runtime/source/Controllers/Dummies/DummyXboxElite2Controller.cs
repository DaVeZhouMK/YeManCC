using HandheldCompanion.Controllers.SDL;
using HandheldCompanion.Inputs;

namespace HandheldCompanion.Controllers.Dummies
{
    public class DummyXboxElite2Controller : Xbox360Controller
    {
        public override bool IsVirtual() => true;
        public override bool IsDummy() => true;

        public DummyXboxElite2Controller()
        {
            TargetButtons.Add(ButtonFlags.L4);
            TargetButtons.Add(ButtonFlags.R4);
            TargetButtons.Add(ButtonFlags.L5);
            TargetButtons.Add(ButtonFlags.R5);
        }

        public override void Tick(long ticks, float delta, bool commit = false)
        {
            ButtonState.Overwrite(InjectedButtons, Inputs.ButtonState);
        }
    }
}
