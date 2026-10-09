using HandheldCompanion.Controllers.Steam;
using HandheldCompanion.Inputs;

namespace HandheldCompanion.Controllers.Dummies
{
    public class DummySteamDeckController : NeptuneController
    {
        public override bool IsVirtual() => true;
        public override bool IsDummy() => true;

        public override void Tick(long ticks, float delta, bool commit = false)
        {
            ButtonState.Overwrite(InjectedButtons, Inputs.ButtonState);
        }
    }
}
